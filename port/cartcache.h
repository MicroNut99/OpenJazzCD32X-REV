/*
 * OpenJazz CD32X R5 - the 8 / 16 MB cart as a cache of the world packs (port/cartcache.c,
 * docs/CART_8MB_16MB.md). Only in the CART=8 / CART=16 test builds (OJ32X_CART_MB).
 */
#ifndef OJ32X_CARTCACHE_H
#define OJ32X_CARTCACHE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the pages the cart has (probed once): 2 = a 4 MB cart, 4 = 8 MB, 8 = 16 MB */
int cartcache_pages(void);

/* A world (or bonus) pack and its Genesis plane pack (gb_id 0: none) in a cache page, mapped
 * into window 1: from the cart when it is there and unchanged, else loaded from the disc.
 * 1: done, *world_off / *gb_off = their cart offsets (gb_off 0: no Genesis pack);
 * 0: not done (a 4 MB cart, or it does not fit) - the caller uses the home slot. */
int cartcache_world(int world_id, int gb_id, uint32_t *world_off, uint32_t *gb_off);

/* FILL: the packs of the list that are not in the cart yet, into pages with room (the first
 * boot); progress(i, n) nonzero stops it. Returns the number copied. */
/* R7: something other than cartcache_world wrote the home slot (a pack loaded there) */
void cartcache_home_changed(void);

int cartcache_fill(const int *ids, const int *gb_ids, int n, int (*progress)(int done, int total));

/* R10: the world pack is in the cart already (a page, or unpacked in the home slot) */
int cartcache_has(int world_id);

#ifdef __cplusplus
}
#endif

#endif
