/*
 * OpenJazz CD32X REV - the Genesis plane, SH2 side (description in genbg.h and
 * docs/GENESIS_PLANE.md). Shared by the 32X and the PC build.
 */
#include "genbg.h"
#include "pack.h"
#include "plat.h"
#include <stdio.h>
#include <string.h>

enum { OFF, LOADED, SHOWN };
static int state = OFF;
static bool level_sky;
static uint16_t base555[256];           /* the level's own colours: the fade is measured on them */
static uint16_t comm4, comm6;
static int fade_bits;                   /* COMM6 bits 11-14 */
static bool plain;                      /* the last picture was not the level */

/* tiles changed by the game logic (on the slave SH2 during the master's drawing): sent from
 * the master at the next frame */
#define CELLS 64
static uint32_t cells[CELLS];
static int ncells;

bool genbg_active(void) { return state != OFF; }

bool genbg_is_background(int i)
{
	return i == 127 || (level_sky && i >= 156);
}

void genbg_level_loaded(int level, bool sky, const uint8_t *rgb)
{
	char name[16];
	const uint8_t *data;
	uint32_t size;
	if (state != OFF) genbg_level_left();
	ncells = 0;
	snprintf(name, sizeof name, "GENESIS%d.BIN", level);
	if (!pack_file(name, &data, &size) || size < 40 || memcmp(data, "OJGB", 4) != 0 || data[5] != 3) {
		char msg[64];
		snprintf(msg, sizeof msg, "GENBG: no %s - the 32X draws the level", name);
		plat_log(msg);
		return;
	}
	level_sky = sky;
	for (int i = 0; i < 256; i++)
		base555[i] = (uint16_t)((rgb[i * 3] >> 3) | (rgb[i * 3 + 1] >> 3) << 5 | (rgb[i * 3 + 2] >> 3) << 10);
	const int r = plat_genbg(GENBG_LOAD, plat_cart_offset_of(data));
	char msg[80];
	snprintf(msg, sizeof msg, "GENBG: %s %lu bytes at cart %06lX: %s", name, (unsigned long)size,
		(unsigned long)plat_cart_offset_of(data), r == 1 ? "loaded" : "NOT LOADED");
	plat_log(msg);
	if (r == 1) state = LOADED;
}

void genbg_level_left(void)
{
	if (state == OFF) return;
	plat_genbg(GENBG_OFF, 0);
	state = OFF;
	ncells = 0;
	comm6 = GENBG_HIDE;
}

void genbg_cell(int x, int y, int tile)
{
	if (state == OFF) return;
	if (ncells < CELLS) cells[ncells++] = ((uint32_t)((y << 8) | x) << 16) | (uint32_t)(tile & 255);
}

void genbg_frame(int vX, int vY)
{
	if (state == OFF) return;
	if (vX < 0) vX = 0;
	if (vY < 0) vY = 0;
	comm4 = (uint16_t)vX;
	comm6 = (uint16_t)((vY & 0x7FF) | fade_bits);
	plain = false;
	if (state == LOADED) {
		plat_genbg(GENBG_START, ((uint32_t)vX << 16) | (uint32_t)(vY & 0xFFFF));
		state = SHOWN;
	}
	for (int i = 0; i < ncells; i++) plat_genbg(GENBG_CELL, cells[i]);
	ncells = 0;
}

void genbg_plain_screen(void)
{
	plain = true;
}

void genbg_cram(const uint16_t *pal, uint16_t *cram)
{
	/* the fade: the level's colours 0-111 (no rotations there) as shown now against their own */
	long shown = 0, base = 0, white = 0;
	for (int i = 0; i < 112; i++) {
		if (genbg_is_background(i)) continue;
		const uint16_t s = pal[i], b = base555[i];
		shown += (s & 31) + (s >> 5 & 31) + (s >> 10 & 31);
		base += (b & 31) + (b >> 5 & 31) + (b >> 10 & 31);
		white += 93;
	}
	int bits = 0;
	if (base > 0 && shown * 64 < base * 63) {
		int lv = (int)(((base - shown) * 7 + base / 2) / base);
		if (lv > 7) lv = 7;
		if (lv < 1) lv = 1;
		bits = lv << GENBG_FADE_SHIFT;
	} else if (white > base && shown * 63 > base * 64) {
		int lv = (int)(((shown - base) * 7 + (white - base) / 2) / (white - base));
		if (lv > 7) lv = 7;
		if (lv < 1) lv = 1;
		bits = (lv << GENBG_FADE_SHIFT) | GENBG_FADE_WHITE;
	}
	fade_bits = bits;
	for (int i = 0; i < 256; i++) {
		const uint16_t c = pal[i] ? pal[i] : 0x0400;
		cram[i] = genbg_is_background(i) ? c : (uint16_t)(c | 0x8000);
	}
}

uint16_t genbg_comm4(void) { return comm4; }

uint16_t genbg_comm6(void)
{
	if (state != SHOWN || plain) return GENBG_HIDE;
	return (uint16_t)((comm6 & 0x7FF) | fade_bits);
}
