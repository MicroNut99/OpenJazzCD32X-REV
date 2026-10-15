/*
 * OpenJazz CD32X - an allocator for the RAM cart, used by the PC build only.
 *
 * On the console the SH2 can read the 4 MB RAM cart but not write it (writes go through the
 * 68000), so the 32X never allocates there: everything in the cart comes from the packs
 * (pack.h). The PC build imitates the cart as read-only memory and can still place pictures
 * that are missing from a pack in it; any write other than through cart_write() crashes at
 * once - a test that the game never writes to cart data.
 *
 * The block table (who owns which part) lives in normal memory: CART_BLOCKS entries.
 */
#ifndef OJ32X_CART_H
#define OJ32X_CART_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef OJ32X_MARS
#define CART_BLOCKS 16      /* the 32X never allocates in the cart (the SH2 cannot write it) */
#else
#define CART_BLOCKS 1024
#endif

void  cart_init(void);
void *cart_alloc(size_t bytes);        /* 4-byte aligned; NULL when full */
void  cart_free(void *p);              /* NULL is fine */
void  cart_write(void *dst, const void *src, size_t bytes);
bool  cart_contains(const void *p);
size_t cart_used(void);
size_t cart_peak(void);
size_t cart_size(void);
int    cart_blocks(void);

#ifdef __cplusplus
}
#endif

#endif
