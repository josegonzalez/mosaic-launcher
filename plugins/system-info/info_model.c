#include "info_model.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

// stock's layout units, before scaling
#define PADDING 10
#define PILL_SIZE 30
#define BUTTON_PADDING 12
#define GRAPH_MIN 20 // the shortest graph worth drawing

const char* Si_label(int row) {
	static const char* labels[SI_ROWS] = {
		"Battery", "Wi-Fi", "IP address", "Storage", "Memory", "Uptime", "Platform", "Version",
	};
	return row >= 0 && row < SI_ROWS ? labels[row] : "";
}

int Si_changed(const SiValues* a, const SiValues* b) {
	for (int i = 0; i < SI_ROWS; i++) {
		if (strcmp(a->text[i], b->text[i]) != 0) return 1;
	}
	return 0;
}

void Si_formatSize(unsigned long long bytes, char* out, size_t size) {
	static const char* units[] = { "KB", "MB", "GB", "TB", "PB" };
	if (!out || !size) return;
	double value = (double)bytes / 1024.0;
	int unit = 0;
	// 999.5 would round to 1000
	while (value >= 999.5 && unit < 4) {
		value /= 1024.0;
		unit++;
	}
	if (value < 99.95) snprintf(out, size, "%.1f %s", value, units[unit]);
	else snprintf(out, size, "%.0f %s", value, units[unit]);
}

static void ofTotal(unsigned long long part, unsigned long long total, char* out, size_t size) {
	if (!total) {
		snprintf(out, size, "unavailable");
		return;
	}
	char a[32], b[32];
	Si_formatSize(part, a, sizeof(a));
	Si_formatSize(total, b, sizeof(b));
	snprintf(out, size, "%s free of %s", a, b);
}

void Si_formatStorage(unsigned long long free_bytes, unsigned long long total_bytes, char* out, size_t size) {
	ofTotal(free_bytes, total_bytes, out, size);
}

void Si_formatMemory(unsigned long long available, unsigned long long total, char* out, size_t size) {
	ofTotal(available, total, out, size);
}

// The kB value of a "Key:   1234 kB" line, or -1 when text has no such key.
static long long meminfoKey(const char* text, const char* key) {
	size_t n = strlen(key);
	for (const char* line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
		if (strncmp(line, key, n) != 0 || line[n] != ':') continue;
		char* end;
		errno = 0;
		long long v = strtoll(line + n + 1, &end, 10);
		if (errno || end == line + n + 1 || v < 0) return -1;
		return v;
	}
	return -1;
}

int Si_parseMeminfo(const char* text, unsigned long long* total, unsigned long long* available) {
	if (!text) return -1;
	long long t = meminfoKey(text, "MemTotal");
	long long avail = meminfoKey(text, "MemAvailable");
	if (avail < 0) {
		long long free_kb = meminfoKey(text, "MemFree");
		if (free_kb < 0) return -1;
		long long buffers = meminfoKey(text, "Buffers"), cached = meminfoKey(text, "Cached");
		avail = free_kb + (buffers > 0 ? buffers : 0) + (cached > 0 ? cached : 0);
	}
	if (t <= 0) return -1;
	if (avail > t) avail = t;
	*total = (unsigned long long)t * 1024;
	*available = (unsigned long long)avail * 1024;
	return 0;
}

long long Si_parseUptime(const char* text) {
	if (!text) return -1;
	char* end;
	errno = 0;
	double v = strtod(text, &end);
	if (errno || end == text || v < 0) return -1;
	return (long long)v;
}

void Si_formatUptime(long long seconds, char* out, size_t size) {
	if (seconds < 0) {
		snprintf(out, size, "unavailable");
		return;
	}
	long long minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
	if (days) snprintf(out, size, "%lldd %lldh %lldm", days, hours % 24, minutes % 60);
	else if (hours) snprintf(out, size, "%lldh %lldm", hours, minutes % 60);
	else snprintf(out, size, "%lldm", minutes);
}

void Si_formatWifi(int supported, int enabled, int rc, const MosaicWifi* w, char* out, size_t size) {
	if (supported != 1) snprintf(out, size, "unavailable");
	else if (enabled == 0) snprintf(out, size, "off");
	else if (rc < 0 || !w) snprintf(out, size, "checking");
	else if (rc == 0) snprintf(out, size, "not connected");
	else {
		char signal[32] = "";
		if (w->rssi < 0) snprintf(signal, sizeof(signal), ", %i dBm", w->rssi);
		else if (w->bars > 0) snprintf(signal, sizeof(signal), ", %i of 3 bars", w->bars);
		snprintf(out, size, "%.63s, %.47s%s", w->ssid[0] ? w->ssid : "hidden network", w->ip[0] ? w->ip : "no address yet",
			signal);
	}
}

void Si_formatBattery(int level, int charging, char* out, size_t size) {
	if (level < 0) snprintf(out, size, "unavailable");
	else if (charging == 1) snprintf(out, size, "%i%%, charging", level);
	else snprintf(out, size, "%i%%", level);
}

// The line at *p up to its newline, trimmed, into out, moving *p past it.
static void takeLine(const char** p, char* out, size_t size) {
	const char* s = *p;
	size_t n = strcspn(s, "\r\n");
	*p = s + n;
	while (**p == '\r' || **p == '\n') (*p)++;
	while (n && isspace((unsigned char)*s)) {
		s++;
		n--;
	}
	while (n && isspace((unsigned char)s[n - 1])) n--;
	if (n >= size) n = size - 1;
	memcpy(out, s, n);
	out[n] = '\0';
}

int Si_formatVersion(const char* text, char* out, size_t size) {
	char release[SI_TEXT_MAX] = "", commit[SI_TEXT_MAX] = "";
	if (text) {
		const char* p = text;
		takeLine(&p, release, sizeof(release));
		takeLine(&p, commit, sizeof(commit));
	}
	if (!release[0]) {
		snprintf(out, size, "unavailable");
		return -1;
	}
	if (commit[0]) snprintf(out, size, "%s (%s)", release, commit);
	else snprintf(out, size, "%s", release);
	return 0;
}

void Si_addrsInit(SiAddrs* a) {
	memset(a, 0, sizeof(*a));
}

int Si_addrsAdd(SiAddrs* a, const char* name, const char* addr) {
	if (!name || !addr || !*addr || a->count >= SI_ADDRS_MAX) return 0;
	if (!strcmp(name, "lo") || !strncmp(name, "lo0", 3) || !strncmp(addr, "127.", 4) || !strcmp(addr, "::1")) return 0;
	if (!strncasecmp(addr, "fe80:", 5)) return 0;
	for (int i = 0; i < a->count; i++) {
		if (!strcmp(a->name[i], name)) return 0;
	}
	snprintf(a->name[a->count], sizeof(a->name[0]), "%s", name);
	snprintf(a->addr[a->count], sizeof(a->addr[0]), "%s", addr);
	a->count++;
	return 1;
}

void Si_formatAddrs(const SiAddrs* a, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!a->count) {
		snprintf(out, size, "none");
		return;
	}
	size_t used = 0;
	for (int i = 0; i < a->count && used < size; i++) {
		int n = snprintf(out + used, size - used, "%s%s (%s)", i ? ", " : "", a->addr[i], a->name[i]);
		if (n < 0) break;
		used += (size_t)n;
	}
}

///////////////////////////////////////
// battery history

void Si_historyInit(SiHistory* h) {
	memset(h, 0, sizeof(*h));
}

// The CSV field at *p into out, unquoting it, moving *p past its comma.
// Returns 0, or -1 at the end of the line or for one that doesn't fit.
static int csvField(const char** p, char* out, size_t size) {
	const char* s = *p;
	size_t o = 0;
	if (!*s || *s == '\r' || *s == '\n') return -1;
	if (*s == '"') {
		s++;
		for (;;) {
			if (!*s || *s == '\n') return -1;
			if (*s == '"') {
				if (s[1] != '"') {
					s++;
					break;
				}
				s++;
			}
			if (o + 1 >= size) return -1;
			out[o++] = *s++;
		}
	} else {
		while (*s && *s != ',' && *s != '\r' && *s != '\n') {
			if (o + 1 >= size) return -1;
			out[o++] = *s++;
		}
	}
	out[o] = '\0';
	if (*s == ',') s++;
	else if (*s && *s != '\r' && *s != '\n') return -1;
	*p = s;
	return 0;
}

static int wholeNumber(const char* text, long long lo, long long hi, long long* out) {
	char* end;
	errno = 0;
	long long v = strtoll(text, &end, 10);
	if (errno || end == text || *end || v < lo || v > hi) return -1;
	*out = v;
	return 0;
}

int Si_parseRow(const char* line, SiSession* out) {
	if (!line) return -1;
	char date[32], skip[2048], number[32];
	const char* p = line;
	if (csvField(&p, date, sizeof(date)) || csvField(&p, skip, sizeof(skip)) || csvField(&p, skip, sizeof(skip)))
		return -1;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	char tail;
	if (sscanf(date, "%4d-%2d-%2d %2d:%2d:%2d%c", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
			&tm.tm_sec, &tail) != 6)
		return -1;
	if (tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 || tm.tm_mday > 31) return -1;
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	tm.tm_isdst = -1;
	time_t when = mktime(&tm);
	if (when == (time_t)-1) return -1;
	long long seconds, start_level, end_level;
	if (csvField(&p, number, sizeof(number)) || wholeNumber(number, 1, 100LL * 24 * 3600, &seconds)) return -1;
	if (csvField(&p, number, sizeof(number)) || wholeNumber(number, 0, 100, &start_level)) return -1;
	if (csvField(&p, number, sizeof(number)) || wholeNumber(number, 0, 100, &end_level)) return -1;
	out->start = (long long)when;
	out->seconds = seconds;
	out->start_level = (int)start_level;
	out->end_level = (int)end_level;
	return 0;
}

int Si_historyAdd(SiHistory* h, const char* text) {
	int added = 0;
	for (const char* line = text; line && *line;) {
		SiSession s;
		if (Si_parseRow(line, &s) == 0) {
			// in start order, dropping the oldest when full
			int at = h->count;
			while (at > 0 && h->sessions[at - 1].start > s.start) at--;
			if (h->count == SI_SESSIONS_MAX) {
				if (at == 0) goto next;
				memmove(&h->sessions[0], &h->sessions[1], sizeof(SiSession) * (size_t)(at - 1));
				at--;
			} else {
				memmove(&h->sessions[at + 1], &h->sessions[at], sizeof(SiSession) * (size_t)(h->count - at));
				h->count++;
			}
			h->sessions[at] = s;
			added++;
		}
	next:
		line = strchr(line, '\n');
		if (line) line++;
	}
	return added;
}

long long Si_historySeconds(const SiHistory* h) {
	long long total = 0;
	for (int i = 0; i < h->count; i++) total += h->sessions[i].seconds;
	return total;
}

int Si_graph(const SiHistory* h, int columns, int* levels) {
	long long total = Si_historySeconds(h);
	if (columns <= 0 || total <= 0) return 0;
	int session = 0;
	long long before = 0; // play time before this session
	for (int c = 0; c < columns; c++) {
		// the column's middle, in play seconds, times two to keep it whole
		long long mid2 = ((long long)c * 2 + 1) * total / columns;
		while (session < h->count - 1 && (before + h->sessions[session].seconds) * 2 <= mid2) {
			before += h->sessions[session].seconds;
			session++;
		}
		const SiSession* s = &h->sessions[session];
		long long into2 = mid2 - before * 2;
		if (into2 < 0) into2 = 0;
		if (into2 > s->seconds * 2) into2 = s->seconds * 2;
		long long span = (long long)(s->end_level - s->start_level);
		long long num = span * into2;
		long long den = s->seconds * 2;
		// rounded to the nearest, either side of 0
		long long step = num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
		levels[c] = s->start_level + (int)step;
	}
	return columns;
}

int Si_barHeight(int level, int height) {
	if (level <= 0 || height <= 0) return 0;
	if (level > 100) level = 100;
	int h = (int)(((long long)level * height + 50) / 100);
	return h < 1 ? 1 : h;
}

void Si_graphCaption(const SiHistory* h, char* out, size_t size) {
	if (!h->count) {
		snprintf(out, size, "%s", "");
		return;
	}
	char play[32];
	Si_formatUptime(Si_historySeconds(h), play, sizeof(play));
	snprintf(out, size, "Battery over the last %i game%s, %s of play", h->count, h->count == 1 ? "" : "s", play);
}

///////////////////////////////////////
// layout

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

MosaicRect Si_hole(const MosaicRect* status, const MosaicRect* header) {
	MosaicRect none = { 0, 0, 0, 0 };
	int has_status = !empty(status), has_header = !empty(header);
	if (!has_status && !has_header) return none;
	if (!has_header) return *status;
	if (!has_status) return *header;
	int x1 = status->x < header->x ? status->x : header->x;
	int y1 = status->y < header->y ? status->y : header->y;
	int x2 = status->x + status->w > header->x + header->w ? status->x + status->w : header->x + header->w;
	int y2 = status->y + status->h > header->y + header->h ? status->y + status->h : header->y + header->h;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

int Si_layout(const MosaicRect* area, const MosaicRect* hole, int scale, int title_w, int rows, int label_w, int row_h,
		int caption_h, SiLayout* out) {
	memset(out, 0, sizeof(*out));
	if (empty(area) || scale < 1 || (rows > 0 && row_h <= 0)) return -1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale, inner = BUTTON_PADDING * scale;
	int left = area->x + pad, right = area->x + area->w - pad;

	// the title pill, stopping short of the hole on its line
	int title_right = right;
	if (!empty(hole) && hole->y < area->y + pad + pill && hole->y + hole->h > area->y + pad && hole->x - pad < title_right)
		title_right = hole->x - pad;
	int tw = title_w + 2 * inner;
	if (tw > title_right - left) tw = title_right - left;
	if (title_w > 0 && tw > 2 * inner) out->title = (MosaicRect){ left, area->y + pad, tw, pill };

	int top = area->y + pad + pill + pad;
	if (!empty(hole) && hole->y + hole->h + pad > top) top = hole->y + hole->h + pad;
	int bottom = area->y + area->h - pad - pill - pad; // above the hints
	if (bottom - top < (rows > 0 ? row_h : GRAPH_MIN * scale) || right - left <= 0) return -1;

	int y = top;
	for (int i = 0; i < rows && i < SI_ROWS; i++) {
		if (y + row_h > bottom) break;
		out->rows[i] = (MosaicRect){ left + inner, y, right - left - 2 * inner, row_h };
		y += row_h;
	}
	int lw = label_w + pad * 2;
	int max_lw = (right - left - 2 * inner) / 2;
	out->label_w = lw < max_lw ? lw : max_lw;

	// the graph under the rows, when there's room for one at least GRAPH_MIN tall
	if (rows > 0) y += pad;
	int graph_y = y + caption_h + scale * 2;
	if (bottom - graph_y >= GRAPH_MIN * scale) {
		out->caption = (MosaicRect){ left + inner, y, right - left - 2 * inner, caption_h };
		out->graph = (MosaicRect){ left + inner, graph_y, right - left - 2 * inner, bottom - graph_y };
	}
	return 0;
}

void Si_hints(int own_page, int on_graph, MosaicHint left[1], int* left_count, MosaicHint right[1], int* right_count) {
	*left_count = 0;
	*right_count = 0;
	if (!own_page) {
		right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
		return;
	}
	left[(*left_count)++] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_A, on_graph ? "INFO" : "GRAPH" };
}

int Si_due(unsigned long* next, unsigned long now, unsigned long period) {
	if (now < *next) return 0;
	*next = now + period;
	return 1;
}
