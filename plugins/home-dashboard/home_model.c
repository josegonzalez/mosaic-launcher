// Home dashboard, without the host; see home_model.h.

#include "home_model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

// English the plugin translates where it draws it, for make pot
#define TR_MARK(text) (text)

static HDWords words; // none set: English

void HD_setWords(const HDWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (words.tr_plural) return words.tr_plural(singular, plural, n);
	return n == 1 ? singular : plural;
}

static const char* const mode_names[] = { "dashboard", "minui", "retroachievements", "pico8", "minimal", "consoles" };

int HD_modeOf(const char* name) {
	if (!name) return HD_MODE_DASHBOARD;
	for (int i = 0; i < (int)(sizeof(mode_names) / sizeof(mode_names[0])); i++) {
		if (!strcmp(name, mode_names[i])) return i;
	}
	return HD_MODE_DASHBOARD;
}

const char* HD_modeName(int mode) {
	if (mode < 0 || mode >= (int)(sizeof(mode_names) / sizeof(mode_names[0]))) return mode_names[0];
	return mode_names[mode];
}

int HD_setupReply(const char* current, int refused, char* out, size_t size) {
	int count = (int)(sizeof(mode_names) / sizeof(mode_names[0]));
	size_t used = 0;
	int n = snprintf(out, size, "mode=%s\nmode.count=%i\n", current ? current : "", count);
	if (n < 0 || (size_t)n >= size) return -1;
	used = (size_t)n;
	for (int i = 0; i < count; i++) {
		// the options as the Plugins page shows them, in this plugin's words
		n = snprintf(out + used, size - used, "mode.%i=%s\nmode.label.%i=%s\n", i, mode_names[i], i, tr(mode_names[i]));
		if (n < 0 || (size_t)n >= size - used) return -1;
		used += (size_t)n;
	}
	n = snprintf(out + used, size - used, "restart=1%s", refused ? "\nerror=mode" : "");
	if (n < 0 || (size_t)n >= size - used) return -1;
	return (int)(used + (size_t)n);
}

int HD_tabs(int pinned_on, int favorites_loaded, int stats_on, int* out) {
	int n = 0;
	out[n++] = HD_TAB_HOME;
	if (pinned_on && favorites_loaded) out[n++] = HD_TAB_PINNED;
	if (stats_on) out[n++] = HD_TAB_STATS;
	return n;
}

const char* HD_tabLabel(int tab) {
	switch (tab) {
		case HD_TAB_PINNED: return TR_MARK("Pinned");
		case HD_TAB_STATS: return TR_MARK("Stats");
		default: return TR_MARK("Home");
	}
}

int HD_tabIndex(const int* tabs, int count, int tab) {
	for (int i = 0; i < count; i++) {
		if (tabs[i] == tab) return i;
	}
	return 0;
}

int HD_tabStep(int index, int count, int delta) {
	if (count <= 0) return 0;
	int next = (index + delta) % count;
	return next < 0 ? next + count : next;
}

void HD_rowsMove(HDFocus* focus, const int* counts, int rows, int button) {
	if (rows <= 0 || focus->row >= rows) {
		focus->row = rows > 0 ? rows - 1 : HD_FOCUS_CONTINUE;
		focus->col = 0;
	}
	if (focus->row == HD_FOCUS_CONTINUE) {
		focus->col = 0;
		if (rows > 0 && (button == MOSAIC_BUTTON_DOWN || button == MOSAIC_BUTTON_RIGHT)) focus->row = 0;
		return;
	}
	int last = counts[focus->row] - 1;
	if (focus->col > last) focus->col = last > 0 ? last : 0;
	switch (button) {
		case MOSAIC_BUTTON_DOWN:
			if (focus->row + 1 < rows) focus->row++;
			break;
		case MOSAIC_BUTTON_UP:
			focus->row--;
			break;
		case MOSAIC_BUTTON_LEFT:
			if (focus->col > 0) focus->col--;
			else if (focus->row == 0) focus->row = HD_FOCUS_CONTINUE;
			break;
		case MOSAIC_BUTTON_RIGHT:
			if (focus->col < last) focus->col++;
			break;
	}
	if (focus->row == HD_FOCUS_CONTINUE) focus->col = 0;
	else if (focus->col > counts[focus->row] - 1) focus->col = counts[focus->row] > 0 ? counts[focus->row] - 1 : 0;
}

int HD_listNext(const char** p, char* out, size_t size) {
	if (size) out[0] = '\0';
	if (!p || !*p) return 0;
	const char* s = *p;
	for (;;) {
		while (*s == ',' || isspace((unsigned char)*s)) s++;
		if (!*s) {
			*p = s;
			return 0;
		}
		const char* start = s;
		while (*s && *s != ',') s++;
		const char* end = s;
		while (end > start && isspace((unsigned char)end[-1])) end--;
		if (end > start) {
			snprintf(out, size, "%.*s", (int)(end - start), start);
			*p = s;
			return 1;
		}
	}
}

static const char* const row_names[HD_ROW_KINDS] = { "continue", "recent", "added", "played", "random", "shortcuts" };

int HD_parseRows(const char* text, int* out, int max, char* unknown, size_t size) {
	if (size) unknown[0] = '\0';
	int n = 0;
	char name[64];
	const char* p = text;
	while (HD_listNext(&p, name, sizeof(name))) {
		int row = -1;
		for (int i = 0; i < HD_ROW_KINDS && row < 0; i++) {
			if (!strcasecmp(name, row_names[i])) row = i;
		}
		if (row < 0) {
			size_t used = strlen(unknown);
			if (used < size) snprintf(unknown + used, size - used, "%s%s", used ? "," : "", name);
			continue;
		}
		int seen = 0;
		for (int i = 0; i < n && !seen; i++) seen = out[i] == row;
		if (!seen && n < max) out[n++] = row;
	}
	return n;
}

const char* HD_rowName(int row) {
	return row >= 0 && row < HD_ROW_KINDS ? row_names[row] : "";
}

const char* HD_rowLabel(int row) {
	switch (row) {
		case HD_ROW_CONTINUE: return TR_MARK("RESUME");
		case HD_ROW_RECENT: return TR_MARK("RECENT");
		case HD_ROW_ADDED: return TR_MARK("NEWLY ADDED");
		case HD_ROW_PLAYED: return TR_MARK("MOST PLAYED");
		case HD_ROW_RANDOM: return TR_MARK("RANDOM");
		case HD_ROW_SHORTCUTS: return TR_MARK("SHORTCUTS");
	}
	return "";
}

int HD_recentPick(int row, const int* can_resume, const int* available, int count, int* out, int max) {
	int n = 0;
	for (int i = 1; i < count && n < max; i++) {
		if (row == HD_ROW_CONTINUE && (!can_resume[i] || !available[i])) continue;
		out[n++] = i;
	}
	return n;
}

int HD_topAdd(long long* keys, int* ids, int n, int max, long long key, int id) {
	if (max <= 0) return 0;
	int at = n;
	while (at > 0 && keys[at - 1] < key) at--;
	if (at >= max) return n;
	int last = n < max ? n : max - 1;
	for (int k = last; k > at; k--) {
		keys[k] = keys[k - 1];
		ids[k] = ids[k - 1];
	}
	keys[at] = key;
	ids[at] = id;
	return n < max ? n + 1 : n;
}

unsigned int HD_random(unsigned int* state) {
	unsigned int x = *state ? *state : 1;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

int HD_reservoir(int* out, int want, int seen, int id, unsigned int* state) {
	if (want <= 0 || seen < 0) return 0;
	if (seen < want) {
		out[seen] = id;
		return seen + 1;
	}
	unsigned int at = HD_random(state) % (unsigned int)(seen + 1);
	if (at < (unsigned int)want) out[at] = id;
	return want;
}

void HD_systemName(const char* folder, char* out, size_t size) {
	snprintf(out, size, "%s", folder ? folder : "");
	char* open = strrchr(out, '(');
	if (!open || open == out || !strchr(open, ')')) return;
	while (open > out && open[-1] == ' ') open--;
	if (open > out) *open = '\0';
}

int HD_consoleAdd(HDConsole* list, int count, int max, int kind, int emulator, const char* system, const char* path,
		const char* name, int counts) {
	if (!system) return count;
	if (kind == MOSAIC_INDEX_SYSTEM) {
		if (!emulator || count >= max || !path) return count;
		HDConsole* c = &list[count];
		memset(c, 0, sizeof(*c));
		snprintf(c->system, sizeof(c->system), "%s", system);
		snprintf(c->path, sizeof(c->path), "%s", path);
		HD_systemName(name && name[0] ? name : system, c->name, sizeof(c->name));
		return count + 1;
	}
	if (kind != MOSAIC_INDEX_GAME || !counts) return count;
	// a system's entries follow it, so its console is the last, or near it
	for (int i = count - 1; i >= 0; i--) {
		if (!strcmp(list[i].system, system)) {
			list[i].games++;
			break;
		}
	}
	return count;
}

int HD_consolesKeep(HDConsole* list, int count) {
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (list[i].games <= 0) continue;
		if (n != i) list[n] = list[i];
		n++;
	}
	return n;
}

static void trimEnd(char* text) {
	size_t len = strlen(text);
	while (len && text[len - 1] == ' ') text[--len] = '\0';
}

int HD_wrap(const char* text, int width, int (*measure)(const char* text, void* data), void* data,
		char lines[][HD_LINE_MAX], int max_lines) {
	int n = 0;
	if (!text || max_lines <= 0) return 0;
	const char* p = text;
	while (*p == ' ') p++;
	while (*p && n < max_lines) {
		char* line = lines[n];
		line[0] = '\0';
		if (n == max_lines - 1) {
			// the rest, cut when it's drawn
			snprintf(line, HD_LINE_MAX, "%s", p);
			trimEnd(line);
			n++;
			break;
		}
		// as many words as fit, at least one
		const char* end = p;
		for (;;) {
			const char* next = end;
			while (*next == ' ') next++;
			while (*next && *next != ' ') next++;
			char trial[HD_LINE_MAX];
			snprintf(trial, sizeof(trial), "%.*s", (int)(next - p), p);
			if (end != p && measure(trial, data) > width) break;
			end = next;
			if (!*end) break;
		}
		snprintf(line, HD_LINE_MAX, "%.*s", (int)(end - p), p);
		trimEnd(line);
		n++;
		p = end;
		while (*p == ' ') p++;
	}
	return n;
}

int HD_linesFit(char lines[][HD_LINE_MAX], int count, int width, int (*measure)(const char* text, void* data), void* data) {
	for (int i = 0; i < count; i++) {
		if (measure(lines[i], data) > width) return 0;
	}
	return 1;
}

int HD_linesTop(const MosaicRect* box, int lines, int line_h) {
	return box->y + (box->h - lines * line_h) / 2;
}

void HD_clock(int hour, int minute, int h24, char* out, size_t size) {
	if (h24) {
		snprintf(out, size, "%02i:%02i", hour, minute);
		return;
	}
	int h = hour % 12;
	snprintf(out, size, "%i:%02i %s", h ? h : 12, minute, hour < 12 ? "AM" : "PM");
}

int HD_listMove(int selected, int count, int delta, int wrap) {
	if (count <= 0) return -1;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int next = selected + delta;
	if (next < 0) return wrap && delta == -1 ? count - 1 : 0;
	if (next >= count) return wrap && delta == 1 ? 0 : count - 1;
	return next;
}

int HD_window(int selected, int count, int rows, int first) {
	if (count <= 0 || rows <= 0) return 0;
	if (rows >= count) return 0;
	if (first > count - rows) first = count - rows;
	if (first < 0) first = 0;
	if (selected < first) first = selected;
	if (selected >= first + rows) first = selected - rows + 1;
	return first < 0 ? 0 : first;
}

int HD_scroll(int first, int by, int count, int rows) {
	int last = count - (rows > 0 ? rows : 1);
	if (last < 0) last = 0;
	first += by;
	if (first > last) first = last;
	return first < 0 ? 0 : first;
}

int HD_shown(const int* header, int count, int first, int rows) {
	if (first < 0) first = 0;
	int n = count - first;
	if (n > rows) n = rows;
	while (n > 0 && header[first + n - 1]) n--;
	return n < 0 ? 0 : n;
}

void HD_duration(long long seconds, char* out, size_t size) {
	if (seconds <= 0) snprintf(out, size, "%s", "");
	else if (seconds < 60) snprintf(out, size, "%s", tr("<1m"));
	else if (seconds < 3600) snprintf(out, size, tr("%lldm"), seconds / 60);
	else snprintf(out, size, tr("%lldh %lldm"), seconds / 3600, seconds / 60 % 60);
}

void HD_playedLine(long long seconds, char* out, size_t size) {
	char played[64];
	HD_duration(seconds, played, sizeof(played));
	if (played[0]) snprintf(out, size, tr("%s played"), played);
	else snprintf(out, size, "%s", "");
}

int HD_timeLeft(const char* state, int limit, int granted, long long left, char* out, size_t size) {
	snprintf(out, size, "%s", "");
	if (!state || (strcmp(state, "ok") && strcmp(state, "warn") && strcmp(state, "up")) || limit <= 0 || left < 0) return 0;
	char shown[64], of[64];
	HD_duration((left + 59) / 60 * 60, shown, sizeof(shown));
	if (!shown[0]) snprintf(shown, sizeof(shown), tr("%lldm"), 0LL);
	HD_duration(((long long)limit + (granted > 0 ? granted : 0)) * 60, of, sizeof(of));
	snprintf(out, size, tr("%s of %s"), shown, of);
	return 1;
}

void HD_daysAgo(int days, char* out, size_t size) {
	if (days <= 0) snprintf(out, size, "%s", tr("Today"));
	else if (days == 1) snprintf(out, size, "%s", tr("Yesterday"));
	else snprintf(out, size, trPlural("%i day ago", "%i days ago", (unsigned long)days), days);
}

void HD_achievementLine(int unlocked, int total, int points_unlocked, int points, char* out, size_t size) {
	if (total <= 0) snprintf(out, size, "%s", "");
	else if (points <= 0) snprintf(out, size, "%i/%i", unlocked, total);
	else {
		char earned[128];
		snprintf(earned, sizeof(earned), tr("%i/%i points"), points_unlocked, points);
		snprintf(out, size, "%i/%i \xc2\xb7 %s", unlocked, total, earned);
	}
}

int HD_mostPlayed(const long long* times, int count, int* out, int max) {
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (times[i] <= 0) continue;
		// insert after every one with as much time or more, so ties keep index order
		int at = n;
		while (at > 0 && times[out[at - 1]] < times[i]) at--;
		if (at >= max) continue;
		int last = n < max ? n : max - 1;
		for (int k = last; k > at; k--) out[k] = out[k - 1];
		out[at] = i;
		if (n < max) n++;
	}
	return n;
}

int HD_tagOf(const char* folder, char* out, size_t size) {
	if (size) out[0] = '\0';
	if (!folder) return 0;
	const char* open = strrchr(folder, '(');
	if (!open) return 0;
	const char* close = strchr(open, ')');
	if (!close || close == open + 1) return 0;
	snprintf(out, size, "%.*s", (int)(close - open - 1), open + 1);
	return 1;
}

int HD_tagIn(const char* tag, const char* tags) {
	if (!tag || !tag[0] || !tags) return 0;
	const char* p = tags;
	while (*p) {
		while (*p == ',' || isspace((unsigned char)*p)) p++;
		const char* start = p;
		while (*p && *p != ',') p++;
		const char* end = p;
		while (end > start && isspace((unsigned char)end[-1])) end--;
		size_t len = (size_t)(end - start);
		if (len && len == strlen(tag) && !strncasecmp(start, tag, len)) return 1;
	}
	return 0;
}

static int endsWithCase(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a > b && !strcasecmp(s + a - b, suffix);
}

int HD_isPicoCart(const char* name) {
	if (!name || name[0] == '.') return 0;
	return endsWithCase(name, ".p8") || endsWithCase(name, ".p8.png");
}

int HD_pluginLoaded(const MosaicHost* host, const char* id) {
	return host && id && id[0] && MOSAIC_HOST_HAS(host, plugin_loaded) && host->plugin_loaded(id);
}

const char* HD_openLabel(int type, int can_resume) {
	if (type == MOSAIC_ENTRY_PAK) return "OPEN";
	return can_resume ? "RESUME" : "PLAY";
}

int HD_cellsFit(int width, int cell, int gap) {
	if (cell <= 0 || width < cell) return 0;
	return (width + gap) / (cell + gap);
}

// the continue card's art, square at its left, and its text beside it
static void cardParts(HDLayout* out, int pad) {
	int art = out->card.h - 2 * pad;
	if (art > out->card.w / 2) art = out->card.w / 2;
	if (art < 0) art = 0;
	out->art = (MosaicRect){ out->card.x + pad, out->card.y + (out->card.h - art) / 2, art, art };
	out->text.x = out->art.x + out->art.w + pad;
	out->text.y = out->card.y + pad;
	out->text.w = out->card.x + out->card.w - pad - out->text.x;
	out->text.h = out->card.h - 2 * pad;
}

int HD_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int top_h, int hints_y,
		int scale, int name_h, int shelf_on, HDLayout* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int pad = 10 * scale;
	if (top_h <= 0) top_h = 30 * scale;

	int right = area->x + area->w;
	if (header && header->w > 0) right = header->x;
	else if (status && status->w > 0) right = status->x;
	out->tabs.x = area->x + pad;
	out->tabs.y = status && status->w > 0 ? status->y : area->y + pad;
	out->tabs.w = right - pad - out->tabs.x;
	out->tabs.h = top_h;

	out->content.x = area->x + pad;
	out->content.y = out->tabs.y + out->tabs.h + pad;
	out->content.w = area->w - 2 * pad;
	out->content.h = hints_y - pad - out->content.y;
	if (out->content.w <= 0 || out->content.h < 40 * scale || out->tabs.w <= 0) return -1;

	out->card = out->content;
	out->gap = pad;
	if (shelf_on) {
		out->card.h = out->content.h * 55 / 100;
		out->shelf.x = out->content.x;
		out->shelf.y = out->card.y + out->card.h + pad;
		out->shelf.w = out->content.w;
		out->shelf.h = out->content.y + out->content.h - out->shelf.y;
		out->cell = out->shelf.h - name_h - 4 * scale;
		if (out->cell < 16 * scale) out->cell = 0;
		out->cells = HD_cellsFit(out->shelf.w, out->cell, out->gap);
		if (!out->cells) {
			out->card = out->content;
			memset(&out->shelf, 0, sizeof(out->shelf));
			out->cell = 0;
		}
	}

	cardParts(out, pad);
	return 0;
}

void HD_layoutTitle(HDLayout* layout, int title_h, int card_min_h, int name_h, int scale) {
	if (!layout->cells || title_h <= 0) return;
	if (scale < 1) scale = 1;
	int from_card = layout->card.h - card_min_h;
	if (from_card > title_h) from_card = title_h;
	if (from_card < 0) from_card = 0;
	int from_shelf = title_h - from_card;
	HDLayout titled = *layout;
	titled.card.h -= from_card;
	titled.shelf.y += from_shelf;
	titled.shelf.h -= from_shelf;
	titled.cell = titled.shelf.h - name_h - 4 * scale;
	if (titled.cell < 16 * scale) return; // no room: no heading
	titled.cells = HD_cellsFit(titled.shelf.w, titled.cell, titled.gap);
	if (!titled.cells) return;
	cardParts(&titled, titled.gap);
	*layout = titled;
}

int HD_minimalLayout(const MosaicRect* content, int clock_h, int scale, HDLayout* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int pad = 10 * scale;
	out->content = *content;
	out->gap = pad;
	int room = content->h - clock_h - pad;
	if (content->w <= 0 || room < 40 * scale) return -1;
	int h = content->h * 55 / 100;
	if (h > room) h = room;
	int w = content->w * 80 / 100;
	int top = content->y + clock_h + pad;
	out->card = (MosaicRect){ content->x + (content->w - w) / 2, top + (room - h) / 2, w, h };
	cardParts(out, pad);
	return 0;
}

int HD_grid(const MosaicRect* content, int scale, int name_h, int* cell, int* cols, int* rows) {
	if (scale < 1) scale = 1;
	int gap = 10 * scale;
	*rows = 2;
	*cell = (content->h - *rows * name_h - gap * (*rows - 1)) / *rows;
	if (*cell < 16 * scale) {
		*rows = 1;
		*cell = content->h - name_h;
	}
	*cols = 0;
	if (*cell < 8 * scale) return -1;
	*cols = HD_cellsFit(content->w, *cell, gap);
	if (*cols < 1) *cols = 1;
	return 0;
}

int HD_fit(int src_w, int src_h, const MosaicRect* box, MosaicRect* out) {
	if (src_w <= 0 || src_h <= 0 || box->w <= 0 || box->h <= 0) return -1;
	int w = box->w, h = (int)((long long)src_h * box->w / src_w);
	if (h > box->h) {
		h = box->h;
		w = (int)((long long)src_w * box->h / src_h);
	}
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	*out = (MosaicRect){ box->x + (box->w - w) / 2, box->y + (box->h - h) / 2, w, h };
	return 0;
}
