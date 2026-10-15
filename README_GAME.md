# Jazz Jackrabbit CD32X

Jazz Jackrabbit (1994, Epic MegaGames) for the **Sega CD + 32X**, built on the OpenJazz engine.
All episodes of the CD edition, Holiday Hare '94 and '95, the bonus levels, CD audio music and
sound effects.

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

## Known limitations

- The game runs at about 10-13 frames per second in levels; scrolling is not smooth.
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
