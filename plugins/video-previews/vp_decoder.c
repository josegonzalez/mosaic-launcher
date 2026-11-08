#include "vp_decoder.h"

#ifndef VP_HAVE_FFMPEG

// Built without ffmpeg's headers (CI, or a desktop without ffmpeg): the
// plugin loads, says why once, and plays nothing.

void VPD_init(const MosaicHost* host, const char* id, const char* libdir) {
	(void)host;
	(void)id;
	(void)libdir;
}
const char* VPD_unavailable(void) {
	return "built without ffmpeg";
}
void VPD_request(const char* clip, int audio) {
	(void)clip;
	(void)audio;
}
void VPD_stop(int wait) {
	(void)wait;
}
void VPD_shutdown(void) {}
int VPD_busy(void) {
	return 0;
}
int VPD_lockFrame(VpFrame* out) {
	(void)out;
	return 0;
}
void VPD_unlockFrame(void) {}
void VPD_verdict(int shown) {
	(void)shown;
}
int VPD_audioAsk(int* rate, int* channels) {
	(void)rate;
	(void)channels;
	return 0;
}
void VPD_audioAnswer(int ok) {
	(void)ok;
}
int VPD_takeLog(char* out, size_t size) {
	(void)out;
	(void)size;
	return 0;
}

#else

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include "vp_model.h"

///////////////////////////////////////
// ffmpeg, loaded with dlopen: the headers give the types, and nothing
// from the libraries is linked.

#define VP_FFMPEG(X) \
	X(UTIL, avutil_version) \
	X(UTIL, av_log_set_level) \
	X(UTIL, av_frame_alloc) \
	X(UTIL, av_frame_free) \
	X(UTIL, av_frame_unref) \
	X(CODEC, avcodec_version) \
	X(CODEC, avcodec_find_decoder) \
	X(CODEC, avcodec_alloc_context3) \
	X(CODEC, avcodec_free_context) \
	X(CODEC, avcodec_parameters_to_context) \
	X(CODEC, avcodec_open2) \
	X(CODEC, avcodec_send_packet) \
	X(CODEC, avcodec_receive_frame) \
	X(CODEC, avcodec_flush_buffers) \
	X(CODEC, av_packet_alloc) \
	X(CODEC, av_packet_free) \
	X(CODEC, av_packet_unref) \
	X(FORMAT, avformat_version) \
	X(FORMAT, avformat_open_input) \
	X(FORMAT, avformat_find_stream_info) \
	X(FORMAT, av_find_best_stream) \
	X(FORMAT, av_read_frame) \
	X(FORMAT, av_seek_frame) \
	X(FORMAT, avformat_close_input)

static struct {
#define VP_FIELD(lib, name) __typeof__(name)* name;
	VP_FFMPEG(VP_FIELD)
#undef VP_FIELD
} ff;

#ifdef __APPLE__
#define VP_DARWIN 1
#else
#define VP_DARWIN 0
#endif

///////////////////////////////////////
// what the thread and the main thread share, under lock

#define LOG_LINES 8
#define LOG_SIZE 240

static struct {
	pthread_mutex_t lock;
	pthread_cond_t wake; // the thread waits on it for work and answers
	pthread_cond_t idle; // VPD_stop(1) waits on it
	pthread_t thread;
	int started;

	char path[1024]; // the clip asked for
	int audio;
	int pending; // a clip is asked for
	int stop;    // stop the clip playing
	int quit;
	int busy; // the thread has a clip

	unsigned char* frames[2]; // the thread fills one while the other is shown
	size_t capacity[2];
	int ready; // which one is shown
	int fresh; // a frame not taken yet
	int fresh_first;
	int fw, fh;

	int verdict; // -1 while waiting, else 0 or 1

	int asking; // waiting for the audio device
	int ask_rate, ask_channels;
	int answer; // -1 while waiting, else 0 or 1

	char logs[LOG_LINES][LOG_SIZE];
	int log_head, log_count;
} S = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.wake = PTHREAD_COND_INITIALIZER,
	.idle = PTHREAD_COND_INITIALIZER,
};

static const MosaicHost* host;
static const char* plugin_id;
static char lib_dir[1024];
static int loaded = 0; // 1 loaded, -1 failed
static int load_reported = 0;

static unsigned long nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000);
}

static void sleepMs(int ms) {
	if (ms <= 0) return;
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

// Waits on cond until ms have passed; the lock is held.
static void waitFor(pthread_cond_t* cond, int ms) {
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += ms / 1000;
	deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	pthread_cond_timedwait(cond, &S.lock, &deadline);
}

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char line[LOG_SIZE];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	pthread_mutex_lock(&S.lock);
	if (S.log_count == LOG_LINES) {
		// full: the oldest goes
		S.log_head = (S.log_head + 1) % LOG_LINES;
		S.log_count--;
	}
	memcpy(S.logs[(S.log_head + S.log_count) % LOG_LINES], line, sizeof(line));
	S.log_count++;
	pthread_mutex_unlock(&S.lock);
}

static int stopping(void) {
	pthread_mutex_lock(&S.lock);
	int stop = S.stop || S.quit;
	pthread_mutex_unlock(&S.lock);
	return stop;
}

///////////////////////////////////////
// loading

static void* openLib(const char* name, int major) {
	char file[128], path[1200];
	if (!VP_libName(name, major, VP_DARWIN, file, sizeof(file))) return NULL;
	snprintf(path, sizeof(path), "%s/%s", lib_dir, file);
	void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		const char* error = dlerror();
		say("unavailable: can't load %s: %s", path, error ? error : "unknown error");
	}
	return handle;
}

static int majorMatches(unsigned runtime, unsigned built, const char* name) {
	if (AV_VERSION_MAJOR(runtime) == AV_VERSION_MAJOR(built)) return 1;
	say("unavailable: lib%s is %u.x, but this was built for %u.x", name, AV_VERSION_MAJOR(runtime), AV_VERSION_MAJOR(built));
	return 0;
}

// Loads ffmpeg in dependency order, by full path from lib_dir: once
// libavutil is in, the others' need for it is met by its soname. Handles
// aren't closed on a failure, as nothing else is loaded either way.
static int load(void) {
	if (loaded) return loaded > 0;
	loaded = -1;
	unsigned long started = nowMs();
	void* UTIL = openLib("avutil", LIBAVUTIL_VERSION_MAJOR);
	void* CODEC = UTIL ? openLib("avcodec", LIBAVCODEC_VERSION_MAJOR) : NULL;
	void* FORMAT = CODEC ? openLib("avformat", LIBAVFORMAT_VERSION_MAJOR) : NULL;
	if (!FORMAT) return 0;
#define VP_RESOLVE(lib, name) \
	ff.name = (__typeof__(name)*)dlsym(lib, #name); \
	if (!ff.name) { \
		say("unavailable: no " #name " in ffmpeg's libraries"); \
		return 0; \
	}
	VP_FFMPEG(VP_RESOLVE)
#undef VP_RESOLVE
	if (!majorMatches(ff.avutil_version(), LIBAVUTIL_VERSION_INT, "avutil")
		|| !majorMatches(ff.avcodec_version(), LIBAVCODEC_VERSION_INT, "avcodec")
		|| !majorMatches(ff.avformat_version(), LIBAVFORMAT_VERSION_INT, "avformat"))
		return 0;
	// ffmpeg writes a lot to stderr, which is the launcher's log
	ff.av_log_set_level(AV_LOG_ERROR);
	loaded = 1;
	say("loaded ffmpeg %u.%u from %s in %lu ms", AV_VERSION_MAJOR(ff.avformat_version()),
		AV_VERSION_MINOR(ff.avformat_version()), lib_dir, nowMs() - started);
	return 1;
}

///////////////////////////////////////
// a clip

typedef struct Stream {
	AVFormatContext* format;
	AVCodecContext* codec;
	AVPacket* packet;
	AVFrame* frame;
	int index;
	double base; // seconds a timestamp unit
	int64_t start;
	int eof; // no more packets
	int drained; // and no more frames
} Stream;

static void closeStream(Stream* s) {
	if (s->frame) ff.av_frame_free(&s->frame);
	if (s->packet) ff.av_packet_free(&s->packet);
	if (s->codec) ff.avcodec_free_context(&s->codec);
	if (s->format) ff.avformat_close_input(&s->format);
	memset(s, 0, sizeof(*s));
	s->index = -1;
}

// Opens path's best stream of type, with its decoder; each stream has the
// file open on its own, so sound can be read ahead of the picture. Returns
// 1, -1 when the file has no such stream, or 0 on an error, logged.
static int openStream(Stream* s, const char* path, enum AVMediaType type, const char* what, const char* name) {
	memset(s, 0, sizeof(*s));
	s->index = -1;
	int error = ff.avformat_open_input(&s->format, path, NULL, NULL);
	if (error < 0) {
		say("can't open %s: error %i", name, error);
		return 0;
	}
	if (ff.avformat_find_stream_info(s->format, NULL) < 0) {
		say("can't read %s's streams", name);
		closeStream(s);
		return 0;
	}
	s->index = ff.av_find_best_stream(s->format, type, -1, -1, NULL, 0);
	if (s->index < 0) {
		closeStream(s);
		return -1;
	}
	AVStream* stream = s->format->streams[s->index];
	for (unsigned i = 0; i < s->format->nb_streams; i++) {
		if ((int)i != s->index) s->format->streams[i]->discard = AVDISCARD_ALL;
	}
	const AVCodec* decoder = ff.avcodec_find_decoder(stream->codecpar->codec_id);
	if (!decoder) {
		say("%s has no %s decoder for codec %i", name, what, (int)stream->codecpar->codec_id);
		closeStream(s);
		return 0;
	}
	s->codec = ff.avcodec_alloc_context3(decoder);
	if (!s->codec || ff.avcodec_parameters_to_context(s->codec, stream->codecpar) < 0) {
		closeStream(s);
		return 0;
	}
	if (type == AVMEDIA_TYPE_VIDEO) {
		long pixels = (long)s->codec->width * s->codec->height;
		if (pixels <= 0 || pixels > VP_MAX_PIXELS) {
			say("%s is %ix%i, past the %i pixels a preview may have", name, s->codec->width, s->codec->height, VP_MAX_PIXELS);
			closeStream(s);
			return 0;
		}
		// frame threads add a frame of delay each, so only bigger clips get one more
		s->codec->thread_count = pixels > 640 * 480 ? 2 : 1;
	} else {
		s->codec->thread_count = 1;
	}
	if (ff.avcodec_open2(s->codec, decoder, NULL) < 0) {
		say("can't open %s's %s decoder", name, what);
		closeStream(s);
		return 0;
	}
	s->packet = ff.av_packet_alloc();
	s->frame = ff.av_frame_alloc();
	if (!s->packet || !s->frame) {
		closeStream(s);
		return 0;
	}
	s->base = av_q2d(stream->time_base);
	s->start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
	return 1;
}

// The next decoded frame into s->frame: 1, 0 at the end, or -1 on an error.
static int nextFrame(Stream* s) {
	for (;;) {
		ff.av_frame_unref(s->frame);
		int got = ff.avcodec_receive_frame(s->codec, s->frame);
		if (got == 0) return 1;
		if (got == AVERROR_EOF) {
			s->drained = 1;
			return 0;
		}
		if (got != AVERROR(EAGAIN)) return -1;
		if (s->eof) {
			// flushed already and still nothing
			s->drained = 1;
			return 0;
		}
		int read = ff.av_read_frame(s->format, s->packet);
		if (read < 0) {
			s->eof = 1;
			ff.avcodec_send_packet(s->codec, NULL); // flush what's left
			continue;
		}
		if (s->packet->stream_index == s->index) {
			int sent = ff.avcodec_send_packet(s->codec, s->packet);
			if (sent < 0 && sent != AVERROR(EAGAIN) && sent != AVERROR_INVALIDDATA) {
				ff.av_packet_unref(s->packet);
				return -1;
			}
		}
		ff.av_packet_unref(s->packet);
	}
}

static long long frameMs(const Stream* s, const AVFrame* frame) {
	int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
	if (pts == AV_NOPTS_VALUE) return -1;
	return (long long)((double)(pts - s->start) * s->base * 1000.0);
}

static int rewindStream(Stream* s) {
	if (ff.av_seek_frame(s->format, s->index, s->start, AVSEEK_FLAG_BACKWARD) < 0) return 0;
	ff.avcodec_flush_buffers(s->codec);
	s->eof = 0;
	s->drained = 0;
	return 1;
}

static int pcmFormat(int format) {
	switch (format) {
	case AV_SAMPLE_FMT_S16: return VP_PCM_S16;
	case AV_SAMPLE_FMT_S16P: return VP_PCM_S16P;
	case AV_SAMPLE_FMT_S32: return VP_PCM_S32;
	case AV_SAMPLE_FMT_S32P: return VP_PCM_S32P;
	case AV_SAMPLE_FMT_FLT: return VP_PCM_FLT;
	case AV_SAMPLE_FMT_FLTP: return VP_PCM_FLTP;
	}
	return -1;
}

static int yuvMode(const AVFrame* frame) {
	int full = frame->color_range == AVCOL_RANGE_JPEG || frame->format == AV_PIX_FMT_YUVJ420P;
	int says_709 = frame->colorspace == AVCOL_SPC_BT709;
	int says_601 = frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M;
	return VP_yuvMode(full, says_709, says_601, frame->height);
}

// Converts a decoded picture into the frame the main thread doesn't have,
// then swaps them. 0 when it can't.
static int publish(const AVFrame* frame, int first) {
	if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) return 0;
	int w = frame->width, h = frame->height;
	pthread_mutex_lock(&S.lock);
	int back = 1 - S.ready;
	pthread_mutex_unlock(&S.lock);
	size_t need = (size_t)w * h * 4;
	if (S.capacity[back] < need) {
		unsigned char* grown = realloc(S.frames[back], need);
		if (!grown) return 0;
		S.frames[back] = grown;
		S.capacity[back] = need;
	}
	VP_yuvToRgba(frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1], frame->data[2],
		frame->linesize[2], w, h, yuvMode(frame), S.frames[back], w * 4);
	pthread_mutex_lock(&S.lock);
	S.ready = back;
	S.fresh = 1;
	S.fresh_first = first;
	S.fw = w;
	S.fh = h;
	pthread_mutex_unlock(&S.lock);
	return 1;
}

// Waits for the main thread's answer in *field (-1 until then), up to ms.
static int await(int* field, int ms) {
	unsigned long until = nowMs() + (unsigned long)ms;
	pthread_mutex_lock(&S.lock);
	while (*field < 0 && !S.stop && !S.quit && (long)(until - nowMs()) > 0) waitFor(&S.wake, 50);
	int answer = *field;
	pthread_mutex_unlock(&S.lock);
	return answer;
}

typedef struct Sound {
	Stream stream;
	int on;
	int format, channels, out_channels, rate;
	short* buffer;
	int capacity; // frames
} Sound;

// Decodes and queues sound until VP_AUDIO_HIGH_MS (or target_ms) is
// queued, or the sound ends. Turns the sound off when the device goes.
static void topUp(Sound* sound, VpClock* clock, long long target_ms) {
	while (sound->on && !sound->stream.drained) {
		int queued = host->audio_queued(plugin_id);
		if (queued < 0) {
			sound->on = 0; // the launcher closed the device, such as before a sleep
			return;
		}
		if (VP_queuedMs(clock, queued) >= target_ms) return;
		int got = nextFrame(&sound->stream);
		if (got <= 0) {
			sound->stream.drained = 1;
			VP_clockAudioDone(clock);
			return;
		}
		AVFrame* frame = sound->stream.frame;
		int samples = frame->nb_samples;
		if (samples > sound->capacity) {
			short* grown = realloc(sound->buffer, (size_t)samples * sound->out_channels * sizeof(short));
			if (!grown) return;
			sound->buffer = grown;
			sound->capacity = samples;
		}
		int frames = VP_pcmToS16(sound->format, sound->channels, (const unsigned char* const*)frame->extended_data, samples,
			sound->buffer);
		if (frames <= 0) continue;
		if (host->audio_queue(plugin_id, sound->buffer, frames) == 0) VP_clockPushed(clock, frames);
		else if (host->audio_queued(plugin_id) < 0) sound->on = 0;
	}
}

// Opens the clip's sound and asks the main thread for the device.
static void openSound(Sound* sound, const char* path, const char* name) {
	memset(sound, 0, sizeof(*sound));
	sound->stream.index = -1;
	int opened = openStream(&sound->stream, path, AVMEDIA_TYPE_AUDIO, "audio", name);
	if (opened <= 0) {
		if (opened < 0) say("%s has no sound", name);
		return;
	}
	AVCodecContext* codec = sound->stream.codec;
	sound->format = pcmFormat(codec->sample_fmt);
	sound->channels = codec->ch_layout.nb_channels;
	sound->out_channels = VP_outChannels(sound->channels);
	sound->rate = codec->sample_rate;
	if (sound->format < 0 || sound->channels < 1 || sound->rate < 8000 || sound->rate > 96000) {
		say("playing %s without sound: %i Hz, %i channels, sample format %i", name, sound->rate, sound->channels,
			(int)codec->sample_fmt);
		closeStream(&sound->stream);
		return;
	}
	pthread_mutex_lock(&S.lock);
	S.asking = 1;
	S.ask_rate = sound->rate;
	S.ask_channels = sound->out_channels;
	S.answer = -1;
	pthread_mutex_unlock(&S.lock);
	int ok = await(&S.answer, 1000);
	pthread_mutex_lock(&S.lock);
	S.asking = 0;
	pthread_mutex_unlock(&S.lock);
	if (ok != 1) {
		if (ok == 0) say("playing %s without sound: the audio device didn't open", name);
		closeStream(&sound->stream);
		return;
	}
	sound->on = 1;
}

// Starts a pass: with sound, its clock, primed so the first thing heard
// isn't a stutter; without (or once it's gone), the wall clock.
static void startClock(VpClock* clock, Sound* sound) {
	if (sound->on) {
		VP_clockStart(clock, sound->rate, nowMs());
		topUp(sound, clock, VP_AUDIO_PRIME_MS);
	}
	if (!sound->on) VP_clockStart(clock, 0, nowMs());
}

static void play(const char* path, int with_audio) {
	const char* name = strrchr(path, '/');
	name = name ? name + 1 : path;
	Stream video;
	int opened = openStream(&video, path, AVMEDIA_TYPE_VIDEO, "video", name);
	if (opened <= 0) {
		if (opened < 0) say("%s has no video", name);
		return;
	}

	// the first frame shows before anything else happens, so a clip the
	// launcher won't show costs one picture
	if (nextFrame(&video) <= 0 || !publish(video.frame, 1)) {
		say("%s: no picture this can show (pixel format %i)", name, video.frame ? video.frame->format : -1);
		closeStream(&video);
		return;
	}
	pthread_mutex_lock(&S.lock);
	S.verdict = -1;
	pthread_mutex_unlock(&S.lock);
	if (await(&S.verdict, 2000) != 1) {
		closeStream(&video);
		return;
	}
	long long first_ms = frameMs(&video, video.frame);
	ff.av_frame_unref(video.frame);

	Sound sound;
	memset(&sound, 0, sizeof(sound));
	sound.stream.index = -1;
	if (with_audio) openSound(&sound, path, name);
	say("playing %s: %ix%i, %s", name, video.codec->width, video.codec->height, sound.on ? "with sound" : "silent");

	VpClock clock;
	VpLate late = { 0, 0 };
	long long last_ms = first_ms;
	int passes = 1, shown = 1;
	startClock(&clock, &sound);
	int have = 0;
	long long pts = 0;

	while (!stopping()) {
		if (sound.on) topUp(&sound, &clock, VP_AUDIO_HIGH_MS);
		if (!have) {
			int got = nextFrame(&video);
			if (got < 0) {
				say("%s: a decoding error stopped it", name);
				break;
			}
			if (got == 0) {
				// the end: let the sound play out, then go round again
				unsigned long until = nowMs() + VP_AUDIO_HIGH_MS + 200;
				while (sound.on && !stopping() && host->audio_queued(plugin_id) > 0 && (long)(until - nowMs()) > 0)
					sleepMs(VP_SLICE_MS);
				if (!rewindStream(&video) || (sound.on && !rewindStream(&sound.stream))) {
					say("%s: can't go back to the start", name);
					break;
				}
				passes++;
				last_ms = -1;
				startClock(&clock, &sound);
				continue;
			}
			pts = frameMs(&video, video.frame);
			if (pts < 0) pts = last_ms < 0 ? 0 : last_ms + VP_MIN_INTERVAL_MS;
			have = 1;
		}
		int queued = sound.on ? host->audio_queued(plugin_id) : clock.rate ? -1 : 0;
		long long now_ms = VP_clockMs(&clock, queued, nowMs());
		int wait = 0;
		int pace = VP_pace(pts, now_ms, last_ms, &wait);
		if (pace == VP_PACE_WAIT) {
			sleepMs(wait);
			continue;
		}
		have = 0;
		if (pace == VP_PACE_DROP) continue;
		if (VP_late(&late, pts, now_ms)) {
			say("%s decodes slower than it plays; skipping frames nothing refers to", name);
			video.codec->skip_frame = AVDISCARD_NONREF;
		}
		if (publish(video.frame, 0)) shown++;
		last_ms = pts;
	}
	say("stopped %s after %i frames, %i passes", name, shown, passes);
	free(sound.buffer);
	if (sound.stream.format) closeStream(&sound.stream);
	closeStream(&video);
}

static void* worker(void* unused) {
	(void)unused;
	pthread_mutex_lock(&S.lock);
	for (;;) {
		while (!S.pending && !S.quit) pthread_cond_wait(&S.wake, &S.lock);
		if (S.quit) break;
		char path[sizeof(S.path)];
		memcpy(path, S.path, sizeof(path));
		int audio = S.audio;
		S.pending = 0;
		S.stop = 0;
		S.busy = 1;
		pthread_mutex_unlock(&S.lock);

		if (load()) play(path, audio);
		else if (!load_reported) {
			load_reported = 1;
			say("unavailable: ffmpeg didn't load, so no clip plays");
		}

		pthread_mutex_lock(&S.lock);
		S.busy = 0;
		S.fresh = 0;
		pthread_cond_broadcast(&S.idle);
	}
	S.busy = 0;
	pthread_cond_broadcast(&S.idle);
	pthread_mutex_unlock(&S.lock);
	return NULL;
}

///////////////////////////////////////
// the main thread's side

void VPD_init(const MosaicHost* h, const char* id, const char* libdir) {
	host = h;
	plugin_id = id;
	snprintf(lib_dir, sizeof(lib_dir), "%s", libdir ? libdir : "");
}

const char* VPD_unavailable(void) {
	return NULL;
}

void VPD_request(const char* clip, int audio) {
	pthread_mutex_lock(&S.lock);
	if (!S.started) {
		S.quit = 0;
		if (pthread_create(&S.thread, NULL, worker, NULL) == 0) S.started = 1;
		else {
			pthread_mutex_unlock(&S.lock);
			say("can't start the decoder's thread");
			return;
		}
	}
	snprintf(S.path, sizeof(S.path), "%s", clip);
	S.audio = audio;
	S.pending = 1;
	S.stop = 1; // whatever is playing gives way
	S.fresh = 0;
	pthread_cond_broadcast(&S.wake);
	pthread_mutex_unlock(&S.lock);
}

void VPD_stop(int wait) {
	pthread_mutex_lock(&S.lock);
	S.pending = 0;
	S.stop = 1;
	S.fresh = 0;
	pthread_cond_broadcast(&S.wake);
	unsigned long until = nowMs() + 3000;
	while (wait && S.busy && (long)(until - nowMs()) > 0) waitFor(&S.idle, 50);
	pthread_mutex_unlock(&S.lock);
}

void VPD_shutdown(void) {
	pthread_mutex_lock(&S.lock);
	int started = S.started;
	S.quit = 1;
	S.stop = 1;
	S.pending = 0;
	pthread_cond_broadcast(&S.wake);
	pthread_mutex_unlock(&S.lock);
	if (started) pthread_join(S.thread, NULL);
	S.started = 0;
	for (int i = 0; i < 2; i++) {
		free(S.frames[i]);
		S.frames[i] = NULL;
		S.capacity[i] = 0;
	}
}

int VPD_busy(void) {
	pthread_mutex_lock(&S.lock);
	int busy = S.busy || S.pending;
	pthread_mutex_unlock(&S.lock);
	return busy;
}

int VPD_lockFrame(VpFrame* out) {
	pthread_mutex_lock(&S.lock);
	if (!S.fresh || S.stop || S.pending) {
		pthread_mutex_unlock(&S.lock);
		return 0;
	}
	S.fresh = 0;
	out->rgba = S.frames[S.ready];
	out->w = S.fw;
	out->h = S.fh;
	out->first = S.fresh_first;
	return 1; // the lock stays held until VPD_unlockFrame
}

void VPD_unlockFrame(void) {
	pthread_mutex_unlock(&S.lock);
}

void VPD_verdict(int shown) {
	pthread_mutex_lock(&S.lock);
	S.verdict = shown ? 1 : 0;
	pthread_cond_broadcast(&S.wake);
	pthread_mutex_unlock(&S.lock);
}

int VPD_audioAsk(int* rate, int* channels) {
	pthread_mutex_lock(&S.lock);
	int asking = S.asking && S.answer < 0 && !S.stop;
	if (asking) {
		*rate = S.ask_rate;
		*channels = S.ask_channels;
	}
	pthread_mutex_unlock(&S.lock);
	return asking;
}

void VPD_audioAnswer(int ok) {
	pthread_mutex_lock(&S.lock);
	S.answer = ok ? 1 : 0;
	pthread_cond_broadcast(&S.wake);
	pthread_mutex_unlock(&S.lock);
}

int VPD_takeLog(char* out, size_t size) {
	pthread_mutex_lock(&S.lock);
	int any = S.log_count > 0;
	if (any) {
		snprintf(out, size, "%s", S.logs[S.log_head]);
		S.log_head = (S.log_head + 1) % LOG_LINES;
		S.log_count--;
	}
	pthread_mutex_unlock(&S.lock);
	return any;
}

#endif
