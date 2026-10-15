/*
 * OpenJazz CD32X - RAM cart allocator (see cart.h). First fit over a table of used blocks,
 * kept sorted by address. Same code on the PC and the 32X; only plat_cart_* differ.
 */
#include "cart.h"
#include "plat.h"

#include <stdio.h>
#include <string.h>

typedef struct { uint32_t off, size; } Block;

static Block blocks[CART_BLOCKS];
static int nblocks;
static uint8_t *base;
static uint32_t limit;
static size_t used, peak;

void cart_init(void)
{
	base = plat_cart_data(&limit);
	nblocks = 0;
	used = peak = 0;
}

void *cart_alloc(size_t bytes)
{
	if (base == NULL)
		cart_init();
	uint32_t need = (uint32_t)((bytes + 3) & ~(size_t)3);
	if (need == 0)
		need = 4;
	if (nblocks >= CART_BLOCKS)
	{
		plat_log("cart: block table full");
		return NULL;
	}
	uint32_t at = 0;
	int i;
	for (i = 0; i < nblocks; ++i)
	{
		if (blocks[i].off - at >= need)
			break;
		at = blocks[i].off + blocks[i].size;
	}
	if (i == nblocks && limit - at < need)
	{
		char msg[80];
		snprintf(msg, sizeof msg, "cart: full (%lu bytes wanted, %lu used)",
		         (unsigned long)bytes, (unsigned long)used);
		plat_log(msg);
		return NULL;
	}
	memmove(&blocks[i + 1], &blocks[i], (size_t)(nblocks - i) * sizeof blocks[0]);
	blocks[i].off = at;
	blocks[i].size = need;
	++nblocks;
	used += need;
	if (used > peak)
		peak = used;
	return base + at;
}

void cart_free(void *p)
{
	if (p == NULL)
		return;
	uint32_t off = (uint32_t)((uint8_t *)p - base);
	int lo = 0, hi = nblocks - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (blocks[mid].off == off)
		{
			used -= blocks[mid].size;
			memmove(&blocks[mid], &blocks[mid + 1], (size_t)(nblocks - mid - 1) * sizeof blocks[0]);
			--nblocks;
			return;
		}
		if (blocks[mid].off < off) lo = mid + 1; else hi = mid - 1;
	}
	plat_fatal("cart_free: not a cart block");
}

void cart_write(void *dst, const void *src, size_t bytes)
{
	/* plat_cart_write counts from the start of the data area, not from the allocator's
	 * base (which lies after a mounted pack) - writing relative to the base overwrote the pack */
	plat_cart_write((uint32_t)((uint8_t *)dst - plat_cart_ptr(PLAT_CART_DATA_OFFSET)), src, bytes);
}

bool cart_contains(const void *p)
{
	return base != NULL && (const uint8_t *)p >= base && (const uint8_t *)p < base + limit;
}

size_t cart_used(void) { return used; }
size_t cart_peak(void) { return peak; }
size_t cart_size(void) { return limit; }
int cart_blocks(void) { return nblocks; }
