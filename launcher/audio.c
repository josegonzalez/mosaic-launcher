#include <pthread.h>
#include "launcher.h"
#include "audio.h"
#include "audiomodel.h"

// Guards owner and device. Calls hold it while they use the device, so a
// close waits for a queue or count in progress on a plugin's thread. Only
// the main thread opens or closes the device.
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static AudioOwner owner;
static SDL_AudioDeviceID device = 0;
static pthread_t main_thread;
static int main_known = 0;

void Audio_setMainThread(void) {
	main_thread = pthread_self();
	main_known = 1;
}

int Audio_onMain(const char* id, const char* call) {
	if (!main_known || pthread_equal(pthread_self(), main_thread)) return 1;
	LOG_warn("plugins: %s called %s off the main thread, refusing it\n", id ? id : "(null)", call);
	return 0;
}

// With the lock held: closes the device, if it's open, and SDL's audio
// with it, so the launcher is back to having no audio, as stock's has.
static void closeLocked(void) {
	if (device) {
		SDL_CloseAudioDevice(device);
		device = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
	AudioOwner_clear(&owner);
}

static int queuedLocked(void) {
	return AudioQueue_frames(&owner, SDL_GetQueuedAudioSize(device));
}

int Audio_open(const char* id, int rate, int channels) {
	if (!Audio_onMain(id, "audio_open")) return -1;
	pthread_mutex_lock(&lock);
	int what = AudioOwner_open(&owner, id, rate, channels);
	if (what == AUDIO_OPEN_REFUSED) {
		if (!AudioSpec_valid(id, rate, channels))
			LOG_warn("plugins: %s can't open audio at %i Hz with %i channels\n", id ? id : "(null)", rate, channels);
		else LOG_warn("plugins: %s can't open audio, %s has it\n", id, owner.id);
		pthread_mutex_unlock(&lock);
		return -1;
	}
	if (what == AUDIO_OPEN_KEEP) {
		pthread_mutex_unlock(&lock);
		return 0;
	}
	if (what == AUDIO_OPEN_REOPEN) closeLocked();

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		LOG_warn("plugins: %s can't open audio: %s\n", id, SDL_GetError());
		pthread_mutex_unlock(&lock);
		return -1;
	}
	SDL_AudioSpec want;
	SDL_AudioSpec have;
	memset(&want, 0, sizeof(want));
	want.freq = rate;
	want.format = AUDIO_S16SYS;
	want.channels = channels;
	want.samples = AUDIO_BUFFER_FRAMES;
	want.callback = NULL; // queued with SDL_QueueAudio
	// no allowed changes: SDL converts to whatever the device plays
	device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!device) {
		LOG_warn("plugins: %s can't open audio: %s\n", id, SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		pthread_mutex_unlock(&lock);
		return -1;
	}
	AudioOwner_set(&owner, id, rate, channels);
	SDL_PauseAudioDevice(device, 0);
	LOG_info("plugins: %s %s audio at %i Hz with %i channel%s (%s)\n", id, what == AUDIO_OPEN_REOPEN ? "reopened" : "opened",
		rate, channels, channels == 1 ? "" : "s", SDL_GetCurrentAudioDriver());
	pthread_mutex_unlock(&lock);
	return 0;
}

int Audio_queue(const char* id, const short* frames, int count) {
	pthread_mutex_lock(&lock);
	int result = -1;
	if (device && AudioOwner_is(&owner, id) && (frames || count == 0)) {
		long bytes = AudioQueue_bytes(&owner, queuedLocked(), count);
		if (bytes == 0) result = 0;
		else if (bytes > 0) result = SDL_QueueAudio(device, frames, (Uint32)bytes) == 0 ? 0 : -1;
		else if (count > 0 && !owner.draining && !owner.over_logged) {
			// once an open: a plugin on its own thread could ask every frame
			owner.over_logged = 1;
			LOG_warn("plugins: %s queued more than %i ms of audio, refusing what goes past it\n", id, AUDIO_QUEUE_MAX_MS);
		}
	}
	pthread_mutex_unlock(&lock);
	return result;
}

int Audio_queued(const char* id) {
	pthread_mutex_lock(&lock);
	int queued = device && AudioOwner_is(&owner, id) ? queuedLocked() : -1;
	pthread_mutex_unlock(&lock);
	return queued;
}

int Audio_pause(const char* id, int paused) {
	if (!Audio_onMain(id, "audio_pause")) return -1;
	pthread_mutex_lock(&lock);
	int result = -1;
	if (device && AudioOwner_is(&owner, id)) {
		owner.paused = paused ? 1 : 0;
		SDL_PauseAudioDevice(device, owner.paused);
		result = 0;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

int Audio_close(const char* id) {
	if (!Audio_onMain(id, "audio_close")) return -1;
	pthread_mutex_lock(&lock);
	int result = -1;
	if (device && AudioOwner_is(&owner, id)) {
		closeLocked();
		LOG_info("plugins: %s closed audio\n", id);
		result = 0;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

void Audio_hostClose(const char* why, int drain) {
	pthread_mutex_lock(&lock);
	if (!device) {
		pthread_mutex_unlock(&lock);
		return;
	}
	char id[AUDIO_ID_SIZE];
	snprintf(id, sizeof(id), "%s", owner.id);
	Uint32 drained = 0;
	if (drain) {
		// nothing more goes in, and plugins' threads aren't kept waiting
		// meanwhile; only this thread closes, so the device stays open
		owner.draining = 1;
		Uint32 wait = AudioDrain_ms(&owner, queuedLocked());
		Uint32 buffer_ms = AudioDrain_ms(&owner, 0);
		SDL_AudioDeviceID playing = device;
		pthread_mutex_unlock(&lock);
		Uint32 start = SDL_GetTicks();
		while (SDL_GetTicks() - start < wait && SDL_GetQueuedAudioSize(playing) > 0) SDL_Delay(5);
		// then what's left in the device's own buffer
		Uint32 spent = SDL_GetTicks() - start;
		if (spent < wait) SDL_Delay(wait - spent < buffer_ms ? wait - spent : buffer_ms);
		drained = SDL_GetTicks() - start;
		pthread_mutex_lock(&lock);
	}
	closeLocked();
	pthread_mutex_unlock(&lock);
	if (drain) LOG_info("plugins: closed %s's audio %s, after %u ms for it to play out\n", id, why, (unsigned)drained);
	else LOG_info("plugins: closed %s's audio %s\n", id, why);
}

void Audio_closeFor(const char* id, const char* why) {
	pthread_mutex_lock(&lock);
	int mine = device && AudioOwner_is(&owner, id);
	if (mine) closeLocked();
	pthread_mutex_unlock(&lock);
	if (mine) LOG_info("plugins: closed %s's audio %s\n", id, why);
}
