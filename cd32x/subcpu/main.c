#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw_md.h"
#include "files.h"

uint16_t gen_lvl2 = 0;

void write_byte(unsigned int dst, unsigned char val) { *(volatile unsigned char *)dst = val; }
void write_word(unsigned int dst, unsigned short val) { *(volatile unsigned short *)dst = val; }
void write_long(unsigned int dst, unsigned int val) { *(volatile unsigned int *)dst = val; }
unsigned char read_byte(unsigned int src) { gen_lvl2++; return *(volatile unsigned char *)src; }
unsigned short read_word(unsigned int src) { return *(volatile unsigned short *)src; }
unsigned int read_long(unsigned int src) { return *(volatile unsigned int *)src; }
void scd_init_pcm(void) {}
void bump_fm(void) { gen_lvl2++; }

int mystrlen(const char* string) {
    int rc = 0;
    while (*(string++)) rc++;
    return rc;
}

extern int sh2_app_length;
extern char sh2_app_start[];
extern char Sub_Start[];
extern char Sub_End[];
extern uint16_t InitCD(void);
extern volatile int scd_last_timeout_code;

extern int64_t scd_open_file(const char *name);
extern void scd_read_sectors(void *ptr, int lba, int len, void (*wait)(void));
extern void scd_stream_cmd(char cmd); 
extern void scd_sub_read(void *ptr, int lba, int len);
extern int  scd_cd_status(void);                           
extern int  scd_track_info(int track);                     
extern void scd_cdda_play(int track);                       
extern int  scd_bios_call(int fn, int d1, void *a0);       

/* STREAM (OpenJazz R7, Hurrican P2b): the chunks of a file (CMD 46 / 47 and the boot loads) come from a drive that
   keeps reading: cd_stream_read starts it once (ROMREADN to the end of the file) and takes the
   sectors as they come, checking each one's header; a missed or wrong sector starts it again at
   the sector wanted. Anything else that uses the drive (other reads, opening a file, the CD
   audio, any BIOS call) stops the stream first (the macros below), so it starts afresh. */
extern void scd_stream_start(int lba, int count);
extern int  scd_stream_sector(void *buf, void *hdr, int spins);
static int st_next = -1, st_end;               /* the drive delivers st_next .. st_end - 1 */
static uint32_t st_restarts;                   /* diagnostics: how often it had to start again */
#define scd_sub_read(p, l, n)  (st_next = -1, scd_sub_read(p, l, n))
#define scd_open_file(n)       (st_next = -1, scd_open_file(n))
#define scd_bios_call(f, d, a) (st_next = -1, scd_bios_call(f, d, a))
static uint32_t st_low, st_high, st_err;        /* diagnostics */
static int unbcd(int v) { return (v >> 4) * 10 + (v & 15); }
static void cd_stream_read(uint8_t *dst, int lba, int n, int file_end)
{
    int fails = 0;
    while (n > 0) {
        if (lba != st_next || lba >= st_end) {
            int count = file_end - lba;
            if (count < n) count = n;
            scd_stream_start(lba, count);
            st_next = lba; st_end = lba + count;
            st_restarts++;
        }
        uint8_t hdr[4];
        const int r = scd_stream_sector(dst, hdr, 400000);
        const int got = unbcd(hdr[0]) * 4500 + unbcd(hdr[1]) * 75 + unbcd(hdr[2]) - 150;
        if (r == 0 && got < lba && got >= lba - 64) {    /* an earlier sector (the drive's run-up): skip it */
            st_low++;
            continue;
        }
        if (r != 0 || got != lba) {                      /* failed, or sectors were missed */
            if (r != 0) st_err++; else st_high++;
            st_next = -1;                                /* start again at this sector */
            if (++fails > 64) { scd_sub_read(dst, lba, n); return; }   /* the old way */
            continue;
        }
        st_next++; lba++; n--; dst += 2048;
    }
}

/* STREAM: read-ahead. While the 68000 copies a chunk to the cart (and reads it back), and while
   the Sub-CPU waits for the next command, the drive goes on delivering sectors; they would be
   lost (its buffer holds only a few), so cd_pump takes each one as it arrives into st_buf - the
   start of the next chunk. cd_chunk_read uses what st_buf holds and reads the rest. */
#define ST_RING 32                                   /* sectors (64 KB): two chunks */
static uint8_t st_buf[ST_RING * 2048] __attribute__((aligned(4)));
static int st_buf_lba = -1, st_buf_n, st_buf_pos;      /* sector st_buf_lba is in slot st_buf_pos */
extern void send_md_cmd2(int cmd, int arg1, int arg2);
extern void send_md_cmd3(int cmd, int arg1, int arg2, int arg3);
extern int wait_md_cmd(void);
static void cd_pump(void)
{
    if (st_next < 0 || st_buf_lba < 0 || st_buf_n >= ST_RING || st_next >= st_end ||
        st_next != st_buf_lba + st_buf_n)
        return;
    uint8_t hdr[4];
    const int r = scd_stream_sector(st_buf + ((st_buf_pos + st_buf_n) % ST_RING) * 2048, hdr, 1);
    if (r == -2) return;                                 /* no sector yet */
    const int got = unbcd(hdr[0]) * 4500 + unbcd(hdr[1]) * 75 + unbcd(hdr[2]) - 150;
    if (r == 0 && got < st_next && got >= st_next - 64) { st_low++; return; }   /* run-up */
    if (r != 0 || got != st_next) { st_next = -1; return; }                    /* lost: start again when asked */
    st_buf_n++; st_next++;
}
/* a 68000 command that pumps the drive while it runs */
static int md_cmd3_pumping(int cmd, int a1, int a2, int a3)
{
    send_md_cmd3(cmd, a1, a2, a3);
    while (read_byte(0xFF800E)) cd_pump();               /* 0xFF800E: the 68000 is busy */
    return wait_md_cmd();
}
extern void send_md_cmd0(int cmd);
extern void send_md_cmd1(int cmd, int arg1);
extern void send_md_cmd4(int cmd, int arg1, int arg2, int arg3, int arg4);
static int md_wait_pumping(void)
{
    while (read_byte(0xFF800E)) cd_pump();
    return wait_md_cmd();
}
static int md_cmd0_p(int c) { send_md_cmd0(c); return md_wait_pumping(); }
static int md_cmd1_p(int c, int a) { send_md_cmd1(c, a); return md_wait_pumping(); }
static int md_cmd2_p(int c, int a, int b) { send_md_cmd2(c, a, b); return md_wait_pumping(); }
static int md_cmd4_p(int c, int a, int b, int d, int e) { send_md_cmd4(c, a, b, d, e); return md_wait_pumping(); }
/* from here on every 68000 command keeps the drive's sectors coming (the main loop's commands) */
#define do_md_cmd0 md_cmd0_p
#define do_md_cmd1 md_cmd1_p
#define do_md_cmd2 md_cmd2_p
#define do_md_cmd3 md_cmd3_pumping
#define do_md_cmd4 md_cmd4_p
/* R7: the disc's root directory read once and kept: opening a file (CMD 38) took about 0.7 s
   (the BIOS reads the directory from the disc every time), now no disc read. A name not found
   here goes to the BIOS as before. */
static uint8_t dir_buf[24 * 2048] __attribute__((aligned(4)));
static int dir_len = -1;
static uint32_t disc_stamp;      /* R7: the disc's build (a hash of the volume's creation time) */
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void cd_dir_load(void)
{
    if (dir_len < 0) {
        dir_len = 0;
        scd_sub_read(dir_buf, 16, 1);                              /* the volume descriptor */
        if (dir_buf[1] == 'C' && dir_buf[2] == 'D' && dir_buf[3] == '0' && dir_buf[4] == '0' && dir_buf[5] == '1') {
            /* R7: every build of the disc gets a new creation time (bytes 813-829): the stamp the
               cart pages note beside each pack, so a pack copied from this disc needs no disc
               compare later (CMD 70) */
            uint32_t h = 2166136261u;
            for (int k = 813; k < 830; k++) h = (h ^ dir_buf[k]) * 16777619u;
            disc_stamp = h ? h : 1;
            const uint32_t lba = le32(dir_buf + 156 + 2), len = le32(dir_buf + 156 + 10);
            if (len > 0 && len <= sizeof dir_buf) {
                scd_sub_read(dir_buf, (int)lba, (int)((len + 2047) / 2048));
                dir_len = (int)len;
            }
        }
    }
}
static int64_t cd_open(const char *name)
{
    cd_dir_load();
    const int nl = (int)strlen(name);
    for (int p = 0; p < dir_len; ) {
        const uint8_t *e = dir_buf + p;
        if (e[0] == 0) { p = (p / 2048 + 1) * 2048; continue; }   /* the rest of this sector is empty */
        const int l = e[32];
        if (!(e[25] & 2) && l >= nl && memcmp(e + 33, name, (size_t)nl) == 0 && (l == nl || e[33 + nl] == ';'))
            return (int64_t)le32(e + 10) << 32 | le32(e + 2);
        p += e[0];
    }
    return scd_open_file(name);
}

static void cd_chunk_read(uint8_t *dst, int lba, int n, int file_end)
{
    int k = 0;
    if (st_buf_lba >= 0 && lba >= st_buf_lba && lba < st_buf_lba + st_buf_n) {
        const int skip = lba - st_buf_lba;               /* sectors read ahead that are not wanted */
        st_buf_pos = (st_buf_pos + skip) % ST_RING; st_buf_lba = lba; st_buf_n -= skip;
        k = st_buf_n < n ? st_buf_n : n;
        for (int i = 0; i < k; i++)
            memcpy(dst + i * 2048, st_buf + ((st_buf_pos + i) % ST_RING) * 2048, 2048);
        st_buf_pos = (st_buf_pos + k) % ST_RING; st_buf_lba += k; st_buf_n -= k;
    }
    if (k < n) {
        cd_stream_read(dst + k * 2048, lba + k, n - k, file_end);
        st_buf_lba = lba + n; st_buf_n = 0; st_buf_pos = 0;   /* the read-ahead goes on from here */
    }
}

/* 68K VBlank Tick Macro. Sub-CPU accesses Main CPU Comm Registers via 0xFF8000 */
#define GET_TICKS read_long(0xFF801C)

#define SUB_MAIN_FLAG  0xFF800E
#define SUB_SUB_FLAG   0xFF800F

static void fourcc(char *out, unsigned int v)
{
    int i, ok = 1;
    for (i = 0; i < 4; i++) {
        unsigned char c = (v >> (24 - 8 * i)) & 0xFF;
        if (c < 0x20 || c > 0x7E) ok = 0;
    }
    if (ok) {
        for (i = 0; i < 4; i++) out[i] = (v >> (24 - 8 * i)) & 0xFF;
        out[4] = 0;
    } else {
        sprintf(out, "%08X", v);
    }
}

extern int do_md_cmd0(int cmd);
extern int do_md_cmd1(int cmd, int arg1);
extern int do_md_cmd2(int cmd, int arg1, int arg2);
extern int do_md_cmd3(int cmd, int arg1, int arg2, int arg3);

#define MD_CMD_CLEAR_B        4
#define MD_CMD_CLEAR_A        8
#define MD_CMD_READ_32X_16    19
#define MD_CMD_WRITE_32X_16   20
#define MD_CMD_READ_32X_32    21
#define MD_CMD_WRITE_32X_32   22
#define MD_CMD_COPY_FROM_CART 24
#define MD_CMD_COPY_WORDS     25
#define MD_CMD_SAFE_CART_XFER 26   
#define MD_CMD_SAFE_CART_XFER_W 27     
#define MD_CMD_CHILLY_MEMCPY  32         
#define MD_CMD_CHILLY_RV_MEMCPY 33       
#define MD_CMD_BRAM_PUT       28         
#define MD_CMD_BRAM_TO_FB     29         
#define MD_CMD_BRAM_GET       30         
#define MD_CMD_INSTALL_EXC    31         /* HW1: 68K exception trap (K29), prints 68K EXC in red */
#define MD_CMD_SET_VDP_REG    34
#define MD_CMD_START_D32XR    35         /* D32X M1: 68K -> D32XR's _start (never returns) */
#define MD_CMD_SET_VSRAM      36         /* CD32X ledger additions: one VSRAM word */
#define MD_CMD_GENBG_LOAD     37         /* REV: Genesis plane - a level's data (cart offset) */
#define MD_CMD_GENBG_START    38         /* REV: shown, following the camera (x << 16 | y) */
#define MD_CMD_GENBG_OFF      39         /* REV: off, the listener's own VDP set-up back */
#define MD_CMD_GENBG_CELL     40         /* REV: a map tile changed (cell << 16 | tile) */
#define MD_CMD_RV_COPY_FAST   41         /* R7: cart <-> Word RAM with movem (hw_md.s rv_copy_fast) */
/* KOBO K19: hw_md.s set_vdp_reg - one VDP register word */

/* KOBO K19 - GREEN DIAGNOSTIC TEXT.  1 = printed as before (C on the pad shows/hides it).
   0 = print_diag() prints nothing at all: every print_diag() call stays in the code, so
   setting 1 again brings all the text back.  (Chilly's white 32X start-up messages come
   from hw_md.s and are not affected.) */
#define KOBO_DIAG_TEXT        0          /* OJ32X: off for the release (1 while debugging the boot) */

/* K30 - BOOT TEXT.  0 = the Genesis display is switched OFF at the very start of main(), so
   neither the green text nor Chilly's white 32X start-up messages appear (dev request): the
   SEGA logo is followed by the splash.  The text is still written to video RAM, so setting 1
   (or pressing C in the game) shows it.  Set 1 when debugging a start-up problem. */
#define KOBO_HW_QUIET         0          /* HW9: 1 = once the game reports 4B10, the Sub-CPU stops polling
                                                   the 68K; it only writes its heartbeat twice a second. */
static int sub_quiet = 0;
static int ol_game_running = 0;   /* OL-2b: STATE 4B10 seen - no more per-pass text */
#define KOBO_BOOT_TEXT        1          /* OJ32X: on - Chilly's white 32X start-up messages show (K31)
                                                   (green text stays off: KOBO_DIAG_TEXT 0) */

#define LEGACY_68K_CART 0

static uint32_t saved_lba = 0;
/* K10: length of the file opened by CMD 38, kept here (like saved_lba) so CMD 50 never has
   to trust COMM8 - in K9 CMD 50 read a wrong length from COMM8 and read 0 sectors. */
static uint32_t saved_len = 0;
static int quiet = 0;      
#define SLIDESHOW_DEBUG 1

static const char *find_sh2_marker(const char *p, int len)
{
    int i;
    for (i = 0; i + 8 <= len; i++)
        if (p[i] == 'S' && p[i+1] == 'H' && p[i+2] == '2' && p[i+3] == '-' && p[i+4] == 'V')
            return p + i + 5;
    return 0;
}

void print_diag(char *temp, const char *msg, int x, int y) {
    if (quiet) return;
#if !KOBO_DIAG_TEXT
    return;                                   /* K19: green text switched off (see top) */
#endif
    strcpy(temp, msg);
    switch_banks();
    do_md_cmd4(MD_CMD_PUT_STR, 0x200000, TEXT_GREEN, x, y);
    switch_banks();
}

/* =====================================================================================
 * KOBO STORAGE MODE - must match d32xr-master/sh2_main.c KOBO_NOCART
 *
 *   1 = NO-CART (development / Fusion): the RAM cart is never touched - not even the
 *       start-up cart check (accessing a missing cart crashes the console).  The SH2 reads
 *       KOBOGFX.BIN straight from CD with CMD 38 (open) + CMD 50 (one 32 KB chunk into the
 *       frame buffer).  Runs in Fusion and on the console, cart plugged in or not.
 *   0 = CART (real hardware + the cart maker cart): KOBOGFX.BIN is stored in the cart at
 *       offset 2 MB with CMD 38 + CMD 46 (Chilly's rules, verified), the SH2 reads it at
 *       0x22000000.  Proven on hardware.
 * ===================================================================================== */
#define KOBO_NOCART     0          /* HW1: CART MODE - real hardware WITH the cart plugged in */

#define CART_TARGET     0              /* the cart maker cart layout (only used when KOBO_NOCART 0) */
#define KOBO_PACK_NAME  "KOBOGFX.BIN"
#define KOBO_PACK_SLOT  16             /* cart mode: 16 * CART_SLOT = 2 MB into the 4 MB cart */
#define CART_WINDOW_READS    1   
#define CART_VERIFY_READBACK 1   

#define CART_IMG_BASE   0x000000
#define CART_SLOT       0x20000
#define CMD_CHUNK_TO_CART 46
#define CMD_CHUNK_COMPARE 47       /* D32X M1c: CMD 46 without writing - is the chunk already in the cart? */
#define CMD_CART_TO_FB    47             
static int cur_img = 0;
#ifdef CART_PAGED
/* R5 (8 / 16 MB cart test builds, docs/CART_8MB_16MB.md): CMD 38 with COMM2 bit 15 takes the
   cart destination from COMM8L (cur_dest, used by CMD 46 / 47 instead of cur_img's slot);
   CMD 68 writes window 1's page register (the shared proposal, variant B: the listener's
   safe_cart_xfer (routine 26: interrupts off, RV = 1), one even byte to 0xA130F2, as the ledger's
   section 7 registers it - no listener change). */
static uint32_t cur_dest = 0xFFFFFFFFu;
/* ED-SAVE (CART_EDSAVE, the EverDrive launcher's program only - build.sh EDCD_SAVE=1): page 1's save
   area (pieces 0-15, cart 0x3F0000 + piece * 4 KB) lives in the EverDrive's save bank instead - bank 31,
   kept on the SD card - which the listener (MODE1-7, M1SAVE=1) reaches as cart 0x800000: CMD 61 writes
   it there while window 1 shows page 1, CMD 62 brings a piece of it into the 4 KB buffer for the SH2
   (CMD 57).  The EverDrive wipes its PSRAM at its menu, so nothing in page 1 itself would outlive a
   relaunch (OpenLara CD32X EDSAVE-1; CD32X BootCamp docs/12). */
static int ed_page = 1;                            /* what window 1 shows (1 after the boot) */
/* MicroNut99, 26 Oct 2026: "bank 31 belongs to the battery. do not touch it. Follow Lara."  So the
   save bank holds the SAVE and nothing else.  OpenJazz's saves are pieces 0-10 (0 the settings file
   both games share, 1-4 SAVE.0-3, 5-9 Holiday Hare's, 10 the port's own settings).  Pieces 14 and 15
   are the cart cache's ledger and its board probe marker (port/cartcache.c) - working notes about
   PSRAM that the EverDrive wipes anyway, which have no business in battery-backed memory.  They stay
   in the cart, where they always were. */
#define SAVE_PIECE_TOP 10
/* ED-PAGE (26 Oct 2026, measured on the console with CD32X-BootCamp/edpage):
   the EverDrive IGNORES the one even byte to 0xA130F2 that the 16MB-PCB cart's page register wants,
   and TAKES a word write to CTRL4-7 (0xA130F8..FE = the four 512 KB windows of cart 0x200000-
   0x3FFFFF).  The boot stack's listener is meant to translate that byte (MODE1-9, m1_page) and the
   same probe showed m1_page itself working when called directly - but the EverDrive builds never
   switched a page, which is why they loaded to completion and then found nothing where they put it:
   with no switch every page's data goes to whatever was mapped at reset, and each chunk still
   verifies because the read-back lands in the same wrong place.
   So an ED build does not ask for the byte at all.  It asks the listener for the four word writes
   directly, which go through its ordinary cart-write path (m1_cart -> m1_rvcpy -> m1_cpy_w: RV = 1,
   W on for the write) - the operation the probe proved.  Nothing in the stack changes; a 4 / 8 /
   16 MB build is untouched.
   The command is 27 (SAFE_CART_XFER_W), not 26: 26 asks the listener for BYTES, and the only thing
   that would promote an all-even byte copy to words is OL_M1WORDS, which defaults to 0 and is not
   passed when these launchers are built - so 26 would have written each register as two byte
   writes, and an even-byte write to a CTRL register is precisely what the EverDrive ignores.  27
   sets d3 = 1 in the listener, so m1_cpy makes one move.w per register.  A 2-byte length also
   misses m1_page's interception, which wants exactly 1. */
static void cart_page_write(int page)
{
    volatile uint8_t *wr = (volatile uint8_t *)0x0C0000;
    ed_page = page;
#ifdef CART_ED
    {
        int k;
        for (k = 0; k < 4; k++) {
            wr[0] = 0;                             /* the bank as a WORD, in Word RAM */
            wr[1] = (uint8_t)(page * 4 + k);       /* page p -> banks 4p..4p+3 */
            switch_banks();                        /* Word RAM -> 68000 (0x200000) */
            do_md_cmd3(MD_CMD_SAFE_CART_XFER_W, 0x200000, 0xA130F8 + 2 * k, 2);   /* 27: WORDS */
            switch_banks();
        }
    }
#else
    wr[0] = (uint8_t)page;                         /* the byte, in Word RAM */
    switch_banks();                                /* Word RAM -> 68000 (0x200000) */
    do_md_cmd3(MD_CMD_SAFE_CART_XFER, 0x200000, 0xA130F2, 1);   /* ledger section 7: routine 26 (interrupts off, RV = 1) */
    switch_banks();
#endif
}

/* R7 - CMD 69 "unpack" (OpenJazz docs/R7_COMPRESSED_PACKS.md): a compressed pack (tools/ojlz.c:
   "OJLZ", the unpacked length, the blocks' packed lengths, then blocks that unpack to 32 KB
   each, standing alone) from one cart page into another, both through window 1:
     COMM2 = source page | destination page << 8, COMM8L = the source's cart address (window 1:
     0x200000-0x3FFFFF), COMM12 = the destination's cart address / 32 KB.
   Reply: COMM8L = the unpacked length (0: failed), COMM12 = 0 done, 1 not a compressed pack.
   Window 1 shows the destination page afterwards. Per two blocks the 68000 copies the packed
   bytes from the cart into Word RAM (routine 27: words, interrupts off, RV = 1), the Sub-CPU
   unpacks them into the same bank (lz4_block), and the 68000 copies the 64 KB into the cart. */
extern void lz4_block(const void *src, int len, void *dst);
__asm__(
    "       .text\n"
    "       .align  2\n"
    "       .globl  lz4_block\n"
    "lz4_block:\n"                             /* (src, len, dst): one block (offsets < 32 KB) */
    "       movem.l %d2-%d5/%a2-%a3,-(%sp)\n"
    "       movea.l 28(%sp),%a0\n"
    "       move.l  32(%sp),%d0\n"
    "       movea.l 36(%sp),%a1\n"
    "       lea     0(%a0,%d0.l),%a2\n"
    "       moveq   #63,%d4\n"
    "1:     dbra    %d4,0f\n"                   /* every 64 tokens: keep the 68000 busy (md_service) */
    "       moveq   #63,%d4\n"
    "       movem.l %d0-%d3/%a0-%a3,-(%sp)\n"
    "       jsr     md_service\n"
    "       movem.l (%sp)+,%d0-%d3/%a0-%a3\n"
    "0:     moveq   #0,%d1\n"
    "       move.b  (%a0)+,%d1\n"
    "       move.w  %d1,%d2\n"
    "       lsr.w   #4,%d2\n"
    "       beq.b   5f\n"
    "       cmpi.w  #15,%d2\n"
    "       bne.b   3f\n"
    "2:     moveq   #0,%d3\n"
    "       move.b  (%a0)+,%d3\n"
    "       add.w   %d3,%d2\n"
    "       cmpi.b  #-1,%d3\n"
    "       beq.b   2b\n"
    "       move.w  %a0,%d0\n"                   /* a long literal run: words when a0, a1 share parity */
    "       move.w  %a1,%d3\n"
    "       eor.w   %d3,%d0\n"
    "       btst    #0,%d0\n"
    "       bne.b   3f\n"
    "       btst    #0,%d3\n"
    "       beq.b   20f\n"
    "       move.b  (%a0)+,(%a1)+\n"
    "       subq.w  #1,%d2\n"
    "20:    move.w  %d2,%d0\n"
    "       lsr.w   #2,%d0\n"
    "       subq.w  #1,%d0\n"
    "21:    move.l  (%a0)+,(%a1)+\n"
    "       dbra    %d0,21b\n"
    "       andi.w  #3,%d2\n"
    "       beq.b   5f\n"
    "3:     subq.w  #1,%d2\n"
    "30:    move.b  (%a0)+,(%a1)+\n"
    "       dbra    %d2,30b\n"
    "5:     cmpa.l  %a2,%a0\n"
    "       bhs.w   9f\n"
    "       move.b  1(%a0),-(%sp)\n"            /* offset, little-endian: the high byte through the stack */
    "       move.w  (%sp)+,%d5\n"
    "       move.b  (%a0),%d5\n"
    "       addq.l  #2,%a0\n"
    "       movea.l %a1,%a3\n"
    "       suba.w  %d5,%a3\n"
    "       andi.w  #15,%d1\n"
    "       cmpi.w  #15,%d1\n"
    "       bne.b   7f\n"
    "6:     moveq   #0,%d2\n"
    "       move.b  (%a0)+,%d2\n"
    "       add.w   %d2,%d1\n"
    "       cmpi.b  #-1,%d2\n"
    "       beq.b   6b\n"
    "       btst    #0,%d5\n"                    /* a long match, even offset >= 4: longs */
    "       bne.b   7f\n"
    "       cmpi.w  #4,%d5\n"
    "       blo.b   7f\n"
    "       addq.w  #4,%d1\n"
    "       move.w  %a1,%d0\n"
    "       btst    #0,%d0\n"
    "       beq.b   60f\n"
    "       move.b  (%a3)+,(%a1)+\n"
    "       subq.w  #1,%d1\n"
    "60:    move.w  %d1,%d0\n"
    "       lsr.w   #2,%d0\n"
    "       subq.w  #1,%d0\n"
    "61:    move.l  (%a3)+,(%a1)+\n"
    "       dbra    %d0,61b\n"
    "       andi.w  #3,%d1\n"
    "       beq.w   1b\n"
    "       subq.w  #1,%d1\n"
    "62:    move.b  (%a3)+,(%a1)+\n"
    "       dbra    %d1,62b\n"
    "       bra.w   1b\n"
    "7:     move.b  (%a3)+,(%a1)+\n"            /* a match is 4 bytes at least */
    "       move.b  (%a3)+,(%a1)+\n"
    "       move.b  (%a3)+,(%a1)+\n"
    "       move.b  (%a3)+,(%a1)+\n"
    "       subq.w  #1,%d1\n"
    "       bmi.w   1b\n"
    "70:    move.b  (%a3)+,(%a1)+\n"
    "       dbra    %d1,70b\n"
    "       bra.w   1b\n"
    "9:     movem.l (%sp)+,%d2-%d5/%a2-%a3\n"
    "       rts\n"
);
#define UNPACK_IN   0x0C0000                   /* Sub-CPU side of a bank: the packed blocks */
#define UNPACK_OUT  0x0D0000                   /* and two unpacked blocks (64 KB) */
/* The 68000's part runs beside the Sub-CPU's: a queue of 68000 commands, sent one after the other
   while the Sub-CPU unpacks (md_service between blocks). The page register byte comes from the
   fourth argument register (0xA1202C, the byte the 68000 copies), so a page change needs no
   Word RAM and no bank switch. */
typedef struct { int cmd, a1, a2, a3, a4; } MdJob;
static MdJob md_q[32];                          /* a batch queues up to 12 */
static int md_qh, md_qt, md_busy;
static void md_push(int cmd, int a1, int a2, int a3, int a4)
{
    MdJob *j = &md_q[md_qt++ & 31];
    j->cmd = cmd; j->a1 = a1; j->a2 = a2; j->a3 = a3; j->a4 = a4;
}
void md_service(void);
void md_service(void)
{
    if (md_busy) {
        if (read_byte(0xFF800E)) return;               /* the 68000 is still on it */
        wait_md_cmd();
        md_busy = 0;
    }
    if (md_qh != md_qt) {
        const MdJob *j = &md_q[md_qh++ & 31];
        send_md_cmd4(j->cmd, j->a1, j->a2, j->a3, j->a4);
        md_busy = 1;
    }
}
static void md_drain(void) { while (md_busy || md_qh != md_qt) md_service(); }
#ifdef CART_ED
/* ED-PAGE, the queued form: four word writes to CTRL4-7 instead of the one byte the EverDrive
   ignores.  send_md_cmd4 puts the 4th argument as a long at 0xA1202C, so bank << 16 leaves the bank
   in the WORD there; command 27 makes the listener copy it as a word. */
static void md_page(int page)
{
    int k;
    ed_page = page;
    for (k = 0; k < 4; k++)
        md_push(MD_CMD_SAFE_CART_XFER_W, 0xA1202C, 0xA130F8 + 2 * k, 2, (page * 4 + k) << 16);   /* 27: WORDS */
}
#else
static void md_page(int page) { ed_page = page; md_push(MD_CMD_SAFE_CART_XFER, 0xA1202C, 0xA130F2, 1, page << 24); }
#endif

static void md_copy(uint32_t src, uint32_t dst, uint32_t len)   /* 16 KB per command */
{
    for (uint32_t p = 0; p < len; p += 0x4000)
        md_push(MD_CMD_RV_COPY_FAST, (int)(src + p), (int)(dst + p), (int)(len - p < 0x4000 ? len - p : 0x4000), 0);
}
static uint32_t unpack_pack(int spage, int dpage, uint32_t src, uint32_t dst)
{
    static uint16_t sizes[128];
    /* the header */
    md_page(spage);
    md_copy(0x400000 + src, 0x200000, 512);
    md_drain();
    switch_banks();
    const volatile uint8_t *h = (const volatile uint8_t *)UNPACK_IN;
    if (h[0] != 'O' || h[1] != 'J' || h[2] != 'L' || h[3] != 'Z') { md_page(dpage); md_drain(); return 0; }
    const uint32_t raw = (uint32_t)h[4] << 24 | (uint32_t)h[5] << 16 | (uint32_t)h[6] << 8 | h[7];
    const int nb = h[8] << 8 | h[9];
    if (nb < 1 || nb > 128 || 12 + 2 * nb > 512) { md_page(dpage); md_drain(); return 0; }
    for (int b = 0; b < nb; b++) sizes[b] = (uint16_t)(h[12 + 2 * b] << 8 | h[13 + 2 * b]);
    /* batches: one or two blocks, their packed bytes 64 KB at most */
    static int bfirst[129];
    static uint32_t bat[129];
    int nbatch = 0;
    uint32_t at = src + 12 + 2 * (uint32_t)nb;
    for (int b = 0; b < nb; ) {
        int e = b + 1;
        uint32_t len = (sizes[b] + 1u) & ~1u;
        if (e < nb && len + ((sizes[e] + 1u) & ~1u) <= 0x10000) len += (sizes[e++] + 1u) & ~1u;
        bfirst[nbatch] = b; bat[nbatch] = at; nbatch++;
        at += len; b = e;
    }
    bfirst[nbatch] = nb; bat[nbatch] = at;
    /* batch 0's packed bytes into the 68000's bank, then the bank to the Sub-CPU */
    md_page(spage);
    md_copy(0x400000 + bat[0], 0x200000, bat[1] - bat[0]);
    md_drain();
    switch_banks();
    uint32_t out = dst, prev_n = 0;
    for (int i = 0; i < nbatch; i++) {
        /* the 68000, in its bank: the last batch's unpacked bytes out, the next batch's packed in */
        if (i > 0) { md_page(dpage); md_copy(0x210000, 0x400000 + out, (prev_n + 1) & ~1u); out += prev_n; }
        if (i + 1 < nbatch) { md_page(spage); md_copy(0x400000 + bat[i + 1], 0x200000, bat[i + 2] - bat[i + 1]); }
        md_service();
        /* the Sub-CPU, in its bank: this batch */
        uint32_t in = UNPACK_IN, n = 0;
        for (int k = bfirst[i]; k < bfirst[i + 1]; k++) {
            const uint32_t braw = (uint32_t)(k + 1) * 32768u <= raw ? 32768u : raw - (uint32_t)k * 32768u;
            lz4_block((const void *)in, sizes[k], (void *)(UNPACK_OUT + n));
            md_service();
            in += (sizes[k] + 1u) & ~1u;
            n += braw;
        }
        md_drain();
        switch_banks();
        prev_n = n;
    }
    md_page(dpage);
    md_copy(0x210000, 0x400000 + out, (prev_n + 1) & ~1u);
    md_drain();
    return raw;
}
#endif

/* ---- R9: the bonus level's floor on the graphics chip (docs/R9_BONUS_FLOOR.md) ----------
 * CMD 71, COMM2 = op | step << 8:
 *   1 LOAD  COMM8 = cart offset of BONUSMD.BIN (tools/bonus_md/make_bonus_md.py): Word RAM to
 *           2M, the stamps, the map and the header copied in by the 68000, the VDP set up.
 *           Reply COMM12 = 1 (0: not a BONUSMD v2 file).
 *   2 FRAME COMM8 = x << 16 | y (13.3 pixels, the player), COMM4 = cos, COMM12 = sin (10-bit
 *           fractions as OpenJazz's fCos / fSin), step = the colour rotation's position (0-15).
 *           The layer(s) drawn (the file says 1 or 2), queued for the 68000's next vblank; reply COMM12 = 1, or 0xFFFF
 *           when the floor is not loaded (another command needed Word RAM: LOAD again).
 *   3 OFF   back to 1M, the Genesis display off.
 * Word RAM in 2M mode (Sub-CPU 0x080000, 68000 0x200000): */
#define BMD_WR      0x080000
#define BMD_MAP_A   0x28000      /* the chip's stamp maps: 8 x 8 stamps (256 x 256 pixels, */
#define BMD_MAP_B   0x28080      /* repeated) around the part of the floor the view shows */
#define BMD_IMG_A   0x30000      /* plane A's picture: 40 x 11 cells, columns of 11 */
#define BMD_IMG_B   0x34000      /* plane B's */
#define BMD_TRACE   0x38000      /* 85 lines x (x, y, dx, dy) */
#define BMD_HEAD    0x3A000      /* the file's first 1024 bytes */
#define BMD_GRIDTMP 0x28000      /* the map (64 KB) on its way into bmd_grid (load only) */
#define BMD_ROWS    85
#define MD_CMD_BMD_SETUP 42
#define MD_CMD_BMD_QUEUE 43
#define MD_CMD_BMD_OFF   44
static int bmd_on;
static uint8_t bmd_mode1m;                 /* 0xFF8003 before (1M mode, its bank) */
static uint16_t bmd_tstamp[2][60], bmd_tanim[2][60];
static int bmd_nphase;
static int bmd_layers;                     /* 1: plane A only (16 colours), 2: A and B (31) */
static int32_t bmd_dist[BMD_ROWS];         /* image line i: OpenJazz's distance for y = 85 - i */
static int16_t bmd_k[BMD_ROWS];            /* distance / 5: step = k * cos >> 15 */
static uint8_t bmd_grid[65536];            /* the bonus map's tiles */
#define WR8(o)  (*(volatile uint8_t *)(BMD_WR + (o)))
#define WR16(o) (*(volatile uint16_t *)(BMD_WR + (o)))
#define WR32(o) (*(volatile uint32_t *)(BMD_WR + (o)))

/* 16 x 16 -> 32 bits in one instruction (GCC calls __mulsi3 for these, some 250 cycles) */
static inline int32_t bmd_muls(int16_t a, int16_t b)
{
    int32_t r = a;
    __asm__ ("muls.w %1,%0" : "+d" (r) : "dmi" (b));
    return r;
}

static void bmd_own(void) { while (read_byte(0xFF8003) & 1) ; }        /* 2M: RET 0 = ours */
static void bmd_give(void) { write_byte(0xFF8003, read_byte(0xFF8003) | 1); }

static void bmd_suspend(void)
{
    if (!bmd_on) return;
    bmd_own();
    do_md_cmd0(MD_CMD_BMD_OFF);
    write_byte(0xFF8003, bmd_mode1m & ~2);  /* 1M again, the bank as it was */
    while (read_byte(0xFF8003) & 2) ;
    bmd_on = 0;
}

static int bmd_load(uint32_t off)
{
    bmd_suspend();
    bmd_mode1m = read_byte(0xFF8003);
    write_byte(0xFF8003, (bmd_mode1m & ~0x1C) | 1);   /* 2M, no priority mode, to the 68000 */
    /* the header, then the stamps and the map (16 KB a command, RV = 1: the cart at 0x400000);
       the 68000 owns Word RAM: it reads the magic and the counts for us */
    const uint32_t a = 0x400000 + off;
    do_md_cmd3(MD_CMD_RV_COPY_FAST, a, 0x200000 + BMD_HEAD, 1024);
    const uint32_t magic = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0x200000 + BMD_HEAD);
    const uint32_t vn = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0x200000 + BMD_HEAD + 4);
    const uint32_t nst = vn & 0xFFFF;
    if (magic != 0x4F4A424DUL || (vn >> 16) != 2 || nst == 0 || nst > 319) {   /* 'OJBM' v2 */
        write_byte(0xFF8003, bmd_mode1m & ~2);
        while (read_byte(0xFF8003) & 2) ;
        return 0;
    }
    for (uint32_t p = 0; p < nst * 512; p += 0x4000)
        do_md_cmd3(MD_CMD_RV_COPY_FAST, a + 1024 + p, 0x200000 + 512 + p, nst * 512 - p < 0x4000 ? nst * 512 - p : 0x4000);
    for (uint32_t p = 0; p < 0x10000; p += 0x4000)
        do_md_cmd3(MD_CMD_RV_COPY_FAST, a + 1024 + nst * 512 + p, 0x200000 + BMD_GRIDTMP + p, 0x4000);
    do_md_cmd0(MD_CMD_BMD_SETUP);                      /* ends with Word RAM to us */
    bmd_own();
    for (uint32_t o = 0; o < 0x10000; o += 4)
        *(uint32_t *)(bmd_grid + o) = WR32(BMD_GRIDTMP + o);
    for (int t = 0; t < 60; t++) {
        bmd_tstamp[0][t] = WR16(BMD_HEAD + 80 + 2 * t);
        bmd_tanim[0][t] = WR16(BMD_HEAD + 200 + 2 * t);
        bmd_tstamp[1][t] = WR16(BMD_HEAD + 320 + 2 * t);
        bmd_tanim[1][t] = WR16(BMD_HEAD + 440 + 2 * t);
    }
    bmd_layers = WR8(BMD_HEAD + 79) == 2 ? 2 : 1;
    bmd_nphase = WR16(BMD_HEAD + 10);
    if (bmd_nphase < 1 || bmd_nphase > 16) bmd_nphase = 1;
    for (uint32_t o = 0; o < 0x100; o += 4) WR32(BMD_MAP_A + o) = 0;     /* the stamp maps */
    for (uint32_t o = 0; o < 0x8000; o += 4) WR32(BMD_IMG_A + o) = 0;    /* lines 85-87 stay 0 */
    for (int i = 0; i < BMD_ROWS; i++) {
        const int y = BMD_ROWS - i;
        bmd_dist[i] = (int32_t)((800L << 10) / (92 - y));
        bmd_k[i] = (int16_t)(bmd_dist[i] / 5);      /* step (5.11) = d * cos / (160 * 1024) */
    }
    bmd_on = 1;
    return 1;
}

/* one layer's stamp map: the 8 x 8 tiles around (cx, cy) - the floor the view shows is less
   than 80 pixels from there (cx, cy: the tile 45 pixels ahead of the player) */
static void bmd_map(int L, int cx, int cy, int ph, uint32_t map)
{
    const uint16_t *ts = bmd_tstamp[L], *ta = bmd_tanim[L];
    for (int ty = cy - 4; ty < cy + 4; ty++) {
        const uint8_t *g = bmd_grid + ((ty & 255) << 8);
        volatile uint16_t *m = (volatile uint16_t *)(BMD_WR + map + ((ty & 7) << 4));
        for (int tx = cx - 4; tx < cx + 4; tx++) {
            int t = g[tx & 255];
            if (t > 59) t = 59;
            const int st = ta[t] ? ta[t] + ph : ts[t];
            m[tx & 7] = (uint16_t)(st << 2);
        }
    }
}

/* the chip: 32 x 32 stamps, 256 x 256 map, repeated; 320 x 85 into 11-cell columns from the
   trace; started now (it reads the trace a line at a time as it goes) */
static void bmd_start(uint32_t map, uint32_t img)
{
    *(volatile uint16_t *)0xFF8058 = 0x0003;
    *(volatile uint16_t *)0xFF805A = (uint16_t)(map >> 2);
    *(volatile uint16_t *)0xFF805C = 10;
    *(volatile uint16_t *)0xFF805E = (uint16_t)(img >> 2);
    *(volatile uint16_t *)0xFF8060 = 0;
    *(volatile uint16_t *)0xFF8062 = 320;
    *(volatile uint16_t *)0xFF8064 = BMD_ROWS;
    *(volatile uint16_t *)0xFF8066 = (uint16_t)(BMD_TRACE >> 2);
}
static void bmd_wait(void) { while (*(volatile uint16_t *)0xFF8058 & 0x8000) ; }

static int bmd_frame(uint16_t x13, uint16_t y13, int16_t cs, int16_t sn, int step)
{
    if (!bmd_on) return 0xFFFF;
    bmd_own();
    /* both stamp maps first, around the tile 45 pixels ahead (13.3: 360) */
    const int ph = (int)bmd_muls((int16_t)step, (int16_t)bmd_nphase) >> 4;
    const int cx = (uint16_t)(x13 + (bmd_muls(sn, 360) >> 10)) >> 8;
    const int cy = (uint16_t)(y13 - (bmd_muls(cs, 360) >> 10)) >> 8;
    bmd_map(0, cx, cy, ph, BMD_MAP_A);
    if (bmd_layers == 2) bmd_map(1, cx, cy, ph, BMD_MAP_B);
    /* the trace: OpenJazz's JJ1BonusLevel::draw, one line of the floor each; layer A starts
       once the first lines are there (the chip takes some 1600 cycles a line, this loop a few
       hundred) */
    const int32_t X = (int32_t)x13 << 7, Y = (int32_t)y13 << 7;   /* 10-bit fractions */
    volatile uint16_t *tr = (volatile uint16_t *)(BMD_WR + BMD_TRACE);
    for (int i = 0; i < BMD_ROWS; i++) {
        const int32_t d = bmd_dist[i];
        const int16_t d4 = (int16_t)(d >> 2), f4 = (int16_t)((d - 16384) >> 2);
        const int32_t sideX = bmd_muls(d4, cs) >> 8, sideY = bmd_muls(d4, sn) >> 8;
        const int32_t fwdX = X + (bmd_muls(f4, sn) >> 8) - (sideX >> 1);
        const int32_t fwdY = Y - (bmd_muls(f4, cs) >> 8) - (sideY >> 1);
        tr[0] = (uint16_t)(fwdX >> 7);
        tr[1] = (uint16_t)(fwdY >> 7);
        tr[2] = (uint16_t)(bmd_muls(bmd_k[i], cs) >> 15);
        tr[3] = (uint16_t)(bmd_muls(bmd_k[i], sn) >> 15);
        tr += 4;
        if (i == 3) bmd_start(BMD_MAP_A, BMD_IMG_A);
    }
    bmd_wait();
    if (bmd_layers == 2) {
        bmd_start(BMD_MAP_B, BMD_IMG_B);
        bmd_wait();
    }
    bmd_give();
    do_md_cmd0(MD_CMD_BMD_QUEUE);                      /* the 68000's vblank copies it and
                                                          gives Word RAM back */
    return 1;
}

/* OL-2: OpenLara levels.  CMD 38 with file number 100 + LevelID opens the level; CMD 46
   stores it at cart slot 16 (2 MB), where the SH2 reads it (0x02200000).  The ORDER must
   match the engine's enum LevelID (OpenLara-src/src/fixed/common.h). */
#define OL_LEVEL_BASE   100
#define OL_LEVEL_SLOT   16
static const char *const ol_level_files[] = {
    "TITLE.PKD",   "GYM.PKD",     "LEVEL1.PKD",  "LEVEL2.PKD",  "LEVEL3A.PKD",
    "LEVEL3B.PKD", "CUT1.PKD",    "LEVEL4.PKD",  "LEVEL5.PKD",  "LEVEL6.PKD",
    "LEVEL7A.PKD", "LEVEL7B.PKD", "CUT2.PKD",    "LEVEL8A.PKD", "LEVEL8B.PKD",
    "LEVEL8C.PKD", "LEVEL10A.PKD","CUT3.PKD",    "LEVEL10B.PKD","CUT4.PKD",
    "LEVEL10C.PKD"
};
#define OL_LEVEL_COUNT  ((int)(sizeof(ol_level_files) / sizeof(ol_level_files[0])))
static int g_nexc = 0;                   

#define XB(off)   (0x0C0100 + (off))       
#define XM(off)   (0x200100 + (off))       
#define PB(off)   (*(volatile uint8_t *)XB(off))

static void cx(int src, int dst, int len)  
{
    do_md_cmd3(MD_CMD_SAFE_CART_XFER, src, dst, len);
}

static int safe_cart_present(char *temp, char *tbuf)
{
    int ok;
#if CART_TARGET == 1
    uint8_t id, orig, rb;
    PB(0) = 0; PB(1) = 0; PB(2) = 0x5A; PB(3) = 0; PB(4) = 1; PB(5) = 0;
    switch_banks();                              
    cx(0x400001, XM(0), 1);                      
    cx(0x600001, XM(1), 1);                      
    cx(XM(4), 0x7FFFFF, 1);                      
    cx(XM(2), 0x600001, 1);                      
    cx(0x600001, XM(3), 1);                      
    cx(XM(1), 0x600001, 1);                      
    cx(XM(5), 0x7FFFFF, 1);                      
    switch_banks();                              
    id = PB(0); orig = PB(1); rb = PB(3);
    ok = (rb == 0x5A);
    sprintf(tbuf, "BRAM ID:%02X (%uKB) OLD:%02X        ", id, (id & 0x80) ? 0 : (0x2000u << (id & 7)) >> 10, orig);
    print_diag(temp, tbuf, 2, 23);
    sprintf(tbuf, "BRAM @600001: WROTE 5A READ %02X  ", rb);
#else
    uint16_t orig = 0, via9 = 0xFFFF, via4 = 0xFFFF;
    PB(0) = 0x5A; PB(1) = 0xA5; PB(2) = 0; PB(3) = 0;
#if CART_WINDOW_READS
    do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, 0);                       
    orig = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0x900000);
#endif
    switch_banks();
    do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x400000, XM(0), 2);          
    switch_banks();                                                     
    via4 = 0xFFFF;                                                      
#if CART_WINDOW_READS
    via9 = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0x900000);        
    PB(0) = orig >> 8; PB(1) = orig & 0xFF;
    switch_banks();
    do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x400000, XM(0), 2);          
    switch_banks();
#endif
#if !CART_WINDOW_READS && !CART_VERIFY_READBACK
    ok = 1;                                                             
#else
    ok = (via4 == 0x5AA5 || via9 == 0x5AA5);
#endif
    sprintf(tbuf, "OLD:%04X  VIA 400000:%04X      ", orig, via4);
    print_diag(temp, tbuf, 2, 23);
    sprintf(tbuf, "WROTE 5AA5  VIA 900000:%04X    ", via9);
#endif
    print_diag(temp, tbuf, 2, 24);
    print_diag(temp, ok ? "SAFE CART: FOUND               " : "SAFE CART: NOT FOUND           ", 2, 25);
    return ok;
}

static uint16_t cdda_track_word;       

static int bios_wait_ready(unsigned int max_ticks)
{
    (void)max_ticks;
    while (scd_cd_status() & 0x80000000) {
        bump_fm();
    }
    return 1;
}


/* =====================================================================================
 * K33 SOUND EFFECTS - Sega CD PCM chip (RF5C164), driven directly by the Sub-CPU.
 *   KOBOSFX.BIN (tools/kobo_sfx.py, from Kobo's own rendered .agw sounds): a table + a
 *   ready-made 64 KB wave-RAM image, loaded ONCE at boot by CMD 53 (before the music:
 *   no CD reads after the music starts).
 *   Playing: the SH2 puts an effect number in the upper byte of its pad request
 *   (COMM0 = 48 | id << 8); the fast path below starts it - no extra exchange, no 68K.
 *   Channel 7 = the player's gun (restarted each shot), channels 0-6 = round robin.
 *   Chip registers (odd bytes at 0xFF0001..): ENV volume, PAN, FDL/FDH step (0x800 = 32552
 *   Hz), LSL/LSH loop address, ST start page, CTRL (bit 7 sound on, bit 6: 1 = choose a
 *   channel for the registers above, 0 = choose a 4 KB wave-RAM bank), ONOFF (bit = 1 off).
 *   Wave RAM window: 0xFF2001 + 2 * n, n < 4096, of the bank chosen in CTRL.
 * ===================================================================================== */
#define PCM_ENV    0xFF0001
#define PCM_PAN    0xFF0003
#define PCM_FDL    0xFF0005
#define PCM_FDH    0xFF0007
#define PCM_LSL    0xFF0009
#define PCM_LSH    0xFF000B
#define PCM_ST     0xFF000D
#define PCM_CTRL   0xFF000F   /* K35b FIX: register 7 (was 0xFF0011 = the ON/OFF register!)    */
#define PCM_ONOFF  0xFF0011   /* K35b FIX: register 8 (was 0xFF0013 - nothing there).  Order of
                                 the registers: ENV PAN FDL FDH LSL LSH ST CTRL ON/OFF at
                                 0xFF0001, 03 ... 11.  K33/K35 wrote the channel/bank selects
                                 into ON/OFF: channels switched on/off at random and never
                                 stopped cleanly - the shot kept looping (seen in Fusion). */
#define PCM_WAVE   0xFF2001
#define SFX_MAX    31
typedef struct { uint8_t st, vol; uint16_t fd, loop, len; } sfx_t;
static sfx_t sfx_tab[SFX_MAX + 1];            /* [1..count] */
static int sfx_count = 0;
static uint8_t sfx_map[256];                  /* OL-6: engine sample index -> effect */
static int sfx_has_map = 0;
static uint8_t pcm_onoff = 0xFF;              /* shadow: the ONOFF register cannot be read */
static int pcm_next = 0;

static void pcm_play(int id) {
    const sfx_t *e;
    int ch;
    if (id < 1 || id > sfx_count) return;
    e = &sfx_tab[id];
    ch = (id == 1) ? 7 : pcm_next;            /* id 1 = player gun: its own channel */
    if (id != 1) pcm_next = pcm_next < 6 ? pcm_next + 1 : 0;
    pcm_onoff |= (uint8_t)(1 << ch);          /* key off (restart from the start page) */
    write_byte(PCM_ONOFF, pcm_onoff);
    write_byte(PCM_CTRL, 0xC0 | ch);          /* sound on + registers of channel ch */
    write_byte(PCM_ENV, e->vol);
    write_byte(PCM_PAN, 0xFF);
    write_byte(PCM_FDL, e->fd & 0xFF);
    write_byte(PCM_FDH, e->fd >> 8);
    write_byte(PCM_LSL, e->loop & 0xFF);
    write_byte(PCM_LSH, e->loop >> 8);
    write_byte(PCM_ST, e->st);
    pcm_onoff &= (uint8_t)~(1 << ch);         /* key on */
    write_byte(PCM_ONOFF, pcm_onoff);
}

/* CMD 53: KOBOSFX.BIN -> PCM wave RAM.  Returns the number of effects (0 = failed). */
static int pcm_load(const char *name) {           /* OL-6: SFX<level>.BIN */
    int64_t fi = scd_open_file(name);
    uint32_t len, lba, nsec, img, i, b;
    volatile uint8_t *buf = (volatile uint8_t *)0x0C0000;   /* Sub-CPU's Word RAM bank */
    if (fi < 0) return 0;
    len = (uint32_t)(fi >> 32); lba = (uint32_t)(fi & 0xFFFFFFFF);
    nsec = (len + 2047) / 2048;
    if (nsec > 48) nsec = 48;
    scd_sub_read((void *)0x0C0000, lba, nsec);
    if (buf[0] != 'K' || buf[1] != 'S' || buf[2] != 'F' || buf[3] != 'X') return 0;
    sfx_count = (buf[4] << 8) | buf[5];
    img = (uint32_t)((buf[6] << 8) | buf[7]);
    if (img == 0) img = 65536;
    sfx_has_map = 0;                                           /* OL-6: map after the image */
    if (len >= 256 + img + 256) {
        for (i = 0; i < 256; i++) sfx_map[i] = buf[256 + img + i];
        sfx_has_map = 1;
    }
    if (sfx_count > SFX_MAX) sfx_count = SFX_MAX;
    for (i = 0; i < (uint32_t)sfx_count; i++) {
        volatile uint8_t *t = buf + 8 + i * 8;
        sfx_tab[i + 1].st = t[0]; sfx_tab[i + 1].vol = t[1];
        sfx_tab[i + 1].fd = (uint16_t)((t[2] << 8) | t[3]);
        sfx_tab[i + 1].loop = (uint16_t)((t[4] << 8) | t[5]);
        sfx_tab[i + 1].len = (uint16_t)((t[6] << 8) | t[7]);
    }
    pcm_onoff = 0xFF;
    write_byte(PCM_ONOFF, 0xFF);                               /* all channels off */
    for (b = 0; b < 16; b++) {                                 /* 16 banks x 4 KB */
        write_byte(PCM_CTRL, (uint8_t)b);                      /* sound off, wave bank b */
        for (i = 0; i < 4096; i++) {
            uint32_t a = b * 4096 + i;
            write_byte(PCM_WAVE + 2 * i, a < img ? buf[256 + a] : 0xFF);   /* unused = stop */
        }
    }
    write_byte(PCM_CTRL, 0x80);                                /* sound on */
    return sfx_count;
}


/* =====================================================================================
 * K36 BACKUP RAM (Sega CD internal) - high scores + options, one 128-byte file.
 *   BIOS entry _BURAM = 0x5F16: d0 = function, a0/a1 = parameters, carry set = error.
 *   BRMINIT 0 (a0 = 0x640-byte work area, a1 = 12-byte string buffer), BRMREAD 3 (a0 = 11-char
 *   file name, a1 = buffer), BRMWRITE 4 (a0 = name(11) + mode(1: 0 = normal) + blocks(word,
 *   64 bytes each), a1 = data).  All registers are saved around the call.
 *   The SH2 exchanges the 128 bytes one word at a time over COMM (CMD 57 get / 58 put) - no
 *   frame buffer, no Word RAM, only at start-up and after a game.
 * ===================================================================================== */
#define BRAM_BYTES 4096                 /* OL-5: OpenLara settings + saved game */
static uint8_t bram_work[0x640];
static uint8_t bram_str[16];
static uint8_t bram_buf[BRAM_BYTES];
static const char bram_name[12] = "OPENJAZZ_JJ";   /* OJ32X: OpenJazz's own file name (OL-5 had OPENLARA_TR); backup RAM
                                                       is no longer used by OpenJazz - its saves use the cart (CMD 58/61) */
static uint8_t bram_param[14];
static int bram_ready = 0;

static int bram_call(int fn, const void *pa0, void *pa1) {
    register int d0 __asm__("d0") = fn;
    register const void *a0 __asm__("a0") = pa0;
    register void *a1 __asm__("a1") = pa1;
    register int d1 __asm__("d1") = 0;
    __asm__ volatile (
        "movem.l %%d2-%%d7/%%a2-%%a6,-(%%sp)\n\t"
        "jsr 0x5F16.w\n\t"
        "scs %%d1\n\t"                          /* carry (error) -> d1 low byte 0xFF */
        "movem.l (%%sp)+,%%d2-%%d7/%%a2-%%a6"
        : "+d"(d0), "+a"(a0), "+a"(a1), "+d"(d1) : : "cc", "memory");
    return (d1 & 0xFF) ? -1 : d0;
}
static int bram_init(void) {
    if (!bram_ready) bram_ready = bram_call(0, bram_work, bram_str) >= 0 ? 1 : -1;
    return bram_ready > 0;
}
static int bram_load(void) {                    /* 1 = loaded, 0 = no file / no BRAM */
    int i;
    for (i = 0; i < BRAM_BYTES; i++) bram_buf[i] = 0;
    if (!bram_init()) return 0;
    return bram_call(3, bram_name, bram_buf) >= 0 ? 1 : 0;
}
static int bram_save(void) {
    int i;
    if (!bram_init()) return 0;
    for (i = 0; i < 11; i++) bram_param[i] = (uint8_t)bram_name[i];
    bram_param[11] = 0;                         /* normal (not protected) mode */
    bram_param[12] = 0; bram_param[13] = BRAM_BYTES / 64;   /* blocks */
    return bram_call(4, bram_param, bram_buf) >= 0 ? 1 : 0;
}

static int cdda_start(int track, int *st_out, int once)   /* OL-4 play once */
{
    int st = 0;
    bios_wait_ready(0);
    scd_bios_call(0x0089, 0, 0);            
    scd_bios_call(0x0002, 0, 0);            
    scd_bios_call(0x0085, 0x0400, 0);       
    cdda_track_word = (uint16_t)track;
    scd_bios_call(once ? 0x0012 : 0x0013, 0, &cdda_track_word);   /* MSCPLAY1 once / MSCPLAYR repeat */  
    st = scd_cd_status();
    *st_out = st; 
    return 1;
}

int main(void)
{
    write_byte(0x8003, read_byte(0x8003) & ~1);
    while (read_byte(0x8003) & 2);
#if !KOBO_BOOT_TEXT
    do_md_cmd1(MD_CMD_SET_VDP_REG, 0x8134);   /* K30: Genesis display off (md_init_hw left it on) */
#endif

    char *temp = (char *)0x0C0000;
    char tbuf[64];
    uint32_t build_counter = 0;
    int cart_ok = 0;

    print_diag(temp, "BUILD: D32XR CD M1C QUICK CHECK     ", 2, 0);
    print_diag(temp, "LOADING 32XCD ENGINE...           ", 2, 2);

    {
        const char *m = find_sh2_marker(sh2_app_start, sh2_app_length);
        if (m) sprintf(tbuf, "32X PAYLOAD: %d BYTES SH2-V%c%c        ", sh2_app_length, m[0], m[1]);
        else   sprintf(tbuf, "32X PAYLOAD: %d BYTES SH2:NONE       ", sh2_app_length);
        print_diag(temp, tbuf, 2, 4);
    }

    memcpy(temp, sh2_app_start, sh2_app_length);
    switch_banks();
    {   
        int r32 = do_md_cmd2(MD_CMD_INIT_32X, 0x200000, sh2_app_length);
        do_md_cmd0(MD_CMD_INSTALL_EXC);            /* HW1: hook the 68K error vectors (after the 32X is on) */
        g_nexc = 0;
        char a[12], b[12], c[12];
        fourcc(a, read_long(0xFF8014));   
        fourcc(b, read_long(0xFF8018));   
        fourcc(c, read_long(0xFF801C));   
        switch_banks();
        
        sprintf(tbuf, "INIT32X:%d 1ST:%s              ", r32, a);
        print_diag(temp, tbuf, 2, 19);
        sprintf(tbuf, "END C0:%s C4:%s          ", b, c);
        print_diag(temp, tbuf, 2, 20);
        if (r32 < 0) {
            print_diag(temp, "32X INIT FAILED - STOPPED       ", 2, 21);
            while (1) bump_fm();
        }

        do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15124, 0x1234);

        {
            uint16_t ready = 0;
            volatile int wait = 0;
            
            {
                unsigned int t0 = GET_TICKS;
                for (wait = 0; wait < 1000000; wait++) {
                    ready = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512A);
                    if (ready == 0x0AAA) break;
                    if ((GET_TICKS - t0) >= 180) break;
                }
            }
            sprintf(tbuf, "SH2 READY FOR ISOLATED TEST: %04X    ", ready);
            print_diag(temp, tbuf, 2, 19);

            if (ready != 0x0AAA) {
                print_diag(temp, "SH2 NEVER SIGNALLED READY - STOPPED", 2, 20);
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512A, 0x0BBB);   
                while (1) bump_fm();
            }
        }

#if KOBO_NOCART
        cart_ok = 0;                                   /* no-cart build: never touch the cart */
        print_diag(temp, "CART: NOT USED (NO-CART BUILD)     ", 2, 25);
#else
        cart_ok = safe_cart_present(temp, tbuf);  
#endif
        (void)cart_ok; 

        do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512A, 0x0BBB);   
    }

    uint16_t init_err = InitCD();

    {
        unsigned char c = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "POST-INITCD COMM: %02X          ", c);
        print_diag(temp, tbuf, 2, 1);
    }

    if (init_err != 0) {
    } else {
        print_diag(temp, "CD DRIVE READY                ", 2, 5);
        print_diag(temp, "BOOTING SUB-CPU...            ", 2, 6);
    }

    {   
        int st = scd_cd_status();
        int t2 = scd_track_info(2);
        sprintf(tbuf, "CD TRACKS %02X-%02X  TRACK2 %08X      ", (st >> 8) & 0xFF, st & 0xFF, t2);
        print_diag(temp, tbuf, 2, 14);
    }

    memcpy(temp, Sub_Start, Sub_End - Sub_Start);

    {
        unsigned char c = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "POST-MEMCPY2 COMM: %02X         ", c);
        print_diag(temp, tbuf, 2, 3);
    }

    print_diag(temp, "INITIALIZING SUB-CPU Sfx...   ", 2, 7);
    send_md_cmd0('I'); 

    {
        unsigned char c = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "POST-SENDMD COMM: %02X          ", c);
        print_diag(temp, tbuf, 2, 18);
    }

    print_diag(temp, "68K LISTENER ACTIVE           ", 2, 8);

    {
        unsigned char boot_comm = read_byte(SUB_MAIN_FLAG);
        sprintf(tbuf, "BOOT COMM PORT: %02X            ", boot_comm);
        print_diag(temp, tbuf, 2, 9);
    }

    print_diag(temp, "(TEXT BLINKS TO SHOW SH2 IS ALIVE)", 2, 26);

    while (1) {
        build_counter++;

        if (build_counter == 1) {
            unsigned char first_iter_comm = read_byte(SUB_MAIN_FLAG);
            sprintf(tbuf, "ITER1 COMM PORT: %02X           ", first_iter_comm);
            print_diag(temp, tbuf, 2, 15);
        }

        bump_fm();

#if KOBO_HW_QUIET
        if (sub_quiet) {                 /* HW9 QUIET TEST: no 68K traffic except this heartbeat */
            static unsigned int t_last = 0;
            static uint16_t q_count = 0;
            unsigned int now = GET_TICKS;             /* read directly - no 68K command */
            if (now - t_last >= 30) {
                t_last = now;
                q_count++;
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512E,
                           ((read_byte(0xFF8038) & 0x0F) << 12) | (q_count & 0x0FFF));
            }
            continue;
        }
#endif
        {   /* HW1 SUB-CPU HEARTBEAT (K29): every 16 passes the pass counter goes to COMM14.  Only
               the Sub-CPU writes COMM14; the SH2 only reads it (hardware readout "P"). */
            static uint16_t hb_pass = 0;
            /* HW8: top digit = the CD drive's status (CDD status register 0xFF8038, first
               nibble: 0 stopped, 1 playing, 2 seeking, 3 scanning, 4 paused, 5 tray open,
               9 reading TOC, 0xB no disc / error ...), lower 3 digits = pass count / 64 */
            if ((++hb_pass & 63) == 0) {
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512E,
                           ((read_byte(0xFF8038) & 0x0F) << 12) | ((hb_pass >> 6) & 0x0FFF));
#if KOBO_DIAG_TEXT   /* OL-5 */
                /* OL-2 DIAG 3: Sub-CPU heartbeat on screen + the command waiting in COMM0 */
                sprintf(tbuf, "S%04X C0:%04X ST:%04X M:%04X SL:%04X", hb_pass,   /* DIAG 4 */
                        do_md_cmd1(MD_CMD_READ_32X_16, 0xA15120),
                        do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512C),
                        do_md_cmd1(MD_CMD_READ_32X_16, 0xA15126),
                        do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512A));
                if (!ol_game_running) print_diag(temp, tbuf, 2, 19);   /* OL-2b */
#endif
            }
        }
        uint16_t cmd = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15120);
        if (cmd == 0 && !bmd_on) {       /* HW6: idle - pause ~0.2 ms before the 68K reads COMM0
                                            again (fewer reads while the SH2 may be writing it).
                                            R9: not while the bonus floor runs (no disc reads) */
            volatile int k;
            for (k = 0; k < 150; k++) { if ((k & 3) == 0) cd_pump(); }   /* STREAM: read ahead */
        }

        /* KOBO K5 - PAD FAST PATH.  WHY: the SH2 asks for the pad (CMD 48) every frame and
           only waits a short time.  The rest of this loop (3 more register reads + redrawing
           row 10, each a round trip through the 68K) took longer than that, so the SH2 gave
           up and scrolled with pad = 0.  Answer CMD 48 first and start the next pass. */
        if ((cmd & 0xFF) == 48) {
            uint16_t pad = GET_PAD(0);
            if (cmd >> 8) pcm_play(sfx_has_map ? sfx_map[((cmd >> 8) - 1) & 0xFF] : (cmd >> 8));   /* OL-6 */           /* K33: sound effect requested */
            /* K32: reply = pad (COMM8, upper 16 bits) + the 68K's vblank counter (COMM10, lower
               16 bits) in ONE 32-bit write.  The SH2 uses the counter to run Kobo's logic at
               its fixed 30 ms step, whatever the frame rate.  COMM10 is only used by the
               start-up handshake, long finished by now. */
            do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, ((uint32_t)pad << 16) | (GET_TICKS & 0xFFFF));
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);     /* done           */
            continue;
        }

#if KOBO_DIAG_TEXT   /* OL-5: no register reads for text nobody sees */
        uint16_t comm4 = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15124);
        uint16_t sh2_state = do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512C);

        {   /* HW5: redraw row 10 ONLY when the SH2's step code changes, and never again once the
               game runs (0x4B10).  WHY: the old test also watched COMM0 (flips 00/30 every
               frame with the pad request) and COMM8/10 (pad + frame counter since K32), so on
               hardware (text on) the Sub-CPU reprinted a text row EVERY pass - each print swaps
               Word RAM and goes through the 68K - and the pad replies waited: 1-2 fps. */
            static uint16_t l_st = 0xFFFF;
            static int running = 0;
            if (!running && sh2_state != l_st) {
                uint32_t ckpt = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
                l_st = sh2_state;
                sprintf(tbuf, "32X:%X 32XReg:%X STATE:%X CKPT:%04X     ", cmd, comm4, sh2_state, (unsigned int)(ckpt >> 16));
                print_diag(temp, tbuf, 2, 10);
                if (sh2_state == 0x4B10) { running = 1; ol_game_running = 1; sub_quiet = KOBO_HW_QUIET; }   /* HW9 */
            }
        }

        if (comm4 == 0x0EEE) {
            print_diag(temp, "SH2 REACHED THE LDC LINE              ", 2, 18); 
        } else if (!ol_game_running && comm4 != 0x1234 && comm4 != 0x0000) {   /* OL-2b: quiet once the game runs */
            print_diag(temp, "SH2 NEVER EVEN REACHED THIS FILE      ", 2, 18); 
        }

#endif

        /* R9: a command that may use Word RAM in 1M mode ends the bonus floor's 2M mode (the
           floor is loaded again with its next frame: CMD 71 FRAME replies 0xFFFF) */
        if (bmd_on && cmd != 0 && cmd != 71 && cmd != 45 && cmd != 54) bmd_suspend();

        if (cmd == 38) { 
            int img_idx = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            char img_name[24];
#ifdef CART_PAGED
            cur_dest = 0xFFFFFFFFu;
            if (img_idx & 0x8000) {                     /* R5: destination given in COMM8L */
                cur_dest = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128) & 0x3FFFFCu;
                img_idx &= 0x7FFF;
            }
#endif
            cur_img = img_idx < 0 ? 0 : img_idx;         
            /* KOBO M1: file number 16 = the Kobo graphics pack (tools/kobo_gfx.py).
               CMD 46 then stores it at cart offset 16 * CART_SLOT = 2 MB. */
            if (img_idx == 16) { strcpy(img_name, KOBO_PACK_NAME); cur_img = KOBO_PACK_SLOT; }
            else if (img_idx == 20) { strcpy(img_name, "OPENLARA.32X"); cur_img = 0; }  /* D32X M1: ROM at cart 0 */
            else if (img_idx >= OL_LEVEL_BASE && img_idx < OL_LEVEL_BASE + OL_LEVEL_COUNT) {
                strcpy(img_name, ol_level_files[img_idx - OL_LEVEL_BASE]);   /* OL-2 */
                cur_img = OL_LEVEL_SLOT;
            }
            else if (img_idx >= 200 && img_idx < 1200) {   /* OJ32X: OpenJazz world and bonus packs OJ<n-200>.PAK (port/pack.h) */
                sprintf(img_name, "OJ%03d.PAK", img_idx - 200);
                cur_img = 20;                               /* cart 20 * 128 KB = 0x280000 */
            }
            else if (img_idx >= 1200 && img_idx < 2200) {  /* REV: the Genesis plane's files of a world, GB<n-1200>.PAK */
                sprintf(img_name, "GB%03d.PAK", img_idx - 1200);
                cur_img = 4;                                /* cart 4 * 128 KB = 0x080000 (free up to 0x100000) */
            }
            else if (img_idx >= 2200 && img_idx < 3200) {  /* R7: a world / bonus pack compressed, OZ<n-2200>.PAK (tools/ojlz.c) */
                sprintf(img_name, "OZ%03d.PAK", img_idx - 2200);
                cur_img = 20;
            }
            else if (img_idx >= 3200 && img_idx < 4200) {  /* R7: a Genesis plane pack compressed, GZ<n-3200>.PAK */
                sprintf(img_name, "GZ%03d.PAK", img_idx - 3200);
                cur_img = 4;
            }
            else if (img_idx <= 0) strcpy(img_name, "IMAGE.RAW");
            else sprintf(img_name, "IMAGE%d.RAW", img_idx);
#ifdef CART_PAGED
            if (img_idx == 8) cur_img = 4;              /* R5: the common pack at 0x080000 (page 0) */
#endif

            sprintf(tbuf, "OPENING %s...              ", img_name);
            print_diag(temp, tbuf, 2, 11);

            unsigned char entry_comm = read_byte(SUB_MAIN_FLAG);
            if (entry_comm != 0) {
                sprintf(tbuf, "ENTRY COMM FAULT: %02X        ", entry_comm);
                print_diag(temp, tbuf, 2, 12);
            }

            int64_t file_info = cd_open(img_name);   /* R7: from the cached directory */
            if (file_info < 0) {
                sprintf(tbuf, "ERR: %s NOT FOUND          ", img_name);
                print_diag(temp, tbuf, 2, 12);
                sprintf(tbuf, "TIMEOUT: %d (0=none 1=cmd 2=ack)", scd_last_timeout_code);
                print_diag(temp, tbuf, 2, 13);
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, 0); 
                saved_len = 0;
            } else {
                uint32_t length = (uint32_t)(file_info >> 32);
                saved_lba = (uint32_t)(file_info & 0xFFFFFFFF);
                saved_len = length;                                   /* K10: for CMD 50 */
                sprintf(tbuf, "%s %lu BYTES%s          ", img_name, (unsigned long)length,
                        length > 128000 ? " (TOO TALL)" : " OK");
                print_diag(temp, tbuf, 2, 12);
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, length); 
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0); 
        }
        else if (cmd == CMD_CHUNK_TO_CART || cmd == CMD_CHUNK_COMPARE) {
            int chunk = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int novfy = chunk & 0x8000;          /* OL-13: level loads skip the read-back */
            chunk &= 0x7FFF;
            int length = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
            int off = chunk * 32768;
            int n = 0, bytes, p, k, bad = 0;
            uint32_t coff = CART_IMG_BASE + (uint32_t)cur_img * CART_SLOT + (uint32_t)off;
#ifdef CART_PAGED
            if (cur_dest != 0xFFFFFFFFu) coff = cur_dest + (uint32_t)off;
#endif

            if (off < length) n = (length - off + 2047) / 2048;
            if (n > 16) n = 16;
            bytes = n * 2048;

#if CART_TARGET == 1
            if (n > 0) {
                volatile uint8_t *flag = (volatile uint8_t *)0x0DF000;   
                scd_sub_read((void *)0x0C0000, saved_lba + chunk * 16, n);
                flag[0] = 1; flag[1] = 0;
                switch_banks();                                   
                cx(0x21F000, 0x7FFFFF, 1);                        
                for (p = 0; p < bytes; p += 4096)
                    do_md_cmd3(MD_CMD_BRAM_PUT, 0x200000 + p, 0x600001 + 2 * (coff + p),
                               bytes - p < 4096 ? bytes - p : 4096);
                cx(0x21F001, 0x7FFFFF, 1);                        
                for (p = 0; p < bytes; p += 4096)                 
                    do_md_cmd3(MD_CMD_BRAM_GET, 0x600001 + 2 * (coff + p), 0x208000 + p,
                               bytes - p < 4096 ? bytes - p : 4096);
                switch_banks();                                   
                {
                    volatile uint8_t *disc = (volatile uint8_t *)0x0C0000;
                    volatile uint8_t *cart = (volatile uint8_t *)0x0C8000;
                    for (k = 0; !novfy && k < bytes; k++) {
                        if (disc[k] != cart[k]) {
                            sprintf(tbuf, "MISMATCH C%d +%05X DISC %02X CART %02X ", chunk, k,
                                    disc[k], cart[k]);
                            print_diag(temp, tbuf, 2, 17);
                            bad = 1;
                            break;
                        }
                    }
                }
            }
#else
            if (n > 0) {
                /* STREAM: the drive reads on from the last chunk */
                cd_chunk_read((uint8_t *)0x0C0000, saved_lba + chunk * 16, n,
                              (int)(saved_lba + (saved_len + 2047) / 2048));
                switch_banks();                                   
                if (cmd != CMD_CHUNK_COMPARE)          /* CMD 47: compare only, no write */
                    for (p = 0; p < bytes; p += 0x4000)
                        md_cmd3_pumping(MD_CMD_CHILLY_RV_MEMCPY, 0x400000 + coff + p, 0x200000 + p,
                                        bytes - p < 0x4000 ? bytes - p : 0x4000);
#if CART_VERIFY_READBACK
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(coff >> 20) & 3);   
                for (p = 0; !novfy && p < bytes; p += 0x4000)
                    md_cmd3_pumping(MD_CMD_CHILLY_MEMCPY, 0x208000 + p,
                               0x900000 + ((coff + p) & 0xFFFFF),
                               bytes - p < 0x4000 ? bytes - p : 0x4000);
#endif
                switch_banks();                                   
#if CART_VERIFY_READBACK
                {   
                    volatile uint8_t *disc = (volatile uint8_t *)0x0C0000;
                    volatile uint8_t *cart = (volatile uint8_t *)0x0C8000;
                    /* STREAM: four bytes at a time (the byte loop took about 100 ms a chunk, longer
                       than the drive's read-ahead lasts); the byte compare below finds the place */
                    {
                        const volatile uint32_t *dw = (const volatile uint32_t *)disc;
                        const volatile uint32_t *cw = (const volatile uint32_t *)cart;
                        int w;
                        for (w = 0; !novfy && w < bytes / 4; w++) {
                            if ((w & 255) == 0) cd_pump();
                            if (dw[w] != cw[w]) break;
                        }
                        k = novfy ? bytes : w * 4;               /* the byte loop starts here */
                    }
                    for (; !novfy && k < bytes; k++) {
                        if (disc[k] != cart[k]) {
                            sprintf(tbuf, "MISMATCH C%d +%05X DISC %02X CART %02X ", chunk, k,
                                    disc[k], cart[k]);
                            print_diag(temp, tbuf, 2, 17);
                            bad = 1;
                            break;
                        }
                    }
                }
#endif
#if CART_WINDOW_READS
                {   
                    int wbad = 0;
                    uint16_t want = 0, got = 0;
                    uint32_t o = 0;
                    do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(coff >> 20) & 3);   
                    for (k = 0; k < 4 && !wbad; k++) {
                        o = (uint32_t)(((bytes - 2) / 3 * k) & ~1);
                        want = *(volatile uint16_t *)(0x0C0000 + o);
                        got  = (uint16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0x900000 + ((coff + o) & 0xFFFFF));
                        if (got != want) wbad = 1;
                    }
                    if (wbad)
                        sprintf(tbuf, "32X WIN C%d +%05lX W%04X R%04X   ", chunk, (unsigned long)o, want, got);
                    else
                        sprintf(tbuf, "32X WIN C%d 900000: OK            ", chunk);
                    print_diag(temp, tbuf, 2, 16);
                }
#else
                print_diag(temp, "32X WIN: OFF (V72 TEST)          ", 2, 16);
#endif
            }
#endif
            sprintf(tbuf, "%s I%d C%d @%06lX %s          ", cmd == CMD_CHUNK_COMPARE ? "CMP " : "CART", cur_img, chunk,
#if CART_TARGET == 1
                    (unsigned long)(0x600001 + 2 * coff),      
#else
                    (unsigned long)(0x400000 + coff),          
#endif
                    n == 0 ? "EMPTY" : bad ? "BAD" : (CART_VERIFY_READBACK ? "OK" : "NOVFY"));
            print_diag(temp, tbuf, 2, 13);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, bad);   /* HW4: 16-bit COMM12 - COMM14 is the heartbeat */
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == CMD_CART_TO_FB) {
            int slot_chunk = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int slot = slot_chunk >> 8;
            int chunk = slot_chunk & 0xFF;
            int length = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
            int off = chunk * 32768, bytes = length - off, p;
            uint32_t coff = CART_IMG_BASE + (uint32_t)slot * CART_SLOT + (uint32_t)off;
            
            if (bytes > 32768) bytes = 32768;
            for (p = 0; p < bytes; p += 4096)
                do_md_cmd3(MD_CMD_BRAM_TO_FB, 0x600001 + 2 * (coff + p), 0x840200 + off + p,
                           bytes - p < 4096 ? bytes - p : 4096);
            
            sprintf(tbuf, "CART->FB S%d C%d %d BYTES          ", slot, chunk, bytes > 0 ? bytes : 0);
            print_diag(temp, tbuf, 2, 16);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 60) {
#ifdef CART_PAGED
            cart_page_write(1);                         /* R5: hand over with the reset mapping */
#endif
            /* D32X M1: the ROM image is in the cart.  Acknowledge the SH2 FIRST (the 68K will be
               gone after this), then make the 68K jump to D32XR's Genesis code (0x880800 via the
               32X window).  send_md_cmd only (no wait: the 68K never comes back).  The Sub-CPU
               has nothing else to do in M1 (D32XR finds no Sega CD and does not call it). */
            /* M1b: what does the 68000 see?  D32XR's _start at cart offset 0x800 must read
               7000323C through the fixed window 0x880000 (that is where the 68K will jump) -
               compare with the proven bank window 0x900000 (bank 0) and our patch at 0x40. */
            {
                uint32_t w88, w90, w40;
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, 0);              /* bank 0 */
                w88 = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0x880800);
                w90 = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0x900800);
                w40 = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0x880040);
                sprintf(tbuf, "880800:%08X 900800:%08X  ", (unsigned)w88, (unsigned)w90);
                print_diag(temp, tbuf, 2, 22);
                sprintf(tbuf, "880040:%08X (PATCH 23FCC000)  ", (unsigned)w40);
                print_diag(temp, tbuf, 2, 23);
                /* M1c: where do the 68000's V-blank / H-blank / external-interrupt vectors
                   point with the 32X on?  RAM slots (FFFDxx) or the cart window (880xxx)? */
                sprintf(tbuf, "V78:%08X V70:%08X V68:%08X ",
                        (unsigned)do_md_cmd1(MD_CMD_READ_32X_32, 0x78),
                        (unsigned)do_md_cmd1(MD_CMD_READ_32X_32, 0x70),
                        (unsigned)do_md_cmd1(MD_CMD_READ_32X_32, 0x68));
                print_diag(temp, tbuf, 2, 24);
            }
            print_diag(temp, "D32XR: STARTING THE GAME FROM THE CART ", 2, 14);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
            send_md_cmd0(MD_CMD_START_D32XR);
            while (1) { }
        }
        else if (cmd == 44) {
#if !SLIDESHOW_DEBUG
            if (!quiet) { do_md_cmd0(MD_CMD_CLEAR_B); do_md_cmd0(MD_CMD_CLEAR_A); quiet = 1; }
#endif
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 50 || cmd == 51) {
            /* K15: CMD 51 = same as CMD 50, but chunk c goes to frame buffer byte
               0x200 + c * 32 KB (a whole picture in order - the splash screen). */
            /* KOBO NO-CART - CMD 50 "file chunk -> frame buffer (raw)".
               COMM2 = chunk number (32 KB each).  File length: saved_len from CMD 38 (K10 -
               no longer read from COMM8).  CD -> Word RAM (Sub-CPU) -> frame buffer at 68K 0x840200 via MD
               command 25 copy_words with flag 0 = plain copy (flag 1 would set bit 15 on every
               word, which is only right for 15-bit pictures).  The SH2 has handed the frame
               buffer to the 68K (FM=0) before sending this. */
            int chunk = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int length = (int)saved_len;           /* K10: from CMD 38, NOT from COMM8 */
            /* K31: CMD 50 chunks are 16 KB (8 sectors) - the off-screen loading window shrank
               to 16 KB so all 5 tile sets fit in spare frame-buffer memory (no CD reads after
               the music starts: a data read silences the CD audio).  CMD 51 stays 32 KB. */
            int sec = cmd == 51 ? 16 : 8;                   /* sectors per chunk */
            int off = chunk * sec * 2048;
            int n = 0;
            if (off < length) n = (length - off + 2047) / 2048;
            if (n > sec) n = sec;
            uint16_t w0 = 0;
            /* (K13 cleared Word RAM priority mode here; K13 showed it was never on - register 05
               before and after - so the write is removed in K15: no needless pokes at the Word
               RAM mode register on real hardware.  The real K12 fault was zero words not landing
               in the frame buffer - fixed on the SH2 side by clearing the area first.) */
            if (n > 0) {
                cd_stream_read((uint8_t *)0x0C0000, saved_lba + chunk * sec, n,      /* CD -> Word RAM (STREAM) */
                               (int)(saved_lba + (saved_len + 2047) / 2048));
                switch_banks();                                               /* -> 68K side    */
                do_md_cmd4(MD_CMD_COPY_WORDS, 0x200000,
                           cmd == 51 ? 0x840200 + off : 0x852000, n * 2048, 0);
                /* K30: CMD 50 now lands OFF-SCREEN (frame buffer byte 0x12000, past the
                   visible 320x224 picture which ends at 0x11A00): loading never overwrites
                   what is on screen (the panel stays intact).  SH2 side: FB_CHUNK_BUF. */
                switch_banks();                                               /* -> Sub side    */
                w0 = *(volatile uint16_t *)0x0C0000;   /* first word read from CD (read it
                                                          BEFORE print_diag reuses this buffer) */
            }
            /* K11 reply for the SH2's diagnostic strip (lines 1-3): chunk | sectors | length>>8.
               Written before COMM0 is cleared; the SH2 reads it only after that. */
            /* K14 reply: chunk | sectors | the word in WORD RAM at offset 0x210 of this chunk, read
               after the copy (the Sub-CPU's buffer at 0x0C0000).  For chunk 0 that is the high
               word of the tile set offset and must be 0000.  Correct here but wrong in the
               frame buffer (strip line T) = the 68K -> frame buffer copy loses zero data.
               (K13: priority mode was never on - register read 05 before and after.)        */
            {
                uint16_t wr = (n > 0) ? *(volatile uint16_t *)(0x0C0000 + 0x210) : 0xEEEE;
                do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA1512C, (chunk << 24) | (n << 16) | wr);
            }
            (void)w0;
            sprintf(tbuf, "CD->FB CHUNK %d  %d SECTORS          ", chunk, n);
            print_diag(temp, tbuf, 2, 16);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 54) {
            /* K36 - CMD 54 "music off": BIOS MSCSTOP (the same call cdda_start makes first).
               Music on again = CMD 45 as always. */
            bios_wait_ready(0);
            scd_bios_call(0x0002, 0, 0);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 56 || cmd == 59) {
            /* K36 - CMD 56 load / CMD 59 save the backup-RAM file.  Reply COMM12: 1 ok, 0 not. */
            int ok = cmd == 56 ? bram_load() : bram_save();
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, ok);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 57 || cmd == 58) {
            /* K36 - CMD 57 get / CMD 58 put one word of the 128-byte buffer: COMM2 = word index,
               COMM8 = value (58: written by the SH2 before the command; 57: our reply). */
            int wi = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & (BRAM_BYTES / 2 - 1);   /* OL-5 */
            if (cmd == 58) {
                int v = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15128);
                bram_buf[wi * 2] = (uint8_t)(v >> 8); bram_buf[wi * 2 + 1] = (uint8_t)v;
            } else {
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15128, (bram_buf[wi * 2] << 8) | bram_buf[wi * 2 + 1]);
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
#ifdef CART_PAGED
        else if (cmd == 68) {
            /* R5: cart page for window 1 (COMM2 = 1-7). Reply COMM12 = 1 done, 0 refused. */
            int page = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFFFF;
#ifdef CART_ED
            int ok = page >= 1 && page <= 6;       /* ED: page 7 is the EverDrive's banks 28-31, and
                                                      31 is its save bank - never hand it out */
#else
            int ok = page >= 1 && page <= 7;
#endif
            if (ok) cart_page_write(page);
            sprintf(tbuf, "CART PAGE %d %s              ", page, ok ? "SET" : "REFUSED");
            print_diag(temp, tbuf, 2, 15);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, ok);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 70) {
            /* R7: the disc's stamp (cd_open above) -> COMM8L; 0: unknown */
            cd_dir_load();
            do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, (int)disc_stamp);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 69) {
            /* R7: unpack a compressed pack from one cart page into another (unpack_pack above) */
            const int pages = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            const uint32_t src = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128) & 0x3FFFFEu;
            const uint32_t dst = ((uint32_t)do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512C) & 0x7F) << 15;
            const int sp = pages & 7, dp = (pages >> 8) & 7;
            uint32_t raw = 0;
            if (sp >= 1 && dp >= 1 && src >= 0x200000 && dst >= 0x200000)
                raw = unpack_pack(sp, dp, src, dst);
            do_md_cmd2(MD_CMD_WRITE_32X_32, 0xA15128, (int)raw);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, raw ? 0 : 1);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
#endif
        else if (cmd == 61) {
            /* OL-9: save area - the 4 KB buffer (filled with CMD 58) -> RAM cart at
               0x3F0000 + COMM2 * 4 KB.  The SH2 cannot write the cart; the 68000 can (RV method,
               exactly as CMD 46).  Read back through the 32X window and compared.
               Reply COMM12: 1 ok, 0 failed. */
            int piece = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 15;
            uint32_t coff = CART_IMG_BASE + 0x3F0000 + (uint32_t)piece * 4096;
            int k, ok = 1;
            volatile uint8_t *wr = (volatile uint8_t *)0x0C0000;
            for (k = 0; k < 4096; k++) wr[k] = bram_buf[k];
            switch_banks();
#ifdef CART_EDSAVE
            if (ed_page == 1 && piece <= SAVE_PIECE_TOP) {
                /* ED-SAVE: into the EverDrive's save bank and back out of it (the listener's window-1
                   route: RV = 1, the write bit on for the write only) */
                do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x800000 + (uint32_t)piece * 4096, 0x200000, 4096);
                do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x208000, 0x800000 + (uint32_t)piece * 4096, 4096);
            } else
#endif
            {
                do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x400000 + coff, 0x200000, 4096);
                do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15104, (int)(coff >> 20) & 3);
                do_md_cmd3(MD_CMD_CHILLY_MEMCPY, 0x208000, 0x900000 + (coff & 0xFFFFF), 4096);
            }
            switch_banks();
            {
                volatile uint8_t *cart = (volatile uint8_t *)0x0C8000;
                for (k = 0; k < 4096; k++)
                    if (cart[k] != bram_buf[k]) { ok = 0; break; }
            }
#ifdef CART_EDSAVE
            if (ok && ed_page == 1 && piece <= SAVE_PIECE_TOP) ok |= 0x200;   /* it went to the save bank */
#endif
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, ok);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
#ifdef CART_PAGED
        else if (cmd == 62) {
            /* ED-SAVE - CMD 62 "save piece in": COMM2 = a piece (0-15) -> the 4 KB buffer, which the SH2
               reads word by word with CMD 57; COMM2 = 0x8000 asks only what this program does.  Reply
               COMM12: 1 | 0x200 (page 1's save area is in the EverDrive's save bank: read it this way,
               the cart has none).  The disc's own program answers 1: the SH2 reads the cart as before. */
            int piece = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int caps = 1;
#ifdef CART_EDSAVE
            caps |= 0x200;
            if (piece != 0x8000 && (piece & 15) <= SAVE_PIECE_TOP) {
                int k;
                volatile uint8_t *rd = (volatile uint8_t *)0x0C8000;
                switch_banks();
                do_md_cmd3(MD_CMD_CHILLY_RV_MEMCPY, 0x208000, 0x800000 + (uint32_t)(piece & 15) * 4096, 4096);
                switch_banks();
                for (k = 0; k < 4096; k++) bram_buf[k] = rd[k];
            }
#endif
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, caps);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
#endif
        else if (cmd == 53) {
            /* K33 - CMD 53 "load sound effects": KOBOSFX.BIN -> PCM chip.  Reply in COMM12:
               number of effects (0 = failed). */
            char sfx_name[16];                              /* OL-6: COMM2 = level */
            sprintf(sfx_name, "SFX%02d.BIN", do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 0xFF);
            int nfx = pcm_load(sfx_name);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, nfx);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 71) {
            /* R9 - CMD 71 "bonus level floor" (see bmd_load above) */
            int op = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int r = 0;
            if ((op & 0xFF) == 1) r = bmd_load((uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128));
            else if ((op & 0xFF) == 2) {
                uint32_t xy = (uint32_t)do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
                int16_t cs = (int16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0xA15124);
                int16_t sn = (int16_t)do_md_cmd1(MD_CMD_READ_32X_16, 0xA1512C);
                r = bmd_frame((uint16_t)(xy >> 16), (uint16_t)xy, cs, sn, (op >> 8) & 15);
            }
            else if ((op & 0xFF) == 3) { bmd_suspend(); r = 1; }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, r);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 67) {
            /* REV - CMD 67 "Genesis plane" (docs/GENESIS_PLANE.md): COMM2 = what, COMM8/10 (32 bits)
               = its argument; reply in COMM12 (the 68000's result). 1 load the level's data
               (argument: its cart offset), 2 show it (camera x << 16 | y), 3 off, 4 a map tile
               changed (cell << 16 | tile). */
            int what = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int arg = do_md_cmd1(MD_CMD_READ_32X_32, 0xA15128);
            int r = 0;
            if (what == 1) r = do_md_cmd1(MD_CMD_GENBG_LOAD, arg);
            else if (what == 2) r = do_md_cmd1(MD_CMD_GENBG_START, arg);
            else if (what == 3) r = do_md_cmd0(MD_CMD_GENBG_OFF);
            else if (what == 4) r = do_md_cmd1(MD_CMD_GENBG_CELL, arg);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA1512C, r);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 52) {
            /* KOBO K19 - CMD 52 "Genesis text layer on/off": COMM2 = 1 on, 0 off.
               Switches the Genesis display itself (VDP register 1 bit 6) - off blanks ALL
               Genesis text (green diagnostics and Chilly's white messages) to black and the
               32X picture shows alone; on brings everything back (it stays in video RAM).
               WHY: the 32X priority bit alone did not hide the text reliably (K17/K18). */
            int on = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122) & 1;
            do_md_cmd1(MD_CMD_SET_VDP_REG, on ? 0x8174 : 0x8134);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 48) {
            /* (K5: normally handled by the fast path at the top of the loop)
               KOBO M1 - CMD 48 "read pad": reply = pad 1 bits (SEGA_CTRL_*) in COMM8.
               WHY a command: the SH2 must never poll a comm register the 68K writes
               (32X manual p.69, and the COMM6 crash).  GET_PAD(0) is the copy the 68K's
               vblank handler keeps in its comm word - no extra 68K work. */
            uint16_t pad = GET_PAD(0);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15128, pad);
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
        else if (cmd == 45) {
            int track = do_md_cmd1(MD_CMD_READ_32X_16, 0xA15122);
            int once = track & 0x8000;                  /* OL-4: bit 15 = play once */
            track &= 0x7FFF;
            sprintf(tbuf, "CDDA: PLAY TRACK %d (REPEAT)        ", track);
            print_diag(temp, tbuf, 2, 27);
            
            {
                int st = 0;
                int tries = cdda_start(track, &st, once);
                sprintf(tbuf, "CDDA T%d ST %02X TRY %d%s          ", track, (st >> 24) & 0xFF,
                        tries, tries ? "" : " FAILED");
                print_diag(temp, tbuf, 2, 27);
            }
            do_md_cmd2(MD_CMD_WRITE_32X_16, 0xA15120, 0);
        }
    }
    return 0;
}