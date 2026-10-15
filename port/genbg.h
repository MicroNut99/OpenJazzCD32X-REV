/*
 * OpenJazz CD32X REV - the Genesis plane: the level's background tiles drawn by the Genesis
 * VDP (plane A, black cells on plane B) behind the 32X picture, which then holds only the
 * sky, the sprites, the foreground tiles and the panel. See docs/GENESIS_PLANE.md.
 *
 * The 68000 does the drawing (cd32x/subcpu/genbg_md.s) from the level's OJGB file in the
 * world pack (GENESIS<level>.BIN, tools/genesis_convert.py). The SH2 side (this file):
 *   - at the level's load: finds the file, has the 68000 load it (Sub-CPU command 67)
 *   - at its first frame: has the 68000 show it at the camera
 *   - every frame: the camera and the fade for the 68000's vblank (COMM4 / COMM6, written with
 *     the swap request), and the 32X palette with bit 15 (in front of the Genesis) on every
 *     colour but the background ones (sky, plain background)
 *   - tiles the game changes (destroyed blocks): sent to the 68000
 *   - leaving the level: off
 * The PC build runs the same code; plat_host.c keeps a model of the 68000's plane and shows
 * both pictures together in the saved frames.
 */
#ifndef OJ32X_GENBG_H
#define OJ32X_GENBG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* operations of Sub-CPU command 67 (cd32x/subcpu/main.c) */
#define GENBG_LOAD  1   /* argument: cart offset of the level's OJGB file */
#define GENBG_START 2   /* camera x << 16 | y */
#define GENBG_OFF   3
#define GENBG_CELL  4   /* (y * 256 + x) << 16 | tile (255 = nothing on plane A) */

/* COMM6 bits besides the camera y (0-10) */
#define GENBG_FADE_SHIFT 11      /* 3 bits: 0 = none .. 7 = all black / all white */
#define GENBG_FADE_WHITE 0x4000
#define GENBG_HIDE       0x8000  /* another screen is shown: the Genesis display off */

/* a level was loaded: palette = its 256 colours as RGB triples; sky = it has the sky */
void genbg_level_loaded(int level, bool sky, const uint8_t *paletteRGB);
/* the level is left */
void genbg_level_left(void);
/* the level's frame: the camera of the picture about to be shown */
void genbg_frame(int vX, int vY);
/* a map tile changed: x, y in tiles, its new tile number or 255 for nothing on the plane
 * (a foreground tile, or no tile) */
void genbg_cell(int x, int y, int tile);
/* the plane is on (the level's file was loaded) */
bool genbg_active(void);
/* the palette entry is part of the background (drawn by the 32X behind the Genesis plane) */
bool genbg_is_background(int index);
/* the palette for the 32X's CRAM: bit 15 (in front of the Genesis) on all colours but the
 * background ones; black (0) is written as 0x0400 as everywhere else. Also measures the fade
 * against the level's own colours for the 68000. */
void genbg_cram(const uint16_t *palette555, uint16_t *cram);
/* COMM4 / COMM6 for the picture that is swapped in next */
uint16_t genbg_comm4(void);
uint16_t genbg_comm6(void);
/* a screen that is not the level is shown (pause menu's save / load / set-up) */
void genbg_plain_screen(void);

/* platform: Sub-CPU command 67 (32X) or the PC's model of the 68000; returns its result */
int plat_genbg(int op, uint32_t arg);
/* platform: the cart offset of an address in the cart */
uint32_t plat_cart_offset_of(const void *p);

#ifdef __cplusplus
}
#endif

#endif
