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
 * The chunk 0-1 lock: the TS-Pico serves HOME and EXROM in chunks 0 and 1
 * with its own /BE, and a card whose firmware doesn't follow the owner table
 * (spec, "Firmware", core 1; TS-Pico main before d5be583) would drive the bus
 * with this board. With the lock on, OWN0/OWN1 read back what was written
 * but the board never answers there. It is off at start-up (VERSION 4 on):
 * the TS-Pico's Megabank ROM does its board I/O through chunk 0. `k` on the
 * console turns it on, until the next restart, for a card on old firmware.
 *
 * Settings: BASE and SIZE live in the last sector of the controller's flash
 * (spec, "The controller"; open question 11), set from the console with `S`.
 * With nothing valid there they are 1 and 64. The battery is measured at boot
 * and every hour after (spec, "Battery monitor"), and on `v`.
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
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

#define VERSION      4
#define SYS_KHZ      250000

// ---- state -------------------------------------------------------------------

static uint8_t own[8], sub[8], wprot, sel;
static volatile uint8_t base = 1, size = 64;
static bool settings_from_flash;   // false: the defaults
static volatile bool battery_low;
static volatile bool rom_lock = false;   // `k`: the chunk 0-1 lock, off at start-up
static bool wp_on;

static uint32_t page_table[8] __attribute__((aligned(32)));

static PIO const pio_bus = pio0;    // be, io
static PIO const pio_page = pio1;   // page
static uint sm_be = 0, sm_io = 1, sm_page = 0;
static uint off_be, off_io, off_page;
static int ch_ptr, ch_data;

// Counters and a log of register writes, for the console.
static volatile uint32_t n_io, n_in, n_out, n_drop, n_reset, n_unstick;
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
    const uint32_t pc_answer = off_io + io_offset_answer;
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
            if (pio_sm_is_rx_fifo_empty(pio_bus, sm_io)) {
                // `io` waiting for an answer with nothing in either FIFO: its
                // request was taken for stray data and never answered (with
                // the 2068 off every pin reads low, /MREQ too). Free it.
                if (pio_sm_get_pc(pio_bus, sm_io) == pc_answer
                    && pio_sm_is_tx_fifo_empty(pio_bus, sm_io)
                    && pio_sm_is_rx_fifo_empty(pio_bus, sm_io)) {
                    pio_bus->txf[sm_io] = h_done;
                    n_unstick++;
                }
                continue;
            }
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

// Two windows (PIO2's GPIO base): 16 sees GP16-GP47 (WINH, the bus's /BE);
// 0 sees GP0-GP31 (SD0-SD7, BA0-BA7). `trig` reads relative to in_base GP16
// in both, so pin 11 is always BE_REQ.
#define LA_N      4096                  // samples, a 16 KB ring
#define LA_DIV    2                     // a sample every 2 clk_sys cycles
static uint32_t la_buf[LA_N] __attribute__((aligned(LA_N * 4)));
static PIO const pio_la = pio2;
static uint sm_la = 0, sm_trig = 1, off_la, off_trig;
static int ch_la = -1;
static uint la_sh;                      // the window's first GPIO

static uint32_t lin[LA_N];
static inline int S(int i, uint g) {
    return g >= la_sh && g < la_sh + 32 ? (int)(lin[i] >> (g - la_sh) & 1) : 1;
}

static void la_setup(uint window, uint trig_pin) {
    pio_set_sm_mask_enabled(pio_la, (1u << sm_la) | (1u << sm_trig), false);
    // The SDK changes a PIO's GPIO base only with no programs loaded (it
    // returns an error otherwise, and the window stays where it was: before
    // this, GP32-47 were never sampled, and their rows showed GP0-15).
    static bool loaded = false;
    if (!loaded || pio_get_gpio_base(pio_la) != window) {
        if (loaded) {
            pio_remove_program(pio_la, &la_program, off_la);
            pio_remove_program(pio_la, &trig_program, off_trig);
        }
        int rc = pio_set_gpio_base(pio_la, window);
        hard_assert(rc == PICO_OK);
        off_la = pio_add_program(pio_la, &la_program);
        off_trig = pio_add_program(pio_la, &trig_program);
        loaded = true;
    }
    if (ch_la < 0) ch_la = dma_claim_unused_channel(true);
    la_sh = window;

    pio_sm_config c = la_program_get_default_config(off_la);
    sm_config_set_in_pins(&c, window);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv_int_frac8(&c, LA_DIV, 0);
    pio_sm_init(pio_la, sm_la, off_la, &c);

    c = trig_program_get_default_config(off_trig);
    sm_config_set_in_pins(&c, 16);
    sm_config_set_jmp_pin(&c, trig_pin);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio_la, sm_trig, off_trig + trig_offset_top, &c);
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

static void la_rows(int from, int to, int t0, uint ns_x10) {
    static const struct { const char *name; uint8_t gpio; } rows[] = {
        {"/MREQ", PIN_NMREQ}, {"/RD", PIN_NRD}, {"/WR", PIN_NWR}, {"/RFSH", PIN_NRFSH}, {"/M1", PIN_NM1},
        {"BE_REQ", PIN_BE_REQ}, {"/BE bus", PIN_NBE_L}, {"nDOE", PIN_NDOE},
        {"/CE1_0", PIN_NCE1_0}, {"/CE1_1", PIN_NCE1_0 + 1}, {"WINH", PIN_WINH},
    };
    const unsigned nrows = sizeof rows / sizeof rows[0];
    if (from < 0) from = 0;
    if (to > LA_N) to = LA_N;
    printf("  one column = 2 samples = %u.%u ns; | marks /MREQ falling\n", 2 * ns_x10 / 10, 2 * ns_x10 % 10);
    for (unsigned r = 0; r < nrows; r++) {
        uint g = rows[r].gpio;
        if (g < la_sh || g >= la_sh + 32) continue;
        printf("  %-8s", rows[r].name);
        for (int i = from; i + 1 < to; i += 2) {
            int x = S(i, g), y = S(i + 1, g);
            putchar(i == t0 || i + 1 == t0 ? '|' : x != y ? 'x' : x ? '-' : '_');
        }
        putchar('\n');
    }
    // The edges, in ns from /MREQ falling.
    for (unsigned r = 0; r < nrows; r++) {
        uint g = rows[r].gpio;
        if (g < la_sh || g >= la_sh + 32) continue;
        printf("  %-8s %s", rows[r].name, S(from, g) ? "hi" : "lo");
        for (int i = from + 1; i < to; i++)
            if (S(i, g) != S(i - 1, g))
                printf("  %s%+d", S(i, g) ? "^" : "v", (int)((i - t0) * (int)ns_x10) / 10);
        putchar('\n');
    }
    if (la_sh == 0) {
        // SD0-SD7 (the SRAM's side of U3) and BA0-BA7, each time they change.
        printf("  SD       %02lx", (unsigned long)(lin[from] & 0xff));
        for (int i = from + 1; i < to; i++)
            if ((lin[i] ^ lin[i - 1]) & 0xff)
                printf("  %02lx@%+d", (unsigned long)(lin[i] & 0xff), (int)((i - t0) * (int)ns_x10) / 10);
        printf("\n  A7-A0    %02lx at /MREQ falling\n", (unsigned long)(lin[t0] >> 8 & 0xff));
    }
}

// The memory cycle around sample i: /MREQ's fall before it and rise after.
static bool la_cycle(int i, int *f, int *r) {
    int a = i, b = i;
    while (a > 0 && !S(a - 1, PIN_NMREQ)) a--;
    while (b < LA_N - 1 && !S(b + 1, PIN_NMREQ)) b++;
    if (a == 0 || b == LA_N - 1) return false;
    *f = a; *r = b + 1;
    return true;
}

static inline bool la_is_write(int i) { return S(i, PIN_BE_REQ) && !S(i, PIN_NWR); }
static inline bool la_is_read(int i) { return S(i, PIN_BE_REQ) && !S(i, PIN_NRD) && S(i, PIN_NM1); }

// write: trigger on a mapped write, window GP16-47; else a mapped read
// (not an opcode fetch), window GP0-31 with the data lines.
static void la_capture(bool write) {
    la_setup(write ? 16 : 0, write ? PIN_NWR : PIN_NRD);
    uint ns_x10 = 10000000u * LA_DIV / (clock_get_hz(clk_sys) / 1000);   // per sample, x10
    printf("\nCapturing: waiting up to 10 s for a %s a mapped chunk...\n", write ? "write to" : "read from");
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

        int t = -1;
        for (int i = LA_N - 1; i >= 0; i--)
            if (write ? la_is_write(i) : la_is_read(i)) { t = i; break; }
        int f, r;
        if (t < 0 || !la_cycle(t, &f, &r)) continue;
        uint chunk = lin[f] >> (PIN_BA13 - la_sh) & 7;
        printf("Mapped %s, chunk %u (%s):\n", write ? "write" : "read", chunk,
               stall ? "SAMPLES DROPPED" : "no samples dropped");
        la_rows(f - 24, r + 24, f, ns_x10);

        if (write)                      // the nearest mapped read before it, for comparison
            for (int i = f - 1; i >= 0; i--)
                if (la_is_read(i)) {
                    int f2, r2;
                    if (la_cycle(i, &f2, &r2)) {
                        printf("Mapped read before it:\n");
                        la_rows(f2 - 24, r2 + 24, f2, ns_x10);
                    }
                    break;
                }
        return;
    }
    printf("None seen. Map a chunk (OUT 253,5: OUT 252,1) and %s it in a loop.\n", write ? "POKE into" : "PEEK");
}

// A long window around an opcode fetch from a mapped chunk: the fetch, its
// refresh and the cycles after, about 2.2 us, with every edge.
// owned false: any opcode fetch, to see /BE with nothing of the board's.
static void la_fetch(bool owned) {
    la_setup(16, PIN_NMREQ);            // trig: any cycle with BE_REQ high
    uint ns_x10 = 10000000u * LA_DIV / (clock_get_hz(clk_sys) / 1000);
    printf("\nCapturing: waiting up to 60 s for an opcode fetch%s...\n",
           owned ? " from a mapped chunk" : "");
    absolute_time_t until = make_timeout_time_ms(60000);
    while (!time_reached(until)) {
        la_arm();
        busy_wait_us_32(40);
        pio_interrupt_clear(pio_la, 0);
        uint32_t irq = save_and_disable_interrupts();
        absolute_time_t slice = make_timeout_time_ms(20);
        bool hit = !owned;
        while (!hit && !time_reached(slice))
            if (pio_la->irq & 1) { hit = true; break; }
        if (!hit) { restore_interrupts(irq); continue; }
        busy_wait_us_32(24);
        uint old = la_stop();
        restore_interrupts(irq);
        for (uint i = 0; i < LA_N; i++) lin[i] = la_buf[(old + i) % LA_N];
        // The first (owned) fetch with room around it.
        for (int i = 400; i < LA_N - 400; i++) {
            if (!((!owned || S(i, PIN_BE_REQ)) && !S(i, PIN_NM1) && !S(i, PIN_NMREQ))) continue;
            int f, r;
            if (!la_cycle(i, &f, &r)) break;
            printf("Opcode fetch from chunk %u, the cycles after it:\n",
                   (unsigned)(lin[f] >> (PIN_BA13 - la_sh) & 7));
            la_rows(f - 24, f + 256, f, ns_x10);
            return;
        }
    }
    printf("None seen. Run code from a mapped chunk (basic/mbloop.bas).\n");
}

// Which cycles find the bus's /BE low, by type and chunk, over many untriggered
// captures: low as /MREQ falls (before the board could act: BE_REQ follows at
// about +40 ns), low at any point, and how many the board owned.
static void be_by_chunk(void) {
    static uint32_t n[4][8], at_fall[4][8], any[4][8], own[4][8];
    static const char *ty[4] = {"fetch", "read", "write", "refresh"};
    memset(n, 0, sizeof n); memset(at_fall, 0, sizeof at_fall);
    memset(any, 0, sizeof any); memset(own, 0, sizeof own);
    la_setup(16, PIN_NMREQ);
    printf("\nSampling the bus for 3 s...\n");
    absolute_time_t until = make_timeout_time_ms(3000);
    uint caps = 0;
    while (!time_reached(until)) {
        la_arm();
        busy_wait_us_32(40);
        uint old = la_stop();
        caps++;
        for (uint i = 0; i < LA_N; i++) lin[i] = la_buf[(old + i) % LA_N];
        for (int i = 1; i < LA_N - 8; i++) {
            if (!(S(i - 1, PIN_NMREQ) && !S(i, PIN_NMREQ))) continue;
            int r = i;
            while (r < LA_N && !S(r, PIN_NMREQ)) r++;
            if (r >= LA_N) break;
            int t;
            if (!S(i + 4, PIN_NRFSH)) t = 3;
            else if (!S(i + 4, PIN_NM1)) t = 0;
            else t = 1;
            bool b = false, o = false;
            for (int k = i; k < r; k++) {
                if (!S(k, PIN_NWR) && t == 1) t = 2;
                if (!S(k, PIN_NBE_L)) b = true;
                if (S(k, PIN_BE_REQ)) o = true;
            }
            uint c = lin[i + 4] >> (PIN_BA13 - la_sh) & 7;
            n[t][c]++;
            if (!S(i, PIN_NBE_L)) at_fall[t][c]++;
            if (b) any[t][c]++;
            if (o) own[t][c]++;
            i = r;
        }
    }
    printf("%u captures. Per chunk: cycles / /BE low as /MREQ falls / /BE low at all / owned\n", caps);
    for (int t = 0; t < 4; t++) {
        printf("  %-7s", ty[t]);
        for (int c = 0; c < 8; c++)
            if (n[t][c]) printf("  %d: %lu/%lu/%lu/%lu", c, (unsigned long)n[t][c],
                                (unsigned long)at_fall[t][c], (unsigned long)any[t][c], (unsigned long)own[t][c]);
        printf("\n");
    }
}

// With the 2068 held in reset (the bus idle): BE_REQ by hand, low then high
// then low, reading the bus's /BE back each time. Shows whether U5 pulls /BE
// and whether anything pulls it up. BE_REQ goes back to the PIO after.
static void be_static(void) {
    if (gpio_get(PIN_NRESET)) {
        printf("\n/RESET is high: hold the 2068 in reset first.\n");
        return;
    }
    gpio_put(PIN_BE_REQ, 0);
    gpio_set_dir(PIN_BE_REQ, GPIO_OUT);
    gpio_set_function(PIN_BE_REQ, GPIO_FUNC_SIO);
    printf("\nBE_REQ by hand, /BE read back (low samples of 10000):\n");
    for (int k = 0; k < 3; k++) {
        bool req = k == 1;
        gpio_put(PIN_BE_REQ, req);
        busy_wait_us_32(200);
        uint lows = 0;
        for (int i = 0; i < 10000; i++) lows += !gpio_get(PIN_NBE_L);
        printf("  BE_REQ %d: /BE low %u\n", req, lows);
    }
    gpio_put(PIN_BE_REQ, 0);
    // The release: how long /BE stays low after BE_REQ drops (sio reads).
    gpio_put(PIN_BE_REQ, 1);
    busy_wait_us_32(50);
    uint32_t irq = save_and_disable_interrupts();
    gpio_put(PIN_BE_REQ, 0);
    uint n = 0;
    while (!gpio_get(PIN_NBE_L) && n < 1000000) n++;
    restore_interrupts(irq);
    printf("  after release, /BE read low for %u polls (about 10 ns each)\n", n);
    pio_gpio_init(pio_bus, PIN_BE_REQ);
}

// The capture itself, with the 2068 in reset: BE_REQ toggled by hand under
// the LA, then each sample's BE_REQ and /BE bits compared with gpio_get.
static void la_selftest(void) {
    if (gpio_get(PIN_NRESET)) {
        printf("\n/RESET is high: hold the 2068 in reset first.\n");
        return;
    }
    gpio_put(PIN_BE_REQ, 0);
    gpio_set_dir(PIN_BE_REQ, GPIO_OUT);
    gpio_set_function(PIN_BE_REQ, GPIO_FUNC_SIO);
    la_setup(16, PIN_NMREQ);
    la_arm();
    for (int k = 0; k < 6; k++) { gpio_put(PIN_BE_REQ, k & 1); busy_wait_us_32(4); }
    uint old = la_stop();
    for (uint i = 0; i < LA_N; i++) lin[i] = la_buf[(old + i) % LA_N];
    gpio_put(PIN_BE_REQ, 0);
    pio_gpio_init(pio_bus, PIN_BE_REQ);
    printf("\nLA self-test: edges of BE_REQ (GP27) and /BE (GP42) and WINH (GP38) in the ring, sample numbers\n");
    const uint pins[3] = {PIN_BE_REQ, PIN_NBE_L, PIN_WINH};
    const char *nm[3] = {"BE_REQ", "/BE", "WINH"};
    for (int p = 0; p < 3; p++) {
        printf("  %-6s starts %d:", nm[p], S(0, pins[p]));
        int e = 0;
        for (int i = 1; i < LA_N && e < 16; i++)
            if (S(i, pins[p]) != S(i - 1, pins[p])) { printf(" %d", i); e++; }
        printf("\n");
    }
    printf("  raw word 0: %08lx, last: %08lx\n", (unsigned long)lin[0], (unsigned long)lin[LA_N - 1]);
}

// /BE's release, over many captures: for every cycle the board owns, when the
// bus's /BE is back high after /MREQ rises, and how long that leaves before
// the next /MREQ falls. By the cycle (opcode fetch, read, write) and what
// follows it (the fetch's refresh, another owned cycle, one of the 2068's).
// Times are where the readback (an LVC buffer, about 1.5 V) sees /BE cross.
typedef struct {
    uint32_t n, no_gap;                 // no_gap: the next cycle was owned and /BE never rose
    int lo_min, lo_max;                 // /BE low after /MREQ falls
    int rel_min, rel_max;               // BE_REQ off after /MREQ rises
    int rise_min, rise_max;             // /BE high after /MREQ rises
    int gap_min, gap_max;               // /MREQ falls again after /BE is high
} be_stat_t;

static void be_stat_add(int *mn, int *mx, int v, bool first) {
    if (first || v < *mn) *mn = v;
    if (first || v > *mx) *mx = v;
}

static void be_stats(void) {
    static be_stat_t st[3][3];
    static const char *cyc[3] = {"fetch", "read", "write"};
    static const char *nxt[3] = {"refresh", "owned", "2068's"};
    memset(st, 0, sizeof st);
    la_setup(16, PIN_NMREQ);            // trig: any cycle with BE_REQ high
    uint ns_x10 = 10000000u * LA_DIV / (clock_get_hz(clk_sys) / 1000);
    printf("\n/BE release: capturing for 5 s...\n");
    uint32_t caps = 0, drops = 0;
    absolute_time_t until = make_timeout_time_ms(5000);
    while (!time_reached(until)) {
        la_arm();
        busy_wait_us_32(40);
        pio_interrupt_clear(pio_la, 0);
        uint32_t irq = save_and_disable_interrupts();
        absolute_time_t slice = make_timeout_time_ms(20);
        bool hit = false;
        while (!time_reached(slice))
            if (pio_la->irq & 1) { hit = true; break; }
        if (!hit) { restore_interrupts(irq); continue; }
        busy_wait_us_32(24);
        uint old = la_stop();
        restore_interrupts(irq);
        if (pio_la->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + sm_la))) { drops++; continue; }
        for (uint i = 0; i < LA_N; i++) lin[i] = la_buf[(old + i) % LA_N];
        caps++;
        // Each owned cycle: a /MREQ-low run with BE_REQ high somewhere in it.
        for (int i = 1; i < LA_N; i++) {
            if (!(S(i - 1, PIN_NMREQ) && !S(i, PIN_NMREQ))) continue;
            int f = i, r = f;
            while (r < LA_N && !S(r, PIN_NMREQ)) r++;
            if (r >= LA_N) break;
            bool owned = false, m1 = false, wr = false;
            int lo = -1;
            for (int k = f; k < r; k++) {
                if (S(k, PIN_BE_REQ)) owned = true;
                if (!S(k, PIN_NM1)) m1 = true;
                if (!S(k, PIN_NWR)) wr = true;
                if (lo < 0 && !S(k, PIN_NBE_L)) lo = k;
            }
            if (!owned || lo < 0) { i = r; continue; }
            int rel = r;
            while (rel < LA_N && S(rel, PIN_BE_REQ)) rel++;
            int n = r;                      // the next /MREQ fall
            while (n < LA_N && S(n, PIN_NMREQ)) n++;
            if (n >= LA_N) break;
            int hi = r;
            while (hi < n && !S(hi, PIN_NBE_L)) hi++;
            int ct = m1 ? 0 : wr ? 2 : 1;
            int nk = !S(n, PIN_NRFSH) ? 0 : 1;
            if (nk == 1) {                  // owned if BE_REQ goes high in that cycle
                bool o2 = false;
                for (int k = n; k < LA_N && !S(k, PIN_NMREQ); k++)
                    if (S(k, PIN_BE_REQ)) { o2 = true; break; }
                if (!o2) nk = 2;
            }
            be_stat_t *s = &st[ct][nk];
            if (hi >= n) { s->no_gap++; i = r; continue; }
            bool first = s->n == 0;
            s->n++;
            be_stat_add(&s->lo_min, &s->lo_max, lo - f, first);
            be_stat_add(&s->rel_min, &s->rel_max, rel - r, first);
            be_stat_add(&s->rise_min, &s->rise_max, hi - r, first);
            be_stat_add(&s->gap_min, &s->gap_max, n - hi, first);
            i = r;
        }
    }
    printf("%lu captures (%lu dropped samples, skipped). ns; /MREQ rise is 0 for the last three:\n",
           (unsigned long)caps, (unsigned long)drops);
    printf("  %-6s %-8s %7s %6s  %-11s %-11s %-11s %-11s\n", "cycle", "next", "n", "no gap",
           "/BE low", "BE_REQ off", "/BE high", "gap to next");
#define NS(v) ((int)((v) * (int)ns_x10) / 10)
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 3; k++) {
            be_stat_t *s = &st[c][k];
            if (!s->n && !s->no_gap) continue;
            printf("  %-6s %-8s %7lu %6lu", cyc[c], nxt[k], (unsigned long)s->n, (unsigned long)s->no_gap);
            if (s->n)
                printf("  %4d..%-5d %4d..%-5d %4d..%-5d %4d..%-5d", NS(s->lo_min), NS(s->lo_max),
                       NS(s->rel_min), NS(s->rel_max), NS(s->rise_min), NS(s->rise_max),
                       NS(s->gap_min), NS(s->gap_max));
            putchar('\n');
        }
#undef NS
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

#define BATTERY_LOW_V   2.5f
#define BATTERY_EVERY_S 3600

// Measures the battery and sets STATUS bit 1 from it.
static float battery_check(void) {
    float v = battery_volts();
    battery_low = v < BATTERY_LOW_V;
    return v;
}

// ---- settings in flash: BASE and SIZE ----------------------------------------

// The last 4K sector of the flash. A record is valid only with the magic, the
// version, a check byte and values the spec allows.
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC  0x5453424du     // "MBST"

typedef struct {
    uint32_t magic;
    uint8_t version, base, size, check;
} settings_t;

static bool settings_ok(unsigned b, unsigned s) {
    return (b == 1 || b == 65 || b == 129 || b == 193) && (s == 8 || s == 16 || s == 32 || s == 64);
}

static uint8_t settings_check(uint8_t b, uint8_t s) { return (uint8_t)~(b ^ s ^ 0x5a); }

static bool settings_load(void) {
    const settings_t *st = (const settings_t *)(XIP_BASE + SETTINGS_OFFSET);
    if (st->magic != SETTINGS_MAGIC || st->version != 1 || st->check != settings_check(st->base, st->size)
        || !settings_ok(st->base, st->size))
        return false;
    base = st->base;
    size = st->size;
    return true;
}

static uint8_t settings_page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));

// Runs with core 1 locked out and interrupts off (flash_safe_execute). The
// SDK's erase and program end by re-running the boot XIP setup, which puts the
// flash back on the boot divider: 125 MHz at 250 MHz. flash_slow() puts it
// back to /3 before anything runs from flash again.
static void __no_inline_not_in_flash_func(settings_flash)(void *unused) {
    (void)unused;
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, settings_page, FLASH_PAGE_SIZE);
    flash_slow();
}

static bool settings_save(uint8_t b, uint8_t s) {
    memset(settings_page, 0xff, sizeof settings_page);
    settings_t st = {SETTINGS_MAGIC, 1, b, s, settings_check(b, s)};
    memcpy(settings_page, &st, sizeof st);
    return flash_safe_execute(settings_flash, NULL, 1000) == PICO_OK;
}

// A number typed at the console, ended by Enter; -1 for nothing or Esc.
static int read_number(void) {
    int n = -1;
    for (;;) {
        int ch = getchar_timeout_us(30 * 1000 * 1000);
        if (ch == PICO_ERROR_TIMEOUT || ch == 27) return -1;
        if (ch == '\r' || ch == '\n') return n;
        if (ch >= '0' && ch <= '9' && n < 1000) {
            n = (n < 0 ? 0 : n * 10) + (ch - '0');
            putchar(ch);
        }
    }
}

static void settings_menu(void) {
    printf("\nBASE %u, SIZE %u. New BASE (1, 65, 129, 193; Enter keeps it): ", base, size);
    int b = read_number();
    printf("\nNew SIZE (8, 16, 32, 64; Enter keeps it): ");
    int s = read_number();
    if (b < 0) b = base;
    if (s < 0) s = size;
    if (!settings_ok(b, s)) {
        printf("\nNot changed: BASE must be 1, 65, 129 or 193, SIZE 8, 16, 32 or 64.\n");
        return;
    }
    if (b == base && s == size) {
        printf("\nNot changed.\n");
        return;
    }
    // Core 1 stops while the flash is written (tens of ms): an I/O cycle in
    // that time goes unanswered. Best with the 2068 off or idle.
    if (!settings_save(b, s)) {
        printf("\nThe flash write failed; nothing changed.\n");
        return;
    }
    base = b;
    size = s;
    settings_from_flash = true;
    update_all();
    printf("\nSaved: BASE %u, SIZE %u (banks %u-%u).\n", base, size, base,
           base + size - 1 > 253 ? 253 : base + size - 1);
}

static void show_regs(void) {
    printf("\nOWN :");
    for (int c = 0; c < 8; c++) printf(" %3u", own[c]);
    printf("\nSUB :");
    for (int c = 0; c < 8; c++) printf(" %3u", sub[c]);
    printf("\nours:");
    for (int c = 0; c < 8; c++) printf("   %c", owned(c) ? '*' : '.');
    printf("\nWPROT %02x  STATUS %02x  sel %02x  BASE %u SIZE %u (%s)  chunks 0-1 %s\n", wprot,
           reg_read(0x11), sel, base, size, settings_from_flash ? "flash" : "defaults",
           rom_lock ? "LOCKED" : "unlocked");
    printf("I/O cycles %lu: IN FCh/FDh %lu, OUT %lu, dropped %lu, unstuck %lu; resets %lu\n", n_io, n_in,
           n_out, n_drop, n_unstick, n_reset);
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
           "  v   battery (and STATUS bit 1)\n"
           "  S   settings: BASE and SIZE, kept in flash\n"
           "  c   capture a mapped write (and a read): /MREQ /RD /WR /BE ... timing\n"
           "  C   capture a mapped read, with the data lines (SD0-7) and A7-A0\n"
           "  b   /BE release over many captures: rise after /MREQ, gap to the next cycle\n"
           "  B   capture an opcode fetch from a mapped chunk and the 2 us after it\n"
           "  A   the same for any opcode fetch (/BE with nothing mapped)\n"
           "  a   which cycles find /BE low, by type and chunk\n"
           "  p   with the 2068 in reset: BE_REQ by hand, /BE read back\n"
           "  P   with the 2068 in reset: the same under the capture (LA self-test)\n"
           "  k   toggle the chunk 0-1 lock (off at start-up; on: the board never answers there)\n"
           "  u   reboot to BOOTSEL\n",
           VERSION, (unsigned long)(clock_get_hz(clk_sys) / 1000000));
}

int main(void) {
    board_safe_init();
    clock_up();
    stdio_init_all();
    settings_from_flash = settings_load();
    battery_check();
    bus_start();
    absolute_time_t next_battery = make_timeout_time_ms(BATTERY_EVERY_S * 1000);

    for (;;) {
        int ch = getchar_timeout_us(100 * 1000);
        if (time_reached(next_battery)) {
            battery_check();
            next_battery = make_timeout_time_ms(BATTERY_EVERY_S * 1000);
        }
        if (ch == PICO_ERROR_TIMEOUT) continue;
        switch (ch) {
        case 'r': show_regs(); break;
        case 'l': show_log(); break;
        case 'd': show_diag(); break;
        case 'c': la_capture(true); break;
        case 'C': la_capture(false); break;
        case 'b': be_stats(); break;
        case 'B': la_fetch(true); break;
        case 'A': la_fetch(false); break;
        case 'a': be_by_chunk(); break;
        case 'p': be_static(); break;
        case 'P': la_selftest(); break;
        case 'm': {
            // m<c><b>: OWN of chunk c = bank b (digits), from the console
            int c = getchar_timeout_us(2000000) - '0', b = getchar_timeout_us(2000000) - '0';
            if (c >= 0 && c < 8 && b >= 0 && b < 10) { own[c] = b; sub[c] = c; update_chunk(c); }
            show_regs();
            break;
        }
        case 'v': {
            float v = battery_check();
            printf("\nbattery %.3f V%s\n", v, battery_low ? ": LOW (STATUS bit 1)" : "");
            break;
        }
        case 'S': settings_menu(); break;
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
