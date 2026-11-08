#include "rahash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>
#include <rc_hash.h>
#include "rachd.h"

static void (*say)(const char* message);

static void onError(const char* message, const rc_hash_iterator_t* iterator) {
	(void)iterator;
	if (!say) return;
	char line[512];
	snprintf(line, sizeof(line), "retroachievements: rcheevos: %s", message);
	say(line);
}

void RAHash_setLog(void (*log)(const char* message)) {
	say = log;
}

static volatile int* cancel;

void RAHash_setCancel(volatile int* flag) {
	cancel = flag;
}

static int cancelled(void) {
	return cancel && *cancel;
}

// rcheevos' own reader, but one that stops reading once cancelled.
static void* fileOpen(const char* path) {
	return fopen(path, "rb");
}

static void fileSeek(void* handle, int64_t offset, int origin) {
	fseeko((FILE*)handle, (off_t)offset, origin);
}

static int64_t fileTell(void* handle) {
	return (int64_t)ftello((FILE*)handle);
}

static size_t fileRead(void* handle, void* buffer, size_t size) {
	if (cancelled()) return 0;
	return fread(buffer, 1, size, (FILE*)handle);
}

static void fileClose(void* handle) {
	fclose((FILE*)handle);
}

///////////////////////////////////////
// Zips

static uint32_t le16(const uint8_t* p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t* p) {
	return le16(p) | le16(p + 2) << 16;
}

static int readAt(FILE* f, long offset, void* out, size_t size) {
	return fseek(f, offset, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
}

static int inflateRaw(FILE* f, uint32_t packed, uint8_t* out, uint32_t size) {
	z_stream z;
	memset(&z, 0, sizeof(z));
	if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return 0;
	uint8_t chunk[16384];
	z.next_out = out;
	z.avail_out = size;
	int rc = Z_OK;
	while (rc != Z_STREAM_END && !cancelled()) {
		if (!z.avail_in) {
			if (!packed) break;
			size_t want = packed < sizeof(chunk) ? packed : sizeof(chunk);
			if (fread(chunk, 1, want, f) != want) break;
			packed -= (uint32_t)want;
			z.next_in = chunk;
			z.avail_in = (uInt)want;
		}
		rc = inflate(&z, Z_NO_FLUSH);
		if (rc != Z_OK && rc != Z_STREAM_END) break;
	}
	int ok = rc == Z_STREAM_END && z.total_out == size;
	inflateEnd(&z);
	return ok;
}

int RAHash_unzipFirst(const char* path, uint8_t** data, size_t* size, char* name, size_t name_size) {
	*data = NULL;
	*size = 0;
	FILE* f = fopen(path, "rb");
	if (!f) return 0;
	int ok = 0;
	uint8_t* tail = NULL;
	uint8_t* dir = NULL;
	if (fseek(f, 0, SEEK_END) != 0) goto done;
	long file_size = ftell(f);
	if (file_size < 22) goto done;
	// the end of central directory record, within the last 64 KiB and a bit
	long tail_size = file_size < 65557 ? file_size : 65557;
	if (!(tail = malloc((size_t)tail_size)) || !readAt(f, file_size - tail_size, tail, (size_t)tail_size)) goto done;
	long end = -1;
	for (long i = tail_size - 22; i >= 0; i--) {
		if (le32(tail + i) == 0x06054b50) {
			end = i;
			break;
		}
	}
	if (end < 0) goto done;
	uint32_t entries = le16(tail + end + 10);
	uint32_t dir_size = le32(tail + end + 12), dir_offset = le32(tail + end + 16);
	if (!entries || dir_size > 16 * 1024 * 1024 || (uint64_t)dir_offset + dir_size > (uint64_t)file_size) goto done;
	if (!(dir = malloc(dir_size)) || !readAt(f, (long)dir_offset, dir, dir_size)) goto done;
	uint32_t at = 0;
	for (uint32_t e = 0; e < entries && at + 46 <= dir_size; e++) {
		const uint8_t* c = dir + at;
		if (le32(c) != 0x02014b50) break;
		uint32_t method = le16(c + 10), packed = le32(c + 20), unpacked = le32(c + 24);
		uint32_t name_len = le16(c + 28), extra_len = le16(c + 30), comment_len = le16(c + 32);
		uint32_t local = le32(c + 42);
		if (at + 46 + name_len > dir_size) break;
		const char* entry_name = (const char*)c + 46;
		at += 46 + name_len + extra_len + comment_len;
		if (!name_len || entry_name[name_len - 1] == '/') continue;
		// the first file decides, as minarch takes the first it can
		if ((method != 0 && method != 8) || unpacked > RA_UNZIP_MAX || packed == 0xFFFFFFFF) break;
		uint8_t header[30];
		if (!readAt(f, (long)local, header, sizeof(header)) || le32(header) != 0x04034b50) break;
		long start = (long)local + 30 + (long)le16(header + 26) + (long)le16(header + 28);
		if ((uint64_t)start + packed > (uint64_t)file_size) break;
		uint8_t* out = malloc(unpacked ? unpacked : 1);
		if (!out) break;
		int read = fseek(f, start, SEEK_SET) == 0 &&
			(method == 0 ? (packed == unpacked && fread(out, 1, unpacked, f) == unpacked) : inflateRaw(f, packed, out, unpacked));
		if (!read) {
			free(out);
			break;
		}
		const char* base = entry_name;
		for (uint32_t i = 0; i < name_len; i++)
			if (entry_name[i] == '/') base = entry_name + i + 1;
		size_t n = (size_t)(entry_name + name_len - base);
		if (n >= name_size) n = name_size - 1;
		memcpy(name, base, n);
		name[n] = '\0';
		*data = out;
		*size = unpacked;
		ok = 1;
		break;
	}
done:
	free(tail);
	free(dir);
	fclose(f);
	return ok;
}

///////////////////////////////////////
// Hashing

static int isZip(const char* path) {
	const char* ext = strrchr(path, '.');
	return ext && !strcasecmp(ext, ".zip");
}

static void setUp(rc_hash_iterator_t* it, const char* path, const uint8_t* data, size_t size) {
	rc_hash_initialize_iterator(it, path, data, size);
	RAChd_reader(&it->callbacks.cdreader);
	it->callbacks.error_message = onError;
	it->callbacks.filereader.open = fileOpen;
	it->callbacks.filereader.seek = fileSeek;
	it->callbacks.filereader.tell = fileTell;
	it->callbacks.filereader.read = fileRead;
	it->callbacks.filereader.close = fileClose;
}

int RAHash_game(const char* path, int console, RAKnownHash known, void* ctx, char out[33]) {
	if (!path || !out) return 0;
	out[0] = '\0';
	uint8_t* data = NULL;
	size_t size = 0;
	char inner[256];
	const char* hashed = path;
	if (isZip(path) && console != RC_CONSOLE_ARCADE) {
		if (!RAHash_unzipFirst(path, &data, &size, inner, sizeof(inner))) {
			onError("can't unzip the first file", NULL);
			return 0;
		}
		hashed = inner;
	}
	rc_hash_iterator_t it;
	setUp(&it, hashed, data, size);
	int ok = 0;
	char hash[33];
	if (console != RC_CONSOLE_UNKNOWN) ok = rc_hash_generate(hash, (uint32_t)console, &it);
	else {
		while (!ok && rc_hash_iterate(hash, &it)) ok = known && known(hash, ctx);
	}
	rc_hash_destroy_iterator(&it);
	free(data);
	if (!ok || cancelled()) return 0;
	for (int i = 0; i < 32; i++) out[i] = (char)(hash[i] >= 'A' && hash[i] <= 'F' ? hash[i] - 'A' + 'a' : hash[i]);
	out[32] = '\0';
	return 1;
}
