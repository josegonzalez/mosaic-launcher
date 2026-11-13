#ifndef BATTERY_MODEL_H
#define BATTERY_MODEL_H

#include <stddef.h>

// Battery logger's model: checking readings, the session between the
// reading before a launch and the one on return, the CSV row it makes, and
// appending that row to the log. No SDL and no host here, so the host
// tests can check it on its own.

#define BL_PATH_LEN 1024
#define BL_ROW_LEN 4096

#define BL_HEADER "date,game,path,seconds,start_percent,end_percent,drain_percent,percent_per_hour,charged\n"

// The reading taken in pre_launch, kept in a file until post_return.
typedef struct BLStart {
	long long mono; // CLOCK_MONOTONIC seconds
	long long wall; // time(), for the row's date
	int level;      // 1 to 100
	int charging;   // 0 or 1
	char path[BL_PATH_LEN];
} BLStart;

enum BLResult {
	BL_OK,
	BL_BAD_READING, // the level isn't 1 to 100, or charging isn't 0 or 1
	BL_BAD_TIME,    // the clock went backwards
	BL_TOO_SHORT,   // shorter than the shortest session to log
	BL_CHARGED,     // it charged, and charged sessions aren't logged
};

typedef struct BLSession {
	BLStart start;
	long long seconds;
	int end_level;
	int charged; // charging at either reading, or the level went up
} BLSession;

// 1 for a reading worth logging: a level from 1 to 100 (a failed read of
// the battery's sysfs file comes back as 0, and no battery as -1) and a
// charging state of 0 or 1.
int BL_validReading(int level, int charging);

// The pending start as one line, and back. Format writes it to out and
// returns 0, or -1 when it doesn't fit or the path has a tab or newline.
// Parse returns 0, or -1 for anything Format wouldn't have written.
int BL_formatStart(const BLStart* start, char* out, size_t size);
int BL_parseStart(const char* text, BLStart* out);

// The session from start to the reading on return. Sessions shorter than
// min_seconds, and with log_charged 0 sessions that charged, aren't
// logged. Returns an enum BLResult, filling out for BL_OK.
int BL_session(const BLStart* start, int end_level, int end_charging, long long end_mono,
	long long min_seconds, int log_charged, BLSession* out);

// A game's name from its path: the file name without its extension.
void BL_gameName(const char* path, char* out, size_t size);

// One CSV field: quoted, with quotes doubled, when it has a comma, quote
// or leading or trailing space, and newlines turned into spaces so every
// row is one line. Returns 0, or -1 when it doesn't fit.
int BL_csvField(const char* in, char* out, size_t size);

// The session's row, ending in a newline. The date is the start's local
// time, the path is relative to sdcard when it's on it, and drain_percent
// and percent_per_hour are left empty for a session that charged. Returns
// 0, or -1 when it doesn't fit.
int BL_formatRow(const BLSession* session, const char* sdcard, char* out, size_t size);

// Appends row to the CSV at path, writing the header first into a new or
// empty file. A last row a power cut left without its newline is cut off
// first, so it can't run into the new one. When the file would grow past
// max_bytes it's renamed to old_path, replacing the one there, and a new
// file is started. The row goes in with one write and is synced before
// returning. Returns 0, or -1 on an error.
int BL_append(const char* path, const char* old_path, const char* row, long max_bytes);

///////////////////////////////////////
// the battery.history service

#define BL_HISTORY_MAX 64 // the most recent sessions it gives

// One logged session, as a row of the CSV has it.
typedef struct BLLogged {
	long long start; // the row's local date as a Unix time, as mktime reads it
	long long seconds;
	int start_level, end_level;
	int charged;
} BLLogged;

// The last BL_HISTORY_MAX sessions logged, oldest first.
typedef struct BLHistory {
	int count;
	BLLogged sessions[BL_HISTORY_MAX];
} BLHistory;

void BL_historyInit(BLHistory* h);

// One row as BL_formatRow writes it (quoted fields too). Returns 0, or -1
// for the header or anything else.
int BL_parseRow(const char* line, BLLogged* out);

// Adds every row in text, in start order, keeping the last
// BL_HISTORY_MAX. Returns how many were added.
int BL_historyAdd(BLHistory* h, const char* text);

// The service's reply for the last count sessions (every one for count
// below 1): count=<n>, seconds=<their play time in all>, then
// session.<i>=<start>,<seconds>,<start_percent>,<end_percent>,<charged 1
// or 0> for each, oldest first from 0. Returns 0, or -1 when it doesn't
// fit size.
int BL_reply(const BLHistory* h, int count, char* out, size_t size);

#endif
