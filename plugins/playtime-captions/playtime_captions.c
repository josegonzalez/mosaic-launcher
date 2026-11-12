// Play time captions: a short caption at the right of a game's row, such
// as "2h 15m · 3 days ago", from stock's play tracker through the host's
// play history calls, on the built-in list and in plugin layouts' cells.
// It's in the theme's hint colour moved 40% toward its background, in the
// small font. The time, last and sessions settings pick the parts, the
// format setting how they read, and the never setting says "Never played"
// on games the tracker hasn't seen. Games with unknown history (no
// database or sqlite) get nothing.
//
// The achievements setting (off by default) adds the game's local
// RetroAchievements progress, as "3/12 achievements", from what minarch
// keeps on the card, through the host's achievements call. It shows once
// the launcher has worked out the game's hash, also on games the tracker
// hasn't seen or whose history is unknown, and not on games with no
// achievements cached.
//
// The caption sits on the name's line, right-aligned, and never over the
// name. When row-badges is loaded with its dots on, it leaves room for
// them after the name, and below them in a cell with no name. When the row
// is too narrow it tries the compact format, then drops parts from the
// end, and shows nothing when even one doesn't fit.
//
// The game switcher's captions are switcher-plus's, with its captions
// setting: it reads these settings with cfg_get while this plugin is
// loaded, and writes the caption with a copy of captions_model.c's
// formatter, so a change here goes there too. The built-in switcher gets
// none.

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "captions_model.h"
#include "mosaic_plugin.h"

#define ID "playtime-captions"
#define MUTE_PERCENT 40

static const MosaicHost* host;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static PCOptions options;
static int badges_known = 0;
static int badge_reserve = 0; // room after the name for row-badges' dots, in units of the screen's scale
static int badge_corner = 0;  // room at the top of a cell with no name for them

static int isOn(const char* key, const char* fallback) {
	return !strcmp(host->cfg_get(ID, key, fallback), "on");
}

// row-badges' dots go after the name: 4 units from it, up to 36 wide with
// the unplayed dot and five tier dots, or 4 with just the unplayed dot,
// or in the top right corner, 8 units deep, when there's no name or no
// room after it. Its settings are only there while it's loaded.
static void readBadges(void) {
	if (badges_known) return;
	badges_known = 1;
	int unplayed = !strcmp(host->cfg_get("row-badges", "unplayed", "off"), "on");
	int tiers = !strcmp(host->cfg_get("row-badges", "tiers", "off"), "on");
	int width = tiers ? 36 : unplayed ? 4 : 0;
	badge_reserve = width ? 4 + width + 6 : 6;
	badge_corner = width ? 10 : 0;
}

static int measure(const char* text, void* ctx) {
	(void)ctx;
	return host->text_width(MOSAIC_FONT_SMALL, text);
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	if (host->entry_type(entry) != MOSAIC_ENTRY_ROM) return 0;
	const char* path = host->entry_path(entry);
	if (!path) return 0;
	PCStats stats = { host->play_time(path), host->last_played(path), host->play_count(path), 0, 0 };
	if (options.parts & PC_PART_ACHIEVEMENTS) {
		MosaicAchievements progress = { .size = sizeof(MosaicAchievements) };
		if (host->achievements(path, &progress) == 1 && progress.total > 0) {
			stats.cheevos = progress.unlocked;
			stats.cheevos_total = progress.total;
		}
	}
	int known = stats.time >= 0 && stats.last >= 0 && stats.count >= 0;
	int unseen = known && !stats.count && !stats.time && !stats.last;
	if (!stats.cheevos_total && (!known || (unseen && !options.never))) return 0;

	readBadges();
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int text_h = host->text_height(MOSAIC_FONT_SMALL);
	MosaicRect room;
	if (!PC_room(&row->rect, &row->name, badge_reserve * scale, badge_corner * scale, 4 * scale, text_h, &room)) return 0;
	char caption[160];
	int w = PC_fit(&stats, (long long)time(NULL), &options, room.w, measure, NULL, caption, sizeof(caption));
	if (w <= 0) return 0;

	int hint = host->theme_color(MOSAIC_COLOR_HINT);
	int background = host->theme_color(MOSAIC_COLOR_BACKGROUND);
	if (hint < 0) hint = 0xFFFFFF;
	if (background < 0) background = 0x000000;
	int rgb = PC_mute(hint, background, MUTE_PERCENT);
	host->draw_text(screen, MOSAIC_FONT_SMALL, caption, room.x + room.w - w, room.y, mosaic_color((unsigned int)rgb, 255), 0);
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	static const PCWords words = { tr, trPlural };
	PC_setWords(&words);
	if (!MOSAIC_HOST_HAS(host, decorate_cell) || !MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, theme_color)) {
		host->log(ID, "this launcher can't decorate rows");
		return 1;
	}
	if (!MOSAIC_HOST_HAS(host, play_time)) {
		host->log(ID, "this launcher can't read the play history");
		return 1;
	}
	options.parts = (isOn("time", "on") ? PC_PART_TIME : 0) | (isOn("last", "on") ? PC_PART_LAST : 0) |
		(isOn("sessions", "off") ? PC_PART_SESSIONS : 0);
	options.format = PC_formatOf(host->cfg_get(ID, "format", "relative"));
	options.never = isOn("never", "off");
	if (isOn("achievements", "off")) {
		if (MOSAIC_HOST_HAS(host, achievements)) options.parts |= PC_PART_ACHIEVEMENTS;
		else host->log(ID, "this launcher can't read RetroAchievements progress");
	}
	static const char* names[] = { "relative", "compact", "date" };
	char message[200];
	snprintf(message, sizeof(message), "time %s, last %s, sessions %s, format %s, never %s, achievements %s",
		options.parts & PC_PART_TIME ? "on" : "off", options.parts & PC_PART_LAST ? "on" : "off",
		options.parts & PC_PART_SESSIONS ? "on" : "off", names[options.format], options.never ? "on" : "off",
		options.parts & PC_PART_ACHIEVEMENTS ? "on" : "off");
	host->log(ID, message);
	return 0;
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.decorate_row = decorateRow,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
