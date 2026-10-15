/*
 * OpenJazz CD32X - the game's music (.PSM, played by the libxmp bundled with OpenJazz) as WAV
 * files for CD audio (J10b). 44.1 kHz, 16-bit, stereo. J11c: the song repeats itself (as in
 * the game) until max seconds, then fades out over 3 s; the console repeats the CD track.
 *   psm2wav <in.PSM> <out.wav> [max seconds]
 */
#include "xmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: psm2wav in.PSM out.wav [max seconds]\n"); return 2; }
	const int max_s = argc > 3 ? atoi(argv[3]) : 120;
	/* J11d: 4th argument 1 = one complete pass of the song (no repeats inside the file; the
	 * console repeats the CD track), the fade then covers the last 2 s of that pass */
	const int once = argc > 4 ? atoi(argv[4]) : 0;
	xmp_context c = xmp_create_context();
	if (xmp_load_module(c, argv[1]) != 0) { fprintf(stderr, "%s: cannot load\n", argv[1]); return 1; }
	if (xmp_start_player(c, 44100, 0) != 0) { fprintf(stderr, "%s: cannot play\n", argv[1]); return 1; }
	uint32_t max_bytes = (uint32_t)max_s * 44100u * 4u;
	if (once) {   /* measure one pass, then play it again for real */
		static int16_t tmp[4096];
		uint32_t pass = 0;
		while (pass < max_bytes && xmp_play_buffer(c, tmp, sizeof tmp, 1) == 0) pass += sizeof tmp;
		max_bytes = pass;
		xmp_end_player(c);
		xmp_start_player(c, 44100, 0);
	}
	FILE *f = fopen(argv[2], "wb");
	if (!f) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
	fwrite("data\0\0\0\0", 1, 8, f);
	static int16_t buf[4096];
	uint32_t bytes = 0;
	const uint32_t fade_bytes = (once ? 2u : 3u) * 44100u * 4u;
	while (bytes < max_bytes && xmp_play_buffer(c, buf, sizeof buf, 0) == 0) {   /* 0 = loop on */
		const int n = (int)(sizeof buf / 2);
		for (int i = 0; i < n; i++) {                /* 3 s fade-out at the end */
			const uint32_t at = bytes + (uint32_t)i * 2u;
			if (at + fade_bytes > max_bytes) {
				const double g = ((double)max_bytes - (double)at) / fade_bytes;
				buf[i] = (int16_t)(buf[i] * (g < 0 ? 0 : g));
			}
		}
		uint32_t n_out = sizeof buf;
		if (bytes + n_out > max_bytes) n_out = max_bytes - bytes;   /* exactly max seconds */
		fwrite(buf, 1, n_out, f);        /* little-endian host: WAV byte order */
		bytes += n_out;
	}
	fseek(f, 4, SEEK_SET); put32(f, 36 + bytes);
	fseek(f, 40, SEEK_SET); put32(f, bytes);
	fclose(f);
	xmp_end_player(c);
	xmp_release_module(c);
	xmp_free_context(c);
	printf("  %-14s %3u s\n", argv[2], bytes / (44100u * 4u));
	return 0;
}
