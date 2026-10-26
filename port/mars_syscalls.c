/*
 * OpenJazz CD32X - minimal newlib system calls for the SH2 (from Tyrian 32X T3j,
 * port/mars/syscalls.c, + a few more that OpenJazz's file code can reach).
 * Without them, libc.a's own SH simulator syscalls (lib_a-syscalls.o) are linked in,
 * and they clash with newlib's reent.o over `errno'.
 * malloc gets the SDRAM between the end of .bss and the stacks (__heap_start/__heap_end
 * come from the linker script, port/mars/oj.ld).
 * There is no file system and no console: game files are opened in the cart packs
 * (port/pack.c, fmemopen); saves and settings live in the RAM cart's save area (below).
 */
#include "plat.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>

#undef errno
extern int errno;

extern char heap_start[] __asm__("__heap_start");
extern char heap_end[] __asm__("__heap_end");

static char *heap_brk;

/* For the memory report: highest point the heap reached. */
char *oj32x_heap_peak;

void *_sbrk(ptrdiff_t incr)
{
	if (heap_brk == NULL)
		heap_brk = heap_start;
	if (heap_brk + incr > heap_end || heap_brk + incr < heap_start)
	{
		errno = ENOMEM;
		return (void *)-1;
	}
	char *prev = heap_brk;
	heap_brk += incr;
	if (heap_brk > oj32x_heap_peak)
		oj32x_heap_peak = heap_brk;
	return prev;
}

size_t oj32x_heap_used(void) { return heap_brk ? (size_t)(heap_brk - heap_start) : 0; }
size_t oj32x_heap_size(void) { return (size_t)(heap_end - heap_start); }

void _exit(int code) { plat_exit(code); }

/* Only the console streams 0-2 exist here. Streams from fmemopen (the data packs) have no
 * file (fd -1): newlib's __smakebuf asks _fstat about them. Answering "character device" for
 * any fd and leaving st_blksize uninitialised made newlib size the stream buffer from garbage;
 * so an unknown fd is EBADF (newlib then uses BUFSIZ), and the struct is cleared. */

/* ---- saves and settings in the RAM cart's save area ---------------------------------
 * The Sega CD's internal backup RAM is 8 KB shared by every game, too small for the game's
 * saves (up to ~2 KB each), and its buffers would cost SDRAM. Instead OpenLara's save area at
 * cart 0x3F0000 is used, one 4 KB piece per file: piece 0 openjazz.cfg (the settings, shared
 * by both games), 1-4 SAVE.0-SAVE.3 of Jazz Jackrabbit CD, 6-9 those of Holiday Hare '95
 * (piece 10: the port's own settings, port/plat_mars.c). Writing: the bytes go word by word
 * into the Sub-CPU's buffer (CMD 58); closing the file writes the piece into the cart (CMD 61,
 * through the 68000, read back and compared). Reading: straight from the cart (the SH2 can
 * read it). Piece layout: "OJSF", u16 size, name[12], data (at most 4096 - 18 bytes). */
#include <fcntl.h>
#define PIECE_BYTES 4096
#define PIECE_HEAD  18
#define FD_BASE 10
int plat_save_put_word(int index, int value);     /* plat_mars.c: CMD 58 */
int plat_save_piece(int piece);                   /* plat_mars.c: CMD 61 */
int plat_game(void);
#define SAVE_AREA ((const volatile uint8_t *)0x223F0000u)   /* cart 0x3F0000, cache-through */
#if OJ32X_CART_MB
/* R5: the save area is page 1's; a level's world may be in another page (port/cartcache.c) */
void plat_save_area_enter(void);
void plat_save_area_leave(void);
#define SAVE_AREA_ENTER() plat_save_area_enter()
#define SAVE_AREA_LEAVE() plat_save_area_leave()
#else
#define SAVE_AREA_ENTER() ((void)0)
#define SAVE_AREA_LEAVE() ((void)0)
#endif

typedef struct { int piece; int pos; int size; int writing; int open; int odd; uint8_t pending; } SaveFd;
static SaveFd fds[4];

static void base_name(const char *path, char out[12])
{
	const char *b = path;
	for (const char *p = path; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
	int i = 0;
	for (; b[i] && i < 12; ++i) out[i] = (char)((b[i] >= 'a' && b[i] <= 'z') ? b[i] - 32 : b[i]);   /* 12 = OPENJAZZ.CFG */
	for (; i < 12; ++i) out[i] = 0;
}

/* which piece a file name belongs to (-1: not a file we keep) */
static int piece_of(const char *path)
{
	char n[12];
	base_name(path, n);
	const int base = plat_game() == 1 ? 5 : 0;   /* Holiday Hare '95 keeps its own (5-9) */
	if (memcmp(n, "OPENJAZZ.CFG", 12) == 0) return 0;   /* one settings file for both */
	if (n[0] == 'S' && n[1] == 'A' && n[2] == 'V' && n[3] == 'E' && n[4] == '.' && n[5] >= '0' && n[5] <= '3' && n[6] == 0)
		return base + 1 + (n[5] - '0');
	return -1;
}

void plat_save_read(int piece, int off, uint8_t *dst, int n);   /* plat_mars.c: the cart, or the EverDrive's save bank (ED-SAVE) */

/* size of a stored file, -1 if the piece does not hold that file */
static int piece_size(int piece, const char *path)
{
	uint8_t p[PIECE_HEAD];
	plat_save_read(piece, 0, p, PIECE_HEAD);
	if (p[0] != 'O' || p[1] != 'J' || p[2] != 'S' || p[3] != 'F') return -1;
	char n[12];
	base_name(path, n);
	for (int i = 0; i < 12; i++) if (p[6 + i] != (uint8_t)n[i]) return -1;
	int size = p[4] << 8 | p[5];
	return size <= PIECE_BYTES - PIECE_HEAD ? size : -1;
}

static int store_find(const char *path)
{
	int piece = piece_of(path);
	if (piece < 0) return -1;
	SAVE_AREA_ENTER();
	const int size = piece_size(piece, path);
	SAVE_AREA_LEAVE();
	return size >= 0 ? piece : -1;
}

/* the Sub-CPU buffer is filled byte pairs at a time: the data starts at byte 18 (even) */
static int put_byte(SaveFd *d, uint8_t b)
{
	const int at = PIECE_HEAD + d->pos;
	if (at >= PIECE_BYTES) return 0;
	if (at & 1) { plat_save_put_word(at >> 1, d->pending << 8 | b); d->odd = 0; }
	else { d->pending = b; d->odd = 1; }
	d->pos++;
	return 1;
}

static char open_name[4][12];

int _open(const char *name, int flags, int mode)
{
	(void)mode;
	int slot = -1;
	for (int i = 0; i < 4; i++) if (!fds[i].open) { slot = i; break; }
	if (slot < 0) { errno = EMFILE; return -1; }
	const int piece = piece_of(name);
	if (piece < 0) { errno = ENOENT; return -1; }
	const int writing = (flags & O_ACCMODE) != O_RDONLY;
	if (writing)
	{
		for (int i = 0; i < 4; i++) if (fds[i].open && fds[i].writing) { errno = EBUSY; return -1; }
		fds[slot].size = 0;
	}
	else
	{
		SAVE_AREA_ENTER();
		const int size = piece_size(piece, name);
		SAVE_AREA_LEAVE();
		if (size < 0) { errno = ENOENT; return -1; }
		fds[slot].size = size;
	}
	base_name(name, open_name[slot]);
	fds[slot].piece = piece;
	fds[slot].pos = 0;
	fds[slot].writing = writing;
	fds[slot].odd = 0;
	fds[slot].open = 1;
	return FD_BASE + slot;
}

static SaveFd *fd_of(int fd)
{
	if (fd < FD_BASE || fd >= FD_BASE + 4 || !fds[fd - FD_BASE].open) return NULL;
	return &fds[fd - FD_BASE];
}

int _read(int fd, char *buf, int len)
{
	SaveFd *d = fd_of(fd);
	if (!d || d->writing) return 0;
	int n = d->size - d->pos;
	if (n > len) n = len;
	if (n < 0) n = 0;
	SAVE_AREA_ENTER();
	plat_save_read(d->piece, PIECE_HEAD + d->pos, (uint8_t *)buf, n);
	SAVE_AREA_LEAVE();
	d->pos += n;
	return n;
}

int _write(int fd, const char *buf, int len)
{
	SaveFd *d = fd_of(fd);
	if (!d) return len;                          /* the console streams: nowhere to go */
	if (!d->writing) { errno = EBADF; return -1; }
	for (int i = 0; i < len; i++)
		if (!put_byte(d, (uint8_t)buf[i])) { errno = ENOSPC; return -1; }
	if (d->pos > d->size) d->size = d->pos;
	return len;
}

int _lseek(int fd, int off, int whence)
{
	SaveFd *d = fd_of(fd);
	if (!d) return 0;
	int base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? d->pos : d->size;
	if (d->writing)
		return (base + off == d->pos) ? d->pos : (errno = EINVAL, -1);   /* only where we are */
	if (base + off < 0 || base + off > d->size) { errno = EINVAL; return -1; }
	d->pos = base + off;
	return d->pos;
}

int _close(int fd)
{
	SaveFd *d = fd_of(fd);
	if (!d) return -1;
	d->open = 0;
	if (!d->writing) return 0;
	if (d->odd) plat_save_put_word((PIECE_HEAD + d->pos - 1) >> 1, d->pending << 8);
	/* the header last: magic, size, name */
	plat_save_put_word(0, 'O' << 8 | 'J');
	plat_save_put_word(1, 'S' << 8 | 'F');
	plat_save_put_word(2, d->size);
	const char *n = open_name[d - fds];
	for (int i = 0; i < 12; i += 2) plat_save_put_word(3 + i / 2, (uint8_t)n[i] << 8 | (uint8_t)n[i + 1]);
	if (!plat_save_piece(d->piece)) { errno = EIO; return -1; }
	return 0;
}

int _fstat(int fd, struct stat *st)
{
	if (fd_of(fd)) { memset(st, 0, sizeof *st); st->st_mode = S_IFREG; st->st_size = fds[fd - FD_BASE].size; st->st_blksize = 512; return 0; }
	if (fd < 0 || fd > 2) { errno = EBADF; return -1; }
	memset(st, 0, sizeof *st);
	st->st_mode = S_IFCHR;
	return 0;
}
int _stat(const char *name, struct stat *st) { (void)name; (void)st; errno = ENOENT; return -1; }
int _isatty(int fd) { return fd >= 0 && fd <= 2; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
int _unlink(const char *name) { (void)name; errno = ENOENT; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; errno = EMLINK; return -1; }
int _rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOENT; return -1; }
/* access/getcwd: plain C names (newlib for sh-elf has none; the SH linker shows them as
 * _access/_getcwd because it puts an underscore before every C name) */
/* a directory ("/", from getcwd) is readable and writeable - OpenJazz only adds a path
 * for its settings and saves when access() says so; answering "no such file" for it left
 * the game without a place to write, and saving and loading did nothing */
int access(const char *name, int mode)
{
	(void)mode;
	size_t n = strlen(name);
	if (n == 0 || name[n - 1] == '/' || strcmp(name, ".") == 0) return 0;
	if (store_find(name) >= 0) return 0;
	errno = ENOENT;
	return -1;
}
char *getcwd(char *buf, size_t size) { if (buf && size > 1) { buf[0] = '/'; buf[1] = 0; return buf; } errno = ERANGE; return NULL; }
int mkdir(const char *name, mode_t mode) { (void)name; (void)mode; errno = EROFS; return -1; }

int _gettimeofday(struct timeval *tv, void *tz)
{
	(void)tz;
	uint32_t ms = plat_ticks_ms();
	tv->tv_sec = ms / 1000;
	tv->tv_usec = (ms % 1000) * 1000;
	return 0;
}

clock_t _times(struct tms *buf)
{
	clock_t t = (clock_t)plat_ticks_ms();
	buf->tms_utime = t; buf->tms_stime = 0; buf->tms_cutime = 0; buf->tms_cstime = 0;
	return t;
}

/* ---- heap check (diagnostic) -------------------------------------------------------------
 * newlib's malloc (dlmalloc) keeps its blocks one after another from the first _sbrk address:
 * [prev size][size | flags][data...], sizes multiples of 8, the last block ("top") ending at
 * the current break. Walking them shows when a block header has been overwritten. Called at a
 * few points (port/sdl_oj.cpp, plat_log); the first one that finds damage stops with a red
 * screen naming that point, so the damage happened between it and the check before. */
void oj32x_heap_check(const char *where)
{
	static int busy;
	if (busy || heap_brk == NULL) return;
	busy = 1;
	char *p = heap_start;
	int n = 0;
	while (p + 8 <= heap_brk)
	{
		uint32_t size = ((volatile uint32_t *)p)[1] & ~7u;
		if (size < 16 || (size & 7) || p + size > heap_brk)
		{
			static char msg[96];
			char *m = msg;
			const char *t = "HEAP DAMAGED, FOUND AT ";
			while (*t) *m++ = *t++;
			for (t = where; *t && m < msg + 60; ) *m++ = *t++;
			t = " BLOCK ";
			while (*t) *m++ = *t++;
			int v = n, d = 1; while (v / d >= 10) d *= 10;
			for (; d; d /= 10) *m++ = (char)('0' + (v / d) % 10);
			*m = 0;
			busy = 0;
			plat_fatal(msg);
		}
		p += size;
		n++;
	}
	busy = 0;
}
