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
// video-previews it's the games source.
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
#define CHECK_MS 1000    // how often to look at the clock and music
#define CLOCK_ALPHA 110  // the clock is drawn this dim
#define LINE_MAX 512     // the now playing line
#define LINE_ALPHA 200   // and its alpha, on its dark pill
#define ENTRIES_MAX 128  // games made entries for art, at most (entries can't be freed)
#define GAMES_MAX 64     // recent and favorite games looked at, at most

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

// What music plays, from the music plugin's music.now, or "".
static void musicLine(char* out, size_t size) {
	out[0] = '\0';
	char reply[MOSAIC_CALL_MAX];
	if (!MOSAIC_HOST_HAS(host, call) || host->call("music.now", NULL, reply, sizeof(reply)) < 0) return;
	if (host->event_value(reply, "line", out, size) < 0) out[0] = '\0';
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
		if (host->art_find(entry, MOSAIC_ART_SCREENSHOT, file, sizeof(file)) == 1) SS_poolAddGame(&pool, file, path);
		return;
	}
	if (host->art_find(entry, MOSAIC_ART_BOX, file, sizeof(file)) == 1
		|| host->art_find(entry, MOSAIC_ART_THUMBNAIL, file, sizeof(file)) == 1) {
		SS_poolAddGame(&pool, file, path);
	}
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

static void buildPool(void) {
	SS_poolClear(&pool);
	if (source == SS_SOURCE_GAMES || source == SS_SOURCE_VIDEOS) addGames();
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

static void wantedClock(void);

static int screenEnter(void* data) {
	(void)data;
	showing = 1;
	close_pressed = toggle_pressed = 0;
	shown_clock[0] = shown_music[0] = '\0';
	clip_playing = 0;
	clip_due = source == SS_SOURCE_VIDEOS;
	buildPool();
	SsCycle_init(&cycle, interval_ms, SS_changeMs(interval_ms));
	cycle.current = SS_startIndex(&pool, last_shown[0] ? last_shown : NULL);
	wantedClock();
	musicLine(want_music, sizeof(want_music));
	next_check = last_now + CHECK_MS;
	char message[SS_PATH_MAX + 96];
	if (pool.count) {
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
	close_pressed = toggle_pressed = 0;
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

	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int margin = 8 * scale;
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
	return cycle.changing;
}

///////////////////////////////////////
// hooks

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	if (!supported) return 0;
	int shut = 0, toggled = 0;
	unsigned int toggle = showing && want_music[0] ? MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X) : 0;
	*consumed = SsFilter_frame(&filter, showing, input->pressed, input->held, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER), toggle,
		&shut, &toggled);
	if (shut) close_pressed = 1;
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
	else if (close_pressed) closeScreen("closed by a button");
	else {
		if (toggle_pressed) {
			toggle_pressed = 0;
			toggleMusic();
		}
		loadImages(now);
		if (clip_due && cycle.current >= 0 && shown_copy.surface) {
			clip_due = 0;
			showClip();
		}
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
			if (source == SS_SOURCE_VIDEOS) clip_due = 1;
		}
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
	showing = close_pressed = toggle_pressed = resumed = clip_due = clip_playing = 0;
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
		source == SS_SOURCE_GAMES || source == SS_SOURCE_VIDEOS ? (art == SS_ART_SCREENSHOT ? " (screenshots)" : " (box art)") : "",
		interval_ms / 1000, effect == SS_EFFECT_SLIDE ? "slide" : "fade", SS_clockName(clock_mode));
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
