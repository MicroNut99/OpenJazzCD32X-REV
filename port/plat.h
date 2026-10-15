/*
 * OpenJazz CD32X - platform layer: everything the port needs from the machine.
 *
 * Two implementations:
 *   plat_mars.c  the Sega CD + 32X (master SH2), talking to the Sub-CPU through the
 *                32X COMM registers (commands 38-62, see cd32x/subcpu/main.c)
 *   plat_host.c  the PC test build: the same game and port code, packs read from files,
 *                frames saved as images, pad input from T32X_PAD
 *
 * This layer was taken over from the Tyrian 32X port. Declarations marked "Tyrian leftover"
 * are implemented only by the PC build and are not used by OpenJazz.
 */
#ifndef T32X_PLAT_H
#define T32X_PLAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pad bits as the Sub-CPU reports them (command 48), active-high: the common Genesis
 * 6-button layout, as in D32XR. */
#define PAD_UP     0x0001
#define PAD_DOWN   0x0002
#define PAD_LEFT   0x0004
#define PAD_RIGHT  0x0008
#define PAD_B      0x0010
#define PAD_C      0x0020
#define PAD_A      0x0040
#define PAD_START  0x0080
#define PAD_Z      0x0100
#define PAD_Y      0x0200
#define PAD_X      0x0400
#define PAD_MODE   0x0800

/* Screen geometry: the game draws 320x200; the 32X shows 224 lines (NTSC). */
#define PLAT_SCREEN_W   320
#define PLAT_SCREEN_H   224
#define PLAT_GAME_H     200
#define PLAT_TOP_BORDER ((PLAT_SCREEN_H - PLAT_GAME_H) / 2)

void plat_init(void);

/* Free-running millisecond clock. */
uint32_t plat_ticks_ms(void);

/* Seed for the game's random numbers (replaces time(NULL)). */
uint32_t plat_random_seed(void);

/* Current pad state (PAD_* bits). */
uint16_t plat_read_pad(void);

/* Show one 320x200 8-bit frame with the given 256-entry palette (32X CRAM format
 * 0BBBBBGGGGGRRRRR): copied into the back buffer, then the buffers are swapped at the next
 * vertical blank. Used for screens drawn in SDRAM; see plat_present_canvas for the rest. */
void plat_present(const uint8_t *pixels, int pitch, const uint16_t *palette555);

/* Tyrian leftover (defined on both, used by neither game path): scratch space in the 32X
 * frame buffer (see port/plat_mars.c for the layout).
 * The SH2 only reaches the frame buffer that is NOT on screen, through one fixed
 * address window; every plat_present() swaps which physical buffer that is.
 * So these areas keep their contents only until the next plat_present():
 * afterwards they show the other buffer's contents (from two flips ago).
 *   PLAT_FB_SPARE    64000 bytes, 320 per line: free for the game
 *   PLAT_FB_DISPLAY  where plat_present() puts the picture: usable as scratch
 *                    only for data that is finished before the next present */
#define PLAT_FB_SPARE   0
#define PLAT_FB_DISPLAY 1
uint8_t *plat_fb_area(int area);

/* The game screen drawn straight into the 32X's back buffer (saves a 64,000-byte screen in
 * SDRAM and copying it every frame). plat_canvas: where to draw (320 per line, 200 lines), or
 * NULL when the platform has no such thing (PC); plat_present_canvas: show it and swap. */
uint8_t *plat_canvas(void);
void plat_present_canvas(const uint16_t *palette555);
/* the palette entry the 12 border lines above and below the 200-line picture show (the
 * darkest entry of the current palette) */
void plat_border_colour(int index);

/* The line-table scrolling renderer (OJ32X_SCROLL; description in
 * oj/src/jj1/level/jj1levelframe.cpp). Each frame buffer holds a ring (OJ32X_RING_W x
 * OJ32X_RING_H, world row wy at ring row wy % H) and the status panel (320 x 33).
 * plat_back_index: which of the two buffers is being drawn now (0/1).
 * plat_present_scroll: the line table points the view's lines into the ring (from ring row
 * row0, column xoff; an odd xoff uses the 32X's 1-pixel shift register), then the panel's
 * lines; then the buffers are swapped as for any other frame. */
uint8_t *plat_scroll_ring(void);
uint8_t *plat_scroll_panel(void);
int plat_back_index(void);
void plat_present_scroll(const uint16_t *palette555, int row0, int xoff, int viewH);
/* Wait until the last requested buffer swap has happened. With OJ32X_FLIP_ASYNC presenting
 * only requests the swap, so the game's logic runs while the 32X waits for the vblank; the
 * back buffer must not be drawn into before the swap. */
void plat_wait_flip(void);
/* Which game is being played: 0 = Jazz Jackrabbit CD, 1 = Holiday Hare '95 (chosen in the
 * episode menu; PC build: T32X_HH95=1 at start). It selects the world packs (OJ6xx), the
 * sound effects pack and the save slots. plat_hh95_available: its packs are on the disc. */
int plat_game(void);
void plat_set_game(int g);
int plat_hh95_available(void);
/* the port's own settings (0 = frame counter shown, 1 = A/B swapped), kept in the RAM cart's
 * save area; OpenJazz's settings file has no place for them */
int plat_port_setting(int key);
void plat_port_setting_set(int key, int value);
/* both frame buffers' picture areas cleared (32X); PC: nothing */
void plat_clear_screens(void);

/* Tyrian leftover. Like plat_present, but the playfield (x 0..263, rows 0..183) is already in
 * the back buffer's picture area (drawn there directly during levels, see
 * port/video32x.c): only the status bar parts of `pixels` are copied - the
 * right strip (x 264..319, rows 0..183) and rows 184..199. */
#define PLAT_PLAYFIELD_W 264
#define PLAT_PLAYFIELD_H 184
void plat_present_hud(const uint8_t *pixels, int pitch, const uint16_t *palette555);

/* Tyrian leftover. DMA copy into the frame buffer (port/backgrnd.c: layer 1, one screen line
 * at a time). dst and src 4-byte aligned, bytes a multiple of 4.
 * plat_dma_start returns at once on the 32X (the SH2's DMA unit copies while
 * the CPU goes on); plat_dma_wait waits for the last one to finish. If the DMA
 * unit ever does not finish in time, plat_dma_ok() becomes false for good and
 * the caller falls back to CPU copying. PC build: a plain memcpy. */
void plat_dma_start(void *dst, const void *src, unsigned int bytes);
void plat_dma_wait(void);
bool plat_dma_ok(void);

/* Tyrian leftover. Sound effect mixer command (port/mixer.h): in Tyrian 32X it went to the
 * slave SH2; the PC build mixes directly. OpenJazz plays effects with plat_sfx. */
#include "mixer.h"
void plat_sound_command(const MixCmd *c);

/* Tyrian leftover (OpenJazz uses plat_music). Music: a CD audio track on the Sega CD, played on repeat by the 68000
 * (cart_md.s commands 45 and 54; without a Sega CD the 68000 ignores them).
 * Track 1 is the CD's data track; Tyrian's song n is track n + 2
 * (tools/render_music, tools/make_tyrian_cd.py). */
void plat_music_play(unsigned int track);
void plat_music_stop(void);

/* Tyrian leftover (OpenJazz loads packs: plat_load_pack). Files on the Sega CD's data track (ISO 9660 root, 8.3 names; the level
 * files). plat_cd_open: true if found, with the file's first sector and its
 * length. plat_cd_read: `bytes` (<= 4096) from `offset` (a multiple of 2048)
 * into dst. On the 32X: the 68000 (cart/cd_files.c, commands 60/61) reads
 * through D32XR's Sega CD code and hands the bytes over the COMM registers.
 * PC build: files in the folder T32X_CD_DIR. */
bool plat_cd_present(void);  /* a Sega CD is there (no disc access; cached) */
bool plat_cd_open(const char *name, uint32_t *sector, uint32_t *length);
bool plat_cd_read(uint32_t sector, uint32_t offset, uint32_t bytes, void *dst);

/* Tyrian leftover. Sega Mouse in controller port 2: movement since the last call (Y up =
 * positive, as the mouse counts) and buttons (bit 0 left, 1 right, 2 middle,
 * 3 start); false when no mouse is there. 32X: command 63 (cart/mouse.c). */
#define MOUSE_LEFT   1
#define MOUSE_RIGHT  2
#define MOUSE_MIDDLE 4
#define MOUSE_START  8
bool plat_read_mouse(int *dx, int *dy, uint8_t *buttons);

/* Tyrian leftover. Network play over controller port 2 (D32XR's transport, cart/net_link.s):
 * type 1 = link cable, -1 = serial (4800 baud). Bytes can be lost; the game
 * protocol copes (port/sdlnet32x.c, src/network.c). PC build: two instances
 * connected by named pipes (T32X_LINK_IN / T32X_LINK_OUT). */
#define NET_LINK    1
#define NET_SERIAL (-1)
bool plat_net_setup(int type);
void plat_net_cleanup(void);
bool plat_net_send(const uint8_t *bytes, unsigned int count);   /* false: a byte timed out */
unsigned int plat_net_recv(uint8_t *bytes, unsigned int most);   /* bytes waiting, 0 = none */

/* The RAM cart's data area (read-only for the SH2). plat_cart_data returns its start and
 * size; plat_cart_write copies bytes to an offset in it (32X: stops with an error screen -
 * the SH2 cannot write the cart, packs come from the disc; PC: a mapping made writable just
 * for the copy). */
#define PLAT_CART_DATA_OFFSET 0x100000   /* after the first 1 MB, kept for the engine (0.43 MB) */
#define PLAT_CART_DATA_END    0x3F0000   /* 0x3F0000.. settings and saves (as OpenLara) */
uint8_t *plat_cart_data(uint32_t *size);
void plat_cart_write(uint32_t offset, const void *src, size_t bytes);

/* A world or bonus pack from the disc into the cart at <cart_offset> (32X: command 38 with
 * file number <id>, then command 46 chunk by chunk, with the loading screen; PC:
 * T32X_PACK_DIR/OJ<id-200>.PAK). Returns its length, or a negative number when there is none.
 * plat_packs_ready: the common pack is mounted (PC: mounts T32X_PACK on first use; 32X:
 * nothing, it was loaded at start-up). plat_cart_ptr: the address of a cart offset. */
int32_t plat_load_pack(int id, uint32_t cart_offset, uint32_t max_bytes);
void plat_packs_ready(void);

/* fn(arg, 1) on the slave SH2 while fn(arg, 0) runs here; returns when both are done.
 * Only in builds with OJ32X_SLAVE_DRAWS (measured: no gain, both SH2s wait on the same bus);
 * otherwise, and on the PC, one after the other. fn may only draw and read - no memory
 * allocation, no logging (newlib's allocator is not reentrant). */
void plat_parallel(void (*fn)(void *, int), void *arg);

/* play sound effect n (OpenJazz SE::Type, 1..) - 32X: on the Sega CD's PCM chip, sent with
 * the next pad request; PC: nothing */
void plat_sfx(int id);
/* CD audio track n (0 = stop), repeating; n | 0x8000 plays it once (the credits' CD
 * player). Only in builds with OJ32X_MUSIC 1 (music tracks on the disc). */
void plat_music(int track);
/* a microsecond clock for the profiler (32X: vblank count plus the watchdog counter since
 * the last vblank, about 0.18 ms steps; wraps after ~71 minutes) */
uint32_t plat_prof_us(void);
/* slave SH2 drawing on/off at run time, for OJ32X_SLAVE_DRAWS builds (PC: always off) */
/* J23: fn(arg, 0) on the master and fn(arg, 1) on the slave SH2 (on an 8 KB stack of its own)
 * at the same time - the level's logic beside the background drawing; one after the other
 * when switched off (plat_slave_logic, the Mode button) or on the PC */
/* J24: the time (ms) the next frame will be shown - the game's clock in levels */
uint32_t plat_frame_ms(void);
void plat_logic_parallel(void (*fn)(void *, int), void *arg);
void plat_slave_logic(int on);
int plat_slave_logic_on(void);
void plat_slave_draws(int on);
int plat_slave_draws_on(void);
const uint8_t *plat_cart_ptr(uint32_t cart_offset);

/* Tyrian leftover. Base of the ROM file system image (see tools/mkromfs.py). */
const uint8_t *plat_romfs_base(void);

/* Tyrian leftover (defined on both, not used): bytes that malloc can still get. */
size_t plat_heap_free(void);

/* Memory report at a point of interest (PC build: records the heap for tools/heap_peak.sh;
 * 32X: nothing). */
void plat_mem_mark(const char *label);

/* Log text (PC: printed; 32X: kept as the "last log" lines of the error screens). */
void plat_log(const char *msg);

/* Fatal error: a red screen with the message, the last log lines and the heap use; stops.
 * Never returns. */
void plat_fatal(const char *msg) __attribute__((noreturn));

/* Program exit: on the 32X a blue screen with the exit code and the last log lines (a
 * console has nothing to return to). Never returns. */
void plat_exit(int code) __attribute__((noreturn));

#endif
