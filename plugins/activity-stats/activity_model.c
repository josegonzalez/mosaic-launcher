// Activity stats' model: see activity_model.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// English the plugin translates where it draws it, for make pot
#define TR_MARK(text) (text)
#include "activity_model.h"

// stock's sizes, in its units
#define PADDING 10
#define PILL_SIZE 30

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

void AS_name(const char* name, const char* path, char* out, size_t size) {
	if (!size) return;
	if (name && name[0]) {
		snprintf(out, size, "%s", name);
		return;
	}
	const char* base = path ? strrchr(path, '/') : NULL;
	base = base ? base + 1 : path;
	if (!base || !base[0]) {
		snprintf(out, size, "Unknown game");
		return;
	}
	const char* dot = strrchr(base, '.');
	size_t len = dot && dot != base ? (size_t)(dot - base) : strlen(base);
	if (len >= size) len = size - 1;
	memcpy(out, base, len);
	out[len] = '\0';
}

///////////////////////////////////////
// ranking

static const ASGame* ranking; // the games being sorted, for the comparators

static int compareLong(long long a, long long b) {
	return a > b ? -1 : a < b ? 1 : 0;
}

static int byPlayed(const void* a, const void* b) {
	int i = *(const int*)a, k = *(const int*)b;
	const ASGame *x = &ranking[i], *y = &ranking[k];
	int c = compareLong(x->seconds, y->seconds);
	if (!c) c = compareLong(x->count, y->count);
	if (!c) c = compareLong(x->last_played, y->last_played);
	return c ? c : i - k;
}

static int byLaunched(const void* a, const void* b) {
	int i = *(const int*)a, k = *(const int*)b;
	const ASGame *x = &ranking[i], *y = &ranking[k];
	int c = compareLong(x->count, y->count);
	if (!c) c = compareLong(x->seconds, y->seconds);
	if (!c) c = compareLong(x->last_played, y->last_played);
	return c ? c : i - k;
}

static void rank(const ASGame* games, int count, int* order, int (*compare)(const void*, const void*)) {
	if (count <= 0) return;
	for (int i = 0; i < count; i++) order[i] = i;
	ranking = games;
	qsort(order, (size_t)count, sizeof(int), compare);
	ranking = NULL;
}

void AS_rankPlayed(const ASGame* games, int count, int* order) {
	rank(games, count, order, byPlayed);
}

void AS_rankLaunched(const ASGame* games, int count, int* order) {
	rank(games, count, order, byLaunched);
}

void AS_totals(const ASGame* games, int count, long long* seconds, long long* sessions) {
	*seconds = 0;
	*sessions = 0;
	for (int i = 0; i < count; i++) {
		if (games[i].seconds > 0) *seconds += games[i].seconds;
		if (games[i].count > 0) *sessions += games[i].count;
	}
}

///////////////////////////////////////
// formatting

void AS_duration(long long seconds, char* out, size_t size) {
	if (!size) return;
	if (seconds < 0) {
		out[0] = '\0';
		return;
	}
	if (seconds > 0 && seconds < 60) {
		snprintf(out, size, "<1m");
		return;
	}
	long long hours = seconds / 3600, minutes = seconds % 3600 / 60;
	if (!hours) snprintf(out, size, "%lldm", minutes);
	else if (!minutes) snprintf(out, size, "%lldh", hours);
	else snprintf(out, size, "%lldh %lldm", hours, minutes);
}

void AS_sessions(long long count, char* out, size_t size) {
	snprintf(out, size, "%lld %s", count, count == 1 ? "session" : "sessions");
}

#define DOT " \xc2\xb7 "

void AS_rowRight(const ASGame* game, int launches, char* out, size_t size) {
	char played[32], sessions[32];
	AS_duration(game->seconds, played, sizeof(played));
	AS_sessions(game->count > 0 ? game->count : 0, sessions, sizeof(sessions));
	if (launches) snprintf(out, size, "%s" DOT "%s", sessions, played);
	else snprintf(out, size, "%s" DOT "%s", played, sessions);
}

void AS_listCaption(int games, long long seconds, long long sessions, char* out, size_t size) {
	char played[32], counted[32];
	AS_duration(seconds, played, sizeof(played));
	AS_sessions(sessions, counted, sizeof(counted));
	snprintf(out, size, "%i %s" DOT "%s" DOT "%s", games, games == 1 ? "game" : "games", played, counted);
}

void AS_chartCaption(const long long* day, int days, char* out, size_t size) {
	long long total = 0;
	int known = 0;
	for (int i = 0; i < days; i++) {
		if (day[i] < 0) continue;
		total += day[i];
		known++;
	}
	if (!known) {
		snprintf(out, size, "%i days: no play history", days);
		return;
	}
	char sum[32], average[32];
	AS_duration(total, sum, sizeof(sum));
	AS_duration(total / known, average, sizeof(average));
	snprintf(out, size, "%i days: %s" DOT "%s a day", days, sum, average);
}

void AS_dayOf(time_t now, int days_ago, struct tm* out) {
	struct tm tm;
	localtime_r(&now, &tm);
	tm.tm_mday -= days_ago;
	tm.tm_hour = 12;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	tm.tm_isdst = -1;
	time_t then = mktime(&tm);
	localtime_r(&then, out);
}

void AS_dayLabel(time_t now, int days, int days_ago, char* out, size_t size) {
	static const char* weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	if (!size) return;
	out[0] = '\0';
	if (days_ago < 0 || days_ago >= days) return;
	struct tm tm;
	AS_dayOf(now, days_ago, &tm);
	if (days <= AS_WEEK) snprintf(out, size, "%s", weekdays[tm.tm_wday % 7]);
	else if (days_ago % 7 == 0) snprintf(out, size, "%i", tm.tm_mday);
}

///////////////////////////////////////
// the chart

long long AS_axisMax(const long long* day, int days) {
	static const long long steps[] = { 900, 1800, 3600, 7200, 10800, 14400, 21600, 28800, 43200, 86400 };
	long long most = 0;
	for (int i = 0; i < days; i++) {
		if (day[i] > most) most = day[i];
	}
	if (most <= 0) return 3600;
	for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		if (most <= steps[i]) return steps[i];
	}
	return (most + 86399) / 86400 * 86400;
}

int AS_barHeight(long long seconds, long long axis, int plot_h) {
	if (seconds <= 0 || axis <= 0 || plot_h <= 0) return 0;
	long long h = seconds * plot_h / axis;
	if (h < 1) h = 1;
	if (h > plot_h) h = plot_h;
	return (int)h;
}

MosaicRect AS_slot(const MosaicRect* plot, int days, int days_ago) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(plot) || days <= 0 || days_ago < 0 || days_ago >= days) return none;
	int index = days - 1 - days_ago; // from the left
	int x1 = plot->x + (int)((long long)plot->w * index / days);
	int x2 = plot->x + (int)((long long)plot->w * (index + 1) / days);
	return (MosaicRect){ x1, plot->y, x2 - x1, plot->h };
}

MosaicRect AS_bar(const MosaicRect* plot, int days, int days_ago, long long seconds, long long axis) {
	MosaicRect none = { 0, 0, 0, 0 };
	MosaicRect slot = AS_slot(plot, days, days_ago);
	int h = AS_barHeight(seconds, axis, plot ? plot->h : 0);
	if (slot.w <= 0 || h <= 0) return none;
	int w = slot.w * 2 / 3;
	if (w < 1) w = 1;
	return (MosaicRect){ slot.x + (slot.w - w) / 2, plot->y + plot->h - h, w, h };
}

///////////////////////////////////////
// tabs and moving

const char* AS_tabLabel(int tab) {
	switch (tab) {
		case AS_TAB_WEEK: return TR_MARK("7 days");
		case AS_TAB_MONTH: return TR_MARK("30 days");
		case AS_TAB_GAMES: return TR_MARK("Games");
		case AS_TAB_LAUNCHES: return TR_MARK("Launches");
		default: return "";
	}
}

int AS_isList(int tab) {
	return tab == AS_TAB_GAMES || tab == AS_TAB_LAUNCHES;
}

int AS_tabStep(int tab, int by) {
	int next = (tab + by) % AS_TABS;
	return next < 0 ? next + AS_TABS : next;
}

int AS_step(int selected, int count, int by, int fresh) {
	if (count <= 0) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int next = selected + by;
	if (next < 0) return fresh && selected == 0 ? count - 1 : 0;
	if (next >= count) return fresh && selected == count - 1 ? 0 : count - 1;
	return next;
}

int AS_window(int selected, int count, int fit, int first) {
	if (count <= 0 || fit <= 0) return 0;
	if (selected < first) first = selected;
	if (selected >= first + fit) first = selected - fit + 1;
	if (first > count - fit) first = count - fit;
	return first < 0 ? 0 : first;
}

void AS_hints(int can_open, MosaicHint left[1], int* left_count, MosaicHint right[1], int* right_count) {
	left[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	*left_count = 1;
	*right_count = 0;
	if (can_open) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_A, "PLAY" };
		*right_count = 1;
	}
}

///////////////////////////////////////
// layout

int AS_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]) {
	if (empty(r)) return 0;
	int x1 = r->x, y1 = r->y, x2 = r->x + r->w, y2 = r->y + r->h;
	if (empty(hole) || hole->x >= x2 || hole->y >= y2 || hole->x + hole->w <= x1 || hole->y + hole->h <= y1) {
		out[0] = *r;
		return 1;
	}
	int hx1 = hole->x > x1 ? hole->x : x1, hy1 = hole->y > y1 ? hole->y : y1;
	int hx2 = hole->x + hole->w < x2 ? hole->x + hole->w : x2, hy2 = hole->y + hole->h < y2 ? hole->y + hole->h : y2;
	int n = 0;
	if (hy1 > y1) out[n++] = (MosaicRect){ x1, y1, r->w, hy1 - y1 };
	if (hx1 > x1) out[n++] = (MosaicRect){ x1, hy1, hx1 - x1, hy2 - hy1 };
	if (hx2 < x2) out[n++] = (MosaicRect){ hx2, hy1, x2 - hx2, hy2 - hy1 };
	if (hy2 < y2) out[n++] = (MosaicRect){ x1, hy2, r->w, y2 - hy2 };
	return n;
}

MosaicRect AS_hole(const MosaicRect* status, const MosaicRect* header) {
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

MosaicRect AS_tabRoom(const MosaicRect* area, const MosaicRect* hole, int scale) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(area) || scale < 1) return none;
	int pad = PADDING * scale;
	int x = area->x + pad, y = area->y + pad, h = PILL_SIZE * scale;
	int right = area->x + area->w - pad;
	if (!empty(hole) && hole->y < y + h && hole->y + hole->h > y && hole->x - pad < right) right = hole->x - pad;
	if (right <= x) return none;
	return (MosaicRect){ x, y, right - x, h };
}

int AS_tabsFit(const int* tab_w, int count, int arrows_w, int room_w) {
	int w = arrows_w;
	for (int i = 0; i < count; i++) w += tab_w[i];
	return w <= room_w;
}

MosaicRect AS_room(const MosaicRect* area, const MosaicRect* hole, int scale) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(area) || scale < 1) return none;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int top = area->y + pad + pill + pad;
	if (!empty(hole) && hole->y + hole->h + pad > top) top = hole->y + hole->h + pad;
	int bottom = area->y + area->h - pad - pill - pad;
	MosaicRect room = { area->x + pad, top, area->w - 2 * pad, bottom - top };
	return empty(&room) ? none : room;
}

MosaicRect AS_plot(const MosaicRect* room, int scale, int caption_h, int axis_w, int label_h) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(room) || scale < 1) return none;
	int gap = PADDING * scale / 2;
	MosaicRect plot = { room->x + axis_w + gap, room->y + caption_h + 2 * gap, 0, 0 };
	plot.w = room->x + room->w - plot.x;
	plot.h = room->y + room->h - label_h - gap - plot.y;
	return empty(&plot) ? none : plot;
}
