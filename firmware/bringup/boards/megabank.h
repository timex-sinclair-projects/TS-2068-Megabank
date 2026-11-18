/*
 * Megabank: RP2350B (QFN80), 12 MHz crystal, GD25Q128E 16 MB QSPI flash.
 * DVDD from an external 1.2 V LDO (U23); the on-chip switcher is unused.
 *
 * The pico-sdk board header, after the TS-Pico v3's (tspico-hardware,
 * firmware/bringup/boards/tspico_v3.h): the same chip, crystal and flash.
 */
#ifndef _BOARDS_MEGABANK_H
#define _BOARDS_MEGABANK_H

#define MEGABANK

// RP2350B: 48 GPIOs
#define PICO_RP2350A 0

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
