// The bundled launch-animation plugin, built in whole, with its frames
// read back. Its manifest says id=launch-animation, so the plugin's own
// settings and logs are used. From tick, once the selection has settled,
// it launches the pak its glider_pak setting names. At transition_begin it
// logs where the selection is, and after each of the plugin's frames it
// reads the screen: the pill's top middle where the glide has it, which
// should match the old frame's pill until it starts fading, and a pixel
// the pill never covers, which should be the old frame's dimmed toward
// black. It logs a summary after the last frame, which should be all
// black.

#define mosaic_plugin_get launch_animation_get
#include "../../../plugins/launch-animation/launch_animation.c"
#include "../../../plugins/launch-animation/launch_model.c"
#undef mosaic_plugin_get

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAG "glider"

static const char* glider_pak = "";
static long started = 0;
static int launched = 0;
static int frames = 0, pill_checks = 0, pill_ok = 0, dim_checks = 0, dim_ok = 0;
static int bx = -1, by = -1; // a pixel the pill never covers, or -1

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[400];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(TAG, message);
}

static long ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int inside(const MosaicRect* r, int x, int y) {
	return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static int near(int a, int b) {
	return abs(a - b) <= 3;
}

static int same(MosaicColor a, MosaicColor b) {
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

static int gliderBegin(MosaicTransition* t) {
	int style = transitionBegin(t);
	frames = pill_checks = pill_ok = dim_checks = dim_ok = 0;
	bx = by = -1;
	int w = 0, h = 0;
	host->surface_size(t->old_frame, &w, &h);
	if (gliding) {
		// the corners, as long as the glide never passes over them
		MosaicRect end = LA_glide(&from, w, h, LA_UNIT);
		int xs[] = { 4, w - 5, 4, w - 5 }, ys[] = { h - 5, h - 5, 4, 4 };
		for (int i = 0; i < 4 && bx < 0; i++) {
			int left = from.x < end.x ? from.x : end.x, right = from.x > end.x ? from.x : end.x;
			int top = from.y < end.y ? from.y : end.y, bottom = from.y > end.y ? from.y : end.y;
			MosaicRect swept = { left, top, right - left + from.w, bottom - top + from.h };
			if (!inside(&swept, xs[i], ys[i])) bx = xs[i], by = ys[i];
		}
	}
	MosaicColor on = { 0, 0, 0, 0 }, beside = { 0, 0, 0, 0 };
	if (gliding) {
		host->read_pixel(t->old_frame, from.x + from.w / 2, from.y + 2, &on);
		host->read_pixel(t->old_frame, from.x + from.w + 2, from.y + 2, &beside);
	}
	say("begin style %i gliding %i from %i,%i %ix%i on %02X%02X%02X beside %02X%02X%02X probe %i,%i", style, gliding,
		from.x, from.y, from.w, from.h, on.r, on.g, on.b, beside.r, beside.g, beside.b, bx, by);
	return style;
}

static int gliderFrame(MosaicSurface* screen, const MosaicTransition* t, float progress) {
	int result = transitionFrame(screen, t, progress);
	if (result != 0) return result;
	frames++;
	int p = LA_progress(progress);
	int w = 0, h = 0;
	host->surface_size(screen, &w, &h);
	MosaicColor got, want;
	if (gliding && p <= LA_GLIDE_END) {
		MosaicRect at = LA_glide(&from, w, h, p);
		pill_checks++;
		if (!host->read_pixel(screen, at.x + at.w / 2, at.y + 2, &got)
			&& !host->read_pixel(t->old_frame, from.x + from.w / 2, from.y + 2, &want) && same(got, want)) {
			pill_ok++;
		}
	}
	if (bx >= 0) {
		int a = LA_dim(p);
		dim_checks++;
		if (!host->read_pixel(screen, bx, by, &got) && !host->read_pixel(t->old_frame, bx, by, &want)
			&& near(got.r, want.r * a / 255) && near(got.g, want.g * a / 255) && near(got.b, want.b * a / 255)) {
			dim_ok++;
		}
	}
	if (progress >= 1.0f) {
		int black = 1;
		int xs[] = { 0, w / 2, w - 1, from.x + from.w / 2 }, ys[] = { 0, h / 2, h - 1, from.y + 2 };
		for (int i = 0; i < 4; i++) {
			if (host->read_pixel(screen, xs[i], ys[i], &got) || got.r || got.g || got.b || got.a != 255) black = 0;
		}
		say("last frames %i pill %i of %i dim %i of %i black %i", frames, pill_ok, pill_checks, dim_ok, dim_checks,
			black);
	}
	return 0;
}

static void gliderTick(unsigned long now) {
	(void)now;
	if (launched || !glider_pak[0] || !host->current_list()) return;
	if (!started) started = ms();
	if (ms() < started + 500) return;
	launched = 1;
	say("open pak %i", host->open_path(glider_pak, MOSAIC_ENTRY_PAK));
}

static int gliderInit(const MosaicHost* h) {
	int result = init(h);
	if (result == 0) {
		glider_pak = host->cfg_get("glider_pak", "");
		say("duration %i ease %i", duration, ease);
	}
	return result;
}

static MosaicPlugin glider;

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	glider = *launch_animation_get(h);
	glider.init = gliderInit;
	glider.tick = gliderTick;
	glider.transition_begin = gliderBegin;
	glider.transition_frame = gliderFrame;
	return &glider;
}
