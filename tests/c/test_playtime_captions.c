// The play time captions plugin: its model on its own, then the plugin
// against a fake host.

#include <stdlib.h>
#include <time.h>
#include "check.h"
#include "words.h"
#include "../../plugins/playtime-captions/playtime_captions.c"

struct MosaicEntry {
	char path[256];
	int type;
};

// a local time, in whatever TZ is set
static long long at(int year, int month, int day, int hour, int minute) {
	struct tm t = { 0 };
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_isdst = -1;
	return (long long)mktime(&t);
}

static void useTZ(const char* tz) {
	setenv("TZ", tz, 1);
	tzset();
}

// 6 pixels a byte, so a caption's width is easy to work out
static int bytes(const char* text, void* ctx) {
	(void)ctx;
	return 6 * (int)strlen(text);
}

///////////////////////////////////////
// a fake host

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static char cfg[8][2][32];
static int cfg_count = 0;
static int badges_loaded = 0;
static char badge_unplayed[8] = "off", badge_tiers[8] = "off";
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	if (!strcmp(id, "row-badges")) {
		if (!badges_loaded) return fallback;
		if (!strcmp(key, "unplayed")) return badge_unplayed;
		if (!strcmp(key, "tiers")) return badge_tiers;
		return fallback;
	}
	for (int i = cfg_count - 1; i >= 0; i--) {
		if (!strcmp(cfg[i][0], key)) return cfg[i][1];
	}
	return fallback;
}
static void setCfg(const char* key, const char* value) {
	snprintf(cfg[cfg_count][0], sizeof(cfg[cfg_count][0]), "%s", key);
	snprintf(cfg[cfg_count][1], sizeof(cfg[cfg_count][1]), "%s", value);
	cfg_count++;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static int fakeScale(void) { return 2; }
static int fakeThemeColor(int index) {
	return index == MOSAIC_COLOR_HINT ? 0xFFFFFF : index == MOSAIC_COLOR_BACKGROUND ? 0x000000 : -1;
}
static int fakeTextWidth(int font, const char* text) {
	CHECK(font == MOSAIC_FONT_SMALL);
	return bytes(text, NULL);
}
static int fakeTextHeight(int font) {
	(void)font;
	return 20;
}
static char drawn[160];
static int drawn_x, drawn_y, drawn_count;
static unsigned int drawn_rgb;
static int fakeDrawText(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max_width) {
	(void)s;
	(void)max_width;
	CHECK(font == MOSAIC_FONT_SMALL);
	snprintf(drawn, sizeof(drawn), "%s", text);
	drawn_x = x;
	drawn_y = y;
	drawn_rgb = (unsigned int)(c.r << 16 | c.g << 8 | c.b);
	drawn_count++;
	return bytes(text, NULL);
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) {
	(void)s; (void)i; (void)r; (void)n;
	return 0;
}

// Tetris was played 2h 15m over 4 sessions three days ago, Unplayed never,
// and anything with "Unknown" in it has no history
static long long play_now;
static long long fakePlayTime(const char* path) {
	if (strstr(path, "Unknown")) return MOSAIC_PLAY_UNKNOWN;
	return strstr(path, "Tetris") ? 2 * 3600 + 15 * 60 : 0;
}
static long long fakeLastPlayed(const char* path) {
	if (strstr(path, "Unknown")) return MOSAIC_PLAY_UNKNOWN;
	return strstr(path, "Tetris") ? play_now - 3 * 86400 : 0;
}
static int fakePlayCount(const char* path) {
	if (strstr(path, "Unknown")) return MOSAIC_PLAY_UNKNOWN;
	return strstr(path, "Tetris") ? 4 : 0;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_path = fakeEntryPath,
	.entry_type = fakeEntryType,
	.theme_color = fakeThemeColor,
	.screen_scale = fakeScale,
	.text_width = fakeTextWidth,
	.text_height = fakeTextHeight,
	.draw_text = fakeDrawText,
	.decorate_cell = fakeDecorateCell,
	.play_time = fakePlayTime,
	.last_played = fakeLastPlayed,
	.play_count = fakePlayCount,
};

static void restart(void) {
	badges_known = 0;
	CHECK(init(&fake) == 0);
}

// draws entry in a row, returning how many captions were drawn
static int drawRow(const MosaicEntry* entry, MosaicRect rect, MosaicRect name) {
	MosaicRow row = { sizeof(MosaicRow), 0, 0, rect, name, 0 };
	drawn[0] = '\0';
	drawn_count = 0;
	CHECK(decorateRow(NULL, entry, &row) == 0);
	return drawn_count;
}

int main(void) {
	char out[160];
	useTZ("UTC0");

	// formats by name
	CHECK(PC_formatOf("relative") == PC_FORMAT_RELATIVE);
	CHECK(PC_formatOf("compact") == PC_FORMAT_COMPACT);
	CHECK(PC_formatOf("date") == PC_FORMAT_DATE);
	CHECK(PC_formatOf("bogus") == PC_FORMAT_RELATIVE);
	CHECK(PC_formatOf(NULL) == PC_FORMAT_RELATIVE);

	// durations
	PC_duration(0, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "<1m");
	PC_duration(59, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "<1m");
	PC_duration(60, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1m");
	PC_duration(45 * 60 + 30, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "45m");
	PC_duration(3600, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1h");
	PC_duration(2 * 3600 + 15 * 60 + 59, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "2h 15m");
	PC_duration(2 * 3600 + 15 * 60, PC_FORMAT_DATE, out, sizeof(out));
	CHECK_STR(out, "2h 15m");
	PC_duration(2 * 3600 + 15 * 60, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "2h");
	PC_duration(250LL * 3600 + 60, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "250h 1m");

	// days between, by the local calendar
	long long now = at(2026, 11, 8, 12, 0);
	CHECK(PC_daysBetween(now, now) == 0);
	CHECK(PC_daysBetween(at(2026, 11, 8, 0, 1), now) == 0);
	CHECK(PC_daysBetween(at(2026, 11, 7, 23, 59), now) == 1);
	CHECK(PC_daysBetween(at(2026, 11, 1, 12, 0), now) == 7);
	CHECK(PC_daysBetween(at(2025, 11, 8, 12, 0), now) == 365);
	CHECK(PC_daysBetween(now + 86400, now) == 0);
	// 23:00 and 01:00 UTC, either side of midnight, are 18:00 and 20:00 the
	// same day five hours behind
	useTZ("EST5");
	CHECK(PC_daysBetween(1794006000LL, 1794013200LL) == 0);
	useTZ("UTC0");
	CHECK(PC_daysBetween(1794006000LL, 1794013200LL) == 1);

	// last played
	PC_lastPlayed(now - 3600, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "today");
	PC_lastPlayed(now - 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "yesterday");
	PC_lastPlayed(now - 3 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "3 days ago");
	PC_lastPlayed(now - 7 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1 week ago");
	PC_lastPlayed(now - 20 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "2 weeks ago");
	PC_lastPlayed(now - 30 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1 month ago");
	PC_lastPlayed(now - 150 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "5 months ago");
	PC_lastPlayed(now - 365 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1 year ago");
	PC_lastPlayed(now - 800 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "2 years ago");
	PC_lastPlayed(now + 3600, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "today");
	PC_lastPlayed(now - 3600, now, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "today");
	PC_lastPlayed(now - 86400, now, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "1d");
	PC_lastPlayed(now - 20 * 86400, now, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "2w");
	PC_lastPlayed(now - 150 * 86400, now, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "5mo");
	PC_lastPlayed(now - 800 * 86400, now, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "2y");
	PC_lastPlayed(now - 86400, now, PC_FORMAT_DATE, out, sizeof(out));
	CHECK_STR(out, "yesterday");
	PC_lastPlayed(at(2026, 11, 5, 9, 0), now, PC_FORMAT_DATE, out, sizeof(out));
	CHECK_STR(out, "Nov 5");
	PC_lastPlayed(at(2025, 3, 14, 9, 0), now, PC_FORMAT_DATE, out, sizeof(out));
	CHECK_STR(out, "Mar 14, 2025");

	// sessions
	PC_sessions(1, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1 session");
	PC_sessions(4, PC_FORMAT_DATE, out, sizeof(out));
	CHECK_STR(out, "4 sessions");
	PC_sessions(4, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "4x");

	// in the words it's given, then English again
	static const char* const words[] = { "%lldh %lldm", "%lld h %lld min", "today", "hoy", "%id", "%i d",
		"Nov", "nov", "Never played", "Sin jugar", NULL };
	static const char* const plurals[] = { "%i day ago", "hace %i d\xc3\xad" "a", "hace %i d\xc3\xad" "as",
		"%i session", "%i sesi\xc3\xb3n", "%i sesiones", NULL };
	fake_words = words;
	fake_plurals = plurals;
	PCWords set = { fakeTr, fakePlural };
	PC_setWords(&set);
	PC_duration(2 * 3600 + 15 * 60, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "2 h 15 min");
	PC_lastPlayed(now - 3600, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "hoy");
	PC_lastPlayed(now - 86400, now, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "1 d");
	PC_lastPlayed(now - 3 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "hace 3 d\xc3\xad" "as");
	PC_lastPlayed(at(2026, 11, 5, 9, 0), now, PC_FORMAT_DATE, out, sizeof(out));
	CHECK_STR(out, "nov 5");
	PC_sessions(1, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1 sesi\xc3\xb3n");
	PC_sessions(4, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "4 sesiones");
	PCStats seen = { 2 * 3600 + 15 * 60, now - 86400 * 1, 4 };
	CHECK(PC_build(&seen, now, PC_PART_TIME | PC_PART_SESSIONS, PC_FORMAT_RELATIVE, 0, 3, out, sizeof(out)) == 2);
	CHECK_STR(out, "2 h 15 min \xC2\xB7 4 sesiones");
	PCStats unplayed = { 0, 0, 0 };
	CHECK(PC_build(&unplayed, now, PC_PART_TIME, PC_FORMAT_RELATIVE, 1, 3, out, sizeof(out)) == 1);
	CHECK_STR(out, "Sin jugar");
	PC_setWords(NULL);
	PC_lastPlayed(now - 3 * 86400, now, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "3 days ago");
	PC_sessions(1, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "1 session");

	// building the caption
	PCStats tetris = { 2 * 3600 + 15 * 60, now - 3 * 86400, 4 };
	PCStats never = { 0, 0, 0 };
	PCStats unknown = { -1, -1, -1 };
	int all = PC_PART_TIME | PC_PART_LAST | PC_PART_SESSIONS;
	CHECK(PC_build(&tetris, now, PC_PART_TIME | PC_PART_LAST, PC_FORMAT_RELATIVE, 0, 3, out, sizeof(out)) == 2);
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago");
	CHECK(PC_build(&tetris, now, all, PC_FORMAT_RELATIVE, 0, 3, out, sizeof(out)) == 3);
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago \xC2\xB7 4 sessions");
	CHECK(PC_build(&tetris, now, all, PC_FORMAT_COMPACT, 0, 3, out, sizeof(out)) == 3);
	CHECK_STR(out, "2h \xC2\xB7 3d \xC2\xB7 4x");
	CHECK(PC_build(&tetris, now, all, PC_FORMAT_RELATIVE, 0, 2, out, sizeof(out)) == 2);
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago");
	CHECK(PC_build(&tetris, now, PC_PART_LAST | PC_PART_SESSIONS, PC_FORMAT_RELATIVE, 0, 1, out, sizeof(out)) == 1);
	CHECK_STR(out, "3 days ago");
	CHECK(PC_build(&tetris, now, PC_PART_SESSIONS, PC_FORMAT_RELATIVE, 0, 3, out, sizeof(out)) == 1);
	CHECK_STR(out, "4 sessions");
	CHECK(PC_build(&tetris, now, 0, PC_FORMAT_RELATIVE, 1, 3, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(PC_build(&never, now, all, PC_FORMAT_RELATIVE, 0, 3, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(PC_build(&never, now, all, PC_FORMAT_RELATIVE, 1, 3, out, sizeof(out)) == 1);
	CHECK_STR(out, "Never played");
	CHECK(PC_build(&unknown, now, all, PC_FORMAT_RELATIVE, 1, 3, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	PCStats half_unknown = { 600, -1, 1 };
	CHECK(PC_build(&half_unknown, now, all, PC_FORMAT_RELATIVE, 1, 3, out, sizeof(out)) == 0);
	CHECK(PC_build(NULL, now, all, PC_FORMAT_RELATIVE, 1, 3, out, sizeof(out)) == 0);
	// played but with no session start: the parts that say something
	PCStats no_start = { 600, 0, 1 };
	CHECK(PC_build(&no_start, now, all, PC_FORMAT_RELATIVE, 0, 3, out, sizeof(out)) == 2);
	CHECK_STR(out, "10m \xC2\xB7 1 session");
	// a small buffer keeps whole parts
	char small[12];
	CHECK(PC_build(&tetris, now, all, PC_FORMAT_RELATIVE, 0, 3, small, sizeof(small)) == 1);
	CHECK_STR(small, "2h 15m");

	// RetroAchievements progress, last
	PC_achievements(3, 12, PC_FORMAT_RELATIVE, out, sizeof(out));
	CHECK_STR(out, "3/12 achievements");
	PC_achievements(3, 12, PC_FORMAT_COMPACT, out, sizeof(out));
	CHECK_STR(out, "3/12");
	PCStats cheevos = { 2 * 3600 + 15 * 60, now - 3 * 86400, 4, 3, 12 };
	int with = PC_PART_TIME | PC_PART_LAST | PC_PART_ACHIEVEMENTS;
	CHECK(PC_build(&cheevos, now, with, PC_FORMAT_RELATIVE, 0, 4, out, sizeof(out)) == 3);
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago \xC2\xB7 3/12 achievements");
	CHECK(PC_build(&cheevos, now, with, PC_FORMAT_COMPACT, 0, 4, out, sizeof(out)) == 3);
	CHECK_STR(out, "2h \xC2\xB7 3d \xC2\xB7 3/12");
	// not picked, or no total
	CHECK(PC_build(&cheevos, now, PC_PART_TIME, PC_FORMAT_RELATIVE, 0, 4, out, sizeof(out)) == 1);
	CHECK_STR(out, "2h 15m");
	CHECK(PC_build(&tetris, now, with, PC_FORMAT_RELATIVE, 0, 4, out, sizeof(out)) == 2);
	// on a game the tracker hasn't seen, after "Never played" when that's on
	PCStats unseen = { 0, 0, 0, 0, 12 };
	CHECK(PC_build(&unseen, now, with, PC_FORMAT_RELATIVE, 0, 4, out, sizeof(out)) == 1);
	CHECK_STR(out, "0/12 achievements");
	CHECK(PC_build(&unseen, now, with, PC_FORMAT_RELATIVE, 1, 4, out, sizeof(out)) == 2);
	CHECK_STR(out, "Never played \xC2\xB7 0/12 achievements");
	// with the history unknown, on its own
	PCStats unknown_cheevos = { -1, -1, -1, 5, 5 };
	CHECK(PC_build(&unknown_cheevos, now, with, PC_FORMAT_RELATIVE, 1, 4, out, sizeof(out)) == 1);
	CHECK_STR(out, "5/5 achievements");
	// compact first, then dropped first when there's no room
	PCOptions cheevo_opts = { with, PC_FORMAT_RELATIVE, 0 };
	CHECK(PC_fit(&cheevos, now, &cheevo_opts, 500, bytes, NULL, out, sizeof(out)) > 0);
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago \xC2\xB7 3/12 achievements");
	CHECK(PC_fit(&cheevos, now, &cheevo_opts, 120, bytes, NULL, out, sizeof(out)) == 96);
	CHECK_STR(out, "2h \xC2\xB7 3d \xC2\xB7 3/12");
	CHECK(PC_fit(&cheevos, now, &cheevo_opts, 95, bytes, NULL, out, sizeof(out)) == 48);
	CHECK_STR(out, "2h \xC2\xB7 3d");

	// fitting into a width: "2h 15m · 3 days ago" is 20 bytes, "2h 15m" 6,
	// "2h · 3d" 8 and "2h" 2
	PCOptions opts = { PC_PART_TIME | PC_PART_LAST, PC_FORMAT_RELATIVE, 0 };
	CHECK(PC_fit(&tetris, now, &opts, 500, bytes, NULL, out, sizeof(out)) == 120);
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago");
	CHECK(PC_fit(&tetris, now, &opts, 120, bytes, NULL, out, sizeof(out)) == 120);
	CHECK(PC_fit(&tetris, now, &opts, 119, bytes, NULL, out, sizeof(out)) == 48);
	CHECK_STR(out, "2h \xC2\xB7 3d");
	CHECK(PC_fit(&tetris, now, &opts, 47, bytes, NULL, out, sizeof(out)) == 36);
	CHECK_STR(out, "2h 15m");
	CHECK(PC_fit(&tetris, now, &opts, 35, bytes, NULL, out, sizeof(out)) == 12);
	CHECK_STR(out, "2h");
	CHECK(PC_fit(&tetris, now, &opts, 11, bytes, NULL, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(PC_fit(&tetris, now, &opts, 0, bytes, NULL, out, sizeof(out)) == 0);
	opts.format = PC_FORMAT_COMPACT;
	CHECK(PC_fit(&tetris, now, &opts, 47, bytes, NULL, out, sizeof(out)) == 12);
	CHECK_STR(out, "2h");
	CHECK(PC_fit(&never, now, &opts, 500, bytes, NULL, out, sizeof(out)) == 0);
	opts.never = 1;
	CHECK(PC_fit(&never, now, &opts, 500, bytes, NULL, out, sizeof(out)) == 72);
	CHECK_STR(out, "Never played");
	CHECK(PC_fit(&never, now, &opts, 71, bytes, NULL, out, sizeof(out)) == 0);
	CHECK(PC_fit(&unknown, now, &opts, 500, bytes, NULL, out, sizeof(out)) == 0);
	CHECK(PC_fit(&tetris, now, NULL, 500, bytes, NULL, out, sizeof(out)) == 0);

	// room in a row: on the name's line after the name and the reserve
	MosaicRect rect = { 10, 100, 600, 60 }, name = { 10, 100, 200, 60 }, room;
	CHECK(PC_room(&rect, &name, 50, 20, 8, 20, &room) == 1);
	CHECK(room.x == 260 && room.y == 120 && room.w == 342 && room.h == 20);
	// with no name, along the bottom, below the corner
	MosaicRect none = { 10, 100, 0, 60 };
	CHECK(PC_room(&rect, &none, 50, 20, 8, 20, &room) == 1);
	CHECK(room.x == 18 && room.y == 132 && room.w == 584 && room.h == 20);
	CHECK(PC_room(&rect, NULL, 50, 20, 8, 20, &room) == 1);
	CHECK(room.y == 132);
	CHECK(PC_room(&rect, &none, 50, 40, 8, 20, &room) == 0);
	// a name that fills the row, or text taller than it, leaves no room
	MosaicRect wide = { 10, 100, 560, 60 };
	CHECK(PC_room(&rect, &wide, 50, 20, 8, 20, &room) == 0);
	CHECK(PC_room(&rect, &name, 50, 20, 8, 61, &room) == 0);
	CHECK(PC_room(NULL, &name, 50, 20, 8, 20, &room) == 0);
	CHECK(PC_room(&rect, &name, 50, 20, 8, 0, &room) == 0);

	// muting a colour toward the background
	CHECK(PC_mute(0xFFFFFF, 0x000000, 40) == 0x999999);
	CHECK(PC_mute(0x000000, 0xFFFFFF, 40) == 0x666666);
	CHECK(PC_mute(0x123456, 0xABCDEF, 0) == 0x123456);
	CHECK(PC_mute(0x123456, 0xABCDEF, 100) == 0xABCDEF);
	CHECK(PC_mute(0x123456, 0xABCDEF, 150) == 0xABCDEF);

	///////////////////////////////////////
	// the plugin against the fake host

	play_now = (long long)time(NULL);
	MosaicEntry tetris_rom = { "/sd/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM };
	MosaicEntry unplayed_rom = { "/sd/Roms/GB/Unplayed.gb", MOSAIC_ENTRY_ROM };
	MosaicEntry unknown_rom = { "/sd/Roms/GB/Unknown.gb", MOSAIC_ENTRY_ROM };
	MosaicEntry tetris_dir = { "/sd/Roms/GB/Tetris", MOSAIC_ENTRY_DIR };
	MosaicRect row_rect = { 8, 40, 600, 60 }, row_name = { 8, 40, 150, 60 };

	// defaults: play time and last played, right-aligned on the name's line
	// with the row's 8 pixel margin, in the hint colour 40% toward the
	// background
	restart();
	CHECK(options.parts == (PC_PART_TIME | PC_PART_LAST));
	CHECK(options.format == PC_FORMAT_RELATIVE);
	CHECK(options.never == 0);
	CHECK(drawRow(&tetris_rom, row_rect, row_name) == 1);
	CHECK_STR(drawn, "2h 15m \xC2\xB7 3 days ago");
	CHECK(drawn_x == 8 + 600 - 8 - 120);
	CHECK(drawn_y == 40 + 20);
	CHECK(drawn_rgb == 0x999999);
	// nothing for games never played, unknown history or folders
	CHECK(drawRow(&unplayed_rom, row_rect, row_name) == 0);
	CHECK(drawRow(&unknown_rom, row_rect, row_name) == 0);
	CHECK(drawRow(&tetris_dir, row_rect, row_name) == 0);

	// a narrow row, with 48 pixels after the name's 12 pixel reserve, fits
	// both parts in the compact form, and 11 fits nothing
	MosaicRect narrow = { 8, 40, 150 + 12 + 8 + 48, 60 };
	CHECK(drawRow(&tetris_rom, narrow, row_name) == 1);
	CHECK_STR(drawn, "2h \xC2\xB7 3d");
	narrow.w = 150 + 12 + 8 + 11;
	CHECK(drawRow(&tetris_rom, narrow, row_name) == 0);

	// settings: every part, the date format, and "Never played"
	setCfg("sessions", "on");
	setCfg("format", "date");
	setCfg("never", "on");
	restart();
	CHECK(options.parts == (PC_PART_TIME | PC_PART_LAST | PC_PART_SESSIONS));
	CHECK(options.format == PC_FORMAT_DATE);
	CHECK(drawRow(&tetris_rom, row_rect, row_name) == 1);
	char want[200];
	PC_lastPlayed(play_now - 3 * 86400, play_now, PC_FORMAT_DATE, out, sizeof(out));
	snprintf(want, sizeof(want), "2h 15m \xC2\xB7 %s \xC2\xB7 4 sessions", out);
	CHECK_STR(drawn, want);
	CHECK(drawRow(&unplayed_rom, row_rect, row_name) == 1);
	CHECK_STR(drawn, "Never played");
	CHECK(drawRow(&unknown_rom, row_rect, row_name) == 0);
	setCfg("time", "off");
	setCfg("last", "off");
	setCfg("format", "compact");
	restart();
	CHECK(drawRow(&tetris_rom, row_rect, row_name) == 1);
	CHECK_STR(drawn, "4x");

	// with row-badges loaded and its tier dots on, room after the name for
	// six dots: 4 + 36 + 6 units at scale 2
	cfg_count = 0;
	badges_loaded = 1;
	snprintf(badge_tiers, sizeof(badge_tiers), "on");
	restart();
	narrow.w = 150 + 92 + 8 + 120;
	CHECK(drawRow(&tetris_rom, narrow, row_name) == 1);
	CHECK_STR(drawn, "2h 15m \xC2\xB7 3 days ago");
	CHECK(drawn_x == 150 + 92 + 8);
	narrow.w--;
	CHECK(drawRow(&tetris_rom, narrow, row_name) == 1);
	CHECK_STR(drawn, "2h \xC2\xB7 3d");
	// with no name, at the bottom right, below the corner dots
	MosaicRect cell = { 100, 200, 300, 60 }, no_name = { 100, 200, 0, 60 };
	CHECK(drawRow(&tetris_rom, cell, no_name) == 1);
	CHECK(drawn_x == 100 + 300 - 8 - 120);
	CHECK(drawn_y == 200 + 60 - 8 - 20);
	// a cell too short for both: 20 + 20 + 8 > 47
	cell.h = 47;
	CHECK(drawRow(&tetris_rom, cell, no_name) == 0);
	// just the unplayed dot: 4 + 4 + 6 units
	snprintf(badge_tiers, sizeof(badge_tiers), "off");
	snprintf(badge_unplayed, sizeof(badge_unplayed), "on");
	restart();
	narrow.w = 150 + 28 + 8 + 120;
	CHECK(drawRow(&tetris_rom, narrow, row_name) == 1);
	CHECK(drawn_x == 150 + 28 + 8);
	CHECK_STR(drawn, "2h 15m \xC2\xB7 3 days ago");

	// a launcher without the play history declines
	MosaicHost old = fake;
	old.play_time = NULL;
	CHECK(init(&old) == 1);
	CHECK(init(&fake) == 0);

	CHECK_DONE();
}
