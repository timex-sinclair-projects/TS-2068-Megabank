/*
 * Megabank bring-up test, for the bench: the board on USB alone, no 2068.
 *
 * Talks over USB serial (any terminal; it's CDC). On first connect it runs
 * every automatic test and prints a PASS/FAIL report, then offers a menu.
 * After the TS-Pico v3's bring-up test (tspico-hardware, firmware/bringup).
 *
 * What USB alone can't test: the SRAM. Its A0-A12, ~OE and ~WE come from the
 * 2068 (BA0-BA12, nRD_L, nWR_L through U1, U2, U4 and U12), and with no 2068
 * those buffers' inputs float. The memory test runs on the extender
 * (docs/spec.md, Testing, "On hardware" 2).
 *
 * Safe state, held throughout: the data buffer off (nDOE high), /BE and /NMI
 * released (BE_REQ, NMI_REQ low), Z80 writes inhibited (WINH high), both
 * SRAMs deselected (nCE1_0/1 high), the battery monitor off (BAT_EN low).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "pico/unique_id.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"

// ---- pins (hardware/README.md, "GPIO map") -----------------------------------

#define PIN_SD0      0   // SD0-SD7: 0-7
#define PIN_BA0      8   // BA0-BA7: 8-15
#define PIN_BA13     16  // BA13-BA15: 16-18
#define PIN_NMREQ_L  19  // nMREQ_L nIORQ_L nRD_L nWR_L nRFSH_L nM1_L nRESET_L: 19-25
#define PIN_NRD_L    21
#define PIN_NRESET_L 25
#define PIN_NDOE     26  // R2 pull-up
#define PIN_BE_REQ   27  // R1 pull-down
#define PIN_NCE1_0   28  // R10 pull-down
#define PIN_NCE1_1   29  // R11 pull-down
#define PIN_PA13     30  // PA13-PA20: 30-37
#define PIN_WINH     38  // R12 pull-up
#define PIN_NWP      39  // SW5 to ground; internal pull-up
#define PIN_BAT_ADC  40  // ADC0
#define PIN_BAT_EN   41  // pull-down (spec's R4, R54 here)
#define PIN_NBE_L    42
#define PIN_NINT_L   43
#define PIN_NNMI_L   44
#define PIN_NMI_REQ  45  // R4 pull-down
#define PIN_GP46     46  // TP4
#define PIN_GP47     47  // TP5

typedef struct {
    uint pin;
    const char *name;
    bool level;  // the safe level we drive, and the strap's own level
    const char *strap;
} out_pin_t;

// Outputs with a strap resistor: driven to the strap's level.
static const out_pin_t STRAPS[] = {
    {PIN_NDOE, "nDOE", 1, "R2 pull-up"},
    {PIN_BE_REQ, "BE_REQ", 0, "R1 pull-down"},
    {PIN_NCE1_0, "nCE1_0", 0, "R10 pull-down"},
    {PIN_NCE1_1, "nCE1_1", 0, "R11 pull-down"},
    {PIN_WINH, "WINH", 1, "R12 pull-up"},
    {PIN_BAT_EN, "BAT_EN", 0, "R54 pull-down"},
    {PIN_NMI_REQ, "NMI_REQ", 0, "R4 pull-down"},
};

static const char *const BUS_NAMES[] = {
    "BA0", "BA1", "BA2", "BA3", "BA4", "BA5", "BA6", "BA7",
    "BA13", "BA14", "BA15",
    "nMREQ", "nIORQ", "nRD", "nWR", "nRFSH", "nM1", "nRESET",
};

static void out(uint pin, bool level) {
    gpio_init(pin);
    gpio_disable_pulls(pin);
    gpio_put(pin, level);
    gpio_set_dir(pin, GPIO_OUT);
}

static void board_safe_init(void) {
    // Bus-facing outputs first: buffer off, /BE and /NMI released.
    out(PIN_NDOE, 1);
    out(PIN_BE_REQ, 0);
    out(PIN_NMI_REQ, 0);
    out(PIN_WINH, 1);
    // Both SRAMs deselected (the pull-downs would select them), so nothing
    // but us can drive SD0-SD7.
    out(PIN_NCE1_0, 1);
    out(PIN_NCE1_1, 1);
    for (uint p = PIN_PA13; p < PIN_PA13 + 8; p++) out(p, 0);
    out(PIN_BAT_EN, 0);
    // SD bus and bus-side inputs: plain inputs.
    for (uint p = PIN_SD0; p <= PIN_NRESET_L; p++) {
        gpio_init(p);
        gpio_disable_pulls(p);
    }
    for (uint p = PIN_NBE_L; p <= PIN_NNMI_L; p++) {
        gpio_init(p);
        gpio_disable_pulls(p);
    }
    gpio_init(PIN_NWP);
    gpio_pull_up(PIN_NWP);
    gpio_init(PIN_GP46);
    gpio_pull_down(PIN_GP46);
    gpio_init(PIN_GP47);
    gpio_pull_down(PIN_GP47);
    adc_init();
    adc_gpio_init(PIN_BAT_ADC);
}

static void sram_deselect(void) {
    gpio_put(PIN_NCE1_0, 1);
    gpio_put(PIN_NCE1_1, 1);
}

// ---- reporting --------------------------------------------------------------

static int n_pass, n_fail, n_look;

typedef enum { R_PASS, R_FAIL, R_INFO, R_LOOK, R_SKIP } result_t;

static void report(result_t r, const char *name, const char *fmt, ...) {
    static const char *const tag[] = {"PASS", "FAIL", "info", "LOOK", "skip"};
    if (r == R_PASS) n_pass++;
    if (r == R_FAIL) n_fail++;
    if (r == R_LOOK) n_look++;
    printf("  [%s] %-10s ", tag[r], name);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static void heading(const char *s) { printf("\n== %s\n", s); }

static int read_line(char *buf, int len) {
    int n = 0;
    for (;;) {
        int c = getchar_timeout_us(100 * 1000);
        if (c == PICO_ERROR_TIMEOUT) {
            if (!stdio_usb_connected()) return -1;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (n == 0) continue;
            printf("\n");
            buf[n] = 0;
            return n;
        }
        if ((c == 8 || c == 127) && n > 0) {
            n--;
            printf("\b \b");
            continue;
        }
        if (c >= 32 && c < 127 && n < len - 1) {
            buf[n++] = (char)c;
            putchar(c);
        }
    }
}

// ---- system -----------------------------------------------------------------

static float adc_volts(uint input, int samples) {
    adc_select_input(input);
    sleep_us(50);
    uint32_t acc = 0;
    for (int i = 0; i < samples; i++) acc += adc_read();
    return (acc / (float)samples) * 3.3f / 4096.0f;
}

static void test_system(void) {
    heading("RP2350B (U20)");
    char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(id, sizeof id);
    report(NUM_BANK0_GPIOS == 48 ? R_PASS : R_FAIL, "chip", "RP2350, %u GPIOs, unique id %s",
           NUM_BANK0_GPIOS, id);

    uint32_t xosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC);
    report(xosc > 11940 && xosc < 12060 ? R_PASS : R_FAIL, "crystal", "%lu kHz (expect 12000)", xosc);

    uint32_t sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    uint32_t usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    report(usb > 47900 && usb < 48100 ? R_PASS : R_FAIL, "clocks",
           "clk_sys %lu kHz, clk_usb %lu kHz", sys, usb);

    adc_set_temp_sensor_enabled(true);
    float v = adc_volts(ADC_TEMPERATURE_CHANNEL_NUM, 64);
    adc_set_temp_sensor_enabled(false);
    float t = 27.0f - (v - 0.706f) / 0.001721f;
    // Assumes a 3.3 V ADC reference, so this also sanity-checks ADC_AVDD (R28/C39).
    report(t > 5 && t < 75 ? R_PASS : R_FAIL, "die temp", "%.1f C (sensor %.3f V)", t, v);
}

// ---- QSPI flash -------------------------------------------------------------

#define FLASH_TEST_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint8_t jedec[3];
    const uint8_t *data;
    int op;  // 0 = JEDEC ID, 1 = erase test sector, 2 = program test sector
} flash_job_t;

static void __no_inline_not_in_flash_func(flash_job)(void *arg) {
    flash_job_t *j = arg;
    if (j->op == 0) {
        uint8_t tx[4] = {0x9f, 0, 0, 0}, rx[4];
        flash_do_cmd(tx, rx, 4);
        memcpy(j->jedec, rx + 1, 3);
    } else if (j->op == 1) {
        flash_range_erase(FLASH_TEST_OFFSET, FLASH_SECTOR_SIZE);
    } else {
        flash_range_program(FLASH_TEST_OFFSET, j->data, FLASH_SECTOR_SIZE);
    }
}

static void test_flash(void) {
    heading("QSPI flash (U21)");
    flash_job_t j = {.op = 0};
    if (flash_safe_execute(flash_job, &j, 1000) != PICO_OK) {
        report(R_FAIL, "jedec", "flash_safe_execute failed");
        return;
    }
    bool id_ok = j.jedec[0] == 0xc8 && j.jedec[1] == 0x40 && j.jedec[2] == 0x18;
    report(id_ok ? R_PASS : R_FAIL, "jedec", "%02x %02x %02x (expect c8 40 18, GD25Q128E 16 MB)",
           j.jedec[0], j.jedec[1], j.jedec[2]);

    static uint8_t pattern[FLASH_SECTOR_SIZE];
    uint32_t x = time_us_32() | 1;
    for (unsigned i = 0; i < sizeof pattern; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        pattern[i] = (uint8_t)x;
    }
    const uint8_t *mem = (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + FLASH_TEST_OFFSET);

    j.op = 1;
    flash_safe_execute(flash_job, &j, 1000);
    j.op = 2;
    j.data = pattern;
    flash_safe_execute(flash_job, &j, 1000);
    int bad = 0;
    for (unsigned i = 0; i < sizeof pattern; i++) bad += mem[i] != pattern[i];
    j.op = 1;
    flash_safe_execute(flash_job, &j, 1000);
    int not_blank = 0;
    for (unsigned i = 0; i < sizeof pattern; i++) not_blank += mem[i] != 0xff;
    report(bad == 0 && not_blank == 0 ? R_PASS : R_FAIL, "rw",
           "last sector @ 0x%06x: program/verify %d bad bytes, erase %d not blank",
           FLASH_TEST_OFFSET, bad, not_blank);
}

// ---- straps -----------------------------------------------------------------

// Each strapped output, let go with the internal pull set against its
// resistor (about 50k against 10k): it must read the resistor's level.
static void test_straps(void) {
    heading("Strap resistors");
    int faults = 0;
    char line[160];
    int o = 0;
    for (unsigned i = 0; i < count_of(STRAPS); i++) {
        const out_pin_t *s = &STRAPS[i];
        gpio_set_dir(s->pin, GPIO_IN);
        gpio_set_pulls(s->pin, !s->level, s->level);
        sleep_us(200);
        bool got = gpio_get(s->pin);
        gpio_disable_pulls(s->pin);
        gpio_put(s->pin, i == 2 || i == 3 ? 1 : s->level);  // SRAMs stay deselected
        gpio_set_dir(s->pin, GPIO_OUT);
        if (got != s->level) {
            printf("    GP%u %s reads %d against the internal pull: %s missing?\n", s->pin, s->name,
                   got, s->strap);
            faults++;
        }
        o += snprintf(line + o, sizeof line - o, "%s ", s->name);
    }
    report(faults ? R_FAIL : R_PASS, "straps", "%s(%d fault%s)", line, faults, faults == 1 ? "" : "s");
}

// ---- pins -------------------------------------------------------------------

// SD0-SD7 (SRAMs deselected, U3 off: nothing else drives them), PA13-PA20
// (SRAM address inputs only) and the two test pads: each must follow its
// internal pull, and driving one must not move another.
static void test_pins(void) {
    heading("Pins (bridges and stuck lines)");
    if (!gpio_get_out_level(PIN_NDOE) || !gpio_get_out_level(PIN_NCE1_0) ||
        !gpio_get_out_level(PIN_NCE1_1)) {
        report(R_FAIL, "bridges", "refusing: the data buffer or an SRAM is enabled");
        return;
    }
    uint pins[18];
    unsigned n = 0;
    for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) pins[n++] = p;
    for (uint p = PIN_PA13; p < PIN_PA13 + 8; p++) pins[n++] = p;
    pins[n++] = PIN_GP46;
    pins[n++] = PIN_GP47;

    int faults = 0;
    for (int level = 1; level >= 0; level--) {
        for (unsigned i = 0; i < n; i++) {
            gpio_set_dir(pins[i], GPIO_IN);
            gpio_set_pulls(pins[i], !level, level);
        }
        sleep_us(100);
        for (unsigned i = 0; i < n; i++) {
            if (gpio_get(pins[i]) != !level) {
                printf("    GP%u stuck %s (does not follow its pull-%s)\n", pins[i],
                       level ? "high" : "low", level ? "down" : "up");
                faults++;
            }
        }
        for (unsigned i = 0; i < n; i++) {
            gpio_put(pins[i], level);
            gpio_set_dir(pins[i], GPIO_OUT);
            sleep_us(50);
            for (unsigned k = 0; k < n; k++) {
                if (k != i && gpio_get(pins[k]) == level) {
                    printf("    GP%u follows GP%u driven %s: bridged?\n", pins[k], pins[i],
                           level ? "high" : "low");
                    faults++;
                }
            }
            gpio_set_dir(pins[i], GPIO_IN);
        }
    }
    // Back to the safe state.
    for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) gpio_disable_pulls(p);
    for (uint p = PIN_PA13; p < PIN_PA13 + 8; p++) out(p, 0);
    gpio_pull_down(PIN_GP46);
    gpio_pull_down(PIN_GP47);
    report(faults ? R_FAIL : R_PASS, "bridges", "SD0-SD7, PA13-PA20, GP46, GP47: %d fault%s", faults,
           faults == 1 ? "" : "s");
}

// Bus-side inputs float with no 2068: just show what they do.
static void test_bus_inputs(void) {
    heading("Bus inputs (floating without a 2068)");
    uint pins[count_of(BUS_NAMES)];
    unsigned n = 0;
    for (uint p = PIN_BA0; p < PIN_BA0 + 8; p++) pins[n++] = p;
    for (uint p = PIN_BA13; p <= PIN_NRESET_L; p++) pins[n++] = p;
    unsigned ones[count_of(BUS_NAMES)] = {0};
    const int samples = 1000;
    for (int s = 0; s < samples; s++) {
        for (unsigned i = 0; i < n; i++) ones[i] += gpio_get(pins[i]);
        sleep_us(10);
    }
    char line[256];
    int o = 0;
    for (unsigned i = 0; i < n; i++) {
        char c = ones[i] == 0 ? '0' : ones[i] == (unsigned)samples ? '1' : '~';
        o += snprintf(line + o, sizeof line - o, "%s=%c ", BUS_NAMES[i], c);
    }
    report(R_INFO, "bus", "%s", line);
    report(R_INFO, "via U4/U7", "nBE=%d nINT=%d nNMI=%d  (~ = changed while sampled)",
           gpio_get(PIN_NBE_L), gpio_get(PIN_NINT_L), gpio_get(PIN_NNMI_L));
}

// ---- battery and switch -----------------------------------------------------

static void test_battery(void) {
    heading("Battery (BT1) and monitor (Q1, Q2, R51-R54)");
    float off = adc_volts(0, 64) * 2;
    gpio_put(PIN_BAT_EN, 1);
    sleep_ms(25);
    float on = adc_volts(0, 64) * 2;
    gpio_put(PIN_BAT_EN, 0);
    // C55 then drains through R52: 100k x 100nF, a 10 ms time constant.
    float decay[4];
    static const int at_ms[4] = {10, 25, 50, 100};
    absolute_time_t t0 = get_absolute_time();
    for (int i = 0; i < 4; i++) {
        sleep_until(delayed_by_ms(t0, at_ms[i]));
        decay[i] = adc_volts(0, 16) * 2;
    }
    float after = decay[3];
    report(off < 0.1f && after < 0.1f ? R_PASS : R_FAIL, "off",
           "%.3f V before; after BAT_EN low %.3f/%.3f/%.3f/%.3f V at 10/25/50/100 ms "
           "(expect ~0 by 100 ms: Q1 off, C55 drains through R52)",
           off, decay[0], decay[1], decay[2], decay[3]);
    bool ok = on >= 2.5f && on <= 3.45f;
    report(ok ? R_PASS : R_FAIL, "cell", "%.3f V%s", on,
           on < 0.3f ? " (no cell in BT1 - JLCPCB doesn't fit one - or Q1/Q2 not switching)"
           : on < 2.5f ? " (below 2.5 V: low cell)"
           : on > 3.45f ? " (above a CR2032: divider?)"
           : " (a CR2032: 2.5-3.45 V)");
}

static void test_wp(void) {
    heading("Write-protect switch (SW5)");
    bool wp = !gpio_get(PIN_NWP);
    report(R_INFO, "SW5", "%s (nWP=%d)", wp ? "protected" : "writable", !wp);
}

static void monitor_wp(void) {
    printf("Slide SW5 back and forth; 15 s.\n");
    bool last = gpio_get(PIN_NWP);
    int changes = 0;
    absolute_time_t end = make_timeout_time_ms(15000);
    printf("  SW5: %s\n", last ? "writable" : "protected");
    while (!time_reached(end)) {
        bool now = gpio_get(PIN_NWP);
        if (now != last) {
            sleep_ms(20);  // debounce
            now = gpio_get(PIN_NWP);
            if (now != last) {
                printf("  SW5: %s\n", now ? "writable" : "protected");
                changes++;
                last = now;
            }
        }
        sleep_ms(2);
    }
    report(changes >= 2 ? R_PASS : R_LOOK, "SW5", "%d change%s seen", changes, changes == 1 ? "" : "s");
}

// ---- SRAM probe -------------------------------------------------------------

// Not a memory test (see the top of the file). Selects one chip at a time and
// sees whether it drives SD0-SD7, which it does only if the floating nRD_L
// (its ~OE) happens to sit low. Pulling the 2068's /RD finger to ground
// through 1k makes nRD_L low on purpose.
static void probe_sram(void) {
    heading("SRAM probe (not a memory test)");
    for (int chip = 0; chip < 2; chip++) {
        uint ce = chip ? PIN_NCE1_1 : PIN_NCE1_0;
        uint8_t up, down;
        int nrd;
        sram_deselect();
        gpio_put(ce, 0);
        sleep_us(10);
        nrd = gpio_get(PIN_NRD_L);
        for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) gpio_pull_up(p);
        sleep_us(50);
        up = (gpio_get_all64() >> PIN_SD0) & 0xff;
        for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) gpio_pull_down(p);
        sleep_us(50);
        down = (gpio_get_all64() >> PIN_SD0) & 0xff;
        sram_deselect();
        for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) gpio_disable_pulls(p);
        uint8_t driven = ~(up ^ down);
        report(R_INFO, chip ? "U11" : "U10",
               "nRD_L=%d, SD pulled up %02x, down %02x: %s", nrd, up, down,
               driven == 0xff ? "driven (the chip answers)"
               : driven == 0 ? "not driven" : "partly driven");
    }
}

// ---- SRAM page test ---------------------------------------------------------

// A bench test that leans on the floating inputs: with no 2068, U1/U2/U4's
// inputs drift low and stay there, so A0-A12 = 0, ~OE (nRD_L) = 0 and
// nWR_L = 0. Then WINH alone is ~WE (U12: nSWE = nWR_L OR WINH), and the
// controller can write and read one byte in each of the 512 pages: that
// covers the page bits PA13-PA20, both ~CE1s, all eight data lines, U12 and
// both chips. BA8-BA12 have no GPIO, so they're assumed to sit low like the
// rest. Not a substitute for the memory test on the 2068.

static void sd_drive(uint8_t v) {
    gpio_put_masked(0xff << PIN_SD0, (uint32_t)v << PIN_SD0);
    gpio_set_dir_out_masked(0xff << PIN_SD0);
}

static void sd_release(void) { gpio_set_dir_in_masked(0xff << PIN_SD0); }

static uint8_t sd_read(void) { return (gpio_get_all64() >> PIN_SD0) & 0xff; }

static void sram_page(unsigned page) {  // 0-511: bit 8 picks the chip
    sram_deselect();
    gpio_put_masked64(0xffull << PIN_PA13, (uint64_t)(page & 0xff) << PIN_PA13);
    gpio_put(page & 0x100 ? PIN_NCE1_1 : PIN_NCE1_0, 0);
    busy_wait_us(1);
}

static void sram_write(unsigned page, uint8_t v) {
    sram_page(page);
    gpio_put(PIN_WINH, 0);  // ~WE low: the chip stops driving SD
    busy_wait_us(1);
    sd_drive(v);
    busy_wait_us(1);
    gpio_put(PIN_WINH, 1);  // ~WE high: written
    busy_wait_us(1);
    sd_release();
    sram_deselect();
}

static uint8_t sram_read(unsigned page) {
    sram_page(page);
    busy_wait_us(1);
    uint8_t v = sd_read();
    sram_deselect();
    return v;
}

static uint8_t page_pattern(unsigned page, bool inv) {
    uint8_t v = (uint8_t)(page ^ (page >> 8) * 0x5a ^ 0x33);
    return inv ? (uint8_t)~v : v;
}

static const struct keep_flag_s *keep_flag_any(void);

static void test_sram(void) {
    heading("SRAM page test (bench: A0-A12 at their floating 0)");
    if (keep_flag_any()) {
        report(R_SKIP, "SRAM", "a retention pattern is waiting to be checked: 'c' first");
        return;
    }
    uint64_t in = gpio_get_all64();
    bool lo = !((in >> PIN_BA0) & 0xff) && !((in >> PIN_NRD_L) & 1) && !((in >> (PIN_NRD_L + 1)) & 1);
    if (!lo) {
        report(R_SKIP, "SRAM", "BA0-BA7, nRD_L and nWR_L must all read 0 (see 'i')");
        return;
    }
    sd_release();
    for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) gpio_disable_pulls(p);

    // Data lines: walking ones and zeros in page 0 of each chip.
    int dbad = 0;
    for (unsigned chip = 0; chip < 2; chip++) {
        for (int b = 0; b < 16; b++) {
            uint8_t v = b < 8 ? 1u << b : (uint8_t)~(1u << (b - 8));
            sram_write(chip << 8, v);
            uint8_t got = sram_read(chip << 8);
            if (got != v) {
                if (dbad < 8) printf("    U1%u: wrote %02x read %02x\n", chip, v, got);
                dbad++;
            }
        }
    }
    report(dbad ? R_FAIL : R_PASS, "data", "walking 1s and 0s on SD0-SD7, both chips: %d bad", dbad);

    // Page bits and chip select: a distinct byte in every page, then its inverse.
    int pbad = 0;
    for (int inv = 0; inv < 2; inv++) {
        for (unsigned page = 0; page < 512; page++) sram_write(page, page_pattern(page, inv));
        for (unsigned page = 0; page < 512; page++) {
            uint8_t want = page_pattern(page, inv), got = sram_read(page);
            if (got != want) {
                if (pbad < 8) printf("    page %3u (U1%u, PA=%02x): want %02x read %02x\n", page,
                                     page >> 8, page & 0xff, want, got);
                pbad++;
            }
        }
    }
    report(pbad ? R_FAIL : R_PASS, "pages", "512 pages x 2 patterns (PA13-PA20, nCE1_0/1): %d bad", pbad);
    gpio_put_masked64(0xffull << PIN_PA13, 0);
}

// ---- retention (hardware test 3, on the bench) --------------------------------

// 'k' writes a pattern into every page (one byte each, as the page test) and
// sets a flag in flash; unplug, wait, plug back in. On the next connection
// the automatic run checks the pattern before anything else touches the SRAM,
// then clears the flag. The flag tells "lost" from "never written". It lives
// in the second-to-last flash sector (the flash test uses the last).

#define KEEP_OFFSET (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)
#define KEEP_MAGIC  0x4d42524bu  // "KRBM"

typedef struct keep_flag_s {
    uint32_t magic;
    uint32_t seed;
    uint32_t written_ms;  // uptime when written, for the log
    float cell_v;         // the cell when written
} keep_flag_t;

typedef struct {
    bool erase_only;
    const uint8_t *page;
} keep_job_t;

static void __no_inline_not_in_flash_func(keep_flash_job)(void *arg) {
    keep_job_t *j = arg;
    flash_range_erase(KEEP_OFFSET, FLASH_SECTOR_SIZE);
    if (!j->erase_only) flash_range_program(KEEP_OFFSET, j->page, FLASH_PAGE_SIZE);
}

static const keep_flag_t *keep_flag(void) {
    const keep_flag_t *f = (const keep_flag_t *)(XIP_NOCACHE_NOALLOC_BASE + KEEP_OFFSET);
    return f->magic == KEEP_MAGIC ? f : NULL;
}

static uint8_t keep_pattern(unsigned page, uint32_t seed) {
    uint32_t x = (page + 1) * 2654435761u ^ seed;
    x ^= x >> 15;
    x *= 0x2c1b3c6d;
    x ^= x >> 12;
    return (uint8_t)x;
}

static float cell_volts(void) {
    gpio_put(PIN_BAT_EN, 1);
    sleep_ms(25);
    float v = adc_volts(0, 64) * 2;
    gpio_put(PIN_BAT_EN, 0);
    return v;
}

static bool sram_bench_ready(void) {
    uint64_t in = gpio_get_all64();
    return !((in >> PIN_BA0) & 0xff) && !((in >> PIN_NRD_L) & 1) && !((in >> (PIN_NRD_L + 1)) & 1);
}

static void keep_write(void) {
    heading("Retention: write");
    if (!sram_bench_ready()) {
        report(R_SKIP, "keep", "BA0-BA7, nRD_L and nWR_L must all read 0 (see 'i')");
        return;
    }
    float v = cell_volts();
    if (v < 2.0f) {
        report(R_FAIL, "keep", "cell reads %.2f V: fit a CR2032 first", v);
        return;
    }
    keep_flag_t f = {KEEP_MAGIC, time_us_32() | 1, to_ms_since_boot(get_absolute_time()), v};
    sd_release();
    for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) gpio_disable_pulls(p);
    for (unsigned page = 0; page < 512; page++) sram_write(page, keep_pattern(page, f.seed));
    int bad = 0;
    for (unsigned page = 0; page < 512; page++) bad += sram_read(page) != keep_pattern(page, f.seed);
    gpio_put_masked64(0xffull << PIN_PA13, 0);
    if (bad) {
        report(R_FAIL, "keep", "%d of 512 pages read back wrong straight away; flag not set", bad);
        return;
    }
    static uint8_t buf[FLASH_PAGE_SIZE];
    memset(buf, 0xff, sizeof buf);
    memcpy(buf, &f, sizeof f);
    keep_job_t j = {false, buf};
    flash_safe_execute(keep_flash_job, &j, 1000);
    report(keep_flag() ? R_PASS : R_FAIL, "keep",
           "512 pages written and verified, seed %08lx, cell %.3f V. Unplug USB now; plug back in "
           "and connect to check.", f.seed, v);
}

static const struct keep_flag_s *keep_flag_any(void) { return keep_flag(); }

// Returns true if there was a pattern to check.
static bool keep_check(void) {
    const keep_flag_t *f = keep_flag();
    if (!f) return false;
    heading("Retention: check (hardware test 3)");
    if (!sram_bench_ready()) {
        report(R_SKIP, "kept", "BA0-BA7, nRD_L and nWR_L must all read 0 (see 'i'); flag kept");
        return true;
    }
    sd_release();
    for (uint p = PIN_SD0; p < PIN_SD0 + 8; p++) gpio_disable_pulls(p);
    int bad = 0;
    for (unsigned page = 0; page < 512; page++) {
        uint8_t want = keep_pattern(page, f->seed), got = sram_read(page);
        if (got != want) {
            if (bad < 8) printf("    page %3u (U1%u): want %02x read %02x\n", page, page >> 8, want, got);
            bad++;
        }
    }
    gpio_put_masked64(0xffull << PIN_PA13, 0);
    float v = cell_volts();
    report(bad ? R_FAIL : R_PASS, "kept", "%d of 512 pages lost; seed %08lx; cell %.3f V when written, %.3f V now",
           bad, f->seed, f->cell_v, v);
    keep_job_t j = {true, NULL};
    flash_safe_execute(keep_flash_job, &j, 1000);
    return true;
}

// ---- main -------------------------------------------------------------------

static void run_auto(void) {
    n_pass = n_fail = n_look = 0;
    printf("\nMegabank bring-up (USB only): automatic tests\n");
    keep_check();  // before anything else touches the SRAM
    test_system();
    test_flash();
    test_straps();
    test_pins();
    test_battery();
    test_wp();
    test_bus_inputs();
    test_sram();
    report(R_INFO, "SRAM", "the full test needs the 2068 for A0-A12, /RD and /WR (spec: Testing, On hardware 2)");
    printf("\n== Summary: %d pass, %d fail\n", n_pass, n_fail);
}

static void help(void) {
    printf("\nCommands:\n"
           "  a       all automatic tests\n"
           "  s f     system, flash\n"
           "  r b     strap resistors, pins/bridges\n"
           "  v       battery\n"
           "  i       bus inputs\n"
           "  w       write-protect switch monitor (15 s)\n"
           "  m       SRAM probe (not a memory test)\n"
           "  t       SRAM page test (bench: one byte per page)\n"
           "  k c     retention: write a pattern (then unplug), check it\n"
           "  u       reboot to BOOTSEL (USB drive)\n"
           "  h       this help\n");
}

int main(void) {
    board_safe_init();
    stdio_init_all();

    for (;;) {
        while (!stdio_usb_connected()) sleep_ms(100);
        sleep_ms(300);
        static bool ran;
        if (!ran) {
            run_auto();
            ran = true;
        }
        help();
        char line[32];
        for (;;) {
            printf("> ");
            if (read_line(line, sizeof line) < 0) break;
            n_pass = n_fail = n_look = 0;
            switch (line[0]) {
            case 'a': run_auto(); break;
            case 's': test_system(); break;
            case 'f': test_flash(); break;
            case 'r': test_straps(); break;
            case 'b': test_pins(); break;
            case 'v': test_battery(); break;
            case 'i': test_bus_inputs(); break;
            case 'w': monitor_wp(); break;
            case 'm': probe_sram(); break;
            case 't': test_sram(); break;
            case 'k': keep_write(); break;
            case 'c':
                if (!keep_check()) printf("No retention pattern written ('k' writes one).\n");
                break;
            case 'u':
                printf("Rebooting to BOOTSEL.\n");
                sleep_ms(100);
                reset_usb_boot(0, 0);
                break;
            default: help(); break;
            }
        }
    }
}
