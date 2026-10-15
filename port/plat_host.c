/*
 * Tyrian 32X - PC test platform (copied for OpenJazz CD32X from tyrian32x T3j).
 *
 * Runs the full game + port layer on a PC with no SDL at all:
 *   - ROM file system: loads the romfs image named by T32X_ROMFS
 *     (or the .32x ROM itself, with T32X_ROMFS_OFFSET set to its offset)
 *   - display: writes selected frames as PNG images (frames/frame_NNNNN.png,
 *     frames/last.png), composed exactly as the 32X shows them
 *     (320x224, 12-line borders, 15-bit colour)
 *   - pad: a script in T32X_PAD, e.g. "300:START,320:-,400:A" means
 *     "press START at frame 300, release all at 320, press A at 400"
 *   - time: a virtual clock that advances 1/60 s per presented frame and
 *     1 ms per idle poll, so runs are fast and repeatable
 *
 * Environment:
 *   T32X_ROMFS          path of the image (OpenJazz optional - the PC build
 *                       reads the game folder directly until the ROM file system exists)
 *   T32X_ROMFS_OFFSET   byte offset of the romfs inside that file (default 0)
 *   T32X_FRAMES_DIR     where to write frames (default "frames")
 *   T32X_SAVE_EVERY     save every Nth frame (default 30; 0 = none)
 *   T32X_MAX_FRAMES     stop after this many frames (default 1200)
 *   T32X_MAX_SECONDS    stop after this much game time (default 60; an idle
 *                       title screen draws no new frames, so this ends it)
 *   T32X_MOUSE          scripted Sega Mouse: "t12000:+20,-5,1;t12500:0,0,0" = at
 *                       12 s move by (+20, -5) (Y up = positive) with the left
 *                       button down; at 12.5 s nothing pressed (bits: 1 left,
 *                       2 right, 4 middle, 8 start). No variable: no mouse.
 *   T32X_LINK_OUT       named pipe this instance writes to, and
 *   T32X_LINK_IN        the one it reads from: the cable between two consoles
 *                       (start two instances with the pipes crossed)
 *   T32X_CD_DIR         folder: the Sega CD's data track (files found there by
 *                       name, case-insensitive), e.g. the Tyrian data folder
 *   T32X_AUDIO_WAV      file: the sound effects as the 32X mixes them (16-bit
 *                       mono, 22050 Hz), in step with the virtual clock
 *   T32X_RAW_INDEX      1 = save frames as colour indices (grey), ignoring the
 *                       palette: shows what is drawn even while faded to black
 *   T32X_PAD            pad script (see above); an entry starting with 't' counts
 *                       milliseconds of game time instead of frames, e.g.
 *                       "t9000:DOWN,t9100:-" (menus that wait for input draw no frames)
 */
#include "plat.h"
#include "pack.h"
#include "genbg.h"

#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

void *__real_malloc(size_t size);  /* the ROM image is not SDRAM: keep it out of the heap count */

static uint8_t *romfs_image;

/* Frame buffer emulation: fb_window is what the SH2 sees (the back buffer),
 * fb_other the buffer on screen. A flip swaps their contents, exactly like the
 * 32X swaps which physical buffer sits behind the SH2's window. */
#define FB_SIZE      0x20000
#define FB_DISP_OFF  0x200
#define FB_SPARE_OFF 0x10000
static uint8_t fb_window[FB_SIZE], fb_other[FB_SIZE];
static uint32_t now_ms;
static uint32_t now_frac;  /* 1/3 ms, so frames are exactly 16.667 ms */
static unsigned long frame;
static unsigned long max_frames = 1200;
static unsigned long save_every = 30;
static uint32_t max_ms = 60000;
static const char *frames_dir = "frames";
static const char *pad_script;
static bool raw_index;

static unsigned long env_ul(const char *name, unsigned long dflt)
{
	const char *v = getenv(name);
	return v ? strtoul(v, NULL, 0) : dflt;
}

static void audio_start(void);  /* sound, below */
static void cart_load_pack(void);  /* below */
void plat_packs_ready(void);
void __real_free(void *p);

void plat_init(void)
{
	audio_start();
	const char *path = getenv("T32X_ROMFS");
	if (path == NULL)
		goto no_romfs;  /* OpenJazz uses no ROM file system (a Tyrian leftover) */

	{
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		plat_fatal("cannot open T32X_ROMFS");
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);

	unsigned long off = env_ul("T32X_ROMFS_OFFSET", 0);
	if ((long)off >= len)
		plat_fatal("T32X_ROMFS_OFFSET is past the end of the file");

	uint8_t *all = __real_malloc((size_t)len);
	if (all == NULL || fread(all, 1, (size_t)len, f) != (size_t)len)
		plat_fatal("cannot read T32X_ROMFS");
	fclose(f);
	romfs_image = all + off;
	}
no_romfs:
	cart_load_pack();

	max_frames = env_ul("T32X_MAX_FRAMES", max_frames);
	max_ms = (uint32_t)env_ul("T32X_MAX_SECONDS", max_ms / 1000) * 1000;
	save_every = env_ul("T32X_SAVE_EVERY", save_every);
	if (getenv("T32X_FRAMES_DIR"))
		frames_dir = getenv("T32X_FRAMES_DIR");
	pad_script = getenv("T32X_PAD");
	raw_index = env_ul("T32X_RAW_INDEX", 0) != 0;
}

/* T32X_REAL_TIME=1: the system clock instead of the virtual one - for two
 * instances talking over the link (network play), whose clocks must run at
 * the same speed; the virtual clock races ahead in busy waits, so the
 * network code's 16 s timeout expired at once. Not reproducible frame for
 * frame, so the regression tests keep the virtual clock. */
static int real_time = -1;
static struct timespec real_start;

static uint32_t real_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)((t.tv_sec - real_start.tv_sec) * 1000 + (t.tv_nsec - real_start.tv_nsec) / 1000000);
}

uint32_t plat_ticks_ms(void)
{
	if (real_time < 0)
	{
		real_time = getenv("T32X_REAL_TIME") != NULL;
		clock_gettime(CLOCK_MONOTONIC, &real_start);
	}
	if (real_time)
	{
		now_ms = real_ms();
		if (now_ms >= max_ms)
		{
			fprintf(stderr, "[host] reached %u s of real time (frame %lu), stopping\n", (unsigned)(max_ms / 1000), frame);
			exit(0);
		}
		usleep(200);  /* busy waits: do not spin the PC */
		return now_ms;
	}
	/* Each poll costs 1 ms of virtual time, so busy-wait loops terminate. */
	if (now_ms >= max_ms)
	{
		fprintf(stderr, "[host] reached %u s of game time (frame %lu), stopping\n", (unsigned)(max_ms / 1000), frame);
		exit(0);
	}
	return now_ms++;
}

static void advance_frame_time(void)
{
	if (real_time > 0)
		return;  /* the system clock runs by itself */
	static long frame_thirds = -1;   /* REV: T32X_FRAME_VBLANKS=n - frames as long as n vblanks (the console's 20 fps: 3) */
	if (frame_thirds < 0) { const char *e = getenv("T32X_FRAME_VBLANKS"); frame_thirds = 50L * (e && *e ? atol(e) : 1); }
	now_frac += frame_thirds;  /* 16 2/3 ms per vblank, in thirds */
	now_ms += now_frac / 3;
	now_frac %= 3;
}

static uint16_t parse_buttons(const char *s, const char *end)
{
	static const struct { const char *name; uint16_t bit; } names[] = {
		{ "UP", PAD_UP }, { "DOWN", PAD_DOWN }, { "LEFT", PAD_LEFT }, { "RIGHT", PAD_RIGHT },
		{ "A", PAD_A }, { "B", PAD_B }, { "C", PAD_C }, { "START", PAD_START },
		{ "X", PAD_X }, { "Y", PAD_Y }, { "Z", PAD_Z }, { "MODE", PAD_MODE },
	};
	uint16_t bits = 0;
	while (s < end)
	{
		const char *plus = memchr(s, '+', (size_t)(end - s));
		const char *tok_end = plus ? plus : end;
		for (size_t i = 0; i < sizeof names / sizeof *names; ++i)
		{
			size_t n = strlen(names[i].name);
			if ((size_t)(tok_end - s) == n && strncmp(s, names[i].name, n) == 0)
				bits |= names[i].bit;
		}
		s = plus ? plus + 1 : end;
	}
	return bits;
}

#ifdef T32X_PROFILE
void plat_prof_mark(int slot) { (void)slot; }  /* the PC build has no real time to measure */
#endif

size_t plat_heap_free(void)
{
	/* The PC has plenty; behave like a 32X with this much left at level start
	 * (T32X_HEAP_FREE, bytes) so the tile cache does the same as there. */
	return (size_t)env_ul("T32X_HEAP_FREE", 30 * 1024);
}

/* ---- sound: the same mixer as the 32X's slave SH2 (port/mixer.c) ---- */
static FILE *wav;
static uint32_t wav_samples;
static uint64_t audio_rendered;  /* output samples so far */

static void wav_finish(void)
{
	if (wav == NULL)
		return;
	/* sizes into the RIFF header */
	const uint32_t data_bytes = wav_samples * 2;
	fseek(wav, 4, SEEK_SET);
	uint32_t riff = 36 + data_bytes;
	fwrite(&riff, 4, 1, wav);
	fseek(wav, 40, SEEK_SET);
	fwrite(&data_bytes, 4, 1, wav);
	fclose(wav);
	fprintf(stderr, "[host] audio: %u samples (%.1f s) written\n", wav_samples, wav_samples / (double)MIXER_RATE);
}

static void audio_start(void)
{
	mixer_init();
	const char *path = getenv("T32X_AUDIO_WAV");
	if (path == NULL || (wav = fopen(path, "wb")) == NULL)
		return;
	/* 16-bit mono PCM header, sizes filled in at exit (little-endian host) */
	const uint32_t rate = MIXER_RATE, byte_rate = MIXER_RATE * 2, fmt_size = 16, zero = 0;
	const uint16_t pcm = 1, mono = 1, block = 2, bits = 16;
	fwrite("RIFF", 1, 4, wav); fwrite(&zero, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav);
	fwrite(&fmt_size, 4, 1, wav); fwrite(&pcm, 2, 1, wav); fwrite(&mono, 2, 1, wav);
	fwrite(&rate, 4, 1, wav); fwrite(&byte_rate, 4, 1, wav); fwrite(&block, 2, 1, wav); fwrite(&bits, 2, 1, wav);
	fwrite("data", 1, 4, wav); fwrite(&zero, 4, 1, wav);
	atexit(wav_finish);
}

/* mixes up to the virtual clock (called after every frame) */
static void audio_catch_up(uint64_t ms)
{
	const uint64_t target = ms * MIXER_RATE / 1000;
	int16_t buf[512];
	while (audio_rendered < target)
	{
		int n = (int)(target - audio_rendered < 512 ? target - audio_rendered : 512);
		mixer_render(buf, n);
		if (wav != NULL)
		{
			fwrite(buf, 2, (size_t)n, wav);
			wav_samples += (uint32_t)n;
		}
		audio_rendered += (uint64_t)n;
	}
}

/* ---- the Sega CD's data track: files in T32X_CD_DIR ---- */
/* "sectors" are just indices into this table (the 32X gets real ones) */
static char cd_paths[16][512];
static int cd_count;

bool plat_read_mouse(int *dx, int *dy, uint8_t *buttons)
{
	static const char *next;
	static uint8_t held;
	const char *script = getenv("T32X_MOUSE");
	if (script == NULL)
		return false;
	if (next == NULL)
		next = script;
	*dx = *dy = 0;
	/* apply every entry whose time has come */
	while (*next == 't')
	{
		char *end;
		const unsigned long at = strtoul(next + 1, &end, 10);
		if (now_ms < at || *end != ':')
			break;
		int x = 0, y = 0, b = 0;
		if (sscanf(end + 1, "%d,%d,%d", &x, &y, &b) != 3)
			break;
		*dx += x;
		*dy += y;
		held = (uint8_t)b;
		const char *semi = strchr(end, ';');
		next = semi ? semi + 1 : end + strlen(end);
	}
	*buttons = held;
	return true;
}

/* ---- the link cable between two consoles: two named pipes ---- */
static int link_out = -1, link_in = -1;

bool plat_net_setup(int type)
{
	(void)type;
	const char *out = getenv("T32X_LINK_OUT"), *in = getenv("T32X_LINK_IN");
	if (out == NULL || in == NULL)
		return false;
	/* read-write opens do not wait for the other instance */
	link_out = open(out, O_RDWR | O_NONBLOCK);
	link_in = open(in, O_RDWR | O_NONBLOCK);
	if (link_out < 0 || link_in < 0)
		return false;
	int flags = fcntl(link_out, F_GETFL);
	fcntl(link_out, F_SETFL, flags & ~O_NONBLOCK);  /* writes complete */
	fprintf(stderr, "[host] link: out %s, in %s\n", out, in);
	return true;
}

void plat_net_cleanup(void)
{
	if (link_out >= 0) close(link_out);
	if (link_in >= 0) close(link_in);
	link_out = link_in = -1;
}

bool plat_net_send(const uint8_t *bytes, unsigned int count)
{
	if (link_out < 0)
		return false;
	return write(link_out, bytes, count) == (ssize_t)count;
}

unsigned int plat_net_recv(uint8_t *bytes, unsigned int most)
{
	if (link_in < 0)
		return 0;
	const ssize_t n = read(link_in, bytes, most);
	return n > 0 ? (unsigned int)n : 0;
}

bool plat_cd_present(void)
{
	return getenv("T32X_CD_DIR") != NULL;
}

bool plat_cd_open(const char *name, uint32_t *sector, uint32_t *length)
{
	const char *dir = getenv("T32X_CD_DIR");
	if (dir == NULL || cd_count >= 16)
		return false;
	char lower[64];
	size_t i = 0;
	for (; name[i] && i < sizeof lower - 1; ++i)
		lower[i] = (char)tolower((unsigned char)name[i]);
	lower[i] = 0;
	snprintf(cd_paths[cd_count], sizeof cd_paths[0], "%s/%s", dir, lower);
	FILE *f = fopen(cd_paths[cd_count], "rb");
	if (f == NULL)
		return false;
	fseek(f, 0, SEEK_END);
	*length = (uint32_t)ftell(f);
	fclose(f);
	*sector = (uint32_t)(1000 * (cd_count + 1));
	++cd_count;
	fprintf(stderr, "[host] CD: open %s (frame %lu)\n", name, frame);  /* when the disc is used */
	return true;
}

bool plat_cd_read(uint32_t sector, uint32_t offset, uint32_t bytes, void *dst)
{
	const int k = (int)(sector / 1000) - 1;
	if (k < 0 || k >= cd_count || offset % 2048 != 0 || bytes > 4096)
		return false;
	FILE *f = fopen(cd_paths[k], "rb");
	if (f == NULL)
		return false;
	fseek(f, (long)offset, SEEK_SET);
	const size_t got = fread(dst, 1, bytes, f);
	fclose(f);
	return got == bytes;
}

void plat_music_play(unsigned int track)
{
	fprintf(stderr, "[host] music: CD track %u (frame %lu)\n", track, frame);
}

void plat_music_stop(void)
{
	fprintf(stderr, "[host] music: stop (frame %lu)\n", frame);
}

void plat_sound_command(const MixCmd *c)
{
	mixer_command(c);
}

void plat_dma_start(void *dst, const void *src, unsigned int bytes) { memcpy(dst, src, bytes); }
void plat_dma_wait(void) { }
bool plat_dma_ok(void) { return true; }

uint32_t plat_random_seed(void)
{
	/* fixed, so test runs are reproducible; T32X_SEED picks another */
	return (uint32_t)env_ul("T32X_SEED", 1);
}

uint16_t plat_read_pad(void)
{
	if (pad_script == NULL)
		return 0;

	/* The last entry whose frame has been reached decides the state. */
	uint16_t state = 0;
	const char *p = pad_script;
	while (*p)
	{
		bool by_time = (*p == 't');
		if (by_time)
			++p;
		char *colon;
		unsigned long at = strtoul(p, &colon, 10);
		if (*colon != ':')
			break;
		const char *val = colon + 1;
		const char *comma = strchr(val, ',');
		const char *end = comma ? comma : val + strlen(val);
		if (at <= (by_time ? now_ms : frame))
			state = parse_buttons(val, end);
		p = comma ? comma + 1 : end;
	}
	return state;
}

/* Minimal PNG writer: RGB, deflate "stored" blocks (no compression library needed). */
static uint32_t crc_table[256];

static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
	if (crc_table[1] == 0)
		for (uint32_t i = 0; i < 256; ++i)
		{
			uint32_t v = i;
			for (int k = 0; k < 8; ++k)
				v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
			crc_table[i] = v;
		}
	for (size_t i = 0; i < n; ++i)
		c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c;
}

static void put_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
	uint8_t hdr[8];
	put_be32(hdr, len);
	memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	if (len)
		fwrite(data, 1, len, f);
	uint32_t c = crc32_update(0xFFFFFFFFu, hdr + 4, 4);
	c = crc32_update(c, data, len) ^ 0xFFFFFFFFu;
	uint8_t crc[4];
	put_be32(crc, c);
	fwrite(crc, 1, 4, f);
}

/* ---- REV: a model of the 68000's Genesis plane (cd32x/subcpu/genbg_md.s) -------------------
 * Drawn straight from the level's OJGB file for the camera the SH2 hands over (the 68000's
 * window, scrolling and video memory are checked on their own: tools/genesis_test_rom.sh).
 * The saved frames show the two pictures together as the console mixes them: a 32X colour
 * with bit 15 is in front, one without it shows where the Genesis plane is see-through. */
static const uint8_t *hg_blob;
static uint8_t hg_map[16384];
static int hg_shown;
static bool hg_frame;                  /* this frame is the level's (with the plane) */
static uint16_t hg_cram[256], hg_c4, hg_c6;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

int plat_genbg(int op, uint32_t arg)
{
	switch (op) {
	case GENBG_LOAD: {
		const uint8_t *b = plat_cart_ptr(arg);
		if (memcmp(b, "OJGB", 4) != 0 || be16(b + 4) != 4) return 0;
		hg_blob = b;
		memcpy(hg_map, b + be32(b + 24), sizeof hg_map);
		hg_shown = 0;
		return 1;
	}
	case GENBG_START:
		if (!hg_blob) return 0;
		hg_shown = 1;
		return 1;
	case GENBG_OFF:
		hg_shown = 0;
		hg_blob = NULL;
		return 1;
	case GENBG_CELL:
		if (hg_blob) hg_map[(arg >> 16) & 0x3FFF] = (uint8_t)arg;
		return 1;
	}
	return 0;
}

/* the 64 Genesis colours now: cycles by the vblanks since the start, faded (COMM6) */
static void hg_colours(uint16_t out[64])
{
	const uint8_t *b = hg_blob;
	for (int i = 0; i < 64; i++) out[i] = be16(b + be32(b + 12) + i * 2);
	const uint8_t *c = b + be32(b + 32);
	int groups = be16(c); c += 2;
	const double vbl = now_ms / (1000.0 / 59.92);
	for (int g = 0; g < groups; g++) {
		const int amount = be16(c + 2);
		const int32_t step = (int32_t)be32(c + 4);
		const int n = be16(c + 8);
		const uint8_t *e = c + 10, *col = e + n * 4;
		long long pos = (long long)(vbl * (double)step);
		long long m = (long long)amount << 16;
		pos %= m; if (pos < 0) pos += m;
		const int P = (int)(pos >> 16);
		for (int k = 0; k < n; k++) {
			const int idx = be16(e + k * 4), addr = be16(e + k * 4 + 2);
			out[addr / 2] = be16(col + ((idx + P) % amount) * 2);
		}
		c = col + amount * 2;
	}
	static const uint8_t fade[16][8] = {
		{0,1,2,3,4,5,6,7},{0,1,2,3,3,4,5,6},{0,1,1,2,3,4,4,5},{0,1,1,2,2,3,3,4},
		{0,0,1,1,2,2,3,3},{0,0,1,1,1,1,2,2},{0,0,0,0,1,1,1,1},{0,0,0,0,0,0,0,0},
		{0,1,2,3,4,5,6,7},{1,2,3,4,4,5,6,7},{2,3,3,4,5,6,6,7},{3,4,4,5,5,6,6,7},
		{4,4,5,5,6,6,7,7},{5,5,6,6,6,6,7,7},{6,6,6,6,7,7,7,7},{7,7,7,7,7,7,7,7}};
	const int f = (hg_c6 >> 11) & 15;
	if (f & 7)
		for (int i = 0; i < 64; i++) {
			const uint16_t w = out[i];
			out[i] = (uint16_t)(fade[f][(w >> 9) & 7] << 9 | fade[f][(w >> 5) & 7] << 5 | fade[f][(w >> 1) & 7] << 1);
		}
}

/* the plane's pixel at world (wx, wy): palette * 16 + entry, 0 = see-through */
static int hg_pixel(int wx, int wy)
{
	const uint8_t *b = hg_blob;
	if (wx < 0 || wy < 0 || wx >= 8192 || wy >= 2048) return 63;      /* outside: black (B) */
	const int t = hg_map[(wy >> 5) * 256 + (wx >> 5)];
	if (t != 255 && t < be16(b + 10)) {
		const uint16_t word = be16(b + be32(b + 20) + t * 32 + ((wy >> 3) & 3) * 8 + ((wx >> 3) & 3) * 2);
		if (word) {
			int v = word & 0x7FF, pid;
			if (v < 1280) pid = v;
			else if (v >= 0xB000 / 32 && v < 0xB000 / 32 + 128) pid = 1280 + v - 0xB000 / 32;
			else if (v >= 0xD000 / 32 && v < 0xD000 / 32 + 96) pid = 1408 + v - 0xD000 / 32;
			else pid = 1504 + v - 0xF400 / 32;
			int px = wx & 7, py = wy & 7;
			if (word & 0x800) px = 7 - px;
			if (word & 0x1000) py = 7 - py;
			const uint8_t byte = b[be32(b + 16) + pid * 32 + py * 4 + px / 2];
			const int e = (px & 1) ? (byte & 15) : (byte >> 4);
			if (e) return ((word >> 13) & 3) * 16 + e;
		}
	}
	if (be16(b + 38)) return -1;   /* plane B is the parallax texture: hg_texture */
	const uint8_t blk = b[be32(b + 28) + (wy >> 5) * 32 + (wx >> 8)];
	if (blk & (0x80 >> ((wx >> 5) & 7))) return 63;
	return 0;
}

/* plane B's parallax texture at screen (sx, line s) for the camera, as gb_scroll in
 * cd32x/subcpu/genbg_md.s: palette * 16 + entry */
static int hg_texture(int camx, int camy, int sx, int s)
{
	const uint8_t *b = hg_blob;
	const int mode = be16(b + 38);
	int hb, vb;
	if (mode == 1) { hb = -(camx + ((8192 - camx) >> 3)); vb = camy - 12 + ((2048 - camy) >> 3); }
	else { hb = ((camx + camy) >> 1) - camx; vb = camy - 12; }
	const int px = (sx - hb) & 511, py = (s + vb) & 255;
	const uint16_t word = be16(b + 40 + (mode == 1 ? 0 : (((px >> 3) + (py >> 3)) & 3) * 2));
	const int v = word & 0x7FF;
	int pid;
	if (v < 1280) pid = v;
	else if (v >= 0xB000 / 32 && v < 0xB000 / 32 + 128) pid = 1280 + v - 0xB000 / 32;
	else if (v >= 0xD000 / 32 && v < 0xD000 / 32 + 96) pid = 1408 + v - 0xD000 / 32;
	else pid = 1504 + v - 0xF400 / 32;
	const uint8_t byte = b[be32(b + 16) + pid * 32 + (py & 7) * 4 + (px & 7) / 2];
	const int e = (px & 1) ? (byte & 15) : (byte >> 4);
	return e ? ((word >> 13) & 3) * 16 + e : 0;
}

static void save_frame(const uint8_t *pixels, int pitch, const uint16_t *pal)
{
	char name[512];
	if (frame == 99999)
		snprintf(name, sizeof name, "%s/last.png", frames_dir);
	else
		snprintf(name, sizeof name, "%s/frame_%05lu.png", frames_dir, frame);
	FILE *f = fopen(name, "wb");
	if (f == NULL)
		return;

	/* Raw scanlines (filter byte 0 + RGB), composed exactly as the 32X shows them:
	 * 320x224, 12-line borders in colour 0, 15-bit colour. */
	enum { W = PLAT_SCREEN_W, H = PLAT_SCREEN_H, ROW = 1 + W * 3 };
	static uint8_t raw[ROW * H];
	for (int y = 0; y < H; ++y)
	{
		uint8_t *r = raw + y * ROW;
		*r++ = 0;
		int gy = y - PLAT_TOP_BORDER;
		for (int x = 0; x < W; ++x)
		{
			uint8_t idx = (gy >= 0 && gy < PLAT_GAME_H) ? pixels[gy * pitch + x] : 0;
			if (raw_index)
			{
				*r++ = idx; *r++ = idx; *r++ = idx;
				continue;
			}
			uint16_t c = pal[idx];
			if (hg_frame && hg_blob && hg_shown && !(hg_c6 & GENBG_HIDE) && gy >= 0 && gy < PLAT_GAME_H &&
			    !(hg_cram[idx] & 0x8000)) {
				/* REV: a background colour - the Genesis plane in front where it is not see-through */
				static uint16_t gcol[64];
				if (x == 0 && y == PLAT_TOP_BORDER) hg_colours(gcol);
				int e = hg_pixel(hg_c4 + x, (hg_c6 & 0x7FF) + gy);
				if (e < 0) e = hg_texture(hg_c4, hg_c6 & 0x7FF, x, y);
				if (e) {
					const uint16_t w = gcol[e];
					*r++ = (uint8_t)(((w >> 1) & 7) * 255 / 7);
					*r++ = (uint8_t)(((w >> 5) & 7) * 255 / 7);
					*r++ = (uint8_t)(((w >> 9) & 7) * 255 / 7);
					continue;
				}
			}
			*r++ = (uint8_t)(((c >> 0) & 31) * 255 / 31);
			*r++ = (uint8_t)(((c >> 5) & 31) * 255 / 31);
			*r++ = (uint8_t)(((c >> 10) & 31) * 255 / 31);
		}
	}

	/* zlib stream: header, stored blocks of up to 65535 bytes, Adler-32. */
	static uint8_t z[2 + sizeof raw + (sizeof raw / 65535 + 1) * 5 + 4];
	size_t zn = 0;
	z[zn++] = 0x78; z[zn++] = 0x01;
	for (size_t off = 0; off < sizeof raw; )
	{
		size_t n = sizeof raw - off > 65535 ? 65535 : sizeof raw - off;
		z[zn++] = off + n == sizeof raw ? 1 : 0;
		z[zn++] = (uint8_t)n; z[zn++] = (uint8_t)(n >> 8);
		z[zn++] = (uint8_t)~n; z[zn++] = (uint8_t)(~n >> 8);
		memcpy(z + zn, raw + off, n);
		zn += n; off += n;
	}
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < sizeof raw; ++i) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
	put_be32(z + zn, (b << 16) | a); zn += 4;

	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);
	uint8_t ihdr[13];
	put_be32(ihdr, W); put_be32(ihdr + 4, H);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;  /* 8-bit RGB */
	png_chunk(f, "IHDR", ihdr, 13);
	png_chunk(f, "IDAT", z, (uint32_t)zn);
	png_chunk(f, "IEND", NULL, 0);
	fclose(f);
}

uint8_t *plat_fb_area(int area)
{
	return fb_window + (area == PLAT_FB_DISPLAY ? FB_DISP_OFF : FB_SPARE_OFF);
}

static void fb_flip(const uint8_t *pixels, int pitch, bool hud_only)
{
	/* like plat_mars.c: the picture (or only its status bar parts) goes into
	 * the back buffer, then the buffers swap */
	for (int y = 0; y < PLAT_GAME_H; ++y)
	{
		const int x0 = (hud_only && y < PLAT_PLAYFIELD_H) ? PLAT_PLAYFIELD_W : 0;
		if (hud_only && y < PLAT_PLAYFIELD_H)
			memmove(fb_window + FB_DISP_OFF + y * PLAT_SCREEN_W + x0, pixels + y * pitch + x0, PLAT_SCREEN_W - x0);
		else
			memmove(fb_window + FB_DISP_OFF + y * PLAT_SCREEN_W, pixels + y * pitch, PLAT_SCREEN_W);
	}
	static uint8_t tmp[FB_SIZE];
	memcpy(tmp, fb_window, FB_SIZE);
	memcpy(fb_window, fb_other, FB_SIZE);
	memcpy(fb_other, tmp, FB_SIZE);
}

static void present_common(const uint8_t *pixels, int pitch, const uint16_t *palette555, bool hud_only)
{
	fb_flip(pixels, pitch, hud_only);
	/* save what is now on screen */
	pixels = fb_other + FB_DISP_OFF;
	pitch = PLAT_SCREEN_W;

	if (save_every != 0 && frame % save_every == 0)
		save_frame(pixels, pitch, palette555);

	/* Always keep the latest frame too, for "where did it stop?". */
	unsigned long saved = frame;
	frame = 99999;
	save_frame(pixels, pitch, palette555);
	frame = saved;

	advance_frame_time();
	audio_catch_up(now_ms);
	++frame;
	if (frame >= max_frames)
	{
		fprintf(stderr, "[host] reached %lu frames, stopping\n", frame);
		exit(0);
	}
}

void plat_present(const uint8_t *pixels, int pitch, const uint16_t *palette555)
{
	if (genbg_active()) genbg_plain_screen();   /* REV: not the level: the Genesis plane hidden */
	hg_frame = false;
	present_common(pixels, pitch, palette555, false);
}

void plat_present_hud(const uint8_t *pixels, int pitch, const uint16_t *palette555)
{
	present_common(pixels, pitch, palette555, true);
}

/* OpenJazz the cart data area, read-only except during plat_cart_write */
static uint8_t *cart_mem;
static uint8_t cart_low[PACK_GENESIS_MAX];   /* REV: cart 0x080000-0x100000 (the Genesis files) */
static const uint32_t cart_len = PLAT_CART_DATA_END - PLAT_CART_DATA_OFFSET;

static uint32_t cart_pack_len;   /* a pack (T32X_PACK) sits at the start of the data area */

static void cart_map(void)
{
	if (cart_mem == NULL)
	{
		void *m = mmap(NULL, cart_len, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (m == MAP_FAILED)
			plat_fatal("cannot map the cart");
		cart_mem = m;
	}
}

uint8_t *plat_cart_data(uint32_t *size)
{
	cart_map();
	cart_load_pack();   /* the pack first - the allocator gets the space after it */
	/* the allocator stays below the world / bonus pack area (PACK_LEVEL_CART) */
	*size = (PACK_LEVEL_CART - PLAT_CART_DATA_OFFSET) - cart_pack_len;
	return cart_mem + cart_pack_len;
}

const uint8_t *plat_cart_ptr(uint32_t cart_offset)
{
	cart_map();
	if (cart_offset < PLAT_CART_DATA_OFFSET) return cart_low + (cart_offset - PACK_GENESIS_CART);
	return cart_mem + (cart_offset - PLAT_CART_DATA_OFFSET);
}

/* REV: the cart offset of an address in the cart (the Genesis plane's file, port/genbg.c) */
uint32_t plat_cart_offset_of(const void *p)
{
	cart_map();
	const uint8_t *b = (const uint8_t *)p;
	if (b >= cart_low && b < cart_low + sizeof cart_low) return (uint32_t)(b - cart_low) + PACK_GENESIS_CART;
	return (uint32_t)(b - cart_mem) + PLAT_CART_DATA_OFFSET;
}


int32_t plat_load_pack(int id, uint32_t cart_offset, uint32_t max_bytes)
{
	const char *dir = getenv("T32X_PACK_DIR");
	if (!dir || !*dir)
		return -1;
	char path[1024];
	if (id >= PACK_GENESIS_ID0)   /* REV: a world's Genesis plane files */
		snprintf(path, sizeof path, "%s/GB%03d.PAK", dir, id - PACK_GENESIS_ID0);
	else
		snprintf(path, sizeof path, "%s/OJ%03d.PAK", dir, id - PACK_LEVEL_ID0);
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0 || (uint32_t)len > max_bytes)
	{
		fclose(f);
		fprintf(stderr, "[host] %s: %ld bytes, more than the %u the cart has for it\n", path, len, max_bytes);
		return -2;
	}
	uint8_t *tmp = __real_malloc((size_t)len);
	if (fread(tmp, 1, (size_t)len, f) != (size_t)len)
		plat_fatal("cannot read a level pack");
	fclose(f);
	cart_map();
	if (cart_offset < PLAT_CART_DATA_OFFSET)   /* REV: below the data area (the Genesis files) */
		memcpy(cart_low + (cart_offset - PACK_GENESIS_CART), tmp, (size_t)len);
	else
		plat_cart_write(cart_offset - PLAT_CART_DATA_OFFSET, tmp, (size_t)len);
	__real_free(tmp);
	fprintf(stderr, "[host] level pack %s: %ld bytes\n", path, len);
	return (int32_t)len;
}

/* T32X_PACK=<file>: the pack goes into the cart (like command 46 on the 32X) and is
 * mounted; the cart allocator gets the space after it */
/* game files can be opened before plat_init (configuration, PANEL.000): the pack is
 * mounted at the first look-up */
void plat_packs_ready(void)
{
	cart_load_pack();
}

static void cart_load_pack(void)
{
	static int done;
	if (done)
		return;
	done = 1;
	const char *path = getenv("T32X_PACK");
	if (!path || !*path)
		return;
	FILE *f = fopen(path, "rb");
	if (!f)
		plat_fatal("cannot open T32X_PACK");
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0 || (uint32_t)len > cart_len)
		plat_fatal("T32X_PACK: empty or bigger than the cart data area");
	uint8_t *tmp = __real_malloc((size_t)len);
	if (fread(tmp, 1, (size_t)len, f) != (size_t)len)
		plat_fatal("cannot read T32X_PACK");
	fclose(f);
	cart_map();
	plat_cart_write(0, tmp, (size_t)len);
	__real_free(tmp);
	if (!pack_mount(cart_mem))
		plat_fatal("T32X_PACK is not a pack");
	cart_pack_len = ((uint32_t)len + 0xFFF) & ~0xFFFu;
	fprintf(stderr, "[host] pack %s mounted: %ld bytes\n", path, len);
}

void plat_cart_write(uint32_t offset, const void *src, size_t bytes)
{
	if (offset + bytes > cart_len)
		plat_fatal("plat_cart_write: past the end of the cart");
	uintptr_t page = 4096;
	uintptr_t a = ((uintptr_t)cart_mem + offset) & ~(page - 1);
	uintptr_t e = ((uintptr_t)cart_mem + offset + bytes + page - 1) & ~(page - 1);
	mprotect((void *)a, e - a, PROT_READ | PROT_WRITE);
	memcpy(cart_mem + offset, src, bytes);
	mprotect((void *)a, e - a, PROT_READ);
}

const uint8_t *plat_romfs_base(void)
{
	return romfs_image;
}

void plat_log(const char *msg)
{
	fprintf(stderr, "[log] %s", msg);
	size_t n = strlen(msg);
	if (n == 0 || msg[n - 1] != '\n')
		fputc('\n', stderr);
}

void plat_fatal(const char *msg)
{
	fprintf(stderr, "[FATAL] %s\n", msg);
	exit(1);
}

void plat_exit(int code)
{
	exit(code);
}

/* ------------------------------------------------------------------------
 * Heap accounting (host only): the Makefile wraps malloc & co. so we can
 * report the peak heap the game needed, to compare with the 32X budget.
 * Each block costs its size plus 8 bytes, roughly newlib's overhead on the SH2.
 */
#include <stdalign.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *p, size_t size);
void __real_free(void *p);

typedef struct { size_t size; size_t pad; } HeapHeader;  /* keeps 16-byte alignment */
static size_t heap_now, heap_peak, heap_blocks;

/* OpenJazz T32X_HEAP_LOG=<bytes> - remember who allocated every live block of at least
 * that size (6 return addresses), and print the list as it stood at the heap's peak when the
 * program ends; tools/heap_peak.sh turns the addresses into function names. */
#include <execinfo.h>
#define HEAP_TRACK 1024
#define HEAP_DEPTH 8
typedef struct { void *p; size_t size; void *bt[HEAP_DEPTH]; int n; } BigBlock;
static BigBlock big_live[HEAP_TRACK], big_peak[HEAP_TRACK];
static int big_count, big_peak_count;
static long heap_log_min = -1;  /* -1: not read yet, 0: off */
static size_t big_peak_total;

static void heap_add(long delta)
{
	heap_now += delta;
	if (heap_now > heap_peak)
	{
		heap_peak = heap_now;
		if (heap_log_min > 0)
		{
			memcpy(big_peak, big_live, sizeof big_live[0] * big_count);
			big_peak_count = big_count;
		}
	}
}

static void big_add(void *p, size_t size)
{
	if (heap_log_min < 0)
	{
		const char *v = getenv("T32X_HEAP_LOG");
		heap_log_min = v ? atol(v) : 0;
	}
	if (heap_log_min <= 0 || (long)size < heap_log_min || big_count >= HEAP_TRACK)
		return;
	BigBlock *b = &big_live[big_count++];
	b->p = p;
	b->size = size;
	b->n = backtrace(b->bt, HEAP_DEPTH);
}

static void big_remove(void *p)
{
	for (int i = 0; i < big_count; ++i)
		if (big_live[i].p == p)
		{
			big_live[i] = big_live[--big_count];
			return;
		}
}

static int big_cmp(const void *a, const void *b)
{
	size_t x = ((const BigBlock *)a)->size, y = ((const BigBlock *)b)->size;
	return x < y ? 1 : x > y ? -1 : 0;
}

static void big_print(BigBlock *list, int count, size_t total_heap, const char *what)
{
	qsort(list, count, sizeof list[0], big_cmp);
	size_t total = 0;
	for (int i = 0; i < count; ++i)
		total += list[i].size + 8;
	fprintf(stderr, "[heap-peak] %zu bytes at %s; %d blocks >= %ld bytes hold %zu, smaller blocks %zu\n",
	        total_heap, what, count, heap_log_min, total, total_heap - total);
	for (int i = 0; i < count; ++i)
	{
		fprintf(stderr, "[heap-peak] %8zu", list[i].size);
		for (int j = 1; j < list[i].n; ++j)
			fprintf(stderr, " %p", list[i].bt[j]);
		fputc('\n', stderr);
	}
}

static void big_report(void)
{
	if (heap_log_min <= 0)
		return;
	big_print(big_peak, big_peak_count, heap_peak, "the peak");
}

void *__wrap_malloc(size_t size)
{
	HeapHeader *h = __real_malloc(sizeof *h + size);
	if (h == NULL)
		return NULL;
	h->size = size;
	big_add(h + 1, size);
	heap_add((long)size + 8);
	heap_blocks++;
	return h + 1;
}

void *__wrap_calloc(size_t n, size_t size)
{
	void *p = __wrap_malloc(n * size);
	if (p != NULL)
		memset(p, 0, n * size);
	return p;
}

void __wrap_free(void *p)
{
	if (p == NULL)
		return;
	HeapHeader *h = (HeapHeader *)p - 1;
	if (heap_log_min > 0)
		big_remove(p);
	heap_add(-((long)h->size + 8));
	heap_blocks--;
	__real_free(h);
}

void *__wrap_realloc(void *p, size_t size)
{
	if (p == NULL)
		return __wrap_malloc(size);
	HeapHeader *h = (HeapHeader *)p - 1;
	void *q = __wrap_malloc(size);
	if (q != NULL)
	{
		memcpy(q, p, h->size < size ? h->size : size);
		__wrap_free(p);
	}
	return q;
}

static void big_print(BigBlock *list, int count, size_t total_heap, const char *what);

void plat_mem_mark(const char *label)
{
	/* T32X_HEAP_SNAP=<text>: at the first mark whose label contains it, list the live
	 * blocks (needs T32X_HEAP_LOG) instead of the ones at the peak */
	static int snapped;
	const char *snap = getenv("T32X_HEAP_SNAP");
	if (snap && *snap && !snapped && heap_log_min > 0 && strstr(label, snap))
	{
		snapped = 1;
		BigBlock tmp[HEAP_TRACK];
		memcpy(tmp, big_live, sizeof big_live[0] * big_count);
		big_print(tmp, big_count, heap_now, label);
		heap_log_min = 0;  /* no peak list at the end */
	}
	fprintf(stderr, "[host] heap at '%s' (frame %lu, %u ms): now %zu bytes, peak so far %zu bytes\n",
	        label, frame, (unsigned)now_ms, heap_now, heap_peak);
}

static void heap_report(void)
{
	big_report();
	fprintf(stderr, "[host] heap: peak %zu bytes, now %zu bytes in %zu blocks\n",
	        heap_peak, heap_now, heap_blocks);
}

__attribute__((constructor)) static void heap_report_init(void)
{
	atexit(heap_report);
}

/* the PC build keeps the screen in memory (plat_present) */
uint8_t *plat_canvas(void) { return NULL; }
void plat_present_canvas(const uint16_t *palette555) { (void)palette555; }

void plat_parallel(void (*fn)(void *, int), void *arg)
{
	fn(arg, 0);
	fn(arg, 1);
}

void plat_sfx(int id) { (void)id; }
void plat_music(int track) { (void)track; }
void plat_border_colour(int index) { (void)index; }
void plat_clear_screens(void) { }

#include <time.h>
uint32_t plat_prof_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000000u + t.tv_nsec / 1000u);
}
uint32_t plat_frame_ms(void) { return plat_ticks_ms(); }
void plat_logic_parallel(void (*fn)(void *, int), void *arg) { fn(arg, 0); fn(arg, 1); }
void plat_slave_logic(int on) { (void)on; }
int plat_slave_logic_on(void) { const char *e = getenv("T32X_SLAVE_LOGIC"); return e && *e == '1'; }   /* the 32X's order of logic and drawing (J23) */
void plat_slave_draws(int on) { (void)on; }
int plat_slave_draws_on(void) { return 0; }

/* the scrolling renderer on the PC - two imitated 32X frame buffers; presenting builds the
 * 320x200 picture from the line-table rules, exactly as the 32X shows it, then the normal present */
static uint8_t host_fb[2][0x20000];
static int host_back;
uint8_t *plat_scroll_ring(void)  { return host_fb[host_back] + 0x200; }
uint8_t *plat_scroll_panel(void) { return host_fb[host_back] + 0x12E00; }
int plat_back_index(void) { return host_back; }
void plat_present_scroll(const uint16_t *palette555, int row0, int xoff, int viewH)
{
	static uint8_t img[320 * 200];
	for (int g = 0; g < 200; g++)
	{
		const uint8_t *src;
		if (g < viewH) src = host_fb[host_back] + 0x200 + ((row0 + g) % 200) * 384 + xoff;
		else src = host_fb[host_back] + 0x12E00 + (g - viewH) * 320;
		memcpy(img + g * 320, src, 320);
	}
	/* REV: the Genesis plane: the CRAM with bit 15 and the camera, as plat_mars.c */
	hg_frame = genbg_active();
	if (hg_frame) { genbg_cram(palette555, hg_cram); hg_c4 = genbg_comm4(); hg_c6 = genbg_comm6(); }
	present_common(img, 320, palette555, false);
	hg_frame = false;
	host_back ^= 1;
}
void plat_wait_flip(void) { }
static int host_game = -1;
int plat_game(void) { if (host_game < 0) { const char *e = getenv("T32X_HH95"); host_game = e && *e == '1'; } return host_game; }
void plat_set_game(int g) { host_game = g; }
int plat_hh95_available(void) { return 1; }
static int host_port_set[8] = { 1 };
int plat_port_setting(int key) { return key >= 0 && key < 8 ? host_port_set[key] : 0; }
void plat_port_setting_set(int key, int value) { if (key >= 0 && key < 8) host_port_set[key] = value; }
