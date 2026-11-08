// Transition styles: owns the transition slot and draws folders' pushes and
// pops and the game list's change to the game switcher in the style its
// settings pick (see transitions_model.h), at their speed and ease, or
// leaves them to stock or turns them off. Launches stay stock's: the
// launch slot isn't this plugin's.
//
// The launcher only asks while its Menu transitions setting is on; with it
// off there are no menu transitions, whatever the style.

#include <stdio.h>
#include "mosaic_plugin.h"
#include "transitions_model.h"

#define ID "transitions"

static const MosaicHost* host;
static int style = TR_STYLE_SLIDE;
static int duration_ms = TR_NORMAL_MS;
static int ease = MOSAIC_EASE_OUT;

static int transitionBegin(MosaicTransition* t) {
	int answer = TR_begin(style, t->kind);
	if (answer == MOSAIC_STYLE_CUSTOM) {
		t->duration_ms = duration_ms;
		t->ease = ease;
	}
	return answer;
}

static int transitionFrame(MosaicSurface* screen, const MosaicTransition* t, float progress) {
	int w = 0, h = 0;
	if (host->surface_size(screen, &w, &h) != 0) return -1;
	TRLayer layers[2];
	if (TR_layers(style, t->kind, progress, w, h, layers) != 0) return -1;
	for (int i = 0; i < 2; i++) {
		const TRLayer* l = &layers[i];
		if (l->alpha <= 0) continue;
		const MosaicSurface* frame = l->frame == TR_OLD ? t->old_frame : t->new_frame;
		MosaicRect at = { l->x, l->y, w, h };
		int rc = l->alpha >= 255 ? host->blit(screen, frame, NULL, &at) : host->blit_alpha(screen, frame, NULL, &at, l->alpha);
		if (rc != 0) return -1;
	}
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, transition_max_ms) || !MOSAIC_HOST_HAS(host, blit_alpha)) {
		host->log(ID, "this launcher has no transition styles");
		return 1;
	}
	const char* style_value = host->cfg_get(ID, "style", "slide");
	const char* speed_value = host->cfg_get(ID, "speed", "normal");
	const char* ease_value = host->cfg_get(ID, "ease", "out");
	style = TR_style(style_value);
	duration_ms = TR_duration(TR_speed(speed_value), host->transition_max_ms);
	ease = TR_ease(ease_value);
	static const char* styles[] = { "stock", "slide", "fade", "slide_fade", "off" };
	static const char* eases[] = { "linear", "in", "out", "in_out" };
	char message[96];
	snprintf(message, sizeof(message), "style %s, %i ms, ease %s", styles[style], duration_ms, eases[ease]);
	host->log(ID, message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.transition_begin = transitionBegin,
	.transition_frame = transitionFrame,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
