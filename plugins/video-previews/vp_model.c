#include "vp_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int VP_clipPath(const char* rom_path, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!rom_path) return 0;
	const char* slash = strrchr(rom_path, '/');
	if (!slash || slash == rom_path || !slash[1]) return 0;
	const char* name = slash + 1;
	const char* dot = strrchr(name, '.');
	int name_len = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
	int written = snprintf(out, size, "%.*s/.videos/%.*s.mp4", (int)(slash - rom_path), rom_path, name_len, name);
	if (written < 0 || (size_t)written >= size) {
		out[0] = '\0';
		return 0;
	}
	return 1;
}

int VP_dirOf(const char* path, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!path) return 0;
	const char* slash = strrchr(path, '/');
	if (!slash) return 0;
	size_t len = slash == path ? 1 : (size_t)(slash - path);
	if (len >= size) return 0;
	memcpy(out, path, len);
	out[len] = '\0';
	return 1;
}

int VP_libName(const char* name, int major, int darwin, char* out, size_t size) {
	if (!out || !size) return 0;
	int written = darwin ? snprintf(out, size, "lib%s.%i.dylib", name, major)
		: snprintf(out, size, "lib%s.so.%i", name, major);
	if (written < 0 || (size_t)written >= size) {
		out[0] = '\0';
		return 0;
	}
	return 1;
}

int VP_parseDelayMs(const char* text, int fallback) {
	if (!text || !*text) return fallback;
	char* end = NULL;
	long seconds = strtol(text, &end, 10);
	if (*end || seconds < 1 || seconds > 10) return fallback;
	return (int)seconds * 1000;
}

int VP_parseToggle(const char* text, int fallback) {
	if (!text) return fallback;
	if (!strcmp(text, "on") || !strcmp(text, "1")) return 1;
	if (!strcmp(text, "off") || !strcmp(text, "0")) return 0;
	return fallback;
}

///////////////////////////////////////
// when a clip is due

void VP_arm(VpArm* arm, const char* rom, const char* clip, unsigned long now, unsigned long delay_ms) {
	snprintf(arm->rom, sizeof(arm->rom), "%s", rom ? rom : "");
	snprintf(arm->clip, sizeof(arm->clip), "%s", clip ? clip : "");
	arm->due = now + delay_ms;
	arm->armed = 1;
	arm->retry_ms = delay_ms;
}

void VP_disarm(VpArm* arm) {
	arm->rom[0] = '\0';
	arm->clip[0] = '\0';
	arm->armed = 0;
	arm->retry_ms = 0;
}

int VP_due(VpArm* arm, unsigned long now) {
	if (!arm->armed || (long)(now - arm->due) < 0) return 0;
	arm->armed = 0;
	return 1;
}

void VP_retry(VpArm* arm, unsigned long now, unsigned long delay_ms) {
	if (!arm->rom[0]) return;
	unsigned long wait = arm->retry_ms ? arm->retry_ms : delay_ms;
	arm->due = now + wait;
	arm->armed = 1;
	arm->retry_ms = wait * 2 > VP_RETRY_MS_MAX ? VP_RETRY_MS_MAX : wait * 2;
}

int VP_isFor(const VpArm* arm, const char* rom) {
	return rom && arm->rom[0] && !strcmp(arm->rom, rom);
}

///////////////////////////////////////
// the clock

void VP_clockStart(VpClock* clock, int rate, unsigned long now) {
	memset(clock, 0, sizeof(*clock));
	clock->rate = rate > 0 ? rate : 0;
	clock->start = now;
}

void VP_clockPushed(VpClock* clock, int frames) {
	if (frames > 0) clock->pushed += frames;
}

void VP_clockAudioDone(VpClock* clock) {
	clock->audio_done = 1;
}

static long long playedMs(const VpClock* clock, int queued) {
	long long played = clock->pushed - (queued > 0 ? queued : 0) - VP_DEVICE_FRAMES;
	if (played < 0) played = 0;
	return played * 1000 / clock->rate;
}

long long VP_clockMs(VpClock* clock, int queued, unsigned long now) {
	if (!clock->rate) return (long long)(now - clock->start);
	if (clock->holding) return clock->held_ms + (long long)(now - clock->held_at);
	if (queued < 0 || (clock->audio_done && queued == 0)) {
		// the sound ended or went: carry on from where it got to
		clock->held_ms = playedMs(clock, queued < 0 ? 0 : queued);
		clock->held_at = now;
		clock->holding = 1;
		return clock->held_ms;
	}
	return playedMs(clock, queued);
}

long long VP_queuedMs(const VpClock* clock, int queued) {
	if (!clock->rate || queued <= 0) return 0;
	return (long long)queued * 1000 / clock->rate;
}

///////////////////////////////////////
// pacing

int VP_pace(long long pts_ms, long long clock_ms, long long last_ms, int* wait_ms) {
	if (wait_ms) *wait_ms = 0;
	if (pts_ms > clock_ms) {
		long long wait = pts_ms - clock_ms;
		if (wait_ms) *wait_ms = (int)(wait < VP_SLICE_MS ? wait : VP_SLICE_MS);
		return VP_PACE_WAIT;
	}
	// a few milliseconds of slack, as timestamps in whole milliseconds round
	if (last_ms >= 0 && pts_ms - last_ms < VP_MIN_INTERVAL_MS - 3) return VP_PACE_DROP;
	return VP_PACE_SHOW;
}

int VP_late(VpLate* late, long long pts_ms, long long clock_ms) {
	if (clock_ms - pts_ms > VP_LATE_MS) late->in_a_row++;
	else late->in_a_row = 0;
	if (!late->degraded && late->in_a_row >= VP_LATE_IN_A_ROW) {
		late->degraded = 1;
		return 1;
	}
	return 0;
}

///////////////////////////////////////
// pictures

int VP_yuvMode(int full_range, int says_709, int says_601, int height) {
	if (full_range) return VP_YUV_FULL;
	if (says_709) return VP_YUV_BT709;
	if (says_601) return VP_YUV_BT601;
	return height >= 720 ? VP_YUV_BT709 : VP_YUV_BT601;
}

// Coefficients in 1024ths: luma's scale and offset, then red from Cr,
// green from Cb and Cr, and blue from Cb.
typedef struct Matrix {
	int ky, yoff, rv, gu, gv, bu;
} Matrix;

static const Matrix matrices[] = {
	[VP_YUV_BT601] = { 1192, 16, 1634, 401, 832, 2066 },
	[VP_YUV_BT709] = { 1192, 16, 1836, 218, 546, 2163 },
	[VP_YUV_FULL] = { 1024, 0, 1436, 352, 731, 1815 },
};

static inline unsigned char clamp8(int value) {
	return value < 0 ? 0 : value > 255 ? 255 : (unsigned char)value;
}

void VP_yuvToRgba(const unsigned char* y, int y_stride, const unsigned char* u, int u_stride,
		const unsigned char* v, int v_stride, int w, int h, int mode, unsigned char* out, int pitch) {
	if (mode < VP_YUV_BT601 || mode > VP_YUV_FULL) mode = VP_YUV_BT601;
	const Matrix* m = &matrices[mode];
	for (int row = 0; row < h; row++) {
		const unsigned char* yr = y + (size_t)row * y_stride;
		const unsigned char* ur = u + (size_t)(row >> 1) * u_stride;
		const unsigned char* vr = v + (size_t)(row >> 1) * v_stride;
		unsigned char* o = out + (size_t)row * pitch;
		for (int col = 0; col < w; col++) {
			int c = (yr[col] - m->yoff) * m->ky + 512;
			int d = ur[col >> 1] - 128;
			int e = vr[col >> 1] - 128;
			o[0] = clamp8((c + m->rv * e) >> 10);
			o[1] = clamp8((c - m->gu * d - m->gv * e) >> 10);
			o[2] = clamp8((c + m->bu * d) >> 10);
			o[3] = 255;
			o += 4;
		}
	}
}

///////////////////////////////////////
// sound

int VP_outChannels(int channels) {
	return channels == 1 ? 1 : 2;
}

static int sampleAt(int format, int channels, const unsigned char* const* planes, int frame, int channel) {
	switch (format) {
	case VP_PCM_S16: return ((const short*)planes[0])[frame * channels + channel];
	case VP_PCM_S16P: return ((const short*)planes[channel])[frame];
	case VP_PCM_S32: return ((const int*)planes[0])[frame * channels + channel] >> 16;
	case VP_PCM_S32P: return ((const int*)planes[channel])[frame] >> 16;
	case VP_PCM_FLT:
	case VP_PCM_FLTP: {
		float f = format == VP_PCM_FLT ? ((const float*)planes[0])[frame * channels + channel]
			: ((const float*)planes[channel])[frame];
		if (f != f) return 0; // NaN
		if (f >= 1.0f) return 32767;
		if (f <= -1.0f) return -32768;
		return (int)(f * 32767.0f);
	}
	}
	return 0;
}

int VP_pcmToS16(int format, int channels, const unsigned char* const* planes, int samples, short* out) {
	if (format < VP_PCM_S16 || format > VP_PCM_FLTP || channels < 1 || !planes || !out || samples < 0) return -1;
	int planar = format == VP_PCM_S16P || format == VP_PCM_S32P || format == VP_PCM_FLTP;
	int used = VP_outChannels(channels);
	for (int c = 0; c < (planar ? used : 1); c++) {
		if (!planes[c]) return -1;
	}
	for (int i = 0; i < samples; i++) {
		for (int c = 0; c < used; c++) {
			int channel = channels == 1 ? 0 : c;
			int s = sampleAt(format, channels, planes, i, channel);
			out[i * used + c] = (short)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
		}
	}
	return samples;
}
