# TS-2068 Megabank

A battery-backed 4 MB RAM board for the Timex Sinclair 2068, built the way
Timex meant memory to grow: as **expansion banks** that can take over any 8K
chunk of the address space. It is a card for the expansion bus, for owners
of the TS-Pico v3.

It gives the 2068:

- **Paged memory:** any 8K page of the 4 MB, mapped into any chunk with two
  `OUT`s, at full memory speed.
- **A RAM disk:** `SAVE "r:name"`, `LOAD "r:name"`, `CAT "r:"` and the rest,
  kept through power-off by a battery.

**Status:** design. Nothing is built yet. Start with the
[specification](docs/spec.md).

## Where the work lives

| Part | Repository |
|---|---|
| The board: schematic, PCB, CPLD logic | here |
| The TS-Pico ROM (detection, the RAM disk's copy routine, `r:`, the Timex bank API) and the v3 firmware (bus sharing, the RAM disk's file system) | [tspico-firmware-build](https://github.com/timex-sinclair-projects/tspico-firmware-build) |
| PicoVideo's support (ignoring screen writes to mapped banks) | [PicoVideo](https://github.com/timex-sinclair-projects/PicoVideo) |
| The emulator model | [zesarux-tspico](https://github.com/timex-sinclair-projects/zesarux-tspico) |

## The first test

Before anything else: does a bare 2068 answer the board's ports, FCh and
FDh? With nothing plugged in:

```
OUT 252,2: OUT 253,2: PRINT IN 252, IN 253
```

The border should not change, and both values should be 255 with no keys
pressed. If not, the registers move to another port pair.

## Licences

| What | Licence | File |
|---|---|---|
| Hardware design files: schematic, PCB, and the CPLD logic (its sources and fuse maps) | CERN Open Hardware Licence v2, Permissive (CERN-OHL-P-2.0) | [LICENSE-hardware.txt](LICENSE-hardware.txt) |
| Software: tools and test programs | MIT | [LICENSE](LICENSE) |
| Documentation (`docs/`, this README) | Creative Commons Attribution 4.0 (CC BY 4.0) | [LICENSE-docs.txt](LICENSE-docs.txt) |
