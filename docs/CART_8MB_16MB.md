# 8 MB and 16 MB cart releases (design)

The design for Jazz Jackrabbit CD32X on the paged SRAM cart
([16MB-PCB-CD32X](https://github.com/MicroNut99/16MB-PCB-CD32X), rev 0.5: 16 MB with both chips,
8 MB with U1 only). Jazz is the reference: Hurrican CD32X uses the same mechanism, with its own
numbers in its `docs/CART_8MB_16MB.md`.

**Status:** design only, nothing implemented. The board is not built yet. Numbers marked
*measured* come from the REV packs (`cd32x/subcpu/ROMS`, built 15 Oct 2026). Numbers marked
*estimate* come from a simulation of the trims over those packs.

**Reviewed:** an independent review of this design (Claude Fable, 19 Oct 2026) found 10 problems
in the first draft: page 0 overfull, CMD 46's fixed slots, the tile trim, the write path, the
probe wording and others. All of them are fixed in this version.

## 1. The idea

The cart keeps its content when the console is off (battery), so it already works as a disc
cache. Today that cache holds the engine, the common pack and **one** planet: every planet
loads from the disc when it is reached (30-45 s), and loads again on every later visit.

With 8 or 16 MB the cart holds many planets at once, and each one stays in the cart until it
is evicted:

- **First visit:** loads as today, but shorter after the trims of section 5.
- **Every later visit:** starts at once, including after power-off, game over, loading a saved
  game or a later play session.
- **INSTALL TO CART** (main menu, optional): fills the cart in one sitting, about 7-8 minutes
  for 16 MB at today's ~30 KB/s (*estimate*).

| Cart | Room for packs | What stays in the cart (*estimate*, after the trims) |
|---|---|---|
| 4 MB (today) | 1.4 MB | the current planet only |
| 8 MB | ~5.3 MB | the ~10 planets / bonus sets played last |
| 16 MB | ~13.3 MB | Jazz CD's 24 planets, Holiday Hare '94, most of the bonus game. Whatever else is played stays until evicted |

## 2. What the board gives software

From the PCB README (sections 2 and 12):

- The console still sees a 4 MB cart: **window 0** (cart 0x000000-0x1FFFFF) and **window 1**
  (0x200000-0x3FFFFF). Each window shows one 2 MB **page** of the 16 MB.
- **Page registers** (even byte, clocked by /UWR):
  - `0xA130F0` = window 0's page, `0xA130F2` = window 1's page.
  - After power-on and /RESET: **W0 = 0, W1 = 1**, the first 4 MB, exactly a 4 MB cart.
- **8 MB build:** pages 4-7 mirror pages 0-3.
- **With the 32X adapter on (ADEN = 1):** the README's open point says to write the page
  registers the way the Kobo port writes the cart: **RV = 1** (bit 0 of `0xA15107`) around the
  write, interrupts off. While RV = 1 the SH2s must not touch the cart.
- **The SH2s** see the 4 MB at `0x02000000` (cached) and `0x22000000` (cache-through), and
  `0x02200000` is window 1. They cannot write the cart or the page registers. After a page
  switch they purge their cache.

## 3. Memory plan: home pages + extended pages

**W0 stays 0 forever.** The engine runs from window 0. Only W1 changes.

### Home pages (0 and 1): the 4 MB view, laid out anew

With W1 = 1 the cart is a 4 MB cart, so:

- **Other titles** that know nothing of paging run as today. They see only pages 0 and 1.
- **The shared save area** at `0x3F0000` (the Command Ledger's 16 pieces) stays where every
  title expects it: page 1, offset 0x1F0000.
- **A 4 MB cart** runs the same disc with this same layout. The probe finds no extended pages
  and only the home slot is used.

Everything a level needs besides its own packs must be in **page 0**, because page 1 disappears
while an extended page is mapped. Today's common pack (1.36 MB) does not fit beside the engine
and the shared files. It is split in two:

| Cart (W1 = 1) | Size | Holds |
|---|---|---|
| `0x000000`-`0x07FFFF` | 512 KB | engine (480 KB now) |
| `0x080000`-`0x1FFFFF` | 1.5 MB | **level-time pack** (page 0): what the game uses during a level (panel, fonts, game files it opens in levels) **plus** the shared files of T3 (0.36 MB) |
| `0x200000`-… | ~0.7 MB | **menu pack** (page 1): menus, planet screens, demos, title. Used only outside levels, with W1 = 1 |
| …-`0x3EFFFF` | ~1.25 MB | **home slot**: cached packs, as in an extended page |
| `0x3F0000`-`0x3FFFFF` | 64 KB | shared save area (piece 14: the page directory, section 4) |

The split is made by the bake (`T32X_BAKE` runs a level and a menu pass). The build stops if
the engine and the level-time pack together pass 0x200000. The J13 notes estimate about half
of the common pack is menu and planet-screen graphics. That puts the level-time pack near
0.7 + 0.36 = 1.1 MB, which leaves room, but this is an *estimate* until the bake makes the
split.

Today the Genesis plane pack (GB) sits at 0x080000. It moves next to its world pack, in the
same page or slot.

### Extended pages: the cache

| Board | Extended pages | Size |
|---|---|---|
| 8 MB | 2, 3 | 4 MB |
| 16 MB | 2-7 | 12 MB |

### Rules

1. **A level is played from one page.** A world pack and its GB pack are in the same page or
   slot. So are a bonus map and its graphics set pack (T4). Eviction removes a set pack only
   together with the maps that use it.
2. **The save area is read and written with W1 = 1.** One helper, `save_area_map()` /
   `save_area_unmap()`, wraps every reader:
   - `mars_syscalls.c` (`SAVE_AREA`, `piece_at`: `openjazz.cfg` and `SAVE.0`-`SAVE.3`, also
     used by the pause menu's Save / Load / Setup during a level, `level.cpp`);
   - `plat_mars.c`'s `plat_port_setting` reader (`0x223F0000 + piece`).

   The 68000 side of CMD 61 (save piece → cart) sets W1 = 1 around its write and puts back
   the page it found. The SH2s read the save area cache-through, so no purge is needed.
3. **The menu pack is used with W1 = 1 only.** Returning to a menu or the planet screen maps
   W1 = 1 first.
4. **Before anything leaves the game without a reset** (CMD 60 starting D32XR, any return to
   the BIOS), W1 = 1 is set back.

## 4. The page directory: save piece 14 (shared by every title)

One 4 KB piece of the save area (`0x3FE000`, free in the ledger's table) records what is
where, so that a pack loaded once is found again after power-off.

```
0    "CPGD"  version u16  pages u8 (detected: 2, 4 or 8)  entries u8
8    clock u32                      (bumped at every use: the LRU stamp)
12   127 entries of 32 bytes:
       title[4]   ("OJAZ", "HURR", ...)    pack id u16    page u8    flags u8
       offset u32 (inside the page)        size u32
       checksum u32 (the pack as on the disc)   disc mark u32 (the disc's build id)
       last use u32   group u16 (packs that must stay together: world + GB, set + maps)
       reserved[2]
```

- **Finding a pack:** a title finds its pack by title, pack id and disc mark. A rebuilt disc
  has a new mark, so stale copies never match.
- **Making room:** the title evicts least-recently-used groups (of any title) that overlap the
  room it needs.
- **Packs in the home slot are verified before use.** A title that knows nothing of the
  directory may have overwritten them. A checksum costs ≥ 0.2 s per MB, because SH2 cart reads
  take ≥ 350 ns each (PCB README section 12). **Packs in extended pages are trusted:** only
  titles that keep the directory write there.
- **Who writes it:** the SH2 writes the directory through CMD 61, like any save piece.

### Detecting the board (68000, at boot, RV = 1, interrupts off)

The probe overwrites nothing for good: the words it uses are saved and put back.

1. With W1 = p, read the probe word (the last word of piece 14, cart `0x3FEFFE`) of pages
   0-7 and keep the 8 values.
2. Write marker `0xC0DE + p` into page p, for p = 0 ... 7 in that order.
3. Read the 8 words back:
   - all 8 hold their own marker: **16 MB**;
   - pages 4-7 hold their own markers and pages 0-3 hold `0xC0DE + p + 4` (the later writes to
     4-7 landed in 0-3): **8 MB**;
   - every page reads `0xC0DE + 7` (all writes landed in page 1: the register write never
     reached the cart): **4 MB**;
   - anything else (for example a 16 MB mapper with U2 not fitted): treat as **4 MB** and show
     a warning on the splash.
4. Write the 8 saved values back in reverse order, and leave W1 = 1.

## 5. Fitting the packs (Jazz numbers)

*Measured* (REV packs, 15 Oct 2026; world pack + its GB pack):

| Set | Packs | Size now |
|---|---|---|
| Jazz CD (6 episodes, 24 planets) | 24 + 24 GB | 23.2 MB |
| Holiday Hare '95 | 12 + 12 GB | 12.3 MB |
| Holiday Hare '94 | 3 + 3 GB | 2.6 MB |
| Bonus maps | 30 | 22.5 MB |
| **Total** | | **60.6 MB** |

Most of that is copies. The trims are all on the PC side (bake and pack tools). T2 also needs a
change to the 32X's tile drawing.

| Trim | What | Saves (*estimate*, in the order T3, T4, T1, T2) |
|---|---|---|
| T3 | The shared files: what 8 or more world packs carry the same copy of (MAINCHAR.000 152 KB, the player's decoded frames, fonts) goes once into the level-time pack in page 0 (0.36 MB) | 13.7 MB |
| T4 | Bonus: the 9 graphics sets (`BONUS.000`, `BONUS2-6`, `CDBONUS1-3`) and their decoded pictures are each shared by ~3 maps. One set pack per set; each map pack keeps only its map and its 256 × 256 picture | 17.2 MB |
| T1 | `BLOCKS.xxx`: keep only what `loadTiles` still reads. That is the palette and the RLE blob it skips (the second sky palette, `jj1levelload.cpp`). The bake rewrites the file picture's `end` to the new file size, which `loadTiles` checks (`end == file size`) | ~4.7 MB |
| T2 | The 32X's decoded tile set: since R3 the Genesis draws every background tile. The 32X keeps full pixels only for the **foreground tiles** (9 %: 1,393 of 15,660 tiles over the 78 levels, *measured*) and the **4 sky-orb tiles**. For every other tile it keeps a **1-bit see-through mask**, which `scrollOrb` reads to let the sun show through background tiles. The tile count, today `ph >> 5` of the picture, is stored in the pack | ~6.2 MB |

T2 has a fallback. A level whose GB file is missing or refused (`genbg_active()` false: the 32X
draws every tile, `jj1levelframe.cpp`) needs the full tile set. The build knows which levels
those are (78 of 78 convert today) and keeps the full set in their packs.

After the trims (*estimate*, simulation over the real packs):

| Set | Size | Largest single pack |
|---|---|---|
| Jazz CD | ~8.3 MB | ~0.5 MB |
| Bonus (9 set packs 2.05 MB + 30 maps 3.18 MB) | 5.2 MB | 0.13 MB a map |
| Holiday Hare '94 | ~0.8 MB | ~0.4 MB |
| Holiday Hare '95 | ~4.4 MB | ~0.5 MB |
| **Total** | **~18.7 MB** | |

- **16 MB** has room for ~13.3 MB of packs (12 MB extended + ~1.25 MB home slot): everything
  except about 5 MB.
- **8 MB** has room for ~5.3 MB: a working set of about 10 planets.

The trims also make every first load shorter, including on the 4 MB cart. A planet goes from
0.7-1.0 MB to 0.3-0.5 MB (15-20 s instead of 30-45 s). A bonus map is 0.13 MB once its set is
in the cart (about 4 s).

## 6. Loading and placing a pack

`oj32x_level_pack` (port/sdl_oj.cpp) becomes:

1. **Look up the pack's group** (world + GB, or bonus set + map) in the directory. If all of it
   is there and valid (home-slot packs also checked by checksum):
   1. Map its page (`plat_cart_page`).
   2. Mount it.
   3. Bump the LRU stamp.

   Done: no loading screen.
2. **Choose a place for the missing parts.** Use the page that already holds the rest of the
   group. Otherwise first-fit in the extended pages, then the home slot. Evict LRU groups until
   it fits. A pack never crosses a page boundary.
3. **Load it:**
   1. Map the target page.
   2. Read the pack with CMD 38 / 46 into cart `0x200000 + offset`.

   **CMD 38 needs a new argument for this.** Today it fixes the destination by file id (OJ →
   slot 20, GB → slot 4, IMAGE8 → slot 8, `main.c`). It gets a destination: COMM8L = cart
   offset (4-byte aligned), used when COMM2 bit 15 is set. CMD 46 then writes chunk n at
   destination + n × 32 KB. Without bit 15 it behaves as today.
4. **Record it:** write the directory piece (CMD 61, W1 = 1 for the write), map the page again,
   mount.

`genbg` (port/genbg.c) needs no change. It hands the 68000 `plat_cart_offset_of(data)` =
0x2xxxxx, and `gb_copy` reads that through the 0x900000 bank window (bank = (offset >> 20) & 3),
which shows the page now in window 1. The vblank handler reads only work RAM and COMM
registers, and `genbg_load` runs with interrupts off.

## 7. Commands (registered in the CD32X Command Ledger before use)

| Where | Number | Name | Arguments | Reply |
|---|---|---|---|---|
| SH2 → Sub-CPU | **68** | cart page | COMM2 = page 0-7 (bit 15: probe) | COMM12 = page now in W1; probe: pages (2 / 4 / 8) |
| Sub-CPU → 68000 | **42** | `cart_page` | page, probe flag | RV = 1, interrupts off, `move.b d0,0xA130F2`, RV back; keeps the page in work RAM for CMD 61 |
| SH2 → Sub-CPU | **38** (extended) | open file | COMM2 bit 15 + COMM8L = cart destination | as today |

How a page switch runs:

1. The master SH2 waits for CMD 68's reply in SDRAM code, never on the cart: RV = 1 locks the
   SH2s out of the cart during the write.
2. The slave is idle between levels, in its cached wait loop.
3. After the reply the master purges its cache. The slave already purges before every job
   (`plat_mars.c`).

- **CMD 61** (save piece) changes only on the 68000 side: W1 = 1 around its write.
- **Ledger clash to fix first:** REV R1-R4 use CMD 67 for the Genesis plane and 68000 routines
  37-40. The ledger has CMD 67 = cart → VRAM columns and MD 37 = `copy_vram_col` (SuperTux
  REV4). Move the Genesis plane to **CMD 69** and **MD 38-41** and register them, before
  68 / 42 are added. `MD_CMD_END` then becomes 43.

## 8. Where each change goes

| Part | Change |
|---|---|
| `cd32x/subcpu/hw_md.s` | `cart_page` (42) with the probe; CMD 61's write with W1 = 1 |
| `cd32x/subcpu/main.c` | CMD 68; CMD 38's destination argument; the Genesis plane renumbered to CMD 69; W1 = 1 before CMD 60 |
| `port/plat_mars.c` | `plat_cart_page(p)` (master waits in SDRAM, then purges); `save_area_map/unmap`; directory read / write; `plat_port_setting` through the wrapper |
| `port/mars_syscalls.c` | `piece_at` / `SAVE_AREA` readers through `save_area_map` |
| `port/cartdir.c` (new) | the directory: find, place (groups, first-fit, LRU), checksum, record |
| `port/sdl_oj.cpp` | `oj32x_level_pack` as in section 6; the level-time pack mounted at start-up, the menu pack in menus |
| `port/pack.h` | slots: level-time 0x080000-0x1FFFFF, menu pack at 0x200000, home slot up to 0x3EFFFF, extended pages |
| `jj1levelframe.cpp`, `jj1levelload.cpp` | T2: the reduced tile picture (foreground tile → its place, mask for the others, stored tile count) |
| `tools/`, `build.sh` (PC) | T1-T4; the level-time / menu split; checks: engine + level-time pack ≤ 0x200000, every pack fits a page |
| main menu | INSTALL TO CART (8 / 16 MB): loads every pack group in priority order (Jazz CD, bonus sets and maps, HH '94, HH '95) until the cart is full |
| `port/plat_host.c` | the PC build models the pages and the probe (4 / 8 / 16 MB by an environment switch), so all of this is tested before the board exists |

**Releases:** one disc program for 4, 8 and 16 MB, which detects the board (section 4). "The 8 MB
release" and "the 16 MB release" are what that disc does on each board. The build can still be
named per cart (`NAME=R5-16MB`) if the discs should be labelled.

## 9. To verify on the first board

1. **Page registers through the 32X:** a write to `0xA130F2` with ADEN = 1 and RV = 1 reaches
   the cart. The probe must report 8 or 16. Also try the write before the adapter is enabled,
   and Sega's odd-byte form, as the PCB README asks.
2. **/CAS2 at B21** covers the /LWR and /UWR pulses of a cart write in Sega CD + 32X mode
   (scope; PCB README section 12).
3. **The 68000's cart writes** with a page mapped. Writes go through `chilly_rv_memcpy` with
   RV = 1 to `0x400000 + offset`. CMD 46's read-back goes through `0x900000`.
4. **The SH2 reads** the right page at `0x02200000` after the switch and the purge (master and
   slave).
5. **Saves:**
   1. In a level from an extended page, use the pause menu's Save, Load and Setup.
   2. Switch off and on, then load the save.
   3. Check another title's piece (SuperTux, piece 13) is untouched.
6. **A 4 MB cart** (MD-32X-RAM-Cart-32Bit) with the same disc: probe = 4, plays from the home
   slot.
