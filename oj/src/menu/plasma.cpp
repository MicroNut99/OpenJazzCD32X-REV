
/**
 *
 * @file plasma.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd June 2010: Created plasma.cpp
 *
 * @par Licence:
 * Copyright (c) 2010 Alireza Nejati
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Cool plasma effects for the main menu.
 *
 */


#include "plasma.h"
#ifdef OJ32X
#include "oj32x.h"
#include <string.h>
#include <stdint.h>
#endif

#include "SDL_wrapper.h"
#include "level/level.h"
#include "util.h"
#include "io/gfx/video.h"


/**
 * Create the plasma.
 */
Plasma::Plasma(){

	p0=0;
	p1=0;
	p2=0;
	p3=0;

	//fSin, fCos: pi = 512
	// -1024 < out < 1024
}

/**
 * Draw the plasma.
 *
 * @return Error code
 */
#ifdef OJ32X
/*
 * PORT32X: the same picture, much faster. The colour of a pixel is
 *   ((row term + column term) >> 10) & 15
 * where the column term depends only on x - so it is worked out once per frame for the 320
 * columns, and each pixel is one add and one shift; four pixels go out in one 32-bit write.
 * The screen is drawn in two halves through oj32x_parallel: both on the master SH2 by default,
 * one on each SH2 in builds with OJ32X_SLAVE_DRAWS.
 */
namespace {
	struct PlasmaJob { unsigned char* px; int w, h, pitch, p0, p1; const unsigned int* col; };
	void plasmaRows (void* arg, int part) {
		PlasmaJob* j = static_cast<PlasmaJob*>(arg);
		const int y0 = part ? j->h / 2 : 0, y1 = part ? j->h : j->h / 2;
		int t1 = j->p0 + 2 * y0, t2 = j->p1 + y0;
		for (int y = y0; y < y1; y++, t1 += 2, t2 += 1) {
			const unsigned int colb = (fCos(t1 * 4) << 3) + (fCos(t2 * 4) << 3) + (32 << 10);
			unsigned char* row = j->px + y * j->pitch;
			int x = 0;
			if (((uintptr_t)row & 3) == 0) {
				for (; x + 4 <= j->w; x += 4) {
					unsigned char c[4];
					for (int k = 0; k < 4; k++) c[k] = ((colb + j->col[x + k]) >> 10) & 0xF;
					uint32_t v;
					memcpy(&v, c, 4);
					*reinterpret_cast<uint32_t*>(row + x) = v;
				}
			}
			for (; x < j->w; x++) row[x] = ((colb + j->col[x]) >> 10) & 0xF;
		}
	}
}

int Plasma::draw(){

	static unsigned int col[1024];

	SDL_LockSurface(canvas);

	PlasmaJob job;
	job.px = static_cast<unsigned char*>(canvas->pixels);
	job.w = canvas->w < 1024 ? canvas->w : 1024;
	job.h = canvas->h;
	job.pitch = canvas->pitch;
	job.p0 = p0;
	job.p1 = p1;
	job.col = col;

	int t3 = p2, t4 = p3;
	for (int x = 0; x < job.w; x++, t3 += 3, t4 += 2)
		col[x] = (fCos(t3 * 4) << 3) + (fCos(t4 * 4) << 3);

	oj32x_parallel(plasmaRows, &job);

	p0 = p0 < 256 ? p0+1 : 1;
	p1 = p1 < 256 ? p1+2 : 2;
	p2 = p2 < 256 ? p2+3 : 3;
	p3 = p3 < 256 ? p3+4 : 4;

	SDL_UnlockSurface(canvas);

	return E_NONE;

}
#else
int Plasma::draw(){
	int x,y;

	int w,h,pitch;
	unsigned char *px;
	unsigned char colour;

	// draw plasma

	SDL_LockSurface(canvas);

	w 		= canvas->w;
	h 		= canvas->h;
	pitch 	= canvas->pitch;

	px = static_cast<unsigned char*>(canvas->pixels);

    int t1 = p0;
    int t2 = p1;
    for(y=0;y<h;y++){
        int t3 = p2;
        int t4 = p3;
		unsigned int colb = (fCos(t1*4)<<3)+(fCos(t2*4)<<3)+(32<<10);
        for(x=0;x<w;x++){

			colour = ((colb+(fCos(t3*4)<<3)+(fCos(t4*4)<<3))>>10) & 0xF;

            t3 += 3;
            t4 += 2;

            px[x] = colour;
		}
		// go to next row
		px += pitch;
        t1 += 2;
        t2 += 1;
	}

	p0 = p0 < 256 ? p0+1 : 1;
	p1 = p1 < 256 ? p1+2 : 2;
	p2 = p2 < 256 ? p2+3 : 3;
	p3 = p3 < 256 ? p3+4 : 4;

	SDL_UnlockSurface(canvas);

	return E_NONE;

}
#endif
