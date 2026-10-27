/*
 * OpenJazz CD32X R5 - the 8 / 16 MB cart as a cache of the world packs (docs/CART_8MB_16MB.md).
 * Only in the CART=8 / CART=16 test builds (OJ32X_CART_MB). Shared by the 32X and the PC build.
 *
 * Window 1 (cart 0x200000-0x3FFFFF) shows one 2 MB page. Page 1 (after reset) is the home
 * slot and the save area; pages 2-3 (8 MB) or 2-7 (16 MB) keep world packs, each with its
 * Genesis plane pack right after it, until a page is needed for something else.
 *
 * Each cache page records what it holds in its tail, piece 14 (window 1 + 0x1FE000):
 *   words 0-3 "PGTL" "OJAZ"   4 version (1)   5 entries   6-7 last use
 *   8.. per entry: id, offset (2 words), length (2), checksum (2), and in version 2 (R7 / P2b)
 *       the disc's stamp (2): the disc build the pack was last checked against
 * A pack is used again only when its length and its first and last 32 KB match the disc
 * (CMD 47) and its checksum matches the record: another title may have written the page.
 *
 * The board probe uses piece 15 of every page (on page 1 a free save-area piece):
 * a marker and its inverse written to pages 7 down to 1, then read back. All seven read
 * back: 16 MB. Pages 1-3: 8 MB (pages 4-7 mirror 0-3, or are empty). Otherwise 4 MB.
 */
#include "cartcache.h"
#include "pack.h"
#include "plat.h"
#include <stdio.h>
#include <string.h>

#if OJ32X_CART_MB

#define WIN1        0x200000u
#define PAGE_ROOM   0x1F0000u            /* the last 64 KB of a page: its tail */
#define TAIL_PIECE  14
#define PROBE_PIECE 15
#define MAX_ENTRIES 16
#define HEAD_WORDS  8
#define ENTRY_WORDS 9                    /* version 2; version 1 had 7 (no stamp) */

typedef struct { uint16_t id; uint32_t off, len, sum, stamp; } Entry;
typedef struct { int n; uint32_t used, last; Entry e[MAX_ENTRIES]; } Page;

static int pages;                         /* 2 (4 MB), 4 (8 MB) or 8 (16 MB); 0 = not probed */
static Page pg[8];
static uint32_t clock_now;

static void say(const char *fmt, int a, int b, int c)
{
	char msg[64];
	snprintf(msg, sizeof msg, fmt, a, b, c);
	plat_log(msg);
}

static uint32_t sum_of(uint32_t cart_offset, uint32_t len)
{
	const uint8_t *p = plat_cart_ptr(cart_offset);
	uint32_t s = 0x4F4A5A5Au;
	for (uint32_t i = 0; i + 4 <= len; i += 4) {
		const uint32_t w = (uint32_t)p[i] << 24 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 8 | p[i + 3];
		s = ((s << 5) | (s >> 27)) ^ w;
	}
	return s;
}

/* ED (OJ32X_CART_ED, the Mega EverDrive PRO as the cart - CD32X BootCamp docs/12): page 7 would be
 * the EverDrive's banks 28-31, and bank 31 is its battery RAM, the OS's own.  Pages 1-6 only. */
#ifdef OJ32X_CART_ED
#define PROBE_TOP 6
#else
#define PROBE_TOP 7
#endif
static int probe(void)
{
	int wrote[8] = { 0 }, good[8] = { 0 };
	for (int p = PROBE_TOP; p >= 1; p--) {
		const uint16_t w[3] = { 0x5047, (uint16_t)p, (uint16_t)~p };
		plat_cart_page(p);
		wrote[p] = plat_cart_piece_write(PROBE_PIECE, w, 3);
	}
	for (int p = 1; p <= PROBE_TOP; p++) {
		uint16_t w[3];
		plat_cart_page(p);
		plat_cart_piece_read(PROBE_PIECE, w, 3);
		good[p] = wrote[p] && w[0] == 0x5047 && w[1] == (uint16_t)p && w[2] == (uint16_t)~p;
	}
	plat_cart_page(1);
	int all = 1, low = good[1] && good[2] && good[3];
	for (int p = 1; p <= PROBE_TOP; p++) all &= good[p];
	int n = all ? PROBE_TOP + 1 : low ? 4 : 2;      /* ED: 7 = pages 1-6 */
	say("CART PROBE: %d MB (pages 1-3 %d, 4-7 %d)", n * 2, low, all);
	if (OJ32X_CART_MB == 8 && n > 4) n = 4;        /* the 8 MB build uses pages 2-3 only */
	return n;
}

static void tail_read(int p)
{
	uint16_t w[HEAD_WORDS + MAX_ENTRIES * ENTRY_WORDS];
	Page *g = &pg[p];
	memset(g, 0, sizeof *g);
	plat_cart_page(p);
	plat_cart_piece_read(TAIL_PIECE, w, HEAD_WORDS + MAX_ENTRIES * ENTRY_WORDS);
	if (w[0] != 0x5047 || w[1] != 0x544C || w[2] != 0x4F4A || w[3] != 0x415A || (w[4] != 1 && w[4] != 2) || w[5] > MAX_ENTRIES)
		return;                                 /* empty, or another title's page */
	g->n = w[5];
	g->last = (uint32_t)w[6] << 16 | w[7];
	for (int i = 0; i < g->n; i++) {
		const int ew = w[4] == 2 ? ENTRY_WORDS : 7;
		const uint16_t *e = w + HEAD_WORDS + i * ew;
		g->e[i].id = e[0];
		g->e[i].off = (uint32_t)e[1] << 16 | e[2];
		g->e[i].len = (uint32_t)e[3] << 16 | e[4];
		g->e[i].sum = (uint32_t)e[5] << 16 | e[6];
		g->e[i].stamp = w[4] == 2 ? (uint32_t)e[7] << 16 | e[8] : 0;
		if (g->e[i].off > PAGE_ROOM || g->e[i].len > PAGE_ROOM - g->e[i].off) { g->n = 0; g->used = 0; return; }
		const uint32_t end = (g->e[i].off + g->e[i].len + 0x7FFF) & ~0x7FFFu;
		if (end > g->used) g->used = end;
	}
	if (g->last > clock_now) clock_now = g->last;
}

/* the record of page p (mapped now) into its tail */
static void tail_write(int p)
{
	uint16_t w[HEAD_WORDS + MAX_ENTRIES * ENTRY_WORDS];
	const Page *g = &pg[p];
	w[0] = 0x5047; w[1] = 0x544C; w[2] = 0x4F4A; w[3] = 0x415A; w[4] = 2; w[5] = (uint16_t)g->n;
	w[6] = (uint16_t)(g->last >> 16); w[7] = (uint16_t)g->last;
	for (int i = 0; i < g->n; i++) {
		uint16_t *e = w + HEAD_WORDS + i * ENTRY_WORDS;
		e[0] = g->e[i].id;
		e[1] = (uint16_t)(g->e[i].off >> 16); e[2] = (uint16_t)g->e[i].off;
		e[3] = (uint16_t)(g->e[i].len >> 16); e[4] = (uint16_t)g->e[i].len;
		e[5] = (uint16_t)(g->e[i].sum >> 16); e[6] = (uint16_t)g->e[i].sum;
		e[7] = (uint16_t)(g->e[i].stamp >> 16); e[8] = (uint16_t)g->e[i].stamp;
	}
	if (!plat_cart_piece_write(TAIL_PIECE, w, HEAD_WORDS + g->n * ENTRY_WORDS))
		say("CART PAGE %d: TAIL NOT WRITTEN", p, 0, 0);
}

int cartcache_pages(void)
{
#ifdef OJ32X_CART_ED
	/* ED-NOCACHE (27 Oct 2026, as Hurrican's c33f340): no cache pages on the EverDrive.  The console
	 * stopped at the main menu (red screen, illegal instruction, last log "MAIN()") when the demo's
	 * timeout first asked whether a world was in the cart - this probe (page switches 6..1, piece 15
	 * written and read on each page) running for the first time since 379ebf3 took the boot's fill
	 * away.  Hurrican froze in the same probe at Start; ares (with the RV rule) runs both cleanly.
	 * The EverDrive wipes its memory whenever its menu runs, so the cache could only help within a
	 * session: every world loads from the disc into page 1's home slot, as on the 4 MB cart, and
	 * the menu plays no demo (as the 4 MB cart). */
	return 2;
#endif
	if (!pages) {
		pages = probe();
		for (int p = 2; p < pages; p++) {
			tail_read(p);
			say("CART PAGE %d: %d PACKS, %d KB USED", p, pg[p].n, (int)(pg[p].used >> 10));
		}
		plat_cart_page(1);
	}
	return pages;
}

static Entry *find(int p, int id)
{
	for (int i = 0; i < pg[p].n; i++)
		if (pg[p].e[i].id == id) return &pg[p].e[i];
	return NULL;
}

/* the pack in the cart still the one on the disc, unchanged? */
/* the pack in the cart still the one on the disc, unchanged? R7 / P2b: a pack last checked
 * against this very disc (the same stamp) needs only its checksum (another title may have
 * written the page) - no disc access; otherwise its length and first (and last) 32 KB are
 * compared with the disc, and when they agree the entry gets this disc's stamp (page p's tail
 * written again, the page mapped) */
static int still_good(int p, Entry *e)
{
	if (!e) return 0;
	const uint32_t stamp = plat_disc_stamp();
	if (stamp && e->stamp == stamp) return sum_of(WIN1 + e->off, e->len) == e->sum;
	if (plat_pack_open(e->id) != (int32_t)e->len ||
	    !plat_pack_same_as_disc(e->id, WIN1 + e->off, e->len) ||
	    sum_of(WIN1 + e->off, e->len) != e->sum)
		return 0;
	if (stamp) { e->stamp = stamp; tail_write(p); }
	return 1;
}

static int add(int p, int id, int32_t len)
{
	Page *g = &pg[p];
	if (g->n >= MAX_ENTRIES || len <= 0) return 0;
	const int32_t got = plat_load_pack_at(id, WIN1 + g->used, PAGE_ROOM - g->used);
	if (got != len) return 0;
	Entry *e = &g->e[g->n++];
	e->id = (uint16_t)id; e->off = g->used; e->len = (uint32_t)len;
	e->sum = sum_of(WIN1 + e->off, e->len);
	e->stamp = plat_disc_stamp();
	/* the next pack starts on a 32 KB boundary: CMD 46 / 47 work in 32 KB chunks, and the
	 * Sub-CPU's read-back sets the 1 MB bank once per chunk - a chunk must not cross one */
	g->used = (g->used + (uint32_t)len + 0x7FFF) & ~0x7FFFu;
	return 1;
}

static int cache_pair(int world_id, int gb_id, uint32_t *world_off, uint32_t *gb_off)
{
	*world_off = *gb_off = 0;
	if (cartcache_pages() <= 2) return 0;   /* a 4 MB cart: the home slot, as before */

	/* in the cart already? */
	for (int p = 2; p < pages; p++) {
		Entry *w = find(p, world_id);
		if (!w) continue;
		Entry *g = gb_id ? find(p, gb_id) : NULL;
		plat_cart_page(p);
		if (still_good(p, w) && (!g || still_good(p, g))) {
			pg[p].last = ++clock_now;
			*world_off = WIN1 + w->off;
			*gb_off = g ? WIN1 + g->off : 0;
			say("CART PAGE %d: PACK %d FROM THE CART", p, world_id - PACK_LEVEL_ID0, 0);
			return 1;
		}
		say("CART PAGE %d: PACK %d CHANGED - PAGE CLEARED", p, world_id - PACK_LEVEL_ID0, 0);
		pg[p].n = 0; pg[p].used = 0;
		tail_write(p);
	}

	/* load it: a page with room, else the one used longest ago */
	const int32_t wl = plat_pack_open(world_id);
	const int32_t gl = gb_id ? plat_pack_open(gb_id) : 0;
	if (wl <= 0) return 0;
	const uint32_t need = (((uint32_t)wl + 0x7FFF) & ~0x7FFFu) + (gl > 0 ? (((uint32_t)gl + 0x7FFF) & ~0x7FFFu) : 0);
	if (need > PAGE_ROOM) return 0;
	int best = -1;
	for (int p = 2; p < pages && best < 0; p++)
		if (PAGE_ROOM - pg[p].used >= need && pg[p].n + 2 <= MAX_ENTRIES) best = p;
	if (best < 0) {
		best = 2;
		for (int p = 3; p < pages; p++)
			if (pg[p].last < pg[best].last) best = p;
		say("CART PAGE %d: EVICTED (%d PACKS)", best, pg[best].n, 0);
		pg[best].n = 0; pg[best].used = 0;
	}
	plat_cart_page(best);
	const int first = pg[best].n;
	const uint32_t at = pg[best].used;
	int ok = add(best, world_id, wl);
	if (ok && gl > 0) ok = add(best, gb_id, gl);
	if (!ok) {                                  /* undo, keep the record true */
		pg[best].n = first; pg[best].used = at;
		tail_write(best);
		return 0;
	}
	pg[best].last = ++clock_now;
	tail_write(best);
	*world_off = WIN1 + pg[best].e[first].off;
	*gb_off = gl > 0 ? WIN1 + pg[best].e[first + 1].off : 0;
	say("CART PAGE %d: PACK %d LOADED (%d KB USED)", best, world_id - PACK_LEVEL_ID0, (int)(pg[best].used >> 10));
	return 1;
}

/* R7 - COMPRESSED (docs/R7_COMPRESSED_PACKS.md): the pages keep the compressed copies
 * (OZ<k>.PAK / GZ<k>.PAK, id + PACK_LZ_ADD), about three times as many worlds; the one being
 * played is unpacked into the home slot (page 1, PACK_LEVEL_CART) by the Sub-CPU (command 69).
 * The home slot keeps it until another world is unpacked or loaded there: entering the same
 * world again unpacks nothing. */
#ifndef OJ32X_CART_LZ
#define OJ32X_CART_LZ 1
#endif
static int home_world = -1, home_gb;          /* what the home slot holds unpacked (-1: unknown) */
static uint32_t home_gb_at;

void cartcache_home_changed(void) { home_world = -1; }

int cartcache_world(int world_id, int gb_id, uint32_t *world_off, uint32_t *gb_off)
{
	if (!OJ32X_CART_LZ) return cache_pair(world_id, gb_id, world_off, gb_off);
	*world_off = *gb_off = 0;
	if (cartcache_pages() <= 2) { home_world = -1; return 0; }
	if (world_id == home_world && gb_id == home_gb) {
		plat_cart_page(1);
		*world_off = PACK_LEVEL_CART;
		*gb_off = gb_id ? home_gb_at : 0;
		say("CART: PACK %d STILL UNPACKED", world_id - PACK_LEVEL_ID0, 0, 0);
		return 1;
	}
	home_world = -1;                            /* the home slot is about to change */
	uint32_t zw = 0, zg = 0;
	if (!cache_pair(world_id + PACK_LZ_ADD, gb_id ? gb_id + PACK_LZ_ADD : 0, &zw, &zg)) return 0;
	const int page = plat_cart_page_now();
	const uint32_t wl = plat_unpack(page, zw, 1, PACK_LEVEL_CART);
	if (!wl || wl > PACK_LEVEL_MAX) { say("CART: PACK %d NOT UNPACKED", world_id - PACK_LEVEL_ID0, 0, 0); return 0; }
	uint32_t gat = 0;
	if (zg) {
		gat = PACK_LEVEL_CART + ((wl + 0x7FFF) & ~0x7FFFu);
		const uint32_t gl = plat_unpack(page, zg, 1, gat);
		if (!gl || gat + gl > PACK_LEVEL_CART + PACK_LEVEL_MAX) { say("CART: GENESIS PACK %d NOT UNPACKED", gb_id, 0, 0); return 0; }
	}
	home_world = world_id; home_gb = gb_id; home_gb_at = gat;
	*world_off = PACK_LEVEL_CART;
	*gb_off = gat;
	say("CART: PACK %d UNPACKED (%d KB)", world_id - PACK_LEVEL_ID0, (int)(wl >> 10), 0);
	return 1;
}

/* FILL (OpenJazz R7, Hurrican P2b): on a boot, every pack of the list that no page holds yet goes
 * into the first page with room for it (with its Genesis pack, gb_ids[i], 0: none), in the
 * list's order, until no page has room. Packs already there are not checked against the disc
 * here (cartcache_world does that when one is used), so after the first boot this costs only the
 * page tails read at the probe. progress(i, n) before each pack it copies: nonzero stops the fill (the
 * player pressed Start). Returns the number of packs copied. */
int cartcache_fill(const int *ids, const int *gb_ids, int n, int (*progress)(int done, int total))
{
	if (cartcache_pages() <= 2) return 0;
	int copied = 0;
	/* a page that holds none of the list's packs (an older build's, or the other format's)
	 * is cleared first, so the fill has the room */
	for (int p = 2; p < pages; p++) {
		if (!pg[p].n) continue;
		int wanted = 0;
		for (int e = 0; e < pg[p].n && !wanted; e++)
			for (int i = 0; i < n && !wanted; i++) {
				const int d = OJ32X_CART_LZ ? PACK_LZ_ADD : 0;
				wanted = pg[p].e[e].id == ids[i] + d || (gb_ids && gb_ids[i] && pg[p].e[e].id == gb_ids[i] + d);
			}
		if (wanted) continue;
		say("CART PAGE %d: CLEARED FOR THE FILL (%d PACKS)", p, pg[p].n, 0);
		plat_cart_page(p);
		pg[p].n = 0; pg[p].used = 0; pg[p].last = 0;
		tail_write(p);
	}
	for (int i = 0; i < n; i++) {
		const int id = ids[i] + (OJ32X_CART_LZ ? PACK_LZ_ADD : 0);   /* R7: the compressed copies */
		int there = 0;
		for (int p = 2; p < pages && !there; p++) there = find(p, id) != NULL;
		if (there) continue;
		const int gb = gb_ids && gb_ids[i] ? gb_ids[i] + (OJ32X_CART_LZ ? PACK_LZ_ADD : 0) : 0;
		const int32_t wl = plat_pack_open(id);
		const int32_t gl = gb ? plat_pack_open(gb) : 0;
		if (wl <= 0) continue;
		const uint32_t need = (((uint32_t)wl + 0x7FFF) & ~0x7FFFu) + (gl > 0 ? (((uint32_t)gl + 0x7FFF) & ~0x7FFFu) : 0);
		int best = -1;
		for (int p = 2; p < pages && best < 0; p++)
			if (PAGE_ROOM - pg[p].used >= need && pg[p].n + 2 <= MAX_ENTRIES) best = p;
		if (best < 0) break;                        /* full: the rest load when they are played */
		/* R11: the copying screen only for a pack that is copied - a later boot, with the cart
		 * as full as it gets, shows nothing */
		if (progress && progress(i, n)) break;
		plat_cart_page(best);
		const int first = pg[best].n;
		const uint32_t at = pg[best].used;
		int ok = add(best, id, wl);
		if (ok && gl > 0) ok = add(best, gb, gl);
		if (!ok) { pg[best].n = first; pg[best].used = at; }
		else copied++;
		tail_write(best);                           /* the page's record, true either way */
	}
	plat_cart_page(1);
	if (copied) say("CART FILL: %d PACKS COPIED", copied, 0, 0);
	return copied;
}

/* R10: the world pack is in the cart (a cache page, or the home slot unpacked) */
int cartcache_has(int world_id)
{
	if (cartcache_pages() <= 2) return 0;
	if (world_id == home_world) return 1;
	const int id = world_id + (OJ32X_CART_LZ ? PACK_LZ_ADD : 0);
	for (int p = 2; p < pages; p++)
		if (find(p, id)) return 1;
	return 0;
}

#endif
