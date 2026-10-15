# OpenJazz CD32X – handoff (Jazz Jackrabbit 1 on Sega CD + 32X + 4 MB RAM cart)

Project folder: `/mnt/s/KoboPort/sega-toolchain-12.1/sega/OpenJazzCD32X`
(Windows `S:\KoboPort\sega-toolchain-12.1\sega\OpenJazzCD32X`).
Working method: as in *Handoff: How We Work and the Kobo CD32X Framework* (PC host build
first, memory budget, then the 32X; one zip per round; this file is the memory).


> **Note on the source comments (after J19b).** Code comments no longer carry round numbers
> (J4c, J13e, OJ-J5 ...): each change is explained by what it does and why. This log keeps the
> history. Markers: `PORT32X:` in OpenJazz's sources (`#ifdef OJ32X` blocks), `OJ32X:` in the
> boot programs (`cd32x/`); the port layer (`port/`) is the port's own code.

## Target (proposed, waiting for the tester's decision)
The OpenLara model: the disc boots, `cdloader_sh2.c` copies the engine into the RAM cart,
each level is loaded from the disc into a cart slot, CD audio for music. A cartridge-only
build does not fit (memory table below).

## Folder layout
| Path | Role |
|---|---|
| `oj/` | OpenJazz (AlisterT/openjazz master as uploaded), git-tracked; port changes under `#ifdef OJ32X` with a `PORT32X:` comment |
| `port/include/SDL.h`, `port/sdl_oj.cpp` | the SDL 1.2 subset OpenJazz uses (built with `OJ_SDL1`): 8-bit surfaces, palette-mapped blits, pad → key events |
| `port/plat.h`, `plat_host.c`, `mixer.*` | Tyrian T3j's platform layer (PC side); `T32X_ROMFS` made optional |
| `port/oj32x.h` | what the game calls in the port (pad layout) |
| `host/Makefile`, `run_host.sh` | the PC test build: `make -C host`, `make -C host test`, `make -C host ASAN=1` |
| `tools/make_boot_image.py` | `Splash.png` → `IMAGE.RAW` for the loader (320×224 + palette, 72,192 bytes) |
| `tools/make_music_tracks.py` | from OpenLara (CD audio track numbering) – for later |
| `tools/check_toolchain.sh` | asks the Sega toolchain about C++ (sh-elf-g++, libstdc++, exceptions) |
| `tools/regress.sh` | regression: scripted runs, every 15th frame against `tests/*.ref` (J2) |
| `tools/heap_peak.sh`, `tools/heap_group.py` | who holds the heap at its peak (J2) |
| `tools/level_mem.sh` | heap per level, all 79 levels → `tests/level_mem.txt` (J2) |
| `port/host_new.cpp` | PC build: C++ `new`/`delete` through the counted heap (J2) |
| `mars/Makefile`, `mars/trial_stub.c` | J3a: first SH2 compile + trial link (sizes, list of missing platform functions); nothing runs yet |
| `JazzJackRabbitGame/` | the tester's Jazz 1 CD data (GOG collection), **not in git** |
| `cd32x/ref/` | uploaded references, **unchanged**: `cdloader_sh2.c` (OpenLara loader), `cdl_crt0.s` (its CD boot table start-up), `subcpu_crt0.s`, `APP.BIN` (**Kobo's** Sub-CPU program, not OpenLara's) |

## Measurements (J1)
OpenJazz: C++14, ~35,000 lines; Jazz 1 needs `jj1/ io/ menu/ level/ player/ game/`.
JJ2 is off by default upstream (not built). Network code is compiled but has no sockets.

C++ use: `unique_ptr`/`make_unique`, 5 `std::list`, 2 `std::vector`, no RTTI.
**Exceptions** carry the error paths (`throw E_FILE`, `E_DATA`, `E_QUIT` in loaders and menus).

Code size (PC, x86-64 `-Os`, as a guide): game 174 KB, libxmp 98 KB (music player – not
needed on the 32X if music is CD audio), miniz 30 KB. Data 10.5 KB, bss 8 KB.

| Memory item | As written | Plan |
|---|---|---|
| Tile set, 240 × 32×32 8-bit | ~240 KB | cart (read-only, written by the 68000 at level load) |
| Level grid 256×64 × 8-byte `GridElement` | 128 KB | split: static part in the cart, `hits`/`time` only for live events in SDRAM |
| Tile masks 240 × 64 | 15 KB | cart |
| Sprites per level | ? (needs data) | cart, used in place |
| Screen 320×200 8-bit | 64 KB | straight into the 32X frame buffer |
| Code | ~200–250 KB on the SH2 (estimate) | runs from the cart |

## Pad layout (port/sdl_oj.cpp)
| Button | Playing | Menus (and the in-level menu) |
|---|---|---|
| D-pad | move / look | move |
| C | jump (and swim) | select |
| B | fire | back |
| A | change weapon | – |
| Start | level menu | select |
| Z | statistics | – |

Switching layouts releases every key, and a button held across the switch stays quiet until
it is let go (otherwise Start, held while the menu opens, would select at once).

## Port changes in OpenJazz (`#ifdef OJ32X`)
| Step | File | Change |
|---|---|---|
| J1 | `io/sound.cpp` | `closeAudio` frees the xmp context only if audio opened (upstream frees a null context: crash when audio is unavailable) |
| J1 | `level/level.cpp`, `main.cpp` | pad layout per frame (play while playing, menu otherwise) |
| J2 | `jj1/level/jj1levelload.cpp` | memory marks (before tiles / tiles / sprites / loaded) |

## J2 results (PC build with the game data)
**Reproducible across machines:** the tester's `last.png` (his PC) and the sandbox's are
pixel-identical.

**What runs:** intro cutscene → title → main menu → demo 1 (all of episode 1 level 1,
~95 s) → demo 2 (Tubelectric). Scripted new game: New game → Episodes → Turtle Terror →
Medium → planet screen (Start skips) → level 1; walk, jump (C), fire (B), a kill, score;
Start opens the level menu (paused), B closes it, C picks Continue. All 79 `LEVELn.www`
files load and play 300 frames without an error (`-w`/`-l` start).

**Regression** (`tools/regress.sh <data>`, ~22 s): `attract` (no input, 9,200 frames,
614 compared) and `newgame` (1,380 frames, 92 compared): 0 different.

**Heap - two fixes in the measuring/port layer:**
1. C++ `new` was not counted (only our own objects' malloc calls are wrapped; libstdc++'s
   `operator new` calls the real malloc). `port/host_new.cpp` fixes it: the real peak of
   level 1 was **1.61 MB**, not 1.0 MB.
2. Every surface had its own 1.3 KB palette + remap table in the SDL stand-in (~345 surfaces
   in a level). Now shared until a surface is recoloured: peak **1.27 MB**, frames identical.

**At the peak (level 1, before fix 2; `tools/heap_peak.sh`):**
| What | Size | 32X plan |
|---|---|---|
| Level sprites (~320 surfaces) | ~440 KB | cart |
| Tile set | 247 KB | cart |
| Game menu's episode pictures | 178 KB | cart |
| Main + game menu backgrounds | 196 KB | cart |
| Level object (grid 128 KB, masks 15 KB) | 159 KB | split: static part cart, changing part SDRAM |
| Fonts | ~116 KB | cart |
| Screen | 65 KB | 32X frame buffer |
The menus stay alive while a level plays (level inside game menu inside main menu): their
pictures must go to the cart too.

**Per level (all 79, `tests/level_mem.txt`):** tile set 123–246 KB, sprites 200–358 KB,
heap after loading 1.09–1.32 MB, peak up to 1.34 MB (`LEVEL0.023`). Before any level file is
read: 761 KB (menus, fonts, level object).

**Game data on disc:** 17.5 MB of game files (cutscenes 5.6 MB, tile sets 4.1 MB, music
3.3 MB, levels 1.8 MB, sprites 1.3 MB …): a data track, not the 4 MB cart.

## Tested (PC sandbox)
- Host build from a fresh clone: 0 warnings; `make -C host test`: all pass.
- `tools/regress.sh`: 0 different (twice).

## Not tested / not done
- No sound (the shim's audio does not open; OpenJazz carries on silently).
- No 32X build: waits for the toolchain answer and the OpenLara Sub-CPU sources.

## Waiting for
1. Output of `bash tools/check_toolchain.sh` (still open).
2. From `OpenLaraCD32X`: `subcpu/main.c`, `subcpu/hw_md.s`, `subcpu/Makefile`,
   `subcpu/make_mixed_cd2.py`, the loader's `cdloader.ld` + `Makefile.cdloader`, and
   `tools/prep_openlara_cd.py` (the uploaded `APP.BIN` is Kobo's).
3. The target decision (RAM cart, proposed).

## J3a (SH2 compile: all files compile after one fix; link fix J3b sent)
Toolchain (the tester's check): sh-elf-g++ 12.1 with libstdc++/libsupc++ for SH2 (`!m2`);
a test with exceptions, unique_ptr and std::list compiles and links (~100 KB with the
libraries). `mars/Makefile` compiles the game for the SH2 (-m2 -mb -Os) and trial-links it.
Dry run with the PC compiler: game 174 KB, ext 30 KB, xmp 98 KB, port 5 KB; trial link
282 KB text; missing = only the 6 plat_* functions. The SH2 numbers come from his log.
J3a log: only `jj1save.cpp` failed (`<time.h>` missing; glibc includes it through other
headers, newlib does not) - fixed. Trial link then failed: libc.a's own SH simulator
syscalls (lib_a-syscalls.o) clash with reent.o over `errno`. J3b: our own system calls
(`port/mars_syscalls.c`, from Tyrian's port/mars/syscalls.c + _stat, _rename, mkdir).
**J3b result (the tester's build): the whole game compiles and links for the SH2.**
| Part (-Os, before unused code is removed) | text | data | bss |
|---|---|---|---|
| game | 165,941 | 520 | 7,739 |
| ext (argparse, miniz, stb) | 23,108 | 0 | 0 |
| xmp (music player) | 84,896 | 4 | 8 |
| port | 5,199 | 24 | 3,465 |
| **trial link** (unused code removed, + libstdc++/newlib) | **395,412** | **3,112** | **10,400** |
Code runs from the cart (4 MB; 0.4 MB engine). SDRAM: data + bss 13.5 KB + stacks + heap.
Still missing: 7 plat_* functions, __heap_start/__heap_end (linker script), _access and
_getcwd (added to mars_syscalls.c in my tree, ships with J3).
To check later: GCC 12's emergency exception pool (malloc'd at start-up), and that the
32X start-up runs the C++ initialisers (the unwinder registers its tables there).

## J3 step 1 (PC): read-only graphics in the RAM cart
`port/cart.c` (allocator, block table 1,024 entries in SDRAM) + `plat_cart_data`/`plat_cart_write`
(PC: a read-only mapping, opened only for the copy - any other write crashes = a test).
Surfaces created from pixels go to the cart (`Video::createSurface`), finished font atlases
move there (`oj32x_freeze`). Exception: the HUD panel is drawn into (ammo) - stays in SDRAM.
Results: regression 0 different; all 79 levels play 300 frames with no write into the cart.
Heap after a level has loaded: **321-335 KB** (was 1.09-1.32 MB). Cart: ~0.9 MB in level 1.
Left in SDRAM after loading: level object 159 KB (grid), screen 64 KB (-> frame buffer on
the 32X), HUD panel 10 KB, ~65 KB small blocks. Loading still peaks at up to 777 KB
(the tile set is held twice while it is assembled) -> next: decode straight into the cart.
Cart layout (proposed): 0x000000 engine (≤ 1 MB), 0x100000-0x3EFFFF graphics,
0x3F0000 settings and saves.

`cd32x/ref/APP_openlara.BIN`: a second APP.BIN from the tester - an early OpenLara loader
build (marker SH2-OPENLARA-CD-OL1-LOADER; files D32XR.32X, IMAGE.RAW, Kobo's names), binary only.

## OpenLara sources (received; key files kept in `cd32x/ref/openlara/`, unchanged)
`subcpu/` (Sub-CPU main.c + 68000 listener hw_md.s + disc Makefile), `sh2/` (loader:
cdloader_sh2.c, cdl_crt0.s, cdloader.ld, Makefile.cdloader), `tools/prep_openlara_cd.py`,
`build.sh`, the latest OpenLara handoff. What OpenJazz reuses:
- **Boot, unchanged:** disc → APP.BIN → CDLOADER.BIN copies the engine ROM (a normal 32X
  cartridge image, file 20) into the cart and starts both SH2s. Engine ROM rules
  (prep_openlara_cd.py): 'SEGA' at 0x100, 32X header at 0x3C0, SDRAM image ≤ 0x30000 bytes
  to ≤ SDRAM+0x38000, SH2 entries in SDRAM, marker 'CDB2' at 0x3BC.
- **Commands as they are:** 38 open file, 46 file chunk → cart, 48 pad, 45/54 CD audio,
  51 file → frame buffer, 52 Genesis display, 58/61 saves.
- **Decision: only proven paths, nothing invented.** The SH2 never writes the
  cart. Graphics are decoded on the PC at build time into disc files and copied into the
  cart with the proven CMD 38 + CMD 46, exactly like OpenLara's levels. (A frame buffer →
  68000 → cart path was considered and dropped: no proof it works on the console.)
- **Testing:** an emulator shows whether a disc has a chance to boot; only the console
  (Sega CD + 32X + RAM cart) is the real test, reported with phone photos. Every stage of
  start-up therefore shows something readable on screen.
- Engine start-up: OpenLara's engine uses its own 32x crt0 (not in this zip); OpenJazz will
  use Tyrian's cartridge crt0 (`port/mars/crt0_tyrian.s` + linker script), which already
  has the stack guard, slave start and error screens.

## J4a - first disc: boot test (sent)
- `build.sh`: splash → `make -C mars` (engine) → OpenLara's `prep_openlara_cd.py` (checks the
  header, writes `cd32x/subcpu/ROMS/OPENLARA.32X`) → loader → Sub-CPU → ISO → the disc checks
  of OpenLara's build.sh → a .cue with one 2 s silent track for emulators.
- `cd32x/sh2`, `cd32x/subcpu`: OpenLara's loader and Sub-CPU program, **byte for byte**
  (`cd32x/ref/openlara` is the untouched copy). The engine is on the disc as OPENLARA.32X,
  the name they open (file 20). Renaming comes later, as a separate change.
- Engine: `port/mars/crt0_oj.s` = Tyrian's crt0 (comments only changed), `port/mars/oj.ld`
  = tyrian.ld + .eh_frame/.gcc_except_table kept + .init_array; `port/plat_mars.c` from
  Tyrian's (no 0x0AAA handshake - the loader did it; C++ constructors + __register_frame_info
  before main; the SH2 never writes the cart); `tools/make_oj_rom.py` (Tyrian's cart tool
  without 68000 program/romfs; 'CDB2' at 0x3BC, "OpenJazz 32X" at 0x3C0; checked with a fake
  ELF against prep_openlara_cd.py in the sandbox).
- Cart: engine 0x000000-0x0FFFFF (1 MB), graphics from 0x100000, saves 0x3F0000.
- On the 32X, OpenJazz's log goes to the error screens' "last log" line (io/log.cpp, OJ32X_MARS).
- Boot test screen (before the game): cart read, C++ constructors, new/delete, exception
  throw/catch, heap, live pad + clock. Start runs the game; with no game files yet it must
  end on the blue screen "THE GAME STOPPED WITH AN ERROR", last log about the data files.
- Untested: everything SH2-side (no SH2 compiler in the sandbox); expect a fix round.

## J4a: the first disc (boot test) - sent
**One boot path: the CD32X boot is OpenLara's, unchanged; the game is attached
at its end** as the payload the loader copies into the cart and starts (file 20).
- `cd32x/sh2/`, `cd32x/subcpu/`: OpenLara's loader and Sub-CPU program, byte-identical copies
  (cdl_crt0.s differs only in line endings). The engine goes on the disc as `OPENLARA.32X`,
  the name the Sub-CPU opens for file 20 - nothing in the boot path is edited.
- Engine image: `mars/Makefile` links the game with `port/mars/crt0_oj.s` (Tyrian's crt0,
  unchanged) and `port/mars/oj.ld` (Tyrian's + .eh_frame/.init_array kept for C++);
  `tools/make_oj_rom.py` (Tyrian's make_tyrian_cart.py without 68000 program/romfs) writes
  the header the loader reads; build.sh runs OpenLara's own `prep_openlara_cd.py` on it as
  the check. Tested here with a made-up ELF: header accepted by prep_openlara_cd.py.
- `port/plat_mars.c`: Tyrian's plat_mars.c (display, pacing, clock, pad 48, Genesis text 52,
  error screens) minus the 0x0AAA handshake (the loader did it), minus sound/CD/mouse/net.
  The SH2 never writes the cart: plat_cart_write = red screen.
- Boot test screen before the game: cart readable, C++ constructors, new/delete, exception
  throw/catch, heap, live pad + clock. Start runs the game; expected end for J4a: blue screen
  "THE GAME STOPPED WITH AN ERROR", last log "Unable to find game data files" (no files yet).
- Cart: engine area 1 MB (0x000000-0x0FFFFF), graphics from 0x100000.
- Untested on SH2 here (no compiler in the sandbox): plat_mars.c, crt0/oj.ld link, C++
  start-up. Expect one fix round from the build log.

## J4a console result (the tester's video, real Sega CD + 32X + RAM cart)
SEGA logo -> splash (no bar) -> boot test: cart read OK, C++ constructors OK, new/delete OK,
exceptions OK (0123), heap 12,176 used of 219,024 before the game -> Start -> blue screen
"Unable to find game data files", exactly as expected. Seen: a strip of the splash's bottom
line under the text screens (to look at).

## J4b: splash with progress bar, diagnostics on (the tester's request)
Boot path edits, each marked `OJ-J4B`, only switching back what the docs/ comments show:
- `cd32x/sh2/cdloader_sh2.c`: the progress bar again (OL-7 had removed it): empty bar drawn
  with the splash, `splash_progress` after every chunk; bar moved to x 56-236, y 186-198
  (between the logo and the copyright line, measured on Splash.png); Genesis text ON
  (COMM2 = 1 at CMD 52, OL-3 had 0); 256-colour mode with 0x80 = text in front (Kobo K18).
- `cd32x/subcpu/main.c`: KOBO_DIAG_TEXT 1 (green), KOBO_BOOT_TEXT 1 (white) - the
  "diagnostics" column of the CD32X boot handoff's switch table.
- Engine: leaves the text as the loader set it until C++ start-up has worked, then switches
  it off for its own screens; writes COMM12 = 0x4B10 before main() (HW9: the Sub-CPU stops
  reprinting its rows). Exit code printed signed.
- Next: the engine's own loading screen (same picture + bar) when game files load (J4c).

**Diagnostics switches (one per program, all at the top of the file, all documented there):**
| Switch | File | Diagnostics | Release |
|---|---|---|---|
| `OJ_DIAG_TEXT` | `cd32x/sh2/cdloader_sh2.c` | 1 (text on, in front of the splash) | 0 |

**Now set (J4b5, the tester): only Chilly's white text** - KOBO_BOOT_TEXT 1, KOBO_DIAG_TEXT 0 (green off);
OJ_DIAG_TEXT 1 and OJ32X_DIAG_TEXT 1 keep the Genesis display on so the white text can show
(with 0 the loader's CMD 52 would switch the whole Genesis display off).

**J4b6 (emulator video):** (1) green lines (INSIDE-INITCD, CKPT1) still showed: they come from
`cd32x/subcpu/scd.c`, which prints green without going through print_diag - new switch
`SCD_DIAG_TEXT 0` there (same pattern as KOBO_DIAG_TEXT; also silences its error-only prints:
CD timeout, file-not-found root listing - set 1 when a disc does not boot). (2) glitches at
the top of the splash = the Genesis layer showing through: IMAGE.RAW's black border used
index 0 / value 0x0000, which are see-through on the 32X (Kobo rule 5). make_boot_image.py now
never uses index 0; black = index 252 = 0x0400 (as Tyrian's make_splash.py).
| `SCD_DIAG_TEXT` | `cd32x/subcpu/scd.c` | 1 (green CKPT/INITCD + error lines) | 0 |
| `KOBO_DIAG_TEXT` | `cd32x/subcpu/main.c` | 1 (green STATE text) | 0 |
| `KOBO_BOOT_TEXT` | `cd32x/subcpu/main.c` | 1 (white start-up messages) | 0 |
| `OJ32X_DIAG_TEXT` | `port/plat_mars.c` | 1 (text kept until C++ start-up) | 0 |
The progress bar is not a diagnostic: it is always on.
| `OJ32X_SPLASH` | `port/plat_mars.c` | 1: the loader's splash stays through engine start-up + `OJ32X_SPLASH_HOLD` (120 frames), like Kobo's KOBO_SPLASH/SPLASH_HOLD | 1 |

## Next round (J3)
Graphics into the cart, tested on the PC: a cart image in the host build (4 MB, written only
by a "68000" copy routine, read-only for the game), the decoded tile set, sprites, fonts and
menu pictures placed there, the level grid split. Goal: SDRAM heap under ~150 KB in every
level, regression still 0 different.

## J4b7 (console video 1005261759)
- Chilly's white text shows at the start (Sub-CPU boot), no green: OK.
- Splash colours washed out (lavender instead of purple) with the Genesis display on + text
  priority bit during the splash; J4a (display off during the splash) was vivid. So:
  `OJ_DIAG_TEXT 0` (loader switches the Genesis display off when the splash appears) and
  `OJ32X_DIAG_TEXT 0`; KOBO_BOOT_TEXT stays 1: the white text shows during the Sub-CPU boot,
  before the splash - the J4a/OpenLara-proven picture with the white text in front of it.
- The boot test was drawn OVER the splash: text_screen cleared with byte writes of 0, which did
  not land in the frame buffer; J4a's clean screens came from clear_back_buffer's 32-bit
  writes. text_screen now clears with 32-bit writes.

## J4c: game data in the cart (data pack IMAGE8.RAW)
Only proven commands: main.c's CMD 38 opens `IMAGE<n>.RAW` for cart slot n (n * 128 KB),
CMD 46 copies 32 KB chunks into the cart (bit 15: no read-back, OL-13). One edit in the boot
path: `cd32x/subcpu/Makefile` IMAGE_FILES gets IMAGE8.RAW (marked OJ-J4C).
- `port/pack.h/.c`: pack format (game files + pictures already decoded on the PC + "file
  pictures" = decoded RLE pictures keyed by file + position, so the game skips the decoding).
- PC build: `T32X_BAKE=<file>` records every file opened and every picture built, writes the
  pack at exit (`port/pack_bake.c`); `T32X_PACK=<file>` loads a pack into the write-protected
  cart and runs from it. build.sh bakes boot -> intro -> main menu (940 frames) from
  JazzJackRabbitGame/, then checks that the pack ALONE (no data folder) reaches the menu.
- Game hooks (OJ32X): File opens pack files first (fmemopen over the cart copy); scene frames
  are used in place (mapBlock, not freed when in a pack); Video::createSurface uses the pack's
  copy of a picture; File::loadSurface / scene backgrounds skip decoding when the pack has
  the file picture.
- 32X: after C++ start-up the engine loads IMAGE8.RAW with CMD 38/46 into cart 0x100000,
  the loader's bar (same place/colours) filling on the splash, purges the cache, mounts it.
  A picture not in the pack stays in SDRAM (log "PICTURE WxH NOT IN THE PACK"); the SH2
  never writes the cart. Boot test shows "PACK: OK <bytes>".
- Measured on the PC (pack only, no data folder): frames identical to the normal run;
  pack 1,240,268 bytes (13 files, 411 pictures, 16 file pictures) = 38 chunks; heap peak
  152,722 bytes (was 463,417 without the pack; 32X has ~207,000 free after start-up).
  Without the demo level in the pack, the demo does not start and the menu stays.
- Untested on the SH2: fmemopen from newlib (link will tell), the pack copy from the engine.

## J4c console result (video 1005261847) and J4c2
Pack copied (second bar run, ~40 s), boot test "PACK: OK 1240268 BYTES" (fmemopen links and
works on newlib). After Start: red screen ADDRESS ERROR in newlib free_r (mallocr.c:2655-2669),
last log "PICTURE 56X48 NOT IN THE PACK", heap 130,816 used.
- 56x48 = a panel font atlas: stb_rect_pack sorts with qsort, whose order for equal keys
  differs between glibc and newlib -> the SH2 built a different atlas. Fix: stable insertion
  sort for stb_rect_pack (`port/oj32x_sort.h`, -DSTBRP_SORT), on both builds.
- free_r crash: not reproduced on the PC (also not with the 32X code paths under ASan and a
  forced atlas miss). Platform-specific candidate found: our `_fstat` answered "character
  device" for ANY fd and left st_blksize uninitialised; newlib sizes a stream's buffer from
  that when the stream has no file - every pack file (fmemopen, fd -1) got a garbage-sized
  buffer. Fix: `_fstat` = EBADF for fd outside 0-2 (newlib then uses BUFSIZ), `_isatty` only 0-2.
- Diagnostics: red screens show PC, PR and up to 9 return addresses from the stack (crt0
  pri_err_go passes the old r15 and PR: two added lines, OJ-J4C2) and the last three log lines;
  each pack file read logs "OPEN <name>". `tools/where.sh <address>` looks them up.

## J4c2 console result and J4c3
Same red screen: ADDRESS ERROR in newlib free_r; call chain (tools/where.sh on the photo's
addresses): free_r <- SDL_FreeSurface <- Font::Font(unsigned char*, bool) (the panel font,
freeing its single-character surfaces), last log "PICTURE 56X48 NOT IN THE PACK".
- The 56x48 miss was still there because make did not rebuild stb_rect_pack.o after the
  Makefile change (only sources were dependencies). J4c3: every object depends on its Makefile.
- The free_r crash does not reproduce on the PC, also not with the 32X code paths under ASan
  and with the font atlas misses forced (ties sorted the other way): the heap is damaged by
  something only the SH2 build does. J4c3 adds a heap walker (port/mars_syscalls.c
  oj32x_heap_check, newlib/dlmalloc block layout) at CREATESURFACE, FREESURFACE, BLIT and every
  log line: the first check that finds a damaged block header stops with
  "HEAP DAMAGED, FOUND AT <point> BLOCK <n>" - the damage happened since the check before.
  Untested assumption: newlib's first block starts at __heap_start (16-aligned) - if the very
  first check already fails, the assumption is wrong, not the heap.
- J4c3b: `OJ32X_BOOT_TEST 0` in port/plat_mars.c - the boot test checks still
  run, but its screen (and the wait for Start) only appears when a check fails; otherwise the
  game starts straight after the splash. 1 = the screen as before.

## J4c3b console result (video 1006261330) and J4d
The game runs on the 32X: splash/bar -> (intro: vertical stripes, every line the same - to
investigate) -> "Epic MegaGames presents", "A game by", title, main menu; the cursor moves.
Selecting did nothing; ~20 s idle -> blue screen EXIT CODE 1, last log "OPEN MACRO.4".
- Exit code 1 = abort(): the demo starts (MACRO.4), `new JJ1DemoLevel` needs ~160 KB, the
  SDRAM heap has ~63 KB left -> std::bad_alloc, which OpenJazz never catches. Levels need the
  memory work (grid split etc.) - next. Until then the pack leaves MACRO.1-4 out (no demo).
- "Nothing starts": the episode menu greys out episodes whose files it cannot find; the pack
  only had the files of the intro/menu. J4d: the pack lists every file of the data folder
  (catalog entries, offset 0 = "on the disc, not in this pack"; File::exists sees them).
- J4d bake = a tour of every menu (Instructions, Setup, Order info, New game -> episode ->
  difficulty -> planet screen); build.sh checks the tour from the pack alone gives the same
  70 pictures as from the data folder. Pack 1,861,172 bytes (57 chunks).
- One progress bar for the whole boot: the loader fills it to the half (engine), the engine's
  pack load fills the second half (cdloader_sh2.c one line, OJ-J4D).
- Expected next stop on the console: after the planet screen, the level -> blue screen
  EXIT CODE 1 (no memory for the level yet).

## J4d console result and J4e
Menu reached, no crash any more (no demos), menus slow, intro still vertical stripes.
- Stripes: JJ1Scene::loadCompactedMem fills every coloured run down its whole column and lets
  later rows overwrite it (up to 200 writes per pixel): seconds per cutscene frame on the SH2,
  so the half-done picture (row 0 repeated = stripes) is what shows. J4e (OJ32X, both builds):
  the same result with one write per pixel (a pixel the data does not set takes the last colour
  written in its column above it, else keeps its value). Regression 0 different (the attract
  run includes the whole intro).
- Menu speed: the plasma background (four cosines per pixel, 64,000 pixels a frame) is off on
  the 32X (OJ32X_MARS): plain background colour 28 (the line upstream had commented out).
- Next ("focus on adding content"): levels - fitting a level into SDRAM.

## J5: no intro, no demos, every level ("skip the intro, load the first level, add
all levels, no demo mode")
- Intro skipped on the 32X (oj32x_skip_intro; PC build: T32X_SKIP_INTRO=1). Demos: MACRO.1-4
  stay out of the packs, so the menu never starts one.
- Level memory (all measured on the PC with the 32X code paths, `-DOJ32X_MARS`):
  - grid 128 KB -> 48 KB: event times in a 256-entry table (JJ1Level::eventTimes), grid cell
    3 bytes (event 7 bits + bg 1 bit share a byte). Regression 0 different (demo 1 plays a
    whole level).
  - tile set and the level's tile/event references come decoded from the pack (no 490 KB /
    32 KB decoding buffers in SDRAM).
  - the game screen is the 32X frame buffer (`OJ32X_DIRECT_FB 1`, plat_canvas): no 64 KB
    SDRAM screen and no 64 KB copy per frame. Checked on the PC with T32X_DOUBLE_BUFFER=1
    (screen swaps between two buffers, the second starting as junk, like the frame buffer):
    the new-game scenario (92 frames) and the menu tour (94 frames) identical - the game
    redraws everything it shows each frame.
  - SDL_PixelFormat without colour masks (16 bytes per surface); cart block table 16 entries
    on the 32X (8 KB less .bss); heap checks only at log lines (OJ32X_HEAP_CHECK 0).
- Level packs: OJ<k>.PAK, k = world * 10 + level (port/pack.h), cart 0x280000 (slot 20),
  max 1.5 MB. Sub-CPU main.c: CMD 38 numbers 200-1199 open OJ<n-200>.PAK into slot 20
  (OJ-J5, the same pattern as OL-2's level table). The engine loads a level's pack at
  JJ1Level::load with the loading screen (the loader's splash via CMD 38/51 + the bar) and
  mounts it as pack 1. tools/bake_levels.sh makes all 79 (common pack mounted: only what it
  lacks); sizes 0.5-1.0 MB, 62 MB in all.
- Common pack: no intro, every menu, all PLANET.* files, no music: 1,542,844 bytes (must stay
  under 1.5 MB = 0x180000; build.sh checks).
- Memory, all 79 levels from the packs alone (32X paths, PC): every level plays 400 frames,
  no picture missing, heap peak at most 256,730 bytes INCLUDING the 64,000-byte screen the PC
  still keeps (the 32X draws into the frame buffer) -> about 193 KB; the 32X has 218,448 with
  ~12 KB used before main; the SH2's 4-byte pointers make it smaller still.
- PC-side bugs fixed: cart_write counted from the allocator base (wrote over a mounted pack);
  T32X_PACK mounted only at plat_init (files opened before were missed); heap snapshot with an
  empty label.
- Not in the packs yet: music (no sound), cutscenes between episodes, bonus levels.

## Round names
the tester already had a zip named J5: the J5 above was renamed on his side. The next delivery is **J7**
(no J6), and later zips count on from there.

## J5 console result and J7
Intro skipped OK. New Game -> ... -> blue screen EXIT CODE 1, last log "Could not open file:
MENUSNG.PSM", heap top 89,296 of 226,512 (so not out of SDRAM at the heap top). On the PC with
the 32X paths and the packs, the same steps reach level 1: after the second MENUSNG.PSM line
come "LEVEL PACK OJ000.PAK", OPEN FONTS.000 ... So on the 32X it stops between the menu and
the end of the level pack load. Exit code 1 = abort() = std::terminate (an exception nobody
catches; OpenJazz catches only int).
J7 diagnostics: std::set_terminate -> red screen "C++ TERMINATE: UNCAUGHT EXCEPTION <type>";
operator new on the 32X -> red screen "OUT OF MEMORY: NEW OF n BYTES (HEAP TOP ...)"; the blue
end screen shows the last three log lines; the level hook logs "LEVEL FILE <name>: PACK OJ<k>"
before loading. Packs unchanged: LEVEL_PACKS=keep ./build.sh.

## J7 console result and J8
Red screen: OUT OF MEMORY: NEW OF 156,736 BYTES (heap top 89,104 of 226,320). 156,736 is the
OLD size of JJ1Level (8-byte grid cells). Cause: mars/Makefile had no header dependencies
(-MMD), so files that include a changed header were not rebuilt - game.cpp still allocated the
old JJ1Level while jj1level*.cpp used the new layout. The same thing explains the J4c/J4c2
free() crash: J4c added a member to File (file.h), and objects compiled before kept the old
size of File (heap damage); it vanished when J4c3's Makefile change rebuilt everything.
J8: -MMD -MP and `-include $(OBJS:.o=.d)` in mars/Makefile (the Makefile change itself
rebuilds every object once).

## J8 console result and J9
Level 1 plays on the console (video 1006261545): level menu, save/load screens work; the
in-game FPS counter shows about 11. all remaining content, no cutscenes, no Exit
Game, A = C, more frames per second without changing the game's speed.
- Cutscenes off on the 32X (Level::playScene returns at once; OJ32X_MARS).
- Main menu "Exit game" does nothing on the 32X.
- Pad: A = C (play: jump; menus: select); change weapon moved to X and Y (6-button pad).
- Speed: blits copy runs of visible pixels with aligned 32-bit writes (copy_run): the game
  draws straight into the frame buffer, so this is up to 4x fewer frame buffer writes. The
  game's own timing is unchanged (it runs by the clock, not by frames). Regression 0 different.
- Bonus levels: measured with the 32X paths and a bonus pack (OJ800.PAK, ready in the code:
  BONUSMAP.* -> k 800): peak 236 KB (the bonus map alone is 128 KB, plus a 64 KB decoding
  buffer) - more than the 218 KB. So on the 32X a bonus level is skipped (Game::playLevel,
  back to where the game came from); the bonus pack is not built yet. Needs the same kind of
  map work as the levels (tiles read-only from the pack, events as a small table).

## J10 (one CD-R: as much as possible in one go)
- Slave SH2 draws: JJ1Level::drawBand draws the sky + background tiles and the foreground tiles
  for one band of the view; oj32x_parallel runs the top band on the master and the bottom band
  on the slave at the same time (plat_parallel: job in SDRAM through the cache-through address,
  the slave purges its cache before each job; the band views are made on the master, so malloc
  never runs on both). Same pixels as the original (regression 0 different). Switch
  OJ32X_SLAVE_DRAWS (plat_mars.c), 0 = master draws both bands.
  JJ1 itself has no parallax layer: the sky is a palette gradient (moves with the view by
  palette), the rest are tiles that scroll with the level - smoothness = frame rate.
- Sound effects: tools/make_oj_sfx.py turns SOUNDS.000 (22 sounds, unsigned 8-bit 11025 Hz)
  into SFX00.BIN (KSFX, 5 kHz, all 22 in 60 KB of the PCM chip); CMD 53 loads it at boot;
  OpenJazz's playSound puts the effect on the next pad request (COMM0 = 48 | n << 8, n = SE::Type)
  - main.c's proven K33/OL-6 path, unchanged.
- Music: CD audio, tracks 2-36 = the 35 music files in alphabetical order
  (tools/music_tracks.txt). the tester provides WAVs in music/ named like the files; then
  build.sh makes the tracks (make_music_tracks.py) and builds with OJ32X_MUSIC 1 (CMD 45 to
  play, 54 to stop). Without WAVs the engine asks for no tracks.
- Menus: "(esc) quits" is text (fontbig) -> "(b) back" on the 32X.

## J10b: the game's own music as CD audio (before the burn)
tools/make_music_wavs.sh renders the 35 .PSM songs with OpenJazz's own libxmp (tools/psm2wav.c,
linked with the PC build's xmp objects) into music/<NAME>.wav, 44.1 kHz 16-bit stereo, one pass,
at most 80 s each (MAX_SECONDS): 35 songs = 433 MB, the console repeats each track. build.sh then
makes tracks 2-36 (+ silent 37-80 from make_music_tracks.py) and builds with OJ32X_MUSIC 1.
Tested here: all 35 render (40-80 s, real signal), make_music_tracks.py maps all 35.
Later: a cloned project for the line-table scrolling renderer.

## J11
- Saving: OpenJazz's files (openjazz.cfg, SAVE.0-3) live in a 4 KB store in the Sega CD's
  backup RAM - main.c's OL-5 path (CMD 56 load, 57/58 one word, 59 save), file renamed
  OPENJAZZ_JJ (OJ-J11: OpenLara's OPENLARA_TR would have been overwritten). port/mars_syscalls.c:
  _open/_read/_write/_lseek/_close/_fstat/access serve the store (one pool, one file written at
  a time, saved to backup RAM when it is closed); plat_mars.c reads only the used bytes at the
  first file access. Tested on the PC (store logic in a harness): write, rewrite, reload. A JJ1
  save is up to ~2 KB (its random filler), so 1-2 saves fit next to the settings; a save that
  does not fit fails (log "SAVE: ..." or the game's own message).
- Credits + CD player: main menu "Exit game" -> menu/credits32x.cpp (both builds): the main
  menu's plasma, two credit pages (Jazz Jackrabbit / OpenJazz; D32XR, Chilly Willy, the tester)
  in fontbig, switching every 8 s; CD player under them: tracks 2-36 alphabetical, starting at
  SONG5 (Nippius), planet names read from the game's level files (tools: song per world -
  e.g. SONG0 Diamondus, SONG5 Nippius, SONGCD6 Ceramicus, XM2/XM3 Holidaius). A/C/Start play/
  stop, Left/Right previous/next, B back (menu music again).
- Not yet: Holiday Hare '95 (its own complete data set with the same file names as HH94 -
  needs its own common pack + level packs and a choice at boot), bonus levels, cutscenes (off).

## J11b: the soundtrack video (Noxxa's 37-track OST) as the CD music
tools/cut_ost.py cuts videoplayback.mp4 at the playlist's lengths, each cut moved to the middle
of the nearest silence (+-4 s, ffmpeg silencedetect), so rounding does not add up; each track
-> music/<game file>.wav (playlist -> file mapping in the script: Diamondus SONG0, Secret level
SECRET, HH95 Candion XMAS2 / Bloxonius XMAS3 ...), at most 110 s with a 3 s fade (the playlist
versions loop the songs; all 37 in full would be ~3.5 h): 36 x 110 s + 8 s = 66 min of audio,
about 75 min with the data - fits an 80-minute CD-R. Tested with a made-up 3-track file: the
cuts land in the silences. tools/music_tracks.txt: XMAS2/XMAS3 = tracks 37/38 (the existing
numbers do not move); make_music_tracks.py leaves a missing song silent instead of stopping.

## J11c: longer music from the game's own data
the tester's videoplayback.mp4 is 445 s = only track 1 (Main Menu) of the playlist, so
cut_ost.py stopped (its length check). Instead tools/psm2wav.c now lets each song loop as in
the game up to MAX_SECONDS (default 110) with a 3 s fade (ENDLEVEL: 8 s, one pass):
34 x 110 s + 8 s = 62.5 min of audio, about 71 min with the data and the silent tracks.
cut_ost.py stays for a complete playlist video.

## J11d ("a single complete track, fade and loop that")
psm2wav's 4th argument 1 = one complete pass of the song (measured first, then played), faded
over its last 2 s; make_music_wavs.sh uses it for every song (600 s only as a safety limit).
One pass: 7 s (ENDLEVEL) to 196 s (SECRET), 35 songs = about 53 minutes. The console repeats
each CD track (CMD 45 repeat).

## J12: world packs ("add 3, make 1 and 2 happen")
- (2)+(3) One pack per WORLD (planet), OJ<world*10>.PAK: both levels of the planet, the shared
  tile set and sprites (once instead of twice), the planet screen. A level of the same world as
  the pack in the cart loads nothing - the second level of every planet starts without a
  loading screen. tools/bake_levels.sh plays every level and adds it to its world's pack
  (T32X_BAKE_MERGE=1: pack_bake.c reads the existing pack first). 79 levels -> 39 packs,
  34 MB in all (was 79 packs, 62 MB); biggest 1,488,996 bytes (limit 0x170000).
- (1) The common pack no longer carries the 39 planet screens: 1,375,128 bytes (was
  1,542,844). The planet screen (JJ1Planet) and the level both call oj32x_level_pack, which
  knows PLANET.www and LEVELn.www -> the same world pack; whichever comes first loads it.
- Checked with the 32X code paths (PC): all 79 levels from the world packs, 400 frames each, no
  picture missing, same memory as before; new game -> one pack load (OJ000), then the planet
  screen and level 1 from it.
- Next for real preloading: a second cart slot (needs room: free 0x06E000-0x100000 0.57 MB +
  what the slimmer common pack frees, 0x250000-0x280000).

## J11 console test (photos/videos 1006261906-1930) and J13
- Red screens after deaths / level changes: OUT OF MEMORY: NEW OF 46,272 BYTES (heap top
  195,312 of 217,840), last log OPEN PANEL.000 = JJ1Level::loadPanel decoding PANEL.000 at every
  level start. Not a leak (PC, 32X paths: a second level load leaves +240 bytes) - the 32X heap
  gets fragmented, no 46 KB hole left. J13: File::loadRLEPacked - the decoded block comes from
  the pack (baked as a one-line "file picture"); also at start-up. Plus 8 KB more heap (below).
- Saves: J11's backup-RAM store did not work (the internal backup RAM is shared by all games;
  and base_name cut "OPENJAZZ.CFG" to 11 characters, so the settings never matched). J13: the
  RAM cart's save area, OpenLara's OL-9 path - one 4 KB piece per file at 0x3F0000 (0 cfg,
  1-4 SAVE.0-3), written word by word (CMD 58) and committed per file (CMD 61, read back),
  read straight from the cart. No SDRAM buffers (J11's 8 KB of .bss are gone). Tested on the PC
  in a harness (write cfg + a 2,001-byte save, read both back).
- White bars above/below the picture: the 24 border lines showed palette entry 0, which is
  white in some palettes; now the darkest entry of the current palette (plat_border_colour).
- Sound effects: envelope 96 instead of 255, samples at 3/4 scale (too loud, distorted).
- Volumes: CD audio and the PCM effects have no volume control on this path - set-up volume
  0 = off (music stops / no effects), above 0 = on.
- Set-up menu on the 32X: character, buttons (A/B swap: A fire + B jump, C always jumps),
  audio, gameplay. No keyboard, joystick or video entries.
- FPS: the tester sees no change from the slave SH2 drawing half - next: measure where the
  frame time goes on the console before more speed work; the scrolling rebuild is the real fix.
- Next: warm boot - skip copying engine and common pack when the cart already holds them
  (verify with a checksum on the SH2); hold a button at boot to force the full load.

## J13b: CD player selector
Left/Right only move a selector ("SELECT 24 FANOLINT"); the song changes when A/C/Start
confirms it or after the selector rests 3.5 s on a track; A on the playing track = stop/play.
Credit lines spaced to end above the two player lines. Checked on the PC (frames).

## J13b console test and J13c
SFX volume and the volume bars OK. Saving/loading still not working; quick start not working.
- Saves: OpenJazz adds its settings/save directory only if access() says the directory exists
  and is writeable - our access() said "no such file" for "/" (from getcwd), so the game had no
  place to save at all (J11-J13). Now access() accepts directories.
- Quick start: there was none yet - the loader's 3-chunk quick check (D32X M1c) was switched
  off by OpenLara ("if (same && 0)"). OJ-J13C: on, unless A+B+C are held at power-on (CMD 48);
  the engine does the same for the common pack (CMD 47 on first/middle/last chunk + the pack
  header with the same length in the cart). World packs are still loaded each session.
- CD player: starts at track 31 (SONGCD5 Deckstar); no auto-play on selecting - the selector
  returns to the playing track after 5 s without A; each track plays once (CMD 45 bit 15) and
  then the next one (lengths from the WAVs: tools/music_lengths.py -> port/music_lengths.h,
  made by build.sh before the engine; unknown length = repeat).
- Plasma (credits background): column term once per frame, 32-bit writes, both SH2s
  (oj32x_parallel) - same picture (regression includes the main menu plasma on the PC).
- Open: "junk pixels when the text changes" and "a chunk of graphics on the right side" - need
  close-up photos.

## J13d
Right-edge strip on menus = leftover level graphics: the level left its view clip rectangle on
the screen and clearScreen only fills inside it. JJ1Level::~JJ1Level resets the clip, clears,
and clears both 32X frame buffers (plat_clear_screens). Regression 0 different.

## J13e
- The strip at the right edge (menus, levels) was the Genesis layer: black in the game palettes
  is 0x0000, which the 32X shows as see-through. Game palette entries 0x0000 -> 0x0400 when they
  go to the 32X (present_finish). (J13d clip reset stays, harmless.)
- Demo mode back: MACRO.1-4 in the common pack (levels now fit in memory); after 20 s (T_DEMO)
  at the main menu a demo plays - PC with 32X paths: MACRO.2 -> LEVEL0.000 from OJ000, heap as
  in a level. The main menu "flicker" about every 45 s was the demo timer finding no demo.
- Credits: Y shows / hides the text (oj32x_pad: pad bits as last read).

## J13f
- Audio set-up on the 32X: "music: on/off", "sound effects: on/off" instead of volume bars.
- Credits: Up/Down toggle the text too (3-button pads have no Y); the playlist skips songs with no
  WAV (XMAS2/3: they stopped the list) and starts over after the last.
- Sound effects: two-pass moving average (width src/dst) before resampling to 5 kHz - the high
  parts aliased ("modulated").
- "<B> BACK": the menu font has no < > [ ] { } = glyphs (tested), only letters, digits, ( ) - /.
- Right-edge strip: not the see-through black either (J13e did not remove it); it also spans the
  border lines, which are fully rewritten every frame - so probably not frame-buffer content.
  Open: does it show on the text screens (boot test, blue/red)?

## J13g
Y did not toggle the credits text on a 6-button pad. Now any of Y, X, Z, Mode (0x0F00) or
Up/Down toggles it, and the credits screen shows "PAD xxxx" (the bits as read) at the bottom left
to see what the extra buttons really send. "(B) BACK" stays.

## J14: frame profiler
plat_prof_us (master SH2 free-running timer, phi/32, 1.39 us; PC: clock_gettime) and
oj32x_prof(slot) marks: 0 logic (all catch-up steps of the frame), 1 background bands, 2 sprites,
3 foreground bands, 4 panel/rest of draw, 5 present (line table, frame pacing wait, flip),
6 other (events, pad, palette effects). Once a second the per-frame averages go to a line drawn
in the level's top-left corner with fontiny (OJ32X_PROFILE in mars/Makefile):
"L B S F H P O T  <fps>FPS <steps>ST" in ms per frame. Regression 0 different.

## J14b
- Profiler shown in the level's own font (fontiny was invisible on the console), three lines:
  "2CPU|1CPU <fps>FPS T<total>", "L B S F", "H P O" (whole ms per frame).
- Mode (6-button pad) switches the slave SH2's drawing on/off while playing, to profile both
  renderers on one disc (plat_slave_draws).
- Credits: the PAD xxxx diagnostic removed.
- Reported: lines in the clouds, waterfalls less smooth since the slave drawing - compare with Mode.
- Cutscenes: next; ENDE6.0SC is 1.9 MB raw (more than the 1.4 MB slot) - needs splitting.

## J14c
Profiler drawn as a box with its own font (the 3x5 error-screen font at 2x, 8x12 cells): the
brightest palette entry on a box of the darkest, top left of the level - readable on any level
(the game fonts were either invisible in level colours or too wide). Checked on the PC (32X paths).

## J14d
Console videos (J14b): all values were multiples of ~18 / 15 ms = whole 91 ms periods divided
by 4-6 frames - the free-running timer was not counting as assumed (reset/reprogrammed
elsewhere). plat_prof_us now uses the watchdog counter of plat_ticks_ms (178 us steps). Mode did
not switch to 1CPU in the videos; the profiler now alternates 2CPU / 1CPU every 10 s by itself.

## J14e
Demos off on the 32X (mainmenu.cpp: no demo macros unless built with -DOJ32X_DEMO).

## J14f
J14e videos: 1CPU and 2CPU both 3-7 fps (the slave drawing gains nothing). The times were still
in coarse steps: the watchdog overflow interrupt never comes on the console (that is why
plat_ticks_ms falls back to vblanks), so the watchdog-only clock wrapped every 45.6 ms. Now:
vblank_count x 16.683 ms + the watchdog counter since the last vblank (the VBI handler records it).

## J14e console result and J15: the line-table scrolling renderer
J14e (auto 1CPU/2CPU): both 3-7 fps, typically 4-5 - the slave drawing gains nothing; it is off
now (OJ32X_SLAVE_DRAWS 0). Demos off (J14e).

J15 (OJ32X_SCROLL, on in mars/Makefile): each frame buffer keeps the level background for the
rows in view in a 384 x 200 ring (frame buffer bytes 0x200; panel 320 x 33 at 0x12E00; blank line
at 0x16000). World row wy = ring row wy % 200, world column wx = ring column wx - wx0. The line
table shows it (plat_present_scroll; odd x via the 32X shift bit 0x20004102 - not yet seen on the
console). Per frame only: the rows that came into view, cells whose tile/event/bg changed
(shadow per buffer), and the background under last time's overlays in this buffer (sprites,
foreground tiles, text, profiler, sun - rectangles recorded by the screen mapping in sdl_oj.cpp,
which translates every blit/fill to the screen into ring/panel pieces). Sideways beyond the 64
spare columns, a jump, or >256 overlay rectangles: one full redraw. Sky colours are numbered by
ring row; SkyPaletteEffect turns the 100 sky entries so each ring row gets its screen line's
colour. The sun is drawn each frame onto sky pixels only (behind the tiles, as before).
Checked on the PC: the new-game scenario frame by frame against the old renderer - 245 of 276
frames identical, the 31 others differ only in the sky gradient by one line when the view's y is
odd (two-line colour bands fixed to the world instead of the screen); all 79 levels run 500 frames
with scrolling left and right; the 32X code paths + profiler box render correctly.

## J15 console video and J15b
J15 runs on the console: scrolling, sprites, waterfalls; the game's own counter shows FPS 10 in
Tubelectric. The profiler box was a plain blue box without letters and the sun a white ring: their
colours were sky entries (156-255), which the renderer turns every frame - the profiler now picks
its dark/light colours below 156; the sun and the profiler letters are written with 16-bit
read-modify-write (oj32x_put_pixel), as KOBO-BOOT rule 4 advises.

## J15c
Frame pacing off (OJ32X_PACE 0): pace_before_flip held every frame to the third slowest of the
last frames - at 4-6 vblanks per frame that wasted 1-2 vblanks on most frames. Now each frame is
shown at the next vblank after it is finished. Measured J15 (game counter): 12-13 fps Diamondus,
10 at the Tubelectric waterfalls (J8: 11).

## J15d
Profiler box off in the 32X build (the black block top left). The game's own FPS counter
(set-up, gameplay, hud style) remains for measuring.

## J16: speed switches (mars/Makefile)
- OJ32X_FAST_CODE: copy_run, blit_core, fill_core (own 32-bit fill_row instead of memset) in
  section .sdcode.fast (SDRAM) at -O2 - code in the cart runs through the slow bus / 4 KB cache.
- OJ32X_PANEL_DIFF: with the scrolling renderer the panel is drawn into a 10.5 KB SDRAM copy; at
  present only rows whose hash changed since that frame buffer last got them are copied (32-bit).
  PC: 276 frames identical to J15 scrolling.
- OJ32X_FLIP_ASYNC: presenting writes the line table and requests the swap without waiting; the
  palette and the 1-pixel shift go in at the vblank of the swap (VBI handler, or plat_wait_flip if
  it missed it); plat_wait_flip before the ring is touched again - the logic runs during the wait.
- Not done yet: sprites as pre-split runs (pack format change), logic on the slave SH2.

## J16 console: "choppier at high speed" (no FPS counter in the video). J16b
Pacing back on (OJ32X_PACE 1) but to the median of the recent frame times instead of the third
slowest, and now also before the asynchronous swap (it had none) - even motion without waiting a
vblank on most frames.

## J16c
Settings were never saved on the console: OpenJazz writes openjazz.cfg (music/effects on-off,
HUD style, character name and colours, game options) only in setup.save() at program exit
(main.cpp), which a console never reaches. Now SetupMenu::run saves on leaving the set-up menu
(OJ32X_MARS). The file is global (cart save area piece 0), separate from the saved games
(pieces 1-4: level, planet, difficulty, score ...). The A/B swap is not part of the file.

## J17: Holiday Hare '95
A second game on the disc, from JAZZXMAS1995/ (its own complete data set, same file names).
- Splash: "A JAZZ JACKRABBIT CD / C HOLIDAY HARE 95" (only when IMAGE9.RAW is on the disc).
- Common pack IMAGE9.RAW (CMD 38 id 9 -> cart slot 9 = 0x120000, at most 1.4 MB; 797 KB), world
  packs OJ600/OJ610 (bake_levels.sh 4th argument 100; the level hook adds 100 when plat_game()),
  sound effects SFX01.BIN (CMD 53 COMM2 = 1), saves and settings in save-area pieces 5-9.
- Music: its MENUSNG / ENDLEVEL / XM3 have the same names as Jazz CD's but are its own songs ->
  tracks 39-41 (H95MENU, H95ENDLV, H95XM3); XMAS2 / XMAS3 = tracks 37 / 38.
- In the game: New game -> episode "X Holiday Hare" (the others are greyed out).
- PC, 32X paths, packs only: menus -> Candion level 1 plays (heap peak 267 KB incl. the PC's
  64 KB screen; ~20 KB more small blocks than Jazz CD levels).

## J18: bonus levels
the tester's Jazz_JackrabbitG.zip = the same GOG collection, unpatched (7 level files + MENU.000
differ; our data has the community patch - Orbitus 2, hoverboards, Jungrock, Battleships 2,
episode previews). The bonus maps were always there; they were skipped for memory.
- JJ1BonusLevel (OJ32X): tiles and events of the 256x256 map read from the pack
  (loadRLEPacked, 2 x 64 KB), collected events in an 8 KB bit table (eventAt / tileAt /
  clearEvent); background and tile graphics also from the pack. Network grid changes ignored.
- One pack per map: BONUSMAP.nnn -> OJ<800+nnn>.PAK (CMD 38 ids 1000-1029), made by
  tools/bake_bonus.sh (PC, menu "Z Bonus levels" with T32X_BONUS_MAP=nnn): 30 packs, 19 MB, about
  650 KB each. Game::playLevel no longer skips bonus levels (OJ32X_NO_BONUS would).
- PC, 32X paths, packs only: bonus level 1 plays; heap peak 245 KB incl. the PC's 64 KB screen
  (was 300 KB).

## J18b
Quick start with the game choice: same game as last boot -> common pack check as before (skips).
Other game -> its pack is loaded (IMAGE9 at 0x120000 overlaps IMAGE8 at 0x100000, so switching
games always reloads). Fix: A+B+C held at power-on reaches choose_game first; it is now taken
there (Jazz CD + full load) instead of being lost when the choice waits for the buttons to be let go.

## J18c: white flash with the pause menu's save / load / set-up
1. A palette switch (video.setPalette(menuPalette) in FileMenu/SetupMenu, the level's palette on
   return) showed the old picture in the new colours for one frame - invisible at 60 fps on a PC,
   ~0.1 s on the 32X (the white flash). OJ32X_MARS: clear the screen with each such switch.
2. With the scrolling renderer the mapping stayed on while those screens drew, so they drew into
   the ring and every other frame showed old level pictures. The mapping now lasts one frame (from
   scrollBegin to its present); after any plain present the level redraws both rings and the panel.
PC (scrolling build): the save screen no longer alternates between two pictures.

## J19 (includes the J18d work)
- No game choice at start. Holiday Hare '95 is the last entry of the episode menu, in
  place of "specific level": plat_set_game(1) for its run (world packs +100, SFX01.BIN, music
  mapping, save pieces 6-9), back to 0 afterwards. Its world packs are baked against Jazz CD's
  common pack; of the files both use only MAINCHAR.000 differs (read from JAZZXMAS1995/ via
  T32X_BAKE_UNPACKED, so it lands in OJ600/OJ610); level packs are searched before the common
  pack. No IMAGE9.RAW any more. Settings file: one for both (piece 0).
- Planet screen: the planet stays at its full size for the whole sequence (no zoom in/out).
- Hang after "Setup options" from the pause menu (black screen, music on): an asynchronous swap
  was still pending when the menu's synchronous swaps started, and the next plat_wait_flip
  waited for a buffer state that never came again. flip_wait now finishes a pending swap first.
- Z (the game's statistics key) is what shows the FPS counter - not the HUD style setting. Its
  state and the A/B swap are now kept in save-area piece 10 ("OJPS", plat_port_setting).
- PC, 32X paths: episode menu -> Holiday Hare 95 -> Candion (planet still) -> level with the
  Christmas Jazz sprite.

## J19b
- Bonus level red screen on the console (NEW OF 65,536 BYTES after OPEN BONUSY.000): loadRLEPacked only
  baked blocks up to 65,535 bytes (one picture line), so the bonus map's two 64 KB blocks were never in
  the packs and the 32X decoded them into SDRAM. Blocks longer than that are baked 256 wide. Bonus
  packs made again (build.sh marker .bonus_v2): 23 MB. PC, 32X paths: bonus peak 135 KB incl. the
  PC's 64 KB screen (was 245).
- The FPS counter (Z) starts on when nothing is saved yet (plat_port_setting default).

## J20: 32-bit source reads (OJ32X_LONG_READS, branch long-reads)
The blits read the source a 32-bit word at a time where they read bytes: copy_run when source
and destination are not equally aligned (two aligned words merged per output word, both inside
the run while 8 bytes remain), and the keyed blits when looking for the colour key (skip_key /
skip_visible: 4 pixels per word, has-key test). The bus traffic for cart reads (cache line fills
through the 32X's 16-bit cartridge port) is the same; the gain is fewer load instructions and
no assembling of words from bytes. Mode switches it while playing; the frame counter's top line
shows 32 or 8. Checked: unit test of every alignment / length in both byte orders (600,000 cases,
ASan + UBSan), the game with and without in six levels (1,200 frames identical), ASan runs,
regression 0 different.

## J23: the game logic on the slave SH2 (OJ32X_SLAVE_LOGIC, port/plat_mars.c)
As in the Hurrican CD32X port: each level frame works out the view first (from the state
before this frame's steps - JJ1LevelPlayer::view already looks ahead by the time not yet
stepped), then the master brings the ring up to date (scrollBegin + scrollOrb) while the slave
runs the frame's logic steps (JJ1Level::frameSteps, the loop play() had); after the join the
sprites, foreground and panel are drawn from the new state, with the time change that is left.
- plat_logic_parallel: part 0 on the master, part 1 on the slave on an 8 KB stack of its own
  (call_on_stack; the slave's start-up stack is 2 KB); the master purges its cache after the
  join. The slave purges its own at the job's start (as before).
- Unlike the two drawing SH2s (J10, no gain: both on the frame buffer's bus), the logic works
  in SDRAM and the cache, so the two should not wait on each other much.
- Safe because: the background pass does not malloc or use COMM (the logic may: new events,
  sounds go to sfx_pending, boss music sends a command); the slave only reads what the
  background pass writes nowhere; tile changes made by the logic mid-pass are caught by the
  shadow table next frame.
- J23b: switched in the pause menu, "second cpu on / off" (as the Hurrican port's 2ND CPU);
  Mode does nothing now (it switched the 32-bit reads in J20; those stay on). The frame
  counter's top line shows 2CPU / 1CPU.
- step()'s result (the level ends) is now acted on after that frame is drawn.
- Not tested on hardware when built (built and linked only; the PC host runs both parts one
  after the other). SDRAM heap 8 KB smaller (the slave's stack): 194,096 bytes.

## J23b: pause menu - second cpu on / off, background on / off
- The pause menu gets two items after "quit game" (level.cpp; the debug menus, in debug builds,
  move to the end): "second cpu on / off" (J23's slave logic) and "background on / off".
- Background off (oj32x_sky_on, port/sdl_oj.cpp): the sky's 100 palette entries all get one
  colour of the sky (no colour turning per frame, paletteeffects.cpp), and the sun / moon is not
  drawn (scrollOrb). The ring is not redrawn: its sky pixels just show the one colour. Levels
  without a sky do not change.
- Neither switch is remembered (both on at every start). Built only, not tested on hardware.

## J23c: the camera after the steps again
J23/J23b on the console (four videos, every setting): 15-22 fps, up from J20's 10-13, but jerky
in every setting - the camera was the one guessed before the steps (view() looking ahead by the
whole frame's time), the sprites came from after them, so they jumped against each other.
Now the view is worked out again after the join, from the same old view (its lag applied once)
and with the time change that is left - as before J23; scrollBegin runs a second time only when
it differs from the guess (only the rows / columns between the two), then the sun.
Built only, not tested on hardware.

## J23d: an even camera
J23c still jerky on the console (higher fps). Now, with the second SH2, the camera is worked
out once, before the steps, from the state before them and without looking ahead (view(...,
change 0)) - one camera for the whole frame, a frame behind, as the Hurrican port (smooth
there). The sprites come from after the steps. With "second cpu off" the frame is exactly J20's
(steps, then draw). Open: the white clouds show holes - probably cloud pixels that use the sky's
palette entries (156-255), which the scrolling renderer turns per ring row (and background off
makes one colour); the fix would be to move those pixels to other entries when the packs are
made. Built only, not tested on hardware.

## J24: even frames (frame-locked clock, steadier pacing); fast reads switch
J23d on the console: 25-30 fps in open places, 12-18 in busy ones, still not smooth.
- The game's clock (main.cpp loop: globalTicks) is now plat_frame_ms(): the time the next frame
  will be shown (last swap + the pace, in vblanks), never going back. The plain clock was read
  at a different point between two swaps every frame, so one frame got 2 vblanks of game time
  and the next 3 while both were shown equally long - the motion stepped unevenly.
- Pacing holds each frame to the 12th of the last 16 frame times (was the median): with the
  second SH2 the times spread over 2-5 vblanks, and at the median many frames came late.
- Pause menu: "fast reads on / off" switches J20's 32-bit source reads. The white clouds show
  holes on the console but not on the PC (host frames of level 1 checked; no tile of level 1
  uses the sky's palette entries 156-255 - T32X_TILESTAT=1 prints that count), so the suspect is
  a console-only drawing path; if the holes go with fast reads off, the 32-bit reads are it.
Built only, not tested on hardware.
