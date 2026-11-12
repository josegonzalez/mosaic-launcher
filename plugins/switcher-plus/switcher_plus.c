// Game switcher plus: owns the switcher slot and shows the recents in
// place of the built-in game switcher, in the style the style setting
// picks (see enum SPStyle):
//
//   full   stock's: the selected recent's save state preview fitted to the
//          screen, or "No Preview" on the theme's background colour, with
//          its name on a pill in the top left; left and right step through
//          the recents, wrapping at the ends
//   strip  the selected preview large in the middle with its name on a
//          pill under it and its neighbours smaller either side, as many
//          as fit; left and right step, wrapping on a press and stopping
//          at an end while held
//   list   the recents' names in rows on the left, the selected one on a
//          pill, and its preview in a panel on the right; up and down
//          step and left and right go a page, wrapping on a press
//
// As stock's: A (on its release) opens the selected recent with
// recent_open, resuming its save state when it has one, and the launcher
// comes back here after the game; Y (on its release) removes it, moving
// the selection to the one before; B, or a tap of SELECT, closes it. Only
// presses that start while it shows count, so a release left from the
// button that opened it does nothing. It draws stock's hints with
// draw_hints (see SP_hints).
//
// recent_info reads the card, so each recent's info is kept until the
// recents change (their count or any path, checked each frame) and asked
// again each time the switcher opens. Previews come from art_load, and
// each is copied once with surface_scale to the size it's drawn at; the
// copies, the plugin's own surfaces, are kept until it closes, the least
// recently drawn going first. Everything is drawn around the status pill
// and header widgets, which stay clear.
//
// The captions setting (off by default) adds playtime-captions' caption,
// such as "2h 15m · 3 days ago", for the selected recent from the play
// history calls with its path: on a smaller pill under the title in full,
// under the name pill in strip, and under the preview in list. Its parts
// and format are playtime-captions' settings, public ones read with cfg_read
// each time the switcher opens, which only answer while that plugin is
// loaded, so they're its defaults otherwise. The text is the same as that
// plugin's, from a copy of its formatter (see SP_captionFit).

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "switcher_model.h"
#include "mosaic_plugin.h"

#define ID "switcher-plus"
#define SCREEN_ID ID ".switcher"
#define COPIES 9
#define TAP_MS 250 // stock's SELECT tap

static const MosaicHost* host;

// Another plugin's setting, when it marks it public, else fallback.
static const char* publicSetting(const char* id, const char* key, const char* fallback) {
	return MOSAIC_HOST_HAS(host, cfg_read) ? host->cfg_read(id, key, fallback) : fallback;
}
static int style = SP_STYLE_FULL;
static SPRecents recents;
static int selected = 0;
static int first = 0;               // the list style's first row in view
static int page_rows = 1;           // and how many it showed, for left and right
static unsigned int pressed_here = 0; // buttons pressed while it showed
static unsigned long now_ms = 0, select_at = 0;
static int ticking = 0;
static char shown[SP_PATH_MAX + 512]; // what was last logged as showing
static int captions_on = 0;
static SPCaptions captions;          // playtime-captions' parts and format, read on each open
static char shown_caption[160];      // the selected recent's caption this frame
static int caption_drawn = 0;
static char captions_logged[200];    // the parts and format last logged

static const char* styleName(int s) {
	return s == SP_STYLE_STRIP ? "strip" : s == SP_STYLE_LIST ? "list" : "full";
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static void say(const char* message) {
	host->log(ID, message);
}

///////////////////////////////////////
// the recents

static const char* pathOf(int index, void* data) {
	(void)data;
	return host->recent_path(index);
}

static int infoOf(int index, MosaicRecent* out, void* data) {
	(void)data;
	return host->recent_info(index, out);
}

static void sync(void) {
	SP_sync(&recents, host->recents_count(), pathOf, NULL);
	if (selected >= recents.count) selected = recents.count > 0 ? recents.count - 1 : 0;
	if (selected < 0) selected = 0;
}

static const MosaicRecent* info(int index) {
	return SP_info(&recents, index, infoOf, NULL);
}

// the recent's name, or its file's name when its info can't be read
static const char* nameOf(int index) {
	const MosaicRecent* r = info(index);
	if (r && r->name[0]) return r->name;
	if (index < 0 || index >= recents.count) return "";
	const char* slash = strrchr(recents.path[index], '/');
	return slash ? slash + 1 : recents.path[index];
}

///////////////////////////////////////
// preview copies, kept while it shows

typedef struct Copy {
	char path[SP_PATH_MAX];
	int w, h;
	MosaicSurface* surface; // NULL for a free slot
	unsigned long used;
} Copy;

static Copy copies[COPIES];
static unsigned long frame = 0;

static void copiesClear(void) {
	for (int i = 0; i < COPIES; i++) {
		if (copies[i].surface) host->surface_free(ID, copies[i].surface);
		memset(&copies[i], 0, sizeof(copies[i]));
	}
}

// image, the preview at path, at w by h: a copy kept from before or made
// now, or image itself when it's that size already or no copy can be made
static const MosaicSurface* sized(const char* path, const MosaicSurface* image, int w, int h) {
	int iw = 0, ih = 0;
	if (host->surface_size(image, &iw, &ih) != 0 || (iw == w && ih == h)) return image;
	Copy* slot = NULL;
	for (int i = 0; i < COPIES; i++) {
		Copy* c = &copies[i];
		if (c->surface && c->w == w && c->h == h && strcmp(c->path, path) == 0) {
			c->used = frame;
			return c->surface;
		}
		if (!slot || !c->surface || (slot->surface && c->used < slot->used)) slot = c;
	}
	MosaicSurface* scaled = host->surface_scale(ID, image, w, h, MOSAIC_FIT_STRETCH);
	if (!scaled) return image;
	if (slot->surface) host->surface_free(ID, slot->surface);
	snprintf(slot->path, sizeof(slot->path), "%s", path);
	slot->w = w;
	slot->h = h;
	slot->surface = scaled;
	slot->used = frame;
	return scaled;
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static void fillAround(MosaicSurface* screen, const MosaicRect* rect, const MosaicRect* hole, MosaicColor color) {
	MosaicRect parts[4];
	int n = SP_avoid(rect, hole, parts);
	for (int i = 0; i < n; i++) host->fill_rect(screen, &parts[i], color);
}

// text in font centred in box, around nothing: box is clear of the hole
static void centred(MosaicSurface* screen, int font, const char* text, const MosaicRect* box, MosaicColor color) {
	char cut[256];
	int w = host->text_truncate(font, text, box->w, cut, sizeof(cut));
	int h = host->text_height(font);
	if (w < 0 || h < 0) return;
	host->draw_text(screen, font, cut, box->x + (box->w - w) / 2, box->y + (box->h - h) / 2, color, box->w);
}

// What the selected recent's preview is: 1 drawn, 0 still loading, -1 none.
// Draws index's preview fitted inside box, around hole, with what's left of
// box in fill (when fill.a isn't 0).
static int drawPreview(MosaicSurface* screen, int index, const MosaicRect* box, const MosaicRect* hole, MosaicColor fill) {
	const MosaicRecent* r = info(index);
	if (!r || !r->preview[0]) return -1;
	const MosaicSurface* image = NULL;
	int state = host->art_load(r->preview, &image);
	if (state == MOSAIC_ART_LOADING) return 0;
	int iw = 0, ih = 0;
	if (state != MOSAIC_ART_READY || !image || host->surface_size(image, &iw, &ih) != 0) return -1;
	if (fill.a) fillAround(screen, box, hole, fill);
	MosaicRect to = SP_fit(iw, ih, box);
	if (to.w <= 0 || to.h <= 0) return -1;
	const MosaicSurface* src = sized(r->preview, image, to.w, to.h);
	int sw = 0, sh = 0;
	host->surface_size(src, &sw, &sh);
	MosaicRect parts[4];
	int n = SP_avoid(&to, hole, parts);
	for (int i = 0; i < n; i++) {
		MosaicRect from = SP_source(&to, &parts[i], sw, sh);
		host->blit(screen, src, &from, &parts[i]);
	}
	return 1;
}

// a pill in color at rect with text in ink, BUTTON_PADDING in
static void pill(MosaicSurface* screen, const MosaicRect* rect, const char* text, MosaicColor color, MosaicColor ink) {
	int scale = host->screen_scale();
	int inner = 12 * scale;
	int h = host->text_height(MOSAIC_FONT_LARGE);
	host->fill_round_rect(screen, rect, rect->h / 2, color);
	host->draw_text(screen, MOSAIC_FONT_LARGE, text, rect->x + inner, rect->y + (rect->h - h + 1) / 2, ink,
		rect->w - 2 * inner);
}

///////////////////////////////////////
// play time captions

#define CAPTIONS_ID "playtime-captions"
#define MUTE_PERCENT 40 // as playtime-captions' caption

// playtime-captions' settings, its defaults while it isn't loaded
static void readCaptions(void) {
	captions = SP_captions(publicSetting(CAPTIONS_ID, "time", "on"), publicSetting(CAPTIONS_ID, "last", "on"),
		publicSetting(CAPTIONS_ID, "sessions", "off"), publicSetting(CAPTIONS_ID, "format", "relative"),
		publicSetting(CAPTIONS_ID, "never", "off"), publicSetting(CAPTIONS_ID, "achievements", "off"));
	static const char* names[] = { "relative", "compact", "date" };
	char message[sizeof(captions_logged)];
	snprintf(message, sizeof(message), "captions time %s, last %s, sessions %s, format %s, never %s, achievements %s",
		captions.parts & SP_PART_TIME ? "on" : "off", captions.parts & SP_PART_LAST ? "on" : "off",
		captions.parts & SP_PART_SESSIONS ? "on" : "off", names[captions.format], captions.never ? "on" : "off",
		captions.parts & SP_PART_ACHIEVEMENTS ? "on" : "off");
	if (strcmp(message, captions_logged) == 0) return;
	snprintf(captions_logged, sizeof(captions_logged), "%s", message);
	say(message);
}

static int measureSmall(const char* text, void* ctx) {
	(void)ctx;
	return host->text_width(MOSAIC_FONT_SMALL, text);
}

// The recent at index's caption that fits max_w, written to out. Returns
// its width, or 0 for none.
static int captionOf(int index, int max_w, char* out, size_t size) {
	out[0] = '\0';
	if (!captions_on || index < 0 || index >= recents.count || !recents.path[index][0]) return 0;
	const char* path = recents.path[index];
	SPPlay play = { host->play_time(path), host->last_played(path), host->play_count(path), 0, 0 };
	if (captions.parts & SP_PART_ACHIEVEMENTS) {
		MosaicAchievements progress = { .size = sizeof(MosaicAchievements) };
		if (host->achievements(path, &progress) == 1 && progress.total > 0) {
			play.cheevos = progress.unlocked;
			play.cheevos_total = progress.total;
		}
	}
	int w = SP_captionFit(&play, (long long)time(NULL), &captions, max_w, measureSmall, NULL, out, size);
	if (w > 0 && index == selected) {
		snprintf(shown_caption, sizeof(shown_caption), "%s", out);
		caption_drawn = 1;
	}
	return w;
}

// the theme's hint colour moved toward the colour it's drawn on
static MosaicColor captionInk(int on, unsigned int fallback) {
	int hint = host->theme_color(MOSAIC_COLOR_HINT);
	int under = host->theme_color(on);
	if (hint < 0) hint = 0xFFFFFF;
	if (under < 0) under = (int)fallback;
	return mosaic_color((unsigned int)SP_mute(hint, under, MUTE_PERCENT), 255);
}

// the full style's caption, on a smaller pill under the title
static void titleCaption(MosaicSurface* screen, const MosaicRect* area, const MosaicRect* hole, const MosaicRect* title) {
	int scale = host->screen_scale();
	int inner = 12 * scale;
	int text_h = host->text_height(MOSAIC_FONT_SMALL);
	MosaicRect room = SP_titleCaption(area, hole, title, scale, text_h);
	char text[160];
	int w = room.w > 2 * inner ? captionOf(selected, room.w - 2 * inner, text, sizeof(text)) : 0;
	if (w <= 0) return;
	MosaicRect p = { room.x, room.y, w + 2 * inner, room.h };
	host->fill_round_rect(screen, &p, p.h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	host->draw_text(screen, MOSAIC_FONT_SMALL, text, p.x + inner, p.y + (p.h - text_h + 1) / 2,
		captionInk(MOSAIC_COLOR_ACCENT, 0x9B2257), w);
}

static int renderFull(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole) {
	const MosaicRect* area = &view->area;
	int got = drawPreview(screen, selected, area, hole, mosaic_color(0x000000, 255));
	if (got == 0) fillAround(screen, area, hole, mosaic_color(0x000000, 255));
	if (got < 0) {
		fillAround(screen, area, hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
		centred(screen, MOSAIC_FONT_LARGE, tr("No Preview"), area, mosaic_color(0xFFFFFF, 255));
	}
	const char* name = nameOf(selected);
	MosaicRect title = SP_title(area, hole, host->screen_scale(), host->text_width(MOSAIC_FONT_LARGE, name));
	if (title.w > 0) {
		pill(screen, &title, name, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257), themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF));
		if (captions_on) titleCaption(screen, area, hole, &title);
	}
	return got;
}

static int renderStrip(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole, int* fell_back) {
	int scale = host->screen_scale();
	MosaicRect room = SP_room(&view->area, hole, scale);
	SPStrip strip;
	int pill_h = 30 * scale;
	// a caption line under the name pill, or none when the strip can't fit one
	int text_h = captions_on ? host->text_height(MOSAIC_FONT_SMALL) : 0;
	int with_caption = captions_on && text_h > 0 &&
		SP_strip(&room, view->area.w, view->area.h, scale, pill_h + 5 * scale + text_h, 32 * scale, &strip) == 0;
	if (!with_caption && SP_strip(&room, view->area.w, view->area.h, scale, pill_h, 32 * scale, &strip) != 0) {
		*fell_back = 1;
		return renderFull(screen, view, hole);
	}
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor card = themeColor(MOSAIC_COLOR_ACCENT2, 0x333333);
	int got = -1;
	// outside in, so the selected one goes on last
	for (int d = strip.sides; d >= 0; d--) {
		for (int side = -1; side <= 1; side += 2) {
			if (d == 0 && side > 0) break;
			int index = selected + side * d;
			if (index < 0 || index >= recents.count) continue;
			MosaicRect rect = SP_stripCard(&strip, side * d);
			if (d == 0) {
				MosaicRect frame_rect = { rect.x - 2 * scale, rect.y - 2 * scale, rect.w + 4 * scale, rect.h + 4 * scale };
				host->fill_rect(screen, &frame_rect, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF));
			}
			host->fill_rect(screen, &rect, card);
			int drew = drawPreview(screen, index, &rect, hole, mosaic_color(0x000000, 255));
			if (drew < 0) centred(screen, d == 0 ? MOSAIC_FONT_LARGE : MOSAIC_FONT_SMALL, tr("No Preview"), &rect, ink);
			if (d == 0) got = drew;
		}
	}
	const char* name = nameOf(selected);
	int inner = 12 * scale;
	int w = host->text_width(MOSAIC_FONT_LARGE, name) + 2 * inner;
	if (w > strip.name.w) w = strip.name.w;
	if (strip.name.h > 0 && w > 2 * inner) {
		MosaicRect name_pill = { strip.cx - w / 2, strip.name.y, w, pill_h };
		if (name_pill.x < strip.name.x) name_pill.x = strip.name.x;
		pill(screen, &name_pill, name, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF),
			themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000));
	}
	if (with_caption) {
		MosaicRect line = SP_stripCaption(&strip, pill_h, scale, text_h);
		char text[160];
		int cw = line.w > 0 ? captionOf(selected, line.w, text, sizeof(text)) : 0;
		if (cw > 0) {
			host->draw_text(screen, MOSAIC_FONT_SMALL, text, line.x + (line.w - cw) / 2, line.y,
				captionInk(MOSAIC_COLOR_BACKGROUND, 0x000000), cw);
		}
	}
	return got;
}

static int renderList(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole, int* fell_back) {
	int scale = host->screen_scale();
	SPList list;
	if (SP_list(&view->area, hole, scale, &list) != 0) {
		*fell_back = 1;
		return renderFull(screen, view, hole);
	}
	page_rows = list.count;
	first = SP_window(first, selected, recents.count, list.count);
	int inner = 12 * scale;
	int h = host->text_height(MOSAIC_FONT_LARGE);
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	for (int i = first; i < recents.count && i < first + list.count; i++) {
		int y = list.rows.y + (i - first) * list.row_h;
		const char* name = nameOf(i);
		if (i == selected) {
			int w = host->text_width(MOSAIC_FONT_LARGE, name) + 2 * inner;
			if (w > list.rows.w) w = list.rows.w;
			MosaicRect row = { list.rows.x, y, w, list.row_h };
			pill(screen, &row, name, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF),
				themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000));
		} else {
			host->draw_text(screen, MOSAIC_FONT_LARGE, name, list.rows.x + inner, y + (list.row_h - h + 1) / 2, ink,
				list.rows.w - 2 * inner);
		}
	}
	if (list.panel.w <= 0) return -1;
	// the selected recent's caption along the panel's bottom, under its preview
	MosaicRect preview = list.panel, line = { 0, 0, 0, 0 };
	int text_h = captions_on ? host->text_height(MOSAIC_FONT_SMALL) : 0;
	if (text_h > 0) line = SP_panelCaption(&list.panel, scale, text_h, &preview);
	int got = drawPreview(screen, selected, &preview, hole, mosaic_color(0, 0));
	if (got < 0) centred(screen, MOSAIC_FONT_LARGE, tr("No Preview"), &preview, ink);
	char text[160];
	int cw = line.w > 0 ? captionOf(selected, line.w, text, sizeof(text)) : 0;
	if (cw > 0) {
		host->draw_text(screen, MOSAIC_FONT_SMALL, text, line.x + (line.w - cw) / 2, line.y,
			captionInk(MOSAIC_COLOR_BACKGROUND, 0x000000), cw);
	}
	return got;
}

static void hints(MosaicSurface* screen) {
	const MosaicRecent* r = recents.count > 0 ? info(selected) : NULL;
	MosaicHint left[2], right[2];
	int left_count = 0, right_count = 0;
	SP_hints(recents.count, r ? r->can_resume : 0, left, &left_count, right, &right_count);
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
}

// logs what shows whenever it changes, for tests and bug reports
static void logShown(int got, int fell_back) {
	char line[sizeof(shown)];
	if (recents.count <= 0) {
		snprintf(line, sizeof(line), "showing no recents, style %s", styleName(style));
	} else {
		const MosaicRecent* r = info(selected);
		char caption[sizeof(shown_caption) + 16] = "";
		if (captions_on) {
			if (caption_drawn) snprintf(caption, sizeof(caption), ", caption %s", shown_caption);
			else snprintf(caption, sizeof(caption), ", no caption");
		}
		snprintf(line, sizeof(line), "showing %i of %i: %s, preview %s, %s, style %s%s%s", selected, recents.count,
			nameOf(selected), got > 0 ? "drawn" : got == 0 ? "loading" : "none", r && r->can_resume ? "resume" : "open",
			styleName(style), fell_back ? " (too small, drawn full)" : "", caption);
	}
	if (strcmp(line, shown) == 0) return;
	snprintf(shown, sizeof(shown), "%s", line);
	say(line);
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	// asked again each time it opens
	recents.count = -1;
	sync();
	selected = 0;
	first = 0;
	pressed_here = 0;
	shown[0] = '\0';
	if (captions_on) readCaptions();
	return 0;
}

static void leave(void* data) {
	(void)data;
	pressed_here = 0;
	copiesClear();
}

static void openSelected(void) {
	const char* name = nameOf(selected);
	int rc = host->recent_open(selected, 1);
	char message[SP_PATH_MAX + 64];
	snprintf(message, sizeof(message), rc == 1 ? "opening %i: %s" : "can't open %i: %s", selected, name);
	say(message);
}

static void removeSelected(void) {
	int at = selected;
	int rc = host->recent_remove(at);
	sync();
	if (rc == 0) selected = SP_afterRemove(at, recents.count);
	char message[64];
	snprintf(message, sizeof(message), rc == 0 ? "removed %i, %i left" : "can't remove %i, %i left", at, recents.count);
	say(message);
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	unsigned int bit_a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A), bit_b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	unsigned int bit_y = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y), bit_select = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_SELECT);
	sync();
	if (in->pressed & bit_select) select_at = now_ms;
	pressed_here |= in->pressed;
	unsigned int released = in->released & pressed_here;
	pressed_here &= ~in->released;
	int count = recents.count;
	int before = selected;

	if (in->pressed & bit_b) return MOSAIC_SCREEN_CLOSE;
	if ((released & bit_select) && (!ticking || now_ms - select_at < TAP_MS)) return MOSAIC_SCREEN_CLOSE;
	if (count > 0 && (released & bit_a)) {
		openSelected();
		return MOSAIC_SCREEN_STAY;
	}
	if (count > 0 && (released & bit_y)) {
		removeSelected();
		host->request_redraw();
		return MOSAIC_SCREEN_STAY;
	}

	// in steps for full and strip, and for list in rows, PAGE for a page
	enum { PAGE = 1000 };
	static const struct {
		int button;
		int by[3]; // for each enum SPStyle
	} steps[] = {
		{ MOSAIC_BUTTON_LEFT, { -1, -1, -PAGE } },
		{ MOSAIC_BUTTON_RIGHT, { 1, 1, PAGE } },
		{ MOSAIC_BUTTON_UP, { 0, 0, -1 } },
		{ MOSAIC_BUTTON_DOWN, { 0, 0, 1 } },
	};
	for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		unsigned int bit = MOSAIC_BUTTON_BIT(steps[i].button);
		int by = steps[i].by[style];
		if (by == PAGE || by == -PAGE) by = by / PAGE * page_rows;
		// stock's moves only on a press; the others repeat while held
		unsigned int when = style == SP_STYLE_FULL ? in->pressed : in->repeated;
		if (!by || !(when & bit)) continue;
		selected = SP_move(selected, count, by, (in->pressed & bit) != 0);
		break; // one direction a frame
	}
	if (selected != before) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	sync();
	frame++;
	MosaicRect hole = SP_hole(&view->status, &view->header);
	int got = -1, fell_back = 0;
	shown_caption[0] = '\0';
	caption_drawn = 0;
	if (recents.count <= 0) {
		MosaicColor black = mosaic_color(0x000000, 255);
		fillAround(screen, &view->area, &hole, black);
		centred(screen, MOSAIC_FONT_LARGE, "No Recents", &view->area, mosaic_color(0xFFFFFF, 255));
	} else if (style == SP_STYLE_FULL) {
		got = renderFull(screen, view, &hole);
	} else {
		fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
		got = style == SP_STYLE_STRIP ? renderStrip(screen, view, &hole, &fell_back)
			: renderList(screen, view, &hole, &fell_back);
	}
	hints(screen);
	logShown(got, fell_back);
	return 0;
}

static void tick(unsigned long now) {
	now_ms = now;
	ticking = 1;
}

static int init(const MosaicHost* h) {
	host = h;
	static const SPWords words = { tr, trPlural };
	SP_setWords(&words);
	if (!MOSAIC_HOST_HAS(host, draw_hints) || !MOSAIC_HOST_HAS(host, recent_open) || !MOSAIC_HOST_HAS(host, art_get)) {
		say("this launcher has no switcher slot or button hints");
		return 1;
	}
	style = SP_style(host->cfg_get("style", "full"));
	// the play history and achievements calls came before draw_hints, so
	// every launcher that gets here has them
	captions_on = strcmp(host->cfg_get("captions", "off"), "on") == 0;
	captions_logged[0] = '\0';
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.flags = MOSAIC_SCREEN_GAME_SWITCHER,
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the switcher screen");
		return 1;
	}
	char message[64];
	snprintf(message, sizeof(message), "style %s%s", styleName(style), captions_on ? ", captions on" : "");
	say(message);
	return 0;
}

static void quit(void) {
	copiesClear();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
