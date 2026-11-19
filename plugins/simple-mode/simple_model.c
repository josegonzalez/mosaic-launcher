#include <stdio.h>
#include <stdlib.h>
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

///////////////////////////////////////
// the kid tag

int Kid_add(KidList* list, const char* path) {
	if (!path) return -1;
	if (list->count >= list->room) {
		int room = list->room ? list->room * 2 : 32;
		char** paths = realloc(list->paths, sizeof(char*) * (size_t)room);
		if (!paths) return -1;
		list->paths = paths;
		list->room = room;
	}
	char* copy = strdup(path);
	if (!copy) return -1;
	list->paths[list->count++] = copy;
	return 0;
}

void Kid_clear(KidList* list) {
	for (int i = 0; i < list->count; i++) free(list->paths[i]);
	list->count = 0;
}

void Kid_free(KidList* list) {
	Kid_clear(list);
	free(list->paths);
	memset(list, 0, sizeof(*list));
}

static int endsWith(const char* s, const char* suffix) {
	size_t len = strlen(s), n = strlen(suffix);
	return len >= n && !strcmp(s + len - n, suffix);
}

// The length of path's folder, up to its last slash, or -1 for none.
static int folderLen(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? (int)(slash - path) : -1;
}

// 1 when game is a .m3u or .cue in a folder named after it, and path is
// in that folder too.
static int besideDisc(const char* game, const char* path) {
	if (!endsWith(game, ".m3u") && !endsWith(game, ".cue")) return 0;
	int dir = folderLen(game);
	if (dir <= 0 || folderLen(path) != dir || strncmp(game, path, (size_t)dir) != 0) return 0;
	const char* name = game + dir + 1;
	size_t stem = strlen(name) - 4;
	int parent = dir - 1;
	while (parent >= 0 && game[parent] != '/') parent--;
	const char* folder = game + parent + 1;
	return (size_t)(game + dir - folder) == stem && strncmp(folder, name, stem) == 0;
}

int Kid_allows(const KidList* list, const char* path) {
	if (!list || !path || !path[0]) return 0;
	size_t len = strlen(path);
	for (int i = 0; i < list->count; i++) {
		const char* game = list->paths[i];
		if (!strncmp(game, path, len) && (game[len] == '\0' || game[len] == '/')) return 1;
		if (besideDisc(game, path)) return 1;
	}
	return 0;
}

// 1 for <sdcard>/<name>, and with inside, for anything in it.
static int under(const char* path, const char* sdcard, const char* name, int inside) {
	size_t card = strlen(sdcard), n = strlen(name);
	if (strncmp(path, sdcard, card) != 0 || path[card] != '/' || strncmp(path + card + 1, name, n) != 0) return 0;
	char next = path[card + 1 + n];
	return next == '\0' || (inside && next == '/');
}

int Kid_place(const char* path, int type, const char* sdcard) {
	if (!path || !sdcard || type == MOSAIC_ENTRY_TOGGLE) return KID_KEEP;
	if (type == MOSAIC_ENTRY_DIR) {
		if (under(path, sdcard, "Recently Played", 0)) return KID_RECENTS;
		if (under(path, sdcard, "Collections", 0)) return KID_COLLECTIONS;
		if (under(path, sdcard, "Collections", 1)) return endsWith(path, ".txt") ? KID_COLLECTION : KID_KEEP;
		return under(path, sdcard, "Roms", 1) ? KID_GAME : KID_KEEP;
	}
	return type == MOSAIC_ENTRY_ROM || type == MOSAIC_ENTRY_PAK ? KID_GAME : KID_KEEP;
}

int Kid_collectionAllows(const KidList* list, const char* text, const char* sdcard) {
	if (!text || !sdcard) return 0;
	char full[1024];
	while (*text) {
		const char* end = strchr(text, '\n');
		size_t len = end ? (size_t)(end - text) : strlen(text);
		size_t used = len;
		while (len > 0 && isBlank(text[len - 1])) len--;
		if (len > 0 && snprintf(full, sizeof(full), "%s%.*s", sdcard, (int)len, text) < (int)sizeof(full) && Kid_allows(list, full)) return 1;
		text += used;
		if (*text == '\n') text++;
	}
	return 0;
}

int Kid_tag(const char* setting, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!setting) return 0;
	while (isBlank(*setting)) setting++;
	size_t len = strlen(setting);
	while (len > 0 && isBlank(setting[len - 1])) len--;
	if (len >= size) len = size - 1;
	memcpy(out, setting, len);
	out[len] = '\0';
	return out[0] != '\0';
}

///////////////////////////////////////
// kiosk

int Kiosk_gate(int simple_on, int kiosk_on, int flags, int has_pin) {
	if (!simple_on || !kiosk_on || !(flags & MOSAIC_ACTION_EDITS)) return MOSAIC_GATE_RUN;
	return has_pin ? MOSAIC_GATE_PIN : MOSAIC_GATE_REFUSE;
}
