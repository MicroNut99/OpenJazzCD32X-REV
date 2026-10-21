# R9: the bonus levels' floor on the Sega CD and the Genesis (21 Oct 2026)

Measured in ares (MicroNut99/ares_android, branch `cd32x-bus-jh`, `ARES_32X_TIMING=console`),
not on the console. R9 = R8 + this. All three builds (4, 8, 16 MB).

## Before

The bonus levels drew their floor on the 32X: 320 x 85 pixels, each one a multiplication and
a read of the tile graphics from the cart. **2-3 pictures a second.**

## Now

| Bonus maps | Floor | Pictures a second (ares) |
|---|---|---|
| 24 of 30 | one layer, 16 colours | **30** |
| 6 of 30 (BONUSMAP.008, 009, 013, 015, 016, 017) | two layers, 31 colours | **20** |

The floor is the Mega CD's rotation / scaling chip's work, shown by the Genesis VDP; the 32X
draws only the sky, the horizon, the sprites and the panel.

## How

The floor is what the chip was made for: each line of it is a straight line through the map
(OpenJazz `JJ1BonusLevel::draw`: start point and step per line), which is exactly the chip's
"trace vector" per line.

**At build time** (`tools/bonus_md/make_bonus_md.py`, run by `build.sh`): for each bonus map a
`GB8nn.PAK` (the same route as the levels' Genesis plane packs) with `BONUSMD.BIN`:
- the floor's 32 x 32 tiles as the chip's stamps (4 bits a pixel);
- the colours: Jazz's palettes are ramps of 16 shades; each ramp the floor uses is cut into runs
  of neighbouring shades, and the colours go one by one to the ramp where one more run removes
  the most error from the screen (weighted by how often each shade is seen in 300 sampled
  views). One layer = 16 colours; two layers = 31. A map gets two layers when one layer is
  clearly worse on average, or when a shade that is often on screen is far off (a white stripe
  turning black, a pattern going flat);
- the palette rotations (track sides, springs, spinners: three 16-colour ranges at the same
  speed) as 4 phases of the stamps that use them, picked per frame;
- the 256 x 256 tile map.

**Each frame:**
1. The SH2 sends the player's position, angle and the rotation's phase (Sub-CPU command 71) at
   the start of the frame and draws the rest of the picture meanwhile.
2. The Sub-CPU (`cd32x/subcpu/main.c` `bmd_frame`) fills an 8 x 8 stamp map around the floor
   the view shows (it is always less than 80 pixels from the tile 45 pixels ahead of the
   player; checked for 20,000 random places and angles), writes the 85-line trace and starts
   the chip after the first lines (the chip reads the trace a line at a time). Word RAM is in 2M
   mode during the bonus level (the chip needs it); every other command first returns it to 1M.
3. The 68000 (`cd32x/subcpu/bonusmd_md.s`) copies the floor into video memory by DMA at the
   vblank of the 32X's swap, so the floor and the sprites change together.

**The display trick:** a DMA while the Genesis display is on gets some 18 bytes a line; with
the display off, some 200. The 68000 switches the display off at the vblank and an H interrupt
at line 126 switches it on again. Those lines are the 32X's sky anyway, so the 14 KB copy (28 KB
with two layers) is done long before the floor's first line, with one buffer per layer.

**On screen:** plane A (layer A, palette line 0) in front of plane B (layer B, line 1). The 32X
fills the floor's area with one palette index (the "hole", without the priority bit; all its
other colours get it), so the Genesis planes show there and the 32X's sprites stay in front.
Where both planes have colour 0 the 32X's hole colour shows: one more colour.

## Times (ares, sub-CPU clocks at 12.5 MHz)

| | |
|---|---|
| the chip, one layer (320 x 85) | 10.9 ms |
| the trace and the stamp maps | ~2 ms, under the chip |
| the DMA, one layer | ~70 lines of the vblank / display-off time |
| Word RAM per frame, two layers | ~34 ms: just over two frames, so 20 a second |

## Checked in ares

- The same view, drawn by the old 32X floor and by the chip (test build `OJ32X_BMD_CHECK`):
  map 0 (one layer) and map 9 (two layers) match; map 9: 1.9 % of the floor's pixels differ
  noticeably (colour rounding).
- The floor's first line at the right screen line (the H interrupt's timing).
- Leaving a bonus level (Quit) and starting it again; a normal level after it, with its
  Genesis plane.
- The 4 MB and the 16 MB builds.

Test builds: `EXTRA_DEFS="-DOJ32X_BMD_CHECK"` (from the 700th frame the 32X draws the floor
again), `-DOJ32X_BMD_TEST_MAP=n` (every bonus level is BONUSMAP.n).
