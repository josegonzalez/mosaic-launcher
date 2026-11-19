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
	if (setting && !strcmp(setting, "videos")) return SS_SOURCE_VIDEOS;
	if (setting && !strcmp(setting, "random")) return SS_SOURCE_RANDOM;
	return SS_SOURCE_GAMES;
}

const char* SS_sourceName(int source) {
	if (source == SS_SOURCE_SCREENSHOTS) return "screenshots";
	if (source == SS_SOURCE_FOLDER) return "folder";
	if (source == SS_SOURCE_VIDEOS) return "videos";
	if (source == SS_SOURCE_RANDOM) return "random";
	return "games";
}

int SS_parseArt(const char* setting) {
	return setting && !strcmp(setting, "screenshot") ? SS_ART_SCREENSHOT : SS_ART_BOX;
}

int SS_parseStyle(const char* setting) {
	if (setting && !strcmp(setting, "logo")) return SS_STYLE_LOGO;
	if (setting && !strcmp(setting, "clock")) return SS_STYLE_CLOCK;
	return SS_STYLE_IMAGES;
}

const char* SS_styleName(int style) {
	if (style == SS_STYLE_LOGO) return "logo";
	if (style == SS_STYLE_CLOCK) return "clock";
	return "images";
}

int SS_parseFace(const char* setting) {
	return setting && !strcmp(setting, "over") ? SS_FACE_OVER : SS_FACE_FULL;
}

const char* SS_faceName(int face) {
	return face == SS_FACE_OVER ? "over" : "full";
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

int SS_wants(unsigned long idle_ms, unsigned long setting_ms) {
	return setting_ms > 0 && idle_ms >= setting_ms;
}

int SS_pluginLoaded(const MosaicHost* host, const char* id) {
	return host && id && id[0] && MOSAIC_HOST_HAS(host, plugin_loaded) && host->plugin_loaded(id);
}

unsigned int SsFilter_frame(SsFilter* f, int open, unsigned int pressed, unsigned int held, unsigned int keep,
	unsigned int toggle, int* close, int* toggled) {
	*close = 0;
	*toggled = 0;
	if (open && (pressed & ~keep)) {
		if (toggle && (pressed & ~keep) == toggle) *toggled = 1;
		else *close = 1;
		f->swallowing = 1;
	}
	if (!f->swallowing) return 0;
	// this frame's releases are taken too, and then it's done
	if (!(held & ~keep)) f->swallowing = 0;
	return ~keep;
}

int SS_pressedAlone(unsigned int pressed, unsigned int keep, unsigned int button) {
	return button && (pressed & ~keep) == button;
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
	return SS_poolAddGame(pool, path, NULL);
}

int SS_poolAddGame(SsPool* pool, const char* path, const char* game) {
	return SS_poolAddNamed(pool, path, game, NULL, 0);
}

int SS_poolAddNamed(SsPool* pool, const char* path, const char* game, const char* name, int text) {
	if (!path || !path[0] || strlen(path) >= SS_PATH_MAX || pool->count >= SS_POOL_MAX) return 0;
	if (game && strlen(game) >= SS_PATH_MAX) return 0;
	if (SS_poolFind(pool, path) >= 0) return 0;
	pool->count++;
	return SS_poolPut(pool, pool->count - 1, path, game, name, text);
}

int SS_poolPut(SsPool* pool, int index, const char* path, const char* game, const char* name, int text) {
	if (index < 0 || index >= SS_POOL_MAX || !path || strlen(path) >= SS_PATH_MAX) return 0;
	if (game && strlen(game) >= SS_PATH_MAX) return 0;
	snprintf(pool->paths[index], SS_PATH_MAX, "%s", path);
	snprintf(pool->games[index], SS_PATH_MAX, "%s", game ? game : "");
	snprintf(pool->names[index], SS_NAME_MAX, "%s", name ? name : "");
	pool->text[index] = text ? 1 : 0;
	pool->bad[index] = 0;
	if (pool->count <= index) pool->count = index + 1;
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

void SS_gameName(const char* path, int is_dir, char* out, size_t size) {
	if (!size) return;
	const char* name = SS_baseName(path);
	size_t len = strlen(name);
	const char* dot = is_dir ? NULL : strrchr(name, '.');
	if (dot && dot != name) len = (size_t)(dot - name);
	snprintf(out, size, "%.*s", (int)len, name);
}

int SS_mediaPath(const char* game, int is_dir, const char* kind, int ext, char* out, size_t size) {
	static const char* const exts[SS_MEDIA_EXTS] = { ".png", ".jpg", ".jpeg" };
	if (!size) return -1;
	out[0] = '\0';
	if (!game || !kind || ext < 0 || ext >= SS_MEDIA_EXTS) return -1;
	const char* slash = strrchr(game, '/');
	if (!slash) return -1;
	char name[SS_PATH_MAX];
	SS_gameName(game, is_dir, name, sizeof(name));
	if (!name[0]) return -1;
	int n = snprintf(out, size, "%.*s/.media/%s%s%s%s", (int)(slash - game), game, kind, kind[0] ? "/" : "", name,
		exts[ext]);
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return -1;
	}
	return 0;
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
// The random source

int SS_randomPick(int count, int avoid, int tries, SsRandom random, void* random_data, SsAccept accept,
		void* accept_data) {
	if (count <= 0 || !random || !accept) return -1;
	if (count == 1) avoid = -1;
	int fallback = -1;
	for (int t = 0; t < tries; t++) {
		int i = (int)random((unsigned long)count, random_data);
		if (i < 0 || i >= count || i == avoid) continue;
		int fit = accept(i, accept_data);
		if (fit >= 2) return i;
		if (fit == 1 && fallback < 0) fallback = i;
	}
	if (fallback >= 0) return fallback;
	int start = (int)random((unsigned long)count, random_data);
	if (start < 0 || start >= count) start = 0;
	for (int step = 0; step < count; step++) {
		int i = (start + step) % count;
		if (i != avoid && accept(i, accept_data) > 0) return i;
	}
	return avoid >= 0 && avoid < count && accept(avoid, accept_data) > 0 ? avoid : -1;
}

unsigned long long SS_xorshift(unsigned long long* state) {
	unsigned long long x = *state ? *state : 0x9E3779B97F4A7C15ULL;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*state = x;
	return x;
}

///////////////////////////////////////
// Bouncing and drifting

void SsBounce_init(SsBounce* b, int x, int y, int dx, int dy) {
	b->x = (long)x * 1000;
	b->y = (long)y * 1000;
	b->dx = dx < 0 ? -1 : 1;
	b->dy = dy < 0 ? -1 : 1;
}

// one axis: moves *pos by dist (thousandths) the way *dir goes, inside 0
// to range (thousandths), and returns the edges it turned at
static int bounceAxis(long* pos, int* dir, long long dist, long range) {
	if (range <= 0) {
		*pos = 0;
		return 0;
	}
	if (*pos < 0) *pos = 0;
	if (*pos > range) *pos = range;
	int turns = 0;
	// whole trips there and back change nothing but the count
	long long trip = 2LL * range;
	turns += (int)(dist / trip) * 2;
	long long p = *pos + (long long)*dir * (dist % trip);
	while (p < 0 || p > range) {
		if (p < 0) {
			p = -p;
			*dir = 1;
		}
		else {
			p = 2LL * range - p;
			*dir = -1;
		}
		turns++;
	}
	*pos = (long)p;
	return turns;
}

int SsBounce_step(SsBounce* b, unsigned long ms, int speed, int range_w, int range_h) {
	long long dist = speed > 0 ? (long long)speed * (long long)ms : 0; // pixels a second for ms, in thousandths
	int turns = bounceAxis(&b->x, &b->dx, dist, (long)range_w * 1000);
	return turns + bounceAxis(&b->y, &b->dy, dist, (long)range_h * 1000);
}

int SsBounce_x(const SsBounce* b) {
	return (int)(b->x / 1000);
}

int SsBounce_y(const SsBounce* b) {
	return (int)(b->y / 1000);
}

///////////////////////////////////////
// The analog clock

void SS_handAngles(int hour, int minute, int second, int* hour_deg, int* minute_deg, int* second_deg) {
	hour = ((hour % 12) + 12) % 12;
	minute = minute < 0 ? 0 : minute % 60;
	second = second < 0 ? 0 : second % 60;
	*hour_deg = hour * 30 + minute / 2;
	*minute_deg = minute * 6 + second / 10;
	*second_deg = second * 6;
}

// sin of 0 to 90 degrees, in ten thousandths
static const short sines[91] = {
	0, 175, 349, 523, 698, 872, 1045, 1219, 1392, 1564, 1736, 1908, 2079, 2250, 2419, 2588, 2756, 2924, 3090,
	3256, 3420, 3584, 3746, 3907, 4067, 4226, 4384, 4540, 4695, 4848, 5000, 5150, 5299, 5446, 5592, 5736, 5878,
	6018, 6157, 6293, 6428, 6561, 6691, 6820, 6947, 7071, 7193, 7314, 7431, 7547, 7660, 7771, 7880, 7986, 8090,
	8192, 8290, 8387, 8480, 8572, 8660, 8746, 8829, 8910, 8988, 9063, 9135, 9205, 9272, 9336, 9397, 9455, 9511,
	9563, 9613, 9659, 9703, 9744, 9781, 9816, 9848, 9877, 9903, 9925, 9945, 9962, 9976, 9986, 9994, 9998, 10000,
};

static int sine(int degrees) {
	degrees = ((degrees % 360) + 360) % 360;
	if (degrees <= 90) return sines[degrees];
	if (degrees <= 180) return sines[180 - degrees];
	if (degrees <= 270) return -sines[degrees - 180];
	return -sines[360 - degrees];
}

static int rounded(long long n) {
	return (int)(n >= 0 ? (n + 5000) / 10000 : -((-n + 5000) / 10000));
}

void SS_handPoint(int degrees, int length, int* dx, int* dy) {
	*dx = rounded((long long)length * sine(degrees));
	*dy = -rounded((long long)length * sine(degrees + 90));
}

int SS_faceRadius(int w, int h, int face) {
	int side = w < h ? w : h;
	if (side <= 0) return 0;
	return face == SS_FACE_OVER ? side * 9 / 20 : side * 2 / 5;
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
