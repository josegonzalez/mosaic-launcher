#include <stdio.h>
#include <string.h>
#include "capturemodel.h"

void CaptureFrames_clear(CaptureFrames* frames) {
	memset(frames, 0, sizeof(*frames));
}

int CaptureFrames_begin(CaptureFrames* frames, int menu) {
	frames->drawing = 1;
	frames->drawing_menu = menu != 0;
	if (!menu) {
		if (!frames->held) return CAPTURE_KEEP;
		frames->held = 0;
		return CAPTURE_DROP;
	}
	// a menu over a menu, such as a dialog from the quick menu, keeps the
	// frame from before the first
	if (frames->held || !frames->shown || frames->shown_menu) return CAPTURE_KEEP;
	return CAPTURE_HOLD;
}

void CaptureFrames_held(CaptureFrames* frames, int ok) {
	frames->held = ok != 0;
}

void CaptureFrames_shown(CaptureFrames* frames) {
	if (!frames->drawing) return;
	frames->drawing = 0;
	frames->shown = 1;
	frames->shown_menu = frames->drawing_menu;
}

int CaptureFrames_source(const CaptureFrames* frames) {
	if (frames->drawing || !frames->shown) return CAPTURE_NONE;
	if (!frames->shown_menu) return CAPTURE_LIVE;
	return frames->held ? CAPTURE_HELD : CAPTURE_NONE;
}

int Capture_name(const struct tm* when, char* out, size_t size) {
	if (!when || !out || !size) return -1;
	char date[32];
	if (!strftime(date, sizeof(date), "%Y-%m-%d-%H-%M-%S", when)) return -1;
	int n = snprintf(out, size, "Launcher.%s.png", date);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int Capture_dir(const char* sdcard, char* out, size_t size) {
	if (!sdcard || !out || !size) return -1;
	int n = snprintf(out, size, "%s/Screenshots", sdcard);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int Capture_path(const char* sdcard, const struct tm* when, char* out, size_t size) {
	char dir[512], name[64];
	if (Capture_dir(sdcard, dir, sizeof(dir)) != 0 || Capture_name(when, name, sizeof(name)) != 0) return -1;
	int n = snprintf(out, size, "%s/%s", dir, name);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}
