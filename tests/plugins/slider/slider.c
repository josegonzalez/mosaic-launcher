// A plugin that owns the transition or launch slot, logging the
// transitions it's asked about and the frames it draws. It adds a
// <sd>/Slider folder holding an Inner folder to the root, and from tick
// opens Slider and then Inner, so the launcher pushes twice, and then
// launches the pak its pak setting names, if any. Its custom style slides
// the old frame out to the left and fades the new one in with blit_alpha,
// and it reads back what it drew, and what it was lent, along the way.
//
// Settings: style (custom, the default, off, stock, bad for an invalid
// style, or noframe for custom without a transition_frame), duration (ms,
// 200 by default), ease (a MosaicEase number, linear by default), fail
// (frame: transition_frame fails on its second frame) and pak.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "slider"

static const MosaicHost* host;
static char folder[512];
static char inner[600];
static const char* style = "custom";
static const char* fail = "";
static const char* pak = "";
static int duration = 200;
static int ease = MOSAIC_EASE_LINEAR;
static MosaicSurface* red = NULL;
static int ticks = 0;
static int frames = 0;
static int backwards = 0;
static float last_progress = 0.0f;
static long began = 0;
static const MosaicSurface* old_kept = NULL; // the frames, to try after the transition
static const MosaicSurface* new_kept = NULL;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static long ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static const char* kindName(int kind) {
	switch (kind) {
		case MOSAIC_TRANSITION_PUSH: return "push";
		case MOSAIC_TRANSITION_POP: return "pop";
		case MOSAIC_TRANSITION_SCREEN: return "screen";
		case MOSAIC_TRANSITION_LAUNCH: return "launch";
		default: return "unknown";
	}
}

// "RRGGBBAA" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[4][12];
	static int next = 0;
	char* text = out[next++ % 4];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 12, "%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
	return text;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Slider"));
}

static MosaicList* virtualDir(const char* path) {
	MosaicList* list = NULL;
	if (!strcmp(path, folder)) {
		list = host->list_new();
		host->list_push(list, host->entry_new(inner, MOSAIC_ENTRY_DIR, "Inner"));
	}
	else if (!strcmp(path, inner)) {
		list = host->list_new();
		char file[700];
		snprintf(file, sizeof(file), "%s/Game.gb", inner);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, "Game"));
	}
	return list;
}

static int transitionBegin(MosaicTransition* t) {
	int ow = 0, oh = 0, nw = 0, nh = 0;
	host->surface_size(t->old_frame, &ow, &oh);
	host->surface_size(t->new_frame, &nw, &nh);
	say("begin %s stock %i duration %i ease %i, size %i, old %ix%i new %ix%i, max %i",
		kindName(t->kind), t->stock_ms, t->duration_ms, t->ease, t->size == sizeof(MosaicTransition), ow, oh, nw, nh,
		host->transition_max_ms);
	say("begin %s read-only: fill %i, blit onto %i, free %i, select %i", kindName(t->kind),
		host->fill_rect((MosaicSurface*)t->old_frame, NULL, mosaic_color(0xFFFFFF, 255)),
		host->blit((MosaicSurface*)t->new_frame, red, NULL, NULL),
		host->surface_free(ID, (MosaicSurface*)t->old_frame), host->select_index(0));
	old_kept = t->old_frame;
	new_kept = t->new_frame;
	frames = 0;
	backwards = 0;
	last_progress = 0.0f;
	began = ms();
	if (!strcmp(style, "off")) return MOSAIC_STYLE_OFF;
	if (!strcmp(style, "stock")) return MOSAIC_STYLE_STOCK;
	if (!strcmp(style, "bad")) return 7;
	t->duration_ms = duration;
	t->ease = ease;
	return MOSAIC_STYLE_CUSTOM;
}

static int transitionFrame(MosaicSurface* screen, const MosaicTransition* t, float progress) {
	frames++;
	if (progress < last_progress) backwards++;
	last_progress = progress;
	if (!strcmp(fail, "frame") && frames == 2) {
		say("frame failing");
		return -1;
	}
	int w = 0, h = 0;
	host->surface_size(screen, &w, &h);
	if (frames == 1) {
		say("frame first %s progress %.3f duration %i, screen %ix%i %s, select %i", kindName(t->kind), progress,
			t->duration_ms, w, h, pixel(screen, 0, 0), host->select_index(0));
		// half of red over the black the frame starts as
		MosaicRect spot = { 0, 0, 8, 8 };
		int half = host->blit_alpha(screen, red, NULL, &spot, 128);
		say("frame alpha %i %s, none %i %s, bad %i %i, new %s", half, pixel(screen, 2, 2),
			host->blit_alpha(screen, red, NULL, &(MosaicRect){ 10, 0, 8, 8 }, 0), pixel(screen, 12, 2),
			host->blit_alpha(screen, red, NULL, &spot, 256), host->blit_alpha(screen, red, NULL, &spot, -1),
			pixel(t->new_frame, 0, 0));
	}
	// the old frame slides out to the left as the new one fades in
	int x = (int)(-progress * w);
	host->blit(screen, t->old_frame, NULL, &(MosaicRect){ x, 0, w, h });
	host->blit_alpha(screen, t->new_frame, NULL, &(MosaicRect){ 0, 0, w, h }, (int)(progress * 255));
	if (progress >= 1.0f) {
		say("frame last %s progress %.3f frames %i backwards %i took %li ms", kindName(t->kind), progress, frames,
			backwards, ms() - began);
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks == 4) {
		// the frames went back when the transition ended
		if (old_kept) say("after: old %s new %s", pixel(old_kept, 0, 0), pixel(new_kept, 0, 0));
		old_kept = new_kept = NULL;
		say("open inner %i", host->open_path(inner, MOSAIC_ENTRY_DIR));
	}
	if (ticks == 6) {
		say("done at %s", host->current_path());
		if (pak[0]) say("open pak %i", host->open_path(pak, MOSAIC_ENTRY_PAK));
	}
}

static MosaicPlugin plugin;

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, transition_max_ms) || !MOSAIC_HOST_HAS(host, blit_alpha)) return 1;
	snprintf(folder, sizeof(folder), "%s/Slider", host->sdcard_path);
	snprintf(inner, sizeof(inner), "%s/Inner", folder);
	style = host->cfg_get(ID, "style", "custom");
	fail = host->cfg_get(ID, "fail", "");
	pak = host->cfg_get(ID, "pak", "");
	duration = atoi(host->cfg_get(ID, "duration", "200"));
	ease = atoi(host->cfg_get(ID, "ease", "0"));
	red = host->surface_new(ID, 8, 8);
	host->fill_rect(red, NULL, mosaic_color(0xFF0000, 255));
	if (!strcmp(style, "noframe")) plugin.transition_frame = NULL;
	return 0;
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.transition_begin = transitionBegin,
	.transition_frame = transitionFrame,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
