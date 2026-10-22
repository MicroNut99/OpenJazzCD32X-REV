# Jazz Jackrabbit CD32X

Jazz Jackrabbit (1994, Epic MegaGames) for the **Sega CD + 32X**, built on the OpenJazz engine.
All episodes of the CD edition, Holiday Hare '94 and '95, the bonus levels, CD audio music and
sound effects.

## REV: the Genesis draws the levels

This is the **REV** line of the port: the **Genesis VDP draws the level's background tiles**
(converted at build time into Genesis tiles, palettes and maps), and the **32X draws only the
sky, the sprites, the foreground tiles and the status panel** - the 32X no longer redraws tiles
as the view scrolls. The two pictures are mixed by the console: 32X colours with the priority
bit are in front of the Genesis, the sky's colours behind it.

- All 78 levels of Jazz CD are drawn by the Genesis (R3). The parallax backgrounds the original
  makes by turning palette colours with the camera (a texture, diagonal stripes) are a texture on
  the Genesis's second plane, scrolled by the 68000; the water levels' darkening is the Genesis
  palette fading with the 32X's.
- The Genesis has 61 colours on screen (4 palettes of 15, from 512) where the 32X had 256: the
  tiles are converted to the nearest Genesis colours; the colour cycles (waterfalls, lamps,
  lava) keep running (the 68000 rotates them; the longest ones in coarser steps).
- Details: [docs/GENESIS_PLANE.md](docs/GENESIS_PLANE.md).

**First start of a REV disc: hold A + B + C** while switching on (full reload), so the new
program replaces the one in the RAM cart.

How to build the disc: see *Building* below.

## Why the "Sonic CD Rip and Tear" method works here

The method: tear the game's level graphics apart at build time into the Genesis's own format
(8 x 8 patterns, 16-colour palettes, a map of which pattern goes where), let the **Genesis VDP
draw the level** on its planes, and keep the **32X for what moves**: the sprites, the
foreground and the panel. It is how Sonic CD's levels are built (16 x 16 blocks made of 8 x 8
patterns), applied to a game that was never made for the Genesis.

### The advantage

- **The Genesis draws its planes for free.** Its VDP builds every line from the patterns as the
  picture goes out; scrolling is two register writes. The 32X has no tile hardware at all - in
  the older builds the SH2s spent most of each frame copying tile pixels into the frame buffer
  (new rows and columns as the view moved, and the background again under every sprite).
- **The 32X's work shrinks to the sprites.** The level tiles are most of the screen; the 32X
  now draws only what lies in front of them. In levels the frame rate went from about 10-13 to about 20-22 a
  second (R1 on the console).
- **The 68000 has little to do.** Per screen refresh it writes the scroll position, the
  colours (cycles, fades) and at most a column and a row of new patterns as the camera moves.
  It never decodes anything: the conversion was done on the PC.
- **The Genesis picture is steady.** It scrolls at 60 refreshes a second with the camera, even
  when the 32X's own picture is at 20.

### Why the tile size decides it: tiles that divide by 16 (and so by 8)

The VDP only knows 8 x 8 patterns placed on an 8-pixel grid. A game tile can become patterns
only if its edges fall on that grid:

- **Jazz's tiles are 32 x 32** (32 = 2 x 16). Every tile is exactly 4 x 4 patterns, always in the
  same place on the grid. One tile is converted once, into 16 patterns and 16 map words, and is
  reused everywhere it appears. With identical and mirrored patterns stored once, the tiles a
  level uses fit the 1,600 patterns the video memory has room for in 75 of the 78 levels (the
  other 3 merge a few look-alike patterns). A destroyed block is one tile swapped for another: 16 map words.
- **Tiles that do not divide by 8 do not line up.** Hurrican's tiles are 20 x 20. A 20-pixel
  tile starts at 0, 20, 40, 60 ... so every other tile edge falls in the middle of an 8 x 8
  pattern (20 = 2 x 8 + 4). Those patterns hold parts of 2 or 4 different tiles, and the same
  tile is cut differently depending on where it sits (the grids only meet again every 40
  pixels). The patterns are no longer "this tile" but "these neighbours at this
  offset" - every different combination on the map is a new pattern. A level's map turns into
  thousands of unique patterns, far more than the 1,600 that fit, and nothing a converter can do
  removes them without visibly damaging the picture. Changing one tile in play means rebuilding
  the patterns it shares with its neighbours.

So a game with tiles of 16 or 32 (or any multiple of 8) gets the whole gain: the conversion is
exact and small. A game with other sizes gets little: most of the level would not fit the
Genesis, and what did fit would still leave the 32X drawing much of the picture.

### What else has to fit

- **Colours.** The Genesis shows 4 palettes of 15 colours (61 on screen, from 512); each 8 x 8
  pattern uses one palette. Jazz's levels were drawn with 256, so the converter picks the 4
  palettes that keep them closest. Games with few colours per area convert well; games with
  smooth gradients or many colours in one spot lose more.
- **Effects.** Anything the original does to its background pixels every frame (Jazz's palette
  parallax, colour cycles, water darkening) needs a Genesis form: plane B, the 68000 rotating
  colours, the palette fade (R3). Effects the VDP has no form for stay with the 32X.

## What you need

- A Sega Genesis / Mega Drive with **Sega CD** and **32X**
- A **4 MB RAM cart** in the 32X cartridge slot (the game keeps its data there)
  - or the **8 MB / 16 MB cart** (the 16MB-PCB-CD32X board)
    with the `CART=8` / `CART=16` disc (see *Building*)
- The game disc, burned from `OpenJazzCD32X.cue` (disc-at-once, 80-minute CD-R)

Emulators without a 4 MB RAM cart (for example Fusion) cannot run it: the loader stops on the
splash screen.

## Building

**What the build needs** (Linux or WSL):
- Chilly Willy's Sega toolchain in `/opt/toolchains/sega` (the SH2 and 68000 compilers), gcc,
  Python 3 with numpy and Pillow (`sudo apt install python3-numpy python3-pil`).
- The game files in `JazzJackRabbitGame/` inside this folder (`PANEL.000`, `BONUSMAP.000`,
  `LEVEL0.000`, ... from Jazz Jackrabbit CD). Optional: `JAZZXMAS1995/` (Holiday Hare '95) and
  the music as WAVs in `music/`. Both are copied from `../OpenJazzCD32X` when they are there.

**The commands** - everything is made inside this folder, nothing outside it:

| Cart | Command | The disc |
|---|---|---|
| 4 MB | `NAME=R1 bash tools/make_disc.sh` | `discs/R1/OpenJazzCD32X_R1.cue` |
| 16 MB | `CART=16 bash tools/make_disc.sh` | `discs/R11-16MB/OpenJazzCD32X_R11-16MB.cue` |
| 8 MB | `CART=8 bash tools/make_disc.sh` | `discs/R11-8MB/OpenJazzCD32X_R11-8MB.cue` |

For example, from WSL:
```
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/OpenJazzCD32X-REV && \
CART=16 bash tools/make_disc.sh
```

**Options** (put them before `bash`, as `CART=16` above):
- `NOMUSIC=1` - a disc without the music (one silent audio track): smaller and quicker to make,
  for emulators.
- `LZ=1` - the 8 / 16 MB builds with the world packs compressed in the cart (R8: 27 worlds copied
  on the first boot instead of about ten, a short unpacking when a world starts); the disc name
  gets `-LZ`. Off by default.
- `PROF=1` - the frame profiler box in levels; the disc name gets `-PROF`.
- `NAME=...` - another disc name.

**What happens:** the build makes the data packs (the common pack, a pack per world and per bonus
map, the Genesis plane packs, the bonus floors), the engine, the loader and the Sub-CPU program,
then the disc image. The full log goes to `build_log.txt`; the last lines and the path of the
`.cue` are printed at the end. The first build takes a while (the packs are made by running the
PC build through the levels); later builds keep the world packs.

**Burn the `.cue`** (with its `.bin`, disc-at-once, 80-minute CD-R). The `.iso` alone has no
music.

## Starting

Switch on with the disc inserted. The first start copies the game into the RAM cart (about a
minute and a half, with a loading bar). Later starts find it there and go straight to the main
menu.

If something looks wrong after starting, do a **full reload**: hold **A + B + C**, switch on, and
let go when the loading bar appears.

## Blast loading: the RAM cart as memory

The 32X has only 256 KB of its own memory, far too little for the game. So the game keeps its
program and its data in the **4 MB RAM cart**, which holds its content while the console is
switched off. That is what makes "blast loading" possible:

- **The first start** copies the engine and the common data (menus, fonts, status panel) from
  the disc into the cart, about a minute and a half with the loading bar.
- **Every later start** compares a few pieces of the disc with what is in the cart (a fraction of
  a second). When they match, nothing is copied and the main menu appears within seconds.
- **A + B + C** at power-on skips that check and copies everything again.

During play the cart is shared out like this:

| Cart area | Size | Holds |
|---|---|---|
| `0x000000`-`0x06E000` | 0.43 MB | the game program |
| `0x06E000`-`0x100000` | 0.57 MB | free |
| `0x100000`-`0x280000` | 1.5 MB | common data: menus, fonts, status panel |
| `0x280000`-`0x3F0000` | 1.4 MB | the current planet (both of its levels, tiles, sprites, planet screen) or bonus level |
| `0x3F0000`-`0x400000` | 64 KB | saves and settings |

Everything in the cart is prepared on the PC when the disc is built: pictures are already
decoded, so the 32X uses them straight from the cart instead of unpacking them into its small
memory. Each planet is loaded once, when you reach it, and both of its levels play from it.
The CD drive delivers about 30 KB/s through this path, so a planet takes 30-45 seconds; the
loading time is mostly spent between the chunks of data, not in reading them.

### The 8 MB and 16 MB carts (R8)

With a bigger RAM cart (`CART=8` / `CART=16` builds) the first start also copies the worlds into
the cart, compressed to about a third of their size: 27 of the 41 worlds fit in 16 MB. The splash
shows "COPYING TO THE CART - START: SKIP"; Start stops the copying. A world in the cart starts in
about 6 seconds (3 of them unpacking); a world not in the cart loads from the disc in about 13
seconds and is then kept in the cart. Details: `docs/R8_COMPRESSED_PACKS.md`.

### Bonus levels: the Sega CD draws the floor (R9)

The bonus levels' rotating floor is drawn by the Mega CD's rotation / scaling chip and shown by
the Genesis; the 32X draws only the sky, the horizon, the sprites and the panel. They run at 30
pictures a second (20 on six maps that need two layers for their colours), where the 32X alone
managed 2-3. Details: `docs/R9_BONUS_FLOOR.md`.

### Demos (R10)

Left alone for 20 seconds, the main menu plays the game's demos again (MACRO.2-4: episode 1,
worlds 1 and 2). A demo only plays when its world is already in the cart (copied at the first
boot or played before), so it never loads from the disc; with the 4 MB cart, or when the first
boot's copying was skipped, the menu just waits.

### No compression by default (R11)

The 8 / 16 MB builds keep the world packs in the cart as they are on the disc again (R7's
way: about ten worlds copied on the first boot, no unpacking when one starts). `LZ=1` builds
R8's compressed version (27 worlds, the disc name gets `-LZ`). Later boots no longer show
"COPYING TO THE CART" when there is nothing to copy.


## Controls

| In a level | |
|---|---|
| D-pad | move, look up / down |
| C or A | jump |
| B | fire |
| X or Y | change weapon (6-button pad) |
| Z | frame counter on / off (6-button pad) |
| Start | pause menu: continue, save, load, setup, quit |

The set-up can swap A and B (A fires, B jumps; C always jumps).

| In menus | |
|---|---|
| D-pad | choose |
| A, C or Start | select |
| B | back |

## Main menu

- **New game:** choose an episode, then a difficulty.
  - Episodes 1-6 and A-C, X Holiday Hare ('94), Z Bonus levels.
  - **Holiday Hare 95** is the last entry of the episode list.
- **Load game:** four save slots.
- **Setup options:** character (name and colours), buttons (A/B swap), audio (music on/off,
  sound effects on/off), gameplay options.
- **Instructions**, **Order info:** the original screens.
- **Credits** (last entry): the credits over a moving background, with a **CD player** for the
  soundtrack:
  - Left / Right choose a track, A, C or Start play it (on the playing track: stop / play)
  - tracks play one after another, starting again after the last one
  - Y, X, Z, Mode, Up or Down hide / show the credits text

## Bonus levels

Finish a level carrying the big red gem to reach a 3D bonus stage, or start them from
"Z Bonus levels" in the episode menu.

## Saving

Games are saved in the RAM cart and survive switching off. Each game (Jazz Jackrabbit and
Holiday Hare '95) has its own four slots. Settings are saved when you leave the set-up menu;
the frame counter (Z) and the A/B swap are remembered too.

## Loading times

Each planet (two levels) is loaded once with a loading screen, about 30-45 seconds; its second
level starts without loading. A bonus level is loaded on entry, and the next planet again after it.

## Frame rate

In levels the game runs at about **10–13 frames per second** (Turtle Terror level 1: 12–13 at the
start, about 10 at the waterfalls). The bonus levels, with their 3D floor drawn pixel by pixel, are
slower. The 32X shows a new picture at a screen refresh, 60 times a second, so a frame lasts a
whole number of refreshes: 4 refreshes give 15 frames per second, 5 give 12, 6 give 10.

**Z** (6-button pad) shows the frame counter in levels.

What has been done to get there:
- **Drawing straight into the frame buffer.** The game draws into the 32X's screen memory
  instead of a copy of it, which saves copying 64 KB every frame.
- **Faster pixel copies.** Sprites and tiles are copied as runs of visible pixels with 32-bit
  writes, and fills use 32-bit writes. The scene decoder writes each pixel once; the original
  wrote some pixels up to about 200 times.
- **A scrolling screen that only draws what changed.** The 32X has no scroll registers, but the
  start of each screen line can be set. The level's background stays in each frame buffer and is
  moved with those line starts. Each frame draws only the rows that came into view, the cells that
  changed, and the background under last frame's sprites. This brought Turtle Terror from about
  11 to 12–13 frames per second.
- **Fast code in fast memory.** The pixel routines are copied from the cart into the 32X's SDRAM,
  its fastest memory, and compiled for speed. Code in the cart goes through a slow bus and a
  small cache.
- **The status panel drawn only when it changes.** The panel is drawn in SDRAM and only its
  changed rows are copied to the screen.
- **No waiting for the picture swap.** Showing a frame only requests the swap; the game logic for
  the next frame runs while it waits.
- **Even frame times.** Frames are paced to the median of the recent frame times, so the motion
  stays even. Pacing to the slowest frames wasted a refresh on most frames.
- **Less to draw.** The intro, cutscenes and demos are left out; the credits background is drawn
  more cheaply.

What did not help:
- **Drawing on both SH2 processors.** Both processors wait on the same bus to the screen memory
  and the cart, so splitting the drawing between them gave no gain. It is off.
- **No pacing at all.** Faster on average, but the motion at speed looks choppier.

Ideas not done yet: running the game logic on the second SH2 while the first draws (this helped
the Hurrican CD32X port, where it removed tearing and freed time), preparing sprites and tiles as
ready-made copy runs on the PC, skipping the sky under solid tiles, using the SH2's on-chip RAM
for the innermost loops, and drawing the backgrounds on the Genesis layer behind the 32X picture.

## Known limitations

- The game runs at about 10-13 frames per second in levels; scrolling is not smooth (see
  *Frame rate*).
- The intro and the cutscenes between episodes are not included.
- No demo mode, no multiplayer.
- Music and sound effects can only be switched on or off, not set to a volume.
- Sound effects are lower quality than the original (5 kHz, to fit the Sega CD's sound chip).

## Credits

Jazz Jackrabbit: Epic MegaGames - a game by Arjan Brussee and Cliff Bleszinski.
OpenJazz: Alister Thomson and contributors.
D32XR 32X code: Victor Luchits. Sega CD and 32X framework: Chilly Willy.
Sega CD32X port: Micronut99.

Jazz Jackrabbit is the property of its rights holders. This package contains no game data; the
disc is built from a copy of the game you own.
