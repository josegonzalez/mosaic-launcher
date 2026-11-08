// The transition styles plugin: its model on its own, then the plugin
// against a fake host, for what it answers and what each frame draws.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/transitions/transitions.c"

static int layerIs(TRLayer l, int frame, int x, int y, int alpha) {
	return l.frame == frame && l.x == x && l.y == y && l.alpha == alpha;
}

static void testSettings(void) {
	CHECK(TR_style("stock") == TR_STYLE_STOCK);
	CHECK(TR_style("slide") == TR_STYLE_SLIDE);
	CHECK(TR_style("fade") == TR_STYLE_FADE);
	CHECK(TR_style("slide_fade") == TR_STYLE_SLIDE_FADE);
	CHECK(TR_style("off") == TR_STYLE_OFF);
	CHECK(TR_style("Slide and fade") == TR_STYLE_SLIDE);
	CHECK(TR_style("") == TR_STYLE_SLIDE);
	CHECK(TR_style(NULL) == TR_STYLE_SLIDE);

	CHECK(TR_speed("fast") == 150);
	CHECK(TR_speed("normal") == 250);
	CHECK(TR_speed("slow") == 500);
	CHECK(TR_speed("300") == 250);
	CHECK(TR_speed(NULL) == 250);

	CHECK(TR_ease("linear") == MOSAIC_EASE_LINEAR);
	CHECK(TR_ease("in") == MOSAIC_EASE_IN);
	CHECK(TR_ease("out") == MOSAIC_EASE_OUT);
	CHECK(TR_ease("in_out") == MOSAIC_EASE_IN_OUT);
	CHECK(TR_ease("bounce") == MOSAIC_EASE_OUT);
	CHECK(TR_ease(NULL) == MOSAIC_EASE_OUT);

	CHECK(TR_duration(500, 1000) == 500);
	CHECK(TR_duration(500, 300) == 300);
	CHECK(TR_duration(500, 0) == 500);
	CHECK(TR_duration(-5, 1000) == 0);
}

static void testBegin(void) {
	int kinds[] = { MOSAIC_TRANSITION_PUSH, MOSAIC_TRANSITION_POP, MOSAIC_TRANSITION_SCREEN };
	for (int i = 0; i < 3; i++) {
		CHECK(TR_begin(TR_STYLE_SLIDE, kinds[i]) == MOSAIC_STYLE_CUSTOM);
		CHECK(TR_begin(TR_STYLE_FADE, kinds[i]) == MOSAIC_STYLE_CUSTOM);
		CHECK(TR_begin(TR_STYLE_SLIDE_FADE, kinds[i]) == MOSAIC_STYLE_CUSTOM);
		CHECK(TR_begin(TR_STYLE_STOCK, kinds[i]) == MOSAIC_STYLE_STOCK);
		CHECK(TR_begin(TR_STYLE_OFF, kinds[i]) == MOSAIC_STYLE_OFF);
	}
	// launches, and kinds it doesn't know, are stock's unless it's off
	CHECK(TR_begin(TR_STYLE_SLIDE, MOSAIC_TRANSITION_LAUNCH) == MOSAIC_STYLE_STOCK);
	CHECK(TR_begin(TR_STYLE_FADE, 9) == MOSAIC_STYLE_STOCK);
	CHECK(TR_begin(TR_STYLE_OFF, MOSAIC_TRANSITION_LAUNCH) == MOSAIC_STYLE_OFF);
	CHECK(TR_begin(7, MOSAIC_TRANSITION_PUSH) == MOSAIC_STYLE_STOCK);
}

static void testSlide(void) {
	TRLayer l[2];
	// a push: both go left, the new frame in from the right
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 0.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 1024, 0, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 0.25f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, -256, 0, 255) && layerIs(l[1], TR_NEW, 768, 0, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 1.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, -1024, 0, 255) && layerIs(l[1], TR_NEW, 0, 0, 255));
	// rounded, and the frames always meet
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 0.5f, 641, 480, l) == 0);
	CHECK(l[1].x - l[0].x == 641 && l[0].x == -321);

	// a pop: both go right, the new frame in from the left
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_POP, 0.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_NEW, -1024, 0, 255) && layerIs(l[1], TR_OLD, 0, 0, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_POP, 0.25f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_NEW, -768, 0, 255) && layerIs(l[1], TR_OLD, 256, 0, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_POP, 1.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_NEW, 0, 0, 255) && layerIs(l[1], TR_OLD, 1024, 0, 255));

	// the game switcher comes down from the top over the list
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_SCREEN, 0.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 0, -768, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_SCREEN, 0.5f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 0, -384, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_SCREEN, 1.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[1], TR_NEW, 0, 0, 255));
}

static void testFade(void) {
	TRLayer l[2];
	int kinds[] = { MOSAIC_TRANSITION_PUSH, MOSAIC_TRANSITION_SCREEN };
	for (int i = 0; i < 2; i++) {
		CHECK(TR_layers(TR_STYLE_FADE, kinds[i], 0.0f, 1024, 768, l) == 0);
		CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 0, 0, 0));
		CHECK(TR_layers(TR_STYLE_FADE, kinds[i], 0.5f, 1024, 768, l) == 0);
		CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 0, 0, 128));
		CHECK(TR_layers(TR_STYLE_FADE, kinds[i], 1.0f, 1024, 768, l) == 0);
		CHECK(layerIs(l[1], TR_NEW, 0, 0, 255));
	}
	// a pop: the old frame fades out over the new one
	CHECK(TR_layers(TR_STYLE_FADE, MOSAIC_TRANSITION_POP, 0.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_NEW, 0, 0, 255) && layerIs(l[1], TR_OLD, 0, 0, 255));
	CHECK(TR_layers(TR_STYLE_FADE, MOSAIC_TRANSITION_POP, 0.75f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_NEW, 0, 0, 255) && layerIs(l[1], TR_OLD, 0, 0, 64));
	CHECK(TR_layers(TR_STYLE_FADE, MOSAIC_TRANSITION_POP, 1.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[1], TR_OLD, 0, 0, 0));
}

static void testSlideFade(void) {
	TRLayer l[2];
	// a push: the new frame a quarter of the screen in from the right, fading in
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_PUSH, 0.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 256, 0, 0));
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_PUSH, 0.5f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 128, 0, 128));
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_PUSH, 1.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[1], TR_NEW, 0, 0, 255));
	// a pop: the old frame a quarter of the screen out to the right, fading out
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_POP, 0.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_NEW, 0, 0, 255) && layerIs(l[1], TR_OLD, 0, 0, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_POP, 0.25f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_NEW, 0, 0, 255) && layerIs(l[1], TR_OLD, 64, 0, 191));
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_POP, 1.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[1], TR_OLD, 256, 0, 0));
	// the game switcher a quarter of the screen down from the top, fading in
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_SCREEN, 0.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, 0, 0, 255) && layerIs(l[1], TR_NEW, 0, -192, 0));
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_SCREEN, 0.5f, 1024, 768, l) == 0);
	CHECK(layerIs(l[1], TR_NEW, 0, -96, 128));
	CHECK(TR_layers(TR_STYLE_SLIDE_FADE, MOSAIC_TRANSITION_SCREEN, 1.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[1], TR_NEW, 0, 0, 255));
}

static void testLayersEdges(void) {
	TRLayer l[2];
	// progress outside 0 to 1 is cut to it
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, -0.5f, 1024, 768, l) == 0);
	CHECK(layerIs(l[0], TR_OLD, 0, 0, 255));
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 3.0f, 1024, 768, l) == 0);
	CHECK(layerIs(l[1], TR_NEW, 0, 0, 255));
	// styles it doesn't draw, launches, and empty screens
	CHECK(TR_layers(TR_STYLE_STOCK, MOSAIC_TRANSITION_PUSH, 0.5f, 1024, 768, l) == -1);
	CHECK(TR_layers(TR_STYLE_OFF, MOSAIC_TRANSITION_PUSH, 0.5f, 1024, 768, l) == -1);
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_LAUNCH, 0.5f, 1024, 768, l) == -1);
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 0.5f, 0, 768, l) == -1);
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 0.5f, 1024, 0, l) == -1);
	CHECK(TR_layers(TR_STYLE_SLIDE, MOSAIC_TRANSITION_PUSH, 0.5f, 1024, 768, NULL) == -1);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
};

static MosaicSurface fake_screen = { 1024, 768 };
static MosaicSurface fake_old = { 1024, 768 };
static MosaicSurface fake_new = { 1024, 768 };

typedef struct Blit {
	const MosaicSurface* src;
	MosaicRect at;
	int alpha;
} Blit;

static Blit blits[4];
static int blit_count = 0;
static int blit_fails = 0;
static char logged[128];
static const char* cfg_style = NULL;
static const char* cfg_speed = NULL;
static const char* cfg_ease = NULL;

static void fakeLog(const char* id, const char* message) {
	(void)id;
	snprintf(logged, sizeof(logged), "%s", message);
}

static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	const char* value = !strcmp(key, "style") ? cfg_style : !strcmp(key, "speed") ? cfg_speed : !strcmp(key, "ease") ? cfg_ease : NULL;
	return value ? value : fallback;
}

static int fakeSurfaceSize(const MosaicSurface* s, int* w, int* h) {
	if (!s) return -1;
	*w = s->w;
	*h = s->h;
	return 0;
}

static int record(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* dst, int alpha) {
	if (target != &fake_screen || blit_fails) return -1;
	if (blit_count < 4) blits[blit_count++] = (Blit){ src, *dst, alpha };
	return 0;
}

static int fakeBlit(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst) {
	(void)src_rect;
	return record(target, src, dst, 255);
}

static int fakeBlitAlpha(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst, int alpha) {
	(void)src_rect;
	return record(target, src, dst, alpha);
}

static MosaicHost fake_host;

static void startPlugin(const char* s, const char* speed, const char* e, int max_ms) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.surface_size = fakeSurfaceSize;
	fake_host.blit = fakeBlit;
	fake_host.blit_alpha = fakeBlitAlpha;
	fake_host.transition_max_ms = max_ms;
	cfg_style = s;
	cfg_speed = speed;
	cfg_ease = e;
	logged[0] = '\0';
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
}

static MosaicTransition transitionOf(int kind) {
	MosaicTransition t;
	memset(&t, 0, sizeof(t));
	t.size = sizeof(t);
	t.kind = kind;
	t.old_frame = &fake_old;
	t.new_frame = &fake_new;
	t.stock_ms = 150;
	t.duration_ms = 150;
	t.ease = MOSAIC_EASE_OUT;
	return t;
}

static void testPlugin(void) {
	const MosaicPlugin* p = mosaic_plugin_get(NULL);
	CHECK_STR(p->id, "transitions");

	// the defaults: slide, normal, out
	startPlugin(NULL, NULL, NULL, 1000);
	CHECK_STR(logged, "style slide, 250 ms, ease out");
	MosaicTransition t = transitionOf(MOSAIC_TRANSITION_PUSH);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK(t.duration_ms == 250 && t.ease == MOSAIC_EASE_OUT);
	blit_count = 0;
	CHECK(p->transition_frame(&fake_screen, &t, 0.25f) == 0);
	CHECK(blit_count == 2);
	CHECK(blits[0].src == &fake_old && blits[0].at.x == -256 && blits[0].at.w == 1024 && blits[0].at.h == 768 && blits[0].alpha == 255);
	CHECK(blits[1].src == &fake_new && blits[1].at.x == 768 && blits[1].alpha == 255);
	// a launch is stock's
	t = transitionOf(MOSAIC_TRANSITION_LAUNCH);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_STOCK);
	CHECK(t.duration_ms == 150);

	// a fade, slow and linear: nothing at 0 alpha is drawn
	startPlugin("fade", "slow", "linear", 1000);
	CHECK_STR(logged, "style fade, 500 ms, ease linear");
	t = transitionOf(MOSAIC_TRANSITION_SCREEN);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK(t.duration_ms == 500 && t.ease == MOSAIC_EASE_LINEAR);
	blit_count = 0;
	CHECK(p->transition_frame(&fake_screen, &t, 0.0f) == 0);
	CHECK(blit_count == 1 && blits[0].src == &fake_old);
	blit_count = 0;
	CHECK(p->transition_frame(&fake_screen, &t, 0.5f) == 0);
	CHECK(blit_count == 2 && blits[1].src == &fake_new && blits[1].alpha == 128);

	// the launcher's cap wins over the speed
	startPlugin("slide_fade", "slow", "in_out", 300);
	CHECK_STR(logged, "style slide_fade, 300 ms, ease in_out");
	t = transitionOf(MOSAIC_TRANSITION_POP);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_CUSTOM);
	CHECK(t.duration_ms == 300 && t.ease == MOSAIC_EASE_IN_OUT);
	// a blit's error is the frame's
	blit_fails = 1;
	CHECK(p->transition_frame(&fake_screen, &t, 0.5f) == -1);
	blit_fails = 0;

	// off and stock leave the timing alone
	startPlugin("off", "fast", NULL, 1000);
	t = transitionOf(MOSAIC_TRANSITION_PUSH);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_OFF);
	CHECK(t.duration_ms == 150);
	startPlugin("stock", "fast", NULL, 1000);
	t = transitionOf(MOSAIC_TRANSITION_POP);
	CHECK(p->transition_begin(&t) == MOSAIC_STYLE_STOCK);
	CHECK(t.duration_ms == 150);

	// a launcher without transition styles: it declines
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = offsetof(MosaicHost, blit_alpha);
	fake_host.log = fakeLog;
	CHECK(p->init(&fake_host) == 1);
}

int main(void) {
	testSettings();
	testBegin();
	testSlide();
	testFade();
	testSlideFade();
	testLayersEdges();
	testPlugin();
	CHECK_DONE();
}
