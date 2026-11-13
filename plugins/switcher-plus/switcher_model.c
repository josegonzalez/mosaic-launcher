#include <stdio.h>
#include <string.h>
#include <time.h>
#include "switcher_model.h"

// English the caller's words translate, for make pot
#define TR_MARK(text) (text)

// stock's sizes, at scale 1
#define PADDING 10
#define PILL_SIZE 30
#define BUTTON_PADDING 12

int SP_style(const char* value) {
	if (value && strcmp(value, "strip") == 0) return SP_STYLE_STRIP;
	if (value && strcmp(value, "list") == 0) return SP_STYLE_LIST;
	return SP_STYLE_FULL;
}

int SP_move(int selected, int count, int by, int fresh) {
	if (count <= 1 || selected < 0 || selected >= count || by == 0) return selected;
	if (by < 0) {
		if (selected > 0) return selected + by > 0 ? selected + by : 0;
		return fresh ? count - 1 : selected;
	}
	if (selected < count - 1) return selected + by < count - 1 ? selected + by : count - 1;
	return fresh ? 0 : selected;
}

int SP_afterRemove(int selected, int count) {
	if (count <= 0) return 0;
	int to = selected - 1;
	if (to < 0 || to >= count) to = count - 1;
	return to;
}

int SP_window(int first, int selected, int count, int rows) {
	if (count <= 0 || rows <= 0) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	if (selected < first) first = selected;
	if (selected >= first + rows) first = selected - rows + 1;
	if (first > count - rows) first = count - rows;
	return first < 0 ? 0 : first;
}

void SP_hints(int count, int can_resume, MosaicHint left[2], int* left_count, MosaicHint right[2], int* right_count) {
	*left_count = 0;
	*right_count = 0;
	if (count <= 0) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
		*right_count = 1;
		return;
	}
	left[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	*left_count = 1;
	if (can_resume) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_Y, "REMOVE" };
		right[1] = (MosaicHint){ MOSAIC_BUTTON_A, "RESUME" };
		*right_count = 2;
	} else {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_A, "OPEN" };
		*right_count = 1;
	}
}

///////////////////////////////////////
// the recents

int SP_sync(SPRecents* recents, int count, SPPathFn path, SPSkipFn skip, void* data) {
	if (count < 0) count = 0;
	if (count > SP_MAX_RECENTS) count = SP_MAX_RECENTS;
	int changed = recents->count < 0 || count != recents->host_count;
	for (int i = 0; i < count; i++) {
		const char* p = path(i, data);
		if (!p) p = "";
		if (!changed && strncmp(recents->host_path[i], p, SP_PATH_MAX - 1) == 0) continue;
		changed = 1;
		snprintf(recents->host_path[i], SP_PATH_MAX, "%s", p);
	}
	if (!changed) return 0;
	recents->host_count = count;
	recents->count = 0;
	for (int i = 0; i < count; i++) {
		if (skip && skip(recents->host_path[i], data)) continue;
		memcpy(recents->path[recents->count], recents->host_path[i], SP_PATH_MAX);
		recents->index[recents->count++] = i;
	}
	memset(recents->known, 0, sizeof(recents->known));
	return 1;
}

const MosaicRecent* SP_info(SPRecents* recents, int index, SPInfoFn info, void* data) {
	if (index < 0 || index >= recents->count) return NULL;
	if (!recents->known[index]) {
		MosaicRecent* r = &recents->info[index];
		memset(r, 0, sizeof(*r));
		r->size = sizeof(*r);
		recents->known[index] = info(recents->index[index], r, data) == 0 ? 1 : -1;
	}
	return recents->known[index] > 0 ? &recents->info[index] : NULL;
}

///////////////////////////////////////
// layout

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

static int bottom(const MosaicRect* r) {
	return r->y + r->h;
}

int SP_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]) {
	if (empty(r)) return 0;
	MosaicRect m = empty(hole) ? (MosaicRect){ 0, 0, 0, 0 } : SP_meet(r, hole);
	if (empty(&m)) {
		out[0] = *r;
		return 1;
	}
	int n = 0;
	if (m.y > r->y) out[n++] = (MosaicRect){ r->x, r->y, r->w, m.y - r->y };
	if (m.x > r->x) out[n++] = (MosaicRect){ r->x, m.y, m.x - r->x, m.h };
	if (m.x + m.w < r->x + r->w) out[n++] = (MosaicRect){ m.x + m.w, m.y, r->x + r->w - m.x - m.w, m.h };
	if (bottom(&m) < bottom(r)) out[n++] = (MosaicRect){ r->x, bottom(&m), r->w, bottom(r) - bottom(&m) };
	return n;
}

MosaicRect SP_hole(const MosaicRect* status, const MosaicRect* header) {
	int has_status = !empty(status), has_header = !empty(header);
	if (!has_status && !has_header) return (MosaicRect){ 0, 0, 0, 0 };
	if (!has_header) return *status;
	if (!has_status) return *header;
	int x0 = status->x < header->x ? status->x : header->x;
	int y0 = status->y < header->y ? status->y : header->y;
	int x1 = status->x + status->w > header->x + header->w ? status->x + status->w : header->x + header->w;
	int y1 = bottom(status) > bottom(header) ? bottom(status) : bottom(header);
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
}

MosaicRect SP_meet(const MosaicRect* a, const MosaicRect* b) {
	int x0 = a->x > b->x ? a->x : b->x, y0 = a->y > b->y ? a->y : b->y;
	int x1 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	int y1 = bottom(a) < bottom(b) ? bottom(a) : bottom(b);
	if (empty(a) || empty(b) || x1 <= x0 || y1 <= y0) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
}

MosaicRect SP_source(const MosaicRect* dst, const MosaicRect* part, int src_w, int src_h) {
	if (empty(dst) || src_w <= 0 || src_h <= 0) return (MosaicRect){ 0, 0, 0, 0 };
	MosaicRect r = {
		(int)((long)(part->x - dst->x) * src_w / dst->w),
		(int)((long)(part->y - dst->y) * src_h / dst->h),
		(int)((long)part->w * src_w / dst->w),
		(int)((long)part->h * src_h / dst->h),
	};
	if (r.w < 1) r.w = 1;
	if (r.h < 1) r.h = 1;
	return r;
}

MosaicRect SP_fit(int src_w, int src_h, const MosaicRect* box) {
	if (src_w <= 0 || src_h <= 0 || empty(box)) return (MosaicRect){ box ? box->x : 0, box ? box->y : 0, 0, 0 };
	MosaicRect r = *box;
	if ((long)src_w * box->h > (long)src_h * box->w) {
		r.h = (int)((long)box->w * src_h / src_w);
		r.y += (box->h - r.h) / 2;
	} else {
		r.w = (int)((long)box->h * src_w / src_h);
		r.x += (box->w - r.w) / 2;
	}
	return r;
}

MosaicRect SP_title(const MosaicRect* area, const MosaicRect* hole, int scale, int text_w) {
	if (scale < 1) scale = 1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale, inner = BUTTON_PADDING * scale;
	int x = area->x + pad;
	int right = empty(hole) ? area->x + area->w - pad : hole->x;
	int w = right - x;
	if (text_w < 0) text_w = 0;
	if (text_w + 2 * inner < w) w = text_w + 2 * inner;
	if (w <= 2 * inner) return (MosaicRect){ x, area->y + pad, 0, 0 };
	return (MosaicRect){ x, area->y + pad, w, pill };
}

MosaicRect SP_room(const MosaicRect* area, const MosaicRect* hole, int scale) {
	if (scale < 1) scale = 1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int top = area->y + pad;
	if (!empty(hole) && bottom(hole) + pad > top) top = bottom(hole) + pad;
	int end = bottom(area) - pad - pill - pad;
	MosaicRect r = { area->x + pad, top, area->w - 2 * pad, end - top };
	if (r.w <= 0 || r.h <= 0) return (MosaicRect){ r.x, r.y, 0, 0 };
	return r;
}

int SP_strip(const MosaicRect* room, int aspect_w, int aspect_h, int scale, int name_h, int min_w, SPStrip* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	if (aspect_w <= 0 || aspect_h <= 0) aspect_w = aspect_h = 1;
	if (name_h < 0) name_h = 0;
	if (min_w < 1) min_w = 1;
	if (empty(room)) return -1;
	int gap = PADDING * scale;
	int below = name_h > 0 ? gap + name_h : 0;
	int h = room->h - below;
	int w = (int)((long)h * aspect_w / aspect_h);
	if (w > room->w * 2 / 5) {
		w = room->w * 2 / 5;
		h = (int)((long)w * aspect_h / aspect_w);
	}
	if (w < min_w || h < 1) return -1;

	out->room = *room;
	out->w[0] = w;
	out->h[0] = h;
	out->cx = room->x + room->w / 2;
	int top = room->y + (room->h - h - below) / 2;
	out->cy = top + h / 2;
	if (name_h > 0) out->name = (MosaicRect){ room->x, top + h + gap, room->w, name_h };

	for (int d = 1; d <= SP_MAX_SIDES; d++) {
		int dw = out->w[d - 1] * 3 / 5, dh = out->h[d - 1] * 3 / 5;
		int c = out->centre[d - 1] + out->w[d - 1] / 2 + gap + dw / 2;
		if (dw < min_w || dh < 1 || out->cx + c - dw / 2 + dw > room->x + room->w || out->cx - c - dw / 2 < room->x) break;
		out->w[d] = dw;
		out->h[d] = dh;
		out->centre[d] = c;
		out->sides = d;
	}
	return 0;
}

MosaicRect SP_stripCard(const SPStrip* strip, int offset) {
	int k = offset < 0 ? -offset : offset;
	if (strip->w[0] <= 0 || k > strip->sides) return (MosaicRect){ 0, 0, 0, 0 };
	int sign = offset < 0 ? -1 : 1;
	return (MosaicRect){ strip->cx + sign * strip->centre[k] - strip->w[k] / 2, strip->cy - strip->h[k] / 2, strip->w[k],
		strip->h[k] };
}

int SP_list(const MosaicRect* area, const MosaicRect* hole, int scale, SPList* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int x = area->x + pad;
	int w = area->w * 2 / 5 - pad;
	int top = area->y + pad;
	if (!empty(hole) && hole->x < x + w && bottom(hole) + pad > top) top = bottom(hole) + pad;
	int end = bottom(area) - pad - pill - pad;
	int rows = w > 0 ? (end - top) / pill : 0;
	if (rows < 1) return -1;
	out->rows = (MosaicRect){ x, top, w, rows * pill };
	out->row_h = pill;
	out->count = rows;
	MosaicRect room = SP_room(area, hole, scale);
	int px = x + w + pad;
	MosaicRect panel = { px, room.y, room.x + room.w - px, room.h };
	if (!empty(&panel)) out->panel = panel;
	return 0;
}

///////////////////////////////////////
// play time captions, as playtime-captions' captions_model.c has them

#define BUTTON_SIZE 20
#define BUTTON_MARGIN 5
#define SEPARATOR " \xC2\xB7 " // " · "
#define CAPTION_MAX 256

static const char* months[12] = { TR_MARK("Jan"), TR_MARK("Feb"), TR_MARK("Mar"), TR_MARK("Apr"), TR_MARK("May"),
	TR_MARK("Jun"), TR_MARK("Jul"), TR_MARK("Aug"), TR_MARK("Sep"), TR_MARK("Oct"), TR_MARK("Nov"), TR_MARK("Dec") };

static SPWords words; // none set: English

void SP_setWords(const SPWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text and a count's form in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (words.tr_plural) return words.tr_plural(singular, plural, n);
	return n == 1 ? singular : plural;
}

static int isOn(const char* value) {
	return value && strcmp(value, "on") == 0;
}

SPCaptions SP_captions(const char* time, const char* last, const char* sessions, const char* format,
	const char* never, const char* achievements) {
	SPCaptions c;
	c.parts = (isOn(time) ? SP_PART_TIME : 0) | (isOn(last) ? SP_PART_LAST : 0) |
		(isOn(sessions) ? SP_PART_SESSIONS : 0) | (isOn(achievements) ? SP_PART_ACHIEVEMENTS : 0);
	c.format = SP_FORMAT_RELATIVE;
	if (format && strcmp(format, "compact") == 0) c.format = SP_FORMAT_COMPACT;
	if (format && strcmp(format, "date") == 0) c.format = SP_FORMAT_DATE;
	c.never = isOn(never);
	return c;
}

static void duration(long long seconds, int format, char* out, size_t size) {
	if (seconds < 60) {
		snprintf(out, size, "%s", tr("<1m"));
		return;
	}
	long long hours = seconds / 3600, minutes = seconds / 60 % 60;
	if (!hours) snprintf(out, size, tr("%lldm"), minutes);
	else if (format == SP_FORMAT_COMPACT || !minutes) snprintf(out, size, tr("%lldh"), hours);
	else snprintf(out, size, tr("%lldh %lldm"), hours, minutes);
}

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar.
static long long civilDays(int year, int month, int day) {
	year -= month <= 2;
	long long era = (year >= 0 ? year : year - 399) / 400;
	long long yoe = year - era * 400;
	long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static int localDate(long long when, struct tm* out) {
	time_t t = (time_t)when;
	return localtime_r(&t, out) != NULL;
}

// local calendar days from when to now, 0 when when is after now
static int daysBetween(long long when, long long now) {
	struct tm a, b;
	if (!localDate(when, &a) || !localDate(now, &b)) return 0;
	long long days = civilDays(b.tm_year + 1900, b.tm_mon + 1, b.tm_mday) -
		civilDays(a.tm_year + 1900, a.tm_mon + 1, a.tm_mday);
	return days > 0 ? (int)days : 0;
}

// "1 week ago" or "2 weeks ago", in the unit days are counted in
static void ago(int days, char* out, size_t size) {
	const char* text;
	int n;
	if (days < 7) {
		n = days;
		text = trPlural("%i day ago", "%i days ago", (unsigned long)n);
	} else if (days < 30) {
		n = days / 7;
		text = trPlural("%i week ago", "%i weeks ago", (unsigned long)n);
	} else if (days < 365) {
		n = days / 30;
		text = trPlural("%i month ago", "%i months ago", (unsigned long)n);
	} else {
		n = days / 365;
		text = trPlural("%i year ago", "%i years ago", (unsigned long)n);
	}
	snprintf(out, size, text, n);
}

static void lastPlayed(long long when, long long now, int format, char* out, size_t size) {
	int days = daysBetween(when, now);
	if (days == 0) {
		snprintf(out, size, "%s", tr("today"));
		return;
	}
	if (format == SP_FORMAT_COMPACT) {
		if (days < 7) snprintf(out, size, tr("%id"), days);
		else if (days < 30) snprintf(out, size, tr("%iw"), days / 7);
		else if (days < 365) snprintf(out, size, tr("%imo"), days / 30);
		else snprintf(out, size, tr("%iy"), days / 365);
		return;
	}
	if (days == 1) {
		snprintf(out, size, "%s", tr("yesterday"));
		return;
	}
	if (format == SP_FORMAT_DATE) {
		struct tm then, today;
		if (!localDate(when, &then) || !localDate(now, &today)) {
			out[0] = '\0';
			return;
		}
		// a month's name and its day, then the year when it isn't this one's
		const char* month = tr(months[then.tm_mon]);
		if (then.tm_year == today.tm_year) snprintf(out, size, tr("%s %i"), month, then.tm_mday);
		else snprintf(out, size, tr("%s %i, %i"), month, then.tm_mday, then.tm_year + 1900);
		return;
	}
	ago(days, out, size);
}

static void sessions(int count, int format, char* out, size_t size) {
	if (format == SP_FORMAT_COMPACT) snprintf(out, size, tr("%ix"), count);
	else snprintf(out, size, trPlural("%i session", "%i sessions", (unsigned long)count), count);
}

static void achievements(int unlocked, int total, int format, char* out, size_t size) {
	if (format == SP_FORMAT_COMPACT) snprintf(out, size, "%i/%i", unlocked, total);
	else snprintf(out, size, tr("%i/%i achievements"), unlocked, total);
}

int SP_captionBuild(const SPPlay* play, long long now, int parts, int format, int never, int max_parts, char* out,
	size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!play) return 0;
	int known = play->time >= 0 && play->last >= 0 && play->count >= 0;
	int unseen = known && play->count == 0 && play->time == 0 && play->last == 0;
	if (!known || unseen) parts &= SP_PART_ACHIEVEMENTS;
	if (play->cheevos_total <= 0 || play->cheevos < 0) parts &= ~SP_PART_ACHIEVEMENTS;
	int written = 0;
	size_t used = 0;
	if (unseen && never && max_parts >= 1) {
		int n = snprintf(out, size, "%s", tr("Never played"));
		if (n < 0 || (size_t)n >= size) {
			out[0] = '\0';
			return 0;
		}
		used = (size_t)n;
		written = 1;
	}
	static const int order[] = { SP_PART_TIME, SP_PART_LAST, SP_PART_SESSIONS, SP_PART_ACHIEVEMENTS };
	for (int i = 0; i < 4 && written < max_parts; i++) {
		if (!(parts & order[i])) continue;
		char part[128];
		if (order[i] == SP_PART_TIME) duration(play->time, format, part, sizeof(part));
		else if (order[i] == SP_PART_ACHIEVEMENTS)
			achievements(play->cheevos, play->cheevos_total, format, part, sizeof(part));
		else if (order[i] == SP_PART_LAST) {
			// a session that hasn't got a start time says nothing about when
			if (play->last <= 0) continue;
			lastPlayed(play->last, now, format, part, sizeof(part));
		}
		else sessions(play->count, format, part, sizeof(part));
		if (!part[0]) continue;
		int n = snprintf(out + used, size - used, "%s%s", written ? SEPARATOR : "", part);
		if (n < 0 || (size_t)n >= size - used) {
			out[used] = '\0';
			break;
		}
		used += (size_t)n;
		written++;
	}
	return written;
}

int SP_captionFit(const SPPlay* play, long long now, const SPCaptions* captions, int max_width,
	int (*measure)(const char* text, void* ctx), void* ctx, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!captions || !measure || max_width <= 0) return 0;
	int formats[2] = { captions->format, SP_FORMAT_COMPACT };
	int tries = captions->format == SP_FORMAT_COMPACT ? 1 : 2;
	char text[CAPTION_MAX];
	int all = SP_captionBuild(play, now, captions->parts, captions->format, captions->never, 4, text, sizeof(text));
	for (int n = all; n > 0; n--) {
		for (int f = 0; f < tries; f++) {
			SP_captionBuild(play, now, captions->parts, formats[f], captions->never, n, text, sizeof(text));
			int w = measure(text, ctx);
			if (w > 0 && w <= max_width) {
				snprintf(out, size, "%s", text);
				return w;
			}
		}
	}
	return 0;
}

int SP_mute(int fg, int bg, int percent) {
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	int rgb = 0;
	for (int shift = 16; shift >= 0; shift -= 8) {
		int a = fg >> shift & 0xFF, b = bg >> shift & 0xFF;
		rgb |= (a + (b - a) * percent / 100) << shift;
	}
	return rgb;
}

MosaicRect SP_titleCaption(const MosaicRect* area, const MosaicRect* hole, const MosaicRect* title, int scale,
	int text_h) {
	if (scale < 1) scale = 1;
	MosaicRect none = { title ? title->x : 0, title ? bottom(title) : 0, 0, 0 };
	if (empty(area) || empty(title)) return none;
	int pad = PADDING * scale;
	int h = BUTTON_SIZE * scale > text_h ? BUTTON_SIZE * scale : text_h;
	int y = bottom(title) + BUTTON_MARGIN * scale;
	if (y + h > bottom(area) - pad - PILL_SIZE * scale - pad) return none;
	int right = area->x + area->w - pad;
	if (!empty(hole) && hole->y < y + h && bottom(hole) > y && hole->x < right) right = hole->x;
	if (right <= title->x) return none;
	return (MosaicRect){ title->x, y, right - title->x, h };
}

MosaicRect SP_stripCaption(const SPStrip* strip, int pill_h, int scale, int text_h) {
	if (scale < 1) scale = 1;
	int y = strip->name.y + pill_h + BUTTON_MARGIN * scale;
	if (empty(&strip->name) || text_h <= 0 || y + text_h > bottom(&strip->name)) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ strip->name.x, y, strip->name.w, text_h };
}

MosaicRect SP_panelCaption(const MosaicRect* panel, int scale, int text_h, MosaicRect* preview) {
	if (scale < 1) scale = 1;
	if (preview) *preview = panel ? *panel : (MosaicRect){ 0, 0, 0, 0 };
	if (empty(panel) || text_h <= 0) return (MosaicRect){ 0, 0, 0, 0 };
	int above = panel->h - BUTTON_MARGIN * scale - text_h;
	if (above < panel->h / 2) return (MosaicRect){ 0, 0, 0, 0 };
	if (preview) preview->h = above;
	return (MosaicRect){ panel->x, bottom(panel) - text_h, panel->w, text_h };
}
