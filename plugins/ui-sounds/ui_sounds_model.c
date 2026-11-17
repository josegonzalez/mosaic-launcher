#include "ui_sounds_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"

static const char* const names[UIS_COUNT] = {
	"move", "select", "back", "error", "launch", "menu_open", "menu_close", "toast",
};

const char* UIS_name(int sound) {
	return sound >= 0 && sound < UIS_COUNT ? names[sound] : "";
}

int UIS_byName(const char* name) {
	for (int i = 0; name && i < UIS_COUNT; i++) {
		if (!strcmp(name, names[i])) return i;
	}
	return UIS_NONE;
}

#define BIT(button) MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_##button)

int UIS_forInput(unsigned int pressed, unsigned int repeated) {
	if (pressed & BIT(A)) return UIS_SELECT;
	if (pressed & BIT(B)) return UIS_BACK;
	unsigned int moves = BIT(UP) | BIT(DOWN) | BIT(LEFT) | BIT(RIGHT) | BIT(L1) | BIT(R1);
	if ((pressed | repeated) & moves) return UIS_MOVE;
	return UIS_NONE;
}

int UIS_forEvent(const char* name) {
	static const struct {
		const char* event;
		int sound;
	} events[] = {
		{ "launcher.launch", UIS_LAUNCH },
		{ "launcher.error", UIS_ERROR },
		{ "launcher.menu_open", UIS_MENU_OPEN },
		{ "launcher.menu_close", UIS_MENU_CLOSE },
		{ "launcher.toast", UIS_TOAST },
	};
	for (size_t i = 0; name && i < sizeof(events) / sizeof(events[0]); i++) {
		if (!strcmp(name, events[i].event)) return events[i].sound;
	}
	return UIS_NONE;
}

int UIS_volume(const char* setting) {
	if (!setting || !setting[0]) return UIS_VOLUME_DEFAULT;
	char* end = NULL;
	long volume = strtol(setting, &end, 10);
	if (*end != '\0' || volume < 0 || volume > 100) return UIS_VOLUME_DEFAULT;
	return (int)volume;
}

int UIS_audible(int volume, int system_volume) {
	return volume > 0 && system_volume != 0;
}

int UIS_pickFile(const char* theme_dir, const char* bundled_dir, int sound, int (*is_file)(const char* path), char* out,
		size_t size) {
	if (size) out[0] = '\0';
	if (sound < 0 || sound >= UIS_COUNT || !size) return UIS_FROM_NONE;
	char path[UIS_PATH_MAX];
	if (theme_dir && theme_dir[0]) {
		int n = snprintf(path, sizeof(path), "%s/%s.wav", theme_dir, names[sound]);
		if (n > 0 && (size_t)n < sizeof(path) && (size_t)n < size && is_file(path)) {
			snprintf(out, size, "%s", path);
			return UIS_FROM_THEME;
		}
	}
	if (bundled_dir && bundled_dir[0]) {
		int n = snprintf(path, sizeof(path), "%s/%s.wav", bundled_dir, names[sound]);
		if (n > 0 && (size_t)n < sizeof(path) && (size_t)n < size && is_file(path)) {
			snprintf(out, size, "%s", path);
			return UIS_FROM_BUNDLED;
		}
	}
	return UIS_FROM_NONE;
}

int UIS_themeDirOk(const char* dir) {
	return dir && dir[0] == '/' && strlen(dir) < UIS_PATH_MAX - 16;
}

static unsigned int le16(const unsigned char* p) {
	return (unsigned int)p[0] | (unsigned int)p[1] << 8;
}

static unsigned long le32(const unsigned char* p) {
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static int fail(char* why, size_t size, const char* message) {
	if (why && size) snprintf(why, size, "%s", message);
	return -1;
}

// One sample at p, of bits bits (float for 32-bit floats), as 16 bits.
static short sampleAt(const unsigned char* p, int bits, int is_float) {
	if (is_float) {
		float f;
		unsigned long raw = le32(p);
		unsigned int u = (unsigned int)raw;
		memcpy(&f, &u, sizeof(f));
		if (!(f == f)) f = 0; // NaN
		if (f > 1) f = 1;
		if (f < -1) f = -1;
		return (short)(f * 32767.0f);
	}
	switch (bits) {
		case 8: return (short)(((int)p[0] - 128) * 256);
		case 16: return (short)le16(p);
		case 24: return (short)(le16(p + 1));
		default: return (short)(le16(p + 2)); // 32
	}
}

int UIS_parseWav(const unsigned char* data, size_t size, UISClip* clip, char* why, size_t why_size) {
	memset(clip, 0, sizeof(*clip));
	if (!data || size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
		return fail(why, why_size, "isn't a WAV file");
	int have_fmt = 0;
	unsigned int format = 0, channels = 0, block = 0, bits = 0;
	unsigned long rate = 0;
	const unsigned char* samples = NULL;
	size_t samples_size = 0;
	size_t at = 12;
	while (at + 8 <= size) {
		const unsigned char* chunk = data + at;
		unsigned long len = le32(chunk + 4);
		size_t left = size - at - 8;
		if (!memcmp(chunk, "fmt ", 4)) {
			if (len < 16 || len > left) return fail(why, why_size, "has a short fmt chunk");
			format = le16(chunk + 8);
			channels = le16(chunk + 10);
			rate = le32(chunk + 12);
			block = le16(chunk + 20);
			bits = le16(chunk + 22);
			if (format == 0xFFFE) {
				// WAVE_FORMAT_EXTENSIBLE: the format is its sub-format GUID's first two bytes
				if (len < 40) return fail(why, why_size, "has a short fmt chunk");
				format = le16(chunk + 8 + 24);
			}
			have_fmt = 1;
		}
		else if (!memcmp(chunk, "data", 4)) {
			samples = chunk + 8;
			// a data chunk cut short, as by a writer that didn't finish, plays what's there
			samples_size = len < left ? (size_t)len : left;
			break;
		}
		if (len > left) break;
		at += 8 + (size_t)len + (len & 1);
	}
	if (!have_fmt) return fail(why, why_size, "has no fmt chunk");
	if (!samples) return fail(why, why_size, "has no data chunk");
	int is_float = format == 3;
	if (format != 1 && !(is_float && bits == 32)) return fail(why, why_size, "isn't PCM or 32-bit float");
	if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return fail(why, why_size, "isn't 8, 16, 24 or 32 bits");
	if (channels != 1 && channels != 2) return fail(why, why_size, "isn't mono or stereo");
	if (rate < UIS_RATE_MIN || rate > UIS_RATE_MAX) return fail(why, why_size, "isn't 8000 to 96000 Hz");
	if (block != channels * bits / 8) return fail(why, why_size, "has a block size that doesn't match its format");
	long frames = (long)(samples_size / block);
	if (frames <= 0) return fail(why, why_size, "has no sound");
	long max = (long)rate * UIS_MAX_MS / 1000;
	int cut = 0;
	if (frames > max) {
		frames = max;
		cut = 1;
	}
	short* out = malloc((size_t)frames * channels * sizeof(short));
	if (!out) return fail(why, why_size, "is too big for the memory there is");
	int bytes = (int)bits / 8;
	for (long i = 0; i < frames * (long)channels; i++) out[i] = sampleAt(samples + (size_t)i * bytes, (int)bits, is_float);
	clip->frames = out;
	clip->count = (int)frames;
	clip->rate = (int)rate;
	clip->channels = (int)channels;
	clip->cut = cut;
	return 0;
}

int UIS_loadWav(const char* path, UISClip* clip, char* why, size_t why_size) {
	memset(clip, 0, sizeof(*clip));
	struct stat st;
	if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return fail(why, why_size, "isn't a file");
	if (st.st_size > UIS_FILE_MAX) {
		char message[96];
		snprintf(message, sizeof(message), "is over the %d KiB a sound can be", UIS_FILE_MAX / 1024);
		return fail(why, why_size, message);
	}
	FILE* f = fopen(path, "rb");
	if (!f) return fail(why, why_size, "can't be read");
	unsigned char* data = malloc((size_t)st.st_size + 1);
	size_t got = data ? fread(data, 1, (size_t)st.st_size, f) : 0;
	fclose(f);
	if (!data) return fail(why, why_size, "is too big for the memory there is");
	int rc = UIS_parseWav(data, got, clip, why, why_size);
	free(data);
	return rc;
}

void UIS_scale(UISClip* clip, int volume) {
	if (!clip->frames || volume >= 100) return;
	if (volume < 0) volume = 0;
	long n = (long)clip->count * clip->channels;
	for (long i = 0; i < n; i++) clip->frames[i] = (short)((int)clip->frames[i] * volume / 100);
}

void UIS_freeClip(UISClip* clip) {
	free(clip->frames);
	memset(clip, 0, sizeof(*clip));
}
