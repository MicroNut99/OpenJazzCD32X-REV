/*
 * OpenJazz CD32X - what the game code (under #ifdef OJ32X) calls in the port layer.
 */
#ifndef OJ32X_H
#define OJ32X_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pad layout (port/sdl_oj.cpp): play = in a level, menu = everywhere else. */
#define OJ32X_PAD_MENU 0
#define OJ32X_PAD_PLAY 1
void oj32x_pad_mode(int mode);

#ifdef __cplusplus
}
struct SDL_Surface;
/* a surface for pixels that never change: the identical picture from the cart pack, used in
 * place (PC build: recorded for the pack); a picture no pack has stays in SDRAM */
SDL_Surface *oj32x_cart_surface(const unsigned char *pixels, int w, int h);
/* a finished surface (e.g. a font atlas) is swapped for its cart pack copy; returns the new
 * surface and frees the old one, or returns it unchanged when no pack has it */
SDL_Surface *oj32x_freeze(SDL_Surface *s);
/* a band view of a surface (same pixels, own clip rectangle; index 0 or 1) */
SDL_Surface *oj32x_view(SDL_Surface *base, int index, int x, int y, int w, int h);
extern "C" {
#endif

/* the packs are mounted (PC build: T32X_PACK at the first file look-up) */
void oj32x_packs_ready(void);

/* frame profiler (slots: 0 logic 1 background 2 sprites 3 foreground 4 panel 5 present
 * 6 other) */
void oj32x_prof(int slot);
void oj32x_prof_steps(int n);
void oj32x_prof_frame(void);
const char *oj32x_prof_text(int line);   /* lines 0-2 */
/* the profiler box, drawn into the screen (top left) */
void oj32x_prof_draw(struct SDL_Surface *s);

/* the pad bits as last read (PAD_* in plat.h) */
unsigned int oj32x_pad(void);

/* clear both frame buffers (leaving a level) */
void oj32x_clear_screens(void);

/* bring the world or bonus pack for a level, planet or bonus file into the cart (a loading
 * screen on the 32X; nothing happens when it is already there) */
void oj32x_level_pack(const char *levelFile);

/* a sound effect (32X: Sega CD PCM chip; PC: nothing) */
void oj32x_sfx(int id);

/* a CD audio track directly (credits screen); 0 = stop */
void oj32x_music_track(int track);

/* music as CD audio: the track of this music file (tools/music_tracks.txt); NULL = stop */
void oj32x_music(const char *fileName, bool restart);
#ifdef __cplusplus
}
/* The line-table scrolling renderer (description in jj1levelframe.cpp).
 * oj32x_scroll_map: from now until the next present, blits and fills aimed at the screen go
 *   into the ring (view rows) and the panel block (rows below), split where they wrap.
 * oj32x_scroll_record: where the mapping records the rectangles it drew (ring columns,
 *   world rows), so the background under them can be restored in two frames.
 * oj32x_ring_surface: the back buffer's ring as a surface, for drawing the background.
 * oj32x_screen_row: a pointer to screen row y (mapped), for code that writes pixels itself.
 * oj32x_put_pixel: one pixel into the frame buffer as a 16-bit read-modify-write (single
 *   byte writes are unreliable there).
 * oj32x_scroll_active: the mapping is on (and the view's y).
 * oj32xScrollReset: mapping off, both rings invalid (leaving a level).
 * oj32x_panel_reset: copy the whole panel into both buffers at the next presents.
 * oj32x_plain_presents: counts frames shown without the mapping (another screen was drawn
 *   over the rings - the level then redraws them). */
#define OJ32X_RING_W  384
#define OJ32X_RING_H  200
#define OJ32X_PANEL_H 33
void oj32x_scroll_map(bool on, int wx0, int vX, int vY, int viewH);
void oj32x_scroll_record(struct SDL_Rect *list, int cap, int *count, bool *overflow);
struct SDL_Surface *oj32x_ring_surface(void);
unsigned char *oj32x_screen_row(int y);
void oj32x_put_pixel(unsigned char *p, unsigned char v);
bool oj32x_scroll_active(int *vY);
void oj32xScrollReset();
void oj32x_panel_reset(void);
unsigned oj32x_plain_presents(void);
/* Holiday Hare '95 (plat.h plat_game): set by the episode menu for its run */
void oj32x_set_game(int g);
bool oj32x_hh95_available(void);
/* the port's own settings, kept in the RAM cart (0 = frame counter, 1 = A/B swap) */
int oj32x_port_setting(int key);
void oj32x_port_setting_set(int key, int value);
/* OJ32X_LONG_READS: 32-bit source reads in the blits on (true) or off; Mode switches it */
bool oj32x_long_reads(void);
void oj32x_long_reads_toggle(void);
/* A/B swap (set-up menu): A fires and B jumps instead of the other way round */
bool oj32x_ab_swapped(void);
void oj32x_ab_swap(bool on);
extern "C" {
#endif

/* music volume 0 stops the CD music, more starts it again; oj32x_music_remember notes the
 * music file asked for while off, to start when switched on */
void oj32x_music_volume(int volume);
void oj32x_music_remember(const char *fileName);

/* run fn(arg, 0) and fn(arg, 1) - on both SH2s at once only in OJ32X_SLAVE_DRAWS builds */
void oj32x_parallel(void (*fn)(void *, int), void *arg);
/* J23b: the level sky (gradient + sun / moon) on or off - PAUSE: background on / off */
extern int oj32x_sky_on;

/* skip the intro cutscene (32X: always; PC build: T32X_SKIP_INTRO=1) */
int oj32x_skip_intro(void);

/* a line for the platform's log (32X: the error screens' "last log") */
void oj32x_log(const char *msg);

/* memory report at a point of interest: logged (PC build: also the heap, for
 * tools/heap_peak.sh) */
void oj32x_mem_mark(const char *label);

#ifdef __cplusplus
}
/* set by Level::loop just before ::loop (main.cpp), which applies and clears it */
extern bool oj32xPlayLayout;
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif
