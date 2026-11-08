#ifndef AUDIO_H
#define AUDIO_H

// Plugins' audio over SDL's audio queue (see audiomodel.h for the rules).
// One plugin at a time has the device. SDL's audio subsystem is started
// by the first open and quit again when the device closes, as minarch's
// SND_init and SND_quit do, so a launcher whose plugins play nothing never
// starts it.
//
// Audio_queue and Audio_queued may be called from any thread; the rest
// only from the main thread, which Audio_setMainThread names. A mutex
// guards the device, so a close waits for a queue or count in progress on
// another thread, and calls after it are refused.

// Call on the main thread, before any plugin loads.
void Audio_setMainThread(void);

// 1 on the main thread (or before Audio_setMainThread); otherwise logs
// that id called call from another thread, and returns 0.
int Audio_onMain(const char* id, const char* call);

// The host calls, each for plugin id. 0 or -1, except Audio_queued, which
// returns the frames still queued, or -1.
int Audio_open(const char* id, int rate, int channels);
int Audio_queue(const char* id, const short* frames, int count);
int Audio_queued(const char* id);
int Audio_pause(const char* id, int paused);
int Audio_close(const char* id);

// The launcher closes the device, whoever has it, logging why (eg. "before
// sleep"). With drain, it first waits for what's queued to play out, at
// most AudioDrain_ms. Does nothing when it isn't open.
void Audio_hostClose(const char* why, int drain);

// Closes the device if id has it, logging why.
void Audio_closeFor(const char* id, const char* why);

#endif
