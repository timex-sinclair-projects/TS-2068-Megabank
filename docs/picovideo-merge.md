# PicoVideo on the Megabank (exploration, draft)

Folding [PicoVideo](https://github.com/timex-sinclair-projects/PicoVideo)
into the Megabank: one card that is the 4 MB memory board, the 2068's
enhanced video output (DVI over an HDMI connector, from the RP2350's HSTX
port, with the 2068's sound in the HDMI stream), and a processor that can
reach the board's SRAM without the Z80. That last part is the reason to merge.
It is what Jon's video and rendering work needs, and two separate cards can't
provide it without a new link between them.

It began with David's question (2026-11-07), and was then pointed at Jon
Becker's writeup *Acceleration options for Megabank, TS-Pico, and a
graphics-integrated accelerator* (email, 2026-11-05). Nothing here is
decided. It sits beside the [specification](spec.md) and assumes its board,
registers and [revision 2](spec.md#bus-mastering-revision-2) notes.

Facts about PicoVideo are from its v4.2 source (`picoTsVid4_2.zip`,
`picoCode/`) and the GAL equations in `v3.0 RC1/PicoVidCupl.zip`
(`PICOVID.PLD`). Figures for Z80 copies and Jon's games are Jon's.

## Contents

- [Summary](#summary)
- [Fast moves: why video belongs on this card](#fast-moves-why-video-belongs-on-this-card)
- [The private SRAM path](#the-private-sram-path)
- [What PicoVideo is today](#what-picovideo-is-today)
- [Two controllers](#two-controllers)
- [The video controller](#the-video-controller)
- [DVI through HSTX](#dvi-through-hstx)
- [Sound](#sound)
- [What moves from PicoVideo's firmware](#what-moves-from-picovideos-firmware)
- [The board](#the-board)
- [Steps](#steps)
- [Open questions](#open-questions)
- [Sources](#sources)

## Summary

**Two RP2350Bs on one card, each with an LDO for its core supply, so no
inductor.**

- **The memory controller** keeps the spec's job: the owner table, /BE, the
  page bits. One new duty: it arbitrates the SRAM between the Z80 and the
  second controller, cycle by cycle.
- **The video controller** is PicoVideo rebuilt on an RP2350B. It watches
  the bus for screen and port writes as PicoVideo does, renders PicoVideo's
  modes, and sends them as 640 × 480 DVI out of HSTX, with an emulated AY and
  the beeper as HDMI audio. It also **reads and writes the 4 MB SRAM
  directly**, in the gaps between the Z80's own accesses to it, so data held
  in the banks reaches the picture without the Z80 copying it.

That second path turns the card into Jon's "graphics-integrated
accelerator" with 4 MB behind it:

- Compressed video stored on the RAM disk, decoded straight into the HDMI
  picture. The Z80 spends nothing on it, and the 2068's own bus carries
  nothing.
- Sprites, tiles, backgrounds and bosses kept in banks, and composed by the
  card from small Z80 commands.
- For the native output, the results are delivered by the Z80 or, later, by
  the same controller as a bus master.

A separate PicoVideo can only see what the Z80 (or a DMA master) writes on
the 2068's bus. It can't see what is in the banks.

**Cost.** The memory side is no longer "as routed". The SRAM's address
lines need an isolating bus switch, /OE and /WE need a private-cycle term,
and the GPIO map gains a two-wire handshake. So the merged card is a new
revision, and the board already routed stays the right first build
([open question 1](#open-questions)).

## Fast moves: why video belongs on this card

There are three ways a byte can reach the picture:

| Path | Who moves it | Rate | Works with separate cards? |
|---|---|---|---|
| **A. Z80 writes** to the screen or a port, which the video controller snoops | Z80 | `LDIR` 21 T a byte, about 160 KB/s with the Z80 doing nothing else. Full ECM screen (12 KB): **73 ms** | Yes: this is what PicoVideo does today |
| **B. Bus-master DMA** into HOME (spec, revision 2) | A controller with /BUSRQ, through the SCLD's normal cycles | Jon's four-clock model: full ECM image **about 24 ms plus overhead**, with the Z80 stopped while it runs | Yes: a separate PicoVideo sees master writes as Z80 writes |
| **C. Card-internal**: SRAM to the video controller's frame buffer | The video controller | **About 1 MB/s at least, while the Z80 runs at full speed** ([below](#the-private-sram-path)). Several MB/s when the Z80 isn't using the banks | **No.** Needs the SRAM and the renderer on one card, or a new private link between two cards |

Path C is what Jon's integrated tier needs. In his writeup:

- **Raw ECM at 30 fps** is 368,640 bytes/s. That is out of the Z80's reach
  (path A), and needs most of path B's budget with the Z80 stopped. Path C
  carries it from the banks with room to spare: 4 MB holds about 11 s of it.
- **A compressed stream** at 2 KiB a frame, 25 fps, is 50 KiB/s. 4 MB holds
  about 82 s of it, and the card decodes it into a back buffer and flips at
  vertical blank. With path A the Z80 spends a third of its time just moving
  the compressed bytes.
- **Sinistar and R-Type:** sprites, masks, construction stages and boss parts
  live in banks (his 80 KiB of Sinistar caches; R-Type's 280–528-cell
  bosses). The Z80 sends positions and states, and the card composes. His
  targets are the Sinistar draw from 8.65 ms to about 1 ms and the R-Type boss
  composition from 352 ms to 10–20 ms. With integrated graphics those
  numbers are local work, and the publication step disappears.
- **TSWriter:** glyphs, text runs and scrolling rendered by the card.

**Jon's caveat stands:** pixels made on the card appear on HDMI only.
The 2068's own output (and anything reading HOME RAM) sees them only if
they are published by path A or B. The card should keep both: integrated
graphics for HDMI, and the same composed results handed back as changed
spans for the native screen.

**With separate cards,** path C needs the same private SRAM access on the
Megabank, *plus* a fast link to the PicoVideo board (a ribbon between two
cards on the extender), *plus* a protocol to run over it. The merge does the
first and removes the other two.

## The private SRAM path

**The problem.** In the routed design the SRAM belongs to the Z80. Its A0–A12 come straight from the bus buffers
(U1/U2), its data goes through U3, and only the page bits come from the
controller. Nothing else can address it. The spec's revision 2 reaches it by
turning the address buffers off, which also blinds the memory controller.

**The windows.** The Z80 leaves the SRAM idle for a large share of every
instruction, and the memory controller knows which cycles those are, because it
already decides each one:

- **Refresh.** Every opcode fetch ends with a refresh cycle (T3–T4, /RFSH
  low, about 570 ns at 3.528 MHz). The board never answers refresh.
  Instructions average 4–11 T, so there are something like 400,000 refresh
  windows a second **even when the Z80 runs entirely from banks**.
- **Memory reads the board doesn't own:** HOME, ROM, EXROM, DOCK. Once
  /MREQ falls and the chunk's owner is not the board's, the SRAM is free
  until the next cycle, several hundred ns. (Not writes or I/O cycles: the
  video controller is reading their data off the same SRAM data lines,
  [below](#the-video-controller).)
- **HALT** (refresh and NOP fetches) and the Z80 waiting on the card.

A 45–55 ns SRAM access plus switching fits two or three times in a refresh
window. At two accesses per window, the floor is roughly **800 KB/s with the
Z80 hammering the banks at full speed**. When it isn't, it's several MB/s. Measure
it on the bench ([open question 3](#open-questions)). Unlike /BUSRQ, the Z80
never waits.

**The hardware.** All on the SRAM side of the buffers:

| What | Routed board | Merged card |
|---|---|---|
| SRAM A0–A12 | The BA0–BA12 nets from U1/U2 | Their own net, joined to BA0–BA12 by a bus switch (2 × 74CBTLV3245, under 1 ns through, about 5 ns to open). Closed normally. **Open during a private access**, when the video controller drives the address. |
| SRAM data | SD0–SD7, U3 and the memory controller | The same, and also to the video controller. |
| Page bits, /CE1s | Memory controller | Memory controller: it puts the private page out during the window. |
| SRAM /OE | Buffered /RD | /RD AND NOT a private read (a gate) |
| SRAM /WE | /WR OR write inhibit | Also low for a private write (a gate); write protect still applies |
| Handshake | — | PRIV_REQ (video → memory), PRIV_GNT (memory → video, also opens the bus switch) |

**One access, step by step:**

1. The video controller has the page it wants, sent over the link as one
   message per 8K, and raises PRIV_REQ.
2. The memory controller sees a free window: a refresh, or a cycle not its
   own. It puts the private page on PA13–PA20 and its /CE1, keeps /BE alone,
   and raises PRIV_GNT.
3. GNT opens the bus switch. The video controller drives A0–A12, pulses the
   read or write, and samples SD0–SD7. Repeat while the window lasts.
4. The video controller releases A0–A12. The memory controller drops GNT,
   which closes the switch, and restores the page bits before the next
   cycle's address can arrive.

The memory controller's PIO path already runs at 250 MHz with a decision per
cycle, so adding "or grant" is one more branch in the same program. The risk
is in step 4: the release must land, every time, before the Z80's next T1.
That is the first thing to prove on the bench.

**Coherence** is software's job, Jon's cooperative handoff: the Z80 gives
the card a range of pages for a job, doesn't map them while the job runs,
and the card reports completion. The hardware only promises that the Z80 and
the card never drive the SRAM at the same time, not that they agree on the
contents. A page the Z80 has mapped can still be read by the card (for
display, say). The result is only as consistent as the program makes it.

**The same path serves:**

- the **RAM disk**: the card can copy files between pages without the Z80's
  mover;
- **revision 2's bus master** (open question 6): reading the source from the
  SRAM is exactly this path. The master writes into HOME are the other half;
- the **TS-Pico's** bulk transfers, if the two cards are ever linked.

## What PicoVideo is today

An RP2040 at 250 MHz, a GAL16V8 and four 74LVC245s, on its own expansion
card with a VGA connector. It keeps a shadow of the 2068's screen RAM, built
from the writes it sees, and redraws the picture from that shadow.

**The bus side** (`coreB/busMain.c`, core 0):

- The GAL raises `PSEL` for a memory write to 4000h–7FFFh
  (`!MREQ & RD & M1 & RFSH & !A15 & A14`) or any I/O cycle with /M1 high.
- The Pico has 8 data pins. Two select pins (`PG0`, `PG1`) steer one of the
  four buffers onto them, in turn: A0–A7, A8–A15, D0–D7, control. The loop
  polls `PSEL`, reads each byte two or three times to let the buffers
  settle, and decides.
- **Writes it follows:** FEh (border), FFh (mode, 64-column ink), F4h (the
  HOME/DOCK/EXROM map, so writes to a chunk mapped away from HOME are
  skipped), ULAplus at BF3Bh/FF3Bh (palette and its own registers 40h–48h),
  E0h–E2h (a hi-res write port), 20h/21h (80-column text).
- **Reads it answers:** ULAplus BF3Bh/FF3Bh, 20h and 21h. It turns its data
  buffer outward (`PDDIR`) and drives the byte.
- It can't see /RESET, and it doesn't see the board's FCh/FDh yet (the spec's
  [PicoVideo](spec.md#picovideo) section has that change).

**The video side** (`coreV/`, core 1): three PIO state machines (hsync,
vsync, pixels) and two chained DMA channels send 640 × 480 at 60 Hz out of
an 8-bit resistor DAC. `displayProc80.c` fills one of two 640-byte line
buffers per scan line: the 2068's 256 pixels doubled to 512, with border
either side. The modes: the 2068's own (32-column on either display file,
hi-res, 64-column), PicoVideo's quad-colour, 64-column with attributes,
80-column text from its own font, ULAplus palettes, and "Speccy black".

**Colours.** The palette constants in `macros.h` are GGGRRRBB (green 80h,
red 10h, blue 02h), the same layout as a ULAplus palette entry, despite
the "RRRGGGBB" comment in `vidMain.c`. One format all the way through,
which helps below.

**Known rough edges.** Polling and multiplexed buffers are why the
firmware reads every byte several times. The V4 readme fixes "rogue
pixels" with faster PLDs. `case 0xF4:` falls through into `HIRES_ADRL` (the
spec notes it).

## Two controllers

**One RP2350B can't do it.** Memory needs 46 of its 48 GPIOs. Video adds 8
HSTX pins (fixed at GPIO12–19, where the routed map has BA4–BA7, the chunk
bits and /MREQ), A8–A15, and a data path. That's about 59 pins before the
private path. The savings the spec lists for revision 2 (an I²C expander, a
multiplexed high address byte) bring it to about 51.

**RP2350A for video?** Its 30 GPIOs leave 22 after HSTX, split into GPIO0–11
and 20–29, so the bus would be multiplexed again, as on PicoVideo. That puts
back what the merge removes, and leaves no room for the private path.

**Two RP2350Bs,** each with the core circuit from v3 except the core
supply: an **ME6211C12 LDO feeding DVDD**, as on the OpenSpand card
(openspand-smd, `card/README.md`). The on-chip regulator is unused (VREG_LX
open, VREG_FB to ground; VREG_VIN and VREG_AVDD stay powered for reset and
brownout), as RP2350 datasheet section 6.3.7 allows. That removes the Abracon
inductor, the part with the thinnest stock, along with its layout rules,
at a cost of about 0.1 W per chip. **Check:** the memory controller runs at
250 MHz as v3 does. A fixed 1.2 V core must hold that clock, or the LDO
needs a higher fixed voltage ([open question 7](#open-questions)).
RP2354B (2 MB flash inside) would drop a flash chip from each, if JLCPCB has
it.

## The video controller

**Pins (RP2350B, a sketch):**

| GPIO | Signals | |
|---|---|---|
| 0–11 | /RESET, /INT, PRIV_REQ, PRIV_GNT, data buffer request, UART to the memory controller (2), HPD, DDC (2), spare (2) | |
| 12–19 | HSTX: lane 2, clock, lane 1, lane 0 (Adafruit's order) | fixed |
| 20–32 | SA0–SA12 | the SRAM's own address net: inputs (watching the bus through the closed switch), outputs during a private access |
| 33–35 | BA13–BA15 | from U1/U2 |
| 36–43 | SD0–SD7 | the SRAM data net, shared with the memory controller and U3 |
| 44–47 | /MREQ, /IORQ, /RD, /WR | from U4 |

GPIO20–47 is 28 contiguous pins in one PIO window (GPIOBASE 16), so one
`in pins, 28` captures address, data and strobes, and the same program can
turn SA0–SA12 around for a private access. /M1 and /RFSH aren't needed to
recognise a write: an interrupt acknowledge and a refresh have neither /RD
nor /WR. The memory controller handles refresh windows.

**Seeing the data.** The snoop reads the data from SD0–SD7, so U3 must pass
it inward on every Z80 write, not just on writes to the board's banks.
Facing inward it drives only the card's own nets, so this is safe. It is a
change to how the memory controller drives `nDOE`. During a private
access U3 stays off. So the memory controller grants only in refresh cycles
and in memory reads it doesn't own, never in a write or an I/O cycle, and
the two uses of SD0–SD7 never meet.

**Answering `IN`.** For PicoVideo's read ports (BF3Bh/FF3Bh, 20h, 21h) the
video controller drives SD0–SD7 and asks for U3 through a second enable,
ANDed with the memory controller's `nDOE` (a 74LVC1G08). U3's direction
already follows /RD. These ports don't collide with FCh/FDh.

**The capture program** waits for /WR low with /MREQ or /IORQ low, samples
after the data settles (a 3.53 MHz write holds data well over 200 ns), and
pushes one word to a DMA ring. A core drains it:

- memory writes to 4000h–7FFFh into the shadow, unless F4h or a bank owner
  says the chunk isn't HOME's;
- I/O writes into the register model: PicoVideo's `switch` as it stands,
  plus FDh/FCh (bank owners) and F5h/F6h (the AY, [Sound](#sound)), with a
  timestamp on each.

**Its other core** is the accelerator: decoders, compositors, text, and
Jon's job model. The interface the Z80 sees (a command port pair, job
descriptors in a mapped page, a status register, an optional /INT when a
job is done) is for a later document. Optional: 8 MB of QSPI PSRAM on the
QMI's second chip select (one GPIO), for assets that don't need to survive
power-off.

**Reset.** PicoVideo can't see /RESET. This controller can, through U4.

## DVI through HSTX

**HSTX** is the RP2350's high-speed serial transmitter on GPIO12–19, with a
TMDS encoder and a command expander. The SDK example `dvi_out_hstx_encoder`
sends 640 × 480 DVI from a frame buffer through a DMA command list, with no
CPU time, and Adafruit's HSTX-to-DVI adapter shows the wiring. An HSTX clock
of 126 MHz, DDR, gives the 252 Mb/s a 25.2 MHz pixel clock needs. 126 MHz is
inside the RP2350's rating: no overclock for the video controller, which
suits a fixed LDO.

**The port of PicoVideo's output:**

- Keep the 640-wide line buffers and `makeScanLine` (`displayProc80.c`).
  Their GGGRRRBB byte maps onto the encoder's three lanes with a rotate and
  a width per lane (green 3 bits, red 3, blue 2).
- Replace the three VGA state machines and their DMA pair with an HSTX
  command list per line: sync and porches as raw control symbols, the
  active pixels as TMDS from the line buffer.
- New modes can render from a full frame buffer instead of the shadow (a
  256 × 192 8-bit frame is 48 KB, two are 96 KB of the RP2350's 520 KB). That's
  where accelerated output and decoded video land.
- 640 × 480 at 60 Hz is the mode every HDMI sink accepts, and the US 2068 is
  a 60 Hz machine. A 50 Hz TC2068 would want 720 × 576 at 50 Hz.

**Frame lock.** The 2068's frame isn't exactly 60.00 Hz. Trim the pixel
clock a few hundred ppm so the HDMI frame starts a fixed number of lines after
/INT, or free-run and accept a repeated frame now and then (PicoVideo
today).

**Signal path.** HSTX drives each pair as two single-ended outputs: series
resistors (270 Ω in the SDK example) to an HDMI type-A receptacle, an ESD
array, +5 V on pin 18 through a fuse, HPD in. DDC is optional.

## Sound

**What there is to hear.** The 2068's sound reaches the TV through its RF
modulator: the AY-3-8912 and the beeper. Neither is on the bus as audio,
but every write that drives them is:

- **The AY:** register select on F5h, data on F6h, clocked at half the CPU
  clock (about 1.76 MHz). The video controller keeps a shadow of its
  registers and **emulates the chip** (ayumi, MIT licence, is small). The
  real AY keeps answering reads (the joysticks on port A); the shadow
  ignores the port A registers.
- **The beeper:** a bit of port FEh, already shadowed for the border
  (bit 4, as on the Spectrum; check the Technical Manual).

**Timing.** Sample playback on the AY and beeper music change state
thousands of times a second. The capture program stamps each write with a
timer, and the audio engine replays them at those times, a frame late (about
17 ms). The accelerator can also mix its own streams: the audio track of a
decoded video, kept on its own clock as Jon warns.

**Not heard:** the cassette input (EAR), read by the SCLD in `IN FEh`.

**Into HDMI.** HDMI carries audio in **data islands** in the blanking
(TERC4-coded packets): an AVI InfoFrame, an Audio InfoFrame, Audio Clock
Regeneration, and Audio Sample packets. At 640 × 480 a line is 31.8 µs, and
48 kHz needs 1.5 samples a line, so one sample packet (up to four samples)
in most lines' blanking is enough. HSTX's TMDS encoder can't make TERC4, so
the islands go out as pre-encoded 10-bit symbols in raw mode within the same
command list. Shuichi Takano's data-island code does the encoding.
hsdaoh-rp2350 sends InfoFrames through HSTX with it, and his PicoDVI fork
sends audio on the RP2040. Audio packets on HSTX itself are still to be shown
([open question 2](#open-questions)).

**"Pseudo HDMI":** no HDCP, no licence, no logo. A DVI monitor may refuse
data islands, so audio is a setting.

## What moves from PicoVideo's firmware

| PicoVideo v4.2 | On the card |
|---|---|
| `coreB/busMain.c`: GAL handshake, `PG` buffer selects, polling, double reads | Replaced: the capture PIO program and a ring drain |
| `busMain.c`: the register `switch`, ULAplus, font and colour-table writes, the read answers | Kept, fed from the ring; FDh/FCh, F5h/F6h added; the F4h fall-through fixed |
| `coreV/displayProc80.c`, `col80.c`, palettes, `vgaFont` | Kept |
| `coreV/vgaPioSm.pio`, the DMA pair in `vidMain.c` | Replaced: HSTX command lists, data islands |
| (none) | New: the private SRAM path, the accelerator's job interface, AY and beeper, HDMI audio, frame lock, /RESET |

The SDK goes from 1.5.1 to 2.x. The register map 2068 software sees stays
PicoVideo's, so its 80-column routines and demos (`testCode/`) run
unchanged.

## The board

The second controller, its HDMI connector and the private path don't fit
the 130 × 56 mm card as routed. Additions:

- RP2350B (or RP2354B) with the LDO core circuit: about 20 × 20 mm, and
  simpler than v3's cluster, with no switcher layout;
- 2 × 74CBTLV3245 bus switches, the /OE and /WE gates, the `nDOE` AND gate;
- an HDMI type-A receptacle, its ESD array and eight series resistors, on
  the edge opposite the fingers;
- the memory controller's core supply moved to an LDO as well.

Options: a taller card (if the extender and any case allow it), parts on
both sides, or the fallback memory's space given back. Power: the second
controller and HSTX add perhaps 60–100 mA plus each LDO's loss; the
TLV1117LV33 has room. The battery path is untouched. The generators make
the re-layout cheaper than it sounds: `gen_mcu.py` twice, a new
`gen_video.py`, `gen_memory.py` with the switch.

## Steps

Each can stop the idea cheaply before the next costs anything.

1. **HSTX DVI from PicoVideo's renderer** on a Pico 2 and an HSTX-to-DVI
   adapter, from a captured screen dump. No 2068 needed.
2. **HDMI audio over HSTX:** InfoFrames, ACR and a test tone into a TV and
   a capture stick.
3. **The private path's timing,** on two RP2350B dev boards and an SRAM on
   a breadboard, against a Z80 bus replayed by a third Pico (or on the 2068
   through the routed Megabank, once built, with a wire to its spare GPIOs):
   grants in refresh windows, release before T1, the measured rate.
4. **AY and beeper** from a logged stream of F5h/F6h/FEh writes, against
   ZEsarUX's sound.
5. **One of Jon's workloads end to end** on the bench: decode a TSVideoCodec
   stream from SRAM into the HDMI frame buffer.
6. **The merged schematic and layout.**

## Open questions

1. **Build the routed board first?** It proves the bus timing (/BE in every
   slot, open question 2 of the spec) for about $27 a board, and none of it
   is wasted: the merged card keeps its memory half. Merging now skips a
   build but puts the riskiest new piece, the private path, on the first
   board. The lean here is to build the routed board, with the LDO change
   if it's cheap to re-route, and make the merged card the next revision.
2. **HDMI audio on HSTX:** data islands work there (hsdaoh); audio packets
   and ACR are shown only on the RP2040 (PicoDVI). Step 2 settles it.
3. **The private path's real rate and margin,** and whether a refresh
   window takes two accesses or three. If the release can't be made safe in
   refresh windows, only the cycles the board doesn't own are left. That's
   still plenty unless the Z80 runs from banks.
4. **The accelerator's interface:** command ports, job descriptors, page
   ownership, completion. Jon's writeup is the starting point.
5. **50 Hz** for the TC2068 and Spectrum software: 720 × 576, or 640 × 480
   with repeated frames?
6. **Bus mastering** (revision 2) belongs on the video controller: it
   already has the private SRAM path for sources and sees A0–A15. It would
   still need the address and control buffers turned outward, and pins for
   /BUSRQ, /BUSAK and CLK. That's about 3; the sketch above has 2 spare.
7. **The LDO voltage** against each controller's clock: 250 MHz for the
   memory controller as on v3, 126 MHz for the video controller. If 1.2 V
   doesn't hold 250 MHz, use a higher fixed LDO or run the memory controller
   slower.
8. **Card size and the HDMI connector's position** in the extender and any
   case.
9. **Which sinks accept it:** TVs, monitors, capture sticks, HDMI-to-VGA
   adapters; with and without audio; with frame-lock trimming.
10. **The beeper bit** of FEh and the AY ports F5h/F6h, against the
    Technical Manual.

## Sources

- Jon Becker, *Acceleration options for Megabank, TS-Pico, and a
  graphics-integrated accelerator* (email to David, Charlie Day and Josef
  Jelínek, 2026-11-05): Z80 copy costs, Sinistar and R-Type figures, the
  three tiers, streaming rates.
- PicoVideo, v4.2 source and the `V4`/`v3.0 RC1` files:
  https://github.com/timex-sinclair-projects/PicoVideo
- OpenSpand card, the LDO core supply:
  https://github.com/timex-sinclair-projects/openspand-smd (`card/README.md`)
- The Megabank [specification](spec.md): GPIO map, Parts, Sharing the bus,
  PicoVideo, Bus mastering (revision 2).
- Raspberry Pi pico-examples, `hstx/dvi_out_hstx_encoder`; RP2350 datasheet,
  HSTX and section 6.3.7 (external core supply).
- Adafruit, *RP2350 22-pin FPC HSTX to DVI Adapter* (lane order):
  https://learn.adafruit.com/adafruit-rp2350-22-pin-fpc-hstx-to-dvi-adapter
- hsdaoh-rp2350 (data islands through HSTX, after Shuichi Takano's code):
  https://cnx-software.com/2024/11/19/high-speed-data-acquisition-raspberry-pi-pico-2-hstx-interface-cheaper-hdmi-to-usb-3-0-video-capture-dongle
