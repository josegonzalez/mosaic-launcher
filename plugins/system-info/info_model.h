#ifndef INFO_MODEL_H
#define INFO_MODEL_H

// What system info works out apart from the host and SDL: reading
// /proc/meminfo and /proc/uptime, stock's version.txt and battery-logger's
// CSV, formatting the values, the battery graph's columns and the screen's
// layout, so the host tests can check it on its own.

#include <stddef.h>
#include "mosaic_plugin.h"

#define SI_REFRESH_MS 1000 // how often the values are read again while the screen shows
#define SI_TEXT_MAX 128    // one value's text
#define SI_SESSIONS_MAX 64 // the most recent battery-logger sessions the graph shows
#define SI_ADDRS_MAX 4     // IP addresses shown

// The rows, in the order they're drawn from the top.
enum SiRow {
	SI_BATTERY,
	SI_WIFI,
	SI_IP,
	SI_STORAGE,
	SI_MEMORY,
	SI_UPTIME,
	SI_PLATFORM,
	SI_VERSION,
	SI_ROWS,
};

// The rows' labels, as drawn.
const char* Si_label(int row);

// What every row shows, so a change can be spotted.
typedef struct SiValues {
	char text[SI_ROWS][SI_TEXT_MAX];
} SiValues;

// 1 when any row's text differs.
int Si_changed(const SiValues* a, const SiValues* b);

// A size such as "12.3 GB", "980 MB" or "1.2 TB", in 1024-byte units,
// with one decimal below 100 and none from 100.
void Si_formatSize(unsigned long long bytes, char* out, size_t size);

// Free and total storage, as "12.3 GB free of 59.4 GB", or "unavailable"
// when total is 0.
void Si_formatStorage(unsigned long long free_bytes, unsigned long long total_bytes, char* out, size_t size);

// The text of /proc/meminfo into total and available bytes. Available is
// MemAvailable, or MemFree plus Buffers and Cached on kernels without it.
// Returns 0, or -1 when there's no MemTotal or MemFree.
int Si_parseMeminfo(const char* text, unsigned long long* total, unsigned long long* available);

// Memory as "120 MB free of 1.0 GB", or "unavailable" when total is 0.
void Si_formatMemory(unsigned long long available, unsigned long long total, char* out, size_t size);

// The first number in /proc/uptime's text, in whole seconds, or -1.
long long Si_parseUptime(const char* text);

// Uptime as "3d 4h 12m", "4h 12m" or "12m", or "unavailable" below 0.
void Si_formatUptime(long long seconds, char* out, size_t size);

// The Wi-Fi row from the host's answers: "unavailable" without Wi-Fi
// (supported not 1), "off" while it's turned off, "checking" before the
// host has first looked (rc -1), "not connected", or the network as
// "Home Net, 192.168.1.50, -65 dBm", with "no address yet" before DHCP and
// the signal as "2 of 3 bars" when there's no dBm, or left out.
void Si_formatWifi(int supported, int enabled, int rc, const MosaicWifi* w, char* out, size_t size);

// The battery as "85%", "85%, charging", or "unavailable" when level is
// below 0.
void Si_formatBattery(int level, int charging, char* out, size_t size);

// Stock's version.txt, its release on the first line and its commit on
// the second, as "NextUI-20261013-0 (39a06de)", or the release alone.
// Returns 0, or -1 (and "unavailable") when there's no first line.
int Si_formatVersion(const char* text, char* out, size_t size);

// The IP addresses to show, one per interface, without loopback.
typedef struct SiAddrs {
	int count;
	char name[SI_ADDRS_MAX][32];
	char addr[SI_ADDRS_MAX][64];
} SiAddrs;

void Si_addrsInit(SiAddrs* a);

// Adds an interface's address, skipping loopback (lo, 127.x and ::1),
// IPv6 link-local ones (fe80:) and a second address on the same
// interface. Returns 1 when it's added.
int Si_addrsAdd(SiAddrs* a, const char* name, const char* addr);

// The addresses as "192.168.1.5 (wlan0), 10.0.0.2 (eth0)", or "none".
void Si_formatAddrs(const SiAddrs* a, char* out, size_t size);

// One battery-logger session: when it started, how long it ran and the
// level at either end.
typedef struct SiSession {
	long long start; // local time from the row's date, as mktime gives it
	long long seconds;
	int start_level, end_level;
} SiSession;

// The last SI_SESSIONS_MAX sessions, oldest first.
typedef struct SiHistory {
	int count;
	SiSession sessions[SI_SESSIONS_MAX];
} SiHistory;

void Si_historyInit(SiHistory* h);

// One CSV row of battery-logger's (date,game,path,seconds,start_percent,
// end_percent,...), with quoted fields. Returns 0, or -1 for the header or
// a row that isn't one.
int Si_parseRow(const char* line, SiSession* out);

// Adds every row in text, keeping the last SI_SESSIONS_MAX by start
// time. Returns how many were added.
int Si_historyAdd(SiHistory* h, const char* text);

// Total play time over the sessions.
long long Si_historySeconds(const SiHistory* h);

// The graph's columns: the sessions back to back by play time (the time
// between them left out), each column the level at its middle, from 0 to
// 100. Returns how many there are (columns, or 0 without sessions).
int Si_graph(const SiHistory* h, int columns, int* levels);

// How tall a level's bar is in a graph height pixels tall, at least 1 for
// a level above 0.
int Si_barHeight(int level, int height);

// "8 games, 3h 20m of play" over the graph, or "" without sessions.
void Si_graphCaption(const SiHistory* h, char* out, size_t size);

// Where things go on the screen.
typedef struct SiLayout {
	MosaicRect title;          // the title pill
	MosaicRect rows[SI_ROWS];  // each row, w 0 for one that doesn't fit
	int label_w;               // the labels' column
	MosaicRect caption;        // the graph's caption, w 0 without a graph
	MosaicRect graph;          // the graph, w 0 when it doesn't fit
} SiLayout;

// Lays the screen out in area around hole (the status pill and header
// widgets), at stock's scale: the title pill in the top left, up to rows
// rows under it row_h apart, and the graph with its caption, caption_h
// tall, in what's left above the hints when that's at least 20 units
// tall. label_w is the widest label. With rows 0 it's the graph's own
// page. Returns 0, or -1 when not even a row (or the graph) fits.
int Si_layout(const MosaicRect* area, const MosaicRect* hole, int scale, int title_w, int rows, int label_w,
	int row_h, int caption_h, SiLayout* out);

// The hints: B BACK alone when the graph fits under the rows, or with
// the graph on its own page B BACK on the left and A GRAPH, or A INFO on
// the graph's page, on the right.
void Si_hints(int own_page, int on_graph, MosaicHint left[1], int* left_count, MosaicHint right[1], int* right_count);

// The status pill and header widgets as one rect to stay clear of.
MosaicRect Si_hole(const MosaicRect* status, const MosaicRect* header);

// When it's time to read again: 1 at now >= *next, which moves on by
// period.
int Si_due(unsigned long* next, unsigned long now, unsigned long period);

#endif
