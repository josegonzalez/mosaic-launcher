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

static const char* const mode_names[] = { "dashboard", "minui", "retroachievements", "pico8" };

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

int HD_homeMove(int focus, int shelf_count, int button) {
	if (shelf_count <= 0) return HD_FOCUS_CONTINUE;
	if (focus >= shelf_count) focus = shelf_count - 1;
	switch (button) {
		case MOSAIC_BUTTON_DOWN:
			return focus == HD_FOCUS_CONTINUE ? 0 : focus;
		case MOSAIC_BUTTON_UP:
			return HD_FOCUS_CONTINUE;
		case MOSAIC_BUTTON_LEFT:
			if (focus == HD_FOCUS_CONTINUE) return focus;
			return focus - 1 < 0 ? HD_FOCUS_CONTINUE : focus - 1;
		case MOSAIC_BUTTON_RIGHT:
			if (focus == HD_FOCUS_CONTINUE) return 0;
			return focus + 1 < shelf_count ? focus + 1 : focus;
	}
	return focus;
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

	int art = out->card.h - 2 * pad;
	if (art > out->card.w / 2) art = out->card.w / 2;
	if (art < 0) art = 0;
	out->art = (MosaicRect){ out->card.x + pad, out->card.y + (out->card.h - art) / 2, art, art };
	out->text.x = out->art.x + out->art.w + pad;
	out->text.y = out->card.y + pad;
	out->text.w = out->card.x + out->card.w - pad - out->text.x;
	out->text.h = out->card.h - 2 * pad;
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
