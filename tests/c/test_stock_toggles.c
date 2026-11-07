// The stock toggles plugins can read and set, against stock's own config.c
// and a minuisettings.txt in a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "defines.h"
#include "mosaic_plugin.h"
#include "stocktoggles.h"
#include "check.h"

#define SETTINGS_FILE SHARED_USERDATA_PATH "/minuisettings.txt"

// each toggle and the line stock writes it as
static const struct {
	const char* name;
	const char* key;
} toggles[] = {
	{MOSAIC_TOGGLE_NOTIFY_MANUAL_SAVE, "notifyManualSave"},
	{MOSAIC_TOGGLE_NOTIFY_LOAD, "notifyLoad"},
	{MOSAIC_TOGGLE_NOTIFY_SCREENSHOT, "notifyScreenshot"},
	{MOSAIC_TOGGLE_NOTIFY_ADJUSTMENTS, "notifyAdjustments"},
	{MOSAIC_TOGGLE_SHOW_CLOCK, "showclock"},
	{MOSAIC_TOGGLE_SHOW_BATTERY_PERCENT, "batteryperc"},
	{MOSAIC_TOGGLE_HAPTICS, "haptics"},
};
#define TOGGLE_COUNT (int)(sizeof(toggles) / sizeof(toggles[0]))

static void writeSettings(const char* text) {
	FILE* file = fopen(SETTINGS_FILE, "w");
	fputs(text, file);
	fclose(file);
}

static char* readSettings(void) {
	static char text[8192];
	text[0] = '\0';
	FILE* file = fopen(SETTINGS_FILE, "r");
	if (!file) return NULL;
	size_t n = fread(text, 1, sizeof(text) - 1, file);
	text[n] = '\0';
	fclose(file);
	return text;
}

// the value stock saved for key, or -1 when the line isn't there
static int saved(const char* key) {
	const char* text = readSettings();
	if (!text) return -1;
	char want[64];
	snprintf(want, sizeof(want), "%s=", key);
	for (const char* line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
		if (!strncmp(line, want, strlen(want))) return atoi(line + strlen(want));
	}
	return -1;
}

int main(void) {
	mkdir(TEST_SDCARD_PATH, 0755);
	mkdir(TEST_SDCARD_PATH "/.userdata", 0755);
	mkdir(SHARED_USERDATA_PATH, 0755);
	// CFG_sync finds the file through the environment, CFG_init through defines.h
	setenv("SHARED_USERDATA_PATH", SHARED_USERDATA_PATH, 1);

	writeSettings("showclock=1\nclock24h=1\nbatteryperc=0\nhaptics=1\nnotifyManualSave=1\nnotifyLoad=0\n"
		"notifyScreenshot=1\nnotifyAdjustments=0\n");
	CFG_init(NULL, NULL);

	// reads what stock loaded
	CHECK(StockToggles_get(MOSAIC_TOGGLE_SHOW_CLOCK) == 1);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_SHOW_BATTERY_PERCENT) == 0);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_HAPTICS) == 1);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_NOTIFY_MANUAL_SAVE) == 1);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_NOTIFY_LOAD) == 0);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_NOTIFY_SCREENSHOT) == 1);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_NOTIFY_ADJUSTMENTS) == 0);

	// anything off the list is refused, and nothing is written
	char before_refused[8192];
	snprintf(before_refused, sizeof(before_refused), "%s", readSettings());
	const char* refused[] = {"show_recents", "wifi", "clock24h", "showclock", "SHOW_CLOCK", "show_clock ", "", NULL};
	for (int i = 0; i < (int)(sizeof(refused) / sizeof(refused[0])); i++) {
		CHECK(StockToggles_get(refused[i]) == -1);
		CHECK(StockToggles_set(refused[i], 1) == -1);
	}
	CHECK(StockToggles_set(MOSAIC_TOGGLE_SHOW_CLOCK, 2) == -1);
	CHECK(StockToggles_set(MOSAIC_TOGGLE_SHOW_CLOCK, -1) == -1);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_SHOW_CLOCK) == 1);
	CHECK_STR(readSettings(), before_refused);

	// setting goes through stock, which saves the whole file at once
	CHECK(StockToggles_set(MOSAIC_TOGGLE_SHOW_CLOCK, 0) == 0);
	CHECK(StockToggles_get(MOSAIC_TOGGLE_SHOW_CLOCK) == 0);
	CHECK(CFG_getShowClock() == false);
	CHECK(saved("showclock") == 0);
	CHECK(saved("clock24h") == 1);

	// each toggle flips its own line and no other toggle's
	for (int i = 0; i < TOGGLE_COUNT; i++) {
		int was = StockToggles_get(toggles[i].name);
		CHECK(was == 0 || was == 1);
		CHECK(StockToggles_set(toggles[i].name, !was) == 0);
		CHECK(StockToggles_get(toggles[i].name) == !was);
		CHECK(saved(toggles[i].key) == !was);
		for (int j = 0; j < TOGGLE_COUNT; j++) {
			if (j != i) CHECK(saved(toggles[j].key) == StockToggles_get(toggles[j].name));
		}
	}

	// a set that changes nothing leaves the file alone
	unlink(SETTINGS_FILE);
	int clock = StockToggles_get(MOSAIC_TOGGLE_SHOW_CLOCK);
	CHECK(StockToggles_set(MOSAIC_TOGGLE_SHOW_CLOCK, clock) == 0);
	CHECK(readSettings() == NULL);
	CHECK(StockToggles_set(MOSAIC_TOGGLE_SHOW_CLOCK, !clock) == 0);
	CHECK(saved("showclock") == !clock);

	// and stock reads back what was set
	int before[TOGGLE_COUNT];
	for (int i = 0; i < TOGGLE_COUNT; i++) before[i] = StockToggles_get(toggles[i].name);
	CFG_init(NULL, NULL);
	for (int i = 0; i < TOGGLE_COUNT; i++) CHECK(StockToggles_get(toggles[i].name) == before[i]);

	CHECK_DONE();
}
