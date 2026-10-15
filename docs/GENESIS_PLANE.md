# The Genesis plane (REV)

The Genesis VDP draws a level's background tiles; the 32X draws only what moves or lies in
front: the sky, the sprites, the foreground tiles and the status panel. The tiles were the
32X's biggest drawing job (new rows and columns of 32 x 32 tiles, and the background under
every sprite, every frame).

## What each side draws

| Layer (back to front) | Drawn by | How |
|---|---|---|
| sky / plain background | 32X | colours 156-255 (sky) and 127 (plain background) **without** bit 15 |
| black cells (tiles with a black background) | Genesis plane B | one solid pattern |
| background tiles | Genesis plane A | the level's tiles cut into 8 x 8 patterns |
| sprites, foreground tiles, panel, sun | 32X | every other colour **with** bit 15 |

The 32X priority bit is clear (Genesis in front); a 32X colour with bit 15 set is in front of
the Genesis, one without it shows only where both Genesis planes are see-through. The 32X's
ring (the line-table scroller, jj1levelframe.cpp) now holds only the sky: its strips are plain
fills.

## Conversion (on the PC, at build time)

`tools/dump_levels.sh` runs every level in the PC build with `T32X_DUMP_GB` and writes the
level's tiles, palette, map and the palette entries its sprites use. `tools/genesis_convert.py`
makes from one level:

- 4 palettes of 15 colours (Genesis 9-bit colour). Palette 3 entry 15 is black (black cells).
  The colours of the palette rotations the level runs (`RotatePaletteEffect`: 112-115, 116-123,
  124-126, 132-139 ...) get entries of their own; the 68000 rotates them every vblank at the
  game's speed.
- 8 x 8 patterns, 4 bits a pixel, stored once (mirrored copies merged), at most 1600 (what video
  memory has room for); a level with more has its most similar patterns merged. The whole tile
  set when it fits (so blocks the game changes during play are there too), else the tiles the
  map uses.
- 16 name-table words per tile, the map (tile per cell, 255 = nothing: foreground tiles), the
  black cells (a bit map).

`tools/genesis_pack.py` puts a world's levels into one pack, `GB<k>.PAK` (k = the world pack's
number). Levels it leaves to the 32X: palette effect 8 / 9 (parallax backgrounds made of palette
rotation) and 11 (water darkening), whose colours change with the camera, and levels with more
rotating colours than 4 palettes hold. Jazz CD: 48 of 78 levels.

## At run time

| When | SH2 (port/genbg.c) | 68000 (cd32x/subcpu/genbg_md.s) |
|---|---|---|
| a world is entered | `GB<k>.PAK` read from the disc into the cart at 0x080000 (free space below the common pack), mounted after the world pack | |
| a level is loaded | finds `GENESIS<level>.BIN`, Sub-CPU command 67 / 1 with its cart offset | `genbg_load`: patterns into video memory, palettes, tile table, map and black cells into work RAM (0xFF8000-0xFFF000), VDP set up, display off |
| the level's first frame | command 67 / 2 with the camera | `genbg_start`: the planes filled for the camera, the display on |
| every frame | COMM4 = camera x, COMM6 = camera y, fade, hide - written with the swap request | every vblank: scroll to COMM4 / COMM6, colours (rotated, faded) into CRAM, the columns / rows that came into view |
| a block is destroyed | command 67 / 4 (cell, tile), sent after the logic | `genbg_cell`: map and plane updated |
| another screen (pause menu's save, load, set-up) | COMM6 bit 15 | the Genesis display off |
| the level is left | command 67 / 3 | `genbg_off`: the listener's own VDP set-up, font and colours back |

The fade (level start, level end, pause): the SH2 compares the colours shown with the level's
own and hands the 68000 a level 0-7 towards black or white; the 68000 fades its colours with it.

The vblank handler never reads the cart (only work RAM and COMM registers), and the listener's
commands that use the VDP now run with interrupts off, so the handler cannot split their VDP
writes.

### Video memory

| Address | Holds |
|---|---|
| 0x0000-0x9FFF | patterns 0-1279 |
| 0xA000-0xAFFF | plane B (64 x 32) |
| 0xB000-0xBFFF | patterns 1280-1407 |
| 0xC000-0xCFFF | the listener's text table (not shown) |
| 0xD000-0xDBFF | patterns 1408-1503 |
| 0xDC00 | sprite table (one empty sprite) |
| 0xE000-0xEFFF | plane A (64 x 32) |
| 0xF000 | h-scroll table |
| 0xF400-0xFFFF | patterns 1504-1599 |

### The camera (R2)

With the Genesis plane on, a frame runs as OpenJazz does: the logic steps first, then the camera
from the state after them, then the picture. The J23 order (the camera from the state before
the steps, the steps on the slave SH2 beside the master's ring drawing) put Jazz in a different
place on the screen in each picture - his movement during that frame's steps - so he jittered
against the level (measured on the PC at the console's 20 frames a second: on-screen movement
from picture to picture 5.0 px vertically on average, up to 42; now 0.9, up to 10, which is
the camera catching up). The ring is only the sky now, so the slave had nothing to overlap.
Levels the 32X still draws keep the J23 order (the speed it gives them matters more there).

## Command numbers (CD32X command ledger additions)

| Where | Number | Meaning |
|---|---|---|
| SH2 -> Sub-CPU | 67 | Genesis plane: COMM2 = 1 load, 2 start, 3 off, 4 cell; COMM8/10 = argument; reply COMM12 |
| Sub-CPU -> 68000 | 36 | `set_vsram` (as in the ledger additions) |
| Sub-CPU -> 68000 | 37-40 | `genbg_load`, `genbg_start`, `genbg_off`, `genbg_cell`; the listener's end marker is 41 |
| COMM4 / COMM6 | - | camera x / camera y (0-10), fade level (11-13), towards white (14), hide (15); written by the SH2 only |

## Checks

- `tools/genesis_test_rom.sh`: the 68000 code in a plain Genesis test ROM (COMM registers and
  the bank register moved to work RAM, the level file in ROM), run in PicoDrive with a moving
  camera, a jump and a changed block; every dump drawn as the VDP draws planes A and B and
  compared pixel by pixel with the level file (`tools/genesis_check_vdp.py`), and the longest
  vblank pass measured.
- `make -C host SCROLL=1` builds the PC version with the console's renderer
  (`host/openjazz32x_host_scroll`); `T32X_SLAVE_LOGIC=1` runs its J23 order of logic and
  drawing, `T32X_FRAME_VBLANKS=3` makes each frame last 3 vblanks (the console's 20 fps),
  `T32X_TRACK=1` prints Jazz's place on the screen in each picture.
- The PC build keeps a model of the 68000's plane (port/plat_host.c) and saves its frames as
  the console mixes the two pictures; `tools/genesis_preview.py` shows a level's plane beside
  the original.
