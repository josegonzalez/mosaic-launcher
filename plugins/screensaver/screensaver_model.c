#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "screensaver_model.h"

static SSWords words; // none set: English

void SS_setWords(const SSWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

///////////////////////////////////////
// Settings

static const char* skipSpaces(const char* s) {
	while (*s && isspace((unsigned char)*s)) s++;
	return s;
}

// whole seconds from 0 to SS_MAX_S in ms, or -1
static long parseSeconds(const char* s) {
	s = skipSpaces(s);
	if (!isdigit((unsigned char)*s)) return -1;
	char* end;
	long seconds = strtol(s, &end, 10);
	if (*skipSpaces(end) || seconds < 0 || seconds > SS_MAX_S) return -1;
	return seconds * 1000L;
}

unsigned long SS_parseIdle(const char* setting) {
	if (!setting) return SS_IDLE_DEFAULT_MS;
	const char* s = skipSpaces(setting);
	if (!strncmp(s, "off", 3) && !*skipSpaces(s + 3)) return 0;
	long ms = parseSeconds(s);
	return ms < 0 ? SS_IDLE_DEFAULT_MS : (unsigned long)ms;
}

unsigned long SS_parseInterval(const char* setting) {
	long ms = setting ? parseSeconds(setting) : -1;
	return ms <= 0 ? SS_INTERVAL_DEFAULT_MS : (unsigned long)ms;
}

unsigned long SS_changeMs(unsigned long interval_ms) {
	unsigned long half = interval_ms / 2;
	return half > SS_CHANGE_MAX_MS ? SS_CHANGE_MAX_MS : half;
}

int SS_parseSource(const char* setting) {
	if (setting && !strcmp(setting, "screenshots")) return SS_SOURCE_SCREENSHOTS;
	if (setting && !strcmp(setting, "folder")) return SS_SOURCE_FOLDER;
	return SS_SOURCE_GAMES;
}

const char* SS_sourceName(int source) {
	if (source == SS_SOURCE_SCREENSHOTS) return "screenshots";
	if (source == SS_SOURCE_FOLDER) return "folder";
	return "games";
}

int SS_parseArt(const char* setting) {
	return setting && !strcmp(setting, "screenshot") ? SS_ART_SCREENSHOT : SS_ART_BOX;
}

int SS_parseEffect(const char* setting) {
	return setting && !strcmp(setting, "slide") ? SS_EFFECT_SLIDE : SS_EFFECT_FADE;
}

int SS_parseClock(const char* setting) {
	if (setting && !strcmp(setting, "24h")) return SS_CLOCK_24H;
	if (setting && !strcmp(setting, "12h")) return SS_CLOCK_12H;
	return SS_CLOCK_OFF;
}

const char* SS_clockName(int mode) {
	if (mode == SS_CLOCK_24H) return "24h";
	if (mode == SS_CLOCK_12H) return "12h";
	return "off";
}

void SS_formatClock(int hour, int minute, int mode, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (mode == SS_CLOCK_OFF || hour < 0 || hour > 23 || minute < 0 || minute > 59) return;
	if (mode == SS_CLOCK_12H) {
		int h = hour % 12;
		snprintf(out, size, "%d:%02d %s", h ? h : 12, minute, hour < 12 ? tr("AM") : tr("PM"));
	}
	else snprintf(out, size, "%02d:%02d", hour, minute);
}

int SS_folderPath(const char* sdcard, const char* setting, char* out, size_t size) {
	if (!size) return -1;
	out[0] = '\0';
	if (!sdcard) return -1;
	const char* s = setting ? setting : "";
	while (*s == '/') s++;
	size_t len = strlen(s);
	while (len && s[len - 1] == '/') len--;
	if (!len) {
		s = "Screensaver";
		len = strlen(s);
	}
	if (memchr(s, '\\', len)) return -1;
	// no "." or ".." part, so it stays on the card
	const char* part = s;
	const char* stop = s + len;
	while (part < stop) {
		const char* slash = memchr(part, '/', (size_t)(stop - part));
		const char* end = slash ? slash : stop;
		size_t n = (size_t)(end - part);
		if ((n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return -1;
		part = end + 1;
	}
	int written = snprintf(out, size, "%s/%.*s", sdcard, (int)len, s);
	if (written < 0 || (size_t)written >= size) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}

///////////////////////////////////////
// When it opens and closes

void SsIdle_init(SsIdle* idle, unsigned long idle_ms) {
	*idle = (SsIdle){0};
	idle->idle_ms = idle_ms;
}

void SsIdle_reset(SsIdle* idle, unsigned long now) {
	idle->last_input = now;
	idle->started = 1;
}

int SsIdle_due(SsIdle* idle, unsigned long now, int on_list, int yield) {
	if (!idle->started || !on_list || yield) {
		SsIdle_reset(idle, now);
		return 0;
	}
	if (!idle->idle_ms) return 0;
	return now - idle->last_input >= idle->idle_ms;
}

int SS_idleScreen(const char* current, const char* home) {
	if (!current) return 0;
	return !strcmp(current, "gamelist") || (home && home[0] && !strcmp(current, home));
}

int SS_chargingWins(int plugin_on, const char* charging_idle, int charging) {
	if (!plugin_on || charging != 1) return 0;
	// charging-screen's own reading: off or 0 turns it off, anything else
	// is a time (its default when it isn't one)
	const char* s = skipSpaces(charging_idle ? charging_idle : "");
	if (!strncmp(s, "off", 3) && !*skipSpaces(s + 3)) return 0;
	return parseSeconds(s) != 0;
}

int SS_pluginOn(const char* plugins_txt, const char* id) {
	if (!plugins_txt || !id || !id[0]) return 0;
	size_t len = strlen(id);
	int on = 0;
	const char* line = plugins_txt;
	while (*line) {
		const char* end = strchr(line, '\n');
		if (!end) end = line + strlen(line);
		const char* stop = end;
		while (stop > line && (stop[-1] == '\r' || stop[-1] == ' ' || stop[-1] == '\t')) stop--;
		while (line < stop && (*line == ' ' || *line == '\t')) line++;
		if ((size_t)(stop - line) > len && !strncmp(line, id, len) && line[len] == '=') {
			on = (size_t)(stop - line) == len + 3 && !strncmp(line + len + 1, "on", 2);
		}
		line = *end ? end + 1 : end;
	}
	return on;
}

int SS_parseFake(const char* text, int* level, int* charging) {
	if (!text) return 0;
	int l, c;
	char extra;
	if (sscanf(text, " %d %d %c", &l, &c, &extra) != 2) return 0;
	if (c != 0 && c != 1) return 0;
	*level = l;
	*charging = c;
	return 1;
}

unsigned int SsFilter_frame(SsFilter* f, int open, unsigned int pressed, unsigned int held, unsigned int keep,
	int* close) {
	*close = 0;
	if (open && (pressed & ~keep)) {
		*close = 1;
		f->swallowing = 1;
	}
	if (!f->swallowing) return 0;
	// this frame's releases are taken too, and then it's done
	if (!(held & ~keep)) f->swallowing = 0;
	return ~keep;
}

///////////////////////////////////////
// The images

void SS_poolClear(SsPool* pool) {
	pool->count = 0;
}

int SS_poolFind(const SsPool* pool, const char* path) {
	if (!path) return -1;
	for (int i = 0; i < pool->count; i++) {
		if (!strcmp(pool->paths[i], path)) return i;
	}
	return -1;
}

int SS_poolAdd(SsPool* pool, const char* path) {
	if (!path || !path[0] || strlen(path) >= SS_PATH_MAX || pool->count >= SS_POOL_MAX) return 0;
	if (SS_poolFind(pool, path) >= 0) return 0;
	snprintf(pool->paths[pool->count], SS_PATH_MAX, "%s", path);
	pool->bad[pool->count] = 0;
	pool->count++;
	return 1;
}

const char* SS_baseName(const char* path) {
	if (!path) return "";
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

int SS_isImage(const char* name) {
	if (!name || !name[0] || name[0] == '.' || strchr(name, '/')) return 0;
	const char* dot = strrchr(name, '.');
	if (!dot || dot == name) return 0;
	return !strcasecmp(dot, ".png") || !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")
		|| !strcasecmp(dot, ".bmp");
}

static int byName(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

int SS_scanFolder(const char* dir, SsPool* pool) {
	if (!dir) return -1;
	DIR* dh = opendir(dir);
	if (!dh) return -1;
	char** names = malloc(SS_SCAN_MAX * sizeof(char*));
	if (!names) {
		closedir(dh);
		return -1;
	}
	int count = 0;
	struct dirent* dp;
	while (count < SS_SCAN_MAX && (dp = readdir(dh)) != NULL) {
		if (!SS_isImage(dp->d_name)) continue;
		names[count] = strdup(dp->d_name);
		if (names[count]) count++;
	}
	closedir(dh);
	qsort(names, (size_t)count, sizeof(char*), byName);
	int added = 0;
	for (int i = 0; i < count; i++) {
		char path[SS_PATH_MAX];
		struct stat st;
		int n = snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		if (n > 0 && (size_t)n < sizeof(path) && stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
			added += SS_poolAdd(pool, path);
		}
		free(names[i]);
	}
	free(names);
	return added;
}

int SS_nextLine(const char** cursor, char* out, size_t size) {
	if (!size) return 0;
	const char* s = *cursor;
	while (*s) {
		const char* end = strchr(s, '\n');
		if (!end) end = s + strlen(s);
		const char* stop = end;
		while (stop > s && stop[-1] == '\r') stop--;
		const char* next = *end ? end + 1 : end;
		if (stop > s) {
			size_t n = (size_t)(stop - s);
			if (n >= size) n = size - 1;
			memcpy(out, s, n);
			out[n] = '\0';
			*cursor = next;
			return 1;
		}
		s = next;
	}
	*cursor = s;
	out[0] = '\0';
	return 0;
}

const char* SS_collectionName(const char* setting) {
	if (!setting || !setting[0] || strchr(setting, '/')) return "Favorites";
	return setting;
}

///////////////////////////////////////
// Cycling

void SsCycle_init(SsCycle* c, unsigned long interval_ms, unsigned long change_ms) {
	*c = (SsCycle){0};
	c->current = c->next = -1;
	c->interval_ms = interval_ms;
	c->change_ms = change_ms;
}

int SS_following(const SsPool* pool, int from) {
	for (int step = 1; step <= pool->count; step++) {
		int i = ((from < 0 ? -1 : from) + step) % pool->count;
		if (i == from) break;
		if (!pool->bad[i]) return i;
	}
	return -1;
}

int SS_startIndex(const SsPool* pool, const char* last) {
	int at = SS_poolFind(pool, last);
	int start = SS_following(pool, at);
	if (start < 0 && at >= 0 && !pool->bad[at]) return at;
	return start;
}

int SsCycle_step(SsCycle* c, unsigned long now, int next_ready) {
	if (!c->changing) {
		if (c->current < 0 || c->next < 0 || !next_ready || now - c->shown_at < c->interval_ms) return SS_STEP_NONE;
		c->changing = 1;
		c->change_at = now;
		return SS_STEP_START;
	}
	if (now - c->change_at < c->change_ms) return SS_STEP_MOVING;
	c->changing = 0;
	c->current = c->next;
	c->next = -1;
	c->shown_at = now;
	return SS_STEP_DONE;
}

int SsCycle_progress(const SsCycle* c, unsigned long now) {
	if (!c->changing) return 0;
	if (!c->change_ms || now - c->change_at >= c->change_ms) return 1000;
	long long p = (long long)(now - c->change_at) * 1000 / (long long)c->change_ms;
	// smoothstep: 3p^2 - 2p^3, in thousandths
	return (int)((3 * p * p * 1000 - 2 * p * p * p) / 1000000);
}

///////////////////////////////////////
// Drawing

void SS_fadeAlphas(int progress, int* out, int* in) {
	if (progress < 0) progress = 0;
	if (progress > 1000) progress = 1000;
	*in = progress * 255 / 1000;
	*out = 255 - *in;
}

int SS_slideOffset(int width, int progress) {
	if (width <= 0 || progress <= 0) return 0;
	if (progress >= 1000) return width;
	return (int)((long long)width * progress / 1000);
}

MosaicRect SS_fit(int src_w, int src_h, const MosaicRect* box) {
	MosaicRect r = { box->x, box->y, 0, 0 };
	if (src_w <= 0 || src_h <= 0 || box->w <= 0 || box->h <= 0) return r;
	long long w = box->w, h = (long long)src_h * box->w / src_w;
	if (h > box->h) {
		h = box->h;
		w = (long long)src_w * box->h / src_h;
	}
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	r.w = (int)w;
	r.h = (int)h;
	r.x = box->x + (box->w - r.w) / 2;
	r.y = box->y + (box->h - r.h) / 2;
	return r;
}

int SS_clip(const MosaicRect* dst, const MosaicRect* area, MosaicRect* src, MosaicRect* to) {
	int x0 = dst->x > area->x ? dst->x : area->x;
	int y0 = dst->y > area->y ? dst->y : area->y;
	int x1 = dst->x + dst->w < area->x + area->w ? dst->x + dst->w : area->x + area->w;
	int y1 = dst->y + dst->h < area->y + area->h ? dst->y + dst->h : area->y + area->h;
	if (x1 <= x0 || y1 <= y0) return 0;
	*to = (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
	*src = (MosaicRect){ x0 - dst->x, y0 - dst->y, x1 - x0, y1 - y0 };
	return 1;
}
