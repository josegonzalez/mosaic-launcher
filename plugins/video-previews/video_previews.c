// Video previews: once the game list's selection has rested on a game for
// the delay setting's seconds (2 by default), its clip from
// <rom dir>/.videos/<rom name>.mp4 plays in place of its thumbnail, with
// its sound, looping until the selection moves.
//
// ffmpeg is loaded with dlopen the first time a clip is due, from the
// plugin's own lib folder on devices (a trimmed LGPL build, see
// ffmpeg/build.sh) or the system's on desktop. A decoder thread of the
// plugin's own reads the clip, converts its pictures to RGBA and queues
// its sound; tick shows each new picture with surface_write and
// art_replace, which redraws only the thumbnail on the built-in list, or
// under a plugin layout the layout's area while the layout draws it in its
// own art spot. Where the launcher shows no thumbnail (off the game list,
// with Show game art off) art_replace refuses, and the plugin tries again
// later, waiting longer each time.
//
// The audio device stays open on silence between clips, as closing it
// clicks on some devices, and is closed after a few seconds with no clip.
// Where it can't open, such as beside stock's own device on hardware,
// clips play silently.
//
// It's a source for the screensaver too: the screensaver's videos source
// calls video.play with a game, and the game's clip plays on this plugin's
// own full screen, video-previews.saver, opened in place over the
// screensaver's (MOSAIC_SCREEN_INSTANT, so it doesn't slide),
// with the screensaver's clock and what music plays drawn over it, until
// the screensaver moves on to an image without a clip or closes, which
// closes this screen too. With Sound on, it pauses music while the clip
// plays and plays it on after, when it was playing before. For the tests
// only, MOSAIC_VIDEO_PREVIEWS_FAKE plays a plain grey picture in place of
// each of the screensaver's clips, without ffmpeg.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // dladdr
#endif

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "vp_decoder.h"
#include "vp_model.h"

#define ID "video-previews"
#define SAVER_ID ID ".saver"
#define AUDIO_LINGER_MS 5000
#define SAVER_CHECK_MS 1000 // how often the screensaver's clip looks at the clock and music
#define SAVER_ALPHA 110     // the clock and music are drawn this dim, as on the screensaver
#define FAKE_ENV "MOSAIC_VIDEO_PREVIEWS_FAKE"
#define LINE_ALPHA 200 // music's line, on its dark pill, as on the screensaver

static const MosaicHost* host;
static int available = 0;
static int delay_ms = VP_DELAY_MS_DEFAULT;
static int with_sound = 1;

static VpArm arm;
static int playing = 0;      // a clip was asked for and hasn't stopped
static int shown_frames = 0; // frames shown of the clip playing
static MosaicSurface* surface = NULL;
static int surface_w = 0, surface_h = 0;
static int audio_open = 0;
static unsigned long audio_idle_since = 0;
static int audio_failed_logged = 0;

// the screensaver's clips
static int fake = 0;          // the screensaver's clips are test-only frames
static int saver_open = 0;    // video-previews.saver shows
static int saver_playing = 0; // a clip plays on it
static int saver_rearm = 0;   // the list's preview was due or playing when it opened
static int saver_frames = 0;
static int saver_clock_mode = VP_CLOCK_OFF;
static char saver_clip[1024];
static char saver_clock[32], saver_shown_clock[32];
static char saver_music[512], saver_shown_music[512];
static unsigned long next_saver_check = 0;
static VpMusic music;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[600];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static unsigned long nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000);
}

static int isFile(const char* path) {
	struct stat st;
	return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Where ffmpeg's libraries are: the system's on desktop, from pkg-config at
// build time, else the folder this plugin.so is in.
static void libDir(char* out, size_t size) {
#ifdef VP_FFMPEG_LIBDIR
	snprintf(out, size, "%s", VP_FFMPEG_LIBDIR);
#else
	Dl_info info;
	if (!dladdr((const void*)&libDir, &info) || !info.dli_fname || !VP_dirOf(info.dli_fname, out, size)) out[0] = '\0';
#endif
}

// Stops the clip and gives the thumbnail back. With wait, until the
// decoder has let go of it.
static void stop(int wait) {
	if (playing || VPD_busy()) VPD_stop(wait);
	if (playing) {
		host->art_replace(ID, NULL);
		audio_idle_since = nowMs();
		say("stopped %s after showing %i frames", arm.rom, shown_frames);
	}
	playing = 0;
}

// The selection moved or settled: whatever played was for another game.
static void forget(void) {
	stop(0);
	VP_disarm(&arm);
}

static int selectionChanged(const MosaicEntry* entry) {
	const char* path = entry ? host->entry_path(entry) : NULL;
	if (path && VP_isFor(&arm, path) && (playing || arm.armed)) return 0;
	forget();
	if (!entry || host->entry_type(entry) != MOSAIC_ENTRY_ROM) return 0;
	char clip[1024];
	if (!VP_clipPath(path, clip, sizeof(clip)) || !isFile(clip)) return 0;
	VP_arm(&arm, path, clip, nowMs(), (unsigned long)delay_ms);
	return 0;
}

// Shows the decoder's newest frame, if there's one.
static void showFrame(unsigned long now) {
	VpFrame frame;
	if (!VPD_lockFrame(&frame)) return;
	if (!surface || frame.w != surface_w || frame.h != surface_h) {
		if (surface) host->surface_free(ID, surface);
		surface = host->surface_new(ID, frame.w, frame.h);
		surface_w = surface ? frame.w : 0;
		surface_h = surface ? frame.h : 0;
	}
	int written = surface ? host->surface_write(ID, surface, NULL, frame.rgba, frame.w * 4) : -1;
	VPD_unlockFrame();
	int shown = written == 0 && host->art_replace(ID, surface) == 0;
	if (frame.first) VPD_verdict(shown);
	if (shown) {
		shown_frames++;
		return;
	}
	// no thumbnail to stand in for here: try again later, for as long as
	// the selection stays
	say("art_replace refused %s; trying again in %lu ms", arm.rom, arm.retry_ms);
	VPD_stop(0);
	playing = 0;
	audio_idle_since = now;
	VP_retry(&arm, now, (unsigned long)delay_ms);
}

static void answerAudio(unsigned long now) {
	int rate = 0, channels = 0;
	if (!VPD_audioAsk(&rate, &channels)) return;
	int ok = host->audio_open(ID, rate, channels) == 0;
	audio_open = ok;
	audio_idle_since = now;
	if (!ok && !audio_failed_logged) {
		say("can't open the audio device at %i Hz; playing without sound", rate);
		audio_failed_logged = 1;
	}
	VPD_audioAnswer(ok);
}

///////////////////////////////////////
// the screensaver's clips

static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static int callFor(const char* name, const char* key, char* value, size_t size) {
	char reply[MOSAIC_CALL_MAX];
	value[0] = '\0';
	if (!MOSAIC_HOST_HAS(host, call) || host->call(name, NULL, reply, sizeof(reply)) < 0) return -1;
	return host->event_value(reply, key, value, size) < 0 ? -1 : 0;
}

// what music plays, and the clock, as the screensaver shows them
static void saverWanted(void) {
	callFor("music.now", "line", saver_music, sizeof(saver_music));
	saver_clock[0] = '\0';
	time_t t = time(NULL);
	struct tm local;
	if (saver_clock_mode != VP_CLOCK_OFF && localtime_r(&t, &local)) {
		VP_formatClock(local.tm_hour, local.tm_min, saver_clock_mode, tr("AM"), tr("PM"), saver_clock, sizeof(saver_clock));
	}
}

static void pauseMusic(void) {
	char paused[8];
	if (callFor("music.pause", "paused", paused, sizeof(paused)) != 0 || strcmp(paused, "1")) return;
	VpMusic_paused(&music, 1);
	say("paused music for the clip");
}

static void playMusicOn(void) {
	if (!VpMusic_resume(&music)) return;
	char state[16];
	callFor("music.play", "state", state, sizeof(state));
	say("music plays on: %s", state[0] ? state : "no answer");
}

// Plays clip on the screen, replacing the one playing.
static void saverPlay(const char* clip) {
	snprintf(saver_clip, sizeof(saver_clip), "%s", clip);
	saver_frames = 0;
	saver_playing = 1;
	if (with_sound) pauseMusic();
	const char* name = strrchr(clip, '/');
	name = name ? name + 1 : clip;
	if (!fake) {
		VPD_request(clip, with_sound);
		say("playing %s for the screensaver", name);
		return;
	}
	// the test-only frames: a grey picture, without decoding
	if (!surface) {
		surface = host->surface_new(ID, 64, 48);
		surface_w = surface ? 64 : 0;
		surface_h = surface ? 48 : 0;
	}
	if (surface) {
		MosaicRect all = { 0, 0, 64, 48 };
		host->fill_rect(surface, &all, mosaic_color(0x808080, 255));
	}
	say("playing %s for the screensaver as test frames", name);
	host->request_redraw();
}

static int saverEnter(void* data) {
	(void)data;
	saver_open = 1;
	saver_playing = 0;
	saver_shown_music[0] = saver_shown_clock[0] = '\0';
	return 0;
}

static void saverExit(void* data) {
	(void)data;
	if (saver_playing && !fake) VPD_stop(1);
	if (saver_playing) say("stopped %s after showing %i frames", saver_clip, saver_frames);
	saver_open = saver_playing = 0;
	audio_idle_since = nowMs();
	playMusicOn();
	// the list's preview, again after the delay
	if (saver_rearm && arm.rom[0]) VP_arm(&arm, arm.rom, arm.clip, nowMs(), (unsigned long)delay_ms);
	saver_rearm = 0;
}

static int saverInput(const MosaicInput* input, void* data) {
	(void)input;
	(void)data;
	return MOSAIC_SCREEN_STAY; // the screensaver under it takes the buttons
}

static int saverRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	host->fill_rect(screen, &area, mosaic_color(0x000000, 255));
	if (surface && saver_playing && surface_w > 0 && surface_h > 0) {
		// fitted in the screen, keeping its shape
		MosaicRect to = area;
		if ((long)surface_w * area.h > (long)surface_h * area.w) to.h = (int)((long)surface_h * area.w / surface_w);
		else to.w = (int)((long)surface_w * area.h / surface_h);
		to.x = area.x + (area.w - to.w) / 2;
		to.y = area.y + (area.h - to.h) / 2;
		host->blit(screen, surface, NULL, &to);
	}
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int margin = 8 * scale;
	int rgb = host->theme_color(MOSAIC_COLOR_MAIN);
	MosaicColor dim = mosaic_color(rgb < 0 ? 0xFFFFFF : (unsigned int)rgb, SAVER_ALPHA);
	int clock_w = 0;
	if (saver_clock[0]) {
		clock_w = host->text_width(MOSAIC_FONT_LARGE, saver_clock);
		host->draw_text(screen, MOSAIC_FONT_LARGE, saver_clock, area.x + area.w - clock_w - margin,
			area.y + area.h - host->text_height(MOSAIC_FONT_LARGE) - margin, dim, 0);
	}
	if (saver_music[0]) {
		char cut[sizeof(saver_music)];
		int room = area.w - 3 * margin - clock_w;
		int w = room > 0 ? host->text_truncate(MOSAIC_FONT_SMALL, saver_music, room, cut, sizeof(cut)) : 0;
		if (w > 0) {
			// on a dark pill, as the screensaver draws it
			int h = host->text_height(MOSAIC_FONT_SMALL);
			MosaicRect pill = { area.x + margin / 2, area.y + area.h - h - margin - margin / 2, w + margin, h + margin };
			host->fill_round_rect(screen, &pill, pill.h / 2, mosaic_color(0x000000, 200));
			host->draw_text(screen, MOSAIC_FONT_SMALL, cut, area.x + margin, area.y + area.h - h - margin,
				mosaic_color(rgb < 0 ? 0xFFFFFF : (unsigned int)rgb, LINE_ALPHA), 0);
		}
	}
	snprintf(saver_shown_music, sizeof(saver_shown_music), "%s", saver_music);
	snprintf(saver_shown_clock, sizeof(saver_shown_clock), "%s", saver_clock);
	return 0;
}

static int saverAnimating(void* data) {
	(void)data;
	return 0;
}

// video.play, for the screensaver: path=<game> plays its clip on this
// plugin's screen over the screensaver, with clock=<off, 24h or 12h> and
// what music plays drawn over it, and answers playing=1; a game without a
// clip, or none, closes the screen and answers playing=0.
static int provideSaver(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char game[1024] = "", mode[8] = "", clip[1024] = "";
	host->event_value(args, "path", game, sizeof(game));
	host->event_value(args, "clock", mode, sizeof(mode));
	int has = game[0] && VP_clipPath(game, clip, sizeof(clip)) && isFile(clip);
	if (!has) {
		if (saver_open) host->close_screen(SAVER_ID);
		snprintf(out, size, "playing=0\n");
		return 0;
	}
	saver_clock_mode = VP_parseClock(mode);
	if (!saver_open) {
		// the list's preview stops while the screensaver plays
		// keeping the game it's for, to arm it again after
		saver_rearm = playing || arm.armed;
		stop(1);
		arm.armed = 0;
		saverWanted();
		if (host->open_screen(SAVER_ID) != 0) {
			say("couldn't open the screen for %s", game);
			if (saver_rearm && arm.rom[0]) VP_arm(&arm, arm.rom, arm.clip, nowMs(), (unsigned long)delay_ms);
			saver_rearm = 0;
			snprintf(out, size, "playing=0\n");
			return 0;
		}
	}
	saverPlay(clip);
	next_saver_check = nowMs() + SAVER_CHECK_MS;
	snprintf(out, size, "playing=1\n");
	return 0;
}

// Each frame while the screen shows: the clip's new frames, and once a
// second the clock and music.
static void saverTick(unsigned long now) {
	if (!fake && saver_playing) {
		answerAudio(now);
		VpFrame frame;
		if (VPD_lockFrame(&frame)) {
			if (!surface || frame.w != surface_w || frame.h != surface_h) {
				if (surface) host->surface_free(ID, surface);
				surface = host->surface_new(ID, frame.w, frame.h);
				surface_w = surface ? frame.w : 0;
				surface_h = surface ? frame.h : 0;
			}
			int written = surface ? host->surface_write(ID, surface, NULL, frame.rgba, frame.w * 4) : -1;
			VPD_unlockFrame();
			if (frame.first) VPD_verdict(written == 0);
			if (written == 0) {
				saver_frames++;
				host->request_redraw();
			}
		}
		if (!VPD_busy()) {
			// the clip couldn't play: the screensaver's image shows
			say("can't play %s for the screensaver", saver_clip);
			saver_playing = 0;
			host->close_screen(SAVER_ID);
			return;
		}
	}
	if (now < next_saver_check) return;
	next_saver_check = now + SAVER_CHECK_MS;
	char state[16];
	if (callFor("music.now", "state", state, sizeof(state)) == 0) VpMusic_seen(&music, state);
	saverWanted();
	if (strcmp(saver_music, saver_shown_music) || strcmp(saver_clock, saver_shown_clock)) host->request_redraw();
}


static void tick(unsigned long now_tick) {
	(void)now_tick;
	unsigned long now = nowMs();
	if (!available) {
		if (saver_open) saverTick(now);
		return;
	}
	char line[256];
	while (VPD_takeLog(line, sizeof(line))) say("%s", line);
	if (saver_open) {
		saverTick(now);
		return;
	}

	// the selection moved: selection_changed comes once it settles, but the
	// clip stops now
	if (playing || arm.armed) {
		const MosaicEntry* selected = host->selected_entry();
		if (!selected || !VP_isFor(&arm, host->entry_path(selected))) forget();
	}

	if (VP_due(&arm, now)) {
		VPD_request(arm.clip, with_sound);
		playing = 1;
		shown_frames = 0;
	}
	if (playing) {
		answerAudio(now);
		showFrame(now);
		if (playing && !VPD_busy()) {
			// the clip couldn't play, or stopped by itself
			playing = 0;
			host->art_replace(ID, NULL);
			audio_idle_since = now;
		}
	}
	if (!playing && audio_open && (long)(now - audio_idle_since) >= AUDIO_LINGER_MS) {
		host->audio_close(ID);
		audio_open = 0;
	}
}

static void beforeSleep(void) {
	// music doesn't play on after a sleep
	VpMusic_forget(&music);
	if (saver_playing && !fake) VPD_stop(1);
	saver_playing = 0;
	if (!available) return;
	int was = playing;
	stop(1);
	audio_open = 0; // the launcher closes the device itself
	// play it again after waking, if the selection stays
	if (was && arm.rom[0]) VP_arm(&arm, arm.rom, arm.clip, nowMs(), (unsigned long)delay_ms);
}

static void afterResume(void) {
	if (!available) return;
	audio_open = 0;
	if (arm.armed) arm.due = nowMs() + (unsigned long)delay_ms;
}

static int preLaunch(const MosaicLaunch* launch) {
	(void)launch;
	if (available) {
		stop(1);
		VP_disarm(&arm);
		audio_open = 0; // the launcher closes the device itself
	}
	return MOSAIC_LAUNCH_CONTINUE;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, selection_settle_ms) || !MOSAIC_HOST_HAS(host, surface_write)
		|| !MOSAIC_HOST_HAS(host, art_replace) || !MOSAIC_HOST_HAS(host, audio_open)) {
		say("unsupported: this launcher can't show a plugin's pictures in place of art, or play its sound");
		return 1;
	}
	delay_ms = VP_parseDelayMs(host->cfg_get("delay", "2"), VP_DELAY_MS_DEFAULT);
	with_sound = VP_parseToggle(host->cfg_get("sound", "on"), 1);
	VP_disarm(&arm);
	playing = 0;
	audio_open = 0;
	audio_failed_logged = 0;
	saver_open = saver_playing = saver_rearm = 0;
	music = (VpMusic){0};
	const char* fake_env = getenv(FAKE_ENV);
	fake = fake_env && fake_env[0];
	if (fake) say("the screensaver's clips are test frames");
	const char* why = VPD_unavailable();
	if (why) {
		available = 0;
		say("unavailable: %s, so no clip plays", why);
		if (!fake) return 0;
	}
	else {
		char dir[1024];
		libDir(dir, sizeof(dir));
		VPD_init(host, ID, dir);
		available = 1;
		say("clips play %i ms after the selection settles, %s", delay_ms, with_sound ? "with sound" : "silently");
	}
	// a source for the screensaver
	MosaicScreenHooks hooks = {
		.size = sizeof(hooks),
		// in place over the screensaver, with no slide
		.flags = MOSAIC_SCREEN_FULL | MOSAIC_SCREEN_INSTANT,
		.enter = saverEnter,
		.exit = saverExit,
		.input = saverInput,
		.render = saverRender,
		.animating = saverAnimating,
	};
	if (!MOSAIC_HOST_HAS(host, call) || host->register_screen(SAVER_ID, &hooks) != 0
		|| host->provide("video.play", provideSaver) != 0) {
		say("couldn't be the screensaver's source");
	}
	return 0;
}

static void quit(void) {
	fake = 0;
	if (!available) {
		if (surface) host->surface_free(ID, surface);
		surface = NULL;
		surface_w = surface_h = 0;
		return;
	}
	VPD_shutdown();
	playing = 0;
	if (surface) host->surface_free(ID, surface);
	surface = NULL;
	surface_w = surface_h = 0;
	if (audio_open) host->audio_close(ID);
	audio_open = 0;
	available = 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.before_sleep = beforeSleep,
	.after_resume = afterResume,
	.pre_launch = preLaunch,
	.selection_changed = selectionChanged,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
