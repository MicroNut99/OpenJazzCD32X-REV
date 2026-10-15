/* OpenJazz CD32X - checks of the SDL 1.2 shim against SDL 1.2's documented behaviour.
 *   make -C host test */
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static Uint8 px(SDL_Surface *s, int x, int y) { return static_cast<Uint8 *>(s->pixels)[y * s->pitch + x]; }

int main()
{
	SDL_Surface *dst = SDL_CreateRGBSurface(0, 16, 16, 8, 0, 0, 0, 0);
	SDL_Surface *src = SDL_CreateRGBSurface(0, 4, 4, 8, 0, 0, 0, 0);
	for (int i = 0; i < 16; ++i) static_cast<Uint8 *>(src->pixels)[i] = (Uint8)(10 + i);
	SDL_FillRect(dst, nullptr, 7);
	CHECK(px(dst, 0, 0) == 7 && px(dst, 15, 15) == 7);

	/* plain copy */
	SDL_Rect r = {2, 3, 0, 0};
	SDL_BlitSurface(src, nullptr, dst, &r);
	CHECK(px(dst, 2, 3) == 10 && px(dst, 5, 6) == 25 && px(dst, 6, 3) == 7);
	CHECK(r.w == 4 && r.h == 4);

	/* colour key: index 15 not copied */
	SDL_FillRect(dst, nullptr, 7);
	SDL_SetColorKey(src, SDL_SRCCOLORKEY, 15);
	r.x = 0; r.y = 0;
	SDL_BlitSurface(src, nullptr, dst, &r);
	CHECK(px(dst, 1, 1) == 7);   /* src (1,1) = 15 */
	CHECK(px(dst, 0, 1) == 14);

	/* recolour (logical palette entry 14 -> grey 99): the blit maps 14 to 99 */
	SDL_Color c = {99, 99, 99, 0};
	SDL_SetPalette(src, SDL_LOGPAL, &c, 14, 1);
	SDL_BlitSurface(src, nullptr, dst, &r);
	CHECK(px(dst, 0, 1) == 99);
	CHECK(px(dst, 1, 1) == 7);   /* the key is tested on the source index, before mapping */

	/* clipping: negative position and clip rectangle; dstrect gets the clipped area */
	SDL_FillRect(dst, nullptr, 7);
	SDL_SetColorKey(src, 0, 0);
	SDL_Rect clip = {4, 4, 8, 8};
	SDL_SetClipRect(dst, &clip);
	SDL_Rect r2 = {2, 3, 0, 0};
	SDL_BlitSurface(src, nullptr, dst, &r2);
	CHECK(px(dst, 3, 4) == 7 && px(dst, 4, 3) == 7);
	CHECK(px(dst, 4, 4) == 10 + 1 * 4 + 2);
	CHECK(r2.x == 4 && r2.y == 4 && r2.w == 2 && r2.h == 3);
	SDL_Rect r3 = {-3, 0, 0, 0};
	SDL_SetClipRect(dst, nullptr);
	SDL_BlitSurface(src, nullptr, dst, &r3);
	CHECK(px(dst, 0, 0) == 13 && r3.w == 1);
	SDL_Rect off = {20, 20, 0, 0};
	SDL_BlitSurface(src, nullptr, dst, &off);
	CHECK(off.w == 0 && off.h == 0);

	/* source rectangle */
	SDL_Rect sr = {1, 1, 2, 2}, dr = {10, 10, 0, 0};
	SDL_BlitSurface(src, &sr, dst, &dr);
	CHECK(px(dst, 10, 10) == 15 && px(dst, 11, 10) == 16 && px(dst, 11, 11) == 20);

	/* fill clipped */
	SDL_SetClipRect(dst, &clip);
	SDL_Rect fr = {0, 0, 6, 6};
	SDL_FillRect(dst, nullptr, 7);
	SDL_FillRect(dst, &fr, 3);
	CHECK(px(dst, 3, 3) == 7 && px(dst, 4, 4) == 3 && px(dst, 6, 6) == 7);

	/* restoring the grey ramp makes the blit a plain copy again */
	SDL_Color g = {14, 14, 14, 0};
	SDL_SetPalette(src, SDL_LOGPAL, &g, 14, 1);
	SDL_SetClipRect(dst, nullptr);
	SDL_Rect r4 = {0, 0, 0, 0};
	SDL_BlitSurface(src, nullptr, dst, &r4);
	CHECK(px(dst, 0, 1) == 14);
	CHECK(src->format->palette->colors[14].r == 14);

	/* MapRGB on the grey ramp */
	CHECK(SDL_MapRGB(dst->format, 0, 0, 0) == 0);
	CHECK(SDL_MapRGB(dst->format, 50, 50, 50) == 50);

	SDL_FreeSurface(src);
	SDL_FreeSurface(dst);
	printf(fails ? "%d check(s) failed\n" : "shim checks: all passed\n", fails);
	return fails != 0;
}
