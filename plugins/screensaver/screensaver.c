// Screensaver: once the game list or home has been left alone for the idle
// setting's time, a full screen that cycles through images with a slow
// fade or slide: recent and favorite games' box art or screenshots, the
// card's Screenshots folder, or a folder of the user's own, with an
// optional dim clock. Favorites are the favorites entry set (set_list),
// and games a plugin's filter hides, such as the hidden plugin's, are left
// out (is_filtered).
//
// It's an ambient screen (register_ambient): the launcher keeps the idle
// time and opens it over the game list, home or a later ambient screen, in
// the user's order of ambient screens, once wants says the idle setting's
// time has passed. An ambient screen earlier in the order, such as the
// charging screen, takes over from it.
//
// Any button closes it, back to the list or home, and is taken until it's
// let go, so that press doesn't also act on the screen under it, except X
// while the music plugin has a track, which plays or pauses it
// (music.toggle) and stays. What music plays shows dim in the bottom left
// corner while it has a track. The power button is left to the launcher,
// so it still sleeps; waking closes the screen.
//
// The videos source is the games source with the games' clips: for a
// game with one, video-previews' video.play opens its own screen over this
// one and plays it, with this screen's clock and what music plays, until
// the next image, and a game without one shows its art. Without
// video-previews it's the games source. Clips play only in the images
// style, as they'd cover the logo or the clock face.
//
// The random source picks a game from the card index (index_count and
// the rest) for each image, leaving out what a plugin's filter hides, and
// shows its art with its name. Each pick's entry, made to ask art_find,
// is freed with entry_free. A alone closes the screen and launches the
// game showing (open_path), as any other button closes it.
//
// The style setting picks what's drawn: the images (the default), the
// game's wheel logo (art_find's MOSAIC_ART_LOGO, else its .media/wheel
// file), or its name without one, bouncing on black and changing game
// each interval, or an analog clock face, alone on black and drifting
// against burn-in, or over the dimmed images, as the face setting picks.
//
// It never holds off sleep. Stock's PWR_update counts idle time from the
// last frame with a button held (or the device charging, on HDMI or with
// auto-sleep held off), never from redraws, so the screen timeout turns
// the screen off and sleeps on time while this screen shows and animates,
// and the suspend timeout runs from there as usual.
//
// Images come from art_load, decoded off the main thread into the
// launcher's art cache, and each is copied once with surface_scale to fit
// the screen. It holds two copies at most, the image showing and the next
// one, and frees both when it closes. It redraws every frame only during
// a change or while the logo bounces, and otherwise only when an image or
// the clock's minute changes, or once a second with the analog face.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "screensaver_model.h"

#define ID "screensaver"
#define SCREEN_ID "screensaver.main"
#define CHECK_MS 1000    // how often to look at the clock and music
#define CLOCK_ALPHA 110  // the clock is drawn this dim
#define LINE_MAX 512     // the now playing line
#define LINE_ALPHA 200   // and its alpha, on its dark pill
#define ENTRIES_MAX 128  // games made entries for art, at most (entries can't be freed)
#define GAMES_MAX 64     // recent and favorite games looked at, at most
#define RANDOM_TRIES 16  // random picks tried for a game with what the style shows
#define MAX_DEPTH 3      // subfolders below a system folder a random game can be in
#define BOUNCE_SPEED 30  // the logo's pixels a second, times the screen's scale
#define DRIFT_SPEED 2    // the full clock face's
#define DIM_ALPHA 150    // how much the images are dimmed under the clock face
#define FACE_ALPHA 120   // the dark disc behind the face over the images

static const MosaicHost* host;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static int supported = 0;

// settings
static int source = SS_SOURCE_GAMES;
static int art = SS_ART_BOX;
static int effect = SS_EFFECT_FADE;
static int clock_mode = SS_CLOCK_OFF;
static int style = SS_STYLE_IMAGES;
static int face = SS_FACE_FULL;
static unsigned long interval_ms = SS_INTERVAL_DEFAULT_MS;
static char folder[SS_PATH_MAX];
static unsigned long idle_ms = SS_IDLE_DEFAULT_MS;
static int favorites_on = 0;

// when it opens and closes
static SsFilter filter;
static int showing = 0;
static int close_pressed = 0;  // a button closed it
static int toggle_pressed = 0; // X played or paused music
static int resumed = 0;        // the device woke since the last tick
static unsigned long opened_idle = 0; // the idle time wants opened it after
static unsigned long last_now = 0;
static unsigned long next_check = 0;

// the music plugin's now playing line, "" without a track
static char want_music[LINE_MAX], shown_music[LINE_MAX];

// the videos source: video-previews plays the image's game's clip
static int clip_due = 0;     // look for the image showing's clip at the next tick
static int clip_playing = 0; // video-previews' screen shows a clip

// the random source: the pool's places 0 and 1 hold the game showing and
// the next, with each one's type for open_path and index in the card index
static int slot_type[2], slot_index[2];
static int launch_pressed = 0;      // A alone closed it, to launch the game showing
static unsigned long next_pick = 0; // when to try again after a pick found nothing
static unsigned long long seed;

// the logo bouncing, or the full clock face drifting
static SsBounce bounce;
static unsigned long moved_at = 0;
static long shown_second = -1; // the analog face's time drawn

// the images
static SsPool pool;
static SsCycle cycle;
static char last_shown[SS_PATH_MAX];
typedef struct Copy {
	int index; // in the pool, -1 for none
	int ready; // loaded: surface, or NULL for a name in place of an image
	MosaicSurface* surface;
} Copy;
static Copy shown_copy = { -1, 0, NULL }, next_copy = { -1, 0, NULL };

// entries made for games' art, kept for the run
static struct {
	char path[SS_PATH_MAX];
	MosaicEntry* entry;
} entries[ENTRIES_MAX];
static int entry_count = 0;

static char shown_clock[16], want_clock[16];

static void say(const char* message) {
	host->log(ID, message);
}

// What music plays, from the music plugin's music.now, or "".
static void musicLine(char* out, size_t size) {
	out[0] = '\0';
	char reply[MOSAIC_CALL_MAX];
	if (!MOSAIC_HOST_HAS(host, call) || host->call("music.now", NULL, reply, sizeof(reply)) < 0) return;
	if (host->event_value(reply, "line", out, size) < 0) out[0] = '\0';
}

///////////////////////////////////////
// building the images to cycle through

static int typeOf(const char* path) {
	size_t len = strlen(path);
	return len > 4 && !strcmp(path + len - 4, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
}

static MosaicEntry* entryFor(const char* path, int type) {
	for (int i = 0; i < entry_count; i++) {
		if (!strcmp(entries[i].path, path)) return entries[i].entry;
	}
	if (entry_count >= ENTRIES_MAX || strlen(path) >= SS_PATH_MAX) return NULL;
	MosaicEntry* entry = host->entry_new(path, type, SS_baseName(path));
	if (!entry) return NULL;
	snprintf(entries[entry_count].path, SS_PATH_MAX, "%s", path);
	entries[entry_count].entry = entry;
	entry_count++;
	return entry;
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// A game's image for the style, into file: its logo for the logo style,
// else the art setting's kind. 1 when it has one. The random source's
// games get an entry each time, freed straight after, where the launcher
// has entry_free; the rest are kept, up to ENTRIES_MAX.
static int gameArt(const char* path, int type, char* file, size_t size) {
	int fresh = source == SS_SOURCE_RANDOM && MOSAIC_HOST_HAS(host, entry_free);
	MosaicEntry* entry = fresh ? host->entry_new(path, type, NULL) : entryFor(path, type);
	int found = 0;
	if (entry && style == SS_STYLE_LOGO) found = host->art_find(entry, MOSAIC_ART_LOGO, file, size) == 1;
	else if (entry && art == SS_ART_SCREENSHOT) found = host->art_find(entry, MOSAIC_ART_SCREENSHOT, file, size) == 1;
	else if (entry) {
		found = host->art_find(entry, MOSAIC_ART_BOX, file, size) == 1
			|| host->art_find(entry, MOSAIC_ART_THUMBNAIL, file, size) == 1;
	}
	if (fresh && entry) host->entry_free(entry);
	// the wheel beside the game, as nx-redux keeps it, when no plugin says
	for (int ext = 0; !found && style == SS_STYLE_LOGO && ext < SS_MEDIA_EXTS; ext++) {
		found = SS_mediaPath(path, type == MOSAIC_ENTRY_DIR, "wheel", ext, file, size) == 0 && isFile(file);
	}
	if (!found && size) file[0] = '\0';
	return found;
}

// A game's name: the card index's, as the list shows it, else its file's.
static void gameName(const char* path, int type, char* out, size_t size) {
	MosaicIndexEntry e = { .size = sizeof(e) };
	if (MOSAIC_HOST_HAS(host, index_find) && host->index_find(path, &e) >= 0 && e.name[0]) snprintf(out, size, "%s", e.name);
	else SS_gameName(path, type == MOSAIC_ENTRY_DIR, out, size);
}

// a game's art of the art setting's kind, when it has a file, or for the
// logo style its logo, else its name
static void addGame(const char* path) {
	struct stat st;
	if (!path || stat(path, &st) != 0) return;
	int type = typeOf(path);
	char file[SS_PATH_MAX];
	int found = gameArt(path, type, file, sizeof(file));
	if (style != SS_STYLE_LOGO) {
		if (found) SS_poolAddGame(&pool, file, path);
		return;
	}
	char name[SS_NAME_MAX];
	gameName(path, type, name, sizeof(name));
	if (found) SS_poolAddNamed(&pool, file, path, name, 0);
	else SS_poolAddNamed(&pool, path, path, name, 1);
}

static int filtered(const char* path) {
	return MOSAIC_HOST_HAS(host, is_filtered) && host->is_filtered(path);
}

static int looked = 0; // games looked at for this pool

static void addFavorite(const char* path, void* data) {
	(void)data;
	if (looked >= GAMES_MAX) return;
	looked++;
	if (!filtered(path)) addGame(path);
}

// recents, then the favorites entry set, leaving out what a plugin's
// filter hides
static void addGames(void) {
	looked = 0;
	int recents = host->recents_count();
	for (int i = 0; i < recents && looked < GAMES_MAX; i++, looked++) {
		const char* path = host->recent_path(i);
		if (!path) break;
		char copy[SS_PATH_MAX];
		snprintf(copy, sizeof(copy), "%s", path);
		if (!filtered(copy)) addGame(copy);
	}
	if (favorites_on && MOSAIC_HOST_HAS(host, is_filtered)) host->set_list("favorites", addFavorite, NULL);
}

// 1 when the style shows the source's images: all but the full clock face
static int showsImages(void) {
	return style != SS_STYLE_CLOCK || face == SS_FACE_OVER;
}

static void buildPool(void) {
	SS_poolClear(&pool);
	if (!showsImages() || source == SS_SOURCE_RANDOM) return; // the random source picks as it goes
	if (source == SS_SOURCE_GAMES || source == SS_SOURCE_VIDEOS) addGames();
	else if (source == SS_SOURCE_SCREENSHOTS) {
		char dir[SS_PATH_MAX];
		snprintf(dir, sizeof(dir), "%s/Screenshots", host->sdcard_path);
		SS_scanFolder(dir, &pool);
	}
	else if (folder[0]) SS_scanFolder(folder, &pool);
}

///////////////////////////////////////
// the random source

static unsigned long below(unsigned long n, void* data) {
	(void)data;
	return (unsigned long)(SS_xorshift(&seed) % n);
}

// A game in the card index as random-game takes them, and not hidden: 2
// with what the style shows, 1 without
static int randomFit(int index, void* data) {
	(void)data;
	MosaicIndexEntry e = { .size = sizeof(e) };
	if (host->index_entry(NULL, index, &e) != 0) return 0;
	if (e.kind != MOSAIC_INDEX_GAME || !e.emulator || e.depth > MAX_DEPTH) return 0;
	struct stat st;
	if ((!e.added && stat(e.path, &st) != 0) || filtered(e.path)) return 0;
	char file[SS_PATH_MAX];
	return gameArt(e.path, e.type, file, sizeof(file)) ? 2 : 1;
}

// Picks a random game into the pool's place slot, other than the one in
// place avoid (-1 for none). Returns 1 when it did.
static int pickRandom(int slot, int avoid) {
	if (!MOSAIC_HOST_HAS(host, index_entry)) return 0;
	int count = host->index_count(NULL);
	if (count <= 0) return 0;
	int i = SS_randomPick(count, avoid >= 0 ? slot_index[avoid] : -1, RANDOM_TRIES, below, NULL, randomFit, NULL);
	MosaicIndexEntry e = { .size = sizeof(e) };
	if (i < 0 || host->index_entry(NULL, i, &e) != 0) return 0;
	char file[SS_PATH_MAX];
	int found = gameArt(e.path, e.type, file, sizeof(file));
	if (!SS_poolPut(&pool, slot, found ? file : e.path, e.path, e.name, !found)) return 0;
	slot_type[slot] = e.type;
	slot_index[slot] = i;
	char message[SS_PATH_MAX + 32];
	snprintf(message, sizeof(message), "picked %s%s", e.path, found ? "" : " (its name)");
	say(message);
	return 1;
}

// The random source's game showing, which A launches, or NULL.
static const char* gameShowing(void) {
	if (source != SS_SOURCE_RANDOM || !showsImages() || cycle.current < 0 || !shown_copy.ready) return NULL;
	return pool.games[cycle.current][0] ? pool.games[cycle.current] : NULL;
}

///////////////////////////////////////
// the copies

static void freeCopy(Copy* copy) {
	if (copy->surface) host->surface_free(ID, copy->surface);
	copy->surface = NULL;
	copy->index = -1;
	copy->ready = 0;
}

// The box an image is fitted into: the screen, or for the logo style two
// fifths of its width by a third of its height.
static MosaicRect imageBox(void) {
	MosaicRect screen = { 0, 0, host->screen_width(), host->screen_height() };
	if (style == SS_STYLE_LOGO) {
		screen.w = screen.w * 2 / 5;
		screen.h /= 3;
	}
	return screen;
}

// Loads the pool's image at index into copy, fitted to the screen.
// Returns 1 once it's in, 0 while it loads, -1 when it can't be (it's
// marked bad).
static int loadCopy(Copy* copy, int index) {
	if (copy->index == index && copy->ready) return 1;
	freeCopy(copy);
	if (pool.text[index]) {
		copy->index = index;
		copy->ready = 1;
		return 1;
	}
	const MosaicSurface* image = NULL;
	int state = host->art_load(pool.paths[index], &image);
	if (state == MOSAIC_ART_LOADING) return 0;
	int w = 0, h = 0;
	MosaicRect screen = imageBox();
	MosaicRect to = { 0, 0, 0, 0 };
	if (state == MOSAIC_ART_READY && image && host->surface_size(image, &w, &h) == 0) to = SS_fit(w, h, &screen);
	MosaicSurface* scaled = to.w > 0 ? host->surface_scale(ID, image, to.w, to.h, MOSAIC_FIT_STRETCH) : NULL;
	if (!scaled) {
		char message[SS_PATH_MAX + 32];
		snprintf(message, sizeof(message), "couldn't load %s", SS_baseName(pool.paths[index]));
		say(message);
		if (pool.games[index][0] && pool.names[index][0] && (source == SS_SOURCE_RANDOM || style == SS_STYLE_LOGO)) {
			// its name in its place
			pool.text[index] = 1;
			copy->index = index;
			copy->ready = 1;
			return 1;
		}
		pool.bad[index] = 1;
		return -1;
	}
	copy->index = index;
	copy->surface = scaled;
	copy->ready = 1;
	return 1;
}

static void logShowing(void) {
	char message[SS_PATH_MAX + 64];
	snprintf(message, sizeof(message), "showing %d of %d: %s", cycle.current + 1, pool.count,
		SS_baseName(pool.paths[cycle.current]));
	say(message);
	snprintf(last_shown, sizeof(last_shown), "%s", pool.paths[cycle.current]);
}

// The random source's next game, into the place the one showing isn't in.
static void pickNext(unsigned long now) {
	if (source != SS_SOURCE_RANDOM || !showsImages() || cycle.changing || now < next_pick) return;
	if (cycle.current < 0) {
		if (pickRandom(0, -1)) cycle.current = 0;
		else next_pick = now + CHECK_MS;
	}
	else if (cycle.next < 0 && shown_copy.ready) {
		int slot = 1 - cycle.current;
		if (pickRandom(slot, cycle.current)) cycle.next = slot;
		else next_pick = now + CHECK_MS;
	}
}

// Keeps the image showing and the next one loaded; from tick.
static void loadImages(unsigned long now) {
	pickNext(now);
	while (cycle.current >= 0 && !shown_copy.ready) {
		int got = loadCopy(&shown_copy, cycle.current);
		if (got == 0) return;
		if (got == 1) {
			cycle.shown_at = now;
			logShowing();
			host->request_redraw();
			break;
		}
		cycle.current = SS_following(&pool, cycle.current);
	}
	if (cycle.current < 0 || cycle.changing) return;
	if (source == SS_SOURCE_RANDOM) {
		if (cycle.next >= 0 && !next_copy.ready && loadCopy(&next_copy, cycle.next) < 0) cycle.next = -1;
		return;
	}
	while (!next_copy.ready) {
		if (cycle.next < 0) cycle.next = SS_following(&pool, cycle.current);
		if (cycle.next < 0) return; // only the one
		int got = loadCopy(&next_copy, cycle.next);
		if (got >= 0) return;
		cycle.next = -1;
	}
}

///////////////////////////////////////
// the screen

static void wantedClock(void);

static int screenEnter(void* data) {
	(void)data;
	showing = 1;
	close_pressed = toggle_pressed = launch_pressed = 0;
	shown_clock[0] = shown_music[0] = '\0';
	clip_playing = 0;
	clip_due = source == SS_SOURCE_VIDEOS && style == SS_STYLE_IMAGES;
	moved_at = last_now;
	shown_second = -1;
	next_pick = 0;
	buildPool();
	// the logo changes game at once
	SsCycle_init(&cycle, interval_ms, style == SS_STYLE_LOGO ? 0 : SS_changeMs(interval_ms));
	cycle.current = source == SS_SOURCE_RANDOM ? -1 : SS_startIndex(&pool, last_shown[0] ? last_shown : NULL);
	wantedClock();
	musicLine(want_music, sizeof(want_music));
	next_check = last_now + CHECK_MS;
	char message[SS_PATH_MAX + 96];
	if (!showsImages()) snprintf(message, sizeof(message), "opened after %lus idle: the clock face", opened_idle / 1000);
	else if (source == SS_SOURCE_RANDOM) snprintf(message, sizeof(message), "opened after %lus idle: random games", opened_idle / 1000);
	else if (pool.count) {
		snprintf(message, sizeof(message), "opened after %lus idle: %d image%s from %s", opened_idle / 1000, pool.count,
			pool.count == 1 ? "" : "s", SS_sourceName(source));
	}
	else snprintf(message, sizeof(message), "opened after %lus idle: no images from %s", opened_idle / 1000, SS_sourceName(source));
	say(message);
	loadImages(last_now);
	return 0;
}

static void screenExit(void* data) {
	(void)data;
	showing = 0;
	close_pressed = toggle_pressed = launch_pressed = 0;
	clip_due = clip_playing = 0;
	freeCopy(&shown_copy);
	freeCopy(&next_copy);
}

// buttons are handled in input_filter, so they never reach the screen
static int screenInput(const MosaicInput* input, void* data) {
	(void)input;
	(void)data;
	return MOSAIC_SCREEN_STAY;
}

static void drawCopy(MosaicSurface* screen, const MosaicRect* area, const Copy* copy, int dx, int alpha) {
	int w = 0, h = 0;
	if (!copy->surface || host->surface_size(copy->surface, &w, &h) != 0) return;
	MosaicRect at = SS_fit(w, h, area);
	at.x += dx;
	MosaicRect from, to;
	if (!SS_clip(&at, area, &from, &to)) return;
	if (alpha >= 255) host->blit(screen, copy->surface, &from, &to);
	else if (alpha > 0) host->blit_alpha(screen, copy->surface, &from, &to, alpha);
}

static void wantedClock(void) {
	want_clock[0] = '\0';
	if (clock_mode == SS_CLOCK_OFF) return;
	time_t t = time(NULL);
	struct tm local;
	if (localtime_r(&t, &local)) SS_formatClock(local.tm_hour, local.tm_min, clock_mode, want_clock, sizeof(want_clock));
}

static MosaicColor themeColor(int index, int alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? 0xFFFFFF : (unsigned int)rgb, alpha);
}

static int scaleOf(void) {
	int scale = host->screen_scale();
	return scale < 1 ? 1 : scale;
}

// The copy the logo style draws: the next one once the change began.
static const Copy* logoCopy(void) {
	if (cycle.changing && next_copy.ready && next_copy.index == cycle.next) return &next_copy;
	return shown_copy.ready ? &shown_copy : NULL;
}

// How big a copy is drawn in the logo style: its surface, or its name in
// the large font.
static void logoSize(const Copy* copy, int* w, int* h) {
	*w = *h = 0;
	if (!copy) return;
	if (copy->surface) host->surface_size(copy->surface, w, h);
	else {
		*w = host->text_width(MOSAIC_FONT_LARGE, pool.names[copy->index]);
		*h = host->text_height(MOSAIC_FONT_LARGE);
	}
	if (*w < 0) *w = 0;
	if (*h < 0) *h = 0;
}

static void drawLogo(MosaicSurface* screen, const MosaicRect* area) {
	const Copy* copy = logoCopy();
	int w, h;
	logoSize(copy, &w, &h);
	if (!w || !h) return;
	int x = area->x + SsBounce_x(&bounce), y = area->y + SsBounce_y(&bounce);
	if (copy->surface) {
		MosaicRect at = { x, y, w, h }, from, to;
		if (SS_clip(&at, area, &from, &to)) host->blit(screen, copy->surface, &from, &to);
	}
	else host->draw_text(screen, MOSAIC_FONT_LARGE, pool.names[copy->index], x, y, themeColor(MOSAIC_COLOR_MAIN, 255), area->w);
}

// One hand: dots of its thickness along it, from the centre out.
static void drawHand(MosaicSurface* screen, int cx, int cy, int degrees, int length, int thick, MosaicColor color) {
	int step = thick / 2 > 0 ? thick / 2 : 1;
	for (int d = 0; d <= length; d += step) {
		int dx, dy;
		SS_handPoint(degrees, d, &dx, &dy);
		MosaicRect dot = { cx + dx - thick / 2, cy + dy - thick / 2, thick, thick };
		host->fill_round_rect(screen, &dot, thick / 2, color);
	}
}

// The analog face: the full one where the drift has it, else centred on a
// dark disc over the images.
static void drawFace(MosaicSurface* screen, const MosaicRect* area) {
	int r = SS_faceRadius(area->w, area->h, face);
	if (r <= 0) return;
	int scale = scaleOf();
	int cx = area->x + area->w / 2, cy = area->y + area->h / 2;
	if (face == SS_FACE_FULL) {
		cx = area->x + r + SsBounce_x(&bounce);
		cy = area->y + r + SsBounce_y(&bounce);
	}
	else {
		MosaicRect disc = { cx - r, cy - r, 2 * r, 2 * r };
		host->fill_round_rect(screen, &disc, r, mosaic_color(0x000000, FACE_ALPHA));
	}
	MosaicColor ink = themeColor(MOSAIC_COLOR_MAIN, 255);
	for (int m = 0; m < 60; m++) {
		int hour = m % 5 == 0, size = (hour ? 4 : 2) * scale, dx, dy;
		SS_handPoint(m * 6, r - size - 2 * scale, &dx, &dy);
		MosaicRect mark = { cx + dx - size / 2, cy + dy - size / 2, size, size };
		host->fill_round_rect(screen, &mark, size / 2, hour ? ink : themeColor(MOSAIC_COLOR_MAIN, 140));
	}
	time_t t = time(NULL);
	struct tm local;
	if (!localtime_r(&t, &local)) return;
	int hd, md, sd;
	SS_handAngles(local.tm_hour, local.tm_min, local.tm_sec, &hd, &md, &sd);
	drawHand(screen, cx, cy, hd, r / 2, 5 * scale, ink);
	drawHand(screen, cx, cy, md, r * 4 / 5, 3 * scale, ink);
	drawHand(screen, cx, cy, sd, r * 9 / 10, 2 * scale, themeColor(MOSAIC_COLOR_ACCENT, 255));
	MosaicRect hub = { cx - 4 * scale, cy - 4 * scale, 8 * scale, 8 * scale };
	host->fill_round_rect(screen, &hub, 4 * scale, themeColor(MOSAIC_COLOR_ACCENT, 255));
}

// The random source's game's name: in the top left corner on a dark pill,
// or large in the middle in place of art.
static void drawName(MosaicSurface* screen, const MosaicRect* area, int margin) {
	if (source != SS_SOURCE_RANDOM || cycle.current < 0 || !shown_copy.ready) return;
	const char* name = pool.names[cycle.current];
	if (!name[0]) return;
	if (pool.text[cycle.current] && style == SS_STYLE_IMAGES) {
		int w = host->text_width(MOSAIC_FONT_LARGE, name), h = host->text_height(MOSAIC_FONT_LARGE);
		if (w > area->w - 2 * margin) w = area->w - 2 * margin;
		host->draw_text(screen, MOSAIC_FONT_LARGE, name, area->x + (area->w - w) / 2, area->y + (area->h - h) / 2,
			themeColor(MOSAIC_COLOR_MAIN, 255), area->w - 2 * margin);
		return;
	}
	char cut[SS_NAME_MAX];
	int w = host->text_truncate(MOSAIC_FONT_SMALL, name, area->w - 3 * margin, cut, sizeof(cut));
	if (w <= 0) return;
	int h = host->text_height(MOSAIC_FONT_SMALL);
	MosaicRect pill = { area->x + margin / 2, area->y + margin / 2, w + margin, h + margin };
	host->fill_round_rect(screen, &pill, pill.h / 2, mosaic_color(0x000000, 200));
	host->draw_text(screen, MOSAIC_FONT_SMALL, cut, area->x + margin, area->y + margin, themeColor(MOSAIC_COLOR_MAIN, LINE_ALPHA), 0);
}

static void drawImages(MosaicSurface* screen, const MosaicRect* area) {
	const MosaicRect a = *area;
	int on_next = cycle.changing && next_copy.ready && next_copy.index == cycle.next;
	if (on_next) {
		int p = SsCycle_progress(&cycle, last_now);
		if (effect == SS_EFFECT_SLIDE) {
			int dx = SS_slideOffset(a.w, p);
			drawCopy(screen, &a, &shown_copy, -dx, 255);
			drawCopy(screen, &a, &next_copy, a.w - dx, 255);
		}
		else {
			int out, in;
			SS_fadeAlphas(p, &out, &in);
			drawCopy(screen, &a, &shown_copy, 0, out);
			drawCopy(screen, &a, &next_copy, 0, in);
		}
	}
	else drawCopy(screen, &a, &shown_copy, 0, 255);
}

static int screenRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	host->fill_rect(screen, &area, mosaic_color(0x000000, 255));
	int scale = scaleOf();
	int margin = 8 * scale;
	if (style == SS_STYLE_LOGO) drawLogo(screen, &area);
	else if (showsImages()) {
		drawImages(screen, &area);
		if (style == SS_STYLE_CLOCK) host->fill_rect(screen, &area, mosaic_color(0x000000, DIM_ALPHA));
		drawName(screen, &area, margin);
	}
	if (style == SS_STYLE_CLOCK) drawFace(screen, &area);

	int rgb = host->theme_color(MOSAIC_COLOR_MAIN);
	MosaicColor dim = mosaic_color(rgb < 0 ? 0xFFFFFF : (unsigned int)rgb, CLOCK_ALPHA);
	int clock_w = 0;
	if (want_clock[0]) {
		clock_w = host->text_width(MOSAIC_FONT_LARGE, want_clock);
		int h = host->text_height(MOSAIC_FONT_LARGE);
		host->draw_text(screen, MOSAIC_FONT_LARGE, want_clock, area.x + area.w - clock_w - margin, area.y + area.h - h - margin,
			dim, 0);
	}
	// what music plays, in the bottom left corner, left of the clock
	if (want_music[0]) {
		char cut[LINE_MAX];
		int room = area.w - 3 * margin - clock_w;
		int w = room > 0 ? host->text_truncate(MOSAIC_FONT_SMALL, want_music, room, cut, sizeof(cut)) : 0;
		if (w > 0) {
			// on a dark pill, so it reads over bright art
			int h = host->text_height(MOSAIC_FONT_SMALL);
			MosaicRect pill = { area.x + margin / 2, area.y + area.h - h - margin - margin / 2, w + margin, h + margin };
			host->fill_round_rect(screen, &pill, pill.h / 2, mosaic_color(0x000000, 200));
			host->draw_text(screen, MOSAIC_FONT_SMALL, cut, area.x + margin, area.y + area.h - h - margin,
				mosaic_color(rgb < 0 ? 0xFFFFFF : (unsigned int)rgb, LINE_ALPHA), 0);
		}
	}
	if (strcmp(want_music, shown_music)) {
		char message[LINE_MAX + 16];
		snprintf(message, sizeof(message), "music: %s", want_music[0] ? want_music : "none");
		say(message);
	}
	snprintf(shown_clock, sizeof(shown_clock), "%s", want_clock);
	snprintf(shown_music, sizeof(shown_music), "%s", want_music);
	return 0;
}

static int screenAnimating(void* data) {
	(void)data;
	return cycle.changing || (style == SS_STYLE_LOGO && logoCopy());
}

// Moves the logo on to now, or the full clock face once a second, and
// asks for a redraw when the face's second changed.
static void move(unsigned long now) {
	int sw = host->screen_width(), sh = host->screen_height();
	unsigned long ms = now - moved_at;
	if (style == SS_STYLE_LOGO) {
		int w, h;
		logoSize(logoCopy(), &w, &h);
		SsBounce_step(&bounce, ms, BOUNCE_SPEED * scaleOf(), sw - w, sh - h);
		moved_at = now;
		return;
	}
	if (style != SS_STYLE_CLOCK) return;
	long second = (long)time(NULL);
	if (second == shown_second) return;
	shown_second = second;
	if (face == SS_FACE_FULL) {
		int r = SS_faceRadius(sw, sh, face);
		SsBounce_step(&bounce, ms, DRIFT_SPEED * scaleOf(), sw - 2 * r, sh - 2 * r);
	}
	moved_at = now;
	host->request_redraw();
}

///////////////////////////////////////
// hooks

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	if (!supported) return 0;
	int shut = 0, toggled = 0;
	unsigned int toggle = showing && want_music[0] ? MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X) : 0;
	unsigned int power = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);
	int launchable = showing && gameShowing() && MOSAIC_HOST_HAS(host, open_path);
	*consumed = SsFilter_frame(&filter, showing, input->pressed, input->held, power, toggle, &shut, &toggled);
	if (shut) close_pressed = 1;
	if (shut && launchable && SS_pressedAlone(input->pressed, power, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A))) launch_pressed = 1;
	if (toggled) toggle_pressed = 1;
	return 0;
}

static int wantsToShow(unsigned long idle, void* data) {
	(void)data;
	if (!SS_wants(idle, idle_ms)) return 0;
	opened_idle = idle;
	return 1;
}

static void closeScreen(const char* why) {
	if (host->close_screen(SCREEN_ID) == 0) say(why);
}

// A on the random source: closes the screen and launches the game showing.
static void launchShowing(void) {
	const char* game = gameShowing();
	if (!game) {
		closeScreen("closed by a button");
		return;
	}
	char path[SS_PATH_MAX];
	snprintf(path, sizeof(path), "%s", game);
	int type = slot_type[cycle.current];
	char message[SS_PATH_MAX + 32];
	snprintf(message, sizeof(message), "closed by A: launching %s", path);
	closeScreen(message);
	if (!host->open_path(path, type)) {
		snprintf(message, sizeof(message), "couldn't launch %s", path);
		say(message);
	}
}

// X: plays or pauses music, staying open.
static void toggleMusic(void) {
	char reply[MOSAIC_CALL_MAX], state[16] = "";
	if (host->call("music.toggle", NULL, reply, sizeof(reply)) >= 0) host->event_value(reply, "state", state, sizeof(state));
	char message[64];
	snprintf(message, sizeof(message), "X: music %s", state[0] ? state : "didn't answer");
	say(message);
	musicLine(want_music, sizeof(want_music));
	if (strcmp(want_music, shown_music)) host->request_redraw();
}

// The videos source: video-previews plays the clip of the image showing's
// game over this screen, or shows none for an image without a clip.
static void showClip(void) {
	const char* game = cycle.current >= 0 ? pool.games[cycle.current] : "";
	if (!game[0] && !clip_playing) return;
	if (!MOSAIC_HOST_HAS(host, call) || !host->has_capability("video.play")) return;
	char args[SS_PATH_MAX + 32], reply[MOSAIC_CALL_MAX], playing[8] = "";
	snprintf(args, sizeof(args), "path=%s\nclock=%s\n", game, SS_clockName(clock_mode));
	if (host->call("video.play", args, reply, sizeof(reply)) >= 0) host->event_value(reply, "playing", playing, sizeof(playing));
	int now_playing = !strcmp(playing, "1");
	char message[SS_PATH_MAX + 64];
	if (now_playing) {
		snprintf(message, sizeof(message), "playing the clip of %s", SS_baseName(game));
		say(message);
	}
	else if (clip_playing) say("no clip for this image");
	clip_playing = now_playing;
}

static void tick(unsigned long now) {
	if (!supported) return;
	last_now = now;
	if (!showing) {
		resumed = 0;
		return;
	}
	if (resumed) closeScreen("closed: the device woke");
	else if (close_pressed && launch_pressed) launchShowing();
	else if (close_pressed) closeScreen("closed by a button");
	else {
		if (toggle_pressed) {
			toggle_pressed = 0;
			toggleMusic();
		}
		loadImages(now);
		if (clip_due && cycle.current >= 0 && shown_copy.ready) {
			clip_due = 0;
			showClip();
		}
		int was_next = cycle.next;
		int step = SsCycle_step(&cycle, now, next_copy.ready && next_copy.index == cycle.next);
		if (step == SS_STEP_START) {
			char message[SS_PATH_MAX + 64];
			const char* how = style == SS_STYLE_LOGO ? "changing" : effect == SS_EFFECT_SLIDE ? "sliding" : "fading";
			snprintf(message, sizeof(message), "%s to %d of %d", how, was_next + 1, pool.count);
			say(message);
		}
		else if (step == SS_STEP_DONE) {
			// the next copy is now the one showing
			Copy old = shown_copy;
			shown_copy = next_copy;
			next_copy = old;
			freeCopy(&next_copy);
			logShowing();
			host->request_redraw();
			if (source == SS_SOURCE_VIDEOS && style == SS_STYLE_IMAGES) clip_due = 1;
		}
		move(now);
		if (now >= next_check) {
			next_check = now + CHECK_MS;
			wantedClock();
			musicLine(want_music, sizeof(want_music));
			if (strcmp(want_clock, shown_clock) || strcmp(want_music, shown_music)) host->request_redraw();
		}
	}
	resumed = 0;
}

static void afterResume(void) {
	resumed = 1;
}

static void quit(void) {
	freeCopy(&shown_copy);
	freeCopy(&next_copy);
}

static int init(const MosaicHost* h) {
	host = h;
	static const SSWords words = { tr, NULL };
	SS_setWords(&words);
	supported = 0;
	showing = close_pressed = toggle_pressed = launch_pressed = resumed = clip_due = clip_playing = 0;
	want_music[0] = shown_music[0] = '\0';
	filter = (SsFilter){0};
	last_shown[0] = '\0';
	entry_count = 0;
	if (!MOSAIC_HOST_HAS(host, register_ambient)) {
		say("unsupported: this launcher has no ambient screens");
		return 0;
	}

	idle_ms = SS_parseIdle(host->cfg_get("idle", "30"));
	source = SS_parseSource(host->cfg_get("source", "games"));
	art = SS_parseArt(host->cfg_get("art", "box"));
	effect = SS_parseEffect(host->cfg_get("effect", "fade"));
	clock_mode = SS_parseClock(host->cfg_get("clock", "off"));
	interval_ms = SS_parseInterval(host->cfg_get("interval", "10"));
	style = SS_parseStyle(host->cfg_get("style", "images"));
	face = SS_parseFace(host->cfg_get("face", "full"));
	seed = (unsigned long long)time(NULL) ^ ((unsigned long long)(size_t)&seed << 16);
	// the logo and the full face start a fifth of the way in, going down and right
	SsBounce_init(&bounce, host->screen_width() / 5, host->screen_height() / 5, 1, 1);
	if (!idle_ms) {
		say("idle is off, so the screen never opens");
		return 0;
	}
	if (source == SS_SOURCE_FOLDER
		&& SS_folderPath(host->sdcard_path, host->cfg_get("folder", "Screensaver"), folder, sizeof(folder)) != 0) {
		say("the folder setting isn't a folder on the card, so there are no images");
	}

	// it loads before it, as its optional= asks
	favorites_on = SS_pluginLoaded(host, "favorites");

	MosaicScreenHooks hooks = {
		.size = sizeof(hooks),
		.flags = MOSAIC_SCREEN_FULL,
		.enter = screenEnter,
		.exit = screenExit,
		.input = screenInput,
		.render = screenRender,
		.animating = screenAnimating,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0 || host->register_ambient(SCREEN_ID, wantsToShow, NULL) != 0) {
		say("couldn't register the screen");
		return 0;
	}
	supported = 1;
	char message[SS_PATH_MAX + 160];
	snprintf(message, sizeof(message), "opens after %lus idle on the list or home: %s%s, every %lus, %s, clock %s",
		idle_ms / 1000, SS_sourceName(source),
		source == SS_SOURCE_GAMES || source == SS_SOURCE_VIDEOS || source == SS_SOURCE_RANDOM ? (art == SS_ART_SCREENSHOT ? " (screenshots)" : " (box art)") : "",
		interval_ms / 1000, effect == SS_EFFECT_SLIDE ? "slide" : "fade", SS_clockName(clock_mode));
	say(message);
	if (source == SS_SOURCE_FOLDER && folder[0]) {
		snprintf(message, sizeof(message), "folder %s", folder);
		say(message);
	}
	if (style != SS_STYLE_IMAGES) {
		snprintf(message, sizeof(message), "style %s%s%s", SS_styleName(style), style == SS_STYLE_CLOCK ? ", face " : "",
			style == SS_STYLE_CLOCK ? SS_faceName(face) : "");
		say(message);
	}
	if (source == SS_SOURCE_RANDOM) {
		if (MOSAIC_HOST_HAS(host, index_entry)) host->index_count(NULL); // starts reading the card, so it's ready by the first pick
		else say("this launcher has no card index, so there are no random games");
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.after_resume = afterResume,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
