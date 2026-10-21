/*
 * ojlz - OpenJazz CD32X R8: a pack compressed for the 8 / 16 MB cart (docs/R8_COMPRESSED_PACKS.md).
 *
 *   ojlz in.pak out.pak        compress
 *   ojlz -t in.pak             compress, unpack again and compare (a test)
 *
 * The file: "OJLZ", the unpacked length (32 bits), the number of blocks (16 bits), 0 (16 bits),
 * then each block's packed length (16 bits), all big-endian; then the blocks, each at an
 * even offset (an odd one is followed by a 0 byte). Every block unpacks to 32768 bytes (the last one to the rest) and stands alone:
 * no match reaches into another block, so the Sub-CPU can unpack a pack a few blocks at a time.
 * A block is LZ4's block format (token, literals, 16-bit little-endian offset, match length).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 32768
#define HBITS 15
#define DEPTH 64
#define MINM  4

static int put_len(uint8_t *o, int n)
{
	int k = 0;
	while (n >= 255) { o[k++] = 255; n -= 255; }
	o[k++] = (uint8_t)n;
	return k;
}

/* one block: src[0..n) -> out, returns the packed length */
static int pack_block(const uint8_t *src, int n, uint8_t *out)
{
	static int head[1 << HBITS], prev[BLOCK];
	for (int i = 0; i < (1 << HBITS); i++) head[i] = -1;
	int o = 0, anchor = 0, i = 0;
	const int limit = n - 12;                 /* LZ4's rule: the last 12 bytes are not matched */
	while (i < limit) {
		const uint32_t v = (uint32_t)src[i] | src[i + 1] << 8 | src[i + 2] << 16 | (uint32_t)src[i + 3] << 24;
		const int h = (int)((v * 2654435761u) >> (32 - HBITS));
		int best = 0, boff = 0, c = head[h], d = 0;
		while (c >= 0 && d++ < DEPTH) {
			if (src[c] == src[i] && src[c + 1] == src[i + 1] && src[c + 2] == src[i + 2] && src[c + 3] == src[i + 3]) {
				int l = 4;
				while (i + l < n - 5 && src[c + l] == src[i + l]) l++;
				if (l > best) { best = l; boff = i - c; }
			}
			c = prev[c];
		}
		prev[i] = head[h]; head[h] = i;
		if (best < MINM) { i++; continue; }
		const int lit = i - anchor, m = best - MINM;
		out[o++] = (uint8_t)((lit < 15 ? lit : 15) << 4 | (m < 15 ? m : 15));
		if (lit >= 15) o += put_len(out + o, lit - 15);
		memcpy(out + o, src + anchor, (size_t)lit); o += lit;
		out[o++] = (uint8_t)boff; out[o++] = (uint8_t)(boff >> 8);
		if (m >= 15) o += put_len(out + o, m - 15);
		for (int j = i + 1; j < i + best && j < limit; j++) {
			const uint32_t w = (uint32_t)src[j] | src[j + 1] << 8 | src[j + 2] << 16 | (uint32_t)src[j + 3] << 24;
			const int hj = (int)((w * 2654435761u) >> (32 - HBITS));
			prev[j] = head[hj]; head[hj] = j;
		}
		i += best; anchor = i;
	}
	const int lit = n - anchor;
	out[o++] = (uint8_t)((lit < 15 ? lit : 15) << 4);
	if (lit >= 15) o += put_len(out + o, lit - 15);
	memcpy(out + o, src + anchor, (size_t)lit); o += lit;
	return o;
}

/* the Sub-CPU's decoder (cd32x/subcpu/main.c lz4_block), for the test */
static int unpack_block(const uint8_t *s, int slen, uint8_t *d)
{
	const uint8_t *end = s + slen; uint8_t *d0 = d;
	for (;;) {
		unsigned t = *s++, L = t >> 4;
		if (L == 15) { unsigned b; do { b = *s++; L += b; } while (b == 255); }
		while (L--) *d++ = *s++;
		if (s >= end) break;
		unsigned off = s[0] | s[1] << 8; s += 2;
		unsigned M = t & 15;
		if (M == 15) { unsigned b; do { b = *s++; M += b; } while (b == 255); }
		M += 4;
		const uint8_t *m = d - off;
		while (M--) *d++ = *m++;
	}
	return (int)(d - d0);
}

int main(int argc, char **argv)
{
	int test = argc > 1 && !strcmp(argv[1], "-t");
	if (argc != 3) { fprintf(stderr, "usage: ojlz in.pak out.pak | ojlz -t in.pak\n"); return 2; }
	const char *in = argv[test ? 2 : 1];
	FILE *f = fopen(in, "rb");
	if (!f) { perror(in); return 1; }
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t *src = malloc((size_t)n + 16);
	if (fread(src, 1, (size_t)n, f) != (size_t)n) { perror(in); return 1; }
	fclose(f);
	const int nb = (int)((n + BLOCK - 1) / BLOCK);
	uint8_t *out = malloc((size_t)n + (size_t)nb * 600 + 64 + 2 * (size_t)nb);
	int hdr = 12 + 2 * nb, o = hdr;
	for (int b = 0; b < nb; b++) {
		const int len = (int)(n - (long)b * BLOCK < BLOCK ? n - (long)b * BLOCK : BLOCK);
		const int c = pack_block(src + (long)b * BLOCK, len, out + o);
		if (c > 65535) { fprintf(stderr, "%s: block %d does not pack\n", in, b); return 1; }
		out[12 + 2 * b] = (uint8_t)(c >> 8); out[13 + 2 * b] = (uint8_t)c;
		o += c;
		if (o & 1) out[o++] = 0;               /* even: the next block starts on a word */
	}
	memcpy(out, "OJLZ", 4);
	out[4] = (uint8_t)(n >> 24); out[5] = (uint8_t)(n >> 16); out[6] = (uint8_t)(n >> 8); out[7] = (uint8_t)n;
	out[8] = (uint8_t)(nb >> 8); out[9] = (uint8_t)nb; out[10] = out[11] = 0;
	if (test) {
		uint8_t *chk = malloc((size_t)n + BLOCK);
		int p = hdr;
		for (int b = 0; b < nb; b++) {
			const int c = out[12 + 2 * b] << 8 | out[13 + 2 * b];
			const int len = (int)(n - (long)b * BLOCK < BLOCK ? n - (long)b * BLOCK : BLOCK);
			int got = unpack_block(out + p, c, chk + (long)b * BLOCK);
			if (got < len || memcmp(chk + (long)b * BLOCK, src + (long)b * BLOCK, (size_t)len)) {
				fprintf(stderr, "%s: block %d differs\n", in, b); return 1;
			}
			p += (c + 1) & ~1;
		}
		printf("%s: %ld -> %d bytes (%d%%), %d blocks, unpacks the same\n", in, n, o, (int)(100.0 * o / n), nb);
		return 0;
	}
	f = fopen(argv[2], "wb");
	if (!f || fwrite(out, 1, (size_t)o, f) != (size_t)o) { perror(argv[2]); return 1; }
	fclose(f);
	return 0;
}
