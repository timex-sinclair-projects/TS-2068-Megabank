/*
 * Megabank controller firmware (docs/spec.md, "The board", "Registers",
 * "The controller").
 *
 * Core 1 owns the bus: it answers every I/O cycle the `io` PIO program hands
 * it, keeps the registers, and rewrites the two tables the memory path uses
 * (the `be` program's chunk table, the `page` program's page table) whenever
 * a register changes or /RESET goes low. Core 0 runs a USB console for the
 * bench: registers, counters, a log of register writes.
 *
 * Bring-up safety: chunks 0 and 1 are locked by default. The TS-Pico serves
 * HOME and EXROM there with its own /BE, and until its firmware follows the
 * owner table (spec, "Firmware", core 1) both cards would drive the bus. With
 * the lock on, OWN0/OWN1 read back what was written but the board never
 * answers there. `k` on the console turns the lock off.
 *
 * Not yet: BASE and SIZE from flash (fixed at 1 and 64), the battery bit
 * after boot.
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/busctrl.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"

#include "megabank.pio.h"

// ---- pins (hardware/README.md, "GPIO map") -----------------------------------

#define PIN_SD0      0
#define PIN_BA13     16
#define PIN_NMREQ    19
#define PIN_NIORQ    20
#define PIN_NRD      21
#define PIN_NWR      22
#define PIN_NRFSH    23
#define PIN_NM1      24
#define PIN_NRESET   25
#define PIN_NDOE     26
#define PIN_BE_REQ   27
#define PIN_NCE1_0   28
#define PIN_WINH     38
#define PIN_NWP      39
#define PIN_BAT_ADC  40
#define PIN_BAT_EN   41
#define PIN_NMI_REQ  45
#define PIN_NBE_L    42

#define PAGE_PINS    11   // GP28-GP38
#define PAGE_IDLE    0x7ffu

#define VERSION      2
#define SYS_KHZ      250000

// ---- state -------------------------------------------------------------------

static uint8_t own[8], sub[8], wprot, sel;
static uint8_t base = 1, size = 64;
static volatile bool battery_low;
static volatile bool rom_lock = true;
static bool wp_on;

static uint32_t page_table[8] __attribute__((aligned(32)));

static PIO const pio_bus = pio0;    // be, io
static PIO const pio_page = pio1;   // page
static uint sm_be = 0, sm_io = 1, sm_page = 0;
static uint off_be, off_io, off_page;
static int ch_ptr, ch_data;

// Counters and a log of register writes, for the console.
static volatile uint32_t n_io, n_in, n_out, n_drop, n_reset;
#define LOG_N 64
static volatile uint32_t log_buf[LOG_N];  // port << 16 | reg << 8 | value
static volatile uint32_t log_head;

// ---- registers ---------------------------------------------------------------

static inline bool owned(unsigned c) {
    if (rom_lock && c < 2) return false;
    uint8_t o = own[c];
    return o >= base && (unsigned)o < (unsigned)base + size && o != 0xfe && o != 0xff;
}

// Rewrites chunk c in both tables. An I/O write lasts over a microsecond, so
// this is done before the Z80's next memory cycle can use it.
static void __not_in_flash_func(update_chunk)(unsigned c) {
    uint32_t entry = PAGE_IDLE;
    bool ours = owned(c);
    if (ours) {
        unsigned page = (unsigned)(own[c] - base) * 8 + sub[c];  // 0-511
        unsigned chip = page >> 8;
        bool inhibit = (wprot >> c & 1) || wp_on;
        entry = (chip != 0) | (chip != 1) << 1 | (page & 0xff) << 2 | (uint32_t)inhibit << 10;
    }
    page_table[c] = entry;
    pio_bus->instr_mem[off_be + c] =
        pio_encode_jmp(off_be + (ours ? be_offset_ours : be_offset_idle));
}

static void __not_in_flash_func(update_all)(void) {
    for (unsigned c = 0; c < 8; c++) update_chunk(c);
}

static void __not_in_flash_func(regs_reset)(void) {
    for (unsigned c = 0; c < 8; c++) {
        own[c] = 0;
        sub[c] = c;
    }
    wprot = 0;
    sel = 0;
    update_all();
}

static uint8_t __not_in_flash_func(reg_read)(uint8_t r) {
    if (r < 8) return own[r];
    if (r < 16) return sub[r - 8];
    switch (r) {
    case 0x10: return wprot;
    case 0x11: return (wp_on ? 1 : 0) | (battery_low ? 2 : 0);
    case 0x12: return 0xa5;
    case 0x13: return 0x5a;
    case 0x14: return base;
    case 0x15: return size;
    case 0x16: return VERSION;
    default: return 0xff;
    }
}

static void __not_in_flash_func(reg_write)(uint8_t r, uint8_t v) {
    if (r < 8) {
        own[r] = v;
        sub[r] = r;
        update_chunk(r);
    } else if (r < 16) {
        sub[r - 8] = v & 7;
        update_chunk(r - 8);
    } else if (r == 0x10) {
        wprot = v;
        update_all();
    }
}

// ---- core 1: the bus ---------------------------------------------------------

#define W_PORT(w)  (((w) >> 8) & 0xff)
#define W_RD(w)    (!((w) >> PIN_NRD & 1))
#define W_WR(w)    (!((w) >> PIN_NWR & 1))
#define W_M1(w)    (!((w) >> PIN_NM1 & 1))
#define W_IS_REQ(w) ((w) >> PIN_NMREQ & 1)

static inline uint32_t io_get(void) {
    while (pio_sm_is_rx_fifo_empty(pio_bus, sm_io)) tight_loop_contents();
    return pio_bus->rxf[sm_io];
}

static void __not_in_flash_func(log_write)(uint8_t port, uint8_t reg, uint8_t v) {
    log_buf[log_head % LOG_N] = (uint32_t)port << 16 | (uint32_t)reg << 8 | v;
    log_head++;
}

static void __not_in_flash_func(core1_main)(void) {
    multicore_lockout_victim_init();
    const uint32_t h_reply = off_io + io_offset_reply;
    const uint32_t h_capture = off_io + io_offset_capture;
    const uint32_t h_done = off_io + io_offset_done;
    bool in_reset = false;
    bool have = false;
    uint32_t w = 0;
    for (;;) {
        if (!have) {
            // Idle: watch /RESET and the switch between I/O cycles.
            if (!(sio_hw->gpio_in >> PIN_NRESET & 1)) {
                if (!in_reset) {
                    regs_reset();
                    in_reset = true;
                    n_reset++;
                }
            } else {
                in_reset = false;
            }
            bool wp = !(sio_hw->gpio_hi_in >> (PIN_NWP - 32) & 1);
            if (wp != wp_on) {
                wp_on = wp;
                update_all();
            }
            if (pio_sm_is_rx_fifo_empty(pio_bus, sm_io)) continue;
            w = pio_bus->rxf[sm_io];
        }
        have = false;
        if (!W_IS_REQ(w)) continue;  // stray data word: its request was dropped
        n_io++;
        uint8_t port = W_PORT(w);
        bool ours = (port | 1) == 0xfd && !W_M1(w) && (W_RD(w) || W_WR(w));
        if (!ours) {
            pio_bus->txf[sm_io] = h_done;
            continue;
        }
        if (W_RD(w)) {
            uint8_t v = port == 0xfd ? sel : reg_read(sel);
            pio_bus->txf[sm_io] = h_reply | (uint32_t)v << 5;
            n_in++;
            continue;
        }
        pio_bus->txf[sm_io] = h_capture;
        uint32_t d = io_get();
        if (W_IS_REQ(d)) {
            // The capture was dropped (core 1 was late): this is the next
            // cycle's request.
            n_drop++;
            w = d;
            have = true;
            continue;
        }
        uint8_t v = d >> 24;
        n_out++;
        if (port == 0xfd) {
            sel = v;
        } else {
            reg_write(sel, v);
        }
        log_write(port, sel, v);
    }
}

// ---- setup -------------------------------------------------------------------

static void out_pin(uint pin, bool level) {
    gpio_init(pin);
    gpio_disable_pulls(pin);
    gpio_put(pin, level);
    gpio_set_dir(pin, GPIO_OUT);
}

static void board_safe_init(void) {
    out_pin(PIN_NDOE, 1);
    out_pin(PIN_BE_REQ, 0);
    out_pin(PIN_NMI_REQ, 0);
    for (uint p = PIN_NCE1_0; p <= PIN_WINH; p++) out_pin(p, 1);  // SRAMs off, writes inhibited
    out_pin(PIN_BAT_EN, 0);
    for (uint p = PIN_SD0; p <= PIN_NRESET; p++) {
        gpio_init(p);
        gpio_disable_pulls(p);
    }
    for (uint p = 42; p <= 44; p++) {
        gpio_init(p);
        gpio_disable_pulls(p);
    }
    gpio_init(PIN_NWP);
    gpio_pull_up(PIN_NWP);
    adc_init();
    adc_gpio_init(PIN_BAT_ADC);
}

static void bus_start(void) {
    // PIO0: be at offset 0 (its table must be instructions 0-7), io at 16.
    pio_set_gpio_base(pio_bus, 0);
    pio_set_gpio_base(pio_page, 16);
    off_be = pio_add_program_at_offset(pio_bus, &be_program, 0);
    off_io = pio_add_program_at_offset(pio_bus, &io_program, 16);
    off_page = pio_add_program(pio_page, &page_program);
    regs_reset();

    // Idle levels on the PIO's outputs before the pins go over to it.
    pio_sm_set_pins_with_mask(pio_bus, sm_be, 1u << PIN_NDOE, (1u << PIN_NDOE) | (1u << PIN_BE_REQ));
    pio_sm_set_pindirs_with_mask(pio_bus, sm_be, (1u << PIN_NDOE) | (1u << PIN_BE_REQ),
                                 (1u << PIN_NDOE) | (1u << PIN_BE_REQ));
    pio_sm_set_pindirs_with_mask(pio_bus, sm_io, 0, 0xffu << PIN_SD0);
    pio_sm_set_pins_with_mask64(pio_page, sm_page, (uint64_t)PAGE_IDLE << PIN_NCE1_0,
                                (uint64_t)PAGE_IDLE << PIN_NCE1_0);
    pio_sm_set_pindirs_with_mask64(pio_page, sm_page, (uint64_t)PAGE_IDLE << PIN_NCE1_0,
                                   (uint64_t)PAGE_IDLE << PIN_NCE1_0);
    for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) pio_gpio_init(pio_bus, p);
    pio_gpio_init(pio_bus, PIN_NDOE);
    pio_gpio_init(pio_bus, PIN_BE_REQ);
    for (uint p = PIN_NCE1_0; p <= PIN_WINH; p++) pio_gpio_init(pio_page, p);

    pio_sm_config c = be_program_get_default_config(off_be);
    sm_config_set_in_pins(&c, PIN_BA13);
    sm_config_set_jmp_pin(&c, PIN_NRFSH);
    sm_config_set_sideset_pins(&c, PIN_NDOE);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio_bus, sm_be, off_be + be_offset_entry, &c);

    c = io_program_get_default_config(off_io);
    sm_config_set_in_pins(&c, PIN_SD0);
    sm_config_set_out_pins(&c, PIN_SD0, 8);
    sm_config_set_jmp_pin(&c, PIN_NIORQ);
    sm_config_set_sideset_pins(&c, PIN_NDOE);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio_bus, sm_io, off_io + io_offset_top, &c);

    c = page_program_get_default_config(off_page);
    sm_config_set_in_pins(&c, PIN_BA13);
    sm_config_set_jmp_pin(&c, PIN_NRFSH);
    sm_config_set_out_pins(&c, PIN_NCE1_0, PAGE_PINS);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio_page, sm_page, off_page, &c);
    // X = the page table's address >> 5.
    pio_sm_put(pio_page, sm_page, (uint32_t)page_table >> 5);
    pio_sm_exec(pio_page, sm_page, pio_encode_pull(false, true));
    pio_sm_exec(pio_page, sm_page, pio_encode_mov(pio_x, pio_osr));

    // The DMA chain: page's RX FIFO (an entry's address) -> ch_data's read
    // address, which triggers it; ch_data copies the entry to page's TX FIFO.
    ch_ptr = dma_claim_unused_channel(true);
    ch_data = dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config(ch_data);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, false);
    channel_config_set_high_priority(&d, true);
    dma_channel_configure(ch_data, &d, &pio_page->txf[sm_page], NULL, 1, false);
    dma_channel_config p = dma_channel_get_default_config(ch_ptr);
    channel_config_set_transfer_data_size(&p, DMA_SIZE_32);
    channel_config_set_read_increment(&p, false);
    channel_config_set_write_increment(&p, false);
    channel_config_set_dreq(&p, pio_get_dreq(pio_page, sm_page, false));
    channel_config_set_high_priority(&p, true);
    dma_channel_configure(ch_ptr, &p, &dma_hw->ch[ch_data].al3_read_addr_trig, &pio_page->rxf[sm_page],
                          (DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS << DMA_CH0_TRANS_COUNT_MODE_LSB) | 1, true);  // endless needs a non-zero count
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    multicore_launch_core1(core1_main);
    pio_sm_set_enabled(pio_page, sm_page, true);
    pio_set_sm_mask_enabled(pio_bus, (1u << sm_be) | (1u << sm_io), true);
}

// ---- clock --------------------------------------------------------------------

// The flash runs at clk_sys / CLKDIV. At 250 MHz the boot divider would give
// 125 MHz, over the GD25Q128E's rating: go to /3 (83 MHz) first, from RAM.
static void __no_inline_not_in_flash_func(flash_slow)(void) {
    uint32_t t = qmi_hw->m[0].timing;
    t = (t & ~QMI_M0_TIMING_CLKDIV_BITS) | (3u << QMI_M0_TIMING_CLKDIV_LSB);
    t = (t & ~QMI_M0_TIMING_RXDELAY_BITS) | (2u << QMI_M0_TIMING_RXDELAY_LSB);
    qmi_hw->m[0].timing = t;
    (void)qmi_hw->m[0].timing;
}

static void clock_up(void) {
    flash_slow();
    set_sys_clock_khz(SYS_KHZ, true);   // the core runs from the board's 1.2 V LDO
}

// ---- logic analyser (PIO2) -------------------------------------------------------

#define LA_N      4096                  // samples, a 16 KB ring
#define LA_DIV    2                     // a sample every 2 clk_sys cycles
static uint32_t la_buf[LA_N] __attribute__((aligned(LA_N * 4)));
static PIO const pio_la = pio2;
static uint sm_la = 0, sm_trig = 1, off_la, off_trig;
static int ch_la = -1;

// Bits of a sample (GP16 + n).
enum { B_A13 = 0, B_MREQ = 3, B_IORQ, B_RD, B_WR, B_RFSH, B_M1,
       B_DOE = 10, B_BEREQ, B_CE0, B_CE1, B_WINH = 22, B_BE = 26 };
#define S(i, b)  (lin[i] >> (b) & 1)

static void la_init(void) {
    pio_set_gpio_base(pio_la, 16);
    off_la = pio_add_program(pio_la, &la_program);
    off_trig = pio_add_program(pio_la, &trig_program);

    pio_sm_config c = la_program_get_default_config(off_la);
    sm_config_set_in_pins(&c, 16);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv_int_frac8(&c, LA_DIV, 0);
    pio_sm_init(pio_la, sm_la, off_la, &c);

    c = trig_program_get_default_config(off_trig);
    sm_config_set_in_pins(&c, 16);
    sm_config_set_jmp_pin(&c, PIN_NWR);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio_la, sm_trig, off_trig, &c);

    ch_la = dma_claim_unused_channel(true);
}

static void la_arm(void) {
    pio_set_sm_mask_enabled(pio_la, (1u << sm_la) | (1u << sm_trig), false);
    dma_channel_abort(ch_la);
    pio_sm_clear_fifos(pio_la, sm_la);
    pio_sm_restart(pio_la, sm_la);
    pio_sm_restart(pio_la, sm_trig);
    pio_sm_exec(pio_la, sm_trig, pio_encode_jmp(off_trig + trig_offset_top));
    pio_la->fdebug = (1u << (PIO_FDEBUG_RXSTALL_LSB + sm_la));
    dma_channel_config d = dma_channel_get_default_config(ch_la);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_ring(&d, true, 14);
    channel_config_set_dreq(&d, pio_get_dreq(pio_la, sm_la, false));
    dma_channel_configure(ch_la, &d, la_buf, &pio_la->rxf[sm_la],
                          (DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS << DMA_CH0_TRANS_COUNT_MODE_LSB) | 1, true);
    pio_set_sm_mask_enabled(pio_la, (1u << sm_la) | (1u << sm_trig), true);
}

// Stops the ring; returns the index of the oldest sample.
static uint la_stop(void) {
    pio_sm_set_enabled(pio_la, sm_la, false);
    pio_sm_set_enabled(pio_la, sm_trig, false);
    while (!pio_sm_is_rx_fifo_empty(pio_la, sm_la)) tight_loop_contents();
    busy_wait_us_32(2);
    uint idx = ((uintptr_t)dma_hw->ch[ch_la].write_addr - (uintptr_t)la_buf) / 4 % LA_N;
    dma_channel_abort(ch_la);
    return idx;
}

static uint32_t lin[LA_N];

static void la_rows(int from, int to, int t0, uint ns_x10) {
    static const struct { const char *name; uint8_t bit; } rows[] = {
        {"/MREQ", B_MREQ}, {"/RD", B_RD}, {"/WR", B_WR}, {"/RFSH", B_RFSH}, {"/M1", B_M1},
        {"BE_REQ", B_BEREQ}, {"/BE bus", B_BE}, {"nDOE", B_DOE},
        {"/CE1_0", B_CE0}, {"/CE1_1", B_CE1}, {"WINH", B_WINH},
    };
    if (from < 0) from = 0;
    if (to > LA_N) to = LA_N;
    printf("  one column = 2 samples = %u.%u ns; | marks /MREQ falling\n", 2 * ns_x10 / 10, 2 * ns_x10 % 10);
    for (unsigned r = 0; r < sizeof rows / sizeof rows[0]; r++) {
        printf("  %-8s", rows[r].name);
        for (int i = from; i + 1 < to; i += 2) {
            int a = S(i, rows[r].bit), b = S(i + 1, rows[r].bit);
            putchar(i == t0 || i + 1 == t0 ? '|' : a != b ? 'x' : a ? '-' : '_');
        }
        putchar('\n');
    }
    // The edges, in ns from /MREQ falling.
    for (unsigned r = 0; r < sizeof rows / sizeof rows[0]; r++) {
        printf("  %-8s %s", rows[r].name, S(from, rows[r].bit) ? "hi" : "lo");
        for (int i = from + 1; i < to; i++)
            if (S(i, rows[r].bit) != S(i - 1, rows[r].bit))
                printf("  %s%+d", S(i, rows[r].bit) ? "^" : "v", (int)((i - t0) * (int)ns_x10) / 10);
        putchar('\n');
    }
}

// The memory cycle around sample i: /MREQ's fall before it and rise after.
static bool la_cycle(int i, int *f, int *r) {
    int a = i, b = i;
    while (a > 0 && !S(a - 1, B_MREQ)) a--;
    while (b < LA_N - 1 && !S(b + 1, B_MREQ)) b++;
    if (a == 0 || b == LA_N - 1) return false;
    *f = a; *r = b + 1;
    return true;
}

static void la_capture(void) {
    if (ch_la < 0) la_init();
    uint ns_x10 = 10000000u * LA_DIV / (clock_get_hz(clk_sys) / 1000);   // per sample, x10
    printf("\nCapturing: waiting up to 10 s for a write to a mapped chunk...\n");
    absolute_time_t until = make_timeout_time_ms(10000);
    while (!time_reached(until)) {
        la_arm();
        busy_wait_us_32(100);           // fill the ring once
        pio_interrupt_clear(pio_la, 0);
        uint32_t irq = save_and_disable_interrupts();
        absolute_time_t slice = make_timeout_time_ms(20);
        bool hit = false;
        while (!time_reached(slice))
            if (pio_la->irq & 1) { hit = true; break; }
        if (!hit) { restore_interrupts(irq); continue; }
        busy_wait_us_32(8);             // keep ~8 us after the trigger
        uint old = la_stop();
        restore_interrupts(irq);
        bool stall = pio_la->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + sm_la));
        for (uint i = 0; i < LA_N; i++) lin[i] = la_buf[(old + i) % LA_N];

        // The last mapped write in the ring.
        int t = -1;
        for (int i = LA_N - 1; i >= 0; i--)
            if (S(i, B_BEREQ) && !S(i, B_WR)) { t = i; break; }
        int f, r;
        if (t < 0 || !la_cycle(t, &f, &r)) continue;
        printf("Mapped write, chunk %lu (%s):\n", (unsigned long)(lin[t] & 7),
               stall ? "SAMPLES DROPPED" : "no samples dropped");
        la_rows(f - 24, r + 24, f, ns_x10);

        // The nearest mapped read before it, for comparison.
        for (int i = f - 1; i >= 0; i--)
            if (S(i, B_BEREQ) && !S(i, B_RD) && S(i, B_WR)) {
                int f2, r2;
                if (la_cycle(i, &f2, &r2)) {
                    printf("Mapped read before it, chunk %lu:\n", (unsigned long)(lin[i] & 7));
                    la_rows(f2 - 24, r2 + 24, f2, ns_x10);
                }
                break;
            }
        return;
    }
    printf("No mapped write seen. Map a chunk (OUT 253,5: OUT 252,1) and POKE into it in a loop.\n");
}

// ---- core 0: battery and console ----------------------------------------------

static float battery_volts(void) {
    gpio_put(PIN_BAT_EN, 1);
    sleep_ms(25);
    adc_select_input(0);
    uint32_t acc = 0;
    for (int i = 0; i < 64; i++) acc += adc_read();
    gpio_put(PIN_BAT_EN, 0);
    return acc / 64.0f * 3.3f / 4096.0f * 2;
}

static void show_regs(void) {
    printf("\nOWN :");
    for (int c = 0; c < 8; c++) printf(" %3u", own[c]);
    printf("\nSUB :");
    for (int c = 0; c < 8; c++) printf(" %3u", sub[c]);
    printf("\nours:");
    for (int c = 0; c < 8; c++) printf("   %c", owned(c) ? '*' : '.');
    printf("\nWPROT %02x  STATUS %02x  sel %02x  BASE %u SIZE %u  chunks 0-1 %s\n", wprot, reg_read(0x11),
           sel, base, size, rom_lock ? "LOCKED" : "unlocked");
    printf("I/O cycles %lu: IN FCh/FDh %lu, OUT %lu, dropped %lu; resets %lu\n", n_io, n_in, n_out, n_drop,
           n_reset);
}

static void show_log(void) {
    uint32_t head = log_head, n = head < LOG_N ? head : LOG_N;
    printf("\nLast %lu register writes (oldest first):\n", n);
    for (uint32_t i = head - n; i != head; i++) {
        uint32_t e = log_buf[i % LOG_N];
        if ((e >> 16) == 0xfd)
            printf("  OUT FDh %02lx  (select)\n", e & 0xff);
        else
            printf("  OUT FCh %02lx  -> reg %02lx\n", e & 0xff, (e >> 8) & 0xff);
    }
}

static void show_diag(void) {
    printf("\npage SM: pc %lu (offset %u), rx level %lu, tx level %lu, X-table %08lx\n",
           (unsigned long)pio_sm_get_pc(pio_page, sm_page), off_page,
           (unsigned long)pio_sm_get_rx_fifo_level(pio_page, sm_page),
           (unsigned long)pio_sm_get_tx_fifo_level(pio_page, sm_page), (unsigned long)(uint32_t)page_table);
    printf("be SM: pc %lu; io SM: pc %lu\n", (unsigned long)pio_sm_get_pc(pio_bus, sm_be),
           (unsigned long)pio_sm_get_pc(pio_bus, sm_io));
    printf("dma ptr: ctrl %08lx count %08lx read %08lx write %08lx\n", dma_hw->ch[ch_ptr].ctrl_trig,
           dma_hw->ch[ch_ptr].transfer_count, dma_hw->ch[ch_ptr].read_addr, dma_hw->ch[ch_ptr].write_addr);
    printf("dma data: ctrl %08lx count %08lx read %08lx write %08lx\n", dma_hw->ch[ch_data].ctrl_trig,
           dma_hw->ch[ch_data].transfer_count, dma_hw->ch[ch_data].read_addr, dma_hw->ch[ch_data].write_addr);
    printf("page table:");
    for (int c = 0; c < 8; c++) printf(" %03lx", page_table[c]);
    uint64_t in = gpio_get_all64();
    printf("\nGP28-38 now %03llx, GP26/27 %llu%llu, funcsel GP28 %u GP32 %u\n", (in >> 28) & 0x7ff,
           (in >> 26) & 1, (in >> 27) & 1, gpio_get_function(28), gpio_get_function(32));
    // Sample GP28-38 for 50 ms: which bits ever went low?
    uint64_t low = 0;
    absolute_time_t end = make_timeout_time_ms(50);
    while (!time_reached(end)) low |= ~gpio_get_all64();
    printf("GP28-38 seen low in 50 ms: %03llx; nDOE %s, BE_REQ %s\n", (low >> 28) & 0x7ff,
           (low >> 26 & 1) ? "went low" : "stayed high", (low >> 27 & 1) ? "was low" : "never low");
}

static void help(void) {
    printf("\nMegabank controller v%d, clk_sys %lu MHz. Commands:\n"
           "  r   registers and counters\n"
           "  l   log of register writes\n"
           "  d   diagnostics: page SM, DMA, page pins\n"
           "  mCB OWN of chunk C = bank B (digits)\n"
           "  v   battery\n"
           "  c   capture a mapped write (and a read): /MREQ /RD /WR /BE ... timing\n"
           "  k   toggle the chunk 0-1 lock (the TS-Pico serves the ROM there)\n"
           "  u   reboot to BOOTSEL\n",
           VERSION, (unsigned long)(clock_get_hz(clk_sys) / 1000000));
}

int main(void) {
    board_safe_init();
    clock_up();
    stdio_init_all();
    float v = battery_volts();
    battery_low = v < 2.5f;
    bus_start();

    for (;;) {
        int ch = getchar_timeout_us(100 * 1000);
        if (ch == PICO_ERROR_TIMEOUT) continue;
        switch (ch) {
        case 'r': show_regs(); break;
        case 'l': show_log(); break;
        case 'd': show_diag(); break;
        case 'c': la_capture(); break;
        case 'm': {
            // m<c><b>: OWN of chunk c = bank b (digits), from the console
            int c = getchar_timeout_us(2000000) - '0', b = getchar_timeout_us(2000000) - '0';
            if (c >= 0 && c < 8 && b >= 0 && b < 10) { own[c] = b; sub[c] = c; update_chunk(c); }
            show_regs();
            break;
        }
        case 'v': printf("\nbattery %.3f V\n", battery_volts()); break;
        case 'k':
            rom_lock = !rom_lock;
            // update_all on core 1's side of things: it only rewrites tables,
            // and a chunk 0-1 change can't land mid-cycle in a useful way.
            update_all();
            printf("\nchunks 0-1 %s\n", rom_lock ? "LOCKED" : "unlocked");
            break;
        case 'u':
            printf("\nRebooting to BOOTSEL.\n");
            sleep_ms(100);
            reset_usb_boot(0, 0);
            break;
        case '\r':
        case '\n':
            break;
        default: help(); break;
        }
    }
}
