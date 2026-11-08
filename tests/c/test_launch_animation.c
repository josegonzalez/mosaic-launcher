// The launch animation plugin: its model on its own, then the plugin
// against a fake host, for what it asks for at transition_begin and what
// it draws each frame, gliding the pill or a cell, or fading.

#include "check.h"
#include "../../plugins/launch-animation/launch_animation.c"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void testPick(void) {
	MosaicRect pill = { 30, 132, 400, 48 };
	MosaicRect row = { 30, 132, 760, 48 };
	MosaicRect none = { 0, 0, 0, 0 };
	MosaicRect out = none;
	CHECK(LA_pick(&pill, &row, &out) == 1 && rectIs(out, 30, 132, 400, 48));
	// no pill, as under a layout or with names hidden: the row or cell
	CHECK(LA_pick(&none, &row, &out) == 1 && rectIs(out, 30, 132, 760, 48));
	// neither, as from the game switcher
	out = none;
	CHECK(LA_pick(&none, &none, &out) == 0 && out.w == 0);
	CHECK(LA_pick(NULL, NULL, &out) == 0);
	// a pill with no height doesn't count
	MosaicRect flat = { 30, 132, 400, 0 };
	CHECK(LA_pick(&flat, &row, &out) == 1 && out.w == 760);
}

static void testProgress(void) {
	CHECK(LA_progress(0.0f) == 0);
	CHECK(LA_progress(-0.5f) == 0);
	CHECK(LA_progress(0.5f) == 500);
	CHECK(LA_progress(0.75f) == 750);
	CHECK(LA_progress(0.9996f) == 1000);
	CHECK(LA_progress(1.0f) == 1000);
	CHECK(LA_progress(2.0f) == 1000);
}

static void testGlide(void) {
	// desktop: 1024x768, the top row's pill
	MosaicRect pill = { 30, 132, 400, 48 };
	MosaicRect at = LA_glide(&pill, 1024, 768, 0);
	CHECK(rectIs(at, 30, 132, 400, 48));
	// in the middle at 3/4 of the way, the same size
	at = LA_glide(&pill, 1024, 768, LA_GLIDE_END);
	CHECK(rectIs(at, 312, 360, 400, 48));
	// and resting there as it fades
	at = LA_glide(&pill, 1024, 768, 900);
	CHECK(rectIs(at, 312, 360, 400, 48));
	at = LA_glide(&pill, 1024, 768, LA_UNIT);
	CHECK(rectIs(at, 312, 360, 400, 48));
	// halfway along the glide, halfway there
	at = LA_glide(&pill, 1024, 768, LA_GLIDE_END / 2);
	CHECK(rectIs(at, 171, 246, 400, 48));
	// from below the middle it moves up; one already in the middle stays
	MosaicRect low = { 30, 600, 400, 48 };
	at = LA_glide(&low, 1024, 768, LA_GLIDE_END / 2);
	CHECK(at.y == 480 && at.x == 171);
	MosaicRect mid = { 312, 360, 400, 48 };
	CHECK(rectIs(LA_glide(&mid, 1024, 768, 300), 312, 360, 400, 48));
	// a cell wider than half the screen still lands centred
	MosaicRect cell = { 0, 100, 1024, 200 };
	CHECK(rectIs(LA_glide(&cell, 1024, 768, LA_UNIT), 0, 284, 1024, 200));
}

static void testAlpha(void) {
	// the old frame dims out over the glide
	CHECK(LA_dim(0) == 255);
	CHECK(LA_dim(LA_GLIDE_END / 2) == 127);
	CHECK(LA_dim(LA_GLIDE_END) == 0);
	CHECK(LA_dim(LA_UNIT) == 0);
	// then the pill fades out, finishing on the new frame
	CHECK(LA_pillAlpha(0) == 255);
	CHECK(LA_pillAlpha(LA_GLIDE_END) == 255);
	CHECK(LA_pillAlpha(875) == 127);
	CHECK(LA_pillAlpha(LA_UNIT) == 0);
	// the plain fade takes the whole launch
	CHECK(LA_fade(0) == 255);
	CHECK(LA_fade(500) == 127);
	CHECK(LA_fade(LA_UNIT) == 0);
	// each only goes down
	for (int p = 1; p <= LA_UNIT; p++) {
		CHECK(LA_dim(p) <= LA_dim(p - 1));
		CHECK(LA_pillAlpha(p) <= LA_pillAlpha(p - 1));
		CHECK(LA_fade(p) <= LA_fade(p - 1));
	}
}

static void testSettings(void) {
	CHECK(LA_duration("200", 1000) == 200);
	CHECK(LA_duration("350", 1000) == 350);
	CHECK(LA_duration("500", 1000) == 500);
	CHECK(LA_duration("0", 1000) == 0);
	CHECK(LA_duration("5000", 1000) == 1000);
	CHECK(LA_duration("", 1000) == LA_DURATION);
	CHECK(LA_duration(NULL, 1000) == LA_DURATION);
	CHECK(LA_duration("fast", 1000) == LA_DURATION);
	CHECK(LA_duration("200ms", 1000) == LA_DURATION);
	CHECK(LA_duration("-5", 1000) == LA_DURATION);
	CHECK(LA_ease("linear") == MOSAIC_EASE_LINEAR);
	CHECK(LA_ease("in") == MOSAIC_EASE_IN);
	CHECK(LA_ease("out") == MOSAIC_EASE_OUT);
	CHECK(LA_ease("in-out") == MOSAIC_EASE_IN_OUT);
	CHECK(LA_ease("bounce") == MOSAIC_EASE_OUT);
	CHECK(LA_ease(NULL) == MOSAIC_EASE_OUT);
}

// The fake host: surfaces are tags, and draws are recorded.
struct MosaicSurface {
	int w, h;
};

static MosaicSurface screen_surface = { 1024, 768 };
static MosaicSurface old_surface = { 1024, 768 };
static MosaicSurface new_surface = { 1024, 768 };

typedef struct Draw {
	const MosaicSurface* src;
	int whole; // src_rect NULL
	MosaicRect src_rect;
	MosaicRect dst_rect;
	int alpha;
} Draw;

static Draw draws[8];
static int draw_count = 0;
static int fail_alpha = 0;
static const char* cfg_duration = NULL;
static const char* cfg_ease = NULL;
static char last_log[256];

static void fakeLog(const char* id, const char* m) {
	(void)id;
	snprintf(last_log, sizeof(last_log), "%s", m);
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "duration") && cfg_duration) return cfg_duration;
	if (!strcmp(key, "ease") && cfg_ease) return cfg_ease;
	return fallback;
}
static int fakeSurfaceSize(const MosaicSurface* s, int* w, int* h) {
	*w = s->w;
	*h = s->h;
	return 0;
}
static void record(const MosaicSurface* src, const MosaicRect* sr, const MosaicRect* dr, int alpha) {
	if (draw_count >= 8) return;
	Draw* d = &draws[draw_count++];
	d->src = src;
	d->whole = sr == NULL;
	if (sr) d->src_rect = *sr;
	d->dst_rect = dr ? *dr : (MosaicRect){ 0, 0, src->w, src->h };
	d->alpha = alpha;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr) {
	if (t != &screen_surface) return -1;
	record(s, sr, dr, 255);
	return 0;
}
static int fakeBlitAlpha(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr, int a) {
	if (t != &screen_surface || a < 0 || a > 255 || fail_alpha) return -1;
	record(s, sr, dr, a);
	return 0;
}

static MosaicHost fake_host;

static void setUp(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(fake_host);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.surface_size = fakeSurfaceSize;
	fake_host.blit = fakeBlit;
	fake_host.blit_alpha = fakeBlitAlpha;
	fake_host.transition_max_ms = 1000;
	cfg_duration = cfg_ease = NULL;
	fail_alpha = 0;
}

static MosaicTransition launchOf(MosaicRect row, MosaicRect pill) {
	MosaicTransition t;
	memset(&t, 0, sizeof(t));
	t.size = sizeof(t);
	t.kind = MOSAIC_TRANSITION_LAUNCH;
	t.old_frame = &old_surface;
	t.new_frame = &new_surface;
	t.duration_ms = t.stock_ms = 0;
	t.ease = MOSAIC_EASE_OUT;
	t.old_row = row;
	t.old_pill = pill;
	return t;
}

static int frame(const MosaicTransition* t, float progress) {
	draw_count = 0;
	return plugin.transition_frame(&screen_surface, t, progress);
}

static void testPlugin(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->size == sizeof(MosaicPlugin) && !strcmp(p->id, "launch-animation"));
	CHECK(p->init(&fake_host) == 0);
	MosaicRect row = { 30, 132, 760, 48 };
	MosaicRect pill = { 30, 132, 400, 48 };
	MosaicRect none = { 0, 0, 0, 0 };

	// the defaults: 350 ms, easing out, gliding the pill
	MosaicTransition t = launchOf(row, pill);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK(t.duration_ms == 350 && t.ease == MOSAIC_EASE_OUT);
	CHECK_STR(last_log, "gliding the pill at 30,132 400x48 to the middle over 350 ms");

	// the first frame: the new frame, then the old one whole, then the pill where it was
	CHECK(frame(&t, 0.0f) == 0);
	CHECK(draw_count == 3);
	CHECK(draws[0].src == &new_surface && draws[0].whole && draws[0].alpha == 255);
	CHECK(draws[1].src == &old_surface && draws[1].whole && draws[1].alpha == 255);
	CHECK(draws[2].src == &old_surface && !draws[2].whole && rectIs(draws[2].src_rect, 30, 132, 400, 48));
	CHECK(rectIs(draws[2].dst_rect, 30, 132, 400, 48) && draws[2].alpha == 255);
	// halfway along the glide the old frame is half dimmed
	CHECK(frame(&t, 0.375f) == 0);
	CHECK(draws[1].alpha == 127);
	CHECK(rectIs(draws[2].src_rect, 30, 132, 400, 48) && rectIs(draws[2].dst_rect, 171, 246, 400, 48));
	// in the middle, the old frame gone, the pill fading
	CHECK(frame(&t, 0.875f) == 0);
	CHECK(draws[1].alpha == 0 && draws[2].alpha == 127 && rectIs(draws[2].dst_rect, 312, 360, 400, 48));
	// the last frame is only the new one
	CHECK(frame(&t, 1.0f) == 0);
	CHECK(draws[1].alpha == 0 && draws[2].alpha == 0);

	// a layout's cell, with no pill
	t = launchOf(row, none);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK_STR(last_log, "gliding the cell at 30,132 760x48 to the middle over 350 ms");
	CHECK(frame(&t, 1.0f) == 0);
	CHECK(rectIs(draws[2].src_rect, 30, 132, 760, 48) && rectIs(draws[2].dst_rect, 132, 360, 760, 48));

	// no selection known: a plain fade, the old frame whole over the new
	t = launchOf(none, none);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK_STR(last_log, "fading to black over 350 ms, the selection isn't known");
	CHECK(frame(&t, 0.5f) == 0);
	CHECK(draw_count == 2 && draws[1].src == &old_surface && draws[1].whole && draws[1].alpha == 127);
	CHECK(frame(&t, 1.0f) == 0);
	CHECK(draw_count == 2 && draws[1].alpha == 0);

	// a launcher whose MosaicTransition has no rects fades too
	t = launchOf(row, pill);
	t.size = offsetof(MosaicTransition, old_row);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK(frame(&t, 0.5f) == 0 && draw_count == 2);

	// other kinds are stock's, untouched
	t = launchOf(row, pill);
	t.kind = MOSAIC_TRANSITION_PUSH;
	t.duration_ms = 150;
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_STOCK);
	CHECK(t.duration_ms == 150);

	// a refused draw is an error, so the launcher falls back to stock's
	t = launchOf(row, pill);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	fail_alpha = 1;
	CHECK(frame(&t, 0.5f) == -1);
	fail_alpha = 0;

	// the settings, with the launcher's cap
	setUp();
	cfg_duration = "200";
	cfg_ease = "linear";
	CHECK(p->init(&fake_host) == 0);
	t = launchOf(row, pill);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK(t.duration_ms == 200 && t.ease == MOSAIC_EASE_LINEAR);
	setUp();
	cfg_duration = "4000";
	fake_host.transition_max_ms = 800;
	CHECK(p->init(&fake_host) == 0);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM && t.duration_ms == 800);

	// a launcher without transitions: it declines
	setUp();
	fake_host.size = offsetof(MosaicHost, blit_alpha);
	CHECK(p->init(&fake_host) == 1);
}

int main(void) {
	testPick();
	testProgress();
	testGlide();
	testAlpha();
	testSettings();
	testPlugin();
	CHECK_DONE();
}
