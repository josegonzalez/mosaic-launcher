#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ss_model.h"

// English the plugin translates where it draws it, for make pot
#define TR_MARK(text) (text)

static SSWords words; // none set: English

void SS_setWords(const SSWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

int SS_hasSecondDisplay(const char* platform) {
	return platform && !strcmp(platform, "desktop");
}

static int side(const char* text, char** end) {
	if (!isdigit((unsigned char)*text)) return -1;
	long v = strtol(text, end, 10);
	return v < SS_SIDE_MIN || v > SS_SIDE_MAX ? -1 : (int)v;
}

int SS_parseSize(const char* text, int* w, int* h) {
	if (!text || !w || !h) return 0;
	char* end;
	int sw = side(text, &end);
	if (sw < 0 || *end != 'x') return 0;
	int sh = side(end + 1, &end);
	if (sh < 0 || *end) return 0;
	*w = sw;
	*h = sh;
	return 1;
}

static int least(int a, int b) { return a < b ? a : b; }
static int most(int a, int b) { return a > b ? a : b; }

void SS_layout(int w, int h, SSLayout* out) {
	memset(out, 0, sizeof(*out));
	if (w <= 0 || h <= 0) return;
	int m = most(4, least(w, h) / 20);
	out->margin = m;
	out->beside = w * 3 >= h * 4;
	if (out->beside) {
		int s = most(0, least(h - 2 * m, (w - 3 * m) / 2));
		out->art = (MosaicRect){ m, (h - s) / 2, s, s };
		int tx = m + s + m;
		out->text = (MosaicRect){ tx, m, most(0, w - m - tx), most(0, h - 2 * m) };
	} else {
		int aw = most(0, w - 2 * m);
		int ah = most(0, least(aw, (h - 3 * m) / 2));
		out->art = (MosaicRect){ m, m, aw, ah };
		int ty = m + ah + m;
		out->text = (MosaicRect){ m, ty, aw, most(0, h - m - ty) };
	}
}

MosaicRect SS_fit(int src_w, int src_h, MosaicRect box) {
	MosaicRect r = { 0, 0, 0, 0 };
	if (src_w <= 0 || src_h <= 0 || box.w <= 0 || box.h <= 0) return r;
	// the wider of the two shapes decides which side fills the box
	if ((long long)src_w * box.h >= (long long)src_h * box.w) {
		r.w = box.w;
		r.h = most(1, (int)((long long)src_h * box.w / src_w));
	} else {
		r.h = box.h;
		r.w = most(1, (int)((long long)src_w * box.h / src_h));
	}
	r.x = box.x + (box.w - r.w) / 2;
	r.y = box.y + (box.h - r.h) / 2;
	return r;
}

int SS_wrap(const char* text, int max_width, int max_lines, SSMeasure measure, void* data, char lines[][SS_LINE_SIZE]) {
	if (!text || !measure || !lines || max_lines <= 0) return 0;
	int count = 0;
	const char* p = text;
	while (*p == ' ') p++;
	while (*p && count < max_lines) {
		char* line = lines[count];
		if (count == max_lines - 1) {
			// the last line takes the rest
			snprintf(line, SS_LINE_SIZE, "%s", p);
			count++;
			break;
		}
		size_t len = 0; // of the line so far, without trailing spaces
		const char* next = p;
		line[0] = '\0';
		while (*next) {
			const char* end = next;
			while (*end && *end != ' ') end++;
			size_t word = (size_t)(end - next);
			size_t gap = len ? 1 : 0;
			if (len + gap + word >= SS_LINE_SIZE) {
				if (len) break;
				word = SS_LINE_SIZE - 1;
			}
			char trial[SS_LINE_SIZE];
			memcpy(trial, line, len);
			if (gap) trial[len] = ' ';
			memcpy(trial + len + gap, next, word);
			trial[len + gap + word] = '\0';
			if (len && measure(trial, data) > max_width) break;
			memcpy(line, trial, len + gap + word + 1);
			len += gap + word;
			next += word;
			while (*next == ' ') next++;
		}
		count++;
		p = next;
	}
	return count;
}

void SS_showFrom(SSSetting get, SSShow* out) {
	if (!out) return;
	static const char* const keys[] = { "details", "caption", "marks", "music" };
	int* on[] = { &out->details, &out->caption, &out->marks, &out->music };
	for (int i = 0; i < 4; i++) {
		const char* value = get ? get(keys[i], "on") : "on";
		*on[i] = !value || strcmp(value, "off") != 0;
	}
}

static const char* const info_keys[] = { "year", "genre", "publisher", "developer", "players" };
static const char* const info_labels[] = { TR_MARK("Year"), TR_MARK("Genre"), TR_MARK("Publisher"),
	TR_MARK("Developer"), TR_MARK("Players") };

const char* SS_infoLabel(const char* key) {
	for (int i = 0; key && i < SS_INFO_MAX; i++)
		if (!strcmp(key, info_keys[i])) return info_labels[i];
	return NULL;
}

int SS_infoLines(const char* reply, char lines[][SS_LINE_SIZE], int max) {
	if (!reply || !lines) return 0;
	int count = 0;
	const char* p = reply;
	while (*p && count < max) {
		const char* eol = strchr(p, '\n');
		size_t n = eol ? (size_t)(eol - p) : strlen(p);
		const char* eq = memchr(p, '=', n);
		char key[32];
		size_t k = eq ? (size_t)(eq - p) : 0;
		if (k > 0 && k < sizeof(key)) {
			memcpy(key, p, k);
			key[k] = '\0';
			const char* label = SS_infoLabel(key);
			int v = (int)(n - k - 1);
			if (label && v > 0) snprintf(lines[count++], SS_LINE_SIZE, "%s: %.*s", tr(label), v, eq + 1);
		}
		p = eol ? eol + 1 : p + n;
	}
	return count;
}

int SS_tagAdd(char* list, size_t size, const char* set_name) {
	if (!list || !size || !set_name || strncmp(set_name, "tags:", 5) != 0 || !set_name[5]) return 0;
	size_t used = strlen(list);
	int n = snprintf(list + used, size - used, "%s%s", used ? ", " : "", set_name + 5);
	if (n < 0 || (size_t)n >= size - used) {
		list[used] = '\0';
		return 0;
	}
	return 1;
}

int SS_marks(int favorite, const char* tags, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	char tagged[SS_LINE_SIZE] = "";
	if (tags && tags[0]) snprintf(tagged, sizeof(tagged), tr("Tags: %s"), tags);
	if (favorite && tagged[0]) snprintf(out, size, "%s \xc2\xb7 %s", tr("Favorite"), tagged);
	else snprintf(out, size, "%s", favorite ? tr("Favorite") : tagged);
	return out[0] != '\0';
}

static void addRow(SSPanel* out, int style, const char* text) {
	if (out->count >= SS_ROWS_MAX || !text || !text[0]) return;
	out->rows[out->count].style = style;
	snprintf(out->rows[out->count].text, SS_LINE_SIZE, "%s", text);
	out->count++;
}

void SS_panel(const SSShow* show, const SSPieces* pieces, SSPanel* out) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	if (!show || !pieces) return;
	if (show->music && pieces->music) snprintf(out->music, sizeof(out->music), "%s", pieces->music);
	if (pieces->empty) return;
	const char* kind = SS_kindLabel(pieces->type);
	if (kind[0]) addRow(out, SS_ROW_KIND, tr(kind));
	char marks[SS_LINE_SIZE];
	if (show->marks && SS_marks(pieces->favorite, pieces->tags, marks, sizeof(marks))) addRow(out, SS_ROW_MARKS, marks);
	if (show->details) {
		char lines[SS_INFO_MAX][SS_LINE_SIZE];
		int count = SS_infoLines(pieces->info, lines, SS_INFO_MAX);
		for (int i = 0; i < count; i++) addRow(out, SS_ROW_INFO, lines[i]);
	}
	if (show->caption) addRow(out, SS_ROW_CAPTION, pieces->caption);
}

const char* SS_kindLabel(int type) {
	switch (type) {
	case MOSAIC_ENTRY_ROM: return TR_MARK("Game");
	case MOSAIC_ENTRY_DIR: return TR_MARK("Folder");
	case MOSAIC_ENTRY_PAK: return TR_MARK("Pak");
	default: return "";
	}
}

static void put32(unsigned char* p, unsigned int v) {
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static unsigned int get32(const unsigned char* p) {
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

void SS_frameHeader(unsigned char out[SS_FRAME_HEADER], unsigned int number, int w, int h) {
	memcpy(out, SS_FRAME_MAGIC, 4);
	put32(out + 4, number);
	put32(out + 8, (unsigned int)w);
	put32(out + 12, (unsigned int)h);
}

int SS_frameParse(const unsigned char* data, size_t len, unsigned int* number, int* w, int* h) {
	if (!data || len < SS_FRAME_HEADER || memcmp(data, SS_FRAME_MAGIC, 4)) return 0;
	unsigned int fw = get32(data + 8), fh = get32(data + 12);
	if (fw < 1 || fh < 1 || fw > 4096 || fh > 4096) return 0;
	if (len < SS_FRAME_HEADER + (size_t)fw * fh * 4) return 0;
	if (number) *number = get32(data + 4);
	if (w) *w = (int)fw;
	if (h) *h = (int)fh;
	return 1;
}

int SS_keepEnv(const char* entry) {
	static const char* const names[] = { "DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "HOME",
		"PATH", "USER", "LOGNAME", "TMPDIR", "TZ", "LANG", "LANGUAGE", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH",
		"MOSAIC_SECOND_SCREEN_SHOT" };
	static const char* const prefixes[] = { "XDG_", "LC_", "SDL_" };
	const char* eq = entry ? strchr(entry, '=') : NULL;
	if (!eq || eq == entry) return 0;
	size_t n = (size_t)(eq - entry);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (strlen(names[i]) == n && !strncmp(entry, names[i], n)) return 1;
	for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
		if (n > strlen(prefixes[i]) && !strncmp(entry, prefixes[i], strlen(prefixes[i]))) return 1;
	return 0;
}
