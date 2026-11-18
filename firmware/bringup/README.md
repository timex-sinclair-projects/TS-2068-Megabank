# Megabank bring-up test

Self-test firmware for the Megabank on the bench: the board on USB alone, no
2068. It talks over USB serial: connect any terminal and it runs the automatic
tests, prints PASS/FAIL for each, then offers a menu. After the TS-Pico v3's
bring-up test (tspico-hardware, `firmware/bringup/`).

## Build and flash

Needs the Pico SDK, the Arm GNU toolchain and picotool (`brew install
picotool`). Built with pico-sdk 2.1.1 and Arm GNU 13.3.rel1.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=~/pico/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi
cmake -S . -B build -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
make -C build -j8
picotool load -f -x build/bringup.uf2
```

A blank board comes up in BOOTSEL already (the `RP2350` drive); `-f` reboots
one running this firmware into BOOTSEL first. Or hold BOOTSEL (SW4), tap RUN
(SW3), and copy `build/bringup.uf2` onto the drive.

## Run

```bash
screen /dev/cu.usbmodem101
```

The automatic tests run once on the first connection; `a` runs them again,
`h` lists the rest.

| Key | Test | Checks |
|---|---|---|
| `s` | RP2350B | 48 GPIOs (the B package), crystal, clocks, die temperature (and so the ADC reference) |
| `f` | QSPI flash | JEDEC ID (GD25Q128E), erase/program/verify of the last sector |
| `r` | straps | R1, R2, R4, R10, R11, R12, R54: each output let go against the opposite internal pull reads its resistor's level |
| `b` | pins | bridges and stuck lines on SD0-SD7, PA13-PA20, GP46, GP47 |
| `v` | battery | the monitor off (0 V), then on: the cell's voltage |
| `w` | SW5 | 15 s monitor of the write-protect switch (slide it) |
| `i` | bus inputs | what BA0-BA7, BA13-BA15 and the control copies read (they float) |
| `m` | SRAM probe | whether each chip drives SD0-SD7 when selected |
| `t` | SRAM page test | one byte in each of the 512 pages, walking 1s and 0s on the data lines |
| `u` | reboot | to BOOTSEL, for the next flash |

The safe state is held throughout: data buffer off (nDOE high), /BE and /NMI
released, Z80 writes inhibited (WINH high), both SRAMs deselected, battery
monitor off.

### The SRAM on the bench

The SRAM's A0-A12, ~OE and ~WE come from the 2068 (BA0-BA12, nRD_L, nWR_L
through U1, U2, U4 and U12), so a real memory test needs the 2068 (spec,
Testing, "On hardware" 2). But with nothing on the fingers, those buffers'
inputs drift low and stay there: A0-A12 = 0, ~OE low, nWR_L low. Then WINH
alone works ~WE, and the controller can write and read one byte in every
page. `t` does that, after checking BA0-BA7, nRD_L and nWR_L read 0 (BA8-BA12
have no GPIO and are assumed low too). It covers the page bits, both ~CE1s,
the data lines, U12 and both chips; not A0-A12, nor the 2068's side.

## Results

**Board 1, 2026-11-18** (JLCPCB assembly, RP2350 A4, on USB alone): 11 pass,
1 fail.

- RP2350B: 48 GPIOs, crystal 12.000 MHz, clk_sys 150 MHz, clk_usb 48 MHz, die
  35 °C. Picotool, in BOOTSEL before any firmware: RP2350 A4, QFN80.
- Flash: c8 40 18 (GD25Q128E); the last sector programs, verifies and erases.
  Picotool also wrote and read back a sector at 4 KB and at 16 MB - 4 KB, with
  8 MB - 4 KB still blank, so there's no aliasing: the full 16 MB.
- Straps: all seven read their resistor's level.
- Pins: no bridges or stuck lines on SD0-SD7, PA13-PA20, GP46, GP47.
- Bus inputs: all read a steady 0 with nothing on the fingers.
- SRAM page test: walking 1s and 0s on both chips, and 512 pages × 2
  patterns, no errors.
- Battery: the monitor reads 0 V off (pass) and 0 V on (fail), as it should:
  no cell is fitted (JLCPCB fits the holder, not a CR2032). To do with a
  cell: the reading, and hardware test 3 (retention, standby current).
- SW5: reads "writable"; the switch has not been slid yet.
