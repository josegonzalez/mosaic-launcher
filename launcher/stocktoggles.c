#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "config.h"
#include "mosaic_plugin.h"
#include "stocktoggles.h"

typedef struct StockToggle {
	const char* name;
	bool (*get)(void);
	void (*set)(bool on);
} StockToggle;

// The status bar and vibration read these as they're used, so a change
// shows on the next frame and the next pulse without anything reapplied.
static const StockToggle toggles[] = {
	{MOSAIC_TOGGLE_NOTIFY_MANUAL_SAVE, CFG_getNotifyManualSave, CFG_setNotifyManualSave},
	{MOSAIC_TOGGLE_NOTIFY_LOAD, CFG_getNotifyLoad, CFG_setNotifyLoad},
	{MOSAIC_TOGGLE_NOTIFY_SCREENSHOT, CFG_getNotifyScreenshot, CFG_setNotifyScreenshot},
	{MOSAIC_TOGGLE_NOTIFY_ADJUSTMENTS, CFG_getNotifyAdjustments, CFG_setNotifyAdjustments},
	{MOSAIC_TOGGLE_SHOW_CLOCK, CFG_getShowClock, CFG_setShowClock},
	{MOSAIC_TOGGLE_SHOW_BATTERY_PERCENT, CFG_getShowBatteryPercent, CFG_setShowBatteryPercent},
	{MOSAIC_TOGGLE_HAPTICS, CFG_getHaptics, CFG_setHaptics},
};

static const StockToggle* find(const char* name) {
	if (!name) return NULL;
	for (size_t i = 0; i < sizeof(toggles) / sizeof(toggles[0]); i++) {
		if (!strcmp(toggles[i].name, name)) return &toggles[i];
	}
	return NULL;
}

int StockToggles_get(const char* name) {
	const StockToggle* toggle = find(name);
	if (!toggle) return -1;
	return toggle->get() ? 1 : 0;
}

int StockToggles_set(const char* name, int on) {
	const StockToggle* toggle = find(name);
	if (!toggle || (on != 0 && on != 1)) return -1;
	// every CFG_set* rewrites the whole file, so skip one that changes nothing
	if ((toggle->get() ? 1 : 0) != on) toggle->set(on == 1);
	return 0;
}
