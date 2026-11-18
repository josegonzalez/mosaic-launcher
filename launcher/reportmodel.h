#ifndef REPORTMODEL_H
#define REPORTMODEL_H

// Bug report bundles without SDL or files: taking private details out of
// text, writing a zip, the settings and game list summary that go in one,
// and which reports to keep. report.c reads the card and writes the file.

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "pluginset.h"
#include "indexmodel.h"

#define REPORT_KEEP 10                  // reports kept in Reports/, the newest by name
#define REPORT_LOG_MAX (1024 * 1024)    // the last this many bytes of each log go in
#define REPORT_LOGS_MAX 64              // log files in one report
#define REPORT_LITERALS_MAX 64          // values removed wherever they're found
#define REPORT_PREFIX "mosaic-"

// A growing byte buffer. failed is set once it couldn't grow, after which
// nothing more is added.
typedef struct ReportBuf {
	char* data;
	size_t len, cap;
	int failed;
} ReportBuf;

int ReportBuf_add(ReportBuf* b, const void* data, size_t len);
int ReportBuf_printf(ReportBuf* b, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void ReportBuf_free(ReportBuf* b);

///////////////////////////////////////
// redaction

typedef struct Redactor {
	int keep_network; // 1 keeps IP and MAC addresses, Wi-Fi names and Syncthing device IDs
	char* literals[REPORT_LITERALS_MAX];
	int network[REPORT_LITERALS_MAX]; // 1 for a Wi-Fi name, kept with keep_network
	int count;
	int dropped; // values past REPORT_LITERALS_MAX, not removed
} Redactor;

void Redactor_init(Redactor* r, int keep_network);
void Redactor_free(Redactor* r);

// Adds a value to take out wherever it's found as a whole word, such as a
// secret setting's value (network 0) or the Wi-Fi network's name (network
// 1). A secret under 4 bytes or a name under 2 is skipped, and so is one
// already added. Returns 1 when it was added, 0 when skipped, or -1 past
// REPORT_LITERALS_MAX (counted in dropped).
int Redactor_add(Redactor* r, const char* value, int network);

// Appends text to out with private details replaced:
//
//   the added values                                <redacted>, or <wifi> for a Wi-Fi name
//   a URL's user and password, scheme://user:pw@    scheme://<credentials>@
//   the value after a key such as password, token,  key=<redacted>
//     secret, api_key, apikey, auth, user, pin, psk,
//     cookie or credential, with = or :, quoted or
//     not, in JSON and in <apikey>...</apikey> elements
//   Authorization: Bearer <token>                   Authorization: Bearer <redacted>
//
// and, unless keep_network is set:
//
//   IPv4 addresses, but not 127.x, 0.0.0.0 or 255.x  <ip>
//   IPv6 addresses, but not ::1 or ::                <ip>
//   MAC addresses                                    <mac>
//   ssid= values and system-info's "Wi-Fi: <name>,"  <wifi>
//   Syncthing device IDs                             <device-id>
//
// Returns how many things it replaced.
int Redact(const Redactor* r, const char* text, size_t len, ReportBuf* out);

///////////////////////////////////////
// zip

typedef struct ZipEntry {
	char* name;
	uint32_t crc, size, offset;
	uint16_t time, date;
} ZipEntry;

// Writes a zip of stored (uncompressed) files through write, which returns
// 0 when it wrote all of len.
typedef struct ZipWriter {
	int (*write)(void* ctx, const void* data, size_t len);
	void* ctx;
	uint32_t offset;
	ZipEntry* entries;
	int count, room;
	int failed;
} ZipWriter;

uint32_t Zip_crc32(uint32_t crc, const void* data, size_t len);
void Zip_init(ZipWriter* z, int (*write)(void* ctx, const void* data, size_t len), void* ctx);
// Adds a file, name with / between folders, modified at mtime (local
// time). Returns 0, or -1 once a write failed or out of memory.
int Zip_add(ZipWriter* z, const char* name, const void* data, size_t len, time_t mtime);
// Writes the central directory. Returns 0, or -1 when anything failed.
int Zip_finish(ZipWriter* z);
void Zip_free(ZipWriter* z);

///////////////////////////////////////
// what goes in

// A plugin's settings for the report, from its cfg's keys and values: each
// that m lists, in the cfg's order, but none it marks protected or secret,
// then a line naming those left out and one counting keys m doesn't list,
// which are left out too.
void Report_settings(const PluginManifest* m, char* const* keys, char* const* values, int count, ReportBuf* out);

// Adds the values of m's protected and secret text settings to r, so
// they're taken out of the logs too. Returns how many it added.
int Report_secrets(const PluginManifest* m, char* const* keys, char* const* values, int count, Redactor* r);

// The game list summary: each system folder with its games, then the
// totals. No file names. index may be NULL for one not read yet.
void Report_games(const CardIndex* index, ReportBuf* out);

// Where text read from partway into a log starts on a whole line: just
// after its first newline, or 0 when it has none.
size_t Report_lineStart(const char* data, size_t len);

// A report's name for a time, mosaic-YYYY-MM-DD-HHMMSS, without .zip.
void Report_name(const struct tm* when, char* out, size_t size);

// 1 for a name a report has: mosaic-<anything>.zip, not starting with a dot.
int Report_isReport(const char* name);

// Which of names (each a file in Reports/) to remove so keep reports are
// left: the oldest by name, ignoring files that aren't reports. Sets
// drop[i] to 1 for each and returns how many.
int Report_prune(char* const* names, int count, int keep, int* drop);

#endif
