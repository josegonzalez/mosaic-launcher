// audiomodel.c: who has plugins' audio device, opens, the queue's cap and
// how long a drain waits.

#include "check.h"
#include "audiomodel.h"

static void testOpen(void) {
	AudioOwner o;
	AudioOwner_clear(&o);
	CHECK(!AudioOwner_active(&o));
	CHECK(!AudioOwner_is(&o, "video"));
	CHECK(!AudioOwner_is(&o, ""));
	// bad ids, rates and channel counts
	CHECK(AudioOwner_open(&o, NULL, 48000, 2) == AUDIO_OPEN_REFUSED);
	CHECK(AudioOwner_open(&o, "", 48000, 2) == AUDIO_OPEN_REFUSED);
	char long_id[AUDIO_ID_SIZE + 1];
	memset(long_id, 'a', AUDIO_ID_SIZE);
	long_id[AUDIO_ID_SIZE] = '\0';
	CHECK(AudioOwner_open(&o, long_id, 48000, 2) == AUDIO_OPEN_REFUSED);
	CHECK(AudioOwner_open(&o, "video", AUDIO_RATE_MIN - 1, 2) == AUDIO_OPEN_REFUSED);
	CHECK(AudioOwner_open(&o, "video", AUDIO_RATE_MAX + 1, 2) == AUDIO_OPEN_REFUSED);
	CHECK(AudioOwner_open(&o, "video", 48000, 0) == AUDIO_OPEN_REFUSED);
	CHECK(AudioOwner_open(&o, "video", 48000, 3) == AUDIO_OPEN_REFUSED);
	CHECK(!AudioSpec_valid(NULL, 48000, 2));
	CHECK(!AudioSpec_valid("video", 48000, 3));
	CHECK(AudioSpec_valid("video", 48000, 2));
	CHECK(AudioSpec_valid("video", AUDIO_RATE_MIN, 1));
	CHECK(AudioOwner_open(&o, "video", AUDIO_RATE_MIN, 1) == AUDIO_OPEN_NEW);
	CHECK(AudioOwner_open(&o, "video", AUDIO_RATE_MAX, 2) == AUDIO_OPEN_NEW);

	AudioOwner_set(&o, "video", 44100, 2);
	CHECK(AudioOwner_active(&o));
	CHECK(AudioOwner_is(&o, "video"));
	CHECK(!AudioOwner_is(&o, "other"));
	CHECK(!AudioOwner_is(&o, NULL));
	CHECK(!o.paused && !o.draining);
	// the same format keeps it, another reopens it, another plugin is refused
	CHECK(AudioOwner_open(&o, "video", 44100, 2) == AUDIO_OPEN_KEEP);
	CHECK(AudioOwner_open(&o, "video", 48000, 2) == AUDIO_OPEN_REOPEN);
	CHECK(AudioOwner_open(&o, "video", 44100, 1) == AUDIO_OPEN_REOPEN);
	CHECK(AudioOwner_open(&o, "other", 44100, 2) == AUDIO_OPEN_REFUSED);
	CHECK(AudioOwner_open(&o, "video", 44100, 5) == AUDIO_OPEN_REFUSED);

	// setting it again starts afresh
	o.paused = 1;
	o.draining = 1;
	o.over_logged = 1;
	AudioOwner_set(&o, "other", 22050, 1);
	CHECK(AudioOwner_is(&o, "other"));
	CHECK(!o.paused && !o.draining && !o.over_logged);
	CHECK(o.rate == 22050 && o.channels == 1);
	AudioOwner_clear(&o);
	CHECK(!AudioOwner_active(&o));
	CHECK(AudioOwner_open(&o, "video", 44100, 2) == AUDIO_OPEN_NEW);
}

static void testQueue(void) {
	AudioOwner o;
	AudioOwner_clear(&o);
	// nobody has it
	CHECK(AudioQueue_bytes(&o, 0, 10) == -1);
	CHECK(AudioQueue_frames(&o, 400) == 0);

	AudioOwner_set(&o, "video", 48000, 2);
	int max = AudioQueue_maxFrames(&o);
	CHECK(max == 48000 * AUDIO_QUEUE_MAX_MS / 1000);
	CHECK(AudioQueue_bytes(&o, 0, 0) == 0);
	CHECK(AudioQueue_bytes(&o, 0, 100) == 400);
	CHECK(AudioQueue_bytes(&o, 0, -1) == -1);
	CHECK(AudioQueue_bytes(&o, -1, 1) == -1);
	// up to the cap, and not a frame past it
	CHECK(AudioQueue_bytes(&o, 0, max) == (long)max * 4);
	CHECK(AudioQueue_bytes(&o, max - 10, 10) == 40);
	CHECK(AudioQueue_bytes(&o, max - 10, 11) == -1);
	CHECK(AudioQueue_bytes(&o, max, 0) == 0);
	// no overflow for a count near INT_MAX
	CHECK(AudioQueue_bytes(&o, max, 2147483647) == -1);
	CHECK(AudioQueue_frames(&o, 400) == 100);
	CHECK(AudioQueue_frames(&o, 403) == 100);

	// mono frames are two bytes
	AudioOwner_set(&o, "video", 22050, 1);
	CHECK(AudioQueue_maxFrames(&o) == 22050 * AUDIO_QUEUE_MAX_MS / 1000);
	CHECK(AudioQueue_bytes(&o, 0, 100) == 200);
	CHECK(AudioQueue_frames(&o, 200) == 100);

	// nothing more goes in while it drains
	o.draining = 1;
	CHECK(AudioQueue_bytes(&o, 0, 1) == -1);
	CHECK(AudioQueue_frames(&o, 200) == 100);
}

static void testDrain(void) {
	AudioOwner o;
	AudioOwner_clear(&o);
	CHECK(AudioDrain_ms(&o, 100) == 0);

	AudioOwner_set(&o, "video", 48000, 2);
	int buffer_ms = (AUDIO_BUFFER_FRAMES * 1000 + 47999) / 48000;
	// an empty queue still waits for the device's buffer
	CHECK(AudioDrain_ms(&o, 0) == buffer_ms);
	CHECK(AudioDrain_ms(&o, -5) == buffer_ms);
	CHECK(AudioDrain_ms(&o, 48000 / 2) == 500 + buffer_ms);
	// rounded up
	CHECK(AudioDrain_ms(&o, 1) == 1 + buffer_ms);
	// never longer than the cap allows
	CHECK(AudioDrain_ms(&o, 48000 * 10) == AUDIO_QUEUE_MAX_MS + buffer_ms);
	// nothing plays while paused
	o.paused = 1;
	CHECK(AudioDrain_ms(&o, 48000) == 0);
}

static void testSound(void) {
	CHECK(AudioSound_maxFrames(48000) == 48000 * AUDIO_SOUND_MAX_MS / 1000);
	CHECK(AudioSound_valid("ui-sounds", 1, 22050, 1));
	CHECK(AudioSound_valid("ui-sounds", AudioSound_maxFrames(22050), 22050, 2));
	// longer than the cap, none, or a bad format or id
	CHECK(!AudioSound_valid("ui-sounds", AudioSound_maxFrames(22050) + 1, 22050, 2));
	CHECK(!AudioSound_valid("ui-sounds", 0, 22050, 2));
	CHECK(!AudioSound_valid("ui-sounds", -1, 22050, 2));
	CHECK(!AudioSound_valid("ui-sounds", 100, 1000, 2));
	CHECK(!AudioSound_valid("ui-sounds", 100, 22050, 3));
	CHECK(!AudioSound_valid(NULL, 100, 22050, 2));
	CHECK(!AudioSound_valid("", 100, 22050, 2));

	int buffer_ms = (AUDIO_BUFFER_FRAMES * 1000 + AUDIO_SOUND_RATE - 1) / AUDIO_SOUND_RATE;
	CHECK(AudioSound_drainMs(0) == buffer_ms);
	// half a second of the device's stereo frames
	CHECK(AudioSound_drainMs(AUDIO_SOUND_RATE / 2 * AUDIO_SOUND_CHANNELS * 2) == 500 + buffer_ms);
	CHECK(AudioSound_drainMs(4) == 1 + buffer_ms);

	// quiet from the first look with nothing queued, closing a linger later
	unsigned long quiet = 0;
	CHECK(AudioSound_idle(400, 1000, &quiet) == 0);
	CHECK(quiet == 0);
	CHECK(AudioSound_idle(0, 2000, &quiet) == 0);
	CHECK(quiet == 2000);
	CHECK(AudioSound_idle(0, 2000 + AUDIO_SOUND_LINGER_MS - 1, &quiet) == 0);
	CHECK(AudioSound_idle(0, 2000 + AUDIO_SOUND_LINGER_MS, &quiet) == 1);
	// another sound starts it again
	CHECK(AudioSound_idle(4, 9000, &quiet) == 0);
	CHECK(quiet == 0);
	// at a clock of 0, quiet is still kept
	CHECK(AudioSound_idle(0, 0, &quiet) == 0);
	CHECK(quiet == 1);
}

int main(void) {
	testOpen();
	testQueue();
	testDrain();
	testSound();
	CHECK_DONE();
}
