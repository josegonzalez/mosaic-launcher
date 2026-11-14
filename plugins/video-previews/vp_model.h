#ifndef VP_MODEL_H
#define VP_MODEL_H

#include <stddef.h>

// The video previews plugin's model: where a game's clip and ffmpeg's
// libraries are, when a clip is due, the clock a clip plays against, which
// frames to show, and turning decoded pictures and sound into what the
// host takes. No SDL, no ffmpeg and no host here, so the host tests can
// check it on its own.

#define VP_DELAY_MS_DEFAULT 2000
#define VP_RETRY_MS_MAX 16000

// The most pixels a clip may have: past this its decoded pictures take
// more memory than a preview a few hundred pixels wide is worth.
#define VP_MAX_PIXELS (1920 * 1080)

// The host's own audio buffer, which audio_queued doesn't count.
#define VP_DEVICE_FRAMES 1024

// Frames are shown at most this often (30 a second); faster clips are
// decoded in full but only every other frame is shown.
#define VP_MIN_INTERVAL_MS 33
// Waits for a frame's moment are taken in slices this long, so the audio
// queue can be topped up between them.
#define VP_SLICE_MS 8
// A frame this far behind the clock is late; this many late in a row means
// decoding can't keep up.
#define VP_LATE_MS 120
#define VP_LATE_IN_A_ROW 10

// The sound queued ahead of what's playing: kept up to the high mark, and
// filled to the prime mark before the picture starts.
#define VP_AUDIO_HIGH_MS 250
#define VP_AUDIO_PRIME_MS 150

// <dir>/.videos/<file name without its last extension>.mp4 for a game's
// full path. Returns 1, or 0 (writing "") for a path with no folder or
// name, or one too long for out.
int VP_clipPath(const char* rom_path, char* out, size_t size);

// The folder a file is in. Returns 1, or 0 (writing "") without one.
int VP_dirOf(const char* path, char* out, size_t size);

// A library's file name for its major version, as dlopen takes it:
// lib<name>.so.<major>, or on macOS lib<name>.<major>.dylib. Returns 1, or
// 0 when it doesn't fit.
int VP_libName(const char* name, int major, int darwin, char* out, size_t size);

// The delay setting, in seconds, as milliseconds; fallback for anything
// but a whole number of seconds from 1 to 10.
int VP_parseDelayMs(const char* text, int fallback);

// A setting's toggle: "on" or "1" is on, "off" or "0" off, else fallback.
int VP_parseToggle(const char* text, int fallback);

// The screensaver's clock, as its clock setting names it: "24h", "12h", or
// off for anything else.
enum VpClockMode {
	VP_CLOCK_OFF,
	VP_CLOCK_24H,
	VP_CLOCK_12H,
};
int VP_parseClock(const char* text);
// "09:05" in 24h, "9:05 AM" in 12h with am and pm for the AM and PM, or ""
// with the clock off or a bad time.
void VP_formatClock(int hour, int minute, int mode, const char* am, const char* pm, char* out, size_t size);

// Music paused for a screensaver clip, played on once the clip ends only
// when the clip paused it: not when it wasn't playing, when something else
// played it on in between, or after a sleep, which music doesn't play on
// from either.
typedef struct VpMusic {
	int paused; // the clip paused it, and it's still paused
} VpMusic;
// After asking music to pause: paused is 1 when it was playing and paused.
void VpMusic_paused(VpMusic* m, int paused);
// Music's state now, as music.now says it ("playing", "paused" or
// "stopped"): playing means something else played it on.
void VpMusic_seen(VpMusic* m, const char* state);
// The device goes to sleep: music isn't played on after it.
void VpMusic_forget(VpMusic* m);
// The clip ended: 1 when music should play on, once.
int VpMusic_resume(VpMusic* m);

// When the selected game's clip is due: armed with the game and its clip
// once the selection settles, due delay_ms later, and after a refusal
// (art_replace returning -1, such as off the game list) due again later,
// waiting twice as long each time up to VP_RETRY_MS_MAX.
typedef struct VpArm {
	char rom[1024];   // the game the clip is for, or "" when nothing is armed
	char clip[1024];  // its clip
	unsigned long due; // when it's due
	int armed;        // waiting to be due
	unsigned long retry_ms; // the wait before the next try after a refusal
} VpArm;

void VP_arm(VpArm* arm, const char* rom, const char* clip, unsigned long now, unsigned long delay_ms);
void VP_disarm(VpArm* arm);
// 1 once when the armed clip is due, leaving it disarmed.
int VP_due(VpArm* arm, unsigned long now);
// After a refusal: armed again, due after the next wait.
void VP_retry(VpArm* arm, unsigned long now, unsigned long delay_ms);
// 1 when rom (a full path, or NULL) is the game armed or playing.
int VP_isFor(const VpArm* arm, const char* rom);

// The clock a clip plays against, in milliseconds from its start: the
// sound that has reached the device when the clip has sound, else the
// wall clock. When the sound ends before the picture, or is lost (the
// host closed the device), the clock carries on from there in wall time.
typedef struct VpClock {
	int rate;              // audio frames a second, or 0 for the wall clock
	long long pushed;      // frames queued since the start
	unsigned long start;   // the wall clock at the start, without sound
	int audio_done;        // no more sound is coming
	int holding;           // carrying on in wall time from held_ms
	long long held_ms;
	unsigned long held_at;
} VpClock;

void VP_clockStart(VpClock* clock, int rate, unsigned long now);
void VP_clockPushed(VpClock* clock, int frames);
// No more sound is coming: the clock goes on in wall time once the queue
// empties.
void VP_clockAudioDone(VpClock* clock);
// The clip's time now, with queued frames still waiting to play (from
// audio_queued, or -1 when the device has gone).
long long VP_clockMs(VpClock* clock, int queued, unsigned long now);
// How many milliseconds of sound are queued ahead of what's playing.
long long VP_queuedMs(const VpClock* clock, int queued);

// What to do with a decoded frame at pts_ms, with the clock at clock_ms
// and the last frame shown at last_ms (-1 for none yet this pass).
enum VpPace {
	VP_PACE_SHOW,
	VP_PACE_WAIT, // *wait_ms more, at most VP_SLICE_MS, then ask again
	VP_PACE_DROP, // too soon after the last frame shown
};
int VP_pace(long long pts_ms, long long clock_ms, long long last_ms, int* wait_ms);

// Counts late frames in a row; returns 1 once VP_LATE_IN_A_ROW have been,
// when the decoder should start skipping frames nothing refers to.
typedef struct VpLate {
	int in_a_row;
	int degraded;
} VpLate;
int VP_late(VpLate* late, long long pts_ms, long long clock_ms);

// How a clip's Y'CbCr is read.
enum VpYuv {
	VP_YUV_BT601, // limited range, standard definition: the usual case
	VP_YUV_BT709, // limited range, high definition
	VP_YUV_FULL,  // full range (JPEG)
};
// The matrix for a clip from its height and what it says about itself:
// full range wins, then BT.709 when it says so, then BT.601 when it says
// so, else BT.709 for 720 lines or more.
int VP_yuvMode(int full_range, int says_709, int says_601, int height);

// 4:2:0 planes to RGBA8888 (r, g, b, a bytes, a 255), w by h, into out with
// rows pitch bytes apart.
void VP_yuvToRgba(const unsigned char* y, int y_stride, const unsigned char* u, int u_stride,
	const unsigned char* v, int v_stride, int w, int h, int mode, unsigned char* out, int pitch);

// Decoded sound's sample layouts.
enum VpPcm {
	VP_PCM_S16,  // interleaved 16-bit
	VP_PCM_S16P, // a plane a channel, 16-bit
	VP_PCM_S32,
	VP_PCM_S32P,
	VP_PCM_FLT,  // interleaved float, -1 to 1
	VP_PCM_FLTP,
};
// How many channels to play: 1 for mono, else 2 (the first two).
int VP_outChannels(int channels);
// samples frames of channels channels in format, in planes (one for
// interleaved formats, one a channel for planar ones), to interleaved
// 16-bit frames of VP_outChannels(channels) channels in out. Returns
// the frames written, or -1 for a format or channel count it can't take.
int VP_pcmToS16(int format, int channels, const unsigned char* const* planes, int samples, short* out);

#endif
