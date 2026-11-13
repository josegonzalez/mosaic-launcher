#ifndef CAPTUREMODEL_H
#define CAPTUREMODEL_H

// The SDL-free half of plugins' screen captures (host->capture_screen and
// host->save_screenshot): which frame a capture gets, and the name stock
// gives a screenshot. A capture is the last frame shown, composited as it
// was shown, overlays and pills included. While a menu is shown (the
// launcher's dialogs or the quick menu), it's the last frame shown before
// the menu opened, so an action run from a menu captures what the menu
// was opened over, not the menu.

#include <stddef.h>
#include <time.h>

typedef struct CaptureFrames {
	int shown;        // a frame has been shown
	int shown_menu;   // and the last one shown was a menu's
	int drawing;      // a frame is being drawn, not yet shown
	int drawing_menu; // and it's a menu's
	int held;         // the frame shown before the menu opened is held
} CaptureFrames;

// What CaptureFrames_begin asks the caller to do before drawing a frame.
enum CaptureStep {
	CAPTURE_KEEP, // nothing
	CAPTURE_HOLD, // hold the frame shown now, then call CaptureFrames_held
	CAPTURE_DROP, // drop the held frame
};

// Where a capture comes from, for CaptureFrames_source.
enum CaptureSource {
	CAPTURE_NONE, // nowhere: nothing shown yet, a frame being drawn, or a menu shown with no frame held
	CAPTURE_LIVE, // the frame on screen
	CAPTURE_HELD, // the frame held from before the menu opened
};

void CaptureFrames_clear(CaptureFrames* frames);

// Before a frame is drawn, menu 1 for a menu's frame. A menu drawn over a
// frame that wasn't a menu's holds that frame first; a frame that isn't a
// menu's drops the held one.
int CaptureFrames_begin(CaptureFrames* frames, int menu);

// Whether the frame CAPTURE_HOLD asked for was held.
void CaptureFrames_held(CaptureFrames* frames, int ok);

// The frame begun is on screen.
void CaptureFrames_shown(CaptureFrames* frames);

int CaptureFrames_source(const CaptureFrames* frames);

// The launcher's screenshot name as stock names a game's,
// <name>.<YYYY-MM-DD-HH-MM-SS>.png, with "Launcher" for the name.
// Returns 0, or -1 when it doesn't fit in size bytes.
int Capture_name(const struct tm* when, char* out, size_t size);

// <sdcard>/Screenshots/<Capture_name>, and the folder alone.
int Capture_path(const char* sdcard, const struct tm* when, char* out, size_t size);
int Capture_dir(const char* sdcard, char* out, size_t size);

#endif
