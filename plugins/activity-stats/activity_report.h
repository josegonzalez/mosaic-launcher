// Activity stats' export, without the host: the CSV of every session and
// the HTML summary written to Reports/, with their names and escaping. No
// SDL, so it's tested on the host.

#ifndef ACTIVITY_REPORT_H
#define ACTIVITY_REPORT_H

#include <stdio.h>
#include <time.h>
#include "activity_model.h"

// The local time now as the files' names carry it, "2026-11-19-140312",
// as bug-report names its zips.
void AR_stamp(time_t now, char* out, size_t size);

// A CSV field: as it is, or in double quotes, with each quote doubled,
// when it has a comma, a quote, a line break or a space at either end.
void AR_csvField(FILE* f, const char* text);

// The CSV's header, and one session's row: when it started in local time
// ("2026-11-19 14:03:12"), as a Unix time, its seconds and minutes, the
// game's name, its system and its full path.
void AR_csvHeader(FILE* f);
void AR_csvRow(FILE* f, long long start, long long seconds, const char* game, const char* system, const char* path);

// Text for HTML, with &, <, >, " and ' as entities.
void AR_html(FILE* f, const char* text);

// What the HTML summary shows.
typedef struct ARSummary {
	time_t now;
	const ASAllTime* all;
	const ASSystem* systems;
	int system_count;
	const ASGame* games; // most played first, by order
	const int* order;
	int game_count;
	const long long* days; // days[0] is today, a negative for a day not known
	int day_count;
} ARSummary;

// The HTML summary: the totals, then the charts as tables (the time each
// day, by hour of day and per system) and every game with its time and
// sessions.
void AR_htmlWrite(FILE* f, const ARSummary* summary);

#endif
