/*
 * OpenJazz CD32X - data packs in the RAM cart: reading (PC build and 32X). See pack.h.
 */
#include "pack.h"

#include <ctype.h>
#include <string.h>

typedef struct { const uint8_t *base; uint32_t files, pics, size, fpics; } Pack;
static Pack packs[PACK_MAX];
static int npacks;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool pack_mount(const uint8_t *base)
{
	if (npacks >= PACK_MAX || base == NULL)
		return false;
	if (base[0] != 'O' || base[1] != 'J' || base[2] != 'P' || base[3] != 'K' || be32(base + 4) != 2)
		return false;
	Pack *k = &packs[npacks++];
	k->base = base;
	k->files = be32(base + 8);
	k->pics = be32(base + 12);
	k->size = be32(base + 16);
	k->fpics = be32(base + 20);
	return true;
}

void pack_unmount_all(void) { npacks = 0; }

bool pack_mount_at(int index, const uint8_t *base)
{
	if (index < 0 || index >= PACK_MAX || index > npacks)
		return false;
	if (base == NULL)
	{
		if (index < npacks) npacks = index;
		return true;
	}
	if (base[0] != 'O' || base[1] != 'J' || base[2] != 'P' || base[3] != 'K' || be32(base + 4) != 2)
		return false;
	Pack *k = &packs[index];
	k->base = base;
	k->files = be32(base + 8);
	k->pics = be32(base + 12);
	k->size = be32(base + 16);
	k->fpics = be32(base + 20);
	if (index >= npacks) npacks = index + 1;
	return true;
}
int pack_count(void) { return npacks; }

/* "path/to/Panel.000" -> "PANEL.000" (at most 15 characters) */
static void norm_name(const char *name, char out[16])
{
	const char *b = name;
	for (const char *p = name; *p; ++p)
		if (*p == '/' || *p == '\\')
			b = p + 1;
	int i = 0;
	for (; b[i] && i < 15; ++i)
		out[i] = (char)toupper((unsigned char)b[i]);
	for (; i < 16; ++i)
		out[i] = 0;
}

bool pack_file(const char *name, const uint8_t **data, uint32_t *size)
{
	char want[16];
	norm_name(name, want);
	for (int k = npacks - 1; k >= 0; --k)
	{
		const uint8_t *e = packs[k].base + 24;
		for (uint32_t i = 0; i < packs[k].files; ++i, e += 24)
			if (memcmp(e, want, 16) == 0 && be32(e + 16) != 0)   /* offset 0: listed only */
			{
				*data = packs[k].base + be32(e + 16);
				*size = be32(e + 20);
				return true;
			}
	}
	return false;
}

bool pack_exists(const char *name)
{
	char want[16];
	norm_name(name, want);
	for (int k = npacks - 1; k >= 0; --k)
	{
		const uint8_t *e = packs[k].base + 24;
		for (uint32_t i = 0; i < packs[k].files; ++i, e += 24)
			if (memcmp(e, want, 16) == 0)
				return true;
	}
	return false;
}

FILE *pack_fopen(const char *name)
{
	const uint8_t *data;
	uint32_t size;
	if (!pack_file(name, &data, &size))
		return NULL;
	/* read-only use: the stream never writes into the buffer */
	return fmemopen((void *)data, size ? size : 1, "rb");
}

uint32_t pack_hash(const uint8_t *pixels, int w, int h)
{
	uint32_t x = 2166136261u;                     /* FNV-1a */
	x = (x ^ (uint32_t)w) * 16777619u;
	x = (x ^ (uint32_t)h) * 16777619u;
	const size_t n = (size_t)w * h;
	for (size_t i = 0; i < n; ++i)
		x = (x ^ pixels[i]) * 16777619u;
	return x;
}

const uint8_t *pack_picture(const uint8_t *pixels, int w, int h)
{
	if (npacks == 0)
		return NULL;
	const uint32_t want = pack_hash(pixels, w, h);
	for (int k = npacks - 1; k >= 0; --k)
	{
		const uint8_t *tab = packs[k].base + 24 + packs[k].files * 24;
		int lo = 0, hi = (int)packs[k].pics - 1;
		while (lo <= hi)                           /* sorted by hash: binary search */
		{
			int mid = (lo + hi) / 2;
			uint32_t hv = be32(tab + mid * 12);
			if (hv < want) { lo = mid + 1; continue; }
			if (hv > want) { hi = mid - 1; continue; }
			/* equal hash: look at the neighbours with the same hash too */
			while (mid > 0 && be32(tab + (mid - 1) * 12) == want)
				--mid;
			for (; mid < (int)packs[k].pics && be32(tab + mid * 12) == want; ++mid)
			{
				const uint8_t *e = tab + mid * 12;
				const uint8_t *data = packs[k].base + be32(e + 8);
				if (be16(e + 4) == w && be16(e + 6) == h && memcmp(data, pixels, (size_t)w * h) == 0)
					return data;
			}
			break;
		}
	}
	return NULL;
}

const uint8_t *pack_file_picture(const char *name, uint32_t start, int w, int h, uint32_t *end)
{
	char want[16];
	norm_name(name, want);
	for (int k = npacks - 1; k >= 0; --k)
	{
		const uint8_t *e = packs[k].base + 24 + packs[k].files * 24 + packs[k].pics * 12;
		for (uint32_t i = 0; i < packs[k].fpics; ++i, e += 32)
			if (be32(e + 16) == start && memcmp(e, want, 16) == 0 && be16(e + 24) == w && be16(e + 26) == h)
			{
				*end = be32(e + 20);
				return packs[k].base + be32(e + 28);
			}
	}
	return NULL;
}

const uint8_t *pack_file_picture_any(const char *name, uint32_t start, int *w, int *h, uint32_t *end)
{
	char want[16];
	norm_name(name, want);
	for (int k = npacks - 1; k >= 0; --k)
	{
		const uint8_t *e = packs[k].base + 24 + packs[k].files * 24 + packs[k].pics * 12;
		for (uint32_t i = 0; i < packs[k].fpics; ++i, e += 32)
			if (be32(e + 16) == start && memcmp(e, want, 16) == 0)
			{
				*w = be16(e + 24);
				*h = be16(e + 26);
				*end = be32(e + 20);
				return packs[k].base + be32(e + 28);
			}
	}
	return NULL;
}

bool pack_contains(const void *p)
{
	const uint8_t *q = (const uint8_t *)p;
	for (int k = 0; k < npacks; ++k)
		if (q >= packs[k].base && q < packs[k].base + packs[k].size)
			return true;
	return false;
}

#ifdef OJ32X_MARS
/* the 32X never bakes (port/pack_bake.c is PC only) */
void pack_bake_file(const char *name, const char *path) { (void)name; (void)path; }
void pack_bake_picture(const uint8_t *pixels, int w, int h) { (void)pixels; (void)w; (void)h; }
void pack_bake_file_picture(const char *name, uint32_t start, uint32_t end, const uint8_t *pixels, int w, int h)
{ (void)name; (void)start; (void)end; (void)pixels; (void)w; (void)h; }
#endif
