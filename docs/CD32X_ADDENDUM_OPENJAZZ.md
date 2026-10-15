# CD32X – Addendum: lessons from the OpenJazz (Jazz Jackrabbit) port

An addendum to:
- **[BOOT]** *Kobo Deluxe Sega CD32X – How It Boots and Runs* (`KOBO-CD32X_BOOT_PROCESS`)
- **[LEDGER]** *Sega 32X/CD Boot Sequence and Sub-CPU Command Ledger*
- **[OPENLARA]** *CD32X – Addendum: lessons from the OpenLara (Tomb Raider) port*

OpenJazz is a fourth kind of CD32X game: a **large C++ engine** (about 420 KB of SH2 code) that
runs from the RAM cart, keeps the Sub-CPU busy for its whole life, and uses the cart as its
**main memory for read-only data** – 17.5 MB of game data, 79 levels, 30 bonus levels, a second
game (Holiday Hare '95), CD music, PCM effects and saves. Everything below was confirmed on real
hardware (Sega CD + 32X + 4 MB RAM cart) unless marked **(PC only)** or **(not confirmed)**.
Build names (J4 … J19) refer to the project log `OPENJAZZ_CD32X_HANDOFF.md`.

---

## 1. The model

| | Kobo [BOOT] | OpenLara [OPENLARA] | OpenJazz (this addendum) |
|---|---|---|---|
| SH2 program runs from | SDRAM | cart | cart |
| Language / runtime | C, no C library | C++ | C++ with newlib, libstdc++, exceptions |
| 68000 after boot | listener | listener | listener |
| Sub-CPU after boot | service loop | service loop | service loop |
| Game data | one 200 KB file in SDRAM | one level at a time in the cart | a common pack + one level/bonus pack at a time in the cart, all pre-decoded on the PC |
| Slave SH2 | parked | renderer | idle (drawing on it was measured and gave nothing, §8.4) |
| Second game on the same disc | – | – | yes, as an episode (§2.4) |

---

## 2. Boot path

One proven boot path, never two: OpenLara's SH2 loader and Sub-CPU program, unchanged except for
tagged switches (`OJ-J4B`, `OJ-J5`, `OJ-J11`, `OJ-J13C`). The engine is just the file the loader
copies.

1. **[BOOT] stages 0–5 unchanged.** The SH2 program booted from the frame buffer is the small
   loader (`CDLOADER`), not the game.
2. **The stage 6 handshake belongs to the loader.** The engine must not repeat `0x0AAA`/`0x0BBB`.
   It posts `0x4B10` in COMM12 once running, which stops the Sub-CPU's status redraws.
3. **The loader copies the engine** (file 20) into the cart at offset 0 (CMD 38 + CMD 46, 32 KB
   chunks with read-back), shows the splash (CMD 51) with a bar, then jumps to the engine.
4. **The engine starts from the cart.** Its crt0 copies `.sdcode` + `.data` to SDRAM, clears
   `.bss`, runs the C++ constructors and calls `main`.

### 2.1 Quick start
The loader's three-chunk compare (CMD 47 on first, middle and last chunk) exists but is disabled in
OpenLara (`if (same && 0)`, see [OPENLARA] §2.3). Enabled (`OJ-J13C`), it skips the engine copy when
the cart already holds it. The engine does the same for its common pack (CMD 47 on three chunks
plus the pack header and length in the cart). **A+B+C held at power-on** (read with CMD 48) forces
both copies. Result on hardware: the second boot reaches the main menu in seconds instead of about
80 s. [OPENLARA] §2.3's warning stands: an interrupted copy can leave a mixed cart that passes the
three probes – A+B+C is the remedy.

### 2.2 Diagnostic text switches
One per program, documented at the top of each file: `OJ_DIAG_TEXT` (loader), `KOBO_DIAG_TEXT`,
`KOBO_BOOT_TEXT` (Sub-CPU), `SCD_DIAG_TEXT` (`scd.c` printed green text outside `print_diag`, so
`KOBO_DIAG_TEXT` alone never hid it), `OJ32X_DIAG_TEXT` (engine). Release: Chilly's white lines
only, Genesis display off when the splash appears.

### 2.3 Splash rules
Never use palette index 0 or the value `0x0000` in a prepared picture (see §6.1); black is index
252 = `0x0400`. The splash stays up through the engine's start-up (`OJ32X_SPLASH`), and the boot
test screen is shown only when a check fails (`OJ32X_BOOT_TEST 0`).

### 2.4 A second game on the same disc
Holiday Hare '95 is a complete data set whose files have the same names as Jazz Jackrabbit CD's
(`LEVEL0.050`, `BLOCKS.050`, `MAINCHAR.000` …) with different contents. A start-up choice screen
was tried and removed (it costs a button press on every boot and broke the A+B+C path). What
works: the game appears as an **episode** of the main game (last line of the episode menu). While
it runs, a game flag selects its level packs (`OJ6xx`), its sound effects pack and its save slots;
its levels' packs are baked against the main game's common pack, and the one shared file that
differs (`MAINCHAR.000`) is forced into its packs (`T32X_BAKE_UNPACKED`). Level packs are searched
before the common pack, so the right version always wins.

---

## 3. The RAM cart as main memory

The SH2 reads the cart (`0x02000000` cached, `0x22000000` cache-through) but cannot write it
([OPENLARA] §2.1). So everything the game needs is prepared on the PC and copied in from the disc.

### 3.1 Cart layout

| Cart range | Size | Holds | Written by |
|---|---|---|---|
| `0x000000`–`0x06E000` | 0.43 MB | engine (code runs from here) | loader, CMD 46 |
| `0x06E000`–`0x100000` | 0.57 MB | free | – |
| `0x100000`–`0x280000` | 1.5 MB max | common pack `IMAGE8.RAW`: menus, fonts, panel, demos (1.43 MB) | engine, CMD 38 id 8 + 46 |
| `0x280000`–`0x3F0000` | 1.4 MB max | the current pack: a world (both levels of a planet), a bonus level, or a Holiday Hare '95 world | engine, CMD 38 id 200+k + 46 |
| `0x3F0000`–`0x400000` | 64 KB | save area, 4 KB pieces (§10) | engine, CMD 58 + 61 |

### 3.2 Packs
`port/pack.h`: header `OJPK`, a file table, **pictures already decoded on the PC** (found by size +
FNV-1a hash + `memcmp`), and **file pictures** – a decoded RLE block keyed by file name and byte
position, so the engine never decodes it. Game files are opened in place with newlib `fmemopen`
over the cart. Tile sets, level maps, bonus maps, the status panel, fonts and sprites are used
straight from the cart; SDRAM only holds what changes.

| Pack | Content | Count / size |
|---|---|---|
| common | menus, fonts, panel, demos, file catalog | 1, 1.43 MB |
| world `OJ<world*10>` | both levels of a planet, its tiles, sprites, planet screen | 39 + 2 (Holiday Hare '95), 0.5–1.49 MB, 36 MB |
| bonus `OJ<800+map>` | one bonus map with its graphics and decoded 64 KB map blocks | 30, about 770 KB, 23 MB |

**Baking.** The PC build plays the game (`T32X_BAKE=<file>`) and records every file it opens and
every picture it builds; `T32X_PACK=` runs the PC build from packs alone and must give identical
frames. World packs merge the levels of one planet (`T32X_BAKE_MERGE=1`). Blocks larger than one
picture line (65,535 bytes) must be baked as 256-wide pictures – missing that left the bonus maps'
64 KB blocks out and ran the console out of memory.

**Mapping on the Sub-CPU side.** CMD 38 id 1–99 -> `IMAGE<n>.RAW` at slot n × 128 KB; ids
200–1199 -> `OJ<id-200>.PAK` at slot 20 (`0x280000`) (`OJ-J5`).

### 3.3 Loading speed
About **1 s per 32 KB chunk** (~30 KB/s) against ~150 KB/s for the drive: the cost is per command
(seek, 68000 copy, read-back), not the data. Mitigations in place: world packs (the second level
of a planet needs no load), no read-back for packs (CMD 46 bit 15), the quick start. Not done:
larger chunks per command, compression unpacked by the Sub-CPU, preloading into a second slot.

---

## 4. Sub-CPU commands used (additions to [LEDGER])

| Cmd | Use | Parameters / reply | Notes |
|---|---|---|---|
| 38 | open file | COMM2 = id -> length in COMM8 (32-bit) | id also selects the cart slot for 46/47 |
| 45 | play CD track | COMM2 = track, bit 15 = once | once + the WAV lengths compiled into the engine -> "next track" in the CD player |
| 46 | chunk -> cart | COMM2 = chunk, bit 15 = no read-back; COMM8 = file length | COMM12 0 = ok |
| 47 | compare chunk with cart | as 46, nothing written | quick start |
| 48 | pad (+ sound effect) | pad in COMM8; COMM0 high byte = effect | one effect per request; 6-button bits arrive (X/Y/Z/Mode) |
| 51 | file chunk -> frame buffer | COMM2 = chunk | every loading screen; the SH2 must give the frame buffer to the 68000 first |
| 52 | Genesis display on/off | COMM2 | off = register 1 `0x8134` |
| 53 | load PCM pack | COMM2 = n -> `SFX<n>.BIN` | 00 main game, 01 Holiday Hare '95 |
| 54 | stop CD audio | – | before every data read |
| 58 / 61 | save area | 58: one word into the Sub-CPU's 4 KB buffer; 61: buffer -> cart `0x3F0000` + n × 4 KB, read back | saves and settings (§10) |

Quirks: in `main.c` both `CMD_CHUNK_COMPARE` and `CMD_CART_TO_FB` are 47 (the second is
unreachable); CMD 38 reuses COMM8, so after CMD 51 the file must be reopened before CMD 46.

---

## 5. SH2 runtime: C++, newlib and about 200 KB of heap

### 5.1 Memory budget
256 KB SDRAM: about 5 KB `.sdcode`/`.data` (more with fast code, §8.2), `.bss` (10.5 KB panel copy
with `OJ32X_PANEL_DIFF`), 16 KB master stack, 2 KB slave stack; the heap is the rest (about
200–218 KB depending on switches). What made the levels fit:

| Change | Saved |
|---|---|
| level grid 8 -> 3 bytes per cell (event timers in a 256-entry table) | 80 KB |
| tile set, level map and status panel used decoded from the pack | 490 KB + 32 KB + 46 KB of temporary buffers |
| game screen = the 32X frame buffer (§6) | 64 KB |
| bonus map (2 × 64 KB) from the pack, collected items in an 8 KB bit table | 120 KB |
| no large arrays in `.bss`; saves without SDRAM buffers | 8 KB |

Measured peaks on the PC with the 32X code paths, minus the PC's own 64 KB screen: levels about
180 KB, bonus levels about 70 KB, Holiday Hare '95 levels about 190 KB.

### 5.2 Fragmentation, not leaks
Crashes after several deaths were a 46 KB request failing with the heap top at 195 of 218 KB;
loading a level twice leaves only +240 bytes. newlib's allocator had no 46 KB hole left. Rule:
remove big temporary allocations instead of hunting leaks.

### 5.3 System calls (`port/mars_syscalls.c`)

| Call | What went wrong | Rule |
|---|---|---|
| `_fstat` | "character device" for every fd, `st_blksize` uninitialised -> `fmemopen` buffers sized from garbage | unknown fd -> `EBADF`; zero the struct |
| `access` | "no such file" for the directory `/` -> OpenJazz added no save/config path, saving did nothing | directories exist and are writeable |
| file-name copy | `OPENJAZZ.CFG` (12 characters) cut to 11 | allow 12 |
| `getcwd`, `_open`…`_close` | missing ones clash with newlib's stubs over `errno` | provide all |

### 5.4 C++ failure modes
An uncaught exception ends in `abort()` – a bare "exit code 1". OpenJazz only catches `int`, so
`std::bad_alloc` is never caught. `std::set_terminate` and a replaced `operator new` now name the
cause on a red screen ("OUT OF MEMORY: NEW OF n BYTES (HEAP TOP …)", "C++ TERMINATE: UNCAUGHT
EXCEPTION <type>").

### 5.5 Slave SH2
Usable (job in SDRAM through the cache-through alias `0x26…`, the slave purges its cache with
`CCR = 0x11` before each job; the master's cache is write-through). Never run `malloc` on both:
newlib's allocator is not reentrant.

---

## 6. Frame buffer and display (additions to [BOOT] rules 4–5)

The game draws straight into the frame buffer. Rules learned:

1. **`0x0000` is see-through for every palette that reaches CRAM, including the game's own.**
   Black in the game's palettes let the Genesis layer (leftover VRAM) show as a strip at the right
   edge. Map `0x0000 -> 0x0400` when loading CRAM.
2. **Border lines show one palette entry.** Index 0 is white in some palettes; fill the blank line
   with the darkest entry of the current palette.
3. **Byte writes are unreliable;** write 16- or 32-bit. Single-pixel overlays (text, the sun) use a
   16-bit read-modify-write.
4. **Double buffering means full redraws** (old renderer) – test on the PC by swapping two buffers,
   the second filled with junk (`T32X_DOUBLE_BUFFER=1`).
5. **A palette switch shows the old picture in the new colours for one frame.** Invisible at
   60 fps, a white flash at 10 fps. Clear the screen together with every menu palette switch.
6. **Leaving a screen must reset its clip rectangle**, or the next screen's clear leaves a strip.
7. **Genesis display on behind the 32X washes the picture out**; switch it off (CMD 52).

---

## 7. The line-table scrolling renderer (`OJ32X_SCROLL`)

The 32X has no scroll registers, but each line's start address is in the line table. The renderer
keeps the level's background in each frame buffer and moves it with the line table.

| Item | Layout / rule |
|---|---|
| Ring | 384 × 200 pixels at frame-buffer byte `0x200`: world row wy = ring row wy % 200, world column wx = ring column wx − wx0 |
| Panel | 320 × 33 at `0x12E00` (or an SDRAM copy, §8.3) |
| Blank line | `0x16000` |
| Line table | view lines -> ring rows from (vY % 200), start + (xoff & ~1); odd x via the shift bit `0x20004102` (set at the swap); panel lines below |
| Per frame | rows that came into view; cells whose tile/event/background changed (a shadow table per buffer); the background under last time's overlays in this buffer (sprites, foreground tiles, text, sun – rectangles recorded by the screen mapping) |
| Full redraw | more than 64 spare columns sideways, a jump, or more than 256 overlay rectangles |
| Sky | colours numbered by ring row (two rows per entry, 100 entries = 200 rows: seamless); the sky palette effect turns those 100 entries so each ring row gets its screen line's colour (the sky keeps its parallax) |
| Sun | drawn each frame onto sky pixels only (behind the tiles) |
| Screen mapping | every blit/fill to the screen is split into ring/panel pieces by `port/sdl_oj.cpp`; it lasts one frame (from the level's draw to its present), so menus opened from the pause menu draw to the plain screen; after any plain present the level redraws both rings |

PC check: 245 of 276 frames of a played level identical to the old renderer; the others differ
only in the sky gradient by one line when the view's y is odd. All 79 levels run with scrolling.

---

## 8. Performance: what was done, and what it gave

### 8.1 Measurements
In-game counter (Z), Turtle Terror level 1: old renderer about 11 fps; line-table renderer 12–13 fps
at the start, 10 at the waterfalls. The frame time is spread over drawing, sprites, panel, logic
and the buffer swap; no single item dominates.

### 8.2 Measures in place (all behind switches)

| Switch / change | What it does |
|---|---|
| `OJ32X_DIRECT_FB` | the game draws into the frame buffer: no 64 KB copy per frame |
| 32-bit run copies | transparent blits copy runs of visible pixels with aligned 32-bit writes; fills with 32-bit writes |
| one-write-per-pixel scene decoder | the original wrote each pixel up to ~200 times |
| `OJ32X_SCROLL` | §7: only changes are drawn |
| `OJ32X_FAST_CODE` | the pixel routines run from SDRAM at -O2 (code in the cart goes through the slow bus and a 4 KB cache) |
| `OJ32X_PANEL_DIFF` | the panel is drawn in SDRAM; only changed rows are copied to the frame buffer |
| `OJ32X_FLIP_ASYNC` | presenting only requests the swap; logic runs during the wait; palette and shift bit go in at the swap |
| `OJ32X_PACE` | frame pacing to the median of recent frame times (the third-slowest wasted a vblank on most frames) |
| plasma | column term once per frame, 32-bit writes |
| demos, cutscenes, intro off | – |

### 8.3 What did not help
- **Drawing on both SH2s** (`OJ32X_SLAVE_DRAWS`): 1CPU and 2CPU both 4–5 fps (with profiling
  overhead). Both SH2s wait on the same bus to the frame buffer and the cart; the master has
  priority. Off.
- **No pacing at all:** faster on average, but motion at speed looks choppier.

### 8.4 Profiling lessons
- **The SH2 free-running timer** gave times in steps of its 91 ms period – something resets or
  reprograms it. **The watchdog's overflow interrupt** never arrives (the game clock already falls
  back to vblanks). Working clock: vblank count × 16.68 ms + the watchdog counter since the last
  vblank (178 µs steps).
- **A profiler drawn with a game font** costs a large part of the frame and may be invisible in
  level colours. Use a tiny 3 × 5 font, colours chosen outside palette ranges that effects rotate.

### 8.5 Not done
Pre-split sprites and tiles (skip/copy runs baked on the PC), skipping the sky under fully opaque
tiles, game logic on the slave SH2, the SH2's 2 KB on-chip RAM for inner loops, SH2 DMA for strip
copies, Genesis-plane backgrounds (Chaotix-style: smooth hardware scrolling, but backgrounds
reduced to the Genesis's 61 colours and tile streaming into 64 KB VRAM).

---

## 9. Audio

- **Music:** the game's 35 modules (+ 5 for Holiday Hare '95) rendered on the PC with OpenJazz's
  own libxmp, **one complete pass** each with a 2 s fade, CD tracks 2–41 (`tools/music_tracks.txt`,
  missing songs stay silent tracks). The drive repeats a track (CMD 45). A fixed-length cut (80 s)
  left most songs incomplete; full recordings with loops would need hours of disc.
- **Sound effects:** `SOUNDS.000` (22 sounds, unsigned 8-bit, 11,025 Hz) -> KSFX pack: 5 kHz,
  ≤ 1 s each, all 22 in 60 KB, envelope 96 (255 was far too loud and distorted), samples at ¾ scale,
  and a **low-pass before resampling** (without it the high parts aliased into a metallic,
  "modulated" sound).
- **No runtime volume** on either path: the set-up shows music/effects on/off switches instead of
  bars.

---

## 10. Saves and settings

The Sega CD's internal backup RAM (8 KB shared by all games) is too small for the game's saves
(up to ~2 KB each). The cart save area at `0x3F0000` is used instead ([OPENLARA] §5, CMD 58 + 61),
one 4 KB piece per file, read straight from the cart:

| Piece | Content |
|---|---|
| 0 | settings file `openjazz.cfg` (global: effects/music, character, game options) |
| 1–4 | saved games of the main game |
| 5–9 | saved games of Holiday Hare '95 (6–9 used) |
| 10 | the port's own settings (`OJPS`: FPS counter, A/B swap) |

OpenJazz writes its settings only when the program quits – which a console never does – so the
set-up menu now saves when it is left. The piece is written by the 68000 and read back.

---

## 11. Build system

1. **Header dependencies** (`-MMD -MP`, `-include *.d`) **and** every object depending on its
   Makefile. Without them, a changed class size was compiled into some objects only: heap damage
   in `free()`, and a level object allocated at its old size.
2. **Deterministic code for anything baked on the PC and looked up on the SH2** (newlib's and
   glibc's `qsort` order equal elements differently: font atlases differed).
3. **Big-endian, alignment:** read file data byte by byte; build unaligned words with `memcpy`.
4. **PC first, with the 32X code paths:** a host build using the same port layer and a regression
   of 706 frames that must stay pixel-identical; a second host build compiled with `OJ32X_MARS`,
   run from the packs alone. It finds pack, memory and drawing bugs before a disc is burned – but
   not SH2-only build bugs or console timing bugs (§12).
5. **Verify each delivery** with `grep -c` lines on a fresh copy before burning.

---

## 12. Console-only bugs worth knowing

| Symptom | Cause | Fix |
|---|---|---|
| Hang after "Setup options" from the pause menu (picture frozen, music on) | an asynchronous swap still pending when a synchronous swap started; the next wait expected a buffer state that never came | every synchronous swap first finishes a pending one |
| Save screen flickering with old level pictures | the screen mapping stayed on while another screen drew | mapping lasts one frame; rings redrawn after other screens |
| Out of memory in bonus levels | 64 KB blocks not baked (picture line limit) | bake longer blocks 256 wide |
| Out of memory after several deaths | heap fragmentation (§5.2) | panel block from the pack |
| Menus with a strip on the right | see-through black (§6.1) | `0x0000 -> 0x0400` |

---

## 13. Features and limitations

| Area | State |
|---|---|
| Episodes 1–6, A–C, Holiday Hare '94 (79 levels) | playable |
| Holiday Hare '95 (2 planets, 5 levels) | playable, as an episode |
| Bonus levels (30, 3D floor) | playable; frame rate low (per-pixel floor) |
| Planet screens | shown; the planet stays still (no zoom) |
| Music | 40 CD audio tracks, one pass each, repeated |
| Sound effects | 22, PCM chip, 5 kHz |
| Saves | 4 slots per game, cart save area, survive power-off |
| Settings | global; saved on leaving set-up; FPS counter and A/B swap remembered |
| Set-up | pad only: character, A/B swap, music on/off, effects on/off, game options |
| Credits + CD player | plasma background; selector separate from playback; plays all tracks in order; Y/X/Z/Mode or Up/Down hide the text |
| Quick start | second boot to the menu in seconds; A+B+C forces a full load |
| Frame rate in levels | about 10–13 fps; scrolling not smooth |
| Loading | about 30 KB/s: a planet pack 30–45 s, once per planet |
| Intro, cutscenes | off (the original decoder was far too slow; planned as pre-rendered frame deltas) |
| Demo mode | off (`OJ32X_DEMO` brings it back) |
| Multiplayer, keyboard, joystick set-up | not available |
| Music / effects volume | on/off only |
| Emulators | need the 4 MB RAM cart; Fusion stops on the splash |

---

## 14. Additions to [BOOT], [LEDGER] and [OPENLARA]

- **Command list:** add 47, 58, 61, the bit-15 flags of 45 and 46, the id ranges of 38 (§4).
- **[BOOT] rule 4:** confirmed on the console for byte writes in general, not only zeros.
- **[BOOT] rule 5:** applies to every palette, including the game's.
- **[BOOT] backup RAM (4B05):** fine for a few hundred bytes; larger saves belong in the cart save
  area.
- **[OPENLARA] §2.3:** the quick check can stay on for releases if a button forces the full copy.
- **New:** the line table is enough for scrolling without redrawing (§7), and palette-effect
  skies survive it by turning the palette instead of redrawing.

---

## 15. Source

The source export contains the engine changes (`oj/`, marked `PORT32X`, also as
`OPENJAZZ_PORT32X.patch` against upstream), the port layer (`port/`), the boot programs
(`cd32x/`, changes marked `OJ-…`), the PC host build and regression references, the pack, music
and sound tools, and the documents. It contains **no game data**: the build reads a copy of the
game (and optionally Holiday Hare '95) from folders next to `build.sh` and produces all packs,
sound packs and music tracks from it.
