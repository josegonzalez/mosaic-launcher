#ifndef CAPTURE_H
#define CAPTURE_H

// Plugins' screen captures over stock's PLAT_captureScreenshot: the last
// frame shown, composited from every layer as it was shown, or while a
// menu shows, the frame shown before it opened (see capturemodel.h). Main
// thread only.

#include "api.h"

// Around each frame the main loop draws: begin before anything is drawn,
// menu 1 for the launcher's dialogs and the quick menu, and shown once
// it's on screen.
void Capture_frameBegin(int menu);
void Capture_frameShown(void);

// The frame a capture gets, opaque and with no blending, or NULL when
// there's none (see CaptureFrames_source), with why set to say so. *owned
// is 1 when the caller frees it, and 0 for the held frame, which stays
// the capture's.
SDL_Surface* Capture_frame(int* owned, const char** why);

// Saves frame as a PNG at Screenshots/Launcher.<date>.png, stock's name,
// making the folder as stock does, and copies the path to out (size
// bytes) when out isn't NULL. Shows stock's notification when its
// screenshot notification setting is on. Returns 0, or -1.
int Capture_save(SDL_Surface* frame, char* out, size_t size);

// Drops the held frame, at quit.
void Capture_quit(void);

#endif
