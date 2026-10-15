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

- 48 of the 78 levels of Jazz CD are drawn by the Genesis (every level with a sky or a plain
  background). Levels whose background is made of palette tricks that follow the camera
  (parallax, diagonal stripes) and water levels still draw everything on the 32X, as before.
- The Genesis has 61 colours on screen (4 palettes of 15, from 512) where the 32X had 256: the
  tiles are converted to the nearest Genesis colours; the waterfall and lamp colour cycles keep
  running (the 68000 rotates them).
- Details: [docs/GENESIS_PLANE.md](docs/GENESIS_PLANE.md).

**First start of a REV disc: hold A + B + C** while switching on (full reload), so the new
program replaces the one in the RAM cart.

Build: `NAME=R1 bash tools/make_disc.sh` in this folder (WSL) - the disc goes to
`discs/R1/OpenJazzCD32X_R1.cue` inside the folder.

## What you need

- A Sega Genesis / Mega Drive with **Sega CD** and **32X**
- A **4 MB RAM cart** in the 32X cartridge slot (the game keeps its data there)
- The game disc, burned from `OpenJazzCD32X.cue` (disc-at-once, 80-minute CD-R)

Emulators without a 4 MB RAM cart (for example Fusion) cannot run it: the loader stops on the
splash screen.

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
