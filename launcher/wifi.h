#ifndef WIFI_H
#define WIFI_H

// Wi-Fi for plugins, behind wifi_supported, wifi_enabled and
// wifi_connection in mosaic_plugin.h. Read only: nothing here turns Wi-Fi
// on or off or joins a network.
//
// Stock's connection call runs wpa_cli, ip and iw, so it can take a while.
// One worker thread, started by the first Wifi_connection while Wi-Fi is
// on, asks the source (stock's call, set by plugins.c) every few seconds
// while plugins keep asking, and Wifi_connection only copies the last
// answer. Until something asks, there's no thread and nothing runs.

#define WIFI_SSID_SIZE 64 // stock's SSID_MAX
#define WIFI_IP_SIZE 48

typedef struct WifiInfo {
	int connected;            // 1 when joined to a network
	char ssid[WIFI_SSID_SIZE]; // "" when not connected
	char ip[WIFI_IP_SIZE];     // "" when not connected or before an address
	int rssi;                 // dBm, 0 when not known
	int bars;                 // 1 to 3 as stock's status pill icon, 0 when not known
	int freq;                 // MHz, 0 when not known
	int link;                 // Mbit/s, 0 when not known
} WifiInfo;

// Fills out from stock's struct WIFI_connection's fields, where -1 means
// not known: everything zeroed unless valid, control characters in the
// names turned to '?', and the strength bars stock's status pill shows.
void Wifi_fill(WifiInfo* out, int valid, const char* ssid, const char* ip, int freq, int rssi, int link);

// Stock's status pill icon for a signal: 3 from -60 dBm, 2 from -70, 1
// below, and 0 for -1 and 0, which stock uses for not known.
int Wifi_bars(int rssi);

// A desktop test's stand-in for the device, as key=value lines: enabled,
// connected, ssid, ip, rssi, freq, link (with stock's -1 for not known) and
// delay_ms, how long the look takes. Unknown keys are skipped. Any of
// enabled, out and delay_ms may be NULL.
void Wifi_parseFake(const char* text, int* enabled, WifiInfo* out, int* delay_ms);

// Where the worker looks: fills out and returns 0. Called off the main
// thread, never with the lock held.
typedef int (*WifiSource)(WifiInfo* out);
void Wifi_setSource(WifiSource source);

// The user's Wi-Fi toggle, from the main thread (every tick, and before
// each plugin call), so the worker never reads stock's settings. Turning it
// off forgets the last answer; turning it on looks again at once.
void Wifi_setEnabled(int on);

// The connection as of the last look, into out. Returns 1 when connected,
// 0 when not (or while Wi-Fi is off), or -1 before the first look since
// Wi-Fi came on or there's no source. Starts the worker while Wi-Fi is on,
// and an ask after a quiet spell looks again at once.
int Wifi_connection(WifiInfo* out);

// Holds off looks while the device sleeps; 0 looks again at once.
void Wifi_pause(int paused);

// Stops the worker. A look still running is left to finish on its own,
// after at most a second's wait, and its answer is dropped. Safe to call
// when it never started, and more than once.
void Wifi_quit(void);

// Where the worker's messages go, eg. LOG_info. NULL drops them.
void Wifi_setLog(void (*log)(const char* message));

// How often it looks while asked, and how long without an ask before it
// stops looking, in milliseconds, for tests.
void Wifi_setLimits(int refresh_ms, int idle_ms);

#endif
