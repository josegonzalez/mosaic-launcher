#include "launcher.h"

static int wifiSupported(void) { return WIFI_supported(); }
static int bluetoothSupported(void) { return BT_supported(); }
static int sleepSupported(void) { return PLAT_supportsDeepSleep() && !state.simple_mode; }

static int wifiOn(void) { return CFG_getWifi(); }
static int bluetoothOn(void) { return CFG_getBluetooth(); }

static void toggleWifi(void) { WIFI_enable(!WIFI_enabled()); }
static void toggleBluetooth(void) { BT_enable(!BT_enabled()); }
static void sleepNow(void) { PWR_sleep(); }
static void rebootNow(void) {
	Plugins_beforePoweroff();
	cleanupImageLoaderPool();  // Stop worker threads before SDL cleanup
	PWR_powerOff(1);
}
static void powerOffNow(void) {
	Plugins_beforePoweroff();
	cleanupImageLoaderPool();  // Stop worker threads before SDL cleanup
	PWR_powerOff(0);
}

static const QuickItem items[] = {
	{ .name = "Settings", .kind = QUICK_PAK, .asset = ASSET_SETTINGS },
	{ .name = "Pak Store", .kind = QUICK_PAK, .asset = ASSET_STORE },
	{ .name = "Wifi", .kind = QUICK_TOGGLE, .supported = wifiSupported, .isOn = wifiOn,
		.asset = ASSET_WIFI, .asset_off = ASSET_WIFI_OFF, .activate = toggleWifi },
	{ .name = "Bluetooth", .kind = QUICK_TOGGLE, .supported = bluetoothSupported, .isOn = bluetoothOn,
		.asset = ASSET_BLUETOOTH, .asset_off = ASSET_BLUETOOTH_OFF, .activate = toggleBluetooth },
	{ .name = "Sleep", .kind = QUICK_TOGGLE, .supported = sleepSupported,
		.asset = ASSET_SUSPEND, .activate = sleepNow },
	{ .name = "Reboot", .kind = QUICK_TOGGLE, .asset = ASSET_RESTART, .activate = rebootNow },
	{ .name = "Poweroff", .kind = QUICK_TOGGLE, .asset = ASSET_POWEROFF, .activate = powerOffNow },
};
#define ITEM_COUNT (sizeof(items) / sizeof(items[0]))

const QuickItem* QuickItem_find(const char* name) {
	if (!name) return NULL;
	for (size_t i = 0; i < ITEM_COUNT; i++) {
		if (!strcmp(items[i].name, name)) return &items[i];
	}
	return NULL;
}

Array* QuickItem_entries(void) {
	Array* entries = Array_new();
	for (size_t i = 0; i < ITEM_COUNT; i++) {
		const QuickItem* item = &items[i];
		if (item->kind == QUICK_PAK) {
			Entry* pak = entryFromPakName((char*)item->name);
			if (pak) Array_push(entries, pak);
		}
		else if (!item->supported || item->supported()) {
			Array_push(entries, Entry_new((char*)item->name, ENTRY_DIP));
		}
	}
	return entries;
}

int QuickItem_asset(const char* name) {
	const QuickItem* item = QuickItem_find(name);
	if (!item) return ASSET_WIFI_OFF;
	if (item->isOn && !item->isOn()) return item->asset_off;
	return item->asset;
}

int QuickItem_isOff(const Entry* entry) {
	const QuickTile* tile = QuickTiles_forEntry(entry);
	if (tile) return QuickTile_isOff(tile);
	const QuickItem* item = QuickItem_find(entry->name);
	return item && item->isOn && !item->isOn();
}

void QuickItem_activate(const Entry* entry) {
	const QuickTile* tile = QuickTiles_forEntry(entry);
	if (tile) {
		if (Action_available(tile->action)) Action_run(tile->action);
		return;
	}
	const QuickItem* item = QuickItem_find(entry->name);
	if (item && item->kind == QUICK_TOGGLE && item->activate) item->activate();
}
