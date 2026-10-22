# R8: compressed packs in the 8 / 16 MB cart (21 Oct 2026)

**R11: off by default.** `LZ=1 CART=16 bash tools/make_disc.sh` builds it (disc name `R11-16MB-LZ`).

Measured in the ares emulator (`ARES_32X_TIMING=console`),
not on the console. R8 = R7 + this. The 4 MB build is unchanged.

## Why

The 16 MB cart held about ten of the 41 worlds uncompressed (R7). The packs are mostly tile and
sprite pictures that compress well:

| Packs on the disc | Files | Plain | Compressed |
|---|---|---|---|
| world and bonus packs (`OJ*.PAK` -> `OZ*.PAK`) | 71 | 60.9 MB | 20.3 MB |
| Genesis plane packs (`GB*.PAK` -> `GZ*.PAK`) | 41 | 5.0 MB | 2.4 MB |

About a third of the size, so the first boot copies 27 of the 41 worlds into the cart.

## How

- **The format** (`tools/ojlz.c`, run by `build.sh` for CART builds): "OJLZ", the plain length,
  the number of blocks, each block's packed length; then 32 KB blocks, each LZ4 on its own.
  `ojlz -t` packs, unpacks and compares.
- **The disc** carries both: `OJ` / `GB` (the 4 MB build) and `OZ` / `GZ` (the 8 / 16 MB builds).
- **The Sub-CPU unpacks** (CMD 69, `cd32x/subcpu/main.c` `unpack_pack`): it reads a few blocks
  from the cart, unpacks them into Word RAM (68000 LZ4 decoder, `lz4_block`), and has the
  68000 copy them into the home slot (cart page 1), 16 KB at a time with `movem` (listener
  routine 41).
- **The fill** (first boot) copies the compressed packs, in play order, until the pages are full.
- **Disc stamp** (CMD 70): a hash of the disc's volume descriptor is kept with each pack in the
  cart. A pack copied from the same disc is not compared with the disc again.
- **The root directory** is read once, so opening a file is ~2 frames instead of ~44.
- **The loading screen** is drawn once per load (it was drawn again for the Genesis plane pack:
  1.8 s more).

## Times (ares)

| | R7 | R8 |
|---|---|---|
| first boot, blank 16 MB cart, to the menu | ~150 s (10-11 worlds copied) | ~167 s (27 worlds copied) |
| a world in the cart | ~5 s | ~6 s (3.1 s of it unpacking) |
| a world not in the cart (from the disc) | ~8-10 s (estimated) | ~12.7 s (5 s reading, 3.1 s unpacking) |

A world not in the cart is kept in the cart after it is loaded (the page used longest ago
makes room), so the next time it is a world in the cart. The world just played stays unpacked
in the home slot: its second level, or starting it again, does not unpack again.

Checked in ares: 01-1 from the cart and from the disc, episode 3 from the cart, a bonus level
from the disc, the 4 MB build, the 8 MB build (first boot ~90 s to the menu, then 01-1 from the
cart: 5.7 s).
