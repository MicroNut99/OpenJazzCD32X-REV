# 8 MB and 16 MB cart releases (design)

The design for Jazz Jackrabbit CD32X on the paged SRAM cart
([16MB-PCB-CD32X](https://github.com/MicroNut99/16MB-PCB-CD32X), rev 0.5: 16 MB with both chips,
8 MB with U1 only). Jazz is the reference: Hurrican CD32X uses the same mechanism, with its own
numbers in its `docs/CART_8MB_16MB.md`.

**Status:** design only, nothing implemented. The board is not built yet. Numbers marked *measured*
come from the REV packs (`cd32x/subcpu/ROMS`, built 15 Oct 2026). Numbers marked *estimate* come
from a simulation of the trims over those packs.

## 1. The idea

The cart keeps its content when the console is off (battery), so it already works as a disc
cache. Today that cache holds the engine, the common pack and **one** planet: every planet
loads from the disc when it is reached (30-45 s), and loads again on every later visit.

With 8 or 16 MB the cart holds many planets at once, and each one stays in the cart until it
is evicted:

- **First visit:** loads as today.
- **Every later visit:** starts at once, including after power-off, game over, loading a saved
  game or a later play session. Nothing is read from the disc.
- **INSTALL TO CART** (main menu, optional): fills the cart in one sitting, about 7-8 minutes
  for 16 MB at today's ~30 KB/s (*estimate*). It is quicker if the bigger CD chunks from the
  J13 build notes are done first. After that, the game never shows a loading screen while the
  data stays in the cart.

| Cart | Room for packs | What stays in the cart (*estimate*, after the trims of section 5) |
|---|---|---|
| 4 MB (today) | 1.4 MB | the current planet only |
| 8 MB | ~5.5 MB | the ~10-12 planets / bonus sets played last |
| 16 MB | ~13.5 MB | Jazz CD's 24 planets, Holiday Hare '94 and the bonus graphics, plus most bonus maps. Whatever else is played stays in the cart until evicted |

## 2. What the board gives software

From the PCB README (section 2):

- The console still sees a 4 MB cart: **window 0** (cart 0x000000-0x1FFFFF) and **window 1**
  (0x200000-0x3FFFFF). Each window shows one 2 MB **page** of the 16 MB.
- Page registers (written by the 68000 only, upper byte):
  - `0xA130F0` = window 0's page.
  - `0xA130F2` = window 1's page.
  - After power-on and /RESET they hold **W0 = 0, W1 = 1**: the first 4 MB, exactly like a 4 MB
    cart.
- **8 MB build:** pages 4-7 mirror pages 0-3.
- **The SH2s** see the 4 MB at `0x02000000` (cached) and `0x22000000` (cache-through), and
  `0x02200000` is window 1. They cannot write the cart or the page registers. After a page
  switch they must purge their cache.

## 3. Memory plan: home pages + extended pages

**W0 stays 0 forever.** The engine runs from window 0, and the SH2 executes from it. Only W1
changes.

### Home pages (0 and 1): the 4 MB cart, unchanged

With W1 = 1 the cart is the familiar 4 MB cart. That keeps three things working:

- **Other titles on the same cart.** Titles that know nothing of paging run as they do today.
  They only see pages 0 and 1.
- **The shared save area** at `0x3F0000` (16 pieces of 4 KB, the Command Ledger's table) stays
  at the same address for every title: page 1, offset 0x1F0000.
- **A 4 MB cart.** The same disc runs on a 4 MB cart exactly as the REV builds do.

| Cart (W1 = 1) | Size | Holds |
|---|---|---|
| `0x000000`-`0x07FFFF` | 512 KB | engine (now 480 KB) |
| `0x080000`-`0x0FFFFF` | 512 KB | **shared pack** (new): the files and pictures 8 or more world packs carry the same copy of (MAINCHAR.000, the player's decoded frames, fonts) - 0.36 MB *measured* |
| `0x100000`-`0x25FFFF` | 1.4 MB | common pack (now 1.36 MB) |
| `0x260000`-`0x3EFFFF` | 1.56 MB | **home slot**: cached packs, the same as an extended page |
| `0x3F0000`-`0x3FFFFF` | 64 KB | the shared save area (piece 14: the page directory, section 4) |

Today the Genesis plane pack (GB) sits at 0x080000. It moves next to its world pack, in the same
page or slot (section 6).

### Extended pages: the cache

| Board | Extended pages | Size |
|---|---|---|
| 8 MB | 2, 3 | 4 MB |
| 16 MB | 2-7 | 12 MB |

To use a pack in an extended page, the SH2 maps that page into window 1:

```
W1 = page          (Sub-CPU CMD 68 -> 68000 cart_page)
purge the SH2 caches (master now, slave before its next job - as after a load today)
mount the pack at 0x02200000 + offset
```

While an extended page is mapped, page 1 (the home slot and the save area) is not visible. Two
rules follow, both enforced in `plat_mars.c`:

1. **Saves are written and read with W1 = 1.**
   - The 68000 side of CMD 61 (save piece → cart) sets W1 = 1, writes, and puts the page back.
   - The SH2 maps W1 = 1 itself before it reads a save (`plat_save_read`). It reads
     cache-through, so no purge is needed.
2. **A world is played from one place.** Its world pack and its GB pack are in the same page or
   slot. The common and shared packs are in page 0, which is always visible.

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
       last use u32   reserved[4]
```

It is shared by every title on the cart, like the save pieces:

- **Finding a pack:** a title finds its pack by title, pack id and disc mark. A rebuilt disc
  has a new mark, so stale copies never match.
- **Making room:** the title evicts the least-recently-used entries (of any title) whose
  pages overlap the room it needs.
- **Packs in the home slot are verified before use.** A title that knows nothing of the
  directory may have overwritten them, so their checksum is checked on every use (about 0.1 s
  for 1 MB on the SH2, *estimate*). **Packs in extended pages are trusted.** Only titles that
  keep the directory up to date write there.
- **Who writes it:** the SH2 writes the directory through CMD 61, like any save piece (it
  cannot write the cart itself).

### Detecting the board

The 68000 detects the board once at boot, using its routine `cart_page` with a probe flag. The
probe overwrites nothing: it saves and restores the words it uses.

1. Read the probe word (last word of piece 14, `0x3FEFFE` with page p in window 1) of pages
   0-7 and keep the 8 values.
2. Write marker `0xC0DE + p` into page p, for p = 0 ... 7 in order.
3. Read the 8 words back.
   - All 8 markers intact: **16 MB**.
   - Pages 4-7 show pages 0-3's markers: **8 MB**.
   - Every write landed in page 1, because the register was ignored: **4 MB** (no mapper).
4. Write the 8 saved values back in reverse order. Leave W1 = 1.

On a 4 MB cart the game runs as the REV builds do (one slot at 0x280000, as now).

## 5. Fitting the packs (Jazz numbers)

*Measured* (REV packs, 15 Oct 2026; world pack + its GB pack):

| Set | Packs | Size now |
|---|---|---|
| Jazz CD (6 episodes, 24 planets) | 24 + 24 GB | 23.2 MB |
| Holiday Hare '95 | 12 + 12 GB | 12.3 MB |
| Holiday Hare '94 | 3 + 3 GB | 2.6 MB |
| Bonus maps | 30 | 22.5 MB |
| **Total** | | **60.6 MB** |

Most of that is copies. The trims, all on the PC side (`tools/bake_levels.sh` and the pack
tools), none of them changing what the game shows:

| Trim | What | Saves (*estimate*, each after the ones above it in order T3, T4, T1, T2) |
|---|---|---|
| T1 | `BLOCKS.xxx` raw file: keep only its palettes. The game reads nothing else from it once the tile set comes ready-made (J13 build notes) | 4.7 MB |
| T2 | The 32X's decoded tile set: keep only the foreground tiles. Since R3 the Genesis draws every background tile and the 32X draws only tiles in front of the sprites: **9 % of the tiles** (1,393 of 15,660 over the 78 levels, *measured* from the level dumps). Needs a tile index table in the 32X's tile drawing (foreground tile → place in the reduced picture) | 7.1 MB |
| T3 | The shared pack: whatever 8 or more world packs carry the same copy of (MAINCHAR.000 152 KB, the player's decoded frames, fonts) goes once into page 0 (0.36 MB) | 13.7 MB |
| T4 | Bonus: the 9 graphics sets (`BONUS.000`, `BONUS2-6`, `CDBONUS1-3`) and their decoded pictures are each shared by ~3 maps. Keep one copy per set as a set pack. Each map pack keeps only its map and its 256 × 256 picture | 17.2 MB |

After T1-T4 (*estimate*, from a simulation over the real packs):

| Set | Size | Largest single pack |
|---|---|---|
| Jazz CD | 7.7 MB | 0.47 MB |
| Bonus (9 set packs 2.05 MB + 30 maps 3.18 MB) | 5.2 MB | 0.13 MB a map |
| Holiday Hare '94 | 0.8 MB | 0.40 MB |
| Holiday Hare '95 | 4.1 MB | 0.46 MB |
| **Total** | **17.8 MB** | |

- **16 MB** has room for 13.5 MB of packs: everything except about 4 MB.
- **8 MB** has room for 5.5 MB: a working set of 10-12 planets.

The trims also make every first load shorter: a planet goes from 0.7-1.0 MB to 0.3-0.5 MB, so
15-20 s instead of 30-45 s. A bonus map goes from 0.76 MB to 0.13 MB once its set is in the
cart, about 4 s.

The trims also help the 4 MB cart: smaller packs load faster there too.

## 6. Loading and placing a pack

`oj32x_level_pack` (port/sdl_oj.cpp) becomes:

1. **Look it up in the directory.** If it is there and valid (a home-slot pack also needs its
   checksum to match):
   1. Map its page (CMD 68).
   2. Purge the caches.
   3. Mount it.
   4. Bump its LRU stamp. Done: no loading screen.
2. **If it is not there, choose a place:** first-fit in the extended pages, then the home slot.
   If nothing fits, evict LRU entries until it does. A pack never crosses a page boundary.
3. **Load it:**
   1. Map the target page (CMD 68).
   2. Read it with the existing CMD 38 / 46, to cart offset 0x200000 + offset.
   3. The world's GB pack is read right after it, into the same page.
4. **Record it:** write the directory piece (CMD 61, W1 = 1 for the write), map the page again,
   mount.

The **set pack** a bonus map needs (T4) and the **shared pack** in page 0 (T3) go through the
same steps. A bonus map whose set pack is not in the cart loads both.

`genbg` (port/genbg.c) needs no change. It hands the 68000 `plat_cart_offset_of(data)` = 0x2xxxxx,
and the 68000 reads that through the 0x900000 bank window, which shows the page now in window 1.
The vblank handler never reads the cart, so a page switch cannot catch it mid-read.

## 7. New commands (registered in the CD32X Command Ledger before use)

| Where | Number | Name | Arguments | Reply |
|---|---|---|---|---|
| SH2 → Sub-CPU | **68** | cart page | COMM2 = page 0-7 (bit 15: probe) | COMM12 = page now in W1; probe: number of pages (2 / 4 / 8) |
| Sub-CPU → 68000 | **42** | `cart_page` | page, probe flag | writes `0xA130F2` (upper byte), keeps the page in work RAM for CMD 61's restore |

- **CMD 61** (save piece) changes only on the 68000 side: it sets W1 = 1 around its write, then
  restores the page it found.
- **Ledger clash to fix first:** REV R1-R4 use **CMD 67** for the Genesis plane and **68000
  routines 37-40**. The ledger has CMD 67 = cart → VRAM columns and MD 37 = `copy_vram_col`
  (SuperTux REV4). Move the Genesis plane to **CMD 69** and **MD 38-41**, and register them,
  before 68 / 42 are added. Then `MD_CMD_END` = 43.

## 8. Where each change goes

| Part | Change |
|---|---|
| `cd32x/subcpu/hw_md.s` | `cart_page` (42), with the probe; CMD 61's write path with W1 = 1 |
| `cd32x/subcpu/main.c` | CMD 68; the Genesis plane renumbered to CMD 69 |
| `port/plat_mars.c` | `plat_cart_page(p)` (CMD 68 + master purge + a flag the slave checks before its next job); `plat_save_read` with W1 = 1; directory read / write |
| `port/cartdir.c` (new) | the directory: find, place (first-fit + LRU), checksum, record |
| `port/sdl_oj.cpp` | `oj32x_level_pack` as in section 6; the shared pack mounted at start-up |
| `port/pack.h` | slots: shared 0x080000 (512 KB), home slot 0x260000-0x3EFFFF, extended pages |
| 32X tile drawing (`jj1levelframe.cpp`) | T2: foreground tile → place in the reduced tile picture |
| `tools/` (PC) | T1-T4 in the pack bake; a build-time check that every pack fits a page and the shared pack fits 512 KB |
| main menu | INSTALL TO CART (8 / 16 MB only): loads every pack in priority order (Jazz CD, bonus sets, HH '94, bonus maps, HH '95) until the cart is full |
| `port/plat_host.c` | the PC build models the pages and the probe (4 / 8 / 16 MB by an environment switch), so all of this is tested before the board exists |

**Releases:** one disc program for 4, 8 and 16 MB, which detects the board (section 4). "The 8 MB
release" and "the 16 MB release" are then what that disc does on each board. The build can still
be named per cart (`NAME=R5-16MB`) if the discs should be labelled.

## 9. To verify on the first board

1. **The registers through the 32X.** Check that a 68000 byte write to `0xA130F2` reaches the
   cart with the 32X and Sega CD attached: the probe must report 8 or 16. The 32X sits between
   the console and the cart (/TIME space). Nothing here has used 0xA130F0-F2 before.
2. **The 68000's cart writes** through the 32X bank window `0x900000` with the page mapped,
   CMD 46 with read-back on.
3. **The SH2** reads the right page at `0x02200000` after the switch and the purge, master and
   slave.
4. **Saves:**
   1. Write a save while an extended page is mapped.
   2. Switch off and on, then load the save.
   3. Check another title's piece (SuperTux, piece 13) is untouched.
5. **A 4 MB cart** (MD-32X-RAM-Cart-32Bit) with the same disc: probe = 4, plays as REV.
