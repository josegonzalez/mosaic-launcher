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

#include <stdio.h>
#include <string.h>
#include "switcher_model.h"
#include "mosaic_plugin.h"

#define ID "switcher-plus"
#define SCREEN_ID ID ".switcher"
#define COPIES 9
#define TAP_MS 250 // stock's SELECT tap

static const MosaicHost* host;
static int style = SP_STYLE_FULL;
static SPRecents recents;
static int selected = 0;
static int first = 0;               // the list style's first row in view
static int page_rows = 1;           // and how many it showed, for left and right
static unsigned int pressed_here = 0; // buttons pressed while it showed
static unsigned long now_ms = 0, select_at = 0;
static int ticking = 0;
static char shown[SP_PATH_MAX + 128]; // what was last logged as showing

static const char* styleName(int s) {
	return s == SP_STYLE_STRIP ? "strip" : s == SP_STYLE_LIST ? "list" : "full";
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

static int renderFull(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole) {
	const MosaicRect* area = &view->area;
	int got = drawPreview(screen, selected, area, hole, mosaic_color(0x000000, 255));
	if (got == 0) fillAround(screen, area, hole, mosaic_color(0x000000, 255));
	if (got < 0) {
		fillAround(screen, area, hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
		centred(screen, MOSAIC_FONT_LARGE, "No Preview", area, mosaic_color(0xFFFFFF, 255));
	}
	const char* name = nameOf(selected);
	MosaicRect title = SP_title(area, hole, host->screen_scale(), host->text_width(MOSAIC_FONT_LARGE, name));
	if (title.w > 0) {
		pill(screen, &title, name, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257), themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF));
	}
	return got;
}

static int renderStrip(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole, int* fell_back) {
	int scale = host->screen_scale();
	MosaicRect room = SP_room(&view->area, hole, scale);
	SPStrip strip;
	if (SP_strip(&room, view->area.w, view->area.h, scale, 30 * scale, 32 * scale, &strip) != 0) {
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
			if (drew < 0) centred(screen, d == 0 ? MOSAIC_FONT_LARGE : MOSAIC_FONT_SMALL, "No Preview", &rect, ink);
			if (d == 0) got = drew;
		}
	}
	const char* name = nameOf(selected);
	int inner = 12 * scale;
	int w = host->text_width(MOSAIC_FONT_LARGE, name) + 2 * inner;
	if (w > strip.name.w) w = strip.name.w;
	if (strip.name.h > 0 && w > 2 * inner) {
		MosaicRect name_pill = { strip.cx - w / 2, strip.name.y, w, strip.name.h };
		if (name_pill.x < strip.name.x) name_pill.x = strip.name.x;
		pill(screen, &name_pill, name, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF),
			themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000));
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
	int got = drawPreview(screen, selected, &list.panel, hole, mosaic_color(0, 0));
	if (got < 0) centred(screen, MOSAIC_FONT_LARGE, "No Preview", &list.panel, ink);
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
		snprintf(line, sizeof(line), "showing %i of %i: %s, preview %s, %s, style %s%s", selected, recents.count,
			nameOf(selected), got > 0 ? "drawn" : got == 0 ? "loading" : "none", r && r->can_resume ? "resume" : "open",
			styleName(style), fell_back ? " (too small, drawn full)" : "");
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
	if (!MOSAIC_HOST_HAS(host, draw_hints) || !MOSAIC_HOST_HAS(host, recent_open) || !MOSAIC_HOST_HAS(host, art_get)) {
		say("this launcher has no switcher slot or button hints");
		return 1;
	}
	style = SP_style(host->cfg_get(ID, "style", "full"));
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
	snprintf(message, sizeof(message), "style %s", styleName(style));
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
