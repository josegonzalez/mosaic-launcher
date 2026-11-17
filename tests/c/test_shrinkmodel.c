// shrinkmodel.c: image_load_fit's header sizes, fitting and shrinking.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "shrinkmodel.h"

static void put32(unsigned char* p, unsigned long v) {
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static size_t png(unsigned char* out, unsigned long w, unsigned long h) {
	static const unsigned char head[16] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
	memcpy(out, head, 16);
	put32(out + 16, w);
	put32(out + 20, h);
	memset(out + 24, 0, 9);
	return 33;
}

static void testPng(void) {
	unsigned char d[64];
	int w = 0, h = 0;
	size_t n = png(d, 2550, 3300);
	CHECK(Shrink_headerSize(d, n, &w, &h) == 0);
	CHECK(w == 2550 && h == 3300);
	// cut short, the wrong signature, no IHDR first and a side of 0
	CHECK(Shrink_headerSize(d, 23, &w, &h) == -1);
	d[1] = 'X';
	CHECK(Shrink_headerSize(d, n, &w, &h) == -1);
	png(d, 10, 10);
	d[12] = 'i';
	CHECK(Shrink_headerSize(d, n, &w, &h) == -1);
	png(d, 0, 10);
	CHECK(Shrink_headerSize(d, n, &w, &h) == -1);
	// a side past what an int holds
	png(d, 0x80000000ul, 10);
	CHECK(Shrink_headerSize(d, n, &w, &h) == -1);
	CHECK(Shrink_headerSize(NULL, n, &w, &h) == -1);
}

static void testJpeg(void) {
	int w = 0, h = 0;
	// SOI, an APP0 of 16 bytes, a DHT, then SOF2 (progressive) 640 wide and 480 tall
	unsigned char d[64] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10 };
	size_t at = 6 + 14;
	unsigned char dht[] = { 0xFF, 0xC4, 0x00, 0x04, 0x00, 0x00 };
	memcpy(d + at, dht, sizeof(dht));
	at += sizeof(dht);
	unsigned char sof[] = { 0xFF, 0xFF, 0xC2, 0x00, 0x11, 0x08, 0x01, 0xE0, 0x02, 0x80, 0x03 };
	memcpy(d + at, sof, sizeof(sof));
	at += sizeof(sof);
	CHECK(Shrink_headerSize(d, at, &w, &h) == 0);
	CHECK(w == 640 && h == 480);
	// cut inside the frame's size
	CHECK(Shrink_headerSize(d, at - 2, &w, &h) == -1);
	// the scan before any frame
	unsigned char sos[] = { 0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x08 };
	CHECK(Shrink_headerSize(sos, sizeof(sos), &w, &h) == -1);
	// a segment whose length runs off the end, and garbage between segments
	unsigned char longer[] = { 0xFF, 0xD8, 0xFF, 0xE1, 0xFF, 0xFF, 0x00 };
	CHECK(Shrink_headerSize(longer, sizeof(longer), &w, &h) == -1);
	unsigned char junk[] = { 0xFF, 0xD8, 0x12, 0x34 };
	CHECK(Shrink_headerSize(junk, sizeof(junk), &w, &h) == -1);
	// a frame of 0 lines
	unsigned char zero[] = { 0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x00, 0x02, 0x80 };
	CHECK(Shrink_headerSize(zero, sizeof(zero), &w, &h) == -1);
	// neither
	CHECK(Shrink_headerSize((const unsigned char*)"GIF89a....", 10, &w, &h) == -1);
}

static void testAllowed(void) {
	CHECK(Shrink_allowed(1, 1));
	CHECK(Shrink_allowed(2550, 3300));
	CHECK(Shrink_allowed(5100, 6600));
	CHECK(Shrink_allowed(SHRINK_MAX_SIDE, 4000));
	CHECK(!Shrink_allowed(SHRINK_MAX_SIDE + 1, 10));
	CHECK(!Shrink_allowed(10, SHRINK_MAX_SIDE + 1));
	CHECK(!Shrink_allowed(8000, 8000)); // 64 million pixels
	CHECK(!Shrink_allowed(0, 10));
	CHECK(!Shrink_allowed(10, -1));
}

static void testFit(void) {
	int w = 0, h = 0;
	// a portrait page into a 640x432 view: the height decides
	CHECK(Shrink_fit(2550, 3300, 640, 432, &w, &h) == 0);
	CHECK(w == 334 && h == 432);
	// a landscape one, still too tall for it
	CHECK(Shrink_fit(3300, 2550, 640, 432, &w, &h) == 0);
	CHECK(w == 559 && h == 432);
	CHECK(Shrink_fit(4000, 1000, 640, 432, &w, &h) == 0);
	CHECK(w == 640 && h == 160);
	// one that fits keeps its size, never enlarged
	CHECK(Shrink_fit(300, 200, 640, 432, &w, &h) == 0);
	CHECK(w == 300 && h == 200);
	CHECK(Shrink_fit(640, 432, 640, 432, &w, &h) == 0);
	CHECK(w == 640 && h == 432);
	// a sliver keeps a pixel
	CHECK(Shrink_fit(8000, 1, 100, 100, &w, &h) == 0);
	CHECK(w == 100 && h == 1);
	CHECK(Shrink_fit(0, 10, 100, 100, &w, &h) == -1);
	CHECK(Shrink_fit(10, 10, 0, 100, &w, &h) == -1);
}

// shrinks src, sw by sh, to dw by dh into out
static int shrink(const uint32_t* src, int sw, int sh, uint32_t* out, int dw, int dh) {
	Shrink* s = Shrink_new(sw, sh, dw, dh);
	if (!s) return -1;
	for (int y = 0; y < sh; y++) {
		if (Shrink_row(s, src + y * sw, out, dw) != 0) {
			Shrink_free(s);
			return -1;
		}
		// rows come out as they're finished, not all at the end
		if (y == sh - 1) CHECK(Shrink_done(s));
		else if (dh > 1 && y == 0 && sh / dh > 1) CHECK(!Shrink_done(s));
	}
	int past = Shrink_row(s, src, out, dw);
	Shrink_free(s);
	return past == -1 ? 0 : -1;
}

static void testShrink(void) {
	// one colour stays that colour, at any ratio
	uint32_t flat[7 * 5];
	for (int i = 0; i < 7 * 5; i++) flat[i] = 0xFF336699u;
	uint32_t out[16];
	CHECK(shrink(flat, 7, 5, out, 3, 2) == 0);
	for (int i = 0; i < 6; i++) CHECK(out[i] == 0xFF336699u);

	// 2x2 to 1: the average
	uint32_t four[4] = { 0xFF000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFF000000u };
	CHECK(shrink(four, 2, 2, out, 1, 1) == 0);
	CHECK(out[0] == 0xFF808080u);

	// 3 to 2 across: the middle pixel is split between both
	uint32_t three[3] = { 0xFF000000u, 0xFFF0F0F0u, 0xFF000000u };
	CHECK(shrink(three, 3, 1, out, 2, 1) == 0);
	CHECK(out[0] == 0xFF505050u && out[1] == 0xFF505050u);

	// a transparent pixel's colour doesn't bleed in, but its alpha counts
	uint32_t edge[2] = { 0xFFFF0000u, 0x0000FF00u };
	CHECK(shrink(edge, 2, 1, out, 1, 1) == 0);
	CHECK(out[0] == 0x80FF0000u);
	uint32_t clear[4] = { 0x00FFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu };
	CHECK(shrink(clear, 2, 2, out, 1, 1) == 0);
	CHECK(out[0] == 0);

	// the same size is a copy
	uint32_t same[6] = { 1u << 24 | 5, 0xFF010203u, 0x80FF00FFu, 0xFFFFFFFFu, 0xFF000000u, 0xFFABCDEFu };
	CHECK(shrink(same, 3, 2, out, 3, 2) == 0);
	CHECK(out[1] == 0xFF010203u && out[2] == 0x80FF00FFu && out[5] == 0xFFABCDEFu);

	// down only: 4 tall to 3, rows weighted 3:1, 2:2 and 1:3
	uint32_t col[4] = { 0xFF000000u, 0xFFC0C0C0u, 0xFF000000u, 0xFFC0C0C0u };
	CHECK(shrink(col, 1, 4, out, 1, 3) == 0);
	CHECK(out[0] == 0xFF303030u && out[1] == 0xFF606060u && out[2] == 0xFF909090u);

	// a bigger image, written only inside dw by dh
	int sw = 301, sh = 199, dw = 97, dh = 64;
	uint32_t* big = malloc(sizeof(uint32_t) * sw * sh);
	uint32_t* res = malloc(sizeof(uint32_t) * (dw + 1) * (dh + 1));
	for (int i = 0; i < sw * sh; i++) big[i] = 0xFF000000u | (uint32_t)(i % 251);
	for (int i = 0; i < (dw + 1) * (dh + 1); i++) res[i] = 0xDEADBEEFu;
	Shrink* s = Shrink_new(sw, sh, dw, dh);
	CHECK(s != NULL);
	for (int y = 0; s && y < sh; y++) Shrink_row(s, big + y * sw, res, dw + 1);
	CHECK(Shrink_done(s));
	Shrink_free(s);
	int guards = 1;
	for (int y = 0; y <= dh; y++) {
		for (int x = 0; x <= dw; x++) {
			int inside = x < dw && y < dh;
			if (inside == (res[y * (dw + 1) + x] == 0xDEADBEEFu)) guards = 0;
		}
	}
	CHECK(guards);
	free(big);
	free(res);

	// never enlarged, and nothing out of range
	CHECK(Shrink_new(10, 10, 11, 5) == NULL);
	CHECK(Shrink_new(10, 10, 5, 0) == NULL);
	CHECK(Shrink_new(SHRINK_MAX_SIDE + 1, 10, 5, 5) == NULL);
	CHECK(Shrink_row(NULL, flat, out, 1) == -1);
	CHECK(!Shrink_done(NULL));
	Shrink_free(NULL);
}

// A JPEG's start: SOI, then segs APP1 segments of 65535 bytes, then a SOF0
// w by h, in out (which must hold its size), returning its size.
static size_t bigJpeg(unsigned char* out, int segs, unsigned w, unsigned h) {
	size_t at = 0;
	out[at++] = 0xFF;
	out[at++] = 0xD8;
	for (int i = 0; i < segs; i++) {
		unsigned char app[4] = { 0xFF, 0xE1, 0xFF, 0xFF };
		memcpy(out + at, app, 4);
		memset(out + at + 4, 0, 65533);
		at += 4 + 65533;
	}
	unsigned char sof[] = { 0xFF, 0xC0, 0x00, 0x11, 0x08, h >> 8, h & 0xFF, w >> 8, w & 0xFF, 0x03 };
	memcpy(out + at, sof, sizeof(sof));
	return at + sizeof(sof);
}

static void testCheck(void) {
	unsigned char d[64];
	int w = 0, h = 0;
	// a 33-byte PNG and an 11-byte JPEG whose headers say they're huge:
	// the size is read, and it's one that isn't decoded
	size_t n = png(d, 100000, 100000);
	CHECK(Shrink_headerCheck(d, n, &w, &h) == 0);
	CHECK(w == 100000 && h == 100000 && !Shrink_allowed(w, h));
	unsigned char huge[] = { 0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0xFF, 0xFF, 0xFF, 0xFF };
	CHECK(Shrink_headerCheck(huge, sizeof(huge), &w, &h) == 0);
	CHECK(w == 65535 && h == 65535 && !Shrink_allowed(w, h));
	// 9000x1 is over a side, 7000x7000 over the pixels
	n = png(d, 9000, 1);
	CHECK(Shrink_headerCheck(d, n, &w, &h) == 0 && !Shrink_allowed(w, h));
	n = png(d, 7000, 7000);
	CHECK(Shrink_headerCheck(d, n, &w, &h) == 0 && !Shrink_allowed(w, h));
	// just over image_load's 4096 a side is within these limits, so the
	// size is read for image_load to refuse it itself
	n = png(d, 4097, 1);
	CHECK(Shrink_headerCheck(d, n, &w, &h) == 0);
	CHECK(w == 4097 && h == 1 && Shrink_allowed(w, h));
	// ordinary sizes are decoded
	n = png(d, 640, 480);
	CHECK(Shrink_headerCheck(d, n, &w, &h) == 0);
	CHECK(w == 640 && h == 480 && Shrink_allowed(w, h));
	// other formats are decoded as before, their size unknown
	CHECK(Shrink_headerCheck((const unsigned char*)"GIF89a....", 10, &w, &h) == SHRINK_UNKNOWN);
	CHECK(Shrink_headerCheck((const unsigned char*)"BM\x46\0\0\0", 6, &w, &h) == SHRINK_UNKNOWN);
	CHECK(Shrink_headerCheck((const unsigned char*)"\x89PN", 3, &w, &h) == SHRINK_UNKNOWN);
	CHECK(Shrink_headerCheck(d, 0, &w, &h) == SHRINK_UNKNOWN);
	// a PNG or JPEG whose size can't be read isn't: a PNG cut short, one
	// with the start of its signature only, and a JPEG with junk before
	// its frame, which libjpeg would skip
	CHECK(Shrink_headerCheck(d, 20, &w, &h) == -1);
	CHECK(Shrink_headerCheck((const unsigned char*)"\x89PNG\0\0\0\0", 8, &w, &h) == -1);
	unsigned char junk[] = { 0xFF, 0xD8, 0x00, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0xFF, 0xFF, 0xFF, 0xFF };
	CHECK(Shrink_headerCheck(junk, sizeof(junk), &w, &h) == -1);
	CHECK(Shrink_headerCheck(huge, 2, &w, &h) == -1);
	CHECK(Shrink_headerCheck(NULL, 10, &w, &h) == -1);
	CHECK(Shrink_headerCheck(d, n, NULL, &h) == -1);
}

// A file in memory, handed out at most chunk bytes a read, or past its
// end, when endless, as APP1 segments for ever.
typedef struct {
	const unsigned char* data;
	size_t size, at, chunk, total;
	int endless, extra;
} Mem;

static size_t memRead(void* ctx, void* buf, size_t n) {
	Mem* m = ctx;
	if (m->extra) return n + 1;
	if (n > m->chunk) n = m->chunk;
	unsigned char* out = buf;
	size_t got = 0;
	for (; got < n; got++, m->at++) {
		if (m->at < m->size) out[got] = m->data[m->at];
		else if (!m->endless) break;
		else {
			size_t i = (m->at - m->size) % 65537;
			out[got] = i == 0 ? 0xFF : i == 1 ? 0xE1 : i < 4 ? 0xFF : 0;
		}
	}
	m->total += got;
	return got;
}

static void testReadCheck(void) {
	unsigned char d[64];
	int w = 0, h = 0;
	// the huge PNG is refused after reading the file's 33 bytes
	size_t n = png(d, 100000, 100000);
	Mem m = { d, n, 0, 7, 0, 0, 0 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == 0);
	CHECK(w == 100000 && h == 100000 && m.total == n);
	// a JPEG whose frame comes after three 64 KiB segments is read until
	// it's found, and not much further
	size_t big = 3 * 65537 + 64;
	unsigned char* j = malloc(big + 100000);
	n = bigJpeg(j, 3, 30000, 20000);
	memset(j + n, 0, 100000);
	m = (Mem){ j, n + 100000, 0, 4096, 0, 0, 0 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == 0);
	CHECK(w == 30000 && h == 20000 && !Shrink_allowed(w, h));
	CHECK(m.total >= n && m.total <= 2 * n);
	// and one at an ordinary size is decoded
	n = bigJpeg(j, 3, 640, 480);
	m = (Mem){ j, n, 0, 65536, 0, 0, 0 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == 0 && w == 640 && h == 480);
	// the same without its frame: read to its end, and its size can't be read
	m = (Mem){ j, n - 10, 0, 65536, 0, 0, 0 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == -1 && m.total == n - 10);
	// segments for ever: it gives up after SHRINK_HEADER_MAX
	m = (Mem){ j, 2, 0, 1 << 20, 0, 1, 0 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == -1 && m.total == SHRINK_HEADER_MAX);
	free(j);
	// another format reads only the first part
	unsigned char gif[40000] = "GIF89a";
	m = (Mem){ gif, sizeof(gif), 0, 1 << 20, 0, 0, 0 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == SHRINK_UNKNOWN && m.total == 16 * 1024);
	// an empty file, a reader that claims more than it was asked for, and no reader
	m = (Mem){ gif, 0, 0, 10, 0, 0, 0 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == SHRINK_UNKNOWN);
	m = (Mem){ gif, sizeof(gif), 0, 10, 0, 0, 1 };
	CHECK(Shrink_readCheck(memRead, &m, &w, &h) == SHRINK_UNKNOWN);
	CHECK(Shrink_readCheck(NULL, &m, &w, &h) == -1);
	CHECK(Shrink_readCheck(memRead, &m, &w, NULL) == -1);
}

// The whole check, header and refusal together, as the launcher's loads
// use it: ordinary art isn't refused, and a refusal names the real size
// (gcc once read the size before the check had written it, refusing every
// PNG as 0x0).
static void testRefusal(void) {
	unsigned char d[64];
	char why[96];
	size_t n = png(d, 4, 2);
	CHECK(Shrink_dataRefusal(d, n, why, sizeof(why)) == NULL);
	n = png(d, 900, 1200);
	CHECK(Shrink_dataRefusal(d, n, why, sizeof(why)) == NULL);
	Mem m = { d, n, 0, 7, 0, 0, 0 };
	CHECK(Shrink_readRefusal(memRead, &m, why, sizeof(why)) == NULL);
	unsigned char* j = malloc(3 * 65537 + 64);
	n = bigJpeg(j, 3, 640, 480);
	m = (Mem){ j, n, 0, 4096, 0, 0, 0 };
	CHECK(Shrink_readRefusal(memRead, &m, why, sizeof(why)) == NULL);
	free(j);
	CHECK(Shrink_dataRefusal((const unsigned char*)"BM\x46\0\0\0", 6, why, sizeof(why)) == NULL);
	n = png(d, 100000, 100000);
	const char* no = Shrink_dataRefusal(d, n, why, sizeof(why));
	CHECK(no && !strcmp(no, "100000x100000 is over 8192 a side or 40000000 pixels"));
	m = (Mem){ d, n, 0, 7, 0, 0, 0 };
	no = Shrink_readRefusal(memRead, &m, why, sizeof(why));
	CHECK(no && !strcmp(no, "100000x100000 is over 8192 a side or 40000000 pixels"));
	no = Shrink_dataRefusal(d, 20, why, sizeof(why));
	CHECK(no && !strcmp(no, "its PNG or JPEG header can't be read"));
	// image_load's own 4096 a side, from the same answer
	CHECK(Shrink_refusal(0, 4096, 4096, 4096, why, sizeof(why)) == NULL);
	no = Shrink_refusal(0, 4097, 1, 4096, why, sizeof(why));
	CHECK(no && !strcmp(no, "4097x1 is over 4096 a side"));
	no = Shrink_refusal(0, 9000, 1, 4096, why, sizeof(why));
	CHECK(no && !strcmp(no, "9000x1 is over 8192 a side or 40000000 pixels"));
	CHECK(Shrink_refusal(SHRINK_UNKNOWN, 0, 0, 4096, why, sizeof(why)) == NULL);
}

int main(void) {
	testPng();
	testJpeg();
	testAllowed();
	testCheck();
	testReadCheck();
	testRefusal();
	testFit();
	testShrink();
	CHECK_DONE();
}
