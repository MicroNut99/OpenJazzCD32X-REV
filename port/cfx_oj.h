/*
 * OpenJazz CD32X - the CD32X cart compositor (CD32X-BootCamp cartfx/) for Jazz's levels, OJ32X_FX.
 *
 * The engine on the EverDrive PRO's FPGA composes the level's 32X picture (the sky, the sun, the background
 * tiles, the sprites, the foreground tiles) in 16 x 8 cells; the master copies the cells it composed into the
 * scrolling renderer's ring (jj1levelfx.cpp). This file is the engine's side of it:
 *   - the engine itself: the cart's window on the 32X; on the PC the BootCamp's reference model (cartfx.c),
 *     the C the Verilog was checked against word for word;
 *   - its PSRAM: the engine reads pictures from the cart. On the PC the engine-form copies (tiles and sprites
 *     with their colour key moved to 0, cartfx.h) are made here at run time; the console build reads them
 *     from the world pack.
 * The specification of every word: cartfx/model/cartfx.h.
 */
#ifndef OJ32X_CFX_OJ_H
#define OJ32X_CFX_OJ_H

#include <stdint.h>
#include <stdbool.h>
#include "cartfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SRAM word addresses of what Jazz keeps in the engine's SRAM (the pictures take words 0-0xFFFF) */
#define CFXOJ_BGMAP   0x10000u    /* the background tiles' map: 256 x 64 entries */
#define CFXOJ_FGMAP   0x14000u    /* the foreground tiles' map: 256 x 64 entries */
#define CFXOJ_SUNMAP  0x18000u    /* the sun's map: 16 x 8 entries, one of them the sun */

/* the key; true when Jazz's core answered (ID 0xC2) - then every other call here may be used */
bool cfxoj_start(void);
bool cfxoj_active(void);
/* the engine off (leaving a level) */
void cfxoj_stop(void);
/* around the SH2's own reads of cart 0x3E0000-0x3FFFFF (a page's tail, the save area): with the engine
 * on those addresses are its window, so it is turned off for them (it keeps its layers, maps and
 * pictures) and keyed again after. suspend: 1 when it was on. */
int cfxoj_suspend(void);
void cfxoj_resume(int was_on);
/* one command word into the engine (it waits while the engine's queue says FULL) */
void cfxoj_send(uint16_t w);
/* counter n (cartfx.h), and the status */
uint16_t cfxoj_counter(int n);
uint16_t cfxoj_status(void);
/* until the engine is idle (all commands done, its memory writes out): false after about 200 ms */
bool cfxoj_wait_idle(void);
/* until the window shows buffer b: false after about 200 ms */
bool cfxoj_wait_shown(int b);
/* the shown picture's cell map (24 rows x 2 words, cartfx.h): row r -> bit k = cell k (0-20) */
void cfxoj_cell_map(uint32_t rows[24]);
/* cell (cx, cy) of the shown picture (cols cells wide) into dst: 8 rows of 16 bytes, pitch bytes apart */
void cfxoj_copy_cell(int cols, int cx, int cy, uint8_t *dst, int pitch);

/* PSRAM: the engine's data - copies of the game's pictures in its form, tables. They are filed in the
 * packs as pictures under a hash (pack_picture_hash): w x h bytes found by hash, w and h; the PSRAM
 * byte address of the mounted copy comes back (the cart's page as the SH2 sees it mapped now). On the
 * PC what no pack has is made by make(out, w, h, arg) where the model can read it, and baked into the
 * pack being made (T32X_BAKE: the world's engine pack); the console has 0 then (cfxoj_n_missing).
 * cfxoj_data_reset at a level's start; cfxoj_hash: a hash from a picture's own (pack_hash) and a tag. */
typedef void (*cfxoj_make)(uint8_t *out, int w, int h, const void *arg);
void cfxoj_data_reset(void);
uint32_t cfxoj_data(uint32_t hash, int w, int h, cfxoj_make make, const void *arg);
uint32_t cfxoj_hash(uint32_t base, uint32_t tag);
/* the tables every world's engine pack has (the flash colours' blend tables): made when baking */
void cfxoj_bake_tables(void);
/* the engine-form copy of a picture (w x h, pitch bytes a row) whose colour key is key: every byte v stored
 * as (v - key) & 255, so the key is the engine's 0 and a sprite drawn with colour offset key gives v back.
 * Made once per picture and key (looked up by the pixels' address); 0: none */
uint32_t cfxoj_sprite_copy(const uint8_t *pixels, int w, int h, int pitch, uint8_t key);

/* a frame's picture size (cols x rows cells): SETUP when it changed; no flash colour chosen yet */
void cfxoj_frame(int cols, int rows);
/* a sprite flashed to one colour c (Sprite::flashPalette: a hit) is drawn through a blend table that is
 * c everywhere; one such table a frame: true when c can be drawn so this frame (the table set), false
 * when another colour has the table (the SH2 draws it) */
bool cfxoj_flash(uint8_t c);

/* counts for the profiler's lines: engine sprites, pictures the SH2 drew itself, cells copied */
extern uint32_t cfxoj_n_sprites, cfxoj_n_sh2, cfxoj_n_cells, cfxoj_n_missing, cfxoj_n_copies;

#ifdef __cplusplus
}
#endif

#endif
