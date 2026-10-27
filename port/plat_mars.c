/*
 * OpenJazz CD32X - platform layer for the Sega CD + 32X (master SH2).
 *
 * What is here:
 *   - start-up: C++ runtime set-up, the boot checks, the common data pack into the cart
 *     (with the quick check that skips it when the cart already holds it)
 *   - commands to the Sub-CPU through the 32X COMM registers (OpenLara's Sub-CPU program and
 *     68000 listener, cd32x/subcpu): files and packs (38/46/47), pad and sound effects (48),
 *     loading screen (51), Genesis display (52), PCM pack (53), CD audio (45/54), saves (58/61)
 *   - the display: presenting frames, the scrolling renderer's line table, palette, buffer
 *     swaps (synchronous or OJ32X_FLIP_ASYNC), frame pacing
 *   - world and bonus packs with the loading screen; the save area in the RAM cart
 *   - clocks, the profiler clock, error and exit screens, the optional slave SH2 job
 *
 * Derived from the Tyrian 32X port's plat_mars.c. Differences that matter:
 *   - No 0x0AAA/0x0BBB start-up handshake: OpenLara's loader (cdloader_sh2.c) did it with
 *     the Sub-CPU before it started this program; it is not repeated.
 *   - The SH2 never writes the cart: plat_cart_write stops with a red screen. Everything in
 *     the cart comes from the disc (commands 38 + 46), written by the 68000.
 *   - C++: the global constructors are run and .eh_frame is registered with the unwinder
 *     before main() (Tyrian is C and needed neither).
 */
#include "plat.h"
#include "cart.h"
#include "pack.h"
#include "genbg.h"

#include <malloc.h>
#include <stdint.h>
#include <string.h>

#include "mars/font3x5.h"

static int loading_up;     /* the loading screen is on: a second pack of the same load keeps it */
static int bmd_hole = -1;  /* R9: the bonus floor's 32X palette index (-1: no floor) */
static uint16_t bmd_hole555;
static int bmd_show = -1;  /* R9: the floor bit the next swap shows (it changes with each new floor) */
static int fill_running;   /* FILL: the boot's copying shows the splash, not the loading screen */

/* ------------------------------------------------------------ registers */
#define MARS_ADAPTER_B   (*(volatile uint8_t  *)0x20004000)
#define MARS_INTMSK      (*(volatile uint16_t *)0x20004000)
#define MARS_CMD_CLR     (*(volatile uint16_t *)0x2000401A)
#define MARS_COMM0       (*(volatile uint16_t *)0x20004020)
#define MARS_COMM2       (*(volatile uint16_t *)0x20004022)
#define MARS_COMM4       (*(volatile uint16_t *)0x20004024)   /* REV: camera x for the 68000's Genesis plane */
#define MARS_COMM6       (*(volatile uint16_t *)0x20004026)   /* REV: camera y, fade, hide (port/genbg.h) */
/* R7: COMM8 / COMM10 carry the answer to a pad request sent ahead (pad_ask): every other use
 * waits for that answer first, so it cannot land on an argument written here or be read as a
 * result (the RAW_ names are for the pad's own answer) */
#define RAW_COMM8        (*(volatile uint16_t *)0x20004028)
static void pad_collect(uint32_t tries);
static inline volatile uint16_t *comm16_settled(uintptr_t a) { pad_collect(40000UL); return (volatile uint16_t *)a; }
static inline volatile uint32_t *comm32_settled(uintptr_t a) { pad_collect(40000UL); return (volatile uint32_t *)a; }
#define MARS_COMM8       (*comm16_settled(0x20004028))
#define MARS_COMM10      (*comm16_settled(0x2000402A))
#define MARS_COMM12      (*(volatile uint16_t *)0x2000402C)
#define VDP_DISPMODE     (*(volatile uint16_t *)0x20004100)
#define VDP_FBCTL        (*(volatile uint16_t *)0x2000410A)
#define VDP_CRAM         ((volatile uint16_t *)0x20004200)
#define FB16             ((volatile uint16_t *)0x24000000)
#define FB32             ((volatile uint32_t *)0x24000000)
#define CART_BYTES       ((const volatile uint8_t *)0x22000000)   /* cart, cache-through */

#define FBCTL_VBLK       0x8000
#define FBCTL_PEN        0x2000
#define FBCTL_FS         0x0001
#define DISP_MODE_256    0x0001
#define DISP_PRI_TEXT    0x0080

#define SH2_WTCSR_TCNT   (*(volatile uint16_t *)0xFFFFFE80)
#define SH2_WTCNT_R      (*(volatile uint8_t  *)0xFFFFFE81)
#define SH2_IPRA         (*(volatile uint16_t *)0xFFFFFEE2)
#define SH2_VCRWDT       (*(volatile uint16_t *)0xFFFFFEE4)

/* DIAGNOSTICS SWITCH (with OJ_DIAG_TEXT in cd32x/sh2/cdloader_sh2.c and KOBO_DIAG_TEXT /
 * KOBO_BOOT_TEXT in cd32x/subcpu/main.c): 1 = the Genesis text the loader switched on stays
 * until C++ start-up has worked (a crash before that still shows the Sub-CPU's last STATE);
 * 0 = text off at once. Either way it goes off for the engine's own screens (CMD 52). */
#ifndef OJ32X_DIAG_TEXT
#define OJ32X_DIAG_TEXT 0
#endif

/* SPLASH SWITCH (like Kobo's KOBO_SPLASH / SPLASH_HOLD): 1 = the
 * loader's splash (picture + full bar, in both frame buffers) stays on screen when the engine
 * starts, through C++ start-up, for at least OJ32X_SPLASH_HOLD frames; 0 = the engine clears
 * the screen at once (Tyrian's start-up). */
#ifndef OJ32X_SPLASH
#define OJ32X_SPLASH 1
#endif
#define OJ32X_SPLASH_HOLD 120   /* frames at 60 Hz = 2 s */

/* DIRECT FRAME BUFFER SWITCH: 1 = the game draws its screen straight into the 32X frame
 * buffer (no 64 KB screen in SDRAM, no copy each frame); 0 = SDRAM screen + copy. */
/* SLAVE SH2 SWITCH: 1 = the slave SH2 draws half of the work given to plat_parallel (the
 * original renderer's tile bands, the credits' plasma) while the master draws the other
 * half; 0 = the master draws both. Off: measured on the console, two SH2s gave no gain - both
 * wait on the same bus to the frame buffer and the cart, and the master has priority. */
/* FRAME PACING SWITCH: 1 = each frame is held to the median of the recent frame times
 * (even motion), 0 = each frame is shown at the first vblank after it is finished (choppier
 * at speed). */
#ifndef OJ32X_PACE
#define OJ32X_PACE 1
#endif

#ifndef OJ32X_SLAVE_DRAWS
#define OJ32X_SLAVE_DRAWS 0
#endif

#ifndef OJ32X_DIRECT_FB
#define OJ32X_DIRECT_FB 1
#endif

/* BOOT TEST SWITCH: 1 = the boot test screen is shown and waits for Start;
 * 0 = the checks still run, but the screen only appears (and waits) when one FAILS -
 * otherwise the game starts straight after the splash. */
#ifndef OJ32X_BOOT_TEST
#define OJ32X_BOOT_TEST 0
#endif

#define CMD_GET_PAD      48
#define CMD_OPEN_FILE    38      /* main.c: COMM2 = file number (n < 100: IMAGE<n>.RAW for cart slot n; 200+: OJ<n-200>.PAK); length -> COMM8 (32 bit) */
#define CMD_CHUNK_TO_CART 46     /* main.c: COMM2 = 32 KB chunk (bit 15: no read-back); reply COMM12 = bad */
#define CMD_CHUNK_COMPARE 47     /* main.c: the same, but only compared with the cart (the quick check) */
#define PACK_SLOT        8       /* IMAGE8.RAW -> cart 8 * 128 KB = 0x100000 = PLAT_CART_DATA_OFFSET */
#define CMD_CHUNK_TO_FB  51      /* main.c: file chunk -> frame buffer (the loader's splash) */
#define CMD_LOAD_SFX     53      /* main.c: SFX<COMM2>.BIN -> PCM chip; COMM12 = number of effects */
#define MARS_COMM8_32    (*comm32_settled(0x20004028))
#define CMD_TEXT_ONOFF   52

#define PPHI_HZ          23011364u

/* Frame buffer layout (byte offsets) in each 128 KB buffer, as Tyrian:
 *   0x00000 line table, 0x00200 200 picture lines, 0x0FE00 blank line, 0x10000 spare */
#define FB_PIX_WORD      0x100
#define FB_LINE_WORDS    (PLAT_SCREEN_W / 2)
#define FB_BLANK_LINE    PLAT_GAME_H

/* ---------------------------------------------- symbols for crt0.s */
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
static volatile uint32_t vblank_count = 0;

static volatile uint8_t vblank_wtcnt;   /* the watchdog counter at the last vblank */
/* OJ32X_FLIP_ASYNC: a requested swap, and the palette and shift register value that go with
 * it - set by the vblank handler at the moment of the swap, so picture and colours change
 * together */
static volatile int flip_pending, pal_pending;
static volatile uint16_t flip_target;
static uint16_t pending_pal[256];
static volatile uint16_t pending_shift;
#define VDP_SHIFT_REG (*(volatile uint16_t *)0x20004102)
static void load_pending_palette(void)
{
	pal_pending = 0;
	VDP_SHIFT_REG = pending_shift;
	for (int i = 0; i < 256; ++i) VDP_CRAM[i] = pending_pal[i];
}
void pri_vbi_handler(void)
{
	vblank_count++;
	vblank_wtcnt = SH2_WTCNT_R;
	/* the swap happens at the vblank: its palette goes in now, before the picture is shown */
	if (flip_pending && pal_pending && (VDP_FBCTL & FBCTL_FS) == flip_target) load_pending_palette();
}
void pri_cmd_handler(void) { MARS_CMD_CLR = 0; }
void sec_cmd_handler(void) { MARS_CMD_CLR = 0; }
void sec_dma1_handler(void) { }

/* OJ32X_SLAVE_DRAWS: the slave SH2 runs half of a plat_parallel job. The job lives in SDRAM
 * and both SH2s use it through the cache-through address (0x26xxxxxx), so neither sees a stale
 * copy; the slave purges its cache before each job, so the level data it reads is current
 * (the SH2 caches write through: the master's writes are already in SDRAM). */
typedef struct { void (*fn)(void *, int); void *arg; int go; int done; int park, parked; } SlaveJob;   /* ED: park (EDSAVE-4) */
/* the slave's part can be switched off while playing (Mode button) to compare one and two
 * SH2s with the profiler */
static int slave_draws_now = 1;
void plat_slave_draws(int on) { slave_draws_now = on; }
int plat_slave_draws_on(void) { return OJ32X_SLAVE_DRAWS && slave_draws_now; }
static SlaveJob slave_job;
#define JOB ((volatile SlaveJob *)((uintptr_t)&slave_job | 0x20000000u))

/* R7: a wait that stays off the bus. The two SH2s share one bus (SDRAM, cart, frame buffer,
 * 32X registers): an SH2 spinning on a cache-through address or a 32X register keeps the bus
 * busy almost all the time, and every cart, SDRAM and frame buffer access of the other SH2
 * waits for it. Between two looks at what it waits for, a busy-wait now runs this loop: a
 * few registers and the cache, nothing on the bus (about 3 clocks a turn). */
static inline void bus_rest(int turns)
{
	for (int k = turns; k > 0; k--) __asm__ __volatile__("" ::: "memory");
}

/* ED / EDSAVE-4 (OpenLara CD32X on the Mega EverDrive PRO, 26 Oct 2026; CD32X BootCamp docs/12): on the
 * EverDrive an SH2's cart access while the 68000 holds RV = 1 is NOT held until RV = 0 - it is
 * answered at once with garbage (edsave3: ~100% bad words through every such copy).  So for every
 * command whose 68000 side copies with RV = 1 (46/47 chunks, 68 the page, 69 unpack, 61 the save
 * area, 67/71 the Genesis plane and the bonus floor) both SH2s wait in this loop, which lives in
 * SDRAM (.data.sdram), with their interrupts masked: nothing fetches or reads the cart until the
 * Sub-CPU reports the copy done.  A cached loop is luck; in a game the cache is cold. */
__attribute__((section(".data.sdram"), noinline))
static void sdram_wait(volatile uint16_t *reg16, volatile int *flag, volatile int *ack)
{	/* reg16: spin while *reg16 != 0 (the master, COMM0); flag: spin while *flag != 0 (the slave,
	   JOB->park), *ack = 1 first.  No calls, no cart data: this function and its stack only. */
	uint32_t sr, masked;
	__asm__ volatile ("stc sr,%0" : "=r"(sr));
	masked = sr | 0xF0u;
	__asm__ volatile ("ldc %0,sr" : : "r"(masked));
	if (ack) *ack = 1;
	for (int32_t n = 0; n < 4000000; n++) {          /* OpenLara's csSpin bound, to the number */
		if (reg16 ? *reg16 == 0 : *flag == 0) break;
		for (volatile int32_t k = 0; k < 64; k++) {}    /* never hammer a COMM register */
	}
	__asm__ volatile ("ldc %0,sr" : : "r"(sr));
}

void secondary(void)
{
	for (;;)
	{
		while (!JOB->go) {               /* R7: was a tight loop on SDRAM: the master's bus */
			if (JOB->park) sdram_wait(NULL, &JOB->park, &JOB->parked);   /* ED: off the cart */
			bus_rest(64);
		}
		JOB->go = 0;
		*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge + enable the slave's cache */
		JOB->fn(JOB->arg, 1);
		JOB->done = 1;
	}
}

/* J23 - OJ32X_SLAVE_LOGIC: the slave SH2 runs the game logic (the level's steps) while the
 * master brings the screen's background up to date (as the Hurrican CD32X port, which gained
 * most of its frame rate this way). The logic hardly touches the frame buffer, so the two do
 * not fight over that bus as the two drawing SH2s did (OJ32X_SLAVE_DRAWS). The slave runs it
 * on a stack of its own (8 KB: its start-up stack is 2 KB), and the master purges its cache
 * after the join, so it sees what the slave wrote (the SH2 caches write through, but the
 * master may still hold old copies). Mode switches it while playing (frame counter: 2CPU /
 * 1CPU). */
#ifndef OJ32X_SLAVE_LOGIC
#define OJ32X_SLAVE_LOGIC 1
#endif
static int slave_logic_now = 1;
void plat_slave_logic(int on) { slave_logic_now = on; }
int plat_slave_logic_on(void) { return OJ32X_SLAVE_LOGIC && slave_logic_now; }
static uint32_t slave_stack[2048] __attribute__((aligned(16)));   /* 8 KB */
static void (*volatile logic_fn)(void *, int);
static void *volatile logic_arg;
void call_on_stack(void (*fn)(void), void *top);
__asm__(
	"	.text\n"
	"	.align 2\n"
	"	.global _call_on_stack\n"
	"_call_on_stack:\n"           /* r4 = fn, r5 = the new stack's top */
	"	mov.l	r14,@-r15\n"
	"	sts.l	pr,@-r15\n"
	"	mov	r15,r14\n"
	"	mov	r5,r15\n"
	"	jsr	@r4\n"
	"	nop\n"
	"	mov	r14,r15\n"
	"	lds.l	@r15+,pr\n"
	"	rts\n"
	"	mov.l	@r15+,r14\n");
static void logic_on_slave(void) { logic_fn(logic_arg, 1); }
static void logic_trampoline(void *arg, int part)
{
	(void)arg; (void)part;
	call_on_stack(logic_on_slave, slave_stack + 2048);
}

void plat_logic_parallel(void (*fn)(void *, int), void *arg)
{
	if (!plat_slave_logic_on())
	{
		fn(arg, 0);
		fn(arg, 1);
		return;
	}
	logic_fn = fn;
	logic_arg = arg;
	JOB->fn = logic_trampoline;
	JOB->arg = 0;
	JOB->done = 0;
	JOB->go = 1;
	fn(arg, 0);
	while (!JOB->done) bus_rest(32);
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge the master's cache: the slave's writes */
}

void plat_parallel(void (*fn)(void *, int), void *arg)
{
	if (!OJ32X_SLAVE_DRAWS || !slave_draws_now)
	{
		fn(arg, 0);
		fn(arg, 1);
		return;
	}
	JOB->fn = fn;
	JOB->arg = arg;
	JOB->done = 0;
	JOB->go = 1;
	fn(arg, 0);
	while (!JOB->done) bus_rest(32);
}

/* ------------------------------------------------------------ helpers */
static void set_sr(uint32_t sr) { __asm__ __volatile__("ldc %0,sr" : : "r"(sr) : "memory"); }

static void comm_pause(void)
{
	bus_rest(128);  /* R7: was a volatile counter on the stack: a write to SDRAM every turn */
}

static void bmd_collect(void);
static int send_cmd(uint16_t cmd, uint32_t tries)
{
	bmd_collect();          /* R9: a bonus floor sent ahead must be answered first */
	pad_collect(40000UL);   /* R7: a pad request sent ahead must be answered first */
	MARS_COMM0 = cmd;
	for (uint32_t n = 0; n < tries && MARS_COMM0 != 0; n++)
		comm_pause();
	return MARS_COMM0 == 0;
}

/* ED / EDSAVE-4: a command whose 68000 side holds RV = 1 - the slave parked in SDRAM first (it
 * answers between jobs; a running job finishes first), the master waits in SDRAM too. */
static int send_cmd_rv(uint16_t cmd, uint32_t tries)
{
	(void)tries;
	bmd_collect();                                   /* before the park, not between it and the */
	pad_collect(40000UL);                            /* command: OpenLara's cdCmdSafe does neither */
	JOB->parked = 0;
	JOB->park = 1;
	for (uint32_t n = 0; n < 4000000UL && !JOB->parked; n++) bus_rest(64);
	MARS_COMM0 = cmd;
	sdram_wait(&MARS_COMM0, NULL, NULL);
	JOB->park = 0;
	return MARS_COMM0 == 0;
}

/* R7: the pad request goes out before the frame's idle wait (frame pacing, the swap) and is
 * read at the next frame's pad poll, so the SH2 no longer sits through the Sub-CPU / 68000 round
 * trip (main.c CMD 48) in the middle of each frame. The pad is read at the end of the frame, as
 * fresh as before. A waiting sound effect rides on it (main.c K33: COMM0 = 48 | n << 8). */
static volatile uint16_t sfx_pending;
static int pad_asked;
static uint16_t pad_value;

static void pad_take(void)
{
	const uint16_t bits = RAW_COMM8;
	pad_value = (bits & 0xF000) == 0xF000 ? 0 : (uint16_t)(bits & 0x0FFF);
}

static void pad_ask(void)
{
	if (pad_asked || MARS_COMM0 != 0) return;
	const uint16_t fx = sfx_pending;
	sfx_pending = 0;
	MARS_COMM0 = (uint16_t)(CMD_GET_PAD | (fx << 8));
	pad_asked = 1;
}

static void pad_collect(uint32_t tries)
{
	if (!pad_asked) return;
	pad_asked = 0;
	for (uint32_t n = 0; n < tries && MARS_COMM0 != 0; n++)
		comm_pause();
	if (MARS_COMM0 == 0) pad_take();
}

/* R7: for code that writes the 32X communication registers itself */
void plat_comm_settle(void) { pad_collect(40000UL); }

static void take_fb(void)
{
	MARS_ADAPTER_B = 0x80;
	while ((MARS_INTMSK & 0x8000) == 0) bus_rest(8);
}

static void flip_wait(void)
{
	/* a swap requested by the scrolling renderer (OJ32X_FLIP_ASYNC) must be finished and
	 * forgotten first - otherwise this swap changes the buffer state under it, and the next
	 * plat_wait_flip waits for a state that never comes again (a hang: frozen picture, the
	 * music playing on) */
	plat_wait_flip();
	uint16_t cur = VDP_FBCTL & FBCTL_FS;
	VDP_FBCTL = cur ^ FBCTL_FS;
	while ((VDP_FBCTL & FBCTL_FS) == cur) bus_rest(8);
}

static void set_genesis_text(int on)
{
	MARS_COMM2 = on ? 1 : 0;
	send_cmd(CMD_TEXT_ONOFF, 800000UL);
	uint16_t m = VDP_DISPMODE;
	VDP_DISPMODE = on ? (uint16_t)(m | DISP_PRI_TEXT) : (uint16_t)(m & ~DISP_PRI_TEXT);
}

static void write_game_line_table(void)
{
	for (int y = 0; y < 256; ++y)
	{
		int gy = y - PLAT_TOP_BORDER;
		int line = (gy >= 0 && gy < PLAT_GAME_H) ? gy : FB_BLANK_LINE;
		FB16[y] = (uint16_t)(FB_PIX_WORD + line * FB_LINE_WORDS);
	}
}

static void clear_back_buffer(void)
{
	uint32_t longs = (0x200 + (PLAT_GAME_H + 1) * PLAT_SCREEN_W) / 4;
	for (uint32_t i = 0; i < longs; ++i)
		FB32[i] = 0;
}

/* -------------------------------------------------------------- start-up */
static void wdt_start(void)
{
	SH2_VCRWDT = (uint16_t)((65 << 8) | (SH2_VCRWDT & 0x00FF));
	SH2_IPRA = (uint16_t)((SH2_IPRA & 0xFF0F) | 0x0020);
	SH2_WTCSR_TCNT = 0x5A00;
	SH2_WTCSR_TCNT = 0xA53E;
}

#define STACK_GUARD_ADDR  ((volatile uint32_t *)0x0603B800)
#define STACK_GUARD_WORDS 16
#define STACK_GUARD_VALUE 0x5AA5C33Cu

static void stack_guard_set(void)
{
	for (int i = 0; i < STACK_GUARD_WORDS; ++i)
		STACK_GUARD_ADDR[i] = STACK_GUARD_VALUE ^ (uint32_t)i;
}

static void stack_guard_check(void)
{
	for (int i = 0; i < STACK_GUARD_WORDS; ++i)
		if (STACK_GUARD_ADDR[i] != (STACK_GUARD_VALUE ^ (uint32_t)i))
			plat_fatal("STACK OVERFLOW: A FUNCTION NEEDS MORE THAN THE 16 KB MASTER STACK");
}

/* C++ start-up (oj.ld collects these; no crtbegin/crtend with -nostartfiles) */
typedef void (*init_fn)(void);
extern init_fn init_array_start[] __asm__("__init_array_start");
extern init_fn init_array_end[] __asm__("__init_array_end");
extern init_fn ctors_start[] __asm__("__ctors_start");
extern init_fn ctors_end[] __asm__("__ctors_end");
extern const char eh_frame_start[] __asm__("__eh_frame_start");
void __register_frame_info(const void *begin, void *object);
static uint32_t eh_object[16];   /* libgcc's "struct object" (6 words on 32-bit; room to spare) */

static void cxx_start(void)
{
	__register_frame_info(eh_frame_start, eh_object);
	for (init_fn *f = init_array_start; f < init_array_end; ++f)
		(*f)();
	for (init_fn *f = ctors_end; f > ctors_start; )
	{
		--f;
		if (*f != 0 && *f != (init_fn)-1)
			(*f)();
	}
}

int main(int argc, char *argv[]);
void plat_exit(int code) __attribute__((noreturn));
static void boot_test(void);
static void sfx_load(void);

/* ---- the data pack, with the loader's progress bar on the splash -------------------
 * The bar is the loader's (cdloader_sh2.c splash_bar: same place, same colours 253-255 from
 * IMAGE.RAW's palette), drawn in both frame buffers like the loader's splash_progress. */
#define BAR_X0 56
#define BAR_X1 236
#define BAR_Y0 186
#define BAR_Y1 198
static uint8_t bar_c(int x, int y, int fill)
{
	if (y < BAR_Y0 + 2 || y >= BAR_Y1 - 2 || x < BAR_X0 + 2 || x >= BAR_X1 - 2) return 255;
	return (x - (BAR_X0 + 2) < fill) ? 254 : 253;
}
static void splash_bar(uint32_t done, uint32_t total)
{
	volatile uint16_t *fb = FB16 + 0x100;
	uint32_t inner = (uint32_t)((BAR_X1 - 2) - (BAR_X0 + 2));
	int fill = total ? (int)(inner * done / total) : 0;
	for (int y = BAR_Y0; y < BAR_Y1; y++)
		for (int x = BAR_X0; x < BAR_X1; x += 2)
			fb[y * (PLAT_SCREEN_W / 2) + (x >> 1)] = (uint16_t)((bar_c(x, y, fill) << 8) | bar_c(x + 1, y, fill));
}
static void splash_progress(uint32_t done, uint32_t total)
{
	if (!OJ32X_SPLASH) return;
	for (int i = 0; i < 2; i++) { splash_bar(done, total); flip_wait(); }
}

static uint32_t pack_bytes;
static int pack_bad_chunks;

/* Two games on one disc: Jazz Jackrabbit CD (world packs OJ<world*10>, SFX00.BIN, save-area
 * pieces 1-4) and Holiday Hare '95 (world packs OJ<world*10+100>, SFX01.BIN, pieces 6-9). Both
 * use the common pack IMAGE8.RAW. game_hh95 is set by the episode menu (plat_set_game). */
static int game_hh95;
static int force_full_load;   /* A+B+C seen on the (unused) game choice screen */
int plat_game(void) { return game_hh95; }
static uint32_t pack_slot(void)   { return PACK_SLOT; }   /* the FILE number (IMAGE8.RAW): main.c picks the cart slot (CART_PAGED: 0x080000 / 0x060000) */
static uint32_t pack_offset(void) { return PLAT_CART_DATA_OFFSET; }
static void sfx_load(void);

/* Holiday Hare '95 from the episode menu - its world packs (+100), its sound effects
 * (SFX01.BIN), its music and save slots while it runs; back to Jazz CD afterwards */
void plat_set_game(int g)
{
	if (g == game_hh95) return;
	game_hh95 = g;
	sfx_load();
}

int plat_hh95_available(void)
{
	static int known = -1;
	if (known < 0)
	{
		MARS_COMM2 = 800;                  /* OJ600.PAK (CMD 38 ids 200+: OJ<id-200>.PAK) */
		MARS_COMM8_32 = 0;
		known = send_cmd(CMD_OPEN_FILE, 2000000UL) && (int32_t)MARS_COMM8_32 > 0;
	}
	return known;
}

/* text on the splash in the error screens' 3x5 font, drawn 2x with 16-bit writes */
static void splash_text(int x0, int y0, const char *t)
{
	volatile uint16_t *fb = FB16 + 0x100;
	for (int c = 0; t[c]; c++)
	{
		int ch = t[c];
		if (ch < 32 || ch > 95) ch = 32;
		const uint16_t g = font3x5[ch - 32];
		for (int gy = 0; gy < 10; gy++)
			for (int gx = 0; gx < 8; gx += 2)
			{
				const int x = x0 + c * 8 + gx, y = y0 + gy;
				uint8_t a = 253, b = 253;
				if (gx < 6 && (g & (1u << (14 - ((gy >> 1) * 3 + (gx >> 1)))))) a = b = 255;
				fb[y * (PLAT_SCREEN_W / 2) + (x >> 1)] = (uint16_t)(a << 8 | b);
			}
	}
}

/* Unused: a game choice screen on the splash, replaced by Holiday Hare '95's entry in the
 * episode menu. */
static __attribute__((unused)) void choose_game(void)
{
	MARS_COMM2 = 9;                        /* is Holiday Hare '95 on the disc? */
	MARS_COMM8_32 = 0;
	if (!send_cmd(CMD_OPEN_FILE, 2000000UL) || (int32_t)MARS_COMM8_32 <= 0) return;
	for (int i = 0; i < 2; i++)            /* both frame buffers */
	{
		splash_text(56, 150, " A  JAZZ JACKRABBIT CD  ");
		splash_text(56, 164, " C  HOLIDAY HARE 95     ");
		flip_wait();
	}
	for (;;)
	{
		const uint16_t pad = plat_read_pad();
		/* A+B+C held (the "load everything again" combination) also counts as Jazz CD */
		if ((pad & (PAD_A | PAD_B | PAD_C)) == (PAD_A | PAD_B | PAD_C)) { force_full_load = 1; game_hh95 = 0; break; }
		if (pad & PAD_A) { game_hh95 = 0; break; }
		if (pad & PAD_C) { game_hh95 = 1; break; }
	}
	while (plat_read_pad() & (PAD_A | PAD_C)) { }   /* let go first */
}

static void pack_load(void)
{
	MARS_COMM2 = (uint16_t)pack_slot();
	MARS_COMM8_32 = 0;
	if (!send_cmd(CMD_OPEN_FILE, 2000000UL)) { plat_log("PACK: OPEN TIMED OUT"); return; }
	uint32_t len = MARS_COMM8_32;
	if ((int32_t)len <= 0) { plat_log("PACK: COMMON PACK NOT ON THE DISC"); return; }
	if (len > PLAT_CART_DATA_END - pack_offset()) { plat_log("PACK: TOO BIG FOR THE CART"); return; }
	MARS_COMM8_32 = len;                   /* main.c CMD 46 reads the length from COMM8 */
	const uint32_t chunks = (len + 32767) >> 15;

	/* quick check, as the loader's (CMD 47 = compare a chunk with the cart, nothing
	 * written): a pack header in the cart with this length and the first, middle and last chunk
	 * the same -> the pack from an earlier boot is still there: no copy. A+B+C held at power-on
	 * copies it anyway. */
	{
		const volatile uint8_t *h = (const volatile uint8_t *)(0x22000000u + pack_offset());
		const uint32_t have = (uint32_t)h[16] << 24 | (uint32_t)h[17] << 16 | (uint32_t)h[18] << 8 | h[19];
		int same = h[0] == 'O' && h[1] == 'J' && h[2] == 'P' && h[3] == 'K' && have == len;
		const uint32_t probe[3] = { 0, chunks >> 1, chunks - 1 };
		for (int i = 0; i < 3 && same; i++)
		{
			MARS_COMM2 = (uint16_t)probe[i];
			MARS_COMM8_32 = len;
			MARS_COMM12 = 0;
			if (!send_cmd_rv(CMD_CHUNK_COMPARE, 2000000UL) || MARS_COMM12 != 0) same = 0;
		}
		MARS_COMM8_32 = len;
		if (same && !force_full_load && (plat_read_pad() & (PAD_A | PAD_B | PAD_C)) != (PAD_A | PAD_B | PAD_C))
		{
			splash_progress(2 * chunks, 2 * chunks);
			pack_bytes = len;
			pack_mount((const uint8_t *)(0x02000000u + pack_offset()));
			plat_log("PACK: ALREADY IN THE CART");
			return;
		}
	}

	/* one bar for the whole boot - the loader filled the first half (the engine), the pack
	 * fills the second */
	splash_progress(chunks, 2 * chunks);
	for (uint32_t c = 0; c < chunks; c++)
	{
		MARS_COMM2 = (uint16_t)(c | 0x8000);   /* OL-13: no read-back (level loads) */
		MARS_COMM12 = 0;
		if (!send_cmd_rv(CMD_CHUNK_TO_CART, 2000000UL) || MARS_COMM12 != 0)
			pack_bad_chunks++;
		splash_progress(chunks + c + 1, 2 * chunks);
	}
	/* the 68000 wrote the cart behind the SH2's cache: purge it (as the loader's hand-over) */
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;
	pack_bytes = len;
	if (!pack_mount((const uint8_t *)(0x02000000u + pack_offset())))
		plat_log("PACK: COPIED BUT NOT A PACK (OJPK)");
}

#if OJ32X_CART_MB
#include "cartcache.h"
/* R7 - FILL: on the boot, the packs that are not in the cart yet are copied into its free pages
 * (cartcache_fill), so the first boot fills the 16 MB cart and later ones load nothing. Start
 * stops it (the rest load when they are played, as before). */
static int fill_shown;   /* R11: the copying text is on the splash */
static int fill_progress(int done, int total)
{
	fill_shown = 1;
	for (int i = 0; i < 2; i++)
	{
		splash_text(28, 150, "COPYING TO THE CART - START: SKIP");
		splash_bar((uint32_t)done, (uint32_t)total);
		flip_wait();
	}
	return (plat_read_pad() & PAD_START) != 0;
}
static void cart_fill(void)
{
	fill_running = 1;
	/* Jazz CD's worlds in play order, then Holiday Hare '95's (port/pack.h: OJ<world * 10>,
	 * +100 for Holiday Hare '95; GB<k>: the world's Genesis plane) */
	static const int k[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150,
	                         160, 170, 180, 190, 200, 210, 220, 230, 300, 310, 320, 330, 340, 350,
	                         360, 370, 380, 390, 400, 410, 500, 510, 520, 600, 610 };
	enum { N = sizeof k / sizeof k[0] };
	int ids[N], gbs[N];
	for (int i = 0; i < N; i++) { ids[i] = PACK_LEVEL_ID0 + k[i]; gbs[i] = PACK_GENESIS_ID0 + k[i]; }
	cartcache_fill(ids, gbs, N, fill_progress);
	fill_running = 0;
	for (int i = 0; i < 2 && fill_shown; i++)
	{
		splash_text(28, 150, "                                 ");
		flip_wait();
	}
}
#endif

void plat_mars_entry(void)
{
	/* Tyrian's start-up minus the 68000 handshake (see the top of this file). */
	MARS_COMM0 = 0;
	MARS_INTMSK |= 0x0002;
	set_sr(0);

	take_fb();
	wdt_start();
	stack_guard_set();

	/* the Genesis text (the Sub-CPU's green/white diagnostics) stays as the loader left
	 * it until C++ start-up has worked; our own screens take over from there */
	VDP_DISPMODE = (uint16_t)((VDP_DISPMODE & (0x8000 | DISP_PRI_TEXT)) | DISP_MODE_256);
	if (!OJ32X_DIAG_TEXT)
		set_genesis_text(0);

	const uint32_t splash_from = vblank_count;
	if (!OJ32X_SPLASH)
	{
		for (int i = 0; i < 2; ++i)
		{
			clear_back_buffer();
			write_game_line_table();
			flip_wait();
		}
		for (int i = 0; i < 256; ++i)
			VDP_CRAM[i] = 0;
	}
	/* OJ32X_SPLASH 1: the frame buffers, line tables and palette are the loader's splash -
	 * nothing is drawn or flipped until the boot test */

	plat_log("C++ START-UP");
	cxx_start();
	{ void oj32x_cxx_handlers(void); oj32x_cxx_handlers(); }   /* name what stops the game */
	plat_log("PACK LOADING");
	pack_load();   /* always Jazz CD's common pack - Holiday Hare '95 is an episode */
	sfx_load();
#if OJ32X_CART_MB
	cart_fill();   /* R7: the 16 MB cart filled on the first boot */
#endif
	if (OJ32X_SPLASH)
		while (vblank_count - splash_from < OJ32X_SPLASH_HOLD) { }
	set_genesis_text(0);
	boot_test();

	static char arg0[] = "openjazz";
	static char *argv[] = { arg0, NULL };
	plat_log("MAIN()");
	/* state 4B10 = the game runs - the Sub-CPU stops reprinting its diagnostic rows
	 * (each print goes through the 68000 and would slow the pad replies) */
	MARS_COMM12 = 0x4B10;
	plat_exit(main(1, argv));
}

void plat_init(void)
{
	/* Hardware is set up in plat_mars_entry(), before main(). */
}

/* ---------------------------------------------------------------- clock */

/* profiling clock = vblanks (16.68 ms) + the watchdog counter since the last vblank
 * (one count = 4096 cycles = 178 us; it wraps every 45.6 ms, longer than a vblank). The
 * console does not deliver the watchdog's overflow interrupt (plat_ticks_ms counts vblanks
 * for that reason), so the watchdog alone cannot measure more than 45.6 ms. The SH2's
 * free-running timer is no use either: something else resets or reprograms it. */
uint32_t plat_prof_us(void)
{
	uint32_t v;
	uint8_t at, now;
	do
	{
		v = vblank_count;
		at = vblank_wtcnt;
		now = SH2_WTCNT_R;
	} while (v != vblank_count);
	const uint32_t fine = (uint32_t)(uint8_t)(now - at) * 4096u * 1000u / (PPHI_HZ / 1000u);
	return v * 16683u + (fine < 16683u ? fine : 16683u);
}

uint32_t plat_ticks_ms(void)
{
	static uint32_t last_ms;
	uint32_t ovf, cnt;

	do
	{
		ovf = mars_pwdt_ovf_count;
		cnt = SH2_WTCNT_R;
	} while (ovf != mars_pwdt_ovf_count);

	uint32_t ms;
	if (ovf == 0 && vblank_count > 120)
		ms = (uint32_t)((uint64_t)vblank_count * 1000u / 60u);
	else
	{
		uint64_t counts = (uint64_t)ovf * 256u + cnt;
		ms = (uint32_t)(counts * 4096u * 1000u / PPHI_HZ);
	}
	if ((int32_t)(ms - last_ms) < 0)
		ms = last_ms;
	last_ms = ms;
	return ms;
}

uint32_t plat_random_seed(void)
{
	uint32_t s = ((uint32_t)mars_pwdt_ovf_count << 8) ^ SH2_WTCNT_R ^ (vblank_count * 2654435761u);
	return s ^ MARS_COMM10;
}

/* ------------------------------------------------------------------ pad */
/* sound effects on the Sega CD's PCM chip (SFX00.BIN loaded at boot by CMD 53; SFX01.BIN
 * while Holiday Hare '95 runs). One waiting effect is sent with the next pad request. */
void plat_sfx(int id)
{
	if (id > 0 && id < 256)
		sfx_pending = (uint16_t)id;
}

/* MUSIC SWITCH: 1 = CD audio tracks 2-41 for the music (build.sh sets it when music/*.wav
 * exist); 0 = no music (no such tracks on the disc - do not ask for them). */
#ifndef OJ32X_MUSIC
#define OJ32X_MUSIC 0
#endif
#define CMD_PLAY_TRACK   45      /* main.c: COMM2 = track (bit 15 = once) */
#define CMD_MUSIC_OFF    54

void plat_music(int track)
{
	if (!OJ32X_MUSIC) return;
	if (track <= 0) { send_cmd(CMD_MUSIC_OFF, 4000000UL); return; }
	MARS_COMM2 = (uint16_t)track;
	send_cmd(CMD_PLAY_TRACK, 4000000UL);
}

static void sfx_load(void)
{
	MARS_COMM2 = (uint16_t)(game_hh95 ? 1 : 0);   /* SFX00.BIN / SFX01.BIN */
	MARS_COMM12 = 0;
	send_cmd(CMD_LOAD_SFX, 4000000UL);
	if (MARS_COMM12 == 0) plat_log("SOUND EFFECTS: SFX00.BIN NOT LOADED");
}

uint16_t plat_read_pad(void)
{
	static uint16_t pad;
	static uint32_t last_ms = 0xFFFFFFFFu;

	uint32_t now = plat_ticks_ms();
	if (pad_asked) {                 /* R7: asked at the end of the last frame */
		pad_collect(40000UL);
		last_ms = now;
		return pad = pad_value;
	}
	if (last_ms != 0xFFFFFFFFu && now - last_ms < 4)
		return pad;
	last_ms = now;

	/* a waiting sound effect rides on the pad request (main.c K33: COMM0 = 48 | n << 8) */
	const uint16_t fx = sfx_pending;
	sfx_pending = 0;
	if (!send_cmd((uint16_t)(CMD_GET_PAD | (fx << 8)), 40000UL))
		return pad;

	uint16_t bits = MARS_COMM8;
	pad = (bits & 0xF000) == 0xF000 ? 0 : (uint16_t)(bits & 0x0FFF);
	return pad;
}

/* ---------------------------------------------------------------- frame pacing */
#define PACE_HISTORY 16
#define PACE_MAX     6
static uint32_t last_flip_vblank;
static uint8_t pace_history[PACE_HISTORY];
static unsigned int pace_pos;
static int pace_n = 1;

static void pace_before_flip(void)
{
	const uint32_t natural = vblank_count - last_flip_vblank + 1;
	/* OJ32X_PACE: hold each frame to the median of the last 16 frame times (in vblanks), so
	 * motion is even. Not the slowest ones: at 4-6 vblanks a frame that would throw away 1-2
	 * vblanks on most frames. OJ32X_PACE 0: each frame shows at the first vblank after it is
	 * done. */
	if (OJ32X_PACE && natural <= PACE_MAX)
	{
		pace_history[pace_pos++ % PACE_HISTORY] = (uint8_t)natural;
		uint8_t sorted[PACE_HISTORY];
		for (int i = 0; i < PACE_HISTORY; ++i)
		{
			int j = i;
			while (j > 0 && sorted[j - 1] > pace_history[i])
			{
				sorted[j] = sorted[j - 1];
				--j;
			}
			sorted[j] = pace_history[i];
		}
		/* J24: a little above the median (the 12th of 16) - with the second SH2 the frame
		 * times spread wider (2-5 vblanks), and at the median many frames came late: uneven */
		const int n = sorted[(PACE_HISTORY * 11) / 16];
		pace_n = n > 0 ? n : 1;
		while (vblank_count - last_flip_vblank < (uint32_t)(pace_n - 1)) { }
	}
	while (VDP_FBCTL & FBCTL_VBLK) bus_rest(8);
}

/* J24: the game's clock - the time the frame being shown next will appear (the vblank of the
 * swap the next present makes: the last swap + the pace), not the moment the game happens to
 * ask. Asked at different points between two swaps, the plain clock gave a frame 2 vblanks of
 * game time and the next 3 while both were shown for the same time: jerky. Never goes back. */
uint32_t plat_frame_ms(void)
{
	static uint32_t last;
	uint32_t v = last_flip_vblank + (uint32_t)pace_n;
	if (v < vblank_count + 1) v = vblank_count + 1;
	uint32_t ms = (uint32_t)((uint64_t)v * 1000u / 60u);
	if (ms < last) ms = last;
	last = ms;
	return ms;
}

/* -------------------------------------------------------------- display */
static uint32_t border_fill;   /* four pixels of the darkest palette entry */

void plat_border_colour(int index)
{
	border_fill = (uint32_t)(index & 255) * 0x01010101u;
}

static void present_finish(volatile uint32_t *blank_line, const uint16_t *palette555)
{
	for (int x = 0; x < PLAT_SCREEN_W / 4; ++x)
		blank_line[x] = border_fill;
	bmd_collect();                           /* R9: the floor drawn while this picture was */
	pad_ask();                               /* R7: answered while this SH2 waits below */
	pace_before_flip();
	if (bmd_show >= 0)                       /* R9: the 68000 copies a new floor at the swap */
		MARS_COMM6 = (uint16_t)(0x4000 | bmd_show);
	flip_wait();
	last_flip_vblank = vblank_count;
	while ((VDP_FBCTL & FBCTL_PEN) == 0) bus_rest(8);
	if (bmd_hole >= 0) {
		/* R9: every colour in front of the Genesis but the floor's area (its colour: the
		 * floor's pixel value 0, which the Genesis leaves see-through) */
		for (int i = 0; i < 256; ++i)
			VDP_CRAM[i] = (uint16_t)((palette555[i] ? palette555[i] : 0x0400) | 0x8000);
		VDP_CRAM[bmd_hole] = bmd_hole555;
		return;
	}
	for (int i = 0; i < 256; ++i)
		/* 0x0000 is see-through on the 32X ([BOOT] rule 5): black in the game's own
		 * palettes let the Genesis layer show. Black is written as 0x0400 instead. */
		VDP_CRAM[i] = palette555[i] ? palette555[i] : 0x0400;
}

/* REV: a screen that is not the level: the Genesis plane hidden at the next vblank */
static void genbg_hide(void)
{
	if (!genbg_active()) return;
	genbg_plain_screen();
	MARS_COMM6 = GENBG_HIDE;
}

void plat_present(const uint8_t *pixels, int pitch, const uint16_t *palette555)
{
	genbg_hide();
	stack_guard_check();
	loading_up = 0;                          /* the game's picture: a load shows the loading screen again */
	write_game_line_table();

	volatile uint32_t *dst = FB32 + (FB_PIX_WORD * 2) / 4;
	for (int y = 0; y < PLAT_GAME_H; ++y)
	{
		const uint32_t *src = (const uint32_t *)(pixels + y * pitch);
		for (int x = 0; x < PLAT_SCREEN_W / 4; x += 4)
		{
			dst[0] = src[x + 0];
			dst[1] = src[x + 1];
			dst[2] = src[x + 2];
			dst[3] = src[x + 3];
			dst += 4;
		}
	}
	present_finish(dst, palette555);
}

void plat_clear_screens(void)
{
	for (int b = 0; b < 2; b++)
	{
		volatile uint32_t *px = FB32 + (FB_PIX_WORD * 2) / 4;
		for (int i = 0; i < PLAT_SCREEN_W * PLAT_GAME_H / 4; i++)
			px[i] = 0;
		write_game_line_table();
		flip_wait();
	}
}

/* frame buffer layout for the scrolling renderer (bytes): ring at 0x200 (384 x 200 =
 * 76,800), panel at 0x12E00 (320 x 33), blank line at 0x16000 */
#define SCROLL_RING_OFF  0x00200
#define SCROLL_PANEL_OFF 0x12E00
#define SCROLL_BLANK_OFF 0x16000
#define VDP_SHIFT        (*(volatile uint16_t *)0x20004102)   /* bit 0: picture 1 pixel left */

uint8_t *plat_scroll_ring(void)  { return (uint8_t *)FB16 + SCROLL_RING_OFF; }
uint8_t *plat_scroll_panel(void) { return (uint8_t *)FB16 + SCROLL_PANEL_OFF; }
int plat_back_index(void) { return VDP_FBCTL & FBCTL_FS; }

void plat_present_scroll(const uint16_t *palette555, int row0, int xoff, int viewH)
{
	stack_guard_check();
	loading_up = 0;                          /* the game's picture: a load shows the loading screen again */
	for (int line = 0; line < 256; line++)
	{
		const int g = line - PLAT_TOP_BORDER;
		uint32_t byte;
		if (g < 0 || g >= PLAT_GAME_H) byte = SCROLL_BLANK_OFF;
		else if (g < viewH) byte = SCROLL_RING_OFF + (uint32_t)((row0 + g) % 200) * 384u + (uint32_t)(xoff & ~1);
		else byte = SCROLL_PANEL_OFF + (uint32_t)(g - viewH) * PLAT_SCREEN_W;
		FB16[line] = (uint16_t)(byte >> 1);
	}
#ifdef OJ32X_FLIP_ASYNC
	pending_shift = (uint16_t)(xoff & 1);   /* set with the palette, when the swap happens */
	{
		volatile uint32_t *blank = FB32 + SCROLL_BLANK_OFF / 4;
		for (int x = 0; x < PLAT_SCREEN_W / 4; ++x) blank[x] = border_fill;
		if (genbg_active())               /* REV: bit 15 on all but the background colours */
			genbg_cram(palette555, (uint16_t *)pending_pal);
		else
			for (int i = 0; i < 256; ++i) pending_pal[i] = palette555[i] ? palette555[i] : 0x0400;
		pad_ask();                        /* R7: answered while this SH2 waits below */
		pace_before_flip();               /* even frame times (median), then out of vblank */
		const uint16_t cur = VDP_FBCTL & FBCTL_FS;
		flip_target = cur ^ FBCTL_FS;
		pal_pending = 1;
		flip_pending = 1;
		VDP_FBCTL = flip_target;          /* swap at the next vblank; no waiting here */
		last_flip_vblank = vblank_count;
		if (genbg_active()) {             /* REV: the 68000 scrolls the Genesis plane to this
		                                     picture's camera at the same vblank */
			MARS_COMM4 = genbg_comm4();
			MARS_COMM6 = genbg_comm6();
		}
	}
#else
	VDP_SHIFT = (uint16_t)(xoff & 1);
	present_finish(FB32 + SCROLL_BLANK_OFF / 4, palette555);
#endif
}

void plat_wait_flip(void)
{
	if (!flip_pending) return;
	while ((VDP_FBCTL & FBCTL_FS) != flip_target) bus_rest(8);
	if (pal_pending)                       /* the vblank handler did not get to it */
	{
		while ((VDP_FBCTL & FBCTL_PEN) == 0) bus_rest(8);
		load_pending_palette();
	}
	flip_pending = 0;
}

uint8_t *plat_canvas(void)
{
	return OJ32X_DIRECT_FB ? (uint8_t *)FB16 + FB_PIX_WORD * 2 : NULL;
}

void plat_present_canvas(const uint16_t *palette555)
{
	genbg_hide();
	stack_guard_check();
	loading_up = 0;                          /* the game's picture: a load shows the loading screen again */
	write_game_line_table();
	present_finish(FB32 + (FB_PIX_WORD * 2 + PLAT_GAME_H * PLAT_SCREEN_W) / 4, palette555);
}

uint8_t *plat_fb_area(int area)
{
	return (uint8_t *)FB16 + (area == PLAT_FB_DISPLAY ? FB_PIX_WORD * 2 : 0x10000);
}

/* ---------------------------------------------------------------- cart */
uint8_t *plat_cart_data(uint32_t *size)
{
	*size = PLAT_CART_DATA_END - PLAT_CART_DATA_OFFSET;
	return (uint8_t *)(0x02000000u + PLAT_CART_DATA_OFFSET);
}

void plat_cart_write(uint32_t offset, const void *src, size_t bytes)
{
	(void)offset; (void)src; (void)bytes;
	plat_fatal("CART WRITE: THE SH2 CANNOT WRITE THE CART (J4B: GRAPHICS COME FROM THE DISC)");
}

/* ---------------------------------------------------------------- memory */
size_t oj32x_heap_used(void);
size_t oj32x_heap_size(void);
extern char *oj32x_heap_peak;
extern char heap_start_sym[] __asm__("__heap_start");

size_t plat_heap_free(void)
{
	return oj32x_heap_size() - oj32x_heap_used() + (size_t)mallinfo().fordblks;
}

void plat_mem_mark(const char *label)
{
	(void)label;
}

/* ------------------------------------------------------------- messages */
static char last_log[80];
static char prev_log[2][80];   /* the two lines before the last one */

void oj32x_heap_check(const char *where);

void plat_log(const char *msg)
{
	oj32x_heap_check("LOG");
	/* Kept for the error screens: the last three lines */
	strcpy(prev_log[0], prev_log[1]);
	strcpy(prev_log[1], last_log);
	strncpy(last_log, msg, sizeof last_log - 1);
	last_log[sizeof last_log - 1] = '\0';
	for (char *p = last_log; *p; ++p)
		if (*p == '\n')
			*p = '\0';
}

/* Text screen in the back buffer: 224 lines, 3x5 glyphs drawn 2x (8x12 cells). */
static void text_screen(uint16_t bg, uint16_t fg, const char *const *lines, int count)
{
	for (int y = 0; y < 256; ++y)
		FB16[y] = (uint16_t)(FB_PIX_WORD + (y < PLAT_SCREEN_H ? y : 0) * FB_LINE_WORDS);

	/* clear with 32-bit writes: on the console, byte writes of 0 did not clear the frame
	 * buffer (the screen was drawn over the splash), 32-bit writes did ([BOOT] rule 4) */
	volatile uint8_t *pix = (volatile uint8_t *)FB16 + FB_PIX_WORD * 2;
	volatile uint32_t *pix32 = FB32 + (FB_PIX_WORD * 2) / 4;
	for (int i = 0; i < PLAT_SCREEN_W * PLAT_SCREEN_H / 4; ++i)
		pix32[i] = 0;

	int row = 1;
	for (int l = 0; l < count && row < PLAT_SCREEN_H / 12; ++l)
	{
		const char *s = lines[l] ? lines[l] : "";
		int col = 1;
		for (; *s && row < PLAT_SCREEN_H / 12; ++s)
		{
			if (*s == '\n' || col >= PLAT_SCREEN_W / 8 - 1)
			{
				row++;
				col = 1;
				if (*s == '\n')
					continue;
			}
			int c = (unsigned char)*s;
			if (c >= 'a' && c <= 'z')
				c -= 32;
			if (c < 32 || c > 95)
				c = '?';
			uint16_t g = font3x5[c - 32];
			for (int gy = 0; gy < 5; ++gy)
				for (int gx = 0; gx < 3; ++gx)
					if (g & (1u << (14 - (gy * 3 + gx))))
					{
						int px = col * 8 + gx * 2, py = row * 12 + gy * 2;
						volatile uint8_t *p = pix + py * PLAT_SCREEN_W + px;
						p[0] = p[1] = p[PLAT_SCREEN_W] = p[PLAT_SCREEN_W + 1] = 1;
					}
			col++;
		}
		row++;
	}

	flip_wait();
	while ((VDP_FBCTL & FBCTL_PEN) == 0) bus_rest(8);
	VDP_CRAM[0] = bg;
	VDP_CRAM[1] = fg;
}

static void dec(char *out, uint32_t v)
{
	char tmp[11];
	int n = 0;
	do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (n)
		*out++ = tmp[--n];
	*out = '\0';
}

static void hex(char *out, uint32_t v, int digits)
{
	for (int i = digits - 1; i >= 0; --i, v >>= 4)
		out[i] = "0123456789ABCDEF"[v & 15];
	out[digits] = '\0';
}

/* "HEAP 123456 USED, PEAK 130000 OF 172000" */
static const char *heap_line(void)
{
	static char line[64];
	char *p = line;
	strcpy(p, "HEAP "); p += 5;
	dec(p, (uint32_t)oj32x_heap_used()); p += strlen(p);
	strcpy(p, " USED, PEAK "); p += strlen(p);
	dec(p, oj32x_heap_peak ? (uint32_t)(oj32x_heap_peak - heap_start_sym) : 0); p += strlen(p);
	strcpy(p, " OF "); p += strlen(p);
	dec(p, (uint32_t)oj32x_heap_size());
	return line;
}

void plat_fatal(const char *msg)
{
	set_sr(0xF0);
	const char *lines[] = { "OPENJAZZ 32X - FATAL ERROR", "", msg, "", "LAST LOG:", prev_log[0], prev_log[1], last_log, "", heap_line() };
	text_screen(0x0010 /* dark red */, 0x7FFF, lines, 10);
	for (;;) { }
}

/* crt0 passes the old stack pointer and PR as well. The call chain: PR, then the
 * words on the old stack that point into the engine's code (return addresses saved by the
 * callers; some may be stale - look them up with tools/where.sh). */
extern char text_start_sym[] __asm__("__text_start");
extern char text_end_sym[] __asm__("__text_end");

void plat_exception(uint32_t kind, uint32_t pc, uint32_t sp, uint32_t pr)
{
	static const char *const kinds[] = { "?", "ILLEGAL INSTRUCTION", "INVALID SLOT INSTRUCTION",
	                                     "ADDRESS ERROR (MISALIGNED ACCESS?)", "DMA ADDRESS ERROR",
	                                     "OTHER EXCEPTION" };
	static char pc_line[40] = "PC ";
	static char chain[3][48];
	set_sr(0xF0);
	hex(pc_line + 3, pc, 8);
	strcat(pc_line, "  PR ");
	hex(pc_line + strlen(pc_line), pr, 8);
	int found = 0;
	if (sp >= 0x06000000u && sp < 0x06040000u && (sp & 3) == 0)
	{
		const volatile uint32_t *st = (const volatile uint32_t *)(uintptr_t)(sp | 0x20000000u);
		for (int i = 2; i < 256 && found < 9 && sp + 4u * i < 0x06040000u; ++i)
		{
			uint32_t v = st[i];
			if (v >= (uint32_t)(uintptr_t)text_start_sym && v < (uint32_t)(uintptr_t)text_end_sym && (v & 1) == 0)
			{
				char *l = chain[found / 3];
				if (found % 3 == 0) strcpy(l, "CALLS ");
				hex(l + strlen(l), v, 8);
				strcat(l, " ");
				found++;
			}
		}
	}
	const char *lines[] = { "OPENJAZZ 32X - SH2 EXCEPTION", kinds[kind < 6 ? kind : 0], pc_line,
	                        chain[0], chain[1], chain[2], "(TOOLS/WHERE.SH <ADDRESS>)",
	                        "LAST LOG:", prev_log[0], prev_log[1], last_log, heap_line() };
	text_screen(0x0010, 0x7FFF, lines, 12);
	for (;;) { }
}

void plat_exit(int code)
{
	set_sr(0xF0);
	static char code_line[24] = "EXIT CODE ";
	if (code < 0) { code_line[10] = '-'; dec(code_line + 11, (uint32_t)-code); }
	else dec(code_line + 10, (uint32_t)code);
	const char *lines[] = { "OPENJAZZ 32X", "", code == 0 ? "THE GAME HAS ENDED." : "THE GAME STOPPED WITH AN ERROR.",
	                        code_line, "", "LAST LOG:", prev_log[0], prev_log[1], last_log, "", heap_line() };
	text_screen(0x2800 /* dark blue */, 0x7FFF, lines, 11);
	for (;;) { }
}

/* ------------------------------------------------------------ boot test */
/* port/mars_boottest.cpp: C++ checks (constructor ran, throw/catch, new/delete) */
int oj32x_test_ctor(void);
int oj32x_test_throw(void);
int oj32x_test_new(void);

static void line_add(char *line, const char *label, int ok, const char *detail)
{
	strcpy(line, label);
	strcat(line, ok ? "OK" : "FAILED");
	if (detail && *detail) { strcat(line, "  "); strcat(line, detail); }
}

static void boot_test(void)
{
	static char l_build[64], l_cart[64], l_ctor[64], l_new[64], l_throw[64], l_heap[64];
	static char l_pad[64], l_time[64], l_pack[64];
	char tmp[24];

	strcpy(l_build, "BUILD J20  " __DATE__ " " __TIME__);

	/* the engine's own header in the cart (the loader checked it too) */
	int cart_ok = CART_BYTES[0x100] == 'S' && CART_BYTES[0x101] == 'E' &&
	              CART_BYTES[0x102] == 'G' && CART_BYTES[0x103] == 'A' &&
	              CART_BYTES[0x3C0] == 'O' && CART_BYTES[0x3C1] == 'p';
	line_add(l_cart, "CART READ: ", cart_ok, "");

	line_add(l_ctor, "C++ CONSTRUCTORS: ", oj32x_test_ctor(), "");
	line_add(l_new, "C++ NEW/DELETE: ", oj32x_test_new(), "");

	/* if the throw is not caught, the C++ runtime calls abort(): the blue end screen then
	   shows this as its last log line */
	plat_log("EXCEPTION TEST: THROWING");
	int t = oj32x_test_throw();
	plat_log("EXCEPTION TEST: DONE");
	hex(tmp, (uint32_t)t, 4);
	line_add(l_throw, "C++ EXCEPTIONS: ", t == 0x0123, tmp);

	strcpy(l_heap, heap_line());

	/* the data pack */
	if (pack_count() > 0)
	{
		strcpy(l_pack, "PACK: OK  ");
		dec(l_pack + strlen(l_pack), pack_bytes);
		strcat(l_pack, " BYTES");
		if (pack_bad_chunks) { strcat(l_pack, "  BAD CHUNKS "); dec(l_pack + strlen(l_pack), (uint32_t)pack_bad_chunks); }
	}
	else
	{
		strcpy(l_pack, "PACK: FAILED  ");
		strncat(l_pack, last_log, 40);
	}

	/* OJ32X_BOOT_TEST 0: no screen and no Start when every check is OK */
	if (!OJ32X_BOOT_TEST && cart_ok && oj32x_test_ctor() && oj32x_test_new() && t == 0x0123 &&
	    pack_count() > 0 && pack_bad_chunks == 0)
		return;

	/* live screen: pad + clock, until Start */
	uint16_t pad = 0;
	uint32_t frames = 0;
	do
	{
		pad = plat_read_pad();
		strcpy(l_pad, "PAD ");
		hex(l_pad + 4, pad, 4);
		strcat(l_pad, "  START = RUN THE GAME");
		strcpy(l_time, "CLOCK ");
		dec(l_time + 6, plat_ticks_ms());
		strcat(l_time, " MS  VBL ");
		dec(l_time + strlen(l_time), vblank_count);
		strcat(l_time, "  FR ");
		dec(l_time + strlen(l_time), frames++);
		const char *lines[] = { "OPENJAZZ 32X - BOOT TEST", l_build, "", l_cart, l_ctor, l_new, l_throw,
		                        l_pack, "", l_heap, "", l_pad, l_time };
		text_screen(0x0000, 0x7FFF, lines, 13);
	} while (!(pad & PAD_START));

	/* wait for Start to be let go, so the game does not see it */
	while (plat_read_pad() & PAD_START) { }

	/* back to the game's display set-up */
	for (int i = 0; i < 2; ++i)
	{
		clear_back_buffer();
		write_game_line_table();
		flip_wait();
	}
	for (int i = 0; i < 256; ++i)
		VDP_CRAM[i] = 0;
}

/* What crtbegin.o normally provides (we link with -nostartfiles): the handle C++ uses to
   register static destructors (__cxa_atexit). */
void *__dso_handle = 0;


/* ---- the loading screen and the world / bonus packs --------------------------------------
 * The loading screen is the loader's splash (cdloader_sh2.c splash_show, same commands: 38
 * with file 0 = IMAGE.RAW, 51 = chunk into the frame buffer, both buffers) with the bar. */
static void give_fb(void)
{
	MARS_ADAPTER_B = 0x00;
	while ((MARS_INTMSK & 0x8000) != 0) bus_rest(8);
}

static void loading_screen(void)
{
	if (fill_running || loading_up) return;
	loading_up = 1;
	MARS_COMM2 = 0;
	if (!send_cmd(CMD_OPEN_FILE, 2000000UL)) return;
	uint32_t len = MARS_COMM8_32;
	if ((int32_t)len < PLAT_SCREEN_W * 224 + 512) return;     /* no IMAGE.RAW */
	uint32_t chunks = (len + 32767) >> 15;
	for (int pass = 0; pass < 2; pass++)
	{
		give_fb();
		for (uint32_t c = 0; c < chunks; c++) { MARS_COMM2 = (uint16_t)c; send_cmd(CMD_CHUNK_TO_FB, 2000000UL); }
		take_fb();
		for (int i = 0; i < 256; i++)
			FB16[i] = (uint16_t)(0x100 + (i < 224 ? i : 223) * (PLAT_SCREEN_W / 2));
		if (pass == 0)
		{
			const volatile uint16_t *pal = (const volatile uint16_t *)((volatile uint8_t *)FB16 + 0x200 + PLAT_SCREEN_W * 224);
			for (int i = 0; i < 256; i++) VDP_CRAM[i] = pal[i];
		}
		splash_bar(0, 1);
		flip_wait();
	}
}

const uint8_t *plat_cart_ptr(uint32_t cart_offset)
{
	return (const uint8_t *)(0x02000000u + cart_offset);
}

int32_t plat_load_pack(int id, uint32_t cart_offset, uint32_t max_bytes)
{
	(void)cart_offset;   /* main.c puts OJ<k>.PAK at slot 20 = PACK_LEVEL_CART (0x280000) */
	MARS_COMM2 = (uint16_t)id;
	MARS_COMM8_32 = 0;
	if (!send_cmd(CMD_OPEN_FILE, 2000000UL)) { plat_log("LEVEL PACK: OPEN TIMED OUT"); return -1; }
	uint32_t len = MARS_COMM8_32;
	if ((int32_t)len <= 0) { plat_log("LEVEL PACK: NOT ON THE DISC"); return -1; }
	if (len > max_bytes) { plat_log("LEVEL PACK: TOO BIG FOR THE CART"); return -2; }

	loading_screen();
	/* the 32-bit length again for command 46 (command 51 used COMM8 meanwhile): reopen */
	MARS_COMM2 = (uint16_t)id;
	send_cmd(CMD_OPEN_FILE, 2000000UL);
	MARS_COMM8_32 = len;
	const uint32_t chunks = (len + 32767) >> 15;
	int bad = 0;
	for (uint32_t c = 0; c < chunks; c++)
	{
		MARS_COMM2 = (uint16_t)(c | 0x8000);   /* OL-13: no read-back */
		MARS_COMM12 = 0;
		if (!send_cmd_rv(CMD_CHUNK_TO_CART, 2000000UL) || MARS_COMM12 != 0)
			bad++;
		for (int i = 0; i < 2; i++) { splash_bar(c + 1, chunks); flip_wait(); }
	}
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge the cache: the 68000 wrote the cart */
	if (bad) plat_log("LEVEL PACK: BAD CHUNKS");
	return (int32_t)len;
}

void plat_packs_ready(void) { }

/* ED-SAVE (below): page 1's save area read through the Sub-CPU when it is in the EverDrive's save bank */
static int ed_save_bank(void);
static void ed_fetch(int piece);
static uint8_t ed_buf[4096];
static int ed_piece = -1;                /* the piece in ed_buf */
#if OJ32X_CART_MB
/* ---- R5: the 8 / 16 MB cart (port/plat.h, port/cartcache.c, docs/CART_8MB_16MB.md) ---- */
#define CMD_CART_PAGE 68
static int page_now = 1;                 /* window 1's page: 1 after reset */
static int save_depth, save_page;

int plat_cart_page_now(void) { return page_now; }

#define CMD_DISC_STAMP 70
uint32_t plat_disc_stamp(void)
{
	static int known;
	static uint32_t stamp;
	if (!known) {
		MARS_COMM8_32 = 0;
		if (send_cmd(CMD_DISC_STAMP, 2000000UL)) stamp = MARS_COMM8_32;
		known = 1;
	}
	return stamp;
}

#define CMD_UNPACK 69
uint32_t plat_unpack(int src_page, uint32_t src_cart, int dst_page, uint32_t dst_cart)
{
	MARS_COMM2 = (uint16_t)(src_page | dst_page << 8);
	MARS_COMM8_32 = src_cart;
	MARS_COMM12 = (uint16_t)(dst_cart >> 15);
	const int done = send_cmd_rv(CMD_UNPACK, 2000000UL);
	page_now = dst_page;                     /* window 1 shows the destination now */
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge: the 68000 wrote the cart */
	if (!done) { plat_log("UNPACK: COMMAND TIMED OUT"); return 0; }
	return MARS_COMM12 == 0 ? MARS_COMM8_32 : 0;
}

int plat_cart_page(int page)
{
	if (page < 1 || page > 7) return 0;
	if (page == page_now) return 1;
	MARS_COMM2 = (uint16_t)page;
	MARS_COMM12 = 0;
	if (!send_cmd_rv(CMD_CART_PAGE, 2000000UL)) { plat_log("CART PAGE: COMMAND TIMED OUT"); return 0; }
	const int ok = MARS_COMM12 != 0;
	if (ok) page_now = page;
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge: window 1 shows other memory now (the
	                                            slave purges before each job) */
	return ok;
}

int32_t plat_pack_open(int id)
{
	MARS_COMM2 = (uint16_t)id;
	MARS_COMM8_32 = 0;
	if (!send_cmd(CMD_OPEN_FILE, 2000000UL)) return -1;
	return (int32_t)MARS_COMM8_32;
}

/* CMD 38 with the destination (COMM2 bit 15, COMM8 = cart offset): CMD 46 / 47 then use it */
static int32_t open_at(int id, uint32_t cart_offset)
{
	MARS_COMM2 = (uint16_t)(id | 0x8000);
	MARS_COMM8_32 = cart_offset;
	if (!send_cmd(CMD_OPEN_FILE, 2000000UL)) return -1;
	return (int32_t)MARS_COMM8_32;
}

int32_t plat_load_pack_at(int id, uint32_t cart_offset, uint32_t max_bytes)
{
	const int32_t l = plat_pack_open(id);
	if (l <= 0) { plat_log("LEVEL PACK: NOT ON THE DISC"); return -1; }
	const uint32_t len = (uint32_t)l;
	if (len > max_bytes) { plat_log("LEVEL PACK: TOO BIG FOR THE CART"); return -2; }
	loading_screen();
	if (open_at(id, cart_offset) != l) { plat_log("LEVEL PACK: REOPEN FAILED"); return -1; }
	MARS_COMM8_32 = len;
	const uint32_t chunks = (len + 32767) >> 15;
	int bad = 0;
	for (uint32_t c = 0; c < chunks; c++)
	{
		MARS_COMM2 = (uint16_t)(c | 0x8000);   /* no read-back */
		MARS_COMM12 = 0;
		if (!send_cmd_rv(CMD_CHUNK_TO_CART, 2000000UL) || MARS_COMM12 != 0)
			bad++;
		for (int i = 0; i < 2; i++) { splash_bar(c + 1, chunks); flip_wait(); }
	}
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge the cache: the 68000 wrote the cart */
	if (bad) { plat_log("LEVEL PACK: BAD CHUNKS"); return -3; }
	return l;
}

int plat_pack_same_as_disc(int id, uint32_t cart_offset, uint32_t len)
{
	if (open_at(id, cart_offset) != (int32_t)len) return 0;
	const uint32_t chunks = (len + 32767) >> 15;
	const uint32_t probe[2] = { 0, chunks - 1 };
	/* R7: a compressed pack starts with every block's packed length: a changed pack changes them,
	 * so its first 32 KB are enough (one seek less when a world is entered) */
	const int probes = id >= PACK_LEVEL_ID0 + PACK_LZ_ADD || chunks == 1 ? 1 : 2;
	for (int i = 0; i < probes; i++)
	{
		MARS_COMM2 = (uint16_t)probe[i];
		MARS_COMM8_32 = len;
		MARS_COMM12 = 0;
		if (!send_cmd_rv(CMD_CHUNK_COMPARE, 2000000UL) || MARS_COMM12 != 0) return 0;
	}
	return 1;
}

int plat_save_put_word(int index, int value);
#define CMD_SAVE_PIECE_RAW 61
int plat_cart_piece_write(int piece, const uint16_t *words, int n)
{
	for (int i = 0; i < n; i++) plat_save_put_word(i, words[i]);
	MARS_COMM2 = (uint16_t)piece;
	MARS_COMM12 = 0;
	send_cmd_rv(CMD_SAVE_PIECE_RAW, 8000000UL);
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;
	ed_piece = -1;                                   /* ED-SAVE: the Sub-CPU's buffer is ours now */
	return MARS_COMM12 != 0;
}

void plat_cart_piece_read(int piece, uint16_t *words, int n)
{
	if (page_now == 1 && ed_save_bank()) {           /* ED-SAVE: page 1's pieces are in the bank */
		ed_fetch(piece);
		for (int i = 0; i < n; i++) words[i] = (uint16_t)(ed_buf[2 * i] << 8 | ed_buf[2 * i + 1]);
		return;
	}
	const volatile uint8_t *p = (const volatile uint8_t *)(0x223F0000u + (uint32_t)piece * 4096u);
	for (int i = 0; i < n; i++) words[i] = (uint16_t)(p[2 * i] << 8 | p[2 * i + 1]);
}

/* the save area (pieces 0-13 of page 1) is only seen with page 1 in window 1 */
void plat_save_area_enter(void)
{
	if (save_depth++ == 0 && page_now != 1) { save_page = page_now; plat_cart_page(1); }
}
void plat_save_area_leave(void)
{
	if (save_depth > 0 && --save_depth == 0 && save_page) { plat_cart_page(save_page); save_page = 0; }
}
#define SAVE_AREA_ENTER() plat_save_area_enter()
#define SAVE_AREA_LEAVE() plat_save_area_leave()
#else
#define SAVE_AREA_ENTER() ((void)0)
#define SAVE_AREA_LEAVE() ((void)0)
#endif

/* ---- ED-SAVE: page 1's save area in the EverDrive's save bank (cd32x/subcpu/main.c CMD 61/62,
 * CART_EDSAVE).  The SH2 cannot see that bank, so with page 1 in window 1 every read of the area
 * goes through the Sub-CPU: CMD 62 brings a 4 KB piece into its buffer (kept here until a write),
 * CMD 57 hands out the words.  The Sub-CPU says once (CMD 62, COMM2 = 0x8000) whether it works
 * that way (0x200): the disc's own program says no and the area is read from the cart as before. */
#define CMD_SAVE_GET  57
#define CMD_SAVE_IN   62
static int ed_bank = -1;                 /* -1 not asked, 0 the cart, 1 the EverDrive's save bank */
static int ed_save_bank(void)
{
	if (ed_bank < 0) {
#if OJ32X_CART_MB
		MARS_COMM2 = 0x8000;                 /* the paged Sub-CPU programs answer CMD 62 */
		MARS_COMM12 = 0;
		ed_bank = send_cmd(CMD_SAVE_IN, 2000000UL) && (MARS_COMM12 & 0x200) ? 1 : 0;
#else
		ed_bank = 0;                         /* the 4 MB disc's program has no CMD 62: the cart */
#endif
	}
	return ed_bank;
}
static void ed_fetch(int piece)
{
	if (piece == ed_piece) return;
	MARS_COMM2 = (uint16_t)piece;
	if (!send_cmd_rv(CMD_SAVE_IN, 2000000UL)) { plat_log("SAVE BANK: PIECE TIMED OUT"); return; }
	for (int i = 0; i < 2048; i++) {
		MARS_COMM2 = (uint16_t)i;
		send_cmd(CMD_SAVE_GET, 400000UL);
		const uint16_t w = MARS_COMM8;
		ed_buf[2 * i] = (uint8_t)(w >> 8);
		ed_buf[2 * i + 1] = (uint8_t)w;
	}
	ed_piece = piece;
}
/* n bytes at off of save-area piece <piece> (page 1 mapped by the caller: SAVE_AREA_ENTER) */
void plat_save_read(int piece, int off, uint8_t *dst, int n)
{
	if (ed_save_bank()) {
		ed_fetch(piece);
		for (int i = 0; i < n; i++) dst[i] = ed_buf[off + i];
		return;
	}
	const volatile uint8_t *p = (const volatile uint8_t *)(0x223F0000u + (uint32_t)piece * 4096u + (uint32_t)off);
	for (int i = 0; i < n; i++) dst[i] = p[i];
}


/* ---- REV: the Genesis plane (port/genbg.h): Sub-CPU command 67 -> the 68000 (genbg_md.s).
 * COMM2 = what, COMM8/10 = its argument, reply COMM12. A load copies the level's file from the
 * cart into video memory (some 70 KB), a start fills the plane: both well under a second. */
#define CMD_GENBG 67

int plat_genbg(int op, uint32_t arg)
{
	MARS_COMM2 = (uint16_t)op;
	MARS_COMM8_32 = arg;
	MARS_COMM12 = 0;
	if (!send_cmd(CMD_GENBG, 4000000UL)) { plat_log("GENBG: COMMAND TIMED OUT"); return -1; }
	return MARS_COMM12;
}

/* ---- R9: the bonus level's floor (Sub-CPU command 71, cd32x/subcpu/main.c bmd_load /
 * bmd_frame, docs/R9_BONUS_FLOOR.md) */
#define CMD_BMD 71
static uint32_t bmd_file;                /* the file's cart offset: loaded again if the Sub-CPU
                                            gave Word RAM back to 1M mode for another command */
static int bmd_send_load(void)
{
	MARS_COMM2 = 1;
	MARS_COMM8_32 = bmd_file;
	MARS_COMM12 = 0;
	if (!send_cmd_rv(CMD_BMD, 8000000UL)) { plat_log("BONUS FLOOR: LOAD TIMED OUT"); return 0; }
	return MARS_COMM12 == 1;
}

int plat_bonusmd_load(const void *file, int hole, uint16_t hole555)
{
	bmd_file = plat_cart_offset_of(file);
	bmd_show = -1;
	if (!bmd_send_load()) { plat_bonusmd_off(); return 0; }
	bmd_hole = hole & 255;
	bmd_hole555 = hole555;
	return 1;
}

/* the frame's floor goes out at the start of the frame and is answered while the SH2 draws
 * the rest; the answer (the buffer) is collected before the swap (present_finish) or before any
 * other command (send_cmd) */
static int bmd_asked, bmd_reload;

static void bmd_collect(void)
{
	if (!bmd_asked) return;
	bmd_asked = 0;
	for (uint32_t n = 0; n < 4000000UL && MARS_COMM0 != 0; n++)
		comm_pause();
	if (MARS_COMM0 != 0) { plat_log("BONUS FLOOR: FRAME TIMED OUT"); bmd_reload = 1; return; }
	const uint16_t r = MARS_COMM12;
	if (r == 1) bmd_show = bmd_show == 1 ? 0 : 1;   /* a new floor: the bit changes */
	else bmd_reload = 1;                 /* 0xFFFF: Word RAM went back to 1M - load again */
}

int plat_bonusmd_frame(int32_t x, int32_t y, int cs, int sn, int step)
{
	if (bmd_hole < 0) return 0;
	bmd_collect();
	pad_collect(40000UL);
	if (bmd_reload) {
		bmd_reload = 0;
		if (!bmd_send_load()) {
			plat_log("BONUS FLOOR: LOST - THE 32X DRAWS IT");
			plat_bonusmd_off();          /* the Genesis display off: the 32X's colours lose bit 15 */
			return 0;
		}
	}
	MARS_COMM2 = (uint16_t)(2 | (step & 15) << 8);
	MARS_COMM8_32 = (uint32_t)((x >> 7) & 0xFFFF) << 16 | (uint32_t)((y >> 7) & 0xFFFF);
	MARS_COMM4 = (uint16_t)cs;
	MARS_COMM12 = (uint16_t)sn;
	MARS_COMM0 = CMD_BMD;
	bmd_asked = 1;
	return 1;
}

void plat_bonusmd_off(void)
{
	if (!bmd_file) return;
	bmd_collect();
	bmd_reload = 0;
	MARS_COMM2 = 3;
	send_cmd_rv(CMD_BMD, 4000000UL);
	bmd_file = 0;
	bmd_hole = -1;
	bmd_show = -1;
	MARS_COMM6 = 0;
}

uint32_t plat_cart_offset_of(const void *p)
{
	return (uint32_t)(uintptr_t)p & 0x3FFFFFu;   /* 0x02xxxxxx / 0x22xxxxxx: the cart */
}

/* ---- the save area in the RAM cart (main.c OL-9: CMD 58 put one word of the Sub-CPU's
 * 4 KB buffer, CMD 61 buffer -> cart 0x3F0000 + COMM2 * 4 KB through the 68000, read back and
 * compared; reply COMM12 1 = ok). The SH2 reads the area directly (port/mars_syscalls.c). */
#define CMD_SAVE_PUT   58
#define CMD_SAVE_PIECE 61

int plat_save_put_word(int index, int value)
{
	MARS_COMM8 = (uint16_t)value;
	MARS_COMM2 = (uint16_t)index;
	return send_cmd(CMD_SAVE_PUT, 400000UL);
}

int plat_save_piece(int piece)
{
	SAVE_AREA_ENTER();
	MARS_COMM2 = (uint16_t)piece;
	MARS_COMM12 = 0;
	send_cmd_rv(CMD_SAVE_PIECE, 8000000UL);
	const int ok = MARS_COMM12 != 0;
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge: the 68000 wrote the cart */
	ed_piece = -1;                             /* ED-SAVE: the Sub-CPU's buffer is ours now */
	SAVE_AREA_LEAVE();
	if (!ok) { plat_log("SAVE: WRITING THE CART SAVE AREA FAILED"); return 0; }
	return 1;
}

/* the port's own settings (0 = FPS counter shown, 1 = A/B swapped): save-area piece 10,
 * written at once when one changes - OpenJazz's settings file has no place for them */
#define PORT_SET_PIECE 10
int plat_port_setting(int key)
{
	uint8_t p[12];
	if (key < 0 || key > 7) return 0;
	SAVE_AREA_ENTER();
	plat_save_read(PORT_SET_PIECE, 0, p, 12);        /* ED-SAVE: through the bank when it is there */
	int v;
	if (p[0] != 'O' || p[1] != 'J' || p[2] != 'P' || p[3] != 'S')
		v = key == 0 ? 1 : 0;           /* nothing saved yet - the FPS counter starts on */
	else
		v = p[4 + key];
	SAVE_AREA_LEAVE();
	return v;
}
void plat_port_setting_set(int key, int value)
{
	if (key < 0 || key > 7 || plat_port_setting(key) == value) return;
	uint8_t v[8];
	for (int i = 0; i < 8; i++) v[i] = (uint8_t)plat_port_setting(i);
	v[key] = (uint8_t)value;
	plat_save_put_word(0, 'O' << 8 | 'J');
	plat_save_put_word(1, 'P' << 8 | 'S');
	for (int i = 0; i < 8; i += 2) plat_save_put_word(2 + i / 2, v[i] << 8 | v[i + 1]);
	plat_save_piece(PORT_SET_PIECE);
}
