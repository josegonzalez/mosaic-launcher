#ifndef AUDIOMODEL_H
#define AUDIOMODEL_H

// The parts of plugins' audio that don't need SDL: which plugin has the
// device and in what format, whether an open keeps, reopens or is refused,
// how much a plugin may have queued, and how long the launcher waits for
// what's queued to play out before it closes the device for a sleep, and
// plugins' sounds, which play on a device of their own beside it.

#define AUDIO_RATE_MIN 8000
#define AUDIO_RATE_MAX 96000
#define AUDIO_QUEUE_MAX_MS 1000   // the most a plugin may have queued and not yet played
#define AUDIO_BUFFER_FRAMES 1024  // the device's own buffer, which the queue feeds
#define AUDIO_ID_SIZE 64

// Who has the device. Plugins queue S16 frames, channels samples each,
// interleaved, at rate; SDL converts them to what the device plays.
typedef struct AudioOwner {
	char id[AUDIO_ID_SIZE]; // "" while no plugin has it
	int rate;
	int channels;
	int paused;
	int draining;    // closing for a sleep: what's queued plays out, nothing more goes in
	int over_logged; // a queue past the cap was logged since it opened
} AudioOwner;

// What an open does.
enum AudioOpen {
	AUDIO_OPEN_REFUSED = -1, // a bad id, rate or channel count, or another plugin has it
	AUDIO_OPEN_NEW,          // nobody has it: open it
	AUDIO_OPEN_KEEP,         // the plugin has it in this format already: nothing to do
	AUDIO_OPEN_REOPEN,       // the plugin has it in another format: close it and open it again
};

void AudioOwner_clear(AudioOwner* o);

// 1 while a plugin has the device.
int AudioOwner_active(const AudioOwner* o);

// 1 when id is the plugin that has the device.
int AudioOwner_is(const AudioOwner* o, const char* id);

// 1 for a plugin id the launcher can keep, and a rate and channel count
// it opens: AUDIO_RATE_MIN to AUDIO_RATE_MAX Hz, mono or stereo.
int AudioSpec_valid(const char* id, int rate, int channels);

// What opening the device for id in this format does (an enum AudioOpen).
// Changes nothing.
int AudioOwner_open(const AudioOwner* o, const char* id, int rate, int channels);

// id has the device now, unpaused, in this format.
void AudioOwner_set(AudioOwner* o, const char* id, int rate, int channels);

// The most frames the owner may have queued: AUDIO_QUEUE_MAX_MS at its rate.
int AudioQueue_maxFrames(const AudioOwner* o);

// How many bytes count more frames take, with queued frames still waiting,
// or -1 to refuse them: a negative count, nobody has the device, it's
// draining, or the queue would hold more than AudioQueue_maxFrames. 0 for
// none.
long AudioQueue_bytes(const AudioOwner* o, int queued, int count);

// How many whole frames bytes of the owner's queue hold.
int AudioQueue_frames(const AudioOwner* o, unsigned long bytes);

// The longest to wait, in ms, for queued frames to play out before
// closing: them and the device's buffer, rounded up, or 0 while it's
// paused, since nothing plays then.
int AudioDrain_ms(const AudioOwner* o, int queued);

// Plugins' sounds (host->sound_play): short clips, such as a button's
// click, on a device of their own, opened in one format that every sound
// is converted to, so a sound plays beside a plugin's audio_open device
// where the system mixes two. A sound replaces the one playing. The
// device closes once it's been quiet for AUDIO_SOUND_LINGER_MS.
#define AUDIO_SOUND_MAX_MS 2000     // the longest sound sound_play takes
#define AUDIO_SOUND_LINGER_MS 5000  // quiet for this long, the sound device closes
#define AUDIO_SOUND_RATE 48000      // the sound device's format
#define AUDIO_SOUND_CHANNELS 2

// 1 for a sound sound_play takes: a plugin id the launcher can keep,
// count frames (1 up to AUDIO_SOUND_MAX_MS of them at rate) and a rate
// and channel count as AudioSpec_valid takes them.
int AudioSound_valid(const char* id, int count, int rate, int channels);

// The most frames sound_play takes at rate.
int AudioSound_maxFrames(int rate);

// The longest to wait, in ms, for bytes queued on the sound device to
// play out, with its buffer, rounded up.
int AudioSound_drainMs(unsigned long bytes);

// Whether the sound device should close at now, with bytes still queued:
// it keeps *quiet_since at when it went quiet (0 while it plays), and
// returns 1 once it's been quiet for AUDIO_SOUND_LINGER_MS.
int AudioSound_idle(unsigned long bytes, unsigned long now, unsigned long* quiet_since);

#endif
