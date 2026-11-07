#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "quick_model.h"

// The launcher's built-in tiles, top row first.
static const char* const builtins[] = {
	"Recents", "Collections", "Games", "Tools",
	"Settings", "Pak Store", "Wifi", "Bluetooth", "Sleep", "Reboot", "Poweroff",
};
#define TOP_COUNT 4
#define BUILTIN_COUNT (sizeof(builtins) / sizeof(builtins[0]))

// Copies [start, end) into out without the spaces around it.
static void copyTrimmed(char* out, size_t size, const char* start, const char* end) {
	while (start < end && (*start == ' ' || *start == '\t')) start++;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
	size_t len = (size_t)(end - start);
	if (len >= size) len = size - 1;
	memcpy(out, start, len);
	out[len] = '\0';
}

const char* Qmp_builtinName(const char* name, int top_only) {
	if (!name) return NULL;
	char trimmed[QMP_NAME_LEN];
	copyTrimmed(trimmed, sizeof(trimmed), name, name + strlen(name));
	size_t count = top_only ? TOP_COUNT : BUILTIN_COUNT;
	for (size_t i = 0; i < count; i++) {
		if (!strcasecmp(trimmed, builtins[i])) return builtins[i];
	}
	return NULL;
}

// Calls fn with each comma-separated part of setting, trimmed and not empty.
static void eachPart(const char* setting, void (*fn)(const char* part, void* data), void* data) {
	if (!setting) return;
	const char* p = setting;
	while (*p) {
		const char* end = strchr(p, ',');
		if (!end) end = p + strlen(p);
		char part[QMP_NAME_LEN * 2];
		copyTrimmed(part, sizeof(part), p, end);
		if (part[0]) fn(part, data);
		p = *end ? end + 1 : end;
	}
}

typedef struct LabelState {
	QmpLabel* out;
	int max;
	int count;
	int skipped;
} LabelState;

static void addLabel(const char* part, void* data) {
	LabelState* s = data;
	const char* eq = strchr(part, '=');
	const char* name = NULL;
	char label[QMP_NAME_LEN] = "";
	if (eq) {
		char key[QMP_NAME_LEN];
		copyTrimmed(key, sizeof(key), part, eq);
		name = Qmp_builtinName(key, 1);
		copyTrimmed(label, sizeof(label), eq + 1, eq + 1 + strlen(eq + 1));
	}
	if (!name || !label[0]) {
		s->skipped++;
		return;
	}
	for (int i = 0; i < s->count; i++) {
		if (!strcmp(s->out[i].name, name)) {
			snprintf(s->out[i].label, sizeof(s->out[i].label), "%s", label);
			return;
		}
	}
	if (s->count >= s->max) {
		s->skipped++;
		return;
	}
	snprintf(s->out[s->count].name, sizeof(s->out[s->count].name), "%s", name);
	snprintf(s->out[s->count].label, sizeof(s->out[s->count].label), "%s", label);
	s->count++;
}

int Qmp_parseLabels(const char* setting, QmpLabel* out, int max, int* skipped) {
	LabelState s = { out, max, 0, 0 };
	eachPart(setting, addLabel, &s);
	if (skipped) *skipped = s.skipped;
	return s.count;
}

typedef struct HiddenState {
	char (*out)[QMP_NAME_LEN];
	int max;
	int count;
} HiddenState;

static void addHidden(const char* part, void* data) {
	HiddenState* s = data;
	const char* builtin = Qmp_builtinName(part, 0);
	const char* from = builtin ? builtin : part;
	char name[QMP_NAME_LEN]; // a longer name is cut short
	copyTrimmed(name, sizeof(name), from, from + strlen(from));
	for (int i = 0; i < s->count; i++) {
		if (!strcmp(s->out[i], name)) return;
	}
	if (s->count >= s->max) return;
	memcpy(s->out[s->count], name, sizeof(name));
	s->count++;
}

int Qmp_parseHidden(const char* setting, char out[][QMP_NAME_LEN], int max) {
	HiddenState s = { out, max, 0 };
	eachPart(setting, addHidden, &s);
	return s.count;
}

// The names match MOSAIC_TOGGLE_* in mosaic_plugin.h, which this file
// doesn't include so it stays apart from the host.
static const QmpToggle toggles[] = {
	{ "notify_manual_save", "save", "Save notices" },
	{ "notify_load", "load", "Load notices" },
	{ "notify_screenshot", "screenshot", "Screenshot notices" },
	{ "notify_adjustments", "adjustments", "Adjustment notices" },
	{ "show_clock", "clock", "Clock" },
	{ "show_battery_percent", "battery", "Battery %" },
	{ "haptics", "haptics", "Haptics" },
};
#define TOGGLE_COUNT (sizeof(toggles) / sizeof(toggles[0]))

const QmpToggle* Qmp_toggleFor(const char* word) {
	if (!word) return NULL;
	char trimmed[QMP_NAME_LEN];
	copyTrimmed(trimmed, sizeof(trimmed), word, word + strlen(word));
	for (size_t i = 0; i < TOGGLE_COUNT; i++) {
		if (!strcasecmp(trimmed, toggles[i].word) || !strcasecmp(trimmed, toggles[i].name)) return &toggles[i];
	}
	return NULL;
}

typedef struct ToggleState {
	const QmpToggle** out;
	int max;
	int count;
	int skipped;
} ToggleState;

static void addToggle(const char* part, void* data) {
	ToggleState* s = data;
	const QmpToggle* toggle = Qmp_toggleFor(part);
	if (!toggle) {
		s->skipped++;
		return;
	}
	for (int i = 0; i < s->count; i++) {
		if (s->out[i] == toggle) return;
	}
	if (s->count >= s->max) return;
	s->out[s->count++] = toggle;
}

int Qmp_parseToggles(const char* setting, const QmpToggle** out, int max, int* skipped) {
	ToggleState s = { out, max, 0, 0 };
	eachPart(setting, addToggle, &s);
	if (skipped) *skipped = s.skipped;
	return s.count;
}

int Qmp_pakFromExe(const char* exe, char* out, size_t size) {
	if (!exe || exe[0] != '/') return 0;
	char path[1024];
	if (snprintf(path, sizeof(path), "%s", exe) >= (int)sizeof(path)) return 0;
	char* slash = strrchr(path, '/');
	if (!slash || strcmp(slash + 1, "nextui.elf") != 0) return 0;
	*slash = '\0'; // <pak>/bin/<platform>
	slash = strrchr(path, '/');
	if (!slash || !slash[1]) return 0;
	*slash = '\0'; // <pak>/bin
	slash = strrchr(path, '/');
	if (!slash || strcmp(slash + 1, "bin") != 0 || slash == path) return 0;
	*slash = '\0'; // <pak>
	return snprintf(out, size, "%s", path) < (int)size;
}

// Appends s single-quoted, with each ' written as '\''.
static int appendQuoted(char* out, size_t size, size_t* at, const char* s) {
	if (*at + 1 >= size) return 0;
	out[(*at)++] = '\'';
	for (const char* p = s; *p; p++) {
		const char* piece = *p == '\'' ? "'\\''" : NULL;
		size_t len = piece ? strlen(piece) : 1;
		if (*at + len >= size) return 0;
		if (piece) memcpy(out + *at, piece, len);
		else out[*at] = *p;
		*at += len;
	}
	if (*at + 1 >= size) return 0;
	out[(*at)++] = '\'';
	out[*at] = '\0';
	return 1;
}

int Qmp_manageCommand(const char* pak, const char* verb, char* out, size_t size) {
	if (!pak || !pak[0] || !verb || !size) return 0;
	char script[1024];
	if (snprintf(script, sizeof(script), "%s/bin/manage.sh", pak) >= (int)sizeof(script)) return 0;
	size_t at = 0;
	out[0] = '\0';
	if (!appendQuoted(out, size, &at, script)) return 0;
	if (at + 1 >= size) return 0;
	out[at++] = ' ';
	out[at] = '\0';
	return appendQuoted(out, size, &at, verb);
}
