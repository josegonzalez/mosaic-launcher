// UI sounds: plays a short sound as the user moves, selects, goes back,
// opens or closes the quick menu or a dialog, launches a game, sees a toast
// or gets something wrong (a wrong PIN), through the launcher's sound
// device, so it plays over music and video previews.
//
// Each sound is <name>.wav (see ui_sounds_model.c for the names): from the
// theme's sounds/ folder, which theme-plus passes on as the ui-sounds.dir
// key, and otherwise from the set bundled in res/sounds/. They're read on
// the first tick, once the theme is known, and again on
// launcher.theme_changed, and scaled by the volume setting. The system's
// volume applies on top, and a muted system (a volume of 0) plays nothing.
//
// Moves, selects and backs come from the buttons, in input_filter, which
// takes nothing; the rest from the launcher's events. Other plugins can
// play one with the ui-sounds.play service.
//
// MOSAIC_UI_SOUNDS_LOG set in the environment logs each sound played, for
// the desktop tests.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "ui_sounds_model.h"

#define ID "ui-sounds"
#define SERVICE ID ".play"
#define LOG_ENV "MOSAIC_UI_SOUNDS_LOG"

static const MosaicHost* host;
static UISClip clips[UIS_COUNT];
static int volume = UIS_VOLUME_DEFAULT;
static char bundled[UIS_PATH_MAX];
static int loaded = 0;   // the clips are read for the theme in place
static int muted = 0;    // the system was muted at the last sound
static int log_plays = 0;

static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
	char line[700];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	host->log(ID, line);
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// <this plugin's folder>/res/sounds, from plugin_info
static void findBundled(void) {
	int count = host->plugins_count();
	for (int i = 0; i < count; i++) {
		MosaicPluginInfo info = { .size = sizeof(info) };
		if (host->plugin_info(i, &info) != 0 || strcmp(info.id, ID) != 0) continue;
		if ((size_t)snprintf(bundled, sizeof(bundled), "%s/sounds", info.res) >= sizeof(bundled)) bundled[0] = '\0';
		return;
	}
}

static void freeClips(void) {
	for (int i = 0; i < UIS_COUNT; i++) UIS_freeClip(&clips[i]);
}

// Reads every sound, the theme's over the bundled ones.
static void loadClips(void) {
	freeClips();
	loaded = 1;
	// plugin_info lists this plugin once every plugin has loaded
	if (!bundled[0]) findBundled();
	const char* dir = MOSAIC_HOST_HAS(host, theme_value) ? host->theme_value(ID ".dir", "") : "";
	char theme[UIS_PATH_MAX];
	snprintf(theme, sizeof(theme), "%s", dir ? dir : "");
	if (theme[0] && !UIS_themeDirOk(theme)) {
		say("the theme's %s.dir %s isn't a full path, so its sounds aren't played", ID, theme);
		theme[0] = '\0';
	}
	int from_theme = 0, from_bundled = 0;
	for (int i = 0; i < UIS_COUNT; i++) {
		char path[UIS_PATH_MAX];
		char why[96];
		int from = UIS_pickFile(theme, bundled, i, isFile, path, sizeof(path));
		if (from == UIS_FROM_THEME && UIS_loadWav(path, &clips[i], why, sizeof(why)) != 0) {
			say("the theme's %s.wav %s, so the bundled one plays", UIS_name(i), why);
			from = UIS_pickFile(NULL, bundled, i, isFile, path, sizeof(path));
		}
		else if (from == UIS_FROM_THEME) {
			from_theme++;
		}
		if (from == UIS_FROM_BUNDLED) {
			if (UIS_loadWav(path, &clips[i], why, sizeof(why)) != 0) {
				say("the bundled %s.wav %s", UIS_name(i), why);
				continue;
			}
			from_bundled++;
		}
		if (from == UIS_FROM_NONE) {
			say("there's no %s.wav, so it's silent", UIS_name(i));
			continue;
		}
		if (clips[i].cut) say("%s %s.wav is over %d seconds, so it's cut to them", from == UIS_FROM_THEME ? "the theme's" : "the bundled",
			UIS_name(i), UIS_MAX_MS / 1000);
		UIS_scale(&clips[i], volume);
	}
	if (theme[0]) say("sounds: %d from the theme's %s, %d bundled, at volume %d", from_theme, theme, from_bundled, volume);
	else say("sounds: %d bundled, at volume %d", from_bundled, volume);
}

// 1 when it played
static int play(int sound) {
	if (sound < 0 || sound >= UIS_COUNT || volume <= 0) return 0;
	if (!loaded) loadClips();
	int system = host->system_volume();
	int audible = UIS_audible(volume, system);
	if (audible == muted) {
		muted = !audible;
		say(muted ? "the system is muted, so sounds are silent" : "the system isn't muted, sounds play again");
	}
	if (!audible || !clips[sound].frames) return 0;
	const UISClip* c = &clips[sound];
	if (host->sound_play(ID, c->frames, c->count, c->rate, c->channels) != 0) return 0;
	if (log_plays) say("played %s", UIS_name(sound));
	return 1;
}

static void onEvent(const char* name, const char* payload, void* data) {
	(void)payload;
	(void)data;
	if (!strcmp(name, "launcher.theme_changed")) {
		// read again on the next tick, for the new theme
		freeClips();
		loaded = 0;
		return;
	}
	play(UIS_forEvent(name));
}

static int service(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char which[32] = "";
	host->event_value(args, "sound", which, sizeof(which));
	int sound = UIS_byName(which);
	if (sound == UIS_NONE) snprintf(out, size, "error=sound");
	else snprintf(out, size, "played=%d", play(sound));
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, system_volume)) {
		host->log(ID, "this launcher has no sound device for plugins, not starting");
		return 1;
	}
	const char* env = getenv(LOG_ENV);
	log_plays = env && env[0];
	volume = UIS_volume(host->cfg_get("volume", "50"));
	memset(clips, 0, sizeof(clips));
	loaded = 0;
	muted = 0;
	bundled[0] = '\0';
	if (volume == 0) say("the volume is 0, so sounds are off");
	static const char* const events[] = {
		"launcher.launch", "launcher.error", "launcher.menu_open", "launcher.menu_close", "launcher.toast",
		"launcher.theme_changed",
	};
	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) host->subscribe(events[i], onEvent, NULL);
	if (host->provide(SERVICE, service) != 0) say("couldn't provide %s", SERVICE);
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!loaded && volume > 0) loadClips();
}

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	(void)consumed;
	play(UIS_forInput(input->pressed, input->repeated));
	return 0;
}

static void quit(void) {
	freeClips();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.quit = quit,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
