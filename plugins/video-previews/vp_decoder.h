#ifndef VP_DECODER_H
#define VP_DECODER_H

#include <stddef.h>
#include "mosaic_plugin.h"

// The decoder: one thread of the plugin's own playing one clip at a time,
// the latest request winning. It loads ffmpeg with dlopen the first time a
// clip is asked for, on that thread, so nothing is loaded until then. It
// calls the host only through audio_queue and audio_queued, the calls that
// are safe from it; everything else goes through the main thread, which
// polls it from tick: frames to show, whether the first one was shown,
// opening the audio device, and log lines.

// Sets up, starting nothing. libdir is where ffmpeg's libraries are.
void VPD_init(const MosaicHost* host, const char* id, const char* libdir);

// NULL when the plugin was built with ffmpeg, else why it wasn't.
const char* VPD_unavailable(void);

// Plays clip, replacing whatever is playing, with its sound when audio is
// 1 and it has some. Starts the thread the first time.
void VPD_request(const char* clip, int audio);

// Stops the clip. With wait, returns once the thread has let go of it.
void VPD_stop(int wait);

// Stops and joins the thread.
void VPD_shutdown(void);

// 1 while a clip is asked for, opening or playing.
int VPD_busy(void);

// The newest frame, when there's one not taken yet: RGBA8888, w by h, rows
// w * 4 bytes apart, valid until VPD_unlockFrame, which must follow a
// return of 1 soon after (the thread waits for it to swap frames).
typedef struct VpFrame {
	const unsigned char* rgba;
	int w, h;
	int first; // the clip's first frame, which wants VPD_verdict
} VpFrame;
int VPD_lockFrame(VpFrame* out);
void VPD_unlockFrame(void);

// Whether the first frame was shown (art_replace returned 0). The thread
// waits for this before it opens the sound and plays on, and stops the
// clip when it wasn't.
void VPD_verdict(int shown);

// 1 when the thread waits for the main thread to open the audio device,
// with the format it wants.
int VPD_audioAsk(int* rate, int* channels);
// Whether audio_open returned 0; without it the clip plays silently.
void VPD_audioAnswer(int ok);

// A line the thread wants logged, oldest first. Returns 1 with it in out,
// or 0 when there's none.
int VPD_takeLog(char* out, size_t size);

#endif
