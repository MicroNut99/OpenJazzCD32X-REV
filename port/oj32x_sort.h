/*
 * OpenJazz CD32X - a sort that gives the same order on every machine.
 * stb_rect_pack sorts the font characters with qsort; qsort leaves equal elements in an order
 * that depends on the C library (glibc on the PC, newlib on the SH2). So the PC and the 32X
 * packed the font atlases differently, and the 32X did not find its atlas in the data pack
 * ("PICTURE 56X48 NOT IN THE PACK"). An insertion sort is stable: same input, same output.
 * Used through -DSTBRP_SORT=oj32x_stable_sort for ext/stb only.
 */
#ifndef OJ32X_SORT_H
#define OJ32X_SORT_H
#include <stddef.h>
#include <string.h>

static void oj32x_stable_sort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	unsigned char *a = (unsigned char *)base;
	unsigned char tmp[64];                     /* stbrp_rect is 24 bytes */
	if (size > sizeof tmp) return;
	for (size_t i = 1; i < n; ++i)
	{
		memcpy(tmp, a + i * size, size);
		size_t j = i;
		while (j > 0 && cmp(a + (j - 1) * size, tmp) > 0)
		{
			memcpy(a + j * size, a + (j - 1) * size, size);
			--j;
		}
		memcpy(a + j * size, tmp, size);
	}
}
#endif
