#include <stdio.h>
#include <string.h>
#include "simple_model.h"

int Simple_isOn(const char* setting) {
	return !setting || strcmp(setting, "off") != 0;
}

int Simple_isTools(const char* path, const char* sdcard, const char* platform) {
	if (!path || !sdcard || !platform) return 0;
	char tools[512];
	int len = snprintf(tools, sizeof(tools), "%s/Tools/%s", sdcard, platform);
	if (len < 0 || len >= (int)sizeof(tools)) return 0;
	if (strncmp(path, tools, len) != 0) return 0;
	return path[len] == '\0' || path[len] == '/';
}

int Simple_tiles(int hide_settings, const char** out) {
	static const char* const tools[] = { "Tools", "Pak Store" };
	static const char* const settings[] = { "Settings", "Wifi", "Bluetooth", "Reboot" };
	int count = 0;
	for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) out[count++] = tools[i];
	if (hide_settings) {
		for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) out[count++] = settings[i];
	}
	return count;
}

static int isBlank(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void Simple_cleanPin(const char* in, char out[SIMPLE_PIN_LEN]) {
	out[0] = '\0';
	if (!in) return;
	while (isBlank(*in)) in++;
	size_t len = strlen(in);
	while (len > 0 && isBlank(in[len - 1])) len--;
	if (len > SIMPLE_PIN_LEN - 1) len = SIMPLE_PIN_LEN - 1;
	memcpy(out, in, len);
	out[len] = '\0';
}

int Simple_leaveStep(const char* pin) {
	char clean[SIMPLE_PIN_LEN];
	Simple_cleanPin(pin, clean);
	return clean[0] ? SIMPLE_LEAVE_PIN : SIMPLE_LEAVE_CONFIRM;
}

int Simple_pinMatches(const char* pin, const char* typed) {
	if (!typed) return 0;
	char want[SIMPLE_PIN_LEN], got[SIMPLE_PIN_LEN];
	Simple_cleanPin(pin, want);
	Simple_cleanPin(typed, got);
	return want[0] && strcmp(want, got) == 0;
}
