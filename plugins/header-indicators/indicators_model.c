#include "indicators_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Hi_formatSize(unsigned long long bytes, char* out, size_t size) {
	static const char units[] = "KMGTP";
	if (!out || !size) return;
	if (!bytes) {
		snprintf(out, size, "0K");
		return;
	}
	double value = (double)bytes / 1024.0;
	int unit = 0;
	// 999.5 would round to 1000
	while (value >= 999.5 && units[unit + 1]) {
		value /= 1024.0;
		unit++;
	}
	if (value < 99.95) snprintf(out, size, "%.1f%c", value, units[unit]);
	else snprintf(out, size, "%.0f%c", value, units[unit]);
}

void HiFps_init(HiFps* f) {
	memset(f, 0, sizeof(*f));
	f->value = -1;
}

int HiFps_frame(HiFps* f, unsigned long now) {
	if (!f->started || now < f->last || now - f->last > HI_FPS_GAP_MS) {
		f->started = 1;
		f->start = f->last = now;
		f->frames = 0;
		return 0;
	}
	f->last = now;
	f->frames++;
	unsigned long elapsed = now - f->start;
	if (elapsed < HI_FPS_WINDOW_MS) return 0;

	int sample = (int)(((unsigned long)f->frames * 1000 + elapsed / 2) / elapsed);
	if (sample > 999) sample = 999;
	f->start = now;
	f->frames = 0;
	f->smooth = f->value < 0 ? sample : (f->smooth + sample) / 2;
	int value = (int)(f->smooth + 0.5);
	if (value == f->value) return 0;
	f->value = value;
	return 1;
}

// One line of /proc/net/tcp: "  0: 00000000:0016 00000000:0000 0A ...",
// the local address and port in hex, then the remote one, then the state,
// where 0A is listening.
static int lineListens(const char* line, int port) {
	char local[64], remote[64], st[8];
	if (sscanf(line, " %*[0-9]: %63s %63s %7s", local, remote, st) != 3) return 0;
	const char* colon = strrchr(local, ':');
	if (!colon || colon == local || !colon[1]) return 0;
	for (const char* c = local; c < colon; c++) {
		if (!isxdigit((unsigned char)*c)) return 0;
	}
	char* end;
	long p = strtol(colon + 1, &end, 16);
	if (*end) return 0;
	return p == port && !strcmp(st, "0A");
}

int Hi_listening(const char* text, int port) {
	if (!text) return 0;
	const char* line = text;
	while (*line) {
		const char* next = strchr(line, '\n');
		size_t n = next ? (size_t)(next - line) : strlen(line);
		char buf[256];
		if (n < sizeof(buf)) {
			memcpy(buf, line, n);
			buf[n] = '\0';
			if (lineListens(buf, port)) return 1;
		}
		if (!next) break;
		line = next + 1;
	}
	return 0;
}

static int fitWidth(const int widths[HI_COUNT], const int shown[HI_COUNT], int pad, int gap) {
	int total = 0, count = 0;
	for (int i = 0; i < HI_COUNT; i++) {
		if (!shown[i]) continue;
		total += widths[i];
		count++;
	}
	if (!count) return 0;
	return total + gap * (count - 1) + pad * 2;
}

int Hi_fit(const int widths[HI_COUNT], int pad, int gap, int max_width, int shown[HI_COUNT]) {
	static const int drop[HI_COUNT] = { HI_FPS, HI_PROFILE, HI_STORAGE, HI_SSH };
	for (int i = 0; i < HI_COUNT; i++) shown[i] = widths[i] > 0;
	int total = fitWidth(widths, shown, pad, gap);
	for (int i = 0; i < HI_COUNT && total > max_width; i++) {
		shown[drop[i]] = 0;
		total = fitWidth(widths, shown, pad, gap);
	}
	return total <= max_width ? total : 0;
}

int Hi_due(unsigned long* next, unsigned long now, unsigned long period) {
	if (now < *next) return 0;
	*next = now + period;
	return 1;
}

int Hi_parseToggle(const char* setting, int fallback) {
	if (!setting) return fallback;
	while (isspace((unsigned char)*setting)) setting++;
	size_t n = strlen(setting);
	while (n && isspace((unsigned char)setting[n - 1])) n--;
	if (n == 2 && !strncmp(setting, "on", 2)) return 1;
	if (n == 3 && !strncmp(setting, "off", 3)) return 0;
	return fallback;
}
