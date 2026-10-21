/*
 * OpenJazz CD32X - the SDL 1.2 subset OpenJazz uses, on the platform layer
 * (see port/include/SDL.h), plus the port's glue between OpenJazz and the platform:
 *   - surfaces and blits (8-bit), pictures from the cart packs (oj32x_cart_surface)
 *   - the screen mapping of the line-table scrolling renderer
 *   - presenting frames; the pad as key events
 *   - which pack a level file needs (oj32x_level_pack); music as CD audio tracks
 *   - the frame profiler
 *
 * Surfaces are 8-bit only. OpenJazz recolours sprites and fonts through SDL 1.2's
 * palette-mapped blits: every surface has a "logical" palette that is grey ramp
 * i -> (i,i,i), and a recolour sets some entries to another grey (k,k,k), so the
 * blit turns index i into k. Here that is a 256-byte table per surface (remap), and
 * blits copy straight when it is the identity - the common, fast case.
 *
 * The pad becomes key events (oj32x_pad_mode picks the play or the menu layout).
 */
#include "SDL.h"
extern "C" {
#include "plat.h"
}
#include "oj32x.h"
extern "C" {
#include "cart.h"
#include "pack.h"
#include "genbg.h"
#include "cartcache.h"
}

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>

/* heap check points on the 32X (port/mars_syscalls.c oj32x_heap_check) */
/* OJ32X_FAST_CODE: the pixel routines that run thousands of times a frame live in the 32X's
 * SDRAM (section .sdcode.*, copied there at start-up), 16-byte aligned and compiled for speed:
 * code in the cart runs through a slow bus and a 4 KB cache */
#if defined(OJ32X_MARS) && defined(OJ32X_FAST_CODE)
#define OJ_FAST __attribute__((section(".sdcode.fast"), optimize("O2"), noinline))
#else
#define OJ_FAST
#endif

/* profiler overlay colours (set with the palette) */
static int overlay_dark, overlay_light = 255;
static const char *prof_text_line(int line);

/* OJ32X_HEAP_CHECK 1 = check the heap at every picture create/free/blit (slow: a walk over
 * all blocks each time - a diagnostic); 0 = only at log lines (port/plat_mars.c) */
#ifndef OJ32X_HEAP_CHECK
#define OJ32X_HEAP_CHECK 0
#endif
#if defined(OJ32X_MARS) && OJ32X_HEAP_CHECK
extern "C" void oj32x_heap_check(const char *where);
#define HEAP_CHECK(w) oj32x_heap_check(w)
#else
#define HEAP_CHECK(w) ((void)0)
#endif


/* ------------------------------------------------------------------ init/time */

static const char *last_error = "";

int SDL_Init(Uint32 flags) { (void)flags; plat_init(); return 0; }
void SDL_Quit(void) { }
const char *SDL_GetError(void) { return last_error; }
const SDL_version *SDL_Linked_Version(void) { static SDL_version v = {1, 2, 0}; return &v; }

Uint32 SDL_GetTicks(void) { return plat_ticks_ms(); }

void SDL_Delay(Uint32 ms)
{
	Uint32 start = plat_ticks_ms();
	while (plat_ticks_ms() - start < ms) { }
}

/* ------------------------------------------------------------------ surfaces */

/* What a surface carries besides the SDL fields. Most surfaces never change their
 * palette, so they share one grey ramp and have no table (a palette + table per
 * surface cost ~1.3 KB each, 450 KB over a level's 345 surfaces). A surface gets its own
 * colours and remap table (256 + 1024 bytes) only when it is recoloured. */
struct Surf {
	SDL_Surface s;
	SDL_PixelFormat fmt;
	SDL_Palette pal;
	SDL_Color *own_colors;  /* nullptr: the shared grey ramp */
	Uint8 *remap;           /* nullptr: identity */
	bool owns_pixels;
	bool in_cart;           /* pixels in the RAM cart (read-only) */
	bool in_pack;           /* pixels are a picture in a cart pack (read-only, shared) */
};

static SDL_Color grey_ramp[256];

static Surf *surf_of(SDL_Surface *s) { return reinterpret_cast<Surf *>(s); }

static void update_remap(Surf *u)
{
	bool identity = true;
	for (int i = 0; i < 256; ++i) {
		u->remap[i] = u->own_colors[i].r;  /* the logical palette is a grey ramp: r is the index */
		if (u->remap[i] != i) identity = false;
	}
	if (identity) {  /* back to normal (restorePalette): drop the copy */
		delete[] u->own_colors;
		delete[] u->remap;
		u->own_colors = nullptr;
		u->remap = nullptr;
		u->pal.colors = grey_ramp;
	}
}

static Surf *new_surface(int w, int h, void *pixels)
{
	if (grey_ramp[255].r != 255)
		for (int i = 0; i < 256; ++i) grey_ramp[i].r = grey_ramp[i].g = grey_ramp[i].b = (Uint8)i;
	Surf *u = new Surf();
	u->s.format = &u->fmt;
	u->fmt.palette = &u->pal;
	u->fmt.BitsPerPixel = 8;
	u->fmt.BytesPerPixel = 1;
	u->pal.ncolors = 256;
	u->pal.colors = grey_ramp;
	u->s.w = w;
	u->s.h = h;
	u->s.pitch = (Uint16)w;
	if (pixels) {
		u->s.pixels = pixels;
	} else {
		u->s.pixels = calloc((size_t)w * h, 1);
		u->owns_pixels = true;
	}
	u->s.clip_rect.x = 0;
	u->s.clip_rect.y = 0;
	u->s.clip_rect.w = (Uint16)w;
	u->s.clip_rect.h = (Uint16)h;
	u->s.refcount = 1;
	return u;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp, Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{
	(void)flags; (void)r; (void)g; (void)b; (void)a;
	if (bpp != 8) { last_error = "only 8-bit surfaces"; return nullptr; }
	HEAP_CHECK("CREATESURFACE");
	Surf *u = new_surface(w, h, nullptr);
	if (!u->s.pixels) { delete u; last_error = "out of memory"; return nullptr; }
	return &u->s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s) return;
	HEAP_CHECK("FREESURFACE");
	Surf *u = surf_of(s);
	if (u->owns_pixels) free(u->s.pixels);
	if (u->in_cart) cart_free(u->s.pixels);
	delete[] u->own_colors;
	delete[] u->remap;
	delete u;
}

/* A surface whose pixels never change after loading. If a mounted pack has the identical
 * picture (found by size and content), the surface uses it in place in the cart. Otherwise:
 * on the 32X an ordinary SDRAM surface (the SH2 cannot write the cart; logged, since every
 * such picture should be in a pack), on the PC a copy in the imitated cart. */
SDL_Surface *oj32x_cart_surface(const unsigned char *pixels, int w, int h)
{
	/* the PC build records the picture for the pack (T32X_BAKE); a picture that is in a
	 * mounted pack is used in place */
	pack_bake_picture(pixels, w, h);
	const uint8_t *baked = pack_picture(pixels, w, h);
	if (baked) {
		Surf *u = new_surface(w, h, const_cast<uint8_t *>(baked));
		u->in_pack = true;
		return &u->s;
	}
#ifdef OJ32X_MARS
	/* the SH2 cannot write the cart: a picture the pack does not have stays in SDRAM */
	{
		char msg[64];
		snprintf(msg, sizeof msg, "PICTURE %dX%d NOT IN THE PACK", w, h);
		plat_log(msg);
	}
	SDL_Surface *s = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
	if (s) memcpy(s->pixels, pixels, (size_t)w * h);
	return s;
#else
	void *p = cart_alloc((size_t)w * h);
	if (p == nullptr) {
		SDL_Surface *s = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
		if (s) memcpy(s->pixels, pixels, (size_t)w * h);
		return s;
	}
	cart_write(p, pixels, (size_t)w * h);
	Surf *u = new_surface(w, h, p);
	u->in_cart = true;
	return &u->s;
#endif
}

/* A finished surface (e.g. a font atlas, built by blitting) is swapped for the identical
 * picture in a cart pack and the SDRAM copy freed; colour key and recolouring are kept.
 * Unchanged when no pack has it. */
SDL_Surface *oj32x_freeze(SDL_Surface *s)
{
	Surf *u = surf_of(s);
	if (u->in_cart || u->in_pack || s->pitch != s->w) return s;
	SDL_Surface *c = oj32x_cart_surface(static_cast<const unsigned char *>(s->pixels), s->w, s->h);
	if (!c || !(surf_of(c)->in_cart || surf_of(c)->in_pack)) { if (c) SDL_FreeSurface(c); return s; }
	c->flags = s->flags;
	c->format->colorkey = s->format->colorkey;
	if (u->own_colors) SDL_SetPalette(c, SDL_LOGPAL, u->own_colors, 0, 256);
	SDL_FreeSurface(s);
	return c;
}

int SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *r)
{
	if (!r) {
		s->clip_rect.x = 0; s->clip_rect.y = 0;
		s->clip_rect.w = (Uint16)s->w; s->clip_rect.h = (Uint16)s->h;
		return 1;
	}
	int x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
	int x1 = r->x + r->w, y1 = r->y + r->h;
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	s->clip_rect.x = (Sint16)x0; s->clip_rect.y = (Sint16)y0;
	s->clip_rect.w = (Uint16)(x1 - x0); s->clip_rect.h = (Uint16)(y1 - y0);
	return x1 > x0 && y1 > y0;
}

int SDL_SetColorKey(SDL_Surface *s, Uint32 flag, Uint32 key)
{
	if (flag & SDL_SRCCOLORKEY) { s->flags |= SDL_SRCCOLORKEY; s->format->colorkey = key; }
	else s->flags &= ~SDL_SRCCOLORKEY;
	return 0;
}

/* the screen's physical palette (what is shown); its logical one stays the grey ramp */
static SDL_Surface *screen_surface;
static uint16_t phys555[256];

int SDL_SetPalette(SDL_Surface *s, int flags, const SDL_Color *colors, int first, int n)
{
	if (first < 0 || first + n > 256) return 0;
	if (flags & SDL_LOGPAL) {
		Surf *u = surf_of(s);
		bool same = true;
		for (int i = 0; i < n && same; ++i)
			same = memcmp(&u->pal.colors[first + i], &colors[i], 3) == 0;
		if (!same) {
			if (!u->own_colors) {
				u->own_colors = new SDL_Color[256];
				u->remap = new Uint8[256];
				memcpy(u->own_colors, grey_ramp, sizeof grey_ramp);
				u->pal.colors = u->own_colors;
			}
			for (int i = 0; i < n; ++i) u->own_colors[first + i] = colors[i];
			update_remap(u);
		}
	}
	if ((flags & SDL_PHYSPAL) && s == screen_surface) {
		/* R7: the border and profiler colours below depend only on entries that are not the
		 * Genesis plane's background (with the plane on, the border is the darkest of those, the
		 * profiler's come from 0-155): the sky's rotation, every frame, changes none of them -
		 * no search through 256 entries then */
		static bool searched;
		bool search = !searched;
		const bool plane = genbg_active();
		for (int i = 0; i < n; ++i) {
			const SDL_Color &c = colors[i];
			uint16_t v = (uint16_t)((c.r >> 3) | (c.g >> 3) << 5 | (c.b >> 3) << 10);
			if (phys555[first + i] != v) {
				phys555[first + i] = v;
				if (!plane || !genbg_is_background(first + i)) search = true;
			}
		}
		static bool plane_was;
		if (plane != plane_was) { plane_was = plane; search = true; }
		if (!search) return 1;
		searched = true;
		/* the 12 lines above and below the 200-line picture show one palette entry; colour 0
		 * is white in some levels' palettes (white bars) - use the darkest entry instead */
		int best = 0, bestv = 1 << 30, light = 0, lightv = -1;
		int dark2 = 0, dark2v = 1 << 30;
		for (int i = 0; i < 256; ++i) {
			const int v = (phys555[i] & 31) + (phys555[i] >> 5 & 31) + (phys555[i] >> 10 & 31);
			if (v < bestv) { bestv = v; best = i; }
			/* the profiler's colours are never taken from the sky's entries (156-255): the
			 * scrolling renderer rotates those, which would change the box's colours */
			if (i < 156 && v > lightv) { lightv = v; light = i; }
			if (i < 156 && v < dark2v) { dark2v = v; dark2 = i; }
		}
		/* REV: with the Genesis plane, a background entry (sky, plain background) would let the
		 * plane show in the borders: the darkest of the others */
		if (genbg_active() && genbg_is_background(best)) {
			int b2 = -1, b2v = 1 << 30;
			for (int i = 0; i < 256; ++i) {
				if (genbg_is_background(i)) continue;
				const int v = (phys555[i] & 31) + (phys555[i] >> 5 & 31) + (phys555[i] >> 10 & 31);
				if (v < b2v) { b2v = v; b2 = i; }
			}
			if (b2 >= 0) best = b2;
		}
		plat_border_colour(best);
		overlay_dark = dark2;
		overlay_light = light;
	}
	return 1;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b)
{
	/* nearest colour of the (logical) palette */
	const SDL_Palette *p = f->palette;
	int best = 0, bestd = 1 << 30;
	for (int i = 0; i < p->ncolors; ++i) {
		int dr = p->colors[i].r - r, dg = p->colors[i].g - g, db = p->colors[i].b - b;
		int d = dr * dr + dg * dg + db * db;
		if (d < bestd) { bestd = d; best = i; if (!d) break; }
	}
	return (Uint32)best;
}

/* one row of a fill with aligned 32-bit writes (no library memset: it lives in the cart) */
OJ_FAST static void fill_row(Uint8 *p, int n, Uint8 v)
{
	while (n > 0 && ((uintptr_t)p & 3)) { *p++ = v; n--; }
	const Uint32 w = v * 0x01010101u;
	Uint32 *q = reinterpret_cast<Uint32 *>(p);
	for (; n >= 16; n -= 16) { q[0] = w; q[1] = w; q[2] = w; q[3] = w; q += 4; }
	for (; n >= 4; n -= 4) *q++ = w;
	p = reinterpret_cast<Uint8 *>(q);
	while (n-- > 0) *p++ = v;
}

OJ_FAST static int fill_core(SDL_Surface *s, SDL_Rect *r, Uint32 colour)
{
	int x0 = s->clip_rect.x, y0 = s->clip_rect.y;
	int x1 = x0 + s->clip_rect.w, y1 = y0 + s->clip_rect.h;
	if (r) {
		if (r->x > x0) x0 = r->x;
		if (r->y > y0) y0 = r->y;
		if (r->x + r->w < x1) x1 = r->x + r->w;
		if (r->y + r->h < y1) y1 = r->y + r->h;
	}
	if (x1 <= x0 || y1 <= y0) return 0;
	Uint8 *p = static_cast<Uint8 *>(s->pixels) + y0 * s->pitch + x0;
	for (int y = y0; y < y1; ++y, p += s->pitch) fill_row(p, x1 - x0, (Uint8)colour);
	return 0;
}

/* copy n bytes with as many aligned 32-bit writes as possible. On the 32X the game draws
 * straight into the frame buffer, where every write is a bus access: four pixels per write
 * instead of one. The source may be unaligned (tiles in the cart, scrolled positions): its
 * bytes are put together (memcpy into a word = right byte order on any machine). */
/* OJ32X_LONG_READS: read the source a 32-bit word at a time where the code read single
 * bytes - in copy_run when source and destination are not equally aligned, and in the keyed
 * blits when looking for the transparent colour. Fewer load instructions and no assembling
 * of words from bytes; the cart's bus traffic itself (cache line fills) stays the same.
 * long_reads switches it at run time (Mode button) to compare frame rates on one disc. */
#ifdef OJ32X_LONG_READS
static bool long_reads = true;
#else
static const bool long_reads = false;
#endif
bool oj32x_long_reads(void) { return long_reads; }
void oj32x_long_reads_toggle(void)
{
#ifdef OJ32X_LONG_READS
	long_reads = !long_reads;
#endif
}

/* Helpers as macros, not inline functions: the OJ_FAST routines are compiled at -O2 and the
 * rest of the file at -Os, and GCC may refuse to inline across that difference - a call per
 * word into code in the cart would cost more than it saves.
 * word_at: the aligned word at p, as stored in memory (p 4-byte aligned).
 * merge_words: the 4 bytes starting k bytes into word a, continuing into the next word b (in
 *   memory order, for either byte order; k = 1..3).
 * has_key: one of v's bytes equals the key (kk = the key in all four bytes); exact. */
#define word_at(p) (*reinterpret_cast<const Uint32 *>(p))
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define merge_words(a, b, k) (((a) << (8 * (k))) | ((b) >> (32 - 8 * (k))))
#else
#define merge_words(a, b, k) (((a) >> (8 * (k))) | ((b) << (32 - 8 * (k))))
#endif
#define has_key(v, kk) (((((v) ^ (kk)) - 0x01010101u) & ~((v) ^ (kk)) & 0x80808080u) != 0)

OJ_FAST static void copy_run(Uint8 *d, const Uint8 *s, int n)
{
	while (n > 0 && ((uintptr_t)d & 3)) { *d++ = *s++; n--; }
	if (((uintptr_t)s & 3) == 0) {
		Uint32 *dw = reinterpret_cast<Uint32 *>(d);
		const Uint32 *sw = reinterpret_cast<const Uint32 *>(s);
		for (; n >= 16; n -= 16) { dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3]; dw += 4; sw += 4; }
		for (; n >= 4; n -= 4) *dw++ = *sw++;
		d = reinterpret_cast<Uint8 *>(dw);
		s = reinterpret_cast<const Uint8 *>(sw);
	} else if (long_reads && n >= 8) {
		/* source k bytes past an aligned word: each output word is put together from two
		 * aligned source words. While at least 8 bytes remain, both words lie inside the
		 * run (the first one starts at most 3 bytes before s, inside s's own aligned word). */
		const int k = (int)((uintptr_t)s & 3);
		const Uint8 *sa = s - k;
		Uint32 *dw = reinterpret_cast<Uint32 *>(d);
		Uint32 a = word_at(sa);
		for (; n >= 8; n -= 4) {
			sa += 4;
			const Uint32 b = word_at(sa);
			*dw++ = merge_words(a, b, k);
			a = b;
		}
		s = sa + k;
		d = reinterpret_cast<Uint8 *>(dw);
		for (; n >= 4; n -= 4) { Uint32 v; memcpy(&v, s, 4); *reinterpret_cast<Uint32 *>(d) = v; d += 4; s += 4; }
	} else {
		Uint32 *dw = reinterpret_cast<Uint32 *>(d);
		for (; n >= 4; n -= 4) { Uint32 v; memcpy(&v, s, 4); *dw++ = v; s += 4; }
		d = reinterpret_cast<Uint8 *>(dw);
	}
	while (n-- > 0) *d++ = *s++;
}

/* OJ32X_LONG_READS: the end of a run of key-coloured (skip_key) or visible (skip_visible)
 * pixels from x on, testing four pixels per aligned word where possible */
OJ_FAST static int skip_key(const Uint8 *sp, int x, int w, Uint8 key)
{
	while (x < w && ((uintptr_t)(sp + x) & 3)) { if (sp[x] != key) return x; x++; }
	const Uint32 kk = key * 0x01010101u;
	while (x + 4 <= w && word_at(sp + x) == kk) x += 4;
	while (x < w && sp[x] == key) x++;
	return x;
}
OJ_FAST static int skip_visible(const Uint8 *sp, int x, int w, Uint8 key)
{
	while (x < w && ((uintptr_t)(sp + x) & 3)) { if (sp[x] == key) return x; x++; }
	const Uint32 kk = key * 0x01010101u;
	while (x + 4 <= w) { const Uint32 v = word_at(sp + x); if (has_key(v, kk)) break; x += 4; }
	while (x < w && sp[x] != key) x++;
	return x;
}

OJ_FAST static int blit_core(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
	int sx = 0, sy = 0, w = src->w, h = src->h;
	if (srcrect) { sx = srcrect->x; sy = srcrect->y; w = srcrect->w; h = srcrect->h; }
	/* clip against the source */
	if (sx < 0) { w += sx; sx = 0; }
	if (sy < 0) { h += sy; sy = 0; }
	if (sx + w > src->w) w = src->w - sx;
	if (sy + h > src->h) h = src->h - sy;
	int dx = dstrect ? dstrect->x : 0, dy = dstrect ? dstrect->y : 0;
	/* clip against the destination's clip rectangle */
	const SDL_Rect &c = dst->clip_rect;
	if (dx < c.x) { int d = c.x - dx; sx += d; w -= d; dx = c.x; }
	if (dy < c.y) { int d = c.y - dy; sy += d; h -= d; dy = c.y; }
	if (dx + w > c.x + c.w) w = c.x + c.w - dx;
	if (dy + h > c.y + c.h) h = c.y + c.h - dy;
	if (dstrect) {
		dstrect->x = (Sint16)dx; dstrect->y = (Sint16)dy;
		dstrect->w = (Uint16)(w > 0 ? w : 0); dstrect->h = (Uint16)(h > 0 ? h : 0);
	}
	if (w <= 0 || h <= 0) return 0;
	HEAP_CHECK("BLIT");

	const Surf *su = surf_of(src);
	const Surf *du = surf_of(dst);
	/* the destination's logical palette is always the grey ramp in OpenJazz; if a
	 * game path ever changes that, the colours here would be wrong - say so once */
	if (du->remap) {
		static bool warned;
		if (!warned) { warned = true; plat_log("SDL_BlitSurface: destination palette is not the grey ramp"); }
	}
	const Uint8 *sp = static_cast<const Uint8 *>(src->pixels) + sy * src->pitch + sx;
	Uint8 *dp = static_cast<Uint8 *>(dst->pixels) + dy * dst->pitch + dx;
	const bool keyed = (src->flags & SDL_SRCCOLORKEY) != 0;
	const Uint8 key = (Uint8)src->format->colorkey;
	const Uint8 *map = su->remap;
	const bool identity = map == nullptr;

	for (int y = 0; y < h; ++y, sp += src->pitch, dp += dst->pitch) {
		if (!keyed) {
			if (identity) copy_run(dp, sp, w);
			else for (int x = 0; x < w; ++x) dp[x] = map[sp[x]];
		} else if (identity) {
			/* runs of visible pixels, each copied with 32-bit writes where possible */
			int x = 0;
			if (long_reads) {
				while (x < w) {
					x = skip_key(sp, x, w, key);
					const int start = x;
					x = skip_visible(sp, x, w, key);
					if (x > start) copy_run(dp + start, sp + start, x - start);
				}
			} else {
				while (x < w) {
					while (x < w && sp[x] == key) x++;
					const int start = x;
					while (x < w && sp[x] != key) x++;
					if (x > start) copy_run(dp + start, sp + start, x - start);
				}
			}
		} else {
			for (int x = 0; x < w; ++x) { Uint8 v = sp[x]; if (v != key) dp[x] = map[v]; }
		}
	}
	return 0;
}

/* ---- the scrolling renderer's screen mapping ----------------------------------------
 * While a level is drawn with the line-table renderer, the screen is not one 320x200 block:
 * rows 0..viewH-1 (the level view) live in the RING - RING_W x RING_H pixels where world column
 * wx0 is ring column 0 and world row wy is ring row wy % RING_H - and the rows below (the status
 * panel) in a 320-wide panel block. Every blit or fill to the screen surface is translated
 * here, split where it crosses the view/panel line or the ring's wrap row, and (for the view)
 * its world rectangle recorded, so the renderer can restore the background there in two frames,
 * when it draws into this frame buffer again. */
struct ScrollMap {
	bool on;
	Uint8 *ring, *panel;
	int wx0, vX, vY, viewH;
	int r0;                       /* R7: vY % RING_H, worked out once a frame (no division a row) */
	SDL_Rect *rec; int recCap; int *recN; bool *recOverflow;
};
static ScrollMap smap;
static unsigned plain_presents;   /* frames shown without the ring mapping */
unsigned oj32x_plain_presents(void) { return plain_presents; }
/* OJ32X_PANEL_DIFF: the status panel is drawn into this SDRAM copy; at present only the rows
 * that changed since this frame buffer last got them are copied into it (32-bit writes;
 * usually just the timer's row) */
#ifdef OJ32X_PANEL_DIFF
static Uint8 panel_shadow[PLAT_SCREEN_W * OJ32X_PANEL_H];
static Uint32 panel_hash[2][OJ32X_PANEL_H];
static bool panel_hash_valid[2];
#endif
static Surf ring_view, panel_view;

static void view_init(Surf &v, Uint8 *pixels, int w, int h)
{
	v.s.pixels = pixels;
	v.s.w = w; v.s.h = h; v.s.pitch = (Uint16)w;
	v.s.format = &v.fmt;
	v.fmt.palette = &v.pal;
	v.fmt.BitsPerPixel = 8; v.fmt.BytesPerPixel = 1;
	v.pal.ncolors = 256;
	v.s.clip_rect.x = 0; v.s.clip_rect.y = 0;
	v.s.clip_rect.w = (Uint16)w; v.s.clip_rect.h = (Uint16)h;
}

void oj32x_scroll_map(bool on, int wx0, int vX, int vY, int viewH)
{
	smap.on = on;
	if (!on) return;
	smap.ring = plat_scroll_ring();
#ifdef OJ32X_PANEL_DIFF
	smap.panel = panel_shadow;
#else
	smap.panel = plat_scroll_panel();
#endif
	smap.wx0 = wx0; smap.vX = vX; smap.vY = vY; smap.viewH = viewH;
	smap.r0 = vY >= 0 ? vY % OJ32X_RING_H : (OJ32X_RING_H - (-vY % OJ32X_RING_H)) % OJ32X_RING_H;
	view_init(ring_view, smap.ring, OJ32X_RING_W, OJ32X_RING_H);
	view_init(panel_view, smap.panel, PLAT_SCREEN_W, OJ32X_PANEL_H);
}

void oj32x_panel_reset(void)
{
#ifdef OJ32X_PANEL_DIFF
	panel_hash_valid[0] = panel_hash_valid[1] = false;
#endif
}

void oj32x_scroll_record(SDL_Rect *list, int cap, int *count, bool *overflow)
{
	smap.rec = list; smap.recCap = cap; smap.recN = count; smap.recOverflow = overflow;
}

bool oj32x_scroll_active(int *vY)
{
	if (smap.on && vY) *vY = smap.vY;
	return smap.on;
}

SDL_Surface *oj32x_ring_surface(void)
{
	view_init(ring_view, plat_scroll_ring(), OJ32X_RING_W, OJ32X_RING_H);
	return &ring_view.s;
}

/* a pointer to screen row y (screen column 0) while the mapping is on - for code that writes
 * pixels itself (the profiler box); nullptr outside the screen */
OJ_FAST Uint8 *oj32x_screen_row(int y)
{
	if (!smap.on) return screen_surface ? static_cast<Uint8 *>(screen_surface->pixels) + y * screen_surface->pitch : nullptr;
	if (y < 0) return nullptr;
	if (y < smap.viewH) {
		/* R7: the ring row without a division (__sdivsi3 is a long loop on the SH2) */
		int r = smap.r0 + y;
		while (r >= OJ32X_RING_H) r -= OJ32X_RING_H;
		return smap.ring + r * OJ32X_RING_W + (smap.vX - smap.wx0);
	}
	if (y - smap.viewH < OJ32X_PANEL_H) return smap.panel + (y - smap.viewH) * PLAT_SCREEN_W;
	return nullptr;
}

/* one pixel into the 32X frame buffer as a 16-bit read-modify-write (KOBO-BOOT rule 4:
 * write the frame buffer with 16-bit writes - single bytes did not always land) */
void oj32x_put_pixel(Uint8 *p, Uint8 v)
{
	volatile Uint16 *w = reinterpret_cast<volatile Uint16 *>(reinterpret_cast<uintptr_t>(p) & ~(uintptr_t)1);
	Uint16 old = *w;
	Uint8 b[2];
	memcpy(b, (const void *)&old, 2);
	b[reinterpret_cast<uintptr_t>(p) & 1] = v;
	memcpy(&old, b, 2);
	*w = old;
}

static void record_world(int sx, int sy, int w, int h)
{
	if (!smap.rec) return;
	if (*smap.recN >= smap.recCap) { *smap.recOverflow = true; return; }
	SDL_Rect &r = smap.rec[(*smap.recN)++];
	r.x = (Sint16)(sx + smap.vX - smap.wx0);      /* ring column */
	r.y = (Sint16)((sy + smap.vY) & 0x7FFF);       /* world row (< 2048) */
	r.w = (Uint16)w; r.h = (Uint16)h;
}

/* screen rectangle (already clipped) -> pieces in the ring / panel; op(surface, x, y, w, h, sx, sy)
 * gets the piece's position in that surface and its offset inside the screen rectangle */
template <typename Op>
static void map_pieces(int x, int y, int w, int h, Op op)
{
	/* view part */
	int vy0 = y, vy1 = y + h < smap.viewH ? y + h : smap.viewH;
	if (vy1 > vy0) {
		record_world(x, vy0, w, vy1 - vy0);
		int row = vy0;
		while (row < vy1) {
			int r = smap.r0 + row;                     /* R7: no division */
			while (r >= OJ32X_RING_H) r -= OJ32X_RING_H;
			int n = OJ32X_RING_H - r;
			if (n > vy1 - row) n = vy1 - row;
			op(&ring_view.s, x + smap.vX - smap.wx0, r, w, n, 0, row - y);
			row += n;
		}
	}
	/* panel part */
	int py0 = y > smap.viewH ? y : smap.viewH, py1 = y + h;
	if (py1 > smap.viewH + OJ32X_PANEL_H) py1 = smap.viewH + OJ32X_PANEL_H;
	if (py1 > py0) op(&panel_view.s, x, py0 - smap.viewH, w, py1 - py0, 0, py0 - y);
}

static bool clip_to_screen(int &x, int &y, int &w, int &h)
{
	const SDL_Rect &c = screen_surface->clip_rect;
	int x0 = x > c.x ? x : c.x, y0 = y > c.y ? y : c.y;
	int x1 = x + w < c.x + c.w ? x + w : c.x + c.w, y1 = y + h < c.y + c.h ? y + h : c.y + c.h;
	if (x1 > screen_surface->w) x1 = screen_surface->w;
	if (y1 > screen_surface->h) y1 = screen_surface->h;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 <= x0 || y1 <= y0) return false;
	x = x0; y = y0; w = x1 - x0; h = y1 - y0;
	return true;
}

int SDL_FillRect(SDL_Surface *s, SDL_Rect *r, Uint32 colour)
{
	if (!(smap.on && s == screen_surface)) return fill_core(s, r, colour);
	int x = r ? r->x : 0, y = r ? r->y : 0, w = r ? r->w : s->w, h = r ? r->h : s->h;
	if (!clip_to_screen(x, y, w, h)) return 0;
	map_pieces(x, y, w, h, [&](SDL_Surface *t, int tx, int ty, int tw, int th, int, int) {
		SDL_Rect pr = { (Sint16)tx, (Sint16)ty, (Uint16)tw, (Uint16)th };
		fill_core(t, &pr, colour);
	});
	return 0;
}

int SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (!(smap.on && dst == screen_surface)) return blit_core(src, srcrect, dst, dstrect);
	int sx = 0, sy = 0, w = src->w, h = src->h;
	if (srcrect) { sx = srcrect->x; sy = srcrect->y; w = srcrect->w; h = srcrect->h; }
	if (sx < 0) { w += sx; sx = 0; }
	if (sy < 0) { h += sy; sy = 0; }
	if (sx + w > src->w) w = src->w - sx;
	if (sy + h > src->h) h = src->h - sy;
	int dx = dstrect ? dstrect->x : 0, dy = dstrect ? dstrect->y : 0;
	int cx = dx, cy = dy, cw = w, ch = h;
	if (w <= 0 || h <= 0 || !clip_to_screen(cx, cy, cw, ch)) {
		if (dstrect) { dstrect->w = 0; dstrect->h = 0; }
		return 0;
	}
	sx += cx - dx; sy += cy - dy;
	if (dstrect) { dstrect->x = (Sint16)cx; dstrect->y = (Sint16)cy; dstrect->w = (Uint16)cw; dstrect->h = (Uint16)ch; }
	map_pieces(cx, cy, cw, ch, [&](SDL_Surface *t, int tx, int ty, int tw, int th, int ox, int oy) {
		SDL_Rect ps = { (Sint16)(sx + ox), (Sint16)(sy + oy), (Uint16)tw, (Uint16)th };
		SDL_Rect pd = { (Sint16)tx, (Sint16)ty, 0, 0 };
		blit_core(src, &ps, t, &pd);
	});
	return 0;
}

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
	{	/* the A/B swap as it was left */
		static bool once;
		if (!once) { once = true; if (plat_port_setting(1)) oj32x_ab_swap(true); }
	}
	(void)flags;
	if (bpp != 8 || w != PLAT_SCREEN_W || h != PLAT_GAME_H) {
		static char msg[64];
		snprintf(msg, sizeof msg, "video mode %dx%dx%d not supported", w, h, bpp);
		last_error = msg;
		return nullptr;
	}
	/* on the 32X the screen is the frame buffer itself (plat_canvas) */
	if (!screen_surface) screen_surface = &new_surface(w, h, plat_canvas())->s;
	return screen_surface;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *f, Uint32 flags)
{
	(void)f; (void)flags;
	static SDL_Rect mode = {0, 0, PLAT_SCREEN_W, PLAT_GAME_H};
	static SDL_Rect *list[] = {&mode, nullptr};
	return list;
}

int SDL_Flip(SDL_Surface *screen)
{
	if (smap.on && screen == screen_surface) {   /* the line-table renderer's frame */
		oj32x_prof(6);
		plat_wait_flip();                        /* the previous swap must be done */
#ifdef OJ32X_PANEL_DIFF
		{
			const int b = plat_back_index() & 1;
			Uint8 *fbp = plat_scroll_panel();
			for (int r = 0; r < OJ32X_PANEL_H; r++) {
				const Uint8 *row = panel_shadow + r * PLAT_SCREEN_W;
				Uint32 h = 2166136261u;
				const Uint32 *w = reinterpret_cast<const Uint32 *>(row);
				for (int i = 0; i < PLAT_SCREEN_W / 4; i++) h = (h ^ w[i]) * 16777619u;
				if (!panel_hash_valid[b] || h != panel_hash[b][r]) {
					copy_run(fbp + r * PLAT_SCREEN_W, row, PLAT_SCREEN_W);
					panel_hash[b][r] = h;
				}
			}
			panel_hash_valid[b] = true;
		}
#endif
		plat_present_scroll(phys555, smap.r0, smap.vX - smap.wx0, smap.viewH);
		oj32x_prof(5);
		oj32x_prof_frame();
		/* the mapping lasts one frame, from the level's scrollBegin to this present, so a
		 * screen that is not the level (the pause menu's save, load and set-up) draws to the
		 * plain screen, not into the ring */
		smap.on = false;
		return 0;
	}
	if (screen->pixels == plat_canvas() && screen->pixels) {
		plain_presents++;   /* the frame buffers no longer hold the level's rings */
		oj32x_prof(6);                     /* other */
		plat_present_canvas(phys555);
		oj32x_prof(5);                     /* present: line table, waiting for the frame, flip */
		oj32x_prof_frame();
		return 0;
	}
	oj32x_prof(6);
	plat_wait_flip();
	plain_presents++;   /* the frame buffers no longer hold the level's rings */
	plat_present(static_cast<const uint8_t *>(screen->pixels), screen->pitch, phys555);
	oj32x_prof(5);
	oj32x_prof_frame();
#ifndef OJ32X_MARS
	/* PC test: T32X_DOUBLE_BUFFER=1 swaps the screen between two buffers at every flip,
	 * the second one starting as junk - like the 32X frame buffer the game now draws into.
	 * Frames identical to a normal run = the game redraws everything it shows each frame. */
	{
		static int dbl = -1;
		static uint8_t *other;
		if (dbl < 0) { const char *e = getenv("T32X_DOUBLE_BUFFER"); dbl = e && *e == '1'; }
		if (dbl) {
			const size_t n = (size_t)screen->pitch * screen->h;
			if (!other) { other = static_cast<uint8_t *>(malloc(n)); for (size_t i = 0; i < n; i++) other[i] = (uint8_t)(i * 7); }
			uint8_t *cur = static_cast<uint8_t *>(screen->pixels);
			screen->pixels = other;
			other = cur;
		}
	}
#endif
	return 0;
}

/* ------------------------------------------------------------------ keys / pad */

char *SDL_GetKeyName(SDLKey key)
{
	static char buf[16];
	switch (key) {
		case SDLK_UP: return (char *)"up";
		case SDLK_DOWN: return (char *)"down";
		case SDLK_LEFT: return (char *)"left";
		case SDLK_RIGHT: return (char *)"right";
		case SDLK_SPACE: return (char *)"C";
		case SDLK_LALT: return (char *)"B";
		case SDLK_RCTRL: return (char *)"A";
		case SDLK_RETURN: return (char *)"start";
		case SDLK_ESCAPE: return (char *)"start";
		default: snprintf(buf, sizeof buf, "key %d", (int)key); return buf;
	}
}

/* Pad layouts. Play: C or A jump, B fire, X or Y change weapon, Start = the level menu
 * (OpenJazz's Escape), Z statistics (the frame counter). Menu: C, A or Start = Enter,
 * B = back (Escape). The set-up's A/B swap exchanges A's and B's play keys. */
struct PadKey { uint16_t bit; SDLKey play, menu; };
static PadKey pad_keys[] = {
	{PAD_UP,    SDLK_UP,     SDLK_UP},
	{PAD_DOWN,  SDLK_DOWN,   SDLK_DOWN},
	{PAD_LEFT,  SDLK_LEFT,   SDLK_LEFT},
	{PAD_RIGHT, SDLK_RIGHT,  SDLK_RIGHT},
	{PAD_C,     SDLK_SPACE,  SDLK_RETURN},
	{PAD_B,     SDLK_LALT,   SDLK_ESCAPE},
	{PAD_A,     SDLK_SPACE,  SDLK_RETURN},   /* A = C */
	{PAD_START, SDLK_ESCAPE, SDLK_RETURN},
	{PAD_X,     SDLK_RCTRL,  SDLK_UNKNOWN},  /* change weapon on X / Y (6-button pad) */
	{PAD_Y,     SDLK_RCTRL,  SDLK_UNKNOWN},
	{PAD_Z,     SDLK_F9,     SDLK_UNKNOWN},   /* Z: statistics (the frame counter) */
	{PAD_MODE,  SDLK_UNKNOWN, SDLK_UNKNOWN},
};
#define NPAD (int)(sizeof pad_keys / sizeof pad_keys[0])

static int pad_mode = OJ32X_PAD_MENU;
static uint16_t pad_down;       /* buttons whose key is down */
static uint16_t pad_now;        /* the pad as last read */
unsigned int oj32x_pad(void) { return pad_now; }
static uint16_t pad_latched;    /* held across a layout change: ignored until released */
static SDLKey key_down[NPAD];   /* the key each held button produced */

/* small queue of generated events */
static SDL_Event queue[32];
static int q_head, q_tail;
static void push_key(Uint8 type, SDLKey sym)
{
	int next = (q_tail + 1) % 32;
	if (next == q_head) return;
	SDL_Event &e = queue[q_tail];
	memset(&e, 0, sizeof e);
	e.type = type;
	e.key.type = type;
	e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.sym = sym;
	q_tail = next;
}

/* the set-up's one button choice: A and B trade jump and fire (C always jumps) */
static bool ab_swapped;
bool oj32x_ab_swapped(void) { return ab_swapped; }
void oj32x_ab_swap(bool on)
{
	ab_swapped = on;
	plat_port_setting_set(1, on ? 1 : 0);   /* kept across power-off */
	for (auto &k : pad_keys) {
		if (k.bit == PAD_A) k.play = on ? SDLK_LALT : SDLK_SPACE;
		if (k.bit == PAD_B) k.play = on ? SDLK_SPACE : SDLK_LALT;
	}
}

void oj32x_pad_mode(int mode)
{
	if (mode == pad_mode) return;
	/* release what the old layout pressed; buttons still held stay quiet until let go */
	for (int i = 0; i < NPAD; ++i)
		if (pad_down & pad_keys[i].bit) {
			if (key_down[i] != SDLK_UNKNOWN) push_key(SDL_KEYUP, key_down[i]);
			key_down[i] = SDLK_UNKNOWN;
		}
	pad_latched |= pad_down;
	pad_down = 0;
	pad_mode = mode;
}

static void poll_pad(void)
{
	uint16_t now = plat_read_pad();
	pad_now = now;   /* for buttons the game has no key for (Y in the credits) */
	{	/* Mode: in OJ32X_LONG_READS builds switches the 32-bit reads on/off (to compare frame
		 * rates); otherwise the slave SH2's drawing (OJ32X_SLAVE_DRAWS builds, for profiling) */
		static bool modeWas;
		const bool mode = (now & PAD_MODE) != 0;
		/* J23b: Mode does nothing (the second SH2's game logic is switched in the pause
		 * menu: "second cpu on / off"; the 32-bit reads stay on) */
		(void)mode;
		modeWas = mode;
	}
	pad_latched &= now;
	now &= (uint16_t)~pad_latched;
	uint16_t changed = now ^ pad_down;
	for (int i = 0; i < NPAD; ++i) {
		uint16_t bit = pad_keys[i].bit;
		if (!(changed & bit)) continue;
		if (now & bit) {
			SDLKey k = pad_mode == OJ32X_PAD_PLAY ? pad_keys[i].play : pad_keys[i].menu;
			key_down[i] = k;
			if (k != SDLK_UNKNOWN) push_key(SDL_KEYDOWN, k);
		} else {
			if (key_down[i] != SDLK_UNKNOWN) push_key(SDL_KEYUP, key_down[i]);
			key_down[i] = SDLK_UNKNOWN;
		}
	}
	pad_down = now;
}

int SDL_PollEvent(SDL_Event *e)
{
	if (q_head == q_tail) poll_pad();
	if (q_head == q_tail) return 0;
	*e = queue[q_head];
	q_head = (q_head + 1) % 32;
	return 1;
}

/* ------------------------------------------------------------------ audio */
/* SDL audio does not open, so OpenJazz's own mixing (music modules and effects through
 * libxmp) never runs; OpenJazz carries on without it. Its sound functions call the port
 * instead (oj/src/io/sound.cpp): music as CD audio tracks, effects on the Sega CD's PCM
 * chip - both play without SH2 time. */
int  SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	(void)desired; (void)obtained;
	last_error = "no audio in this build yet";
	return -1;
}
void SDL_PauseAudio(int pause_on) { (void)pause_on; }
void SDL_CloseAudio(void) { }
void SDL_LockAudio(void) { }
void SDL_UnlockAudio(void) { }
void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume)
{
	(void)dst; (void)src; (void)len; (void)volume;
}

/* ------------------------------------------------------------------ misc */
void oj32x_log(const char *msg) { plat_log(msg); }

/* a second surface on the same pixels with its own clip rectangle (band x,y,w,h inside
 * base's clip rectangle) - so two bands of one screen can be drawn independently (by the two
 * SH2s at once in OJ32X_SLAVE_DRAWS builds) */
static Surf *views[2];

SDL_Surface *oj32x_view(SDL_Surface *base, int index, int x, int y, int w, int h)
{
	if (index < 0 || index > 1) return base;
	if (!views[index]) views[index] = new_surface(base->w, base->h, base->pixels);  /* master only: see oj32x_parallel */
	Surf *v = views[index];
	v->s.pixels = base->pixels;
	v->s.pitch = base->pitch;
	v->s.w = base->w;
	v->s.h = base->h;
	SDL_Rect r = { (Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h };
	SDL_SetClipRect(&v->s, &r);
	const SDL_Rect &c = base->clip_rect;
	int x0 = v->s.clip_rect.x > c.x ? v->s.clip_rect.x : c.x;
	int y0 = v->s.clip_rect.y > c.y ? v->s.clip_rect.y : c.y;
	int x1 = v->s.clip_rect.x + v->s.clip_rect.w, y1 = v->s.clip_rect.y + v->s.clip_rect.h;
	if (c.x + c.w < x1) x1 = c.x + c.w;
	if (c.y + c.h < y1) y1 = c.y + c.h;
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	v->s.clip_rect.x = (Sint16)x0; v->s.clip_rect.y = (Sint16)y0;
	v->s.clip_rect.w = (Uint16)(x1 - x0); v->s.clip_rect.h = (Uint16)(y1 - y0);
	return &v->s;
}

/* fn(arg, 0) and fn(arg, 1) - at the same time on the master and the slave SH2 in
 * OJ32X_SLAVE_DRAWS builds (plat_parallel), otherwise one after the other (the bands do not
 * overlap) */
int oj32x_sky_on = 1;   /* J23b: see oj32x.h */

void oj32x_parallel(void (*fn)(void *, int), void *arg)
{
	/* the views are made here, on the master: malloc must never run on both SH2s at once */
	for (int i = 0; i < 2; i++)
		if (!views[i] && screen_surface) views[i] = new_surface(screen_surface->w, screen_surface->h, screen_surface->pixels);
	plat_parallel(fn, arg);
}

/* The pack a level, planet or bonus file needs (disc file OJ<k>.PAK, see port/pack.h):
 * world packs k = world * 10 (+ 100 for Holiday Hare '95), bonus packs k = 800 + map. */
void oj32x_level_pack(const char *levelFile)
{
	static int current = -1;
	const char *b = levelFile;
	for (const char *p = levelFile; *p; ++p)
		if (*p == '/' || *p == '\\') b = p + 1;
	/* one pack per WORLD (planet): both levels of a planet, its tiles, sprites and planet
	 * screen - k = world * 10. A level of the same world as the pack already in the cart needs
	 * no loading at all; the planet screen (PLANET.www) loads its world's pack first. */
	int k;
	auto world_of = [](const char *ext) -> int {
		if (!isdigit((unsigned char)ext[0]) || !isdigit((unsigned char)ext[1]) || !isdigit((unsigned char)ext[2])) return -1;
		return (ext[0] - '0') * 100 + (ext[1] - '0') * 10 + (ext[2] - '0');
	};
	if (strncasecmp(b, "BONUSMAP.", 9) == 0 && world_of(b + 9) >= 0) {
		k = PACK_BONUS_K + world_of(b + 9);   /* one pack per bonus map, OJ800-OJ829 */
	} else if (strncasecmp(b, "PLANET.", 7) == 0 && world_of(b + 7) >= 0) {
		k = world_of(b + 7) * 10;
	} else if (strncasecmp(b, "LEVEL", 5) == 0 && isdigit((unsigned char)b[5]) && b[6] == '.' && world_of(b + 7) >= 0) {
		k = world_of(b + 7) * 10;
	} else {
		return;
	}
	if (plat_game() == 1 && k != PACK_BONUS_K) k += 100;   /* Holiday Hare '95's packs (OJ6xx) */
	if (k == current) return;
	{
		char msg[40];
		snprintf(msg, sizeof msg, "LEVEL FILE %s: PACK OJ%03d", b, k);
		plat_log(msg);
	}
	pack_mount_at(2, nullptr);   /* REV: the last world's Genesis plane files are gone */
#if OJ32X_CART_MB
	/* R5: the 8 / 16 MB cart keeps worlds in cache pages (port/cartcache.c); on a 4 MB cart,
	 * the home slot at 0x200000 with the Genesis plane pack right after the world pack */
	{
		uint32_t woff, goff;
		pack_mount_at(1, nullptr);
		const int gb_id = PACK_GENESIS_ID0 + k;   /* R9: a bonus map's has its floor (GB8nn) */
		if (cartcache_world(PACK_LEVEL_ID0 + k, gb_id, &woff, &goff)) {
			if (pack_mount_at(1, plat_cart_ptr(woff))) {
				current = k;
				if (goff) pack_mount_at(2, plat_cart_ptr(goff));
				return;
			}
		}
		if (plat_cart_page_now() != 1) plat_cart_page(1);
		cartcache_home_changed();   /* R7: the home slot no longer holds an unpacked world */
		const int32_t len = plat_load_pack_at(PACK_LEVEL_ID0 + k, PACK_LEVEL_CART, PACK_LEVEL_MAX);
		if (len > 0 && pack_mount_at(1, plat_cart_ptr(PACK_LEVEL_CART))) {
			current = k;
			const uint32_t g = (PACK_LEVEL_CART + (uint32_t)len + 0x7FFF) & ~0x7FFFu;   /* a 32 KB boundary */
			if (gb_id && plat_load_pack_at(gb_id, g, PACK_LEVEL_CART + PACK_LEVEL_MAX - g) > 0)
				pack_mount_at(2, plat_cart_ptr(g));
		} else {
			current = -1;
		}
		return;
	}
#endif
	const int32_t len = plat_load_pack(PACK_LEVEL_ID0 + k, PACK_LEVEL_CART, PACK_LEVEL_MAX);
	if (len > 0 && pack_mount_at(1, plat_cart_ptr(PACK_LEVEL_CART))) {
		current = k;
		char msg[40];
		snprintf(msg, sizeof msg, "LEVEL PACK OJ%03d.PAK", k);
		plat_log(msg);
		/* REV: the world's Genesis plane files (GB<k>.PAK, port/genbg.h); worlds the Genesis
		 * does not draw have none - their levels stay the 32X's. R9: a bonus map's has its
		 * floor for the graphics chip (BONUSMD.BIN) */
		{
			const int32_t glen = plat_load_pack(PACK_GENESIS_ID0 + k, PACK_GENESIS_CART, PACK_GENESIS_MAX);
			if (glen > 0 && pack_mount_at(2, plat_cart_ptr(PACK_GENESIS_CART))) {
				snprintf(msg, sizeof msg, "GENESIS PACK GB%03d.PAK", k);
				plat_log(msg);
			}
		}
	} else {
		pack_mount_at(1, nullptr);
		current = -1;
	}
}

/* R9: the bonus floor (port/oj32x.h, docs/R9_BONUS_FLOOR.md) */
int oj32x_bonusmd_start(void)
{
	const uint8_t *data;
	uint32_t size;
	if (!pack_file("BONUSMD.BIN", &data, &size) || size < 1024 + 65536 || memcmp(data, "OJBM", 4) != 0) {
		plat_log("BONUS FLOOR: NO BONUSMD.BIN - THE 32X DRAWS IT");
		return -1;
	}
	const int hole = data[8] << 8 | data[9];
	if ((data[4] << 8 | data[5]) != 2) {
		plat_log("BONUS FLOOR: BONUSMD.BIN IS NOT VERSION 2 - THE 32X DRAWS IT");
		return -1;
	}
	/* the hole's colour on the 32X (15-bit BGR) from the file's 8-bit RGB */
	const uint16_t c555 = (uint16_t)((data[76] >> 3) | (data[77] >> 3) << 5 | (data[78] >> 3) << 10);
	if (!plat_bonusmd_load(data, hole, c555)) {
		plat_log("BONUS FLOOR: NOT LOADED - THE 32X DRAWS IT");
		return -1;
	}
	plat_log("BONUS FLOOR: ON THE GENESIS");
	return hole;
}

bool oj32x_bonusmd_frame(int x, int y, int cs, int sn, int step)
{
	return plat_bonusmd_frame(x, y, cs, sn, step) != 0;
}

void oj32x_bonusmd_stop(void) { plat_bonusmd_off(); }

void oj32x_packs_ready(void) { plat_packs_ready(); }

/* Frame profiler: oj32x_prof(slot) adds the time since the last mark to a slot; a frame's
 * marks add up to the whole frame. Every second the averages per frame become three text
 * lines the level draws in a box in its corner (only in OJ32X_PROFILE builds). */
/* profiler overlay - its own 3x5 font (the error screens' one) drawn 2x, brightest palette
 * entry on a box of the darkest, so it can be read on any level and any TV */
#include "mars/font3x5.h"

void oj32x_prof_draw(SDL_Surface *s)
{
	if (!s || !s->pixels) return;
	int width = 0;
	for (int l = 0; l < 3; l++) { int n = (int)strlen(prof_text_line(l)); if (n > width) width = n; }
	const int boxW = width * 8 + 4, boxH = 3 * 12 + 4;
	/* the box through SDL_FillRect (mapped and recorded with the scrolling renderer),
	 * the letters through screen-row pointers */
	SDL_Rect box = { 0, 0, (Uint16)boxW, (Uint16)boxH };
	SDL_FillRect(s, &box, (Uint32)overlay_dark);
	for (int l = 0; l < 3; l++) {
		const char *t = prof_text_line(l);
		for (int c = 0; t[c]; c++) {
			int ch = t[c];
			if (ch >= 'a' && ch <= 'z') ch -= 32;
			if (ch < 32 || ch > 95) ch = '?';
			const uint16_t g = font3x5[ch - 32];
			const int x0 = 2 + c * 8, y0 = 2 + l * 12;
			for (int gy = 0; gy < 5; gy++)
				for (int gx = 0; gx < 3; gx++)
					if (g & (1u << (14 - (gy * 3 + gx))))
						for (int dy = 0; dy < 2; dy++)
							for (int dx = 0; dx < 2; dx++) {
								const int x = x0 + gx * 2 + dx, y = y0 + gy * 2 + dy;
								Uint8 *row = (s == screen_surface) ? oj32x_screen_row(y) : static_cast<Uint8 *>(s->pixels) + y * s->pitch;
								if (row && x < s->w && y < s->h) oj32x_put_pixel(row + x, (Uint8)overlay_light);
							}
		}
	}
}

static uint32_t prof_last, prof_acc[8], prof_frames, prof_steps, prof_start;
static char prof_lines[3][48] = { "PROFILING", "", "" };
void oj32x_prof(int slot)
{
	const uint32_t now = plat_prof_us();
	if (slot >= 0 && slot < 8) prof_acc[slot] += now - prof_last;
	prof_last = now;
}
void oj32x_prof_steps(int n) { prof_steps += (uint32_t)n; }
void oj32x_prof_frame(void)
{
	prof_frames++;
	const uint32_t now = plat_prof_us();
	if (!prof_start) {                 /* the first frame: start counting from here */
		prof_start = now ? now : 1;
		for (int i = 0; i < 8; i++) prof_acc[i] = 0;
		prof_frames = prof_steps = 0;
		return;
	}
	if (now - prof_start < 1000000u) return;
	const uint32_t f = prof_frames ? prof_frames : 1;
	uint32_t total = 0;
	for (int i = 0; i < 8; i++) total += prof_acc[i];
	/* whole ms per frame, three short lines: "1CPU|2CPU <fps>FPS T<total>"; logic,
	 * background, sprites, foreground (L B S F); panel, present, other (H P O) */
	#define MS(x) (unsigned)((x) / f / 1000)
	snprintf(prof_lines[0], sizeof prof_lines[0], "%s %uFPS T%u",
	         (plat_slave_draws_on() || plat_slave_logic_on()) ? "2CPU" : "1CPU",
	         (unsigned)(prof_frames * 1000000u / (now - prof_start)), MS(total));
	snprintf(prof_lines[1], sizeof prof_lines[1], "L%u B%u S%u F%u",
	         MS(prof_acc[0]), MS(prof_acc[1]), MS(prof_acc[2]), MS(prof_acc[3]));
	snprintf(prof_lines[2], sizeof prof_lines[2], "H%u P%u O%u",
	         MS(prof_acc[4]), MS(prof_acc[5]), MS(prof_acc[6]));
	#undef MS
	for (int i = 0; i < 8; i++) prof_acc[i] = 0;
	prof_frames = 0;
	prof_steps = 0;
	prof_start = now;

}
const char *oj32x_prof_text(int line) { return prof_lines[line < 0 || line > 2 ? 0 : line]; }
static const char *prof_text_line(int line) { return oj32x_prof_text(line); }

/* both 32X frame buffers' picture areas to colour 0 (PC: nothing) */
void oj32x_clear_screens(void) { plat_clear_screens(); }

void oj32x_sfx(int id) { plat_sfx(id); }
void oj32x_set_game(int g) { plat_set_game(g); }
bool oj32x_hh95_available(void) { return plat_hh95_available() != 0; }
int oj32x_port_setting(int key) { return plat_port_setting(key); }
void oj32x_port_setting_set(int key, int value) { plat_port_setting_set(key, value); }

/* a CD audio track directly (the credits' CD player); 0 = stop */
void oj32x_music_track(int track) { plat_music(track); }

/* Music as CD audio: the game's music files in alphabetical order are disc tracks 2-38,
 * Holiday Hare '95's own songs tracks 39-41 (tools/music_tracks.txt; the build renders the
 * modules into the tracks); NULL = stop. */
static char music_last[16];   /* the song asked for last, for volume 0 -> on */

void oj32x_music_remember(const char *fileName)
{
	if (fileName) { strncpy(music_last, fileName, sizeof music_last - 1); music_last[sizeof music_last - 1] = 0; }
}

void oj32x_music_volume(int volume)
{
	static int was = -1;
	if (volume == 0 && was != 0) { oj32x_music(nullptr, false); }
	else if (volume > 0 && was == 0 && music_last[0]) { oj32x_music(music_last, true); }
	was = volume;
}

void oj32x_music(const char *fileName, bool restart)
{
	if (fileName) oj32x_music_remember(fileName);
	static const char *const songs[] = {
		"BONUS",
		"BOSS",
		"ENDLEVEL",
		"ENDSONG",
		"MENUSNG",
		"SECRET",
		"SONG0",
		"SONG1",
		"SONG10",
		"SONG11",
		"SONG12",
		"SONG13",
		"SONG14",
		"SONG15",
		"SONG16",
		"SONG17",
		"SONG2",
		"SONG3",
		"SONG4",
		"SONG5",
		"SONG6",
		"SONG7",
		"SONG8",
		"SONG9",
		"SONGCD0",
		"SONGCD1",
		"SONGCD2",
		"SONGCD3",
		"SONGCD4",
		"SONGCD5",
		"SONGCD6",
		"SONGCD7",
		"SONGCD8",
		"XM2",
		"XM3",
		"XMAS2",
		"XMAS3"
	};
	static int current = -1;
	if (!fileName) { current = -1; plat_music(0); return; }
	const char *b = fileName;
	for (const char *p = fileName; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
	/* Holiday Hare '95's own menu, level-end and XM3 songs (same names as Jazz CD's,
	 * different music) are tracks 39-41 */
	if (plat_game() == 1) {
		static const char *const hh[] = { "MENUSNG", "ENDLEVEL", "XM3" };
		for (int i = 0; i < 3; i++) {
			size_t n = strlen(hh[i]);
			if (strncasecmp(b, hh[i], n) == 0 && b[n] == '.') {
				if (current == 100 + i && !restart) return;
				current = 100 + i;
				plat_music(39 + i);
				return;
			}
		}
	}
	for (int i = 0; i < (int)(sizeof songs / sizeof songs[0]); i++) {
		size_t n = strlen(songs[i]);
		if (strncasecmp(b, songs[i], n) == 0 && b[n] == '.') {
			if (i == current && !restart) return;
			current = i;
			plat_music(i + 2);
			return;
		}
	}
}

int oj32x_skip_intro(void)
{
#ifdef OJ32X_MARS
	return 1;
#else
	const char *e = getenv("T32X_SKIP_INTRO");
	return e && *e == '1';
#endif
}

void oj32x_mem_mark(const char *label)
{
	plat_mem_mark(label);
	char msg[96];
	snprintf(msg, sizeof msg, "cart at '%s': %lu bytes in %d blocks, peak %lu", label,
	         (unsigned long)cart_used(), cart_blocks(), (unsigned long)cart_peak());
	plat_log(msg);
}
