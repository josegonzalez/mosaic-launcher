#include "reportmodel.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////
// buffers

static int grow(ReportBuf* b, size_t more) {
	if (b->failed) return -1;
	if (b->len + more + 1 <= b->cap) return 0;
	size_t cap = b->cap ? b->cap : 4096;
	while (cap < b->len + more + 1) cap *= 2;
	char* data = realloc(b->data, cap);
	if (!data) {
		b->failed = 1;
		return -1;
	}
	b->data = data;
	b->cap = cap;
	return 0;
}

int ReportBuf_add(ReportBuf* b, const void* data, size_t len) {
	if (grow(b, len) != 0) return -1;
	if (len) memcpy(b->data + b->len, data, len);
	b->len += len;
	b->data[b->len] = '\0';
	return 0;
}

int ReportBuf_printf(ReportBuf* b, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char small[256];
	int n = vsnprintf(small, sizeof(small), fmt, args);
	va_end(args);
	if (n < 0) return -1;
	if ((size_t)n < sizeof(small)) return ReportBuf_add(b, small, (size_t)n);
	if (grow(b, (size_t)n) != 0) return -1;
	va_start(args, fmt);
	vsnprintf(b->data + b->len, (size_t)n + 1, fmt, args);
	va_end(args);
	b->len += (size_t)n;
	return 0;
}

void ReportBuf_free(ReportBuf* b) {
	free(b->data);
	memset(b, 0, sizeof(*b));
}

///////////////////////////////////////
// redaction

void Redactor_init(Redactor* r, int keep_network) {
	memset(r, 0, sizeof(*r));
	r->keep_network = keep_network ? 1 : 0;
}

void Redactor_free(Redactor* r) {
	for (int i = 0; i < r->count; i++) free(r->literals[i]);
	memset(r, 0, sizeof(*r));
}

int Redactor_add(Redactor* r, const char* value, int network) {
	if (!value || strlen(value) < (network ? 2u : 4u)) return 0;
	for (int i = 0; i < r->count; i++) {
		if (!strcmp(r->literals[i], value)) {
			if (!network) r->network[i] = 0; // a secret too: always taken out
			return 0;
		}
	}
	if (r->count >= REPORT_LITERALS_MAX) {
		r->dropped++;
		return -1;
	}
	char* copy = strdup(value);
	if (!copy) {
		r->dropped++;
		return -1;
	}
	// longest first, so a value inside another doesn't cut it up
	int at = r->count;
	while (at > 0 && strlen(r->literals[at - 1]) < strlen(copy)) {
		r->literals[at] = r->literals[at - 1];
		r->network[at] = r->network[at - 1];
		at--;
	}
	r->literals[at] = copy;
	r->network[at] = network ? 1 : 0;
	r->count++;
	return 1;
}

static int isWord(int c) {
	return isalnum(c) || c == '_';
}

static int isIdent(int c) {
	return isalnum(c) || c == '_' || c == '-' || c == '.';
}

static int isHex(int c) {
	return isxdigit(c);
}

// The text being redacted, and where we are.
typedef struct Scan {
	const Redactor* r;
	const char* s;
	size_t len;
	ReportBuf* out;
	int replaced;
} Scan;

static int at(const Scan* sc, size_t i) {
	return i < sc->len ? (unsigned char)sc->s[i] : 0;
}

static int before(const Scan* sc, size_t i) {
	return i > 0 ? (unsigned char)sc->s[i - 1] : 0;
}

static void emit(Scan* sc, const char* text, size_t n) {
	ReportBuf_add(sc->out, text, n);
}

static void mark(Scan* sc, const char* marker) {
	emit(sc, marker, strlen(marker));
	sc->replaced++;
}

static int startsWithNoCase(const char* s, size_t len, const char* word) {
	size_t n = strlen(word);
	if (len < n) return 0;
	for (size_t i = 0; i < n; i++) {
		if (tolower((unsigned char)s[i]) != word[i]) return 0;
	}
	return 1;
}

enum { KEY_NONE, KEY_SECRET, KEY_WIFI };

// What a key's value is: a secret, a Wi-Fi name, or neither.
static int keyKind(const char* key) {
	static const char* const parts[] = {
		"token", "secret", "passw", "passphrase", "apikey", "api_key", "api-key", "authoriz", "credential", "cookie",
		"private_key", "psk",
	};
	static const char* const whole[] = { "pass", "pwd", "pw", "auth", "user", "username", "login", "pin", "key" };
	char lower[64];
	size_t n = 0;
	for (; key[n] && n < sizeof(lower) - 1; n++) lower[n] = (char)tolower((unsigned char)key[n]);
	lower[n] = '\0';
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		if (strstr(lower, parts[i])) return KEY_SECRET;
	}
	// the last part of a dotted or underscored key, such as romm.user or rclone_user
	const char* last = lower;
	for (const char* p = lower; *p; p++) {
		if (*p == '.' || *p == '_') last = p + 1;
	}
	for (size_t i = 0; i < sizeof(whole) / sizeof(whole[0]); i++) {
		if (!strcmp(lower, whole[i]) || !strcmp(last, whole[i])) return KEY_SECRET;
	}
	if (strstr(lower, "ssid")) return KEY_WIFI;
	return KEY_NONE;
}

// An added value as a whole word at i. Returns its length, or 0.
static size_t literalAt(Scan* sc, size_t i, const char** marker) {
	const Redactor* r = sc->r;
	for (int k = 0; k < r->count; k++) {
		if (r->network[k] && r->keep_network) continue;
		const char* v = r->literals[k];
		size_t n = strlen(v);
		if ((unsigned char)v[0] != at(sc, i) || n > sc->len - i || memcmp(sc->s + i, v, n) != 0) continue;
		if (isWord((unsigned char)v[0]) && isWord(before(sc, i))) continue;
		if (isWord((unsigned char)v[n - 1]) && isWord(at(sc, i + n))) continue;
		*marker = r->network[k] ? "<wifi>" : "<redacted>";
		return n;
	}
	return 0;
}

// The end of an unquoted value starting at i: up to the end of the line
// when whole, else up to a space or a separator.
static size_t valueEnd(const Scan* sc, size_t i, int whole, int wifi) {
	size_t j = i;
	while (j < sc->len) {
		int c = at(sc, j);
		if (c == '\n' || c == '\r') break;
		if (!whole) {
			if (wifi) {
				if (c == ',' || c == ';') break;
			} else if (isspace(c) || strchr("&,;\"'<>)]}", c)) {
				break;
			}
		}
		j++;
	}
	// no trailing spaces
	while (j > i && (at(sc, j - 1) == ' ' || at(sc, j - 1) == '\t')) j--;
	return j;
}

// True when only spaces come before i on its line.
static int lineStart(const Scan* sc, size_t i) {
	while (i > 0) {
		int c = before(sc, i);
		if (c == '\n') return 1;
		if (c != ' ' && c != '\t') return 0;
		i--;
	}
	return 1;
}

// A sensitive key and its value at i: key=value, key: value, "key": "value",
// key = value on a line of its own, or <key>value</key>. Emits it redacted
// and returns where it ends, or 0 when there's none.
static size_t keyValueAt(Scan* sc, size_t i) {
	if (isIdent(before(sc, i))) return 0;
	size_t j = i;
	int xml = 0, quote = 0;
	if (at(sc, j) == '<') {
		xml = 1;
		j++;
	} else if (at(sc, j) == '"' || at(sc, j) == '\'') {
		quote = at(sc, j);
		j++;
	}
	if (!isalpha(at(sc, j))) return 0;
	size_t key_start = j;
	while (j < sc->len && isIdent(at(sc, j)) && j - key_start < 63) j++;
	if (isIdent(at(sc, j))) return 0;
	char key[64];
	memcpy(key, sc->s + key_start, j - key_start);
	key[j - key_start] = '\0';
	int kind = keyKind(key);
	int wifi_label = !xml && !quote && !strcmp(key, "Wi-Fi") && at(sc, j) == ':';
	if (wifi_label) kind = KEY_WIFI;
	if (kind == KEY_NONE || (kind == KEY_WIFI && sc->r->keep_network)) return 0;
	if (wifi_label) {
		// system-info's states, not a network's name
		static const char* const states[] = { "off", "not connected", "unavailable", "checking", "hidden network" };
		size_t v = j + 1;
		while (at(sc, v) == ' ') v++;
		size_t e = valueEnd(sc, v, 0, 1);
		for (size_t k = 0; k < sizeof(states) / sizeof(states[0]); k++) {
			if (e - v == strlen(states[k]) && !memcmp(sc->s + v, states[k], e - v)) return 0;
		}
	}
	const char* marker = kind == KEY_WIFI ? "<wifi>" : "<redacted>";

	if (xml) {
		if (at(sc, j) != '>') return 0;
		j++;
		size_t v = j;
		while (j < sc->len && at(sc, j) != '<' && at(sc, j) != '\n') j++;
		if (at(sc, j) != '<' || j == v) return 0;
		emit(sc, sc->s + i, v - i);
		mark(sc, marker);
		return j;
	}
	if (quote) {
		if (at(sc, j) != quote) return 0;
		j++;
	}
	size_t sep = j;
	while (at(sc, sep) == ' ' || at(sc, sep) == '\t') sep++;
	int c = at(sc, sep);
	if (c != '=' && c != ':') return 0;
	if (c == ':' && (at(sc, sep + 1) == '/' || at(sc, sep + 1) == ':')) return 0;
	if (c == '=' && at(sc, sep + 1) == '=') return 0;
	size_t v = sep + 1;
	while (at(sc, v) == ' ' || at(sc, v) == '\t') v++;
	if (v >= sc->len) return 0;
	int vq = at(sc, v);
	size_t start, end, resume;
	if (vq == '"' || vq == '\'') {
		start = v + 1;
		end = start;
		while (end < sc->len && at(sc, end) != vq && at(sc, end) != '\n') {
			if (at(sc, end) == '\\' && end + 1 < sc->len) end++;
			end++;
		}
		if (at(sc, end) != vq) return 0;
		resume = end;
	} else {
		start = v;
		// a scheme, such as Bearer, stays
		static const char* const schemes[] = { "bearer ", "basic ", "token ", "digest " };
		for (size_t k = 0; k < sizeof(schemes) / sizeof(schemes[0]); k++) {
			if (startsWithNoCase(sc->s + v, sc->len - v, schemes[k])) {
				start = v + strlen(schemes[k]);
				break;
			}
		}
		end = valueEnd(sc, start, lineStart(sc, i) && !wifi_label, kind == KEY_WIFI);
		resume = end;
	}
	if (end == start) return 0;
	emit(sc, sc->s + i, start - i);
	mark(sc, marker);
	return resume;
}

// "Bearer <token>" on its own, without a key before it. Emits it redacted
// and returns where the token ends, or 0.
static size_t bearerAt(Scan* sc, size_t i) {
	if (isIdent(before(sc, i)) || !startsWithNoCase(sc->s + i, sc->len - i, "bearer ")) return 0;
	size_t start = i + 7;
	size_t end = valueEnd(sc, start, 0, 0);
	if (end == start) return 0;
	emit(sc, sc->s + i, start - i);
	mark(sc, "<redacted>");
	return end;
}

// scheme://user:password@ at i, the ://. Returns where the @ is, or 0.
static size_t userInfoAt(const Scan* sc, size_t i) {
	if (i + 3 > sc->len || memcmp(sc->s + i, "://", 3) != 0) return 0;
	size_t j = i + 3;
	while (j < sc->len && !strchr("/?#@ \t\r\n\"'<>", at(sc, j))) j++;
	if (at(sc, j) != '@' || j == i + 3) return 0;
	return j;
}

// count digits at i, returning their value, or -1.
static int number(const Scan* sc, size_t i, size_t* end) {
	int value = 0;
	size_t j = i;
	while (j < sc->len && j - i < 3 && isdigit(at(sc, j))) value = value * 10 + (at(sc, j++) - '0');
	if (j == i || isdigit(at(sc, j))) return -1;
	*end = j;
	return value;
}

// An IPv4 address at i: its end, or 0. first gets its first part.
static size_t ipv4At(const Scan* sc, size_t i, int* first) {
	size_t j = i;
	for (int part = 0; part < 4; part++) {
		if (part) {
			if (at(sc, j) != '.') return 0;
			j++;
		}
		size_t end;
		int value = number(sc, j, &end);
		if (value < 0 || value > 255) return 0;
		if (part == 0) *first = value;
		j = end;
	}
	if (at(sc, j) == '.' && isdigit(at(sc, j + 1))) return 0;
	if (isalnum(at(sc, j))) return 0;
	return j;
}

// An IPv6 address at i: its end, or 0. keep is set for ::1 and ::.
static size_t ipv6At(const Scan* sc, size_t i, int* keep) {
	size_t j = i;
	int colons = 0, groups = 0, doubled = 0;
	while (j < sc->len) {
		if (at(sc, j) == ':') {
			if (at(sc, j + 1) == ':') {
				if (doubled) return 0;
				doubled = 1;
				colons += 2;
				j += 2;
				if (at(sc, j) == ':') return 0;
				continue;
			}
			colons++;
			j++;
			continue;
		}
		size_t g = j;
		while (j < sc->len && isHex(at(sc, j)) && j - g < 5) j++;
		if (j == g) break;
		if (j - g > 4) return 0;
		// an IPv4 address at the end, as in ::ffff:192.0.2.1
		if (at(sc, j) == '.') {
			int first;
			size_t v4 = ipv4At(sc, g, &first);
			if (!v4 || colons < 2) return 0;
			groups += 2;
			j = v4;
			break;
		}
		groups++;
		if (at(sc, j) != ':') break;
	}
	if (colons < 2 || groups == 0) return 0;
	if (!doubled && groups != 8) return 0;
	if (doubled && groups > 7) return 0;
	// a ':' it ended on, as in a list "a::b:", isn't part of it
	if (before(sc, j) == ':' && !(doubled && j >= 2 && sc->s[j - 2] == ':')) j--;
	if (at(sc, j) == '%') {
		j++;
		while (j < sc->len && (isalnum(at(sc, j)) || at(sc, j) == '_' || at(sc, j) == '-')) j++;
	}
	if (isalnum(at(sc, j)) || at(sc, j) == '_' || (at(sc, j) == ':' && isHex(at(sc, j + 1)))) return 0;
	*keep = (j - i == 2 && !memcmp(sc->s + i, "::", 2)) || (j - i == 3 && !memcmp(sc->s + i, "::1", 3));
	return j;
}

// A MAC address at i, aa:bb:cc:dd:ee:ff or with dashes: its end, or 0.
static size_t macAt(const Scan* sc, size_t i) {
	if (i + 17 > sc->len) return 0;
	int sep = at(sc, i + 2);
	if (sep != ':' && sep != '-') return 0;
	for (int k = 0; k < 6; k++) {
		size_t p = i + (size_t)k * 3;
		if (!isHex(at(sc, p)) || !isHex(at(sc, p + 1))) return 0;
		if (k < 5 && at(sc, p + 2) != sep) return 0;
	}
	size_t end = i + 17;
	if (isalnum(at(sc, end)) || (at(sc, end) == sep && isHex(at(sc, end + 1)))) return 0;
	return end;
}

// A Syncthing device ID at i, eight groups of seven base32 characters
// joined by dashes: its end, or 0.
static size_t deviceIdAt(const Scan* sc, size_t i) {
	if (i + 63 > sc->len) return 0;
	for (int k = 0; k < 63; k++) {
		int c = at(sc, i + (size_t)k);
		if (k % 8 == 7) {
			if (c != '-') return 0;
		} else if (!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'))) {
			return 0;
		}
	}
	if (isalnum(at(sc, i + 63)) || at(sc, i + 63) == '-') return 0;
	return i + 63;
}

// A network detail at i: emits its marker and returns its end, or 0.
static size_t networkAt(Scan* sc, size_t i) {
	int c = at(sc, i), prev = before(sc, i);
	if (isalnum(prev) || prev == '_') return 0;
	size_t end;
	if (isHex(c) && prev != ':' && prev != '-' && (end = macAt(sc, i))) {
		mark(sc, "<mac>");
		return end;
	}
	if (isupper(c) && prev != '-' && (end = deviceIdAt(sc, i))) {
		mark(sc, "<device-id>");
		return end;
	}
	if (isdigit(c) && prev != '.') {
		int first = 0;
		if ((end = ipv4At(sc, i, &first))) {
			// loopback, unspecified and netmasks say nothing about the network
			int keep = first == 127 || first == 255 || (first == 0 && end - i == 7);
			if (keep) emit(sc, sc->s + i, end - i);
			else mark(sc, "<ip>");
			return end;
		}
	}
	if ((isHex(c) || c == ':') && prev != ':' && prev != '.' && prev != '%') {
		int keep = 0;
		if ((end = ipv6At(sc, i, &keep))) {
			if (keep) emit(sc, sc->s + i, end - i);
			else mark(sc, "<ip>");
			return end;
		}
	}
	return 0;
}

int Redact(const Redactor* r, const char* text, size_t len, ReportBuf* out) {
	Scan sc = { r, text, len, out, 0 };
	size_t i = 0;
	while (i < len) {
		const char* marker = NULL;
		size_t n = literalAt(&sc, i, &marker);
		if (n) {
			mark(&sc, marker);
			i += n;
			continue;
		}
		size_t end = userInfoAt(&sc, i);
		if (end) {
			emit(&sc, "://", 3);
			mark(&sc, "<credentials>");
			i = end; // the @ is copied next
			continue;
		}
		end = bearerAt(&sc, i);
		if (!end) end = keyValueAt(&sc, i);
		if (end) {
			i = end;
			continue;
		}
		if (!r->keep_network) {
			end = networkAt(&sc, i);
			if (end) {
				i = end;
				continue;
			}
		}
		// the rest of a word, which nothing above starts inside
		size_t j = i + 1;
		if (isWord(at(&sc, i))) {
			while (j < len && isWord(at(&sc, j))) j++;
		}
		emit(&sc, text + i, j - i);
		i = j;
	}
	return sc.replaced;
}

///////////////////////////////////////
// zip

uint32_t Zip_crc32(uint32_t crc, const void* data, size_t len) {
	static uint32_t table[256];
	static int ready = 0;
	if (!ready) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		ready = 1;
	}
	const unsigned char* p = data;
	crc = ~crc;
	for (size_t i = 0; i < len; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

void Zip_init(ZipWriter* z, int (*write)(void* ctx, const void* data, size_t len), void* ctx) {
	memset(z, 0, sizeof(*z));
	z->write = write;
	z->ctx = ctx;
}

void Zip_free(ZipWriter* z) {
	for (int i = 0; i < z->count; i++) free(z->entries[i].name);
	free(z->entries);
	z->entries = NULL;
	z->count = z->room = 0;
}

static void put16(unsigned char* p, unsigned v) {
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char* p, uint32_t v) {
	put16(p, v & 0xFFFF);
	put16(p + 2, v >> 16);
}

static int out(ZipWriter* z, const void* data, size_t len) {
	if (z->failed) return -1;
	if (len && z->write(z->ctx, data, len) != 0) {
		z->failed = 1;
		return -1;
	}
	z->offset += (uint32_t)len;
	return 0;
}

static void dosTime(time_t when, uint16_t* time_out, uint16_t* date_out) {
	struct tm tm;
	if (!localtime_r(&when, &tm) || tm.tm_year < 80) {
		*time_out = 0;
		*date_out = (1 << 5) | 1; // 1980-01-01
		return;
	}
	*time_out = (uint16_t)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
	*date_out = (uint16_t)(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
}

int Zip_add(ZipWriter* z, const char* name, const void* data, size_t len, time_t mtime) {
	size_t name_len = strlen(name);
	if (z->failed || name_len == 0 || name_len > 0xFFFF || len > 0x7FFFFFFF || z->count >= 0xFFFF) {
		z->failed = 1;
		return -1;
	}
	if (z->count == z->room) {
		int room = z->room ? z->room * 2 : 16;
		ZipEntry* grown = realloc(z->entries, (size_t)room * sizeof(ZipEntry));
		if (!grown) {
			z->failed = 1;
			return -1;
		}
		z->entries = grown;
		z->room = room;
	}
	ZipEntry* e = &z->entries[z->count];
	e->name = strdup(name);
	if (!e->name) {
		z->failed = 1;
		return -1;
	}
	z->count++;
	e->crc = Zip_crc32(0, data, len);
	e->size = (uint32_t)len;
	e->offset = z->offset;
	dosTime(mtime, &e->time, &e->date);

	unsigned char h[30];
	put32(h, 0x04034b50);
	put16(h + 4, 10);     // version needed: stored
	put16(h + 6, 0x0800); // names are UTF-8
	put16(h + 8, 0);      // stored
	put16(h + 10, e->time);
	put16(h + 12, e->date);
	put32(h + 14, e->crc);
	put32(h + 18, e->size);
	put32(h + 22, e->size);
	put16(h + 26, (unsigned)name_len);
	put16(h + 28, 0);
	if (out(z, h, sizeof(h)) != 0 || out(z, name, name_len) != 0 || out(z, data, len) != 0) return -1;
	return 0;
}

int Zip_finish(ZipWriter* z) {
	uint32_t start = z->offset;
	for (int i = 0; i < z->count && !z->failed; i++) {
		const ZipEntry* e = &z->entries[i];
		size_t name_len = strlen(e->name);
		unsigned char h[46];
		put32(h, 0x02014b50);
		put16(h + 4, (3 << 8) | 20); // made on Unix, by zip 2.0
		put16(h + 6, 10);
		put16(h + 8, 0x0800);
		put16(h + 10, 0);
		put16(h + 12, e->time);
		put16(h + 14, e->date);
		put32(h + 16, e->crc);
		put32(h + 20, e->size);
		put32(h + 24, e->size);
		put16(h + 28, (unsigned)name_len);
		put16(h + 30, 0);
		put16(h + 32, 0);
		put16(h + 34, 0);
		put16(h + 36, 1);                   // text
		put32(h + 38, (uint32_t)0100644 << 16); // -rw-r--r--
		put32(h + 42, e->offset);
		if (out(z, h, sizeof(h)) != 0 || out(z, e->name, name_len) != 0) break;
	}
	unsigned char end[22];
	put32(end, 0x06054b50);
	put16(end + 4, 0);
	put16(end + 6, 0);
	put16(end + 8, (unsigned)z->count);
	put16(end + 10, (unsigned)z->count);
	put32(end + 12, z->offset - start);
	put32(end + 16, start);
	put16(end + 20, 0);
	out(z, end, sizeof(end));
	return z->failed ? -1 : 0;
}

///////////////////////////////////////
// what goes in

static const PluginSetting* settingFor(const PluginManifest* m, const char* key) {
	for (int i = 0; i < m->setting_count; i++) {
		if (!strcmp(m->settings[i].key, key)) return &m->settings[i];
	}
	return NULL;
}

static int isPrivate(const PluginSetting* s) {
	return (s->flags & (SETTING_PROTECTED | SETTING_SECRET)) != 0;
}

void Report_settings(const PluginManifest* m, char* const* keys, char* const* values, int count, ReportBuf* out) {
	ReportBuf_printf(out, "# %s\n", m->id);
	ReportBuf left = { 0 };
	int left_count = 0, unlisted = 0;
	for (int i = 0; i < count; i++) {
		const PluginSetting* s = settingFor(m, keys[i]);
		if (!s) {
			unlisted++;
			continue;
		}
		if (isPrivate(s)) {
			ReportBuf_printf(&left, "%s%s", left_count ? ", " : "", keys[i]);
			left_count++;
			continue;
		}
		ReportBuf_printf(out, "%s=%s\n", keys[i], values[i] ? values[i] : "");
	}
	if (left_count) {
		ReportBuf_printf(out, "# left out, protected or secret in plugin.txt: %s\n", left.data);
	}
	if (unlisted) {
		ReportBuf_printf(out, "# left out: %i %s plugin.txt doesn't list\n", unlisted, unlisted == 1 ? "key" : "keys");
	}
	ReportBuf_free(&left);
}

int Report_secrets(const PluginManifest* m, char* const* keys, char* const* values, int count, Redactor* r) {
	int added = 0;
	for (int i = 0; i < count; i++) {
		const PluginSetting* s = settingFor(m, keys[i]);
		if (s && isPrivate(s) && s->type == SETTING_TEXT && Redactor_add(r, values[i], 0) == 1) added++;
	}
	return added;
}

void Report_games(const CardIndex* index, ReportBuf* out) {
	if (!index) {
		ReportBuf_printf(out, "The card index hasn't been read yet, so there's no game list summary.\n");
		return;
	}
	int total = 0, systems = 0;
	for (int s = 0; s < index->system_count; s++) {
		const IndexSystem* sys = &index->systems[s];
		int games = 0;
		for (int e = 0; e < sys->count; e++) {
			if (sys->entries[e].kind == INDEX_KIND_GAME && !sys->entries[e].added) games++;
		}
		ReportBuf_printf(out, "%s: %i %s%s\n", sys->folder, games, games == 1 ? "game" : "games",
			sys->emulator ? "" : " (no emulator)");
		total += games;
		systems++;
	}
	ReportBuf_printf(out, "total: %i %s in %i %s\n", total, total == 1 ? "game" : "games", systems,
		systems == 1 ? "system" : "systems");
}

size_t Report_lineStart(const char* data, size_t len) {
	const char* nl = memchr(data, '\n', len);
	return nl ? (size_t)(nl - data) + 1 : 0;
}

void Report_name(const struct tm* when, char* out, size_t size) {
	snprintf(out, size, REPORT_PREFIX "%04d-%02d-%02d-%02d%02d%02d", when->tm_year + 1900, when->tm_mon + 1,
		when->tm_mday, when->tm_hour, when->tm_min, when->tm_sec);
}

int Report_isReport(const char* name) {
	size_t n = strlen(name);
	return n > strlen(REPORT_PREFIX) + 4 && !strncmp(name, REPORT_PREFIX, strlen(REPORT_PREFIX))
		&& !strcmp(name + n - 4, ".zip");
}

// Older first: by name without .zip, so mosaic-<time> comes before
// mosaic-<time>-2.
static int older(const char* a, const char* b) {
	size_t la = strlen(a) - 4, lb = strlen(b) - 4;
	size_t n = la < lb ? la : lb;
	int c = memcmp(a, b, n);
	if (c) return c < 0;
	return la < lb;
}

int Report_prune(char* const* names, int count, int keep, int* drop) {
	int reports = 0;
	for (int i = 0; i < count; i++) {
		drop[i] = 0;
		if (Report_isReport(names[i])) reports++;
	}
	int dropped = 0;
	while (reports - dropped > keep) {
		int oldest = -1;
		for (int i = 0; i < count; i++) {
			if (drop[i] || !Report_isReport(names[i])) continue;
			if (oldest < 0 || older(names[i], names[oldest])) oldest = i;
		}
		if (oldest < 0) break;
		drop[oldest] = 1;
		dropped++;
	}
	return dropped;
}
