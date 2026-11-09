// Home dashboard: a home screen in place of the root list, owning the home
// slot.
//
// The "mode" setting picks the home:
//   dashboard          tabs, switched with L1 and R1: Home (a continue card
//                      for the last game played and a top shelf of the
//                      games played before it), Pinned (favorites' games,
//                      while favorites is on) and Stats (play time from the
//                      play history)
//   minui              a plain list of the recently played games, then Games
//   retroachievements  recent and pinned games with RetroAchievements
//                      progress on the card
//   pico8              the Pico-8 carts in the card's Pico-8 folders, as a grid
//
// X closes the home screen on to the game list (the launcher's root list),
// MENU opens the quick menu and SELECT the game switcher, as on the root
// list. B does nothing, as at the root. A opens what's selected: a recent
// through recent_open, resuming its save state when it has one, anything
// else through open_path. Either way the launcher comes back to the home
// screen after the game.
//
// The screen's enter runs each time it opens again (after the quick menu,
// the switcher or a game), so the tab and selections live here, and the
// recents, favorites and carts are read again there.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "home_model.h"
#include "mosaic_plugin.h"

#define ID "home-dashboard"
#define SCREEN_ID "home-dashboard.main"
#define MAX_PATH_LEN 512
#define RECENTS_MAX 24
#define PINNED_MAX 64
#define CARTS_MAX 256
#define ENTRIES_MAX 512
#define MOST_PLAYED_MAX 5
#define ROWS_MAX (RECENTS_MAX + PINNED_MAX + 8)
#define FILE_MAX 65536

static const MosaicHost* host;

// settings
static int mode = HD_MODE_DASHBOARD;
static int pinned_on = 1;
static int stats_on = 1;
static int shelf_on = 1;
static char pico_tags[256];

static int favorites_loaded = 0; // favorites=on in plugins.txt
static char collection_path[MAX_PATH_LEN + 160];

typedef struct Item {
	char path[MAX_PATH_LEN];
	char name[256];
	int type;
	int recent; // its index in recents, or -1
	int available;
	int can_resume;
	char preview[MAX_PATH_LEN]; // its save state's preview, or ""
	const MosaicEntry* entry;   // for its art, from the pool, or NULL
} Item;

static Item recents[RECENTS_MAX];
static int recent_count = 0;
static Item pinned[PINNED_MAX];
static int pinned_count = 0;
static Item carts[CARTS_MAX];
static int cart_count = 0;

// The rows of the last list drawn, for what A opens. NULL item for Games.
typedef struct Row {
	const char* name;
	char right[96];
	int header; // a section's label, not selectable
	Item* item;
} Row;

static Row rows[ROWS_MAX];
static int row_count = 0;

// what's selected, kept while the screen is closed
static int tabs[HD_TABS_MAX];
static int tab_count = 1;
static int tab = HD_TAB_HOME;
static int home_focus = HD_FOCUS_CONTINUE;
static int shelf_first = 0;
static int list_selected = 0;
static int list_first = 0;
static int cart_selected = 0;
static int cart_first_row = 0;
static int shelf_cells = 1; // shelf cards in view at the last render
static int grid_cols = 1;   // carts per row at the last render

static int showing = 0; // the screen is open
static int waiting = 0; // art or achievements were still loading at the last render
static unsigned long asked_at = 0;

static void say(const char* message) {
	host->log(ID, message);
}

///////////////////////////////////////
// entries for art

// The launcher has no call to free an entry that isn't in a list, so these
// are made once per path and kept, up to ENTRIES_MAX for the run (the
// launcher starts again after every game).
static MosaicEntry* pool[ENTRIES_MAX];
static int pool_count = 0;

static const MosaicEntry* entryFor(const char* path, int type) {
	for (int i = 0; i < pool_count; i++) {
		const char* p = host->entry_path(pool[i]);
		if (p && !strcmp(p, path)) return pool[i];
	}
	if (pool_count >= ENTRIES_MAX) return NULL;
	MosaicEntry* entry = host->entry_new(path, type, NULL);
	if (entry) pool[pool_count++] = entry;
	return entry;
}

///////////////////////////////////////
// reading the card

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

// path's file name without its extension, or without ".p8.png" for a cart.
static void baseName(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	snprintf(out, size, "%s", slash ? slash + 1 : path);
	size_t len = strlen(out);
	if (len > 7 && !strcasecmp(out + len - 7, ".p8.png")) {
		out[len - 7] = '\0';
		return;
	}
	char* dot = strrchr(out, '.');
	if (dot && dot != out) *dot = '\0';
}

// The whole of a small file, or NULL. Free it.
static char* readFile(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return NULL;
	char* text = malloc(FILE_MAX);
	if (!text) {
		fclose(f);
		return NULL;
	}
	size_t n = fread(text, 1, FILE_MAX - 1, f);
	text[n] = '\0';
	fclose(f);
	return text;
}

static void loadRecents(void) {
	recent_count = 0;
	int n = host->recents_count();
	for (int i = 0; i < n && recent_count < RECENTS_MAX; i++) {
		const char* path = host->recent_path(i);
		if (!path || strlen(path) >= MAX_PATH_LEN) continue;
		Item* item = &recents[recent_count];
		memset(item, 0, sizeof(*item));
		snprintf(item->path, sizeof(item->path), "%s", path);
		item->recent = i;
		item->type = endsWith(path, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
		item->available = 1;
		MosaicRecent info = { .size = sizeof(MosaicRecent) };
		if (host->recent_info(i, &info) == 0) {
			snprintf(item->name, sizeof(item->name), "%s", info.name);
			item->type = info.type;
			item->available = info.available;
			item->can_resume = info.can_resume;
			snprintf(item->preview, sizeof(item->preview), "%s", info.preview);
		}
		if (!item->name[0]) baseName(path, item->name, sizeof(item->name));
		item->entry = entryFor(item->path, item->type);
		recent_count++;
	}
}

// favorites' list, as it keeps it: card-relative paths in its collection
static void loadPinned(void) {
	pinned_count = 0;
	if (!favorites_loaded) return;
	char* text = readFile(collection_path);
	if (!text) return;
	const char* cursor = text;
	char line[MAX_PATH_LEN];
	while (pinned_count < PINNED_MAX && HD_nextLine(&cursor, line, sizeof(line))) {
		Item* item = &pinned[pinned_count];
		memset(item, 0, sizeof(*item));
		if (snprintf(item->path, sizeof(item->path), "%s%s", host->sdcard_path, line) >= (int)sizeof(item->path)) continue;
		if (!exists(item->path)) continue;
		item->recent = -1;
		item->available = 1;
		item->type = endsWith(item->path, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
		item->entry = entryFor(item->path, item->type);
		const char* name = item->entry ? host->entry_name(item->entry) : NULL;
		if (name && name[0]) snprintf(item->name, sizeof(item->name), "%s", name);
		else baseName(item->path, item->name, sizeof(item->name));
		pinned_count++;
	}
	free(text);
}

static int byName(const void* a, const void* b) {
	return strcasecmp(((const Item*)a)->name, ((const Item*)b)->name);
}

// carts in Roms/<folder>/ whose tag is one of the pico8 tags
static void loadCarts(void) {
	cart_count = 0;
	char roms[MAX_PATH_LEN];
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	DIR* dh = opendir(roms);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL && cart_count < CARTS_MAX) {
		char tag[64];
		if (dp->d_name[0] == '.' || !HD_tagOf(dp->d_name, tag, sizeof(tag)) || !HD_tagIn(tag, pico_tags)) continue;
		char folder[MAX_PATH_LEN];
		if (snprintf(folder, sizeof(folder), "%s/%s", roms, dp->d_name) >= (int)sizeof(folder)) continue;
		DIR* fh = opendir(folder);
		if (!fh) continue;
		struct dirent* fp;
		while ((fp = readdir(fh)) != NULL && cart_count < CARTS_MAX) {
			if (!HD_isPicoCart(fp->d_name)) continue;
			Item* item = &carts[cart_count];
			memset(item, 0, sizeof(*item));
			if (snprintf(item->path, sizeof(item->path), "%s/%s", folder, fp->d_name) >= (int)sizeof(item->path)) continue;
			struct stat st;
			if (stat(item->path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
			item->recent = -1;
			item->available = 1;
			item->type = MOSAIC_ENTRY_ROM;
			baseName(item->path, item->name, sizeof(item->name));
			cart_count++;
		}
		closedir(fh);
	}
	closedir(dh);
	qsort(carts, (size_t)cart_count, sizeof(Item), byName);
	// entries once sorted, as each points into the pool, not the array
	for (int i = 0; i < cart_count; i++) carts[i].entry = entryFor(carts[i].path, carts[i].type);
}

///////////////////////////////////////
// opening things

static void openItem(Item* item) {
	if (!item) return;
	int opened = item->recent >= 0 ? host->recent_open(item->recent, 1) : host->open_path(item->path, item->type);
	char message[MAX_PATH_LEN + 32];
	snprintf(message, sizeof(message), "open %s %i", item->path, opened);
	say(message);
	if (!opened) {
		char shown[300];
		snprintf(shown, sizeof(shown), "Couldn't open %s", item->name);
		host->toast(shown);
	}
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

#define C_MAIN() themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255)
#define C_TEXT() themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 255)
#define C_SELECTED() themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000, 255)
#define C_HINT() themeColor(MOSAIC_COLOR_HINT, 0x999999, 255)
#define C_ACCENT() themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, 255)

// a card that isn't selected: the list text over the background, faintly
static MosaicColor dimCard(void) {
	return themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 28);
}

// the first letter of name, centred on a tile in the accent colour
static void drawLetter(MosaicSurface* screen, const MosaicRect* box, const char* name, int radius) {
	host->fill_round_rect(screen, box, radius, C_ACCENT());
	char letter[8] = { 0 };
	if (name && name[0]) {
		// one character, however many bytes it is
		int n = 1;
		while (name[n] && ((unsigned char)name[n] & 0xC0) == 0x80 && n < 4) n++;
		memcpy(letter, name, (size_t)n);
	}
	int w = host->text_width(MOSAIC_FONT_LARGE, letter);
	int h = host->text_height(MOSAIC_FONT_LARGE);
	host->draw_text(screen, MOSAIC_FONT_LARGE, letter, box->x + (box->w - (w > 0 ? w : 0)) / 2,
		box->y + (box->h - h) / 2, C_TEXT(), box->w);
}

// An item's art fitted into box: its save state's preview first when
// preview is set, then its thumbnail, then its box, or its first letter.
static void drawArt(MosaicSurface* screen, const Item* item, const MosaicRect* box, int preview, int radius) {
	const MosaicSurface* image = NULL;
	int got = MOSAIC_ART_MISSING;
	if (preview && item->preview[0]) got = host->art_load(item->preview, &image);
	if (got != MOSAIC_ART_READY && item->entry) {
		int thumb = host->art_get(item->entry, MOSAIC_ART_THUMBNAIL, &image);
		got = thumb;
		if (thumb == MOSAIC_ART_MISSING) got = host->art_get(item->entry, MOSAIC_ART_BOX, &image);
	}
	if (got == MOSAIC_ART_LOADING) waiting = 1;
	int w = 0, h = 0;
	MosaicRect fit;
	if (got == MOSAIC_ART_READY && image && host->surface_size(image, &w, &h) == 0 && HD_fit(w, h, box, &fit) == 0) {
		host->blit(screen, image, NULL, &fit);
		return;
	}
	drawLetter(screen, box, item->name, radius);
}

// Draws the hints, X for the games and A labelled open (NULL for none),
// and returns the y they start at.
static int drawHints(MosaicSurface* screen, const char* open) {
	int scale = host->screen_scale();
	int top = host->screen_height() - 40 * scale;
	MosaicRect drawn;
	MosaicHint games[] = { { MOSAIC_BUTTON_X, "GAMES" } };
	if (host->draw_hints(screen, MOSAIC_HINTS_LEFT, games, 1, &drawn) == 0) top = drawn.y;
	if (open) {
		MosaicHint a[] = { { MOSAIC_BUTTON_A, open } };
		if (host->draw_hints(screen, MOSAIC_HINTS_RIGHT, a, 1, &drawn) == 0 && drawn.y < top) top = drawn.y;
	}
	return top;
}

// The tab bar, or the mode's title when it has no tabs.
static void drawTabs(MosaicSurface* screen, const MosaicRect* bar) {
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int text_h = host->text_height(MOSAIC_FONT_LARGE);
	int y = bar->y + (bar->h - text_h) / 2;
	if (mode != HD_MODE_DASHBOARD) {
		const char* title = mode == HD_MODE_MINUI ? "Recently played"
			: mode == HD_MODE_RETROACHIEVEMENTS ? "Achievements" : "Pico-8";
		host->draw_text(screen, MOSAIC_FONT_LARGE, title, bar->x + pad, y, C_TEXT(), bar->w - pad);
		return;
	}
	int x = bar->x;
	int right = bar->x + bar->w;
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	if (tab_count > 1) {
		int w = host->draw_text(screen, MOSAIC_FONT_SMALL, "L1", x, bar->y + (bar->h - small_h) / 2, C_HINT(), right - x);
		if (w > 0) x += w + pad / 2;
	}
	for (int i = 0; i < tab_count && x < right; i++) {
		const char* label = HD_tabLabel(tabs[i]);
		int w = host->text_width(MOSAIC_FONT_LARGE, label) + 2 * pad;
		if (x + w > right) w = right - x;
		MosaicRect pill = { x, bar->y, w, bar->h };
		int selected = tabs[i] == tab;
		if (selected) host->fill_round_rect(screen, &pill, bar->h / 2, C_MAIN());
		host->draw_text(screen, MOSAIC_FONT_LARGE, label, x + pad, y, selected ? C_SELECTED() : C_TEXT(), w - 2 * pad);
		x += w;
	}
	if (tab_count > 1 && x + pad / 2 < right) {
		host->draw_text(screen, MOSAIC_FONT_SMALL, "R1", x + pad / 2, bar->y + (bar->h - small_h) / 2, C_HINT(),
			right - x - pad / 2);
	}
}

// A line of text centred in rect, in the hint colour.
static void drawMessage(MosaicSurface* screen, const MosaicRect* rect, const char* message) {
	int w = host->text_width(MOSAIC_FONT_LARGE, message);
	int h = host->text_height(MOSAIC_FONT_LARGE);
	int x = rect->x + (rect->w - (w > 0 ? w : 0)) / 2;
	host->draw_text(screen, MOSAIC_FONT_LARGE, message, x < rect->x ? rect->x : x, rect->y + (rect->h - h) / 2,
		C_HINT(), rect->w);
}

// rows[] as list rows in rect, the selected one (-1 for none) on a pill,
// scrolled to keep it in view.
static void drawRows(MosaicSurface* screen, const MosaicRect* rect, int selected, int* first) {
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int row_h = 30 * scale;
	int fit = rect->h / row_h;
	if (fit < 1) return;
	*first = selected >= 0 ? HD_window(selected, row_count, fit, *first) : 0;
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	for (int i = *first; i < row_count && i < *first + fit; i++) {
		Row* row = &rows[i];
		MosaicRect r = { rect->x, rect->y + (i - *first) * row_h, rect->w, row_h };
		int on = i == selected;
		if (on) host->fill_round_rect(screen, &r, row_h / 2, C_MAIN());
		int right_w = 0;
		if (row->right[0]) {
			right_w = host->text_width(MOSAIC_FONT_SMALL, row->right);
			if (right_w > r.w / 2) right_w = r.w / 2;
			host->draw_text(screen, MOSAIC_FONT_SMALL, row->right, r.x + r.w - pad - right_w, r.y + (row_h - small_h) / 2,
				on ? C_SELECTED() : C_HINT(), right_w);
		}
		int font = row->header ? MOSAIC_FONT_SMALL : MOSAIC_FONT_LARGE;
		int text_h = row->header ? small_h : large_h;
		int name_w = r.w - 2 * pad - (right_w ? right_w + pad : 0);
		host->draw_text(screen, font, row->name, r.x + pad, r.y + (row_h - text_h) / 2,
			on ? C_SELECTED() : row->header ? C_HINT() : C_TEXT(), name_w);
	}
}

static Row* addRow(const char* name, Item* item) {
	if (row_count >= ROWS_MAX) return NULL;
	Row* row = &rows[row_count++];
	memset(row, 0, sizeof(*row));
	row->name = name;
	row->item = item;
	return row;
}

static int historyKnown(void) {
	return host->play_time_today() >= 0;
}

// The home tab: the continue card and the top shelf.
static void drawHome(MosaicSurface* screen, const HDLayout* layout) {
	int scale = host->screen_scale();
	int radius = 8 * scale;
	int shelf_count = recent_count > 1 ? recent_count - 1 : 0;
	if (home_focus >= shelf_count) home_focus = shelf_count ? shelf_count - 1 : HD_FOCUS_CONTINUE;
	int on = home_focus == HD_FOCUS_CONTINUE;
	host->fill_round_rect(screen, &layout->card, radius, on ? C_MAIN() : dimCard());
	MosaicColor text = on ? C_SELECTED() : C_TEXT();
	MosaicColor muted = on ? C_SELECTED() : C_HINT();
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	const MosaicRect* t = &layout->text;
	if (!recent_count) {
		host->draw_text(screen, MOSAIC_FONT_LARGE, "Nothing played yet", t->x, t->y + (t->h - large_h) / 2, muted, t->w);
	} else {
		Item* item = &recents[0];
		drawArt(screen, item, &layout->art, 1, radius);
		int y = t->y;
		host->draw_text(screen, MOSAIC_FONT_SMALL, "CONTINUE", t->x, y, muted, t->w);
		y += small_h + 4 * scale;
		host->draw_text(screen, MOSAIC_FONT_LARGE, item->name, t->x, y, text, t->w);
		y += large_h + 4 * scale;
		char line[128] = "";
		if (!item->available) snprintf(line, sizeof(line), "%s", "Its emulator isn't installed");
		else if (historyKnown()) {
			HD_playLine(host->play_time(item->path), host->last_played(item->path), (long long)time(NULL), line, sizeof(line));
		}
		if (line[0]) host->draw_text(screen, MOSAIC_FONT_SMALL, line, t->x, y, muted, t->w);
	}

	shelf_cells = layout->cells;
	if (!layout->cells) return;
	shelf_first = HD_window(home_focus < 0 ? shelf_first : home_focus, shelf_count, layout->cells, shelf_first);
	for (int k = 0; k < layout->cells && shelf_first + k < shelf_count; k++) {
		int index = shelf_first + k;
		Item* item = &recents[index + 1];
		MosaicRect cell = { layout->shelf.x + k * (layout->cell + layout->gap), layout->shelf.y + 2 * scale, layout->cell,
			layout->cell };
		int selected = index == home_focus;
		if (selected) {
			MosaicRect frame = { cell.x - 2 * scale, cell.y - 2 * scale, cell.w + 4 * scale, cell.h + 4 * scale };
			host->fill_round_rect(screen, &frame, radius, C_MAIN());
		}
		drawArt(screen, item, &cell, 0, radius);
		char name[128];
		int w = host->text_truncate(MOSAIC_FONT_SMALL, item->name, cell.w, name, sizeof(name));
		host->draw_text(screen, MOSAIC_FONT_SMALL, name, cell.x + (cell.w - (w > 0 ? w : 0)) / 2,
			cell.y + cell.h + 4 * scale, selected ? C_TEXT() : C_HINT(), cell.w);
	}
}

static void drawPinned(MosaicSurface* screen, const HDLayout* layout) {
	row_count = 0;
	for (int i = 0; i < pinned_count; i++) addRow(pinned[i].name, &pinned[i]);
	if (!row_count) {
		drawMessage(screen, &layout->content, "No favorites yet");
		return;
	}
	list_selected = HD_listMove(list_selected, row_count, 0, 0);
	drawRows(screen, &layout->content, list_selected, &list_first);
}

// most played of the games the dashboard knows: recents and favorites
static void drawStats(MosaicSurface* screen, const HDLayout* layout) {
	row_count = 0;
	long long today = host->play_time_today();
	if (today < 0) {
		drawMessage(screen, &layout->content, "No play history yet");
		return;
	}
	Row* row = addRow("Played today", NULL);
	HD_duration(today, row->right, sizeof(row->right));
	if (!row->right[0]) snprintf(row->right, sizeof(row->right), "%s", "0m");

	Item* known[RECENTS_MAX + PINNED_MAX];
	long long times[RECENTS_MAX + PINNED_MAX];
	int count = 0;
	long long recent_total = 0;
	int recent_games = 0;
	for (int i = 0; i < recent_count; i++) {
		if (recents[i].type != MOSAIC_ENTRY_ROM) continue;
		known[count] = &recents[i];
		times[count] = host->play_time(recents[i].path);
		if (times[count] > 0) recent_total += times[count];
		recent_games++;
		count++;
	}
	for (int i = 0; i < pinned_count; i++) {
		int seen = 0;
		for (int k = 0; k < count && !seen; k++) seen = !strcmp(known[k]->path, pinned[i].path);
		if (seen || pinned[i].type != MOSAIC_ENTRY_ROM) continue;
		known[count] = &pinned[i];
		times[count] = host->play_time(pinned[i].path);
		count++;
	}
	row = addRow("Recently played", NULL);
	char total[32];
	HD_duration(recent_total, total, sizeof(total));
	snprintf(row->right, sizeof(row->right), "%i %s%s%s", recent_games, recent_games == 1 ? "game" : "games",
		total[0] ? " \xc2\xb7 " : "", total);

	int top[MOST_PLAYED_MAX];
	int n = HD_mostPlayed(times, count, top, MOST_PLAYED_MAX);
	if (n) addRow("MOST PLAYED", NULL)->header = 1;
	for (int i = 0; i < n; i++) {
		Item* item = known[top[i]];
		row = addRow(item->name, item);
		if (!row) break;
		char played[32];
		HD_duration(times[top[i]], played, sizeof(played));
		int sessions = host->play_count(item->path);
		if (sessions > 0) {
			snprintf(row->right, sizeof(row->right), "%s \xc2\xb7 %i %s", played, sessions, sessions == 1 ? "session" : "sessions");
		} else {
			snprintf(row->right, sizeof(row->right), "%s", played);
		}
	}
	int first = 0;
	drawRows(screen, &layout->content, -1, &first);
}

static void drawMinUI(MosaicSurface* screen, const HDLayout* layout) {
	row_count = 0;
	for (int i = 0; i < recent_count; i++) addRow(recents[i].name, &recents[i]);
	addRow("Games", NULL);
	list_selected = HD_listMove(list_selected, row_count, 0, 0);
	drawRows(screen, &layout->content, list_selected, &list_first);
}

// recent and pinned games with achievements on the card; ones still being
// hashed come in on a later frame
static int pending = 0;
static void raRows(void) {
	row_count = 0;
	pending = 0;
	for (int pass = 0; pass < 2; pass++) {
		Item* items = pass ? pinned : recents;
		int n = pass ? pinned_count : recent_count;
		for (int i = 0; i < n; i++) {
			Item* item = &items[i];
			if (item->type != MOSAIC_ENTRY_ROM) continue;
			int seen = 0;
			for (int k = 0; k < row_count && !seen; k++) seen = !strcmp(rows[k].item->path, item->path);
			if (seen) continue;
			MosaicAchievements progress = { .size = sizeof(MosaicAchievements) };
			int got = host->achievements(item->path, &progress);
			if (got == MOSAIC_ACHIEVEMENTS_UNKNOWN) pending++;
			if (got != 1 || progress.total <= 0) continue;
			Row* row = addRow(item->name, item);
			if (!row) return;
			HD_achievementLine(progress.unlocked, progress.total, progress.points_unlocked, progress.points, row->right,
				sizeof(row->right));
		}
	}
}

// rows from raRows, which render runs first
static void drawAchievements(MosaicSurface* screen, const HDLayout* layout) {
	if (pending) waiting = 1;
	if (!row_count) {
		drawMessage(screen, &layout->content, pending ? "Checking achievements" : "No achievements on the card yet");
		return;
	}
	list_selected = HD_listMove(list_selected, row_count, 0, 0);
	drawRows(screen, &layout->content, list_selected, &list_first);
}

static void drawCarts(MosaicSurface* screen, const HDLayout* layout) {
	if (!cart_count) {
		drawMessage(screen, &layout->content, "No Pico-8 carts");
		return;
	}
	int scale = host->screen_scale();
	int gap = 10 * scale;
	int name_h = host->text_height(MOSAIC_FONT_SMALL) + 4 * scale;
	const MosaicRect* c = &layout->content;
	int view_rows = 2;
	int cell = (c->h - view_rows * name_h - gap * (view_rows - 1)) / view_rows;
	if (cell < 16 * scale) {
		view_rows = 1;
		cell = c->h - name_h;
	}
	if (cell < 8 * scale) return;
	grid_cols = HD_cellsFit(c->w, cell, gap);
	if (grid_cols < 1) grid_cols = 1;
	if (cart_selected >= cart_count) cart_selected = cart_count - 1;
	int total_rows = (cart_count + grid_cols - 1) / grid_cols;
	cart_first_row = HD_window(cart_selected / grid_cols, total_rows, view_rows, cart_first_row);
	int radius = 8 * scale;
	for (int r = 0; r < view_rows; r++) {
		for (int col = 0; col < grid_cols; col++) {
			int index = (cart_first_row + r) * grid_cols + col;
			if (index >= cart_count) return;
			Item* item = &carts[index];
			MosaicRect box = { c->x + col * (cell + gap), c->y + r * (cell + name_h + gap), cell, cell };
			int selected = index == cart_selected;
			if (selected) {
				MosaicRect frame = { box.x - 2 * scale, box.y - 2 * scale, box.w + 4 * scale, box.h + 4 * scale };
				host->fill_round_rect(screen, &frame, radius, C_MAIN());
			}
			drawArt(screen, item, &box, 0, radius);
			char name[128];
			int w = host->text_truncate(MOSAIC_FONT_SMALL, item->name, box.w, name, sizeof(name));
			host->draw_text(screen, MOSAIC_FONT_SMALL, name, box.x + (box.w - (w > 0 ? w : 0)) / 2, box.y + box.h + 2 * scale,
				selected ? C_TEXT() : C_HINT(), box.w);
		}
	}
}

// What A does with what's selected now, for its hint: NULL for nothing.
static const char* openLabel(void) {
	Item* item = NULL;
	switch (mode) {
		case HD_MODE_DASHBOARD:
			if (tab == HD_TAB_HOME) {
				int index = home_focus == HD_FOCUS_CONTINUE ? 0 : home_focus + 1;
				if (index < recent_count) item = &recents[index];
			}
			else if (tab == HD_TAB_PINNED && list_selected >= 0 && list_selected < pinned_count) item = &pinned[list_selected];
			break;
		case HD_MODE_MINUI:
			if (list_selected == recent_count) return "OPEN";
			if (list_selected >= 0 && list_selected < recent_count) item = &recents[list_selected];
			break;
		case HD_MODE_RETROACHIEVEMENTS:
			if (list_selected >= 0 && list_selected < row_count) item = rows[list_selected].item;
			break;
		case HD_MODE_PICO8:
			if (cart_selected >= 0 && cart_selected < cart_count) item = &carts[cart_selected];
			break;
	}
	if (!item || !item->available) return NULL;
	return HD_openLabel(item->type, item->can_resume);
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	waiting = 0;
	// achievements' rows decide A's hint, so they come first
	if (mode == HD_MODE_RETROACHIEVEMENTS) raRows();
	int hints_y = drawHints(screen, openLabel());
	int scale = host->screen_scale();
	int name_h = host->text_height(MOSAIC_FONT_SMALL) + 4 * scale;
	int shelf = mode == HD_MODE_DASHBOARD && tab == HD_TAB_HOME && shelf_on;
	HDLayout layout;
	if (HD_layout(&view->area, &view->status, &view->header, view->status.h, hints_y, scale, name_h, shelf, &layout) != 0) {
		return 0; // too small to draw anything but the hints
	}
	drawTabs(screen, &layout.tabs);
	switch (mode) {
		case HD_MODE_MINUI: drawMinUI(screen, &layout); break;
		case HD_MODE_RETROACHIEVEMENTS: drawAchievements(screen, &layout); break;
		case HD_MODE_PICO8: drawCarts(screen, &layout); break;
		default:
			if (tab == HD_TAB_PINNED) drawPinned(screen, &layout);
			else if (tab == HD_TAB_STATS) drawStats(screen, &layout);
			else drawHome(screen, &layout);
	}
	return 0;
}

///////////////////////////////////////
// input

static int pressedBit(unsigned int bits, int button) {
	return (bits & MOSAIC_BUTTON_BIT(button)) != 0;
}

// up and down through a list of count, wrapping on a single press
static int listInput(const MosaicInput* input, int count) {
	int before = list_selected;
	if (pressedBit(input->repeated, MOSAIC_BUTTON_UP)) {
		list_selected = HD_listMove(list_selected, count, -1, pressedBit(input->pressed, MOSAIC_BUTTON_UP));
	}
	if (pressedBit(input->repeated, MOSAIC_BUTTON_DOWN)) {
		list_selected = HD_listMove(list_selected, count, 1, pressedBit(input->pressed, MOSAIC_BUTTON_DOWN));
	}
	int page = 5;
	if (pressedBit(input->repeated, MOSAIC_BUTTON_LEFT)) list_selected = HD_listMove(list_selected, count, -page, 0);
	if (pressedBit(input->repeated, MOSAIC_BUTTON_RIGHT)) list_selected = HD_listMove(list_selected, count, page, 0);
	return list_selected != before;
}

static void setTab(int index) {
	tab = tabs[index];
	list_selected = 0;
	list_first = 0;
	char message[64];
	snprintf(message, sizeof(message), "tab %s", HD_tabLabel(tab));
	say(message);
}

static int input(const MosaicInput* input, void* data) {
	(void)data;
	unsigned int pressed = input->pressed;
	if (pressedBit(pressed, MOSAIC_BUTTON_MENU)) {
		host->run_action("gamelist.quick_menu");
		return MOSAIC_SCREEN_STAY;
	}
	if (pressedBit(pressed, MOSAIC_BUTTON_SELECT)) {
		host->run_action("gamelist.switcher");
		return MOSAIC_SCREEN_STAY;
	}
	if (pressedBit(pressed, MOSAIC_BUTTON_X)) {
		say("games");
		return MOSAIC_SCREEN_CLOSE;
	}

	int moved = 0;
	int open = pressedBit(pressed, MOSAIC_BUTTON_A);
	switch (mode) {
		case HD_MODE_DASHBOARD: {
			int step = pressedBit(pressed, MOSAIC_BUTTON_R1) - pressedBit(pressed, MOSAIC_BUTTON_L1);
			if (step && tab_count > 1) {
				setTab(HD_tabStep(HD_tabIndex(tabs, tab_count, tab), tab_count, step));
				host->request_redraw();
				return MOSAIC_SCREEN_STAY;
			}
			if (tab == HD_TAB_HOME) {
				int shelf_count = shelf_on && shelf_cells && recent_count > 1 ? recent_count - 1 : 0;
				int before = home_focus;
				static const int dpad[] = { MOSAIC_BUTTON_UP, MOSAIC_BUTTON_DOWN, MOSAIC_BUTTON_LEFT, MOSAIC_BUTTON_RIGHT };
				for (int i = 0; i < 4; i++) {
					if (pressedBit(input->repeated, dpad[i])) home_focus = HD_homeMove(home_focus, shelf_count, dpad[i]);
				}
				moved = home_focus != before;
				if (open) {
					int index = home_focus == HD_FOCUS_CONTINUE ? 0 : home_focus + 1;
					if (index < recent_count) openItem(&recents[index]);
				}
			}
			else if (tab == HD_TAB_PINNED) {
				moved = listInput(input, pinned_count);
				if (open && list_selected >= 0 && list_selected < pinned_count) openItem(&pinned[list_selected]);
			}
			break;
		}
		case HD_MODE_MINUI:
			moved = listInput(input, recent_count + 1);
			if (open) {
				if (list_selected == recent_count) {
					say("games");
					return MOSAIC_SCREEN_CLOSE;
				}
				if (list_selected >= 0 && list_selected < recent_count) openItem(&recents[list_selected]);
			}
			break;
		case HD_MODE_RETROACHIEVEMENTS:
			moved = listInput(input, row_count);
			if (open && list_selected >= 0 && list_selected < row_count) openItem(rows[list_selected].item);
			break;
		case HD_MODE_PICO8: {
			int before = cart_selected;
			if (cart_count) {
				if (pressedBit(input->repeated, MOSAIC_BUTTON_LEFT)) cart_selected = HD_listMove(cart_selected, cart_count, -1, 0);
				if (pressedBit(input->repeated, MOSAIC_BUTTON_RIGHT)) cart_selected = HD_listMove(cart_selected, cart_count, 1, 0);
				if (pressedBit(input->repeated, MOSAIC_BUTTON_UP)) cart_selected = HD_listMove(cart_selected, cart_count, -grid_cols, 0);
				if (pressedBit(input->repeated, MOSAIC_BUTTON_DOWN)) cart_selected = HD_listMove(cart_selected, cart_count, grid_cols, 0);
			}
			moved = cart_selected != before;
			if (open && cart_selected >= 0 && cart_selected < cart_count) openItem(&carts[cart_selected]);
			break;
		}
	}
	if (moved) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

///////////////////////////////////////
// opening and closing

static int enter(void* data) {
	(void)data;
	loadRecents();
	if (mode == HD_MODE_RETROACHIEVEMENTS || (mode == HD_MODE_DASHBOARD && pinned_on)) loadPinned();
	if (mode == HD_MODE_PICO8) loadCarts();
	tab_count = HD_tabs(pinned_on, favorites_loaded, stats_on, tabs);
	tab = tabs[HD_tabIndex(tabs, tab_count, tab)];
	if (home_focus >= recent_count - 1) home_focus = HD_FOCUS_CONTINUE;
	if (cart_selected >= cart_count) cart_selected = 0;
	showing = 1;
	char message[256];
	snprintf(message, sizeof(message), "home: mode %s, tab %s of %i, recents %i, pinned %i, carts %i, history %s",
		HD_modeName(mode), HD_tabLabel(tab), tab_count, recent_count, pinned_count, cart_count,
		historyKnown() ? "on" : "unknown");
	say(message);
	return 0;
}

static void leave(void* data) {
	(void)data;
	showing = 0;
}

// art and achievements that were loading: draw again once they're in
static void tick(unsigned long now) {
	if (!showing || !waiting || now - asked_at < 250) return;
	asked_at = now;
	waiting = 0;
	host->request_redraw();
}

static int setting(const char* key, const char* fallback) {
	return !strcmp(host->cfg_get(ID, key, fallback), "on");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_hints)) {
		say("needs a launcher with plugin screens, the home slot and draw_hints");
		return -1;
	}
	mode = HD_modeOf(host->cfg_get(ID, "mode", "dashboard"));
	pinned_on = setting("pinned", "on");
	stats_on = setting("stats", "on");
	shelf_on = setting("shelf", "on");
	snprintf(pico_tags, sizeof(pico_tags), "%s", host->cfg_get(ID, "pico8_tags", "P8,PICO8,PICO"));

	char path[MAX_PATH_LEN];
	snprintf(path, sizeof(path), "%s/launcher/plugins.txt", host->userdata_path);
	char* text = readFile(path);
	favorites_loaded = HD_pluginOn(text, "favorites");
	free(text);
	snprintf(collection_path, sizeof(collection_path), "%s/Collections/%s.txt", host->sdcard_path,
		HD_collectionName(host->cfg_get("favorites", "collection", "Favorites")));

	MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.flags = MOSAIC_SCREEN_HOME,
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("couldn't register the home screen");
		return -1;
	}
	tab_count = HD_tabs(pinned_on, favorites_loaded, stats_on, tabs);
	char message[160];
	snprintf(message, sizeof(message), "mode %s, pinned %s, stats %s, shelf %s, favorites %s", HD_modeName(mode),
		pinned_on ? "on" : "off", stats_on ? "on" : "off", shelf_on ? "on" : "off", favorites_loaded ? "on" : "off");
	say(message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
