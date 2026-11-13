// Screensaver: once the game list or home has been left alone for the idle
// setting's time, a full screen that cycles through images with a slow
// fade or slide: recent and favorite games' box art or screenshots, the
// card's Screenshots folder, or a folder of the user's own, with an
// optional dim clock.
//
// It keeps its own idle time, from input_filter, which sees every button
// on every screen: any button down, another screen showing, or the device
// waking starts the count again. It opens only over the game list or the
// home slot's screen.
//
// Any button closes it, back to the list or home, and is taken until it's
// let go, so that press doesn't also act on the screen under it. The power button is left to
// the launcher, so it still sleeps; waking closes the screen.
//
// It never holds off sleep. Stock's PWR_update counts idle time from the
// last frame with a button held (or the device charging, on HDMI or with
// auto-sleep held off), never from redraws, so the screen timeout turns
// the screen off and sleeps on time while this screen shows and animates,
// and the suspend timeout runs from there as usual.
//
// It gives way to charging-screen: while the device charges and
// charging-screen is loaded with its idle setting on, this
// screen doesn't open, so charging-screen's opens instead. Neither opens
// over the other, since both open only over the game list or home. For the tests
// only, MOSAIC_CHARGING_SCREEN_FAKE, charging-screen's test battery, is
// read in place of the battery.
//
// Images come from art_load, decoded off the main thread into the
// launcher's art cache, and each is copied once with surface_scale to fit
// the screen. It holds two copies at most, the image showing and the next
// one, and frees both when it closes. It redraws every frame only during
// a change, and otherwise only when an image or the clock's minute
// changes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "screensaver_model.h"

#define ID "screensaver"
#define SCREEN_ID "screensaver.main"
#define CHARGING_ID "charging-screen"
#define FAKE_ENV "MOSAIC_CHARGING_SCREEN_FAKE"
#define FAKE_READ_MS 200
#define CHECK_MS 1000    // how often to look at the clock and charging
#define CLOCK_ALPHA 110  // the clock is drawn this dim
#define ENTRIES_MAX 128  // games made entries for art, at most (entries can't be freed)
#define GAMES_MAX 64     // recent and favorite games looked at, at most

static const MosaicHost* host;

// Another plugin's setting, when it marks it public, else fallback.
static const char* publicSetting(const char* id, const char* key, const char* fallback) {
	return MOSAIC_HOST_HAS(host, cfg_read) ? host->cfg_read(id, key, fallback) : fallback;
}

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
static unsigned long interval_ms = SS_INTERVAL_DEFAULT_MS;
static char folder[SS_PATH_MAX];
static int favorites_on = 0, charging_on = 0;
static char collection_path[SS_PATH_MAX];

// when it opens and closes
static SsIdle idle;
static SsFilter filter;
static int showing = 0;
static int activity = 0;      // a button was down since the last tick
static int close_pressed = 0; // a button closed it
static int resumed = 0;       // the device woke since the last tick
static int refused = 0;       // open_screen was refused; logged once
static int yielding = 0;      // charging-screen's screen comes first; logged when it starts
static unsigned long last_now = 0;
static unsigned long next_check = 0;

// the battery, only to give way to charging-screen
static int charging = -1;
static const char* fake_path = NULL;
static unsigned long next_fake = 0;

// the images
static SsPool pool;
static SsCycle cycle;
static char last_shown[SS_PATH_MAX];
typedef struct Copy {
	int index; // in the pool, -1 for none
	MosaicSurface* surface;
} Copy;
static Copy shown_copy = { -1, NULL }, next_copy = { -1, NULL };

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

static char* readFile(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return NULL;
	size_t cap = 4096, len = 0;
	char* text = malloc(cap);
	while (text) {
		size_t n = fread(text + len, 1, cap - len - 1, f);
		len += n;
		if (len < cap - 1) break;
		if (cap >= 1 << 20) break;
		cap *= 2;
		char* grown = realloc(text, cap);
		if (!grown) {
			free(text);
			text = NULL;
		}
		else text = grown;
	}
	fclose(f);
	if (text) text[len] = '\0';
	return text;
}

static void readCharging(unsigned long now) {
	if (!fake_path) {
		charging = host->is_charging();
		return;
	}
	if (now < next_fake) return;
	next_fake = now + FAKE_READ_MS;
	charging = -1;
	char* text = readFile(fake_path);
	int level = -1;
	if (text && !SS_parseFake(text, &level, &charging)) charging = -1;
	free(text);
}

static int chargingWins(void) {
	return SS_chargingWins(charging_on, publicSetting(CHARGING_ID, "idle", "60"), charging);
}

///////////////////////////////////////
// building the images to cycle through

static MosaicEntry* entryFor(const char* path) {
	for (int i = 0; i < entry_count; i++) {
		if (!strcmp(entries[i].path, path)) return entries[i].entry;
	}
	if (entry_count >= ENTRIES_MAX || strlen(path) >= SS_PATH_MAX) return NULL;
	size_t len = strlen(path);
	int type = len > 4 && !strcmp(path + len - 4, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
	MosaicEntry* entry = host->entry_new(path, type, SS_baseName(path));
	if (!entry) return NULL;
	snprintf(entries[entry_count].path, SS_PATH_MAX, "%s", path);
	entries[entry_count].entry = entry;
	entry_count++;
	return entry;
}

// a game's art of the art setting's kind, when it has a file
static void addGame(const char* path) {
	struct stat st;
	if (!path || stat(path, &st) != 0) return;
	MosaicEntry* entry = entryFor(path);
	if (!entry) return;
	char file[SS_PATH_MAX];
	if (art == SS_ART_SCREENSHOT) {
		if (host->art_find(entry, MOSAIC_ART_SCREENSHOT, file, sizeof(file)) == 1) SS_poolAdd(&pool, file);
		return;
	}
	if (host->art_find(entry, MOSAIC_ART_BOX, file, sizeof(file)) == 1
		|| host->art_find(entry, MOSAIC_ART_THUMBNAIL, file, sizeof(file)) == 1) {
		SS_poolAdd(&pool, file);
	}
}

static void addGames(void) {
	int looked = 0;
	int recents = host->recents_count();
	for (int i = 0; i < recents && looked < GAMES_MAX; i++, looked++) {
		const char* path = host->recent_path(i);
		if (!path) break;
		char copy[SS_PATH_MAX];
		snprintf(copy, sizeof(copy), "%s", path);
		addGame(copy);
	}
	if (!favorites_on) return;
	char* text = readFile(collection_path);
	if (!text) return;
	const char* cursor = text;
	char line[SS_PATH_MAX];
	while (looked < GAMES_MAX && SS_nextLine(&cursor, line, sizeof(line))) {
		char path[SS_PATH_MAX];
		if (snprintf(path, sizeof(path), "%s%s", host->sdcard_path, line) >= (int)sizeof(path)) continue;
		addGame(path);
		looked++;
	}
	free(text);
}

static void buildPool(void) {
	SS_poolClear(&pool);
	if (source == SS_SOURCE_GAMES) addGames();
	else if (source == SS_SOURCE_SCREENSHOTS) {
		char dir[SS_PATH_MAX];
		snprintf(dir, sizeof(dir), "%s/Screenshots", host->sdcard_path);
		SS_scanFolder(dir, &pool);
	}
	else if (folder[0]) SS_scanFolder(folder, &pool);
}

///////////////////////////////////////
// the copies

static void freeCopy(Copy* copy) {
	if (copy->surface) host->surface_free(ID, copy->surface);
	copy->surface = NULL;
	copy->index = -1;
}

// Loads the pool's image at index into copy, fitted to the screen.
// Returns 1 once it's in, 0 while it loads, -1 when it can't be (it's
// marked bad).
static int loadCopy(Copy* copy, int index) {
	if (copy->index == index && copy->surface) return 1;
	freeCopy(copy);
	const MosaicSurface* image = NULL;
	int state = host->art_load(pool.paths[index], &image);
	if (state == MOSAIC_ART_LOADING) return 0;
	int w = 0, h = 0;
	MosaicRect screen = { 0, 0, host->screen_width(), host->screen_height() };
	MosaicRect to = { 0, 0, 0, 0 };
	if (state == MOSAIC_ART_READY && image && host->surface_size(image, &w, &h) == 0) to = SS_fit(w, h, &screen);
	MosaicSurface* scaled = to.w > 0 ? host->surface_scale(ID, image, to.w, to.h, MOSAIC_FIT_STRETCH) : NULL;
	if (!scaled) {
		pool.bad[index] = 1;
		char message[SS_PATH_MAX + 32];
		snprintf(message, sizeof(message), "couldn't load %s", SS_baseName(pool.paths[index]));
		say(message);
		return -1;
	}
	copy->index = index;
	copy->surface = scaled;
	return 1;
}

static void logShowing(void) {
	char message[SS_PATH_MAX + 64];
	snprintf(message, sizeof(message), "showing %d of %d: %s", cycle.current + 1, pool.count,
		SS_baseName(pool.paths[cycle.current]));
	say(message);
	snprintf(last_shown, sizeof(last_shown), "%s", pool.paths[cycle.current]);
}

// Keeps the image showing and the next one loaded; from tick.
static void loadImages(unsigned long now) {
	while (cycle.current >= 0 && !shown_copy.surface) {
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
	while (!next_copy.surface) {
		if (cycle.next < 0) cycle.next = SS_following(&pool, cycle.current);
		if (cycle.next < 0) return; // only the one
		int got = loadCopy(&next_copy, cycle.next);
		if (got >= 0) return;
		cycle.next = -1;
	}
}

///////////////////////////////////////
// the screen

static int screenEnter(void* data) {
	(void)data;
	showing = 1;
	close_pressed = 0;
	shown_clock[0] = '\0';
	return 0;
}

static void screenExit(void* data) {
	(void)data;
	showing = 0;
	close_pressed = 0;
	freeCopy(&shown_copy);
	freeCopy(&next_copy);
	SsIdle_reset(&idle, last_now);
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

static int screenRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	host->fill_rect(screen, &area, mosaic_color(0x000000, 255));
	int on_next = cycle.changing && next_copy.surface && next_copy.index == cycle.next;
	if (on_next) {
		int p = SsCycle_progress(&cycle, last_now);
		if (effect == SS_EFFECT_SLIDE) {
			int dx = SS_slideOffset(area.w, p);
			drawCopy(screen, &area, &shown_copy, -dx, 255);
			drawCopy(screen, &area, &next_copy, area.w - dx, 255);
		}
		else {
			int out, in;
			SS_fadeAlphas(p, &out, &in);
			drawCopy(screen, &area, &shown_copy, 0, out);
			drawCopy(screen, &area, &next_copy, 0, in);
		}
	}
	else drawCopy(screen, &area, &shown_copy, 0, 255);

	if (want_clock[0]) {
		int scale = host->screen_scale();
		if (scale < 1) scale = 1;
		int margin = 8 * scale;
		int w = host->text_width(MOSAIC_FONT_LARGE, want_clock);
		int h = host->text_height(MOSAIC_FONT_LARGE);
		int rgb = host->theme_color(MOSAIC_COLOR_MAIN);
		host->draw_text(screen, MOSAIC_FONT_LARGE, want_clock, area.x + area.w - w - margin, area.y + area.h - h - margin,
			mosaic_color(rgb < 0 ? 0xFFFFFF : (unsigned int)rgb, CLOCK_ALPHA), 0);
	}
	snprintf(shown_clock, sizeof(shown_clock), "%s", want_clock);
	return 0;
}

static int screenAnimating(void* data) {
	(void)data;
	return cycle.changing;
}

///////////////////////////////////////
// hooks

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	if (!supported) return 0;
	if (input->pressed | input->held | input->released) activity = 1;
	int shut = 0;
	*consumed = SsFilter_frame(&filter, showing, input->pressed, input->held, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER),
		&shut);
	if (shut) close_pressed = 1;
	return 0;
}

// Where it opens over: the game list or home.
static const char* homeScreen(void) {
	return MOSAIC_HOST_HAS(host, home_screen) ? host->home_screen() : NULL;
}
static int onIdleScreen(void) {
	return SS_idleScreen(host->current_screen(), homeScreen());
}

static void closeScreen(const char* why) {
	if (host->close_screen(SCREEN_ID) == 0) say(why);
}

static void openScreen(unsigned long now) {
	unsigned long waited = now - idle.last_input;
	const char* over = strcmp(host->current_screen(), "gamelist") ? "home" : "the list";
	buildPool();
	SsCycle_init(&cycle, interval_ms, SS_changeMs(interval_ms));
	cycle.current = SS_startIndex(&pool, last_shown[0] ? last_shown : NULL);
	wantedClock();
	next_check = now + CHECK_MS;
	if (host->open_screen(SCREEN_ID) != 0) {
		if (!refused++) say("couldn't open the screen; trying again after the next idle time");
		SsIdle_reset(&idle, now);
		return;
	}
	char message[SS_PATH_MAX + 96];
	if (pool.count) {
		snprintf(message, sizeof(message), "opened after %lus idle on %s: %d image%s from %s", waited / 1000, over,
			pool.count, pool.count == 1 ? "" : "s", SS_sourceName(source));
	}
	else {
		snprintf(message, sizeof(message), "opened after %lus idle on %s: no images from %s", waited / 1000, over,
			SS_sourceName(source));
	}
	say(message);
	loadImages(now);
}

static void tick(unsigned long now) {
	if (!supported) return;
	last_now = now;
	readCharging(now);
	if (activity || resumed) {
		activity = 0;
		SsIdle_reset(&idle, now);
	}

	if (showing) {
		if (resumed) closeScreen("closed: the device woke");
		else if (close_pressed) closeScreen("closed by a button");
		else {
			loadImages(now);
			int was_next = cycle.next;
			int step = SsCycle_step(&cycle, now, next_copy.surface && next_copy.index == cycle.next);
			if (step == SS_STEP_START) {
				char message[SS_PATH_MAX + 64];
				snprintf(message, sizeof(message), "%s to %d of %d", effect == SS_EFFECT_SLIDE ? "sliding" : "fading",
					was_next + 1, pool.count);
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
			}
			if (now >= next_check) {
				next_check = now + CHECK_MS;
				wantedClock();
				if (strcmp(want_clock, shown_clock)) host->request_redraw();
			}
		}
		resumed = 0;
		return;
	}
	resumed = 0;

	int yield = chargingWins();
	if (yield && !yielding) say("waiting: charging-screen's screen comes first while charging");
	yielding = yield;
	if (!SsIdle_due(&idle, now, onIdleScreen(), yield)) return;
	openScreen(now);
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
	showing = activity = close_pressed = resumed = refused = yielding = 0;
	filter = (SsFilter){0};
	last_shown[0] = '\0';
	entry_count = 0;
	if (!MOSAIC_HOST_HAS(host, open_screen) || !MOSAIC_HOST_HAS(host, hold_switch) || !MOSAIC_HOST_HAS(host, blit_alpha)
		|| !MOSAIC_HOST_HAS(host, art_get) || !MOSAIC_HOST_HAS(host, is_charging)) {
		say("unsupported: this launcher has no plugin screens, input filters or art cache");
		return 0;
	}
	fake_path = getenv(FAKE_ENV);
	if (fake_path && !fake_path[0]) fake_path = NULL;

	unsigned long idle_ms = SS_parseIdle(host->cfg_get("idle", "30"));
	source = SS_parseSource(host->cfg_get("source", "games"));
	art = SS_parseArt(host->cfg_get("art", "box"));
	effect = SS_parseEffect(host->cfg_get("effect", "fade"));
	clock_mode = SS_parseClock(host->cfg_get("clock", "off"));
	interval_ms = SS_parseInterval(host->cfg_get("interval", "10"));
	SsIdle_init(&idle, idle_ms);
	if (!idle_ms) {
		say("idle is off, so the screen never opens");
		return 0;
	}
	if (source == SS_SOURCE_FOLDER
		&& SS_folderPath(host->sdcard_path, host->cfg_get("folder", "Screensaver"), folder, sizeof(folder)) != 0) {
		say("the folder setting isn't a folder on the card, so there are no images");
	}

	// both load before it, as its optional= asks
	favorites_on = SS_pluginLoaded(host, "favorites");
	charging_on = SS_pluginLoaded(host, CHARGING_ID);
	snprintf(collection_path, sizeof(collection_path), "%s/Collections/%s.txt", host->sdcard_path,
		SS_collectionName(publicSetting("favorites", "collection", "Favorites")));

	MosaicScreenHooks hooks = {
		.size = sizeof(hooks),
		.flags = MOSAIC_SCREEN_FULL,
		.enter = screenEnter,
		.exit = screenExit,
		.input = screenInput,
		.render = screenRender,
		.animating = screenAnimating,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("couldn't register the screen");
		return 0;
	}
	supported = 1;
	char message[SS_PATH_MAX + 160];
	snprintf(message, sizeof(message), "opens after %lus idle on the list or home: %s%s, every %lus, %s, clock %s%s",
		idle_ms / 1000, SS_sourceName(source),
		source == SS_SOURCE_GAMES ? (art == SS_ART_SCREENSHOT ? " (screenshots)" : " (box art)") : "",
		interval_ms / 1000, effect == SS_EFFECT_SLIDE ? "slide" : "fade", SS_clockName(clock_mode),
		charging_on ? ", after charging-screen while charging" : "");
	say(message);
	if (source == SS_SOURCE_FOLDER && folder[0]) {
		snprintf(message, sizeof(message), "folder %s", folder);
		say(message);
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
