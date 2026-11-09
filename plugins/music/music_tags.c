#include "music_tags.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "music_model.h"

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

void MuTags_init(MuTags* tags) {
	memset(tags, 0, sizeof(*tags));
}

void MuTags_free(MuTags* tags) {
	free(tags->picture);
	MuTags_init(tags);
}

static int hasText(const MuTags* tags) {
	return tags->title[0] || tags->artist[0] || tags->album[0];
}

///////////////////////////////////////
// sources

static int memoryRead(void* ctx, unsigned long long off, void* out, size_t len) {
	const MuMemory* mem = ctx;
	if (off > mem->size || len > mem->size - off) return -1;
	if (len) memcpy(out, mem->data + off, len);
	return 0;
}

void MU_memorySource(MuSource* src, MuMemory* mem, const void* data, size_t size) {
	mem->data = data;
	mem->size = data ? size : 0;
	src->read = memoryRead;
	src->ctx = mem;
	src->size = mem->size;
}

static int srcRead(const MuSource* src, unsigned long long off, void* out, size_t len) {
	if (off > src->size || len > src->size - off) return -1;
	return src->read(src->ctx, off, out, len);
}

// part of another source, from off for size bytes
typedef struct Window {
	const MuSource* parent;
	unsigned long long off;
} Window;

static int windowRead(void* ctx, unsigned long long off, void* out, size_t len) {
	const Window* w = ctx;
	return srcRead(w->parent, w->off + off, out, len);
}

static int window(MuSource* src, Window* w, const MuSource* parent, unsigned long long off, unsigned long long size) {
	if (off > parent->size || size > parent->size - off) return -1;
	w->parent = parent;
	w->off = off;
	src->read = windowRead;
	src->ctx = w;
	src->size = size;
	return 0;
}

// Reads len bytes into a new buffer, or NULL.
static unsigned char* srcAlloc(const MuSource* src, unsigned long long off, size_t len) {
	unsigned char* buf = malloc(len ? len : 1);
	if (!buf) return NULL;
	if (srcRead(src, off, buf, len) != 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

static unsigned int be32(const unsigned char* p) {
	return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3];
}

static unsigned int le32(const unsigned char* p) {
	return (unsigned int)p[3] << 24 | (unsigned int)p[2] << 16 | (unsigned int)p[1] << 8 | p[0];
}

// 0 when a byte has its top bit set
static int syncsafe(const unsigned char* p, unsigned int* out) {
	if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return 0;
	*out = (unsigned int)p[0] << 21 | (unsigned int)p[1] << 14 | (unsigned int)p[2] << 7 | p[3];
	return 1;
}

///////////////////////////////////////
// text

typedef struct Out {
	char* buf;
	size_t size, len;
	int full;
	int space; // a space waits for the next character
} Out;

static void put(Out* o, unsigned int cp) {
	if (o->full) return;
	if (cp == '\t' || cp == '\n' || cp == '\r' || cp == ' ') {
		if (o->len) o->space = 1;
		return;
	}
	if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return;
	if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
	unsigned char b[5];
	size_t n;
	if (cp < 0x80) {
		b[0] = (unsigned char)cp;
		n = 1;
	} else if (cp < 0x800) {
		b[0] = (unsigned char)(0xC0 | cp >> 6);
		b[1] = (unsigned char)(0x80 | (cp & 0x3F));
		n = 2;
	} else if (cp < 0x10000) {
		b[0] = (unsigned char)(0xE0 | cp >> 12);
		b[1] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
		b[2] = (unsigned char)(0x80 | (cp & 0x3F));
		n = 3;
	} else {
		b[0] = (unsigned char)(0xF0 | cp >> 18);
		b[1] = (unsigned char)(0x80 | (cp >> 12 & 0x3F));
		b[2] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
		b[3] = (unsigned char)(0x80 | (cp & 0x3F));
		n = 4;
	}
	size_t need = n + (o->space ? 1 : 0);
	if (o->len + need >= o->size) {
		o->full = 1;
		return;
	}
	if (o->space) o->buf[o->len++] = ' ';
	o->space = 0;
	memcpy(o->buf + o->len, b, n);
	o->len += n;
}

// One UTF-8 character from p (n bytes left): its code point, or U+FFFD for
// a bad byte, with *used set to how many bytes it took.
static unsigned int utf8Char(const unsigned char* p, size_t n, size_t* used) {
	*used = 1;
	unsigned int c = p[0];
	if (c < 0x80) return c;
	size_t len;
	unsigned int cp, min;
	if (c >= 0xC2 && c <= 0xDF) {
		len = 2;
		cp = c & 0x1F;
		min = 0x80;
	} else if (c >= 0xE0 && c <= 0xEF) {
		len = 3;
		cp = c & 0x0F;
		min = 0x800;
	} else if (c >= 0xF0 && c <= 0xF4) {
		len = 4;
		cp = c & 0x07;
		min = 0x10000;
	} else return 0xFFFD;
	if (n < len) return 0xFFFD;
	for (size_t i = 1; i < len; i++) {
		if ((p[i] & 0xC0) != 0x80) return 0xFFFD;
		cp = cp << 6 | (p[i] & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
	*used = len;
	return cp;
}

size_t MU_tagText(int encoding, const unsigned char* text, size_t len, char* out, size_t size) {
	if (!size) return 0;
	Out o = { out, size, 0, 0, 0 };
	out[0] = '\0';
	if (!text) return 0;
	if (encoding == 0) {
		for (size_t i = 0; i < len && text[i] && !o.full; i++) put(&o, text[i]);
	} else if (encoding == 3) {
		for (size_t i = 0; i < len && text[i] && !o.full;) {
			size_t used;
			put(&o, utf8Char(text + i, len - i, &used));
			i += used;
		}
	} else if (encoding == 1 || encoding == 2) {
		int big = encoding == 2;
		size_t i = 0;
		if (len >= 2 && text[0] == 0xFF && text[1] == 0xFE) {
			big = 0;
			i = 2;
		} else if (len >= 2 && text[0] == 0xFE && text[1] == 0xFF) {
			big = 1;
			i = 2;
		}
		for (; i + 1 < len && !o.full; i += 2) {
			unsigned int u = big ? (unsigned int)text[i] << 8 | text[i + 1] : (unsigned int)text[i + 1] << 8 | text[i];
			if (!u) break;
			if (u >= 0xD800 && u <= 0xDBFF && i + 3 < len) {
				unsigned int v = big ? (unsigned int)text[i + 2] << 8 | text[i + 3]
					: (unsigned int)text[i + 3] << 8 | text[i + 2];
				if (v >= 0xDC00 && v <= 0xDFFF) {
					put(&o, 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00));
					i += 2;
					continue;
				}
			}
			put(&o, u); // a lone surrogate becomes U+FFFD
		}
	}
	// a cut part way through a run of spaces leaves none at the end
	out[o.len] = '\0';
	return o.len;
}

static void setText(char* field, int encoding, const unsigned char* text, size_t len) {
	if (field[0]) return;
	MU_tagText(encoding, text, len, field, MU_TAG_TEXT_MAX);
}

long MU_base64Decode(const char* in, size_t len, unsigned char* out, size_t size) {
	unsigned int acc = 0;
	int bits = 0;
	size_t n = 0;
	int ended = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)in[i];
		int v;
		if (c >= 'A' && c <= 'Z') v = c - 'A';
		else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
		else if (c >= '0' && c <= '9') v = c - '0' + 52;
		else if (c == '+') v = 62;
		else if (c == '/') v = 63;
		else if (c == '=') {
			ended = 1;
			continue;
		} else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
		else return -1;
		if (ended) return -1;
		acc = (acc << 6 | (unsigned int)v) & 0xFFFFFF;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (n >= size) return -1;
			out[n++] = (unsigned char)(acc >> bits);
		}
	}
	return (long)n;
}

///////////////////////////////////////
// pictures

int MU_pictureKind(const unsigned char* data, size_t size) {
	if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return MU_PICTURE_JPEG;
	if (size >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8)) return MU_PICTURE_PNG;
	return MU_PICTURE_NONE;
}

const char* MU_pictureExt(int kind) {
	return kind == MU_PICTURE_JPEG ? "jpg" : kind == MU_PICTURE_PNG ? "png" : "";
}

// Takes a picture of type (3 for the front cover) when it's the first, or
// the first front cover, copying data.
static void offerPicture(MuTags* tags, unsigned int type, const unsigned char* data, size_t size) {
	int kind = MU_pictureKind(data, size);
	int front = type == 3;
	if (kind == MU_PICTURE_NONE || size > MU_PICTURE_MAX) return;
	if (tags->picture && (tags->picture_front || !front)) return;
	unsigned char* copy = malloc(size);
	if (!copy) return;
	memcpy(copy, data, size);
	free(tags->picture);
	tags->picture = copy;
	tags->picture_size = size;
	tags->picture_kind = kind;
	tags->picture_front = front;
}

// A FLAC picture block's body, also what a Vorbis comment's
// METADATA_BLOCK_PICTURE holds in base64.
static void flacPicture(MuTags* tags, const unsigned char* p, size_t len) {
	size_t pos = 0;
	if (len < 8) return;
	unsigned int type = be32(p);
	unsigned int mime = be32(p + 4);
	pos = 8;
	if (mime > len - pos) return;
	pos += mime;
	if (len - pos < 4) return;
	unsigned int desc = be32(p + pos);
	pos += 4;
	if (desc > len - pos) return;
	pos += desc;
	if (len - pos < 20) return;
	unsigned int data = be32(p + pos + 16);
	pos += 20;
	if (data > len - pos) return;
	offerPicture(tags, type, p + pos, data);
}

///////////////////////////////////////
// ID3

// Takes out unsynchronisation (0xFF 0x00 is 0xFF) in place; returns the new length.
static size_t unsync(unsigned char* p, size_t len) {
	size_t n = 0;
	for (size_t i = 0; i < len; i++) {
		p[n++] = p[i];
		if (p[i] == 0xFF && i + 1 < len && p[i + 1] == 0x00) i++;
	}
	return n;
}

static int frameIdOk(const unsigned char* id, int n) {
	for (int i = 0; i < n; i++) {
		if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return 0;
	}
	return 1;
}

// An APIC (v2.3, v2.4) or PIC (v2.2) frame's body.
static void id3Picture(MuTags* tags, const unsigned char* p, size_t len, int v2) {
	if (len < 2) return;
	int enc = p[0];
	size_t pos = 1;
	if (v2) {
		if (len < 5) return;
		pos = 4;
	} else {
		while (pos < len && p[pos]) pos++;
		if (pos >= len) return;
		pos++;
	}
	if (pos >= len) return;
	unsigned int type = p[pos++];
	// the description, ended by one NUL, or two on a two-byte boundary in UTF-16
	if (enc == 1 || enc == 2) {
		while (pos + 1 < len && (p[pos] || p[pos + 1])) pos += 2;
		if (pos + 1 >= len) return;
		pos += 2;
	} else {
		while (pos < len && p[pos]) pos++;
		if (pos >= len) return;
		pos++;
	}
	offerPicture(tags, type, p + pos, len - pos);
}

// The ID3 frames from start to end of src, of version ver (2, 3 or 4).
static void id3Frames(const MuSource* src, unsigned long long start, unsigned long long end, int ver, int all_unsync,
	int want_picture, MuTags* tags) {
	int hdr = ver == 2 ? 6 : 10;
	unsigned long long pos = start;
	char album_artist[MU_TAG_TEXT_MAX] = ""; // TPE2, for a track without TPE1
	for (int count = 0; count < MU_TAG_ITEMS_MAX && end - pos >= (unsigned long long)hdr; count++) {
		unsigned char h[10];
		if (srcRead(src, pos, h, (size_t)hdr) != 0) break;
		if (!h[0] || !frameIdOk(h, ver == 2 ? 3 : 4)) break; // padding, or not a frame
		unsigned int size;
		int flags = 0;
		if (ver == 2) size = (unsigned int)h[3] << 16 | (unsigned int)h[4] << 8 | h[5];
		else if (ver == 3) size = be32(h + 4);
		else if (!syncsafe(h + 4, &size)) size = be32(h + 4); // as some writers do
		if (ver > 2) flags = h[8] << 8 | h[9];
		unsigned long long body = pos + (unsigned long long)hdr;
		if (size > end - body) break;
		pos = body + size;
		int frame_unsync = all_unsync;
		if (ver == 3) {
			if (flags & 0x00C0) continue; // compressed or encrypted
			if (flags & 0x0020) {
				if (size < 1) continue;
				body++;
				size--;
			}
		} else if (ver == 4) {
			if (flags & 0x000C) continue;
			if (flags & 0x0040) {
				if (size < 1) continue;
				body++;
				size--;
			}
			if (flags & 0x0001) {
				if (size < 4) continue;
				body += 4;
				size -= 4;
			}
			if (flags & 0x0002) frame_unsync = 1;
		}
		const char* id = (const char*)h;
		char* field = NULL;
		if (ver == 2) {
			if (!memcmp(id, "TT2", 3)) field = tags->title;
			else if (!memcmp(id, "TP1", 3)) field = tags->artist;
			else if (!memcmp(id, "TP2", 3)) field = album_artist;
			else if (!memcmp(id, "TAL", 3)) field = tags->album;
		} else {
			if (!memcmp(id, "TIT2", 4)) field = tags->title;
			else if (!memcmp(id, "TPE1", 4)) field = tags->artist;
			else if (!memcmp(id, "TPE2", 4)) field = album_artist;
			else if (!memcmp(id, "TALB", 4)) field = tags->album;
		}
		int picture = want_picture && !memcmp(id, ver == 2 ? "PIC" : "APIC", ver == 2 ? 3 : 4);
		if (field && !field[0] && size >= 1) {
			size_t n = size < MU_TAG_FRAME_MAX ? size : MU_TAG_FRAME_MAX;
			unsigned char* buf = srcAlloc(src, body, n);
			if (!buf) continue;
			if (frame_unsync) n = unsync(buf, n);
			if (n >= 1) setText(field, buf[0], buf + 1, n - 1);
			free(buf);
		} else if (picture && size <= MU_PICTURE_MAX + 4096 && !(tags->picture && tags->picture_front)) {
			unsigned char* buf = srcAlloc(src, body, size);
			if (!buf) continue;
			size_t n = frame_unsync ? unsync(buf, size) : size;
			id3Picture(tags, buf, n, ver == 2);
			free(buf);
		}
	}
	if (!tags->artist[0] && album_artist[0]) snprintf(tags->artist, sizeof(tags->artist), "%s", album_artist);
}

// An ID3v2 tag at off. Returns its whole size (header, frames and
// footer), or 0 when there's none or it isn't a valid one.
static unsigned long long id3v2(const MuSource* src, unsigned long long off, int want_picture, MuTags* tags) {
	unsigned char h[10];
	if (srcRead(src, off, h, sizeof(h)) != 0 || memcmp(h, "ID3", 3) != 0) return 0;
	int ver = h[3], flags = h[5];
	unsigned int size;
	if (ver < 2 || ver > 4 || h[4] == 0xFF || !syncsafe(h + 6, &size)) return 0;
	unsigned long long total = 10ull + size + (ver == 4 && (flags & 0x10) ? 10 : 0);
	if (total > src->size - off) return 0;
	if (!tags) return total;
	if (ver == 2 && (flags & 0x40)) return total; // compressed, which nothing defined
	unsigned long long start = off + 10, end = off + 10 + size;
	MuMemory mem;
	MuSource whole;
	unsigned char* buf = NULL;
	const MuSource* frames = src;
	if ((flags & 0x80) && ver < 4) {
		// the whole tag is unsynchronised, frame headers too
		if (size > MU_TAG_MAX || !(buf = srcAlloc(src, start, size))) return total;
		size_t n = unsync(buf, size);
		MU_memorySource(&whole, &mem, buf, n);
		frames = &whole;
		start = 0;
		end = n;
	}
	if (flags & 0x40) {
		unsigned char e[4];
		unsigned int ext;
		if (srcRead(frames, start, e, 4) != 0) goto done;
		if (ver == 3) {
			ext = be32(e);
			if (end - start < 4 || ext > end - start - 4) goto done;
			start += 4ull + ext;
		} else {
			if (!syncsafe(e, &ext) || ext < 6 || ext > end - start) goto done;
			start += ext;
		}
	}
	id3Frames(frames, start, end, ver, ver == 4 && (flags & 0x80), want_picture, tags);
done:
	free(buf);
	return total;
}

// The 128-byte ID3v1 tag at the file's end, for the fields still empty.
static void id3v1(const MuSource* src, MuTags* tags) {
	unsigned char t[128];
	if (src->size < 128 || srcRead(src, src->size - 128, t, sizeof(t)) != 0 || memcmp(t, "TAG", 3) != 0) return;
	setText(tags->title, 0, t + 3, 30);
	setText(tags->artist, 0, t + 33, 30);
	setText(tags->album, 0, t + 63, 30);
}

///////////////////////////////////////
// Vorbis comments

static int keyIs(const unsigned char* p, size_t n, const char* key) {
	size_t k = strlen(key);
	return n > k && p[k] == '=' && !strncasecmp((const char*)p, key, k);
}

// A Vorbis comment header's body (from its vendor length) in src.
static void vorbisComments(const MuSource* src, int want_picture, MuTags* tags) {
	unsigned char b[4];
	unsigned long long pos = 0;
	if (srcRead(src, 0, b, 4) != 0) return;
	unsigned long long vendor = le32(b);
	if (vendor > src->size - 4) return;
	pos = 4 + vendor;
	if (srcRead(src, pos, b, 4) != 0) return;
	unsigned int count = le32(b);
	pos += 4;
	char album_artist[MU_TAG_TEXT_MAX] = "";
	for (unsigned int i = 0; i < count && i < MU_TAG_ITEMS_MAX; i++) {
		if (srcRead(src, pos, b, 4) != 0) break;
		unsigned long long len = le32(b);
		pos += 4;
		if (len > src->size - pos) break;
		unsigned long long at = pos;
		pos += len;
		unsigned char key[32];
		size_t klen = len < sizeof(key) ? (size_t)len : sizeof(key);
		if (srcRead(src, at, key, klen) != 0) break;
		char* field = NULL;
		size_t skip = 0;
		if (keyIs(key, klen, "TITLE")) field = tags->title, skip = 6;
		else if (keyIs(key, klen, "ARTIST")) field = tags->artist, skip = 7;
		else if (keyIs(key, klen, "ALBUMARTIST")) field = album_artist, skip = 12;
		else if (keyIs(key, klen, "ALBUM")) field = tags->album, skip = 6;
		if (field && !field[0]) {
			size_t n = len - skip < MU_TAG_FRAME_MAX ? (size_t)(len - skip) : MU_TAG_FRAME_MAX;
			unsigned char* buf = srcAlloc(src, at + skip, n);
			if (!buf) continue;
			setText(field, 3, buf, n);
			free(buf);
		} else if (want_picture && keyIs(key, klen, "METADATA_BLOCK_PICTURE") && !(tags->picture && tags->picture_front)) {
			unsigned long long n = len - 23;
			if (n > (MU_PICTURE_MAX + 4096ull) / 3 * 4 + 8) continue;
			char* text = (char*)srcAlloc(src, at + 23, (size_t)n);
			if (!text) continue;
			size_t cap = (size_t)n / 4 * 3 + 3;
			unsigned char* raw = malloc(cap);
			long got = raw ? MU_base64Decode(text, (size_t)n, raw, cap) : -1;
			if (got > 0) flacPicture(tags, raw, (size_t)got);
			free(raw);
			free(text);
		}
	}
	if (!tags->artist[0] && album_artist[0]) snprintf(tags->artist, sizeof(tags->artist), "%s", album_artist);
}

///////////////////////////////////////
// FLAC

static void flacTags(const MuSource* src, int want_picture, MuTags* tags) {
	unsigned long long pos = id3v2(src, 0, 0, NULL); // an ID3v2 tag some writers put first
	unsigned char b[4];
	if (srcRead(src, pos, b, 4) != 0 || memcmp(b, "fLaC", 4) != 0) return;
	pos += 4;
	for (int count = 0; count < MU_TAG_ITEMS_MAX; count++) {
		if (srcRead(src, pos, b, 4) != 0) break;
		int last = b[0] & 0x80, type = b[0] & 0x7F;
		unsigned long long len = (unsigned long long)b[1] << 16 | (unsigned long long)b[2] << 8 | b[3];
		pos += 4;
		if (type == 127 || len > src->size - pos) break;
		if (type == 4) {
			MuSource block;
			Window w;
			if (window(&block, &w, src, pos, len) == 0) vorbisComments(&block, want_picture, tags);
		} else if (type == 6 && want_picture && len <= MU_PICTURE_MAX + 4096ull && !(tags->picture && tags->picture_front)) {
			unsigned char* buf = srcAlloc(src, pos, (size_t)len);
			if (buf) flacPicture(tags, buf, (size_t)len);
			free(buf);
		}
		pos += len;
		if (last) break;
	}
}

///////////////////////////////////////
// reading

int MU_readTags(const MuSource* src, int kind, int want_picture, MuTags* tags) {
	MuTags_init(tags);
	if (!src || !src->read) return 0;
	if (kind == MU_KIND_MP3) {
		id3v2(src, 0, want_picture, tags);
		if (!tags->title[0] || !tags->artist[0] || !tags->album[0]) id3v1(src, tags);
	} else if (kind == MU_KIND_FLAC) {
		flacTags(src, want_picture, tags);
	}
	return hasText(tags) || tags->picture;
}

static int fileRead(void* ctx, unsigned long long off, void* out, size_t len) {
	int fd = *(int*)ctx;
	unsigned char* p = out;
	while (len) {
		ssize_t n = pread(fd, p, len, (off_t)off);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return -1;
		p += n;
		off += (unsigned long long)n;
		len -= (size_t)n;
	}
	return 0;
}

int MU_readTagsFile(const char* path, int want_picture, MuTags* tags) {
	MuTags_init(tags);
	const char* slash = strrchr(path, '/');
	int kind = MU_trackKind(slash ? slash + 1 : path);
	if (kind == MU_KIND_NONE) return 0;
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return 0;
	struct stat st;
	int found = 0;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
		MuSource src = { fileRead, &fd, (unsigned long long)st.st_size };
		found = MU_readTags(&src, kind, want_picture, tags);
	}
	close(fd);
	return found;
}

///////////////////////////////////////
// cover files

int MU_folderCover(const char* dir, char* out, size_t size) {
	static const char* names[] = { "cover.jpg", "cover.png", "folder.jpg" };
	if (size) out[0] = '\0';
	DIR* d = opendir(dir);
	if (!d) return 0;
	int best = 3;
	char found[16] = ""; // one of names, in its own case
	struct dirent* e;
	for (int count = 0; best > 0 && count < 100000 && (e = readdir(d)); count++) {
		for (int i = 0; i < best; i++) {
			if (strcasecmp(e->d_name, names[i]) != 0) continue;
			char path[MU_PATH_MAX];
			struct stat st;
			if (!MU_childPath(dir, e->d_name, path, sizeof(path)) || stat(path, &st) != 0 || !S_ISREG(st.st_mode)
				|| st.st_size <= 0 || st.st_size > MU_COVER_FILE_MAX) {
				break;
			}
			best = i;
			snprintf(found, sizeof(found), "%.15s", e->d_name);
			break;
		}
	}
	closedir(d);
	return found[0] && MU_childPath(dir, found, out, size);
}

///////////////////////////////////////
// embedded art files

void MU_artName(const char* path, unsigned long long file_size, long long mtime, int kind, char* out, size_t size) {
	unsigned long long h = 14695981039346656037ull; // FNV-1a
	for (const unsigned char* p = (const unsigned char*)path; *p; p++) h = (h ^ *p) * 1099511628211ull;
	h = (h ^ file_size) * 1099511628211ull;
	h = (h ^ (unsigned long long)mtime) * 1099511628211ull;
	snprintf(out, size, "art-%016llx.%s", h, MU_pictureExt(kind));
}

int MU_artDir(const char* dir) {
	if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
	struct stat st;
	if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return -1;
	return 0;
}

int MU_writeArt(const char* dir, const char* name, const unsigned char* data, size_t size) {
	char path[MU_PATH_MAX], part[MU_PATH_MAX];
	if (!name[0] || strchr(name, '/') || snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)
		|| snprintf(part, sizeof(part), "%s/.part-%ld", dir, (long)getpid()) >= (int)sizeof(part)) {
		return -1;
	}
	unlink(part);
	int fd = open(part, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) return -1;
	size_t done = 0;
	while (done < size) {
		ssize_t n = write(fd, data + done, size - done);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		done += (size_t)n;
	}
	if (close(fd) != 0 || done < size || rename(part, path) != 0) {
		unlink(part);
		return -1;
	}
	return 0;
}
