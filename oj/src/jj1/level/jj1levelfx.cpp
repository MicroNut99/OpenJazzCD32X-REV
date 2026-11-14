
/**
 *
 * @file jj1levelfx.cpp
 *
 * Part of the OpenJazz project (the CD32X port, OJ32X_FX)
 *
 * @par Licence:
 * Copyright (c) 2005-2013 AJ Thomson; this file 2026 for the CD32X port
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * The level's picture composed by the CD32X cart compositor on the EverDrive PRO's FPGA
 * (CD32X-BootCamp cartfx/; the port's side of it: port/cfx_oj.h).
 *
 * The engine holds the level's maps in its SRAM and its tiles and sprites in the cart's PSRAM, in
 * the engine's form (a picture's colour key moved to 0, cartfx.h). It composes 16 x 8 cells of a
 * picture whose origin is the view's position rounded down to that grid, so every engine cell is a
 * world cell, back to front:
 *   layer 0  the sky: a bitmap 4 pixels wide and 200 rows, its colour by ring row (156 + row / 2)
 *            as the scrolling renderer draws it (SkyPaletteEffect turns the colours to the screen)
 *   layer 1  the sun or moon: a map of 16 x 8 tiles with the sun in one of them, scrolled so that
 *            it stays where OpenJazz puts it on the screen
 *   layer 2  the background tiles; a cell with the black background uses a copy of its tile whose
 *            see-through pixels are black (LEVEL_BLACK), a foreground cell there a black tile
 *   sprites  every keyed picture from the cart that the game blits to the view (port/sdl_oj.cpp)
 *   layer 3  the foreground tiles, over the sprites (the giant waterfalls too)
 * The master tells it which cells to compose: every cell of the view its frame buffer's ring does not
 * hold yet (rows and columns that scrolled in), and every cell something was drawn over in that ring
 * last time (the sprites, the sun, the water lines) or whose tile changed since. Sprites mark their
 * own cells. After the GO the master copies the composed cells (the engine's cell map) into the ring,
 * then draws what the engine cannot (recoloured sprites, fills) and the rest of the frame as before.
 */

#ifdef OJ32X_FX

#include "jj1level.h"

#include "io/gfx/sprite.h"
#include "io/gfx/video.h"
#include "util.h"
#include "oj32x.h"
extern "C" {
#include "plat.h"
}
#include "cfx_oj.h"
extern "C" {
#include "pack.h"
}
#include <cstdlib>

#include <cstdio>
#include <cstring>


namespace {

	const int RW = OJ32X_RING_W, RH = OJ32X_RING_H;   // the ring: 576 x 200
	const int RCOLS = RW / 16, RROWS = RH / 8;        // in cells: 36 x 25 (oj32x.h: 576 wide)
	const int FX_DIRTY = 96;   // a frame's rectangles kept (more: the ring is drawn again whole); SDRAM is the heap's

	/// one frame buffer's ring, as the engine's cells see it
	struct FxBuf {
		bool valid;
		int wx0;                       ///< world x of ring column 0 (a multiple of 32)
		int rowCell[RROWS];            ///< the world cell row each ring cell row holds (-1: none)
		unsigned long long colValid[RROWS];  ///< bit c: ring cell column c holds that row's world cell
		SDL_Rect dirty[FX_DIRTY];      ///< drawn over the ring last time (ring column, world row)
		int nDirty;
		bool overflow;
		unsigned short changed[64];    ///< tiles changed since (gridY << 8 | gridX)
		int nChanged;
		bool chOverflow;
		int anim;                      ///< the animated foreground tiles' phase last time (ticks & 64)
	};
	FxBuf fxBuf[2];
	FxBuf* fxCur;

	unsigned int fxTiles;        ///< PSRAM: engine tile 1 (the level's tile 0)
	int fxN;                     ///< the level's tiles
	int fxShown;                 ///< the buffer the engine's window shows
	unsigned short fxSun;        ///< the sun map's entry now
	int fxOx, fxOy, fxCols, fxRows;
	unsigned short fxAnim[256];  ///< the cells with an animated foreground tile (gridY << 8 | gridX)
	int fxNAnim;
	int fxAnimPoked;             ///< the phase their engine entries have
	unsigned int fxOverflows;    ///< pictures whose ring was drawn again whole: the dirty list was full

	inline void send (unsigned short w) { cfxoj_send(w); }

	/// the tiles in the engine's form: N tiles, N with black behind their see-through pixels, a black tile
	struct TileArg { const unsigned char* px; int pitch, n; unsigned char key; };
	void makeTiles (unsigned char* out, int w, int h, const void* arg) {
		const TileArg* t = static_cast<const TileArg*>(arg);
		for (int k = 0; k < t->n; k++)
			for (int y = 0; y < 32; y++)
				for (int x = 0; x < 32; x++) {
					const unsigned char v = t->px[(k * 32 + y) * t->pitch + x];
					out[k * 1024 + y * 32 + x] = (unsigned char)(v - t->key);
					out[(t->n + k) * 1024 + y * 32 + x] = (unsigned char)((v == t->key ? LEVEL_BLACK : v) - t->key);
				}
		memset(out + 2 * t->n * 1024, (unsigned char)(LEVEL_BLACK - t->key), 1024);
		(void)w; (void)h;
	}
	/// the sky: 4 x 200, a colour every two rows (or OpenJazz's plain background, 127)
	void makeSky (unsigned char* out, int w, int h, const void* arg) {
		const bool sky = *static_cast<const bool*>(arg);
		for (int r = 0; r < h; r++) memset(out + w * r, sky ? 156 + (r >> 1) : 127, w);
	}
	inline void a24 (unsigned int a) { send(CFX_HI(a)); send(CFX_LO(a)); }

	bool fgTileFx (const GridElement* ge, const JJ1EventType* eventSet) {
		return (ge->event == 123) || (ge->event == 124) || (ge->event == 125) ||
			(eventSet[ge->event].movement == 37) || (eventSet[ge->event].movement == 38);
	}

	/// a world rectangle drawn over or changed: its ring cells no longer hold the picture; the
	/// picture's cells under it are marked
	void invalidate (FxBuf& S, unsigned int* marks, int wx, int wy, int w, int h) {
		if (w <= 0 || h <= 0) return;
		for (int wcy = wy >> 3; wcy <= (wy + h - 1) >> 3; wcy++) {
			const int rr = wcy % RROWS;
			const bool held = S.rowCell[rr] == wcy;
			const int cy = wcy - (fxOy >> 3);
			for (int wcx = wx >> 4; wcx <= (wx + w - 1) >> 4; wcx++) {
				const int rc = wcx - (S.wx0 >> 4);
				if (held && rc >= 0 && rc < RCOLS) S.colValid[rr] &= ~(1ull << rc);
				const int cx = wcx - (fxOx >> 4);
				if (marks && cy >= 0 && cy < fxRows && cx >= 0 && cx < fxCols) marks[cy] |= 1u << cx;
			}
		}
	}

}


/** The background map's entry for a cell: its tile (+1: the engine's 0 is "nothing"), the copy with
 * black behind it, or the black tile under a foreground cell with the black background. */
unsigned short JJ1Level::fxBgEntry (const GridElement* ge) const {

	if (fgTileFx(ge, eventSet)) return ge->bg ? (unsigned short)(2 * fxN + 1) : 0;
	return (unsigned short)((ge->bg ? fxN + 1 : 1) + ge->tile);

}


/** The foreground map's entry: the animated tile's picture for this phase, a foreground tile, or nothing. */
unsigned short JJ1Level::fxFgEntry (const GridElement* ge, int phase) const {

	if (ge->event == 123)
		return (unsigned short)(1 + (unsigned char)(phase ? eventSet[ge->event].multiB : eventSet[ge->event].multiA));
	if ((ge->event == 124) || (ge->event == 125) ||
		(eventSet[ge->event].movement == 37) || (eventSet[ge->event].movement == 38))
		return (unsigned short)(1 + ge->tile);
	return 0;

}


void JJ1Level::fxPoke (unsigned int sram, unsigned short value) {

	send(CFX_WORD(CFX_OP_POKE, 0));
	a24(sram);
	send(CFX_ARG(value >> 8));
	send(CFX_ARG(value & 255));

}


/** A level starts: the key, the engine-form tiles and sky, the layers, the maps. Without Jazz's core
 * (or its data) the level is drawn as before (fxOn stays false). */
void JJ1Level::fxLevelStart () {

	fxOn = false;
	if (!cfxoj_start()) return;
	cfxoj_data_reset();

	// the engine's copies of the tiles and the sky (the world's engine pack, or made here on the PC)
	const unsigned char key = video.getColorKey(tileSet);
	fxN = tileSet->h / 32;
	{
		const TileArg t = { static_cast<const unsigned char*>(tileSet->pixels), tileSet->pitch, fxN, key };
		fxTiles = tileSet->pitch == 32 ?
			cfxoj_data(cfxoj_hash(pack_hash(t.px, 32, fxN * 32), 0x54000000u | key), 32, (2 * fxN + 1) * 32, makeTiles, &t) : 0;
	}
	const bool skyOn = sky;
	const unsigned int skyAt = cfxoj_data(cfxoj_hash(0x534B5920u, skyOn ? 1 : 0), 4, RH, makeSky, &skyOn);
#ifndef __sh__
	// the PC making a world's engine pack (T32X_BAKE): every sprite's copy, not only those this run draws
	if (getenv("T32X_BAKE")) {
		cfxoj_bake_tables();
		for (int i = 0; i < fxSprites; i++) {
			SDL_Surface* s = spriteSet[i].getSurface();
			if (s && (s->flags & SDL_SRCCOLORKEY)) {
				const unsigned char* px = static_cast<const unsigned char*>(s->pixels);
				cfxoj_sprite_copy(px, s->w, s->h, s->pitch, (unsigned char)s->format->colorkey);
			}
		}
	}
#endif
	if (!fxTiles || !skyAt) {
		char msg[48];
		snprintf(msg, sizeof msg, "FX: NO ENGINE DATA (%s%s)", fxTiles ? "" : "TILES ", skyAt ? "" : "SKY");
		plat_log(msg);
		cfxoj_stop();
		return;
	}

	// the layers
	const unsigned int tiles = fxTiles - 1024;
	send(CFX_WORD(CFX_OP_LAYER, 0 | CFX_L_ON | CFX_L_OPAQUE));
	a24(skyAt); send(CFX_ARG(4)); send(CFX_ARG(RH)); send(CFX_ARG(0)); send(CFX_ARG(0));
	a24(0); send(CFX_ARG(0)); send(CFX_ARG(0));
	send(CFX_WORD(CFX_OP_LAYER, 1 | CFX_L_ON | CFX_L_TILES | CFX_L_MAPSRAM));
	a24(tiles); send(CFX_ARG(16)); send(CFX_ARG(8)); send(CFX_ARG(key)); send(CFX_ARG(32));
	a24(CFXOJ_SUNMAP); send(CFX_ARG(0)); send(CFX_ARG(0));
	send(CFX_WORD(CFX_OP_LAYER, 2 | CFX_L_ON | CFX_L_TILES | CFX_L_MAPSRAM));
	a24(tiles); send(CFX_ARG(LW)); send(CFX_ARG(LH)); send(CFX_ARG(key)); send(CFX_ARG(32));
	a24(CFXOJ_BGMAP); send(CFX_ARG(0)); send(CFX_ARG(0));
	send(CFX_WORD(CFX_OP_LAYER, 3 | CFX_L_ON | CFX_L_TILES | CFX_L_MAPSRAM | CFX_L_OVER));
	a24(tiles); send(CFX_ARG(LW)); send(CFX_ARG(LH)); send(CFX_ARG(key)); send(CFX_ARG(32));
	a24(CFXOJ_FGMAP); send(CFX_ARG(0)); send(CFX_ARG(0));

	// the maps
	const int phase = ticks & 64;
	fxNAnim = 0;
	for (int y = 0; y < LH; y++)
		for (int x = 0; x < LW; x++) {
			const GridElement* ge = &grid[y][x];
			fxPoke(CFXOJ_BGMAP + y * LW + x, fxBgEntry(ge));
			fxPoke(CFXOJ_FGMAP + y * LW + x, fxFgEntry(ge, phase));
			if (ge->event == 123 && fxNAnim < 256) fxAnim[fxNAnim++] = (unsigned short)(y << 8 | x);
		}
	fxAnimPoked = phase;
	for (int i = 0; i < 16 * 8; i++) fxPoke(CFXOJ_SUNMAP + i, 0);
	fxSun = 0;

	fxShown = cfxoj_status() & CFX_ST_SHOWN ? 1 : 0;
	fxBuf[0].valid = fxBuf[1].valid = false;
	fxCur = nullptr;
	cfxoj_n_sprites = cfxoj_n_sh2 = cfxoj_n_cells = 0; fxOverflows = 0;
	fxOn = true;

}


void JJ1Level::fxLevelEnd () {

	if (!fxOn) return;
	oj32x_fx_capture(false, 0, 0);
	cfxoj_stop();
	fxOn = false;
	char msg[120];
	snprintf(msg, sizeof msg, "FX: %u SPRITES, %u FOR THE SH2, %u CELLS, %u COPIES MISSING, %u COPIES, SET %d, %u FULL", (unsigned)cfxoj_n_sprites,
		(unsigned)cfxoj_n_sh2, (unsigned)cfxoj_n_cells, (unsigned)cfxoj_n_missing, (unsigned)cfxoj_n_copies, fxSprites, fxOverflows);
	plat_log(msg);

}


/** A cell's tile or event changed: the engine's maps now, both rings when they are next drawn. */
void JJ1Level::fxCell (unsigned char gridX, unsigned char gridY) {

	if (!fxOn) return;
	const GridElement* ge = &grid[gridY][gridX];
	fxPoke(CFXOJ_BGMAP + gridY * LW + gridX, fxBgEntry(ge));
	fxPoke(CFXOJ_FGMAP + gridY * LW + gridX, fxFgEntry(ge, fxAnimPoked));
	for (int b = 0; b < 2; b++) {
		FxBuf& S = fxBuf[b];
		if (S.nChanged < 64) S.changed[S.nChanged++] = (unsigned short)(gridY << 8 | gridX);
		else S.chOverflow = true;
	}

}


/** The frame begins: which cells this frame buffer's ring needs, the scroll, the sun; then the game's
 * sprites go to the engine (port/sdl_oj.cpp) until fxEnd. */
void JJ1Level::fxBegin (int vX, int vY, int viewH) {

	oj32x_scroll_map(false, 0, 0, 0, 0);
	plat_wait_flip();   // the swap asked for last frame must be done
	{	// another screen was shown in between (the pause menu's save, load, set-up): both rings again
		static unsigned seenPlain;
		const unsigned plain = oj32x_plain_presents();
		if (plain != seenPlain) {
			seenPlain = plain;
			fxBuf[0].valid = fxBuf[1].valid = false;
			oj32x_panel_reset();
		}
	}
	fxCur = &fxBuf[plat_back_index() & 1];
	FxBuf& S = *fxCur;

	if (S.valid && S.overflow) fxOverflows++;
	if (!S.valid || S.overflow || S.chOverflow || vX < S.wx0 || vX + canvasW > S.wx0 + RW) {
		S.wx0 = vX - (RW - 320) / 2;   // 128 px each side
		if (S.wx0 < 0) S.wx0 = 0;
		S.wx0 &= ~31;
		for (int r = 0; r < RROWS; r++) { S.rowCell[r] = -1; S.colValid[r] = 0; }
		S.valid = true;
		S.nDirty = 0; S.nChanged = 0;
	}

	// this frame's picture: the view, from the 16 x 8 grid point at or before it
	fxOx = vX & ~15;
	fxOy = vY & ~7;
	fxCols = (vX + canvasW - fxOx + 15) >> 4;
	fxRows = (vY + viewH - fxOy + 7) >> 3;
	unsigned int marks[CFX_ROWS] = {};

	// the ring rows it uses: a ring row that held another world row holds nothing of this one
	for (int cy = 0; cy < fxRows; cy++) {
		const int wcy = (fxOy >> 3) + cy, rr = wcy % RROWS;
		if (S.rowCell[rr] != wcy) { S.rowCell[rr] = wcy; S.colValid[rr] = 0; }
	}
	// what was drawn over this ring last time, the tiles changed since, the animated tiles' phase
	for (int i = 0; i < S.nDirty; i++) {
		const SDL_Rect& d = S.dirty[i];
		invalidate(S, marks, d.x + S.wx0, d.y, d.w, d.h);
	}
	for (int i = 0; i < S.nChanged; i++)
		invalidate(S, marks, (S.changed[i] & 255) * 32, (S.changed[i] >> 8) * 32, 32, 32);
	const int phase = ticks & 64;
	if (phase != fxAnimPoked) {
		for (int i = 0; i < fxNAnim; i++) {
			const int x = fxAnim[i] & 255, y = fxAnim[i] >> 8;
			fxPoke(CFXOJ_FGMAP + y * LW + x, fxFgEntry(&grid[y][x], phase));
		}
		fxAnimPoked = phase;
	}
	if (phase != S.anim) {
		for (int i = 0; i < fxNAnim; i++)
			invalidate(S, marks, (fxAnim[i] & 255) * 32, (fxAnim[i] >> 8) * 32, 32, 32);
		S.anim = phase;
	}

	// the sun: where OpenJazz puts it on the screen, its picture by vX & 3
	const unsigned short sun = (sky && skyOrb && oj32x_sky_on) ? (unsigned short)(1 + skyOrb + (vX & 3)) : 0;
	if (sun != fxSun) { fxPoke(CFXOJ_SUNMAP, sun); fxSun = sun; }
	const int sunX = ((canvasW * 4) / 5) - (vX & 3), sunY = ((canvasH - 33) * 3) / 25;
	if (sun) invalidate(S, marks, sunX + vX, sunY + vY, 32, 32);

#ifndef OJ32X_MARS
	int tr_dirty = 0;   // (PC: T32X_FX_TRACE - the cells marked for what was drawn over and what changed)
	for (int cy = 0; cy < fxRows; cy++) for (int cx = 0; cx < fxCols; cx++) tr_dirty += marks[cy] >> cx & 1;
	const int tr_nd = S.nDirty;
#endif
	// every cell of the picture the ring does not hold
	int marked = 0;
	for (int cy = 0; cy < fxRows; cy++) {
		const int rr = ((fxOy >> 3) + cy) % RROWS;
		for (int cx = 0; cx < fxCols; cx++) {
			const int rc = (fxOx >> 4) + cx - (S.wx0 >> 4);
			if (rc < RCOLS && !(S.colValid[rr] >> rc & 1)) marks[cy] |= 1u << cx;
			if (marks[cy] >> cx & 1) marked++;
		}
	}

	// the frame's commands
	cfxoj_frame(fxCols, fxRows);
	const bool full = marked == fxCols * fxRows;
#ifndef OJ32X_MARS
	{
		static int tr = -1;
		if (tr < 0) { const char* e = getenv("T32X_FX_TRACE"); tr = e && *e == '1'; }
		if (tr) fprintf(stderr, "FXTR v %d %d cells %d marked %d dirty %d rects %d full %d\n", vX, vY,
		                fxCols * fxRows, marked, tr_dirty, tr_nd, full ? 1 : 0);
	}
#endif
	send(CFX_WORD(CFX_OP_BEGIN, full ? 1 : 0));
	send(CFX_WORD(CFX_OP_SCROLL, 0)); send(CFX_ARG(0)); send(CFX_ARG(fxOy % RH));
	{
		const int px = sunX + vX - fxOx, py = sunY + vY - fxOy;   // the sun's place in the picture
		send(CFX_WORD(CFX_OP_SCROLL, 1)); send(CFX_ARG((512 - px % 512) % 512)); send(CFX_ARG((256 - py % 256) % 256));
	}
	send(CFX_WORD(CFX_OP_SCROLL, 2)); send(CFX_ARG(fxOx)); send(CFX_ARG(fxOy));
	send(CFX_WORD(CFX_OP_SCROLL, 3)); send(CFX_ARG(fxOx)); send(CFX_ARG(fxOy));
	if (!full)
		for (int cy = 0; cy < fxRows; cy++)
			if (marks[cy]) {
				send(CFX_WORD(CFX_OP_CELLS, cy));
				send(CFX_ARG(marks[cy] & 0x1FFF));
				send(CFX_ARG(marks[cy] >> 13));
			}

	// from here on the game draws: its sprites to the engine, the rest into the ring (recorded)
	S.nDirty = 0; S.overflow = false; S.nChanged = 0; S.chOverflow = false;
	if (sun) {
		SDL_Rect& d = S.dirty[S.nDirty++];
		d.x = (Sint16)(sunX + vX - S.wx0); d.y = (Sint16)(sunY + vY); d.w = 32; d.h = 32;
	}
	oj32x_scroll_map(true, S.wx0, vX, vY, viewH);
	oj32x_scroll_record(S.dirty, FX_DIRTY, &S.nDirty, &S.overflow);
	oj32x_fx_capture(true, fxOx, fxOy);

}


/** The sprites are in: the engine composes, the composed cells go into the ring, then the SH2 draws
 * what the engine could not. */
void JJ1Level::fxEnd () {

	oj32x_fx_capture(false, 0, 0);
	FxBuf& S = *fxCur;
	oj32x_prof(3);   // (the profiler: what the sprites' commands cost; F from here is the cells' copy)
	send(CFX_WORD(CFX_OP_GO, 0));
	cfxoj_wait_idle();
	const int b = fxShown ^ 1;
	send(CFX_WORD(CFX_OP_SHOW, b));
	cfxoj_wait_shown(b);
	fxShown = b;
	oj32x_prof(7);   // (the profiler's E: the SH2 waiting for the engine)

	uint32_t map[CFX_ROWS];
	cfxoj_cell_map(map);
	unsigned char* ring = plat_scroll_ring();
	for (int cy = 0; cy < fxRows; cy++) {
		if (!map[cy]) continue;
		const int rr = ((fxOy >> 3) + cy) % RROWS;
		for (int cx = 0; cx < fxCols; cx++) {
			if (!(map[cy] >> cx & 1)) continue;
			const int rc = (fxOx >> 4) + cx - (S.wx0 >> 4);
			if (rc < 0 || rc >= RCOLS) continue;
			cfxoj_copy_cell(fxCols, cx, cy, ring + rr * 8 * RW + rc * 16, RW);
			S.colValid[rr] |= 1ull << rc;
		}
	}

	oj32x_fx_replay();

}

#endif
