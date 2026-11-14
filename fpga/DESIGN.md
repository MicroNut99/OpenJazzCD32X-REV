# Jazz Jackrabbit CD32X REV on the EverDrive PRO's FPGA: design

**Status, 14 Nov 2026: the engine and its core are built; the game does not use them yet.** The engine is the CD32X
cart compositor, shared with Hurrican CD32X and SuperTux CD32X REV: the specification, the reference model, the
Verilog, its checks and the Quartus builds are in the CD32X BootCamp's
[`cartfx/`](https://github.com/MicroNut99/CD32X-BootCamp/tree/main/cartfx). This page is Jazz's part: what the
engine would draw here, how, and what changes in the port. Nothing below is measured on the console yet; every frame
time is an estimate until step M0 measures it.

## The core

Jazz's build of the engine: `KEY1` 0x584A, ID 0xC2. `mega-pro.rbf` v2.2 (13 Nov 2026), md5
`7c3947b7c34cefa39e30260b8fe92b87`: 7,446 logic elements (48 %), timing met at every corner (worst setup slack
+4.833 ns). It goes beside the EverDrive launcher (`EDCD_OPENJAZZ<n>.32X`) on the SD card. Without the file, or until
the game sends the key, the cart is krikzz's SSF mapper exactly and REV runs as it does today.

The core's console test, `CFXTEST.32X` (BootCamp `cartfx/test`), runs from the SD card with no CD: it names the core,
checks 8 composed frames word for word and times the copy into the frame buffer. That is step M0 below.

## What the engine draws for Jazz

Today (REV) the Genesis draws the level's background tiles in 61 colours and the 32X draws the sky, the sprites, the
foreground tiles and the panel. On the console the levels run at 18-22 frames a second in caves, 10 or less in open
sky, 3-5 in front of the giant waterfalls (whose foreground tiles 59 and 179 are no longer drawn,
`oj32xNoWaterfall`).

With the engine (variant (b) of the design document, proposed): **the engine draws the level's whole 32X picture in
256 colours**, as Jazz drew it, and the Genesis plane is off. The 32X keeps its ring (384 x 200 in the frame buffer,
moved by the line table, a 167-line view over the 33-line panel); each frame only the cells that changed are
composed and copied:

| Back to front | The engine's form |
|---|---|
| the sky: a colour a row (indexes 156-255; the 32X palette rotates them, so a rotation redraws nothing) | layer 0: a bitmap 4 pixels wide, one row a line, opaque |
| the background tiles, 32 x 32 at 8 bits | layer 1: a tile map of 32, the level's map (256 x 64 cells) UPLOADed into the engine's SRAM from the world pack; a destroyed block is one POKE |
| the sprites: Jazz, enemies, bullets, pick-ups, the sun or the moon | SPRITE, keyed (byte 0 = nothing), flipped as the game flips them |
| the foreground tiles, the waterfalls among them | layer 2: an over tile map of 32 (the same tiles, the foreground map in SRAM) |

The panel stays the SH2's: drawn where it changed, as now.

**The ring with the engine.** The engine's layers are scrolled to a world origin on the 16 x 8 grid (x a multiple of
16, y of 8), so every engine cell is a world cell. The master tells the engine which cells to compose (`CELLS`):
the columns and rows that scroll into view, the cells under each sprite now and two frames ago (the back buffer
shows the frame before last), and any map cell that changed. It copies each composed cell to its place in the ring:
(world x mod 384, world y mod 200).

## The frame (estimates)

| Part | Runs on | Estimate | Basis |
|---|---|---|---|
| the game's logic | master or slave | not measured | the first Jazz number M0 takes |
| the frame's commands | master | about 0.3 ms | 20-40 objects, a cart read (about 0.4 us) a word |
| composing about 200 cells | engine | 3-6 ms | the Verilator runs: 46-50 us a cell with 3-4 layers (BootCamp `cartfx/README.md`) |
| copying them | master | 6-10 ms | about 26 KB at 5-9 SH2 clocks a byte (M0 measures it: `CFXTEST.32X`) |
| panel, line table, swap | master | about 1 ms | |

The engine composes frame n while the master copies frame n-1 (two buffers in the engine's SRAM, `SHOW` picks the
one the window shows). A steady 30 frames a second needs the logic under about 20 ms (two refreshes); 60 needs it
under about 7.

## What changes in the port

- `OJ32X_FX=1`, EverDrive build only (`CART=ED EDCD=1`). The 4 / 8 / 16 MB builds are unchanged.
- **The pack.** Each world pack carries its levels' tiles at 8 bits (32 x 32, 1 KB a tile), the background and
  foreground maps in the engine's entry form (tile in bits 11-0, h-flip in bit 12), the sprites as plain 8-bit
  pictures with 0 for nothing, and the sky's rows. **The last 128 KB of every 2 MB page stay free**: the engine
  answers at cart 0x3E0000-0x3FFFFF once keyed (today the 4 MB layout keeps the saves at 0x3F0000; the EverDrive
  build saves to the EverDrive's save bank, `EDCD_SAVE=1`, and its packer moves the planet area off the window).
- **`jj1levelframe.cpp`** draws through the engine: the tiles, the sprites and the sun become the frame's commands;
  the ring keeps its bookkeeping (`row0`, `xoff`, the line table); the copy replaces the drawing.
- **A level load holds the engine**: no GO between the start of a world's copy into the cart and its end (the engine
  reads the PSRAM while it composes; Metal Slug CD32X's lesson).
- **The key at the level's start**; the status's ID must be 0xC2, or the game draws as today (the fallback).
- **The STATUS line** (the profiler build, `PROF=1`): the engine's own time (counter 4), cells, sprites, accesses
  redone, words lost.
- In variant (b) `genesis_convert.py` and the `GB<k>.PAK` packs drop out of that build; the giant waterfalls come
  back (`oj32xNoWaterfall` off).

## Steps

1. **M0, the console** (no new logic): `CFXTEST.32X` with Jazz's core - the core checked, the copy rate measured. Then
   Jazz's own profile on the console (the logic's time).
2. **M1, the PC build** draws through the reference model (BootCamp `cartfx/model/cartfx.c`): picture for picture the
   same as REV's renderer wherever the content is the same.
3. **M2, ares** (`ares_android` branch `fpga-cartfx`, which knows Jazz's `.rbf` by its SHA-256): the game's side,
   the picture, and the frame rate at console timing; the traps (a full queue, the 68000 writing the cart while the
   engine composes) must stay silent.
4. **M5, the console**: the profiler build's STATUS line photographed, with and without the `.rbf`.

Decisions taken (13 Nov 2026, the owner: "Yes. Move forward"): Jazz first; the engine's source in the BootCamp;
variant (b).
