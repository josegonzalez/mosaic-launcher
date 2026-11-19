// Activity stats' export: see activity_report.h.

#include <string.h>

// English the plugin translates where it draws it, for make pot
#define TR_MARK(text) (text)
#include "activity_report.h"

#define tr AS_word

void AR_stamp(time_t now, char* out, size_t size) {
	struct tm tm;
	localtime_r(&now, &tm);
	if (!strftime(out, size, "%Y-%m-%d-%H%M%S", &tm) && size) out[0] = '\0';
}

void AR_csvField(FILE* f, const char* text) {
	if (!text) text = "";
	size_t len = strlen(text);
	int quote = strpbrk(text, ",\"\r\n") != NULL || (len && (text[0] == ' ' || text[len - 1] == ' '));
	if (!quote) {
		fputs(text, f);
		return;
	}
	fputc('"', f);
	for (const char* c = text; *c; c++) {
		if (*c == '"') fputc('"', f);
		fputc(*c, f);
	}
	fputc('"', f);
}

void AR_csvHeader(FILE* f) {
	fputs("started,start_unix,seconds,minutes,game,system,path\r\n", f);
}

void AR_csvRow(FILE* f, long long start, long long seconds, const char* game, const char* system, const char* path) {
	time_t t = (time_t)start;
	struct tm tm;
	localtime_r(&t, &tm);
	char when[32];
	if (!strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm)) when[0] = '\0';
	fprintf(f, "%s,%lld,%lld,%lld,", when, start, seconds, (seconds + 30) / 60);
	AR_csvField(f, game);
	fputc(',', f);
	AR_csvField(f, system);
	fputc(',', f);
	AR_csvField(f, path);
	fputs("\r\n", f);
}

void AR_html(FILE* f, const char* text) {
	if (!text) return;
	for (const char* c = text; *c; c++) {
		switch (*c) {
			case '&': fputs("&amp;", f); break;
			case '<': fputs("&lt;", f); break;
			case '>': fputs("&gt;", f); break;
			case '"': fputs("&quot;", f); break;
			case '\'': fputs("&#39;", f); break;
			default: fputc(*c, f);
		}
	}
}

static void cell(FILE* f, const char* tag, const char* text) {
	fprintf(f, "<%s>", tag);
	AR_html(f, text);
	fprintf(f, "</%s>", tag);
}

static void durationCell(FILE* f, long long seconds) {
	char text[32];
	AS_duration(seconds, text, sizeof(text));
	cell(f, "td", text);
}

static void heading(FILE* f, const char* text, const char* first, const char* second) {
	fputs("<h2>", f);
	AR_html(f, tr(text));
	fputs("</h2>\n<table>\n<tr>", f);
	cell(f, "th", tr(first));
	cell(f, "th", tr(second));
	fputs("</tr>\n", f);
}

void AR_htmlWrite(FILE* f, const ARSummary* s) {
	char stamp[64], text[512];
	struct tm tm;
	localtime_r(&s->now, &tm);
	if (!strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &tm)) stamp[0] = '\0';
	fputs("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>", f);
	AR_html(f, tr("Activity"));
	fputs("</title>\n<style>body{font-family:sans-serif;margin:1em}table{border-collapse:collapse;margin-bottom:1em}"
		"th,td{border:1px solid #999;padding:2px 8px;text-align:left}</style>\n</head>\n<body>\n<h1>", f);
	AR_html(f, tr("Activity"));
	fputs("</h1>\n<p>", f);
	AR_html(f, stamp);
	fputs("</p>\n<p>", f);
	AS_allTimeCaption(s->all, text, sizeof(text));
	AR_html(f, text);
	AS_longestCaption(s->all, text, sizeof(text));
	if (text[0]) {
		fputs("<br>", f);
		AR_html(f, text);
	}
	AS_hourCaption(s->all->hours, text, sizeof(text));
	fputs("<br>", f);
	AR_html(f, text);
	fputs("</p>\n", f);

	heading(f, TR_MARK("Time each day"), TR_MARK("Day"), TR_MARK("Time"));
	for (int i = 0; i < s->day_count; i++) {
		struct tm day;
		AS_dayOf(s->now, i, &day);
		char date[32];
		if (!strftime(date, sizeof(date), "%Y-%m-%d", &day)) date[0] = '\0';
		fputs("<tr>", f);
		cell(f, "td", date);
		if (s->days[i] < 0) cell(f, "td", "");
		else durationCell(f, s->days[i]);
		fputs("</tr>\n", f);
	}
	fputs("</table>\n", f);

	heading(f, TR_MARK("By hour of day"), TR_MARK("Hour"), TR_MARK("Time"));
	for (int h = 0; h < AS_HOURS; h++) {
		char hour[16];
		snprintf(hour, sizeof(hour), "%02i:00", h);
		fputs("<tr>", f);
		cell(f, "td", hour);
		durationCell(f, s->all->hours[h]);
		fputs("</tr>\n", f);
	}
	fputs("</table>\n", f);

	heading(f, TR_MARK("Time per system"), TR_MARK("System"), TR_MARK("Time"));
	for (int i = 0; i < s->system_count; i++) {
		fputs("<tr>", f);
		cell(f, "td", s->systems[i].label);
		durationCell(f, s->systems[i].seconds);
		fputs("</tr>\n", f);
	}
	fputs("</table>\n", f);

	heading(f, TR_MARK("Games"), TR_MARK("Game"), TR_MARK("Played"));
	for (int i = 0; i < s->game_count; i++) {
		const ASGame* game = &s->games[s->order ? s->order[i] : i];
		char sessions[32];
		AS_sessions(game->count > 0 ? game->count : 0, sessions, sizeof(sessions));
		char played[32];
		AS_duration(game->seconds, played, sizeof(played));
		snprintf(text, sizeof(text), "%s, %s", played, sessions);
		fputs("<tr>", f);
		cell(f, "td", game->name);
		cell(f, "td", text);
		fputs("</tr>\n", f);
	}
	fputs("</table>\n</body>\n</html>\n", f);
}
