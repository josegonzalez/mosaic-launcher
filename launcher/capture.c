#include <sys/stat.h>
#include <time.h>
#include "launcher.h"
#include "capturemodel.h"
#include "capture.h"

static CaptureFrames frames;
static SDL_Surface* held = NULL; // the frame shown before a menu opened

// What's on screen now, read back from every layer as stock's
// PLAT_captureScreenshot composites them, made opaque as a screenshot is.
static SDL_Surface* grab(void) {
	if (!screen) return NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return NULL;
	if (PLAT_captureScreenshot(s) != 0) {
		LOG_warn("capture: can't read the screen back: %s\n", SDL_GetError());
		SDL_FreeSurface(s);
		return NULL;
	}
	Uint32* pixels = (Uint32*)s->pixels;
	for (int y = 0; y < s->h; y++) {
		Uint32* row = (Uint32*)((Uint8*)pixels + y * s->pitch);
		for (int x = 0; x < s->w; x++) row[x] |= 0xFF000000u;
	}
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
	return s;
}

static void dropHeld(void) {
	if (held) SDL_FreeSurface(held);
	held = NULL;
}

void Capture_frameBegin(int menu) {
	switch (CaptureFrames_begin(&frames, menu)) {
		case CAPTURE_HOLD:
			held = grab();
			CaptureFrames_held(&frames, held != NULL);
			break;
		case CAPTURE_DROP:
			dropHeld();
			break;
		default:
			break;
	}
}

void Capture_frameShown(void) {
	CaptureFrames_shown(&frames);
}

SDL_Surface* Capture_frame(int* owned, const char** why) {
	*owned = 0;
	switch (CaptureFrames_source(&frames)) {
		case CAPTURE_LIVE: {
			SDL_Surface* s = grab();
			if (!s) *why = "the screen couldn't be read back";
			*owned = s != NULL;
			return s;
		}
		case CAPTURE_HELD:
			return held;
		default:
			*why = frames.drawing ? "a frame is being drawn" : "no frame has been shown";
			return NULL;
	}
}

int Capture_save(SDL_Surface* frame, char* out, size_t size) {
	if (!frame) return -1;
	time_t now = time(NULL);
	struct tm when;
	char dir[512], path[600];
	if (!localtime_r(&now, &when) || Capture_dir(SDCARD_PATH, dir, sizeof(dir)) != 0
			|| Capture_path(SDCARD_PATH, &when, path, sizeof(path)) != 0) return -1;
	// make sure it exists, as stock does
	mkdir(dir, 0755);
	if (IMG_SavePNG(frame, path) != 0) {
		LOG_warn("capture: can't save %s: %s\n", path, IMG_GetError());
		return -1;
	}
	if (out && size) snprintf(out, size, "%s", path);
	if (CFG_getNotifyScreenshot()) {
		LOG_info("capture: saved %s, with the notification\n", path);
		Toast_show(TR("Screenshot saved"));
	}
	else LOG_info("capture: saved %s\n", path);
	return 0;
}

void Capture_quit(void) {
	dropHeld();
	CaptureFrames_clear(&frames);
}
