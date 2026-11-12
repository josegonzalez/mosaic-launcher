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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // dladdr
#endif

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "vp_decoder.h"
#include "vp_model.h"

#define ID "video-previews"
#define AUDIO_LINGER_MS 5000

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

static void tick(unsigned long now_tick) {
	(void)now_tick;
	if (!available) return;
	unsigned long now = nowMs();
	char line[256];
	while (VPD_takeLog(line, sizeof(line))) say("%s", line);

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
	const char* why = VPD_unavailable();
	if (why) {
		say("unavailable: %s, so no clip plays", why);
		available = 0;
		return 0;
	}
	char dir[1024];
	libDir(dir, sizeof(dir));
	VPD_init(host, ID, dir);
	available = 1;
	say("clips play %i ms after the selection settles, %s", delay_ms, with_sound ? "with sound" : "silently");
	return 0;
}

static void quit(void) {
	if (!available) return;
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
