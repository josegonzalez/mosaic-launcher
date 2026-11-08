#include <stdio.h>
#include <string.h>
#include "cfgfile.h"
#include "pin.h"

static int isBlank(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void Pin_clean(const char* in, char out[PIN_LEN]) {
	out[0] = '\0';
	if (!in) return;
	while (isBlank(*in)) in++;
	size_t len = strlen(in);
	while (len > 0 && isBlank(in[len - 1])) len--;
	if (len > PIN_LEN - 1) len = PIN_LEN - 1;
	memcpy(out, in, len);
	out[len] = '\0';
}

int Pin_read(const char* path, char out[PIN_LEN]) {
	out[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	char line[PIN_LEN * 2] = "";
	if (!fgets(line, sizeof(line), f)) line[0] = '\0';
	fclose(f);
	Pin_clean(line, out);
	return out[0] != '\0';
}

int Pin_isSet(const char* path) {
	char pin[PIN_LEN];
	return Pin_read(path, pin);
}

int Pin_matches(const char* path, const char* typed) {
	if (!typed) return 0;
	char want[PIN_LEN], got[PIN_LEN];
	if (!Pin_read(path, want)) return 0;
	Pin_clean(typed, got);
	return strcmp(want, got) == 0;
}

int Pin_write(const char* path, const char* pin) {
	char clean[PIN_LEN];
	Pin_clean(pin, clean);
	if (!clean[0]) {
		FILE* f = fopen(path, "r");
		if (!f) return 0;
		fclose(f);
		return remove(path) == 0 ? 0 : -1;
	}
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	int ok = fprintf(f, "%s\n", clean) > 0;
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
		return -1;
	}
	return 0;
}

int Pin_adopt(const char* path, const char* cfg_path) {
	char legacy[256];
	if (!Cfg_get(cfg_path, "pin", "", legacy, sizeof(legacy))) return 0;
	char clean[PIN_LEN];
	Pin_clean(legacy, clean);
	if (!clean[0]) return 0;
	int moved = 0;
	if (!Pin_isSet(path)) {
		if (Pin_write(path, clean) != 0) return 0; // keep the old copy until the new one is saved
		moved = 1;
	}
	Cfg_set(cfg_path, "pin", "");
	return moved;
}
