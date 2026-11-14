/*
 * OpenJazz CD32X - the CD32X cart compositor's side of the port, OJ32X_FX (cfx_oj.h says what it is).
 */
#include "cfx_oj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "plat.h"
#include "pack.h"
#include "genbg.h"
}

uint32_t cfxoj_n_sprites, cfxoj_n_sh2, cfxoj_n_cells, cfxoj_n_missing, cfxoj_n_copies;

static bool on;
static int setup_cols, setup_rows, frame_flash = -1;
static uint32_t setup_blend;
static void send_setup(void);

/* the PSRAM address the engine reads for a byte the SH2 sees at p (a mounted pack in the cart): window 0
 * shows page 0, window 1 the page mapped now (2 MB pages: bank = page * 4 + offset / 512 KB) */
static uint32_t phys_of_offset(uint32_t off)
{
	if (off < 0x200000u) return off;
#if OJ32X_CART_MB
	return (uint32_t)plat_cart_page_now() * 0x200000u + (off - 0x200000u);
#else
	return off;
#endif
}

#ifdef OJ32X_MARS
/* ---- the console: the cart's window (cartfx.h), cache-through ---- */
#define CART_THRU   0x22000000u
#define PORT(w)     (*(volatile uint16_t *)(CART_THRU + CFX_CMD_BASE + 2u * (uint32_t)(w)))

static void pause_a_little(void) { for (volatile int i = 0; i < 40; i++) { } }   /* between status reads (BootCamp docs/06 rule 7) */

static uint16_t port(uint16_t w) { return PORT(w); }

static uint16_t pic_word(uint32_t byte) { return *(volatile uint16_t *)(CART_THRU + CFX_PIC_BASE + byte); }

static uint32_t phys(const void *p) { return phys_of_offset((uint32_t)(uintptr_t)p & 0x3FFFFFu); }

void cfxoj_copy_cell(int cols, int cx, int cy, uint8_t *dst, int pitch)
{
	const uint32_t pw = 16u * (uint32_t)cols;
	const volatile uint32_t *s = (const volatile uint32_t *)(CART_THRU + CFX_PIC_BASE + (uint32_t)cy * 8u * pw + (uint32_t)cx * 16u);
	for (int r = 0; r < 8; r++, s += pw / 4, dst += pitch) {
		uint32_t *d = (uint32_t *)dst;
		const uint32_t a = s[0], b = s[1], c = s[2], e = s[3];
		d[0] = a; d[1] = b; d[2] = c; d[3] = e;
	}
	cfxoj_n_cells++;
}

#else
/* ---- the PC: the reference model. Its PSRAM is the PC build's cart (pages as plat_host.c maps them),
 * and above 0xE00000 (page 7, which the EverDrive build never uses) what this run made itself ---- */
static cfx_t fx;
static uint16_t sram[1u << 18];
#define PRIV_BASE 0xE00000u
static uint8_t priv[2u << 20];
static uint32_t priv_top;
static struct { uint32_t hash, a; int w, h; } made[64];   /* what this level made itself (not the sprites) */
static int nmade;

static const uint8_t *phys_ptr(uint32_t a)
{
	uint32_t off = a;
	if (a >= 0x200000u) {
#if OJ32X_CART_MB
		if ((int)(a >> 21) != plat_cart_page_now()) return NULL;
#else
		if (a >= 0x400000u) return NULL;
#endif
		off = 0x200000u + (a & 0x1FFFFFu);
	}
	if (off < PACK_GENESIS_CART || off >= PLAT_CART_DATA_END) return NULL;
	return plat_cart_ptr(off);
}

static uint8_t psram_byte(void *ctx, uint32_t a)
{
	(void)ctx;
	a &= 0xFFFFFFu;
	if (a >= PRIV_BASE) return a - PRIV_BASE < sizeof priv ? priv[a - PRIV_BASE] : 0xFF;
	const uint8_t *p = phys_ptr(a);
	return p ? *p : 0xFF;
}

static uint32_t phys(const void *p) { return phys_of_offset(plat_cart_offset_of(p)); }

static void pause_a_little(void) { }

static uint16_t port(uint16_t w)
{
	uint16_t v = 0;
	cfx_read(&fx, CFX_CMD_BASE + 2u * w, &v);
	return v;
}

static uint16_t pic_word(uint32_t byte)
{
	uint16_t v = 0;
	cfx_read(&fx, CFX_PIC_BASE + byte, &v);
	return v;
}

void cfxoj_copy_cell(int cols, int cx, int cy, uint8_t *dst, int pitch)
{
	const uint32_t pw = 16u * (uint32_t)cols;
	for (int r = 0; r < 8; r++, dst += pitch) {
		const uint32_t row = ((uint32_t)cy * 8u + (uint32_t)r) * pw + (uint32_t)cx * 16u;
		for (int i = 0; i < 16; i += 2) {
			const uint16_t v = pic_word(row + (uint32_t)i);
			dst[i] = (uint8_t)(v >> 8);
			dst[i + 1] = (uint8_t)v;
		}
	}
	cfxoj_n_cells++;
}
#endif

/* ---- the engine's data: filed in the packs under a hash; the PC makes (and bakes) what is missing ---- */
uint32_t cfxoj_hash(uint32_t base, uint32_t tag)
{
	uint32_t x = base ^ 0x43465821u;              /* FNV-1a over the base and the tag, 'CFX!' apart */
	for (int i = 0; i < 4; i++) x = (x ^ ((tag >> (8 * i)) & 255u)) * 16777619u;
	return x;
}

uint32_t cfxoj_data(uint32_t hash, int w, int h, cfxoj_make make, const void *arg)
{
	const uint8_t *p = pack_picture_hash(hash, w, h);
	if (p) return phys(p);
#ifdef OJ32X_MARS
	(void)make; (void)arg;
	cfxoj_n_missing++;
	return 0;
#else
	{	/* T32X_FX_PACK_ONLY=1: as the console - only what the packs have (the engine pack checked) */
		static int only = -1;
		if (only < 0) only = getenv("T32X_FX_PACK_ONLY") != NULL;
		if (only) { cfxoj_n_missing++; return 0; }
	}
	for (int i = 0; i < nmade; i++)
		if (made[i].hash == hash && made[i].w == w && made[i].h == h) return made[i].a;
	const uint32_t n = (uint32_t)w * (uint32_t)h;
	if (priv_top + n > sizeof priv) return 0;
	uint8_t *out = priv + priv_top;
	make(out, w, h, arg);
	pack_bake_picture_hash(hash, out, w, h);      /* (T32X_BAKE: into the pack being made) */
	const uint32_t a = PRIV_BASE + priv_top;
	priv_top = (priv_top + n + 1u) & ~1u;
	if (nmade < 64) { made[nmade].hash = hash; made[nmade].w = w; made[nmade].h = h; made[nmade].a = a; nmade++; }
	return a;
#endif
}

/* the sprite copies seen this level: pixels' address and key -> PSRAM address (0: none to be had) */
/* 8 bytes an entry, 4 KB in all (SDRAM is the heap's: a level needs ~175 KB of it). A level uses ~40-100
 * copies (its sprite set is ~240); when every entry is taken the SH2 draws the picture itself. ak: the
 * PSRAM address (24 bits) and the key in the top byte; p == 0: free. */
#define NCOPY 512
static struct { const uint8_t *p; uint32_t ak; } copies[NCOPY];

void cfxoj_data_reset(void)
{
	memset(copies, 0, sizeof copies);
	cfxoj_n_copies = 0;
	setup_cols = setup_rows = 0; setup_blend = 0;
	cfxoj_n_missing = 0;
#ifndef OJ32X_MARS
	priv_top = 0;
	nmade = 0;
#endif
}

struct SpriteArg { const uint8_t *pixels; int pitch; uint8_t key; };
static void make_sprite(uint8_t *out, int w, int h, const void *arg)
{
	const SpriteArg *s = static_cast<const SpriteArg *>(arg);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			out[y * w + x] = (uint8_t)(s->pixels[y * s->pitch + x] - s->key);
}

uint32_t cfxoj_sprite_copy(const uint8_t *pixels, int w, int h, int pitch, uint8_t key)
{
	uint32_t k = ((uint32_t)((uintptr_t)pixels >> 2) * 2654435761u) ^ key;
	for (int probe = 0; probe < NCOPY; probe++) {
		const uint32_t i = (k + (uint32_t)probe) & (NCOPY - 1);
		if (copies[i].p == pixels && (uint8_t)(copies[i].ak >> 24) == key) return copies[i].ak & 0xFFFFFFu;
		if (copies[i].p) continue;
		uint32_t a = 0;
		if (pitch == w) {                         /* (a picture from a pack: rows packed) */
			const SpriteArg arg = { pixels, pitch, key };
			a = cfxoj_data(cfxoj_hash(pack_hash(pixels, w, h), 0x53000000u | key), w, h, make_sprite, &arg);
		}
		copies[i].p = pixels; copies[i].ak = (a & 0xFFFFFFu) | (uint32_t)key << 24; cfxoj_n_copies++;
		return a;
	}
	return 0;
}

static void make_fill(uint8_t *out, int w, int h, const void *arg)
{
	memset(out, *static_cast<const uint8_t *>(arg), (size_t)w * (size_t)h);
}

static uint32_t flash_table(uint8_t c)
{
	return cfxoj_data(cfxoj_hash(0x464C4153u, c), 256, 256, make_fill, &c);   /* 'FLAS': 64 KB of c */
}

void cfxoj_bake_tables(void)
{
	flash_table(0);    /* the enemies' hit flash (JJ1StandardEvent, the guardians) */
	flash_table(36);   /* Jazz's when he is hurt (JJ1LevelPlayer::draw) */
}

/* ---- both ---- */
bool cfxoj_start(void)
{
#ifndef OJ32X_MARS
	static bool made;
	if (!made) {
		made = true;
		fx.psram = psram_byte; fx.sram = sram; fx.ctx = nullptr;
		fx.psram_t = 5;
		cfx_reset(&fx, CFX_KEY1_OJ, CFX_ID_OJ);
		for (uint32_t i = 0; i < (1u << 18); i++) sram[i] = (uint16_t)(i * 0x9E37u + 0x5A5Au);   /* not zeros */
	}
#endif
	(void)port(CFX_KEY0); (void)port(CFX_KEY1_OJ);
	const uint16_t st = port(CFX_KEY2);
	on = (st >> 8) == CFX_ID_OJ && port(8) == CFX_KEY2;
	if (on) {
		char msg[48];
		snprintf(msg, sizeof msg, "FX: JAZZ'S CORE (PSRAM READ %u)", port(9));
		plat_log(msg);
	}
	return on;
}

bool cfxoj_active(void) { return on; }

int cfxoj_suspend(void)
{
	if (!on) return 0;
	cfxoj_stop();
	return 1;
}

void cfxoj_resume(int was_on)
{
	if (!was_on) return;
	(void)port(CFX_KEY0); (void)port(CFX_KEY1_OJ);
	on = (port(CFX_KEY2) >> 8) == CFX_ID_OJ;
}

static void send_setup(void)
{
	cfxoj_send(CFX_WORD(CFX_OP_SETUP, 0));
	cfxoj_send(CFX_ARG(setup_cols)); cfxoj_send(CFX_ARG(setup_rows));
	cfxoj_send(CFX_HI(setup_blend)); cfxoj_send(CFX_LO(setup_blend));
	cfxoj_send(CFX_HI(0)); cfxoj_send(CFX_LO(0));
}

void cfxoj_frame(int cols, int rows)
{
	frame_flash = -1;
	if (cols != setup_cols || rows != setup_rows) { setup_cols = cols; setup_rows = rows; send_setup(); }
}

bool cfxoj_flash(uint8_t c)
{
	if (frame_flash >= 0) return frame_flash == c;
	const uint32_t t = flash_table(c);
	if (!t) return false;
	if (t != setup_blend) { setup_blend = t; send_setup(); }
	frame_flash = c;
	return true;
}

void cfxoj_stop(void)
{
	if (!on) return;
	cfxoj_wait_idle();
	(void)port(CFX_WORD(CFX_OP_OFF, 0));
	on = false;
}

void cfxoj_send(uint16_t w)
{
	uint16_t st = port(w);
	if (st & CFX_ST_FULL) {
		const uint32_t t0 = plat_ticks_ms();
		do { pause_a_little(); st = port(0); } while ((st & CFX_ST_FULL) && plat_ticks_ms() - t0 < 200);
	}
}

uint16_t cfxoj_counter(int n) { return port((uint16_t)n); }
uint16_t cfxoj_status(void) { return port(0); }

/* (the clock is read only once the engine is found busy: the PC build's clock advances a millisecond
   at every read, and the game's time must not depend on the engine) */
bool cfxoj_wait_idle(void)
{
	if (!(port(0) & CFX_ST_BUSY)) return true;
	const uint32_t t0 = plat_ticks_ms();
	while (port(0) & CFX_ST_BUSY) {
		pause_a_little();
		if (plat_ticks_ms() - t0 > 200) return false;
	}
	return true;
}

bool cfxoj_wait_shown(int b)
{
	if (((port(0) & CFX_ST_SHOWN) != 0) == (b != 0)) return true;
	const uint32_t t0 = plat_ticks_ms();
	while (((port(0) & CFX_ST_SHOWN) != 0) != (b != 0)) {
		pause_a_little();
		if (plat_ticks_ms() - t0 > 200) return false;
	}
	return true;
}

void cfxoj_cell_map(uint32_t rows[24])
{
	for (uint32_t r = 0; r < 24; r++)
		rows[r] = (uint32_t)pic_word(CFX_MAP_OFF + 4u * r) | (uint32_t)pic_word(CFX_MAP_OFF + 4u * r + 2u) << 16;
}
