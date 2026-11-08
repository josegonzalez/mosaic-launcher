#ifndef INDICATORS_MODEL_H
#define INDICATORS_MODEL_H

// What header indicators decides, apart from the host, so the host tests
// can check it on its own.

#include <stddef.h>

#define HI_REFRESH_MS 5000 // how often free storage and SSH are looked at again
#define HI_FPS_WINDOW_MS 1000 // frames are counted over this long for each FPS reading
#define HI_FPS_GAP_MS 2000 // a frame this long after the last starts counting again, such as after sleep
#define HI_SSH_PORT 22

// The indicators, in the order they're drawn from the left.
enum HiItem {
	HI_PROFILE,
	HI_STORAGE,
	HI_FPS,
	HI_SSH,
	HI_COUNT,
};

// Free space as a short size, such as "12.3G", "980M" or "1.2T": in
// 1024-byte units, with one decimal below 100 and none from 100, moving to
// the next unit before it would show 1000.
void Hi_formatSize(unsigned long long bytes, char* out, size_t size);

// Frames a second from the times tick is called. A reading comes every
// HI_FPS_WINDOW_MS, smoothed with the one before it so the number doesn't
// jump around; a frame more than HI_FPS_GAP_MS after the last starts
// counting again without a reading.
typedef struct HiFps {
	unsigned long start; // when this window began
	unsigned long last;  // the last frame
	int frames;          // frames since start
	int started;         // 0 before the first frame
	double smooth;       // the smoothed reading, unrounded
	int value;           // the smoothed reading rounded, or -1 before the first
} HiFps;

void HiFps_init(HiFps* f);

// One frame at now. Returns 1 when value changed.
int HiFps_frame(HiFps* f, unsigned long now);

// 1 when the text of /proc/net/tcp or /proc/net/tcp6 has a socket
// listening on port, 0 otherwise.
int Hi_listening(const char* text, int port);

// Which indicators fit in max_width. widths[i] is how wide each one is, 0
// for one that's off or has nothing to show yet; they sit pad in from the
// pill's ends and gap apart. Drops the FPS, then the profile, then free
// storage, then the SSH dot, until the rest fit, and writes 1 into shown[i]
// for each one kept. Returns the width they take, or 0 for none.
int Hi_fit(const int widths[HI_COUNT], int pad, int gap, int max_width, int shown[HI_COUNT]);

// When it's time to look again: 1 at now >= *next, which moves on by
// period.
int Hi_due(unsigned long* next, unsigned long now, unsigned long period);

// "on" or "off", with spaces around it allowed; anything else is fallback.
int Hi_parseToggle(const char* setting, int fallback);

#endif
