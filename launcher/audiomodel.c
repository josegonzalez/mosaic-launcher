#include <stdio.h>
#include <string.h>
#include "audiomodel.h"

void AudioOwner_clear(AudioOwner* o) {
	memset(o, 0, sizeof(*o));
}

int AudioOwner_active(const AudioOwner* o) {
	return o->id[0] != '\0';
}

int AudioOwner_is(const AudioOwner* o, const char* id) {
	return id && id[0] && AudioOwner_active(o) && !strcmp(o->id, id);
}

int AudioSpec_valid(const char* id, int rate, int channels) {
	if (!id || !id[0] || strlen(id) >= AUDIO_ID_SIZE) return 0;
	return rate >= AUDIO_RATE_MIN && rate <= AUDIO_RATE_MAX && (channels == 1 || channels == 2);
}

int AudioOwner_open(const AudioOwner* o, const char* id, int rate, int channels) {
	if (!AudioSpec_valid(id, rate, channels)) return AUDIO_OPEN_REFUSED;
	if (!AudioOwner_active(o)) return AUDIO_OPEN_NEW;
	if (!AudioOwner_is(o, id)) return AUDIO_OPEN_REFUSED;
	return o->rate == rate && o->channels == channels ? AUDIO_OPEN_KEEP : AUDIO_OPEN_REOPEN;
}

void AudioOwner_set(AudioOwner* o, const char* id, int rate, int channels) {
	AudioOwner_clear(o);
	snprintf(o->id, sizeof(o->id), "%s", id);
	o->rate = rate;
	o->channels = channels;
}

int AudioQueue_maxFrames(const AudioOwner* o) {
	return (int)((long long)o->rate * AUDIO_QUEUE_MAX_MS / 1000);
}

long AudioQueue_bytes(const AudioOwner* o, int queued, int count) {
	if (count < 0 || queued < 0 || !AudioOwner_active(o) || o->draining) return -1;
	if ((long long)queued + count > AudioQueue_maxFrames(o)) return -1;
	return (long)count * o->channels * 2;
}

int AudioQueue_frames(const AudioOwner* o, unsigned long bytes) {
	if (!AudioOwner_active(o)) return 0;
	return (int)(bytes / ((unsigned long)o->channels * 2));
}

// ms for frames at rate, rounded up
static int framesMs(long long frames, int rate) {
	return (int)((frames * 1000 + rate - 1) / rate);
}

int AudioDrain_ms(const AudioOwner* o, int queued) {
	if (!AudioOwner_active(o) || o->paused) return 0;
	if (queued < 0) queued = 0;
	if (queued > AudioQueue_maxFrames(o)) queued = AudioQueue_maxFrames(o);
	return framesMs(queued, o->rate) + framesMs(AUDIO_BUFFER_FRAMES, o->rate);
}
