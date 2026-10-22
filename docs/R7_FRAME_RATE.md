# R7: frame rate work (20 Oct 2026)

Measured in the ares emulator, not on the console. The
console readings so far were all taken before R7.

## What changed

| Change | Where | Why |
|---|---|---|
| **Busy-waits stay off the bus.** The slave SH2's wait for a job, the master's wait for the slave, and the waits on 32X registers (swap, vblank, palette access, frame buffer access) now rest between two looks in a loop that touches only registers and the cache. `comm_pause` no longer counts with a variable on the stack (a write to SDRAM every turn). | `port/plat_mars.c` `bus_rest` | The two SH2s share one bus. With the Genesis plane on, the slave has no work in a level and spun on a cache-through address all frame: in ares about 460,000 SDRAM reads per picture, every one competing with the master's cart, SDRAM and frame buffer accesses. Now about 40,000. |
| **The pad is asked for ahead.** The pad request (CMD 48, with a waiting sound effect) goes out when the frame is handed over, before the frame pacing wait; the answer is read at the next frame's pad poll. Any other command first waits for that answer. | `port/plat_mars.c` `pad_ask`, `pad_collect` | The SH2 no longer waits for the Sub-CPU / 68000 round trip in the middle of the frame; that round trip happens while it waits for the swap anyway. The pad is read at the end of the frame, as fresh as before. |
| **The sun.** Drawn a pair of pixels at a time from SDRAM: one 16-bit frame buffer read and one write a pair, the Genesis plane's tile looked up once per 32 pixels. | `jj1levelframe.cpp` `drawOrb` | Before: three frame buffer accesses and a function call per pixel, reads and writes alternating. Same pictures (PC build, 150 frames compared). |
| **No division per ring row.** The ring row of the camera is worked out once a frame. | `port/sdl_oj.cpp` `smap.r0` | `% 200` was a call to `__sdivsi3` (a long loop) for every row drawn. |
| **The palette search only when it can change.** The border and profiler colours are searched for again only when an entry they depend on changed (the sky's rotation every frame does not). | `port/sdl_oj.cpp` `SDL_SetPalette` | Two loops over 256 entries every frame. |
| **The FPS counter shows below 10.** It is worked out from the real time between frames. | `oj/src/level/level.cpp` | The level's clock moves at most 100 ms a frame, so the counter could not show less than 10 fps: "10" meant "10 or less". |

## In ares (pictures shown per second)

`ARES_32X_TIMING=console` is the ares cost set closest to the console readings.

| Place (01·1) | Console, R5 | ares upstream, R5 → R7 | ares `console`, R5 → R7 |
|---|---|---|---|
| start (sky, sun) | 10 or less | 30 → 30 | 15 → 20 |
| cave (waterfalls, signs) | 18-20 | 30 → 30 | 12-13 → 16 |

## Build

The default disc name is now R7 (`R7-16MB`, `R7-16MB-PROF`, `R7-8MB`).

## Loading (R7, measured in ares)

**The drive reads on.** `cd32x/subcpu/main.c` + `scd.c` (STREAM):
- The old reads sent the drive back to its sector for every 32 KB chunk (`ReadSectorsSUB`:
  CDCSTOP + ROMREADN each time), so it searched once per chunk: about 41 KB/s, the same as
  measured on the console.
- Now one ROMREADN runs to the end of the file. Each sector's header is checked; a missed or
  wrong sector starts the drive again at the sector wanted.
- While the 68000 copies a chunk to the cart and reads it back, the Sub-CPU takes the sectors
  that arrive into a 64 KB read-ahead buffer (`st_buf`). The read-back compare runs four bytes
  at a time.
- Loads now run at about 104-145 KB/s.

**The 16 MB cart is filled on the first boot.** `cartcache_fill`, called from `plat_mars.c`:
- **What is copied:** the world packs (with their Genesis plane packs) that are not in the cart
  yet, in play order, until the pages are full. That is about 9.4 MB, ten or eleven worlds.
- **The screen:** the splash shows "COPYING TO THE CART - START: SKIP" and the bar.
- **Start** stops the copying; the rest load when they are played, as before.
- **Later boots** copy nothing. Packs already in the cart are checked against the disc when
  they are used, as before.
- **Worlds that do not fit** load from the disc the first time they are played (now about 2.5
  times faster). Each one replaces the page used longest ago.

| ares, blank 16 MB cart | R5 | R7 |
|---|---|---|
| first boot to the menu | ~83 s (engine + common pack) | ~150 s (also about ten worlds copied) |
| a world not in the cart | ~22 s | ~8-10 s (estimated from the read speed, not timed) |
| a world in the cart | ~5 s (disc check) | ~5 s |
