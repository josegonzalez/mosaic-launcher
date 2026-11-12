// Launch animation: as a game or pak starts from the game list, the
// selection pill glides from where it was to the middle of the screen
// while the rest of the screen dims to black, and then fades out itself,
// finishing on the black the launch goes into. It owns the launch slot.
// Under a plugin layout, which draws its own highlight, the selected cell
// glides instead. When the launcher doesn't know where the selection is,
// such as a launch from the game switcher, the screen fades to black
// without a glide.
//
// The duration setting is how long it runs, in ms (350 by default, up to
// the launcher's cap), and the ease setting how the glide moves (out by
// default, slowing as it reaches the middle).

#include <stdio.h>
#include "launch_model.h"
#include "mosaic_plugin.h"

#define ID "launch-animation"

static const MosaicHost* host;
static int duration = LA_DURATION;
static int ease = MOSAIC_EASE_OUT;
static int gliding = 0;  // 1 when this launch's selection is known
static MosaicRect from; // where the pill or cell is in the old frame

static int transitionBegin(MosaicTransition* t) {
	if (t->kind != MOSAIC_TRANSITION_LAUNCH) return MOSAIC_STYLE_STOCK;
	gliding = MOSAIC_TRANSITION_HAS(t, new_pill) && LA_pick(&t->old_pill, &t->old_row, &from);
	char message[160];
	if (gliding) {
		snprintf(message, sizeof(message), "gliding the %s at %i,%i %ix%i to the middle over %i ms",
			t->old_pill.w > 0 ? "pill" : "cell", from.x, from.y, from.w, from.h, duration);
	}
	else snprintf(message, sizeof(message), "fading to black over %i ms, the selection isn't known", duration);
	host->log(ID, message);
	t->duration_ms = duration;
	t->ease = ease;
	return MOSAIC_STYLE_CUSTOM;
}

static int transitionFrame(MosaicSurface* screen, const MosaicTransition* t, float progress) {
	int p = LA_progress(progress);
	int w = 0, h = 0;
	if (host->surface_size(screen, &w, &h) != 0) return -1;
	if (host->blit(screen, t->new_frame, NULL, NULL) != 0) return -1;
	if (!gliding) return host->blit_alpha(screen, t->old_frame, NULL, NULL, LA_fade(p)) != 0 ? -1 : 0;
	if (host->blit_alpha(screen, t->old_frame, NULL, NULL, LA_dim(p)) != 0) return -1;
	MosaicRect at = LA_glide(&from, w, h, p);
	return host->blit_alpha(screen, t->old_frame, &from, &at, LA_pillAlpha(p)) != 0 ? -1 : 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, transition_max_ms) || !MOSAIC_HOST_HAS(host, blit_alpha)) {
		host->log(ID, "this launcher can't animate launches");
		return 1;
	}
	duration = LA_duration(host->cfg_get("duration", "350"), host->transition_max_ms);
	ease = LA_ease(host->cfg_get("ease", "out"));
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
