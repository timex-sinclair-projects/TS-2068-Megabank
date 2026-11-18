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
| `k` / `c` | retention | `k` writes a seeded byte in every page and sets a flag in flash; unplug, plug back in, and the next automatic run checks it first, then clears the flag |
| `z [n]` | bus monitor | in a 2068: n 1 s windows (default 5) of /INT, /NMI, /BE, /M1, memory cycles by chunk, I/O ports in and out; passive |
| `u` | reboot | to BOOTSEL, for the next flash |

The firmware is safe to leave in a 2068: it never drives the 2068's bus.
The bench SRAM tests (`t`, `k`, the retention check) run only when BA0-BA7,
nRD_L, nWR_L and nRESET_L read a steady 0 for 2 ms, which a running 2068
never gives. Core 1 samples the bus for `z` (one 32-bit GPIO read every
268 ns, against a 2068's /MREQ low for at least about 425 ns); it starts at
boot, since `flash_safe_execute` needs it as a lockout victim once
pico_multicore is linked.

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
1 fail with no cell; 12 pass with one.

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
- Battery, first run: 0 V on, since no cell was fitted (JLCPCB fits the
  holder, not a CR2032).
- Battery, with a fresh CR2032: 3.23 V. Off, the pin reads 0 V before
  BAT_EN goes high; after it goes low, 1.67/0.41/0.04/0.00 V at
  10/25/50/100 ms. Q1 takes a few ms to turn off (R3, 1 M, charging its
  gate), then C55 drains through R52 with the expected 10 ms time constant.
  (The first version of the test looked at 25 ms and called that a fail.)
- SW5: 11 changes in 15 s of sliding, no bounce seen after the 20 ms debounce.

- Retention (hardware test 3, short form): all 512 pages written, USB
  unplugged for a short while, plugged back in: 0 of 512 lost, cell 3.231 V
  before, 3.229 V after. The battery takes over the SRAM rail (U32) and CE2
  (U31) keeps the chips deselected while 3.3 V falls and rises.

With the cell: **13 pass, 0 fail.** Still to do: retention over a day, the
standby current (a meter in series with the cell), then the 2068 on the
extender.

**Board 1 in a 2068, 2026-11-18** (TS2068_Extender, board in a free slot,
jumper open; passive, `z`): the 2068 boots and runs normally with the board
fitted.

- /RESET high; /INT 60-61 a second; 60 interrupt acknowledges a second.
- Every buffered line moves: BA0-BA7, BA13-BA15, /MREQ, /IORQ, /RD, /WR,
  /RFSH, /M1.
- Idle at the K cursor: IN FEh 480 times a second (8 keyboard rows a frame),
  about 747,000 memory reads a second in chunks 0, 2 and 3, none in 4-7,
  the same to within 0.1% from one second to the next.
- Typing: two OUT FEh per key (the click), chunk 1 reads appear.
- `OUT 252,2: OUT 253,2: PRINT IN 252, IN 253`: exactly one OUT and one IN
  each at FCh and FDh, with the AY's F5h/F6h for the ENTER beep. So the
  address buffers and strobes give port decode what it needs. Nothing
  answers yet: there's no controller firmware.
- /BE with no TS-Pico: a steady high, as it should be (nothing pulls it
  low). On the bench, with nothing on the fingers, nBE_L reads 0 like every
  other floating input; here it read 1, so something on the 2068 or the
  extender pulls /BE up (spec, open question 13). Not yet measured how
  strongly.
- /BE with the TS-Pico in slot 1 and this board in slot 2, SW2 open: still a
  steady high. The extender chains /BE through its slide switches (spec,
  "Sharing the bus"): with SW2 open, slot 2's /BE is only slot 1's BUSISO.
- **SW2 closed:** /BE falls 591,478 times a second against 586,082 chunk 0
  reads: the TS-Pico serving HOME, and the readback (U4's spare channel,
  GPIO42) following it.
- The monitor's refresh count is approximate: it fell from about 259,000 to
  56,000 a second once /BE moved, against 370,000 /M1. A refresh /MREQ comes
  about 140 ns after the fetch's /MREQ rises, inside one 268-285 ns sample,
  so the two often read as one cycle; the extra /BE work slows the loop.
