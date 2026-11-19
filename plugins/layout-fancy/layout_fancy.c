// Fancy layout: owns the layout slot and draws the game list as stock's
// list of rows, with the selected game's name on a pill, beside a panel
// that shows its box art (or else its thumbnail) and the preview of its
// last save state.
//
// The rows, their window and the d-pad are the built-in list's: the layout
// takes no buttons, draws the rows in view at stock's places (see
// FL_layout), and leaves the selection and its window to the launcher.
// Names show when the launcher shows names; a name a plugin hid shows only
// on the selected row while the panel has no art, as on the built-in list.
//
// The panel shows art only while Show game art is on. A game's last save
// state is the slot minarch last used, named in
// .userdata/shared/.minui/<tag>/<rom>.txt, and its preview is
// <rom>.<slot>.bmp beside it, found as stock finds them (see
// FL_resumeFile and FL_slotPaths). With the prefer_preview setting on, the
// default, the preview stands in for the box when there is one; with it
// off, the box goes in the panel's top half and the preview in its bottom
// half. The state files are looked for once each time the selection moves
// to another game, and again back from a game, never while drawing.
//
// Art: art_get's and art_load's images are only good for the frame they're
// asked in, so they're never kept. Each one is copied once with
// surface_scale to its place in the panel, and the copies, which are the
// plugin's own surfaces, are kept across frames, the least recently drawn
// going first. A copy that can't be made (the plugin's surfaces are full)
// is drawn straight from the cache's image that frame instead.
//
// A plugin's art_replace surface, such as a video preview, takes the box's
// place in the panel (see FL_replacedArt), fitted the same way. It's asked
// for with art_replacement each frame and drawn straight from the plugin's
// surface, never copied, since each of its frames is new. Art decorators,
// such as metadata-band's, go under the panel's art with
// host->decorate_art.
//
// The position setting puts the panel on the right (as stock), on the
// left with the rows on the right, or across the top half with the rows
// under it (see FL_layoutAt). With the description setting on, a game
// whose .info sidecar has a description shows it wrapped in the panel:
// the art in the panel's top half (its left half at the top), pinned to
// that half's top (left), and the description in what's left under (beside)
// it. Its lines are drawn once a selection into a surface of the plugin's
// own, which each frame blits the part in view of. Once the selection has
// stayed put FL_DESC_WAIT_MS, a description taller than its place scrolls
// up and loops, and only then does tick ask for a frame each frame, while
// the layout is still drawn. Art decorators don't run while a description
// shows, as it takes the space under the art they'd draw in.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fancy_model.h"
#include "mosaic_plugin.h"

#define ID "layout-fancy"
#define MAX_PATH_LEN 1024
#define CACHE_MAX 6

static const MosaicHost* host;
static int prefer_preview = 1;
static int position = FL_ART_RIGHT;
static int show_desc = 0;
static char roms[MAX_PATH_LEN];
static unsigned long now_ms = 0; // the last tick's

///////////////////////////////////////
// the selected game's save state, looked for once a selection

static char state_for[MAX_PATH_LEN]; // the entry path it's for; "" for none yet
static char preview[MAX_PATH_LEN];   // its preview, or "" for none

static int fileExists(const char* path, void* ctx) {
	(void)ctx;
	return access(path, F_OK) == 0;
}

static int regularFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void findState(const MosaicEntry* entry) {
	preview[0] = '\0';
	int type = host->entry_type(entry);
	if (type != MOSAIC_ENTRY_ROM && type != MOSAIC_ENTRY_DIR) return;
	char file[MAX_PATH_LEN], slot_path[MAX_PATH_LEN], stem[MAX_PATH_LEN];
	if (!FL_resumeFile(host->entry_path(entry), type == MOSAIC_ENTRY_DIR, fileExists, NULL, file, sizeof(file))) return;
	if (!FL_slotPaths(host->sdcard_path, roms, file, slot_path, stem, sizeof(stem))) return;
	FILE* f = fopen(slot_path, "r");
	if (!f) return;
	char text[16] = "";
	if (!fgets(text, sizeof(text), f)) text[0] = '\0';
	fclose(f);
	char path[MAX_PATH_LEN];
	if (FL_previewPath(stem, FL_slot(text), path, sizeof(path)) && regularFile(path)) {
		snprintf(preview, sizeof(preview), "%s", path);
	}
}

// the selected game's preview, looked for when the selection moved to it
static const char* stateFor(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	if (!path || strlen(path) >= sizeof(state_for)) return "";
	if (!state_for[0] || strcmp(path, state_for) != 0) {
		snprintf(state_for, sizeof(state_for), "%s", path);
		findState(entry);
	}
	return preview;
}

///////////////////////////////////////
// scaled art, kept across frames

typedef struct Scaled {
	char key[MAX_PATH_LEN + 8]; // the entry's path and the kind, or the preview's path
	int w, h;
	MosaicSurface* surface; // NULL for a free slot
	unsigned long used;
} Scaled;

static Scaled cache[CACHE_MAX];
static unsigned long frame = 0;

static void cacheFree(Scaled* s) {
	if (s->surface) host->surface_free(ID, s->surface);
	memset(s, 0, sizeof(*s));
}

static void cacheClear(void) {
	for (int i = 0; i < CACHE_MAX; i++) cacheFree(&cache[i]);
}

static Scaled* cacheFind(const char* key, int w, int h) {
	for (int i = 0; i < CACHE_MAX; i++) {
		Scaled* s = &cache[i];
		if (s->surface && s->w == w && s->h == h && !strcmp(s->key, key)) return s;
	}
	return NULL;
}

// a slot for a new copy, freeing the least recently drawn when all are in use
static Scaled* cacheSlot(void) {
	Scaled* oldest = NULL;
	for (int i = 0; i < CACHE_MAX; i++) {
		Scaled* s = &cache[i];
		if (!s->surface) return s;
		if (!oldest || s->used < oldest->used) oldest = s;
	}
	cacheFree(oldest);
	return oldest;
}

// Draws image, known by key, fitted inside rect; writes where it went to
// drawn.
static int pinned = 0; // the art goes to its place's top (left), as a description is under (beside) it

static MosaicRect fitted(const MosaicRect* rect, int w, int h) {
	MosaicRect to = FL_contain(rect, w, h);
	return pinned ? FL_pin(&to, rect, position) : to;
}

static void drawImage(MosaicSurface* screen, const char* key, const MosaicSurface* image, const MosaicRect* rect,
		MosaicRect* drawn) {
	int w = 0, h = 0;
	if (rect->w <= 0 || rect->h <= 0 || host->surface_size(image, &w, &h) != 0) return;
	MosaicRect to = fitted(rect, w, h);
	if (to.w <= 0 || to.h <= 0) return;
	*drawn = to;
	Scaled* hit = cacheFind(key, to.w, to.h);
	if (hit) {
		hit->used = frame;
		host->blit(screen, hit->surface, NULL, &to);
		return;
	}
	if (strlen(key) < sizeof(hit->key)) {
		MosaicSurface* copy = host->surface_scale(ID, image, to.w, to.h, MOSAIC_FIT_STRETCH);
		if (copy) {
			Scaled* s = cacheSlot();
			snprintf(s->key, sizeof(s->key), "%s", key);
			s->w = to.w;
			s->h = to.h;
			s->surface = copy;
			s->used = frame;
			host->blit(screen, copy, NULL, &to);
			return;
		}
	}
	// no copy: this frame straight from the cache's image
	host->blit(screen, image, NULL, &to);
}

///////////////////////////////////////
// the selected game's description, drawn once into a surface of its own

#define DESC_FONT MOSAIC_FONT_SMALL

static char desc_for[MAX_PATH_LEN]; // the entry path it's for; "" for none yet
static char desc_text[512];         // its description, or "" for none
static MosaicSurface* desc_surface; // its lines, desc_w wide and desc_h tall; NULL before they're drawn
static int desc_w = 0, desc_h = 0, desc_lines = 0;
static unsigned long desc_since = 0; // when the selection moved to it
static int scrolling = 0;            // the description is taller than its place, so it scrolls once settled
static int drawn = 0;                // layout_render ran since tick last asked for a frame
static int scroll_logged = 0;

static void descFree(void) {
	if (desc_surface) host->surface_free(ID, desc_surface);
	desc_surface = NULL;
	desc_w = desc_h = desc_lines = 0;
}

static void descReset(void) {
	descFree();
	desc_for[0] = '\0';
	desc_text[0] = '\0';
	scrolling = 0;
}

// the selected game's description, read when the selection moved to it
static const char* descFor(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	if (!path || strlen(path) >= sizeof(desc_for)) return "";
	if (!desc_for[0] || strcmp(path, desc_for) != 0) {
		descFree();
		snprintf(desc_for, sizeof(desc_for), "%s", path);
		desc_since = now_ms;
		scrolling = 0;
		scroll_logged = 0;
		if (!MOSAIC_HOST_HAS(host, entry_info) || host->entry_info(entry, "description", desc_text, sizeof(desc_text)) != 1)
			desc_text[0] = '\0';
	}
	return desc_text;
}

static int measure(const char* text, int len, void* ctx) {
	(void)ctx;
	char part[sizeof(desc_text)];
	if (len < 0 || (size_t)len >= sizeof(part)) return 1 << 30;
	memcpy(part, text, (size_t)len);
	part[len] = '\0';
	int w = host->text_width(DESC_FONT, part);
	return w < 0 ? 1 << 30 : w;
}

// draws the description's lines, w wide, into desc_surface
static void descDraw(int w, MosaicColor color) {
	descFree();
	int line_h = host->text_height(DESC_FONT);
	if (w <= 0 || line_h <= 0) return;
	FLLine lines[FL_DESC_LINES];
	int count = FL_wrap(desc_text, w, measure, NULL, lines, FL_DESC_LINES);
	if (count * line_h > 4096) count = 4096 / line_h;
	if (count <= 0) return;
	desc_surface = host->surface_new(ID, w, count * line_h);
	if (!desc_surface) return;
	for (int i = 0; i < count; i++) {
		char part[sizeof(desc_text)];
		snprintf(part, sizeof(part), "%.*s", lines[i].len, desc_text + lines[i].start);
		host->draw_text(desc_surface, DESC_FONT, part, 0, i * line_h, color, w);
	}
	desc_w = w;
	desc_h = count * line_h;
	desc_lines = count;
}

// the gap between the art and the description, and between a description's end and its start again
static int descGap(void) {
	return FL_PADDING * host->screen_scale();
}

// Draws the description in rect, scrolled as far as it has since the
// selection settled.
static void drawDesc(MosaicSurface* screen, const MosaicRect* rect, MosaicColor color) {
	if (rect->w <= 0 || rect->h <= 0) return;
	if (!desc_surface || desc_w != rect->w) {
		descDraw(rect->w, color);
		if (!desc_surface) return;
		char message[160];
		snprintf(message, sizeof(message), "description %i line%s, %i tall in %ix%i at %i,%i", desc_lines,
			desc_lines == 1 ? "" : "s", desc_h, rect->w, rect->h, rect->x, rect->y);
		host->log(ID, message);
	}
	int gap = 2 * host->text_height(DESC_FONT);
	scrolling = desc_h > rect->h;
	int offset = FL_scrollOffset((long)(now_ms - desc_since), desc_h, rect->h, gap,
		FL_DESC_SPEED * host->screen_scale());
	if (offset > 0 && !scroll_logged) {
		scroll_logged = 1;
		host->log(ID, "description scrolling");
	}
	MosaicRect src[2], dst[2];
	int parts = FL_scrollParts(rect, desc_h, gap, offset, src, dst);
	for (int i = 0; i < parts; i++) host->blit(screen, desc_surface, &src[i], &dst[i]);
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static int layoutOf(const MosaicListView* view, FLLayout* layout) {
	MosaicRect header = MOSAIC_VIEW_HAS(view, header) ? view->header : (MosaicRect){ 0, 0, 0, 0 };
	return FL_layoutAt(&view->area, &view->status, &header, host->screen_scale(), view->show_art, position, layout);
}

// logs the layout each time its shape changes, such as when art turns off
static void logLayout(const FLLayout* layout) {
	static FLLayout logged;
	static int any = 0;
	if (any && logged.row_w == layout->row_w && logged.top_w == layout->top_w && logged.panel.x == layout->panel.x &&
		logged.panel.y == layout->panel.y && logged.panel.w == layout->panel.w && logged.panel.h == layout->panel.h) {
		return;
	}
	any = 1;
	logged = *layout;
	char message[160];
	snprintf(message, sizeof(message), "rows %i wide (%i at the top), panel %i,%i %ix%i", layout->row_w, layout->top_w,
		layout->panel.x, layout->panel.y, layout->panel.w, layout->panel.h);
	host->log(ID, message);
}

static int hiddenName(const MosaicListView* view, int index) {
	return MOSAIC_VIEW_HAS(view, names_hidden) && view->names_hidden && index >= 0 && index < view->count &&
		view->names_hidden[index];
}

// image fitted inside rect, straight from it; writes where it went to drawn
static void drawStraight(MosaicSurface* screen, const MosaicSurface* image, const MosaicRect* rect, MosaicRect* drawn) {
	int w = 0, h = 0;
	if (rect->w <= 0 || rect->h <= 0 || host->surface_size(image, &w, &h) != 0) return;
	MosaicRect to = fitted(rect, w, h);
	if (to.w > 0 && to.h > 0 && host->blit(screen, image, NULL, &to) == 0) *drawn = to;
}

// Draws the selected entry's art in the panel, writing the rect around
// what it drew to drawn (w 0 for nothing), and its description when it
// has one, writing 1 to described. Returns 1 when it has art (drawn, or
// still loading), or 0 when it has none.
static int drawPanel(MosaicSurface* screen, const MosaicListView* view, const FLLayout* layout,
		const MosaicEntry* entry, MosaicRect* drawn, int* described) {
	*drawn = (MosaicRect){ layout->panel.x, layout->panel.y, 0, 0 };
	*described = 0;
	scrolling = 0;
	if (!view->show_art || layout->panel.w <= 0) return 0;
	const char* path = host->entry_path(entry);
	if (!path) return 0;
	MosaicRect art_area = layout->panel, desc_area = layout->panel;
	pinned = show_desc && descFor(entry)[0];
	if (pinned) FL_descParts(&layout->panel, position, descGap(), &art_area, &desc_area);

	const char* state = stateFor(entry);
	const MosaicSurface* shot = NULL;
	int shot_state = state[0] ? host->art_load(state, &shot) : MOSAIC_ART_MISSING;
	int has_preview = shot_state != MOSAIC_ART_MISSING;

	// a plugin's replacement, in the box's place
	const MosaicSurface* replaced = NULL;
	if (MOSAIC_HOST_HAS(host, art_replacement) && host->art_replacement(view->selected, &replaced) != 1) replaced = NULL;

	const MosaicSurface* box = NULL;
	int box_state = MOSAIC_ART_MISSING, box_kind = MOSAIC_ART_BOX;
	if (!replaced && !(has_preview && prefer_preview)) {
		box_state = host->art_get(entry, MOSAIC_ART_BOX, &box);
		if (box_state == MOSAIC_ART_MISSING) {
			box_kind = MOSAIC_ART_THUMBNAIL;
			box_state = host->art_get(entry, MOSAIC_ART_THUMBNAIL, &box);
		}
	}

	int show = replaced ? FL_replacedArt(view->show_art, prefer_preview, has_preview)
		: FL_panelArt(view->show_art, prefer_preview, box_state != MOSAIC_ART_MISSING, has_preview);
	MosaicRect box_rect, preview_rect, box_drawn = *drawn, preview_drawn = *drawn;
	FL_panelParts(&art_area, show, FL_PADDING * host->screen_scale(), &box_rect, &preview_rect);
	if ((show & FL_SHOW_BOX) && replaced) {
		drawStraight(screen, replaced, &box_rect, &box_drawn);
	} else if ((show & FL_SHOW_BOX) && box_state == MOSAIC_ART_READY && box) {
		char key[MAX_PATH_LEN + 8];
		snprintf(key, sizeof(key), "%s#%i", path, box_kind);
		drawImage(screen, key, box, &box_rect, &box_drawn);
	}
	if ((show & FL_SHOW_PREVIEW) && shot_state == MOSAIC_ART_READY && shot) {
		drawImage(screen, state, shot, &preview_rect, &preview_drawn);
	}
	*drawn = FL_around(&box_drawn, &preview_drawn);
	if (pinned) {
		// under (beside) what's drawn, or the whole panel while there's no art
		MosaicRect rest = FL_descRest(&layout->panel, show ? drawn : NULL, position, descGap());
		if (show && drawn->w <= 0) rest = desc_area; // still loading: where it'll be once the art's in
		drawDesc(screen, &rest, themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF));
		*described = 1;
	}
	return show != 0;
}

static int layoutInit(const MosaicListView* view) {
	FLLayout layout;
	if (layoutOf(view, &layout) != 0) {
		host->log(ID, "the screen is too small for the list, using the built-in list");
		return 1;
	}
	logLayout(&layout);
	return 0;
}

static int layoutInput(const MosaicInput* input, const MosaicListView* view, unsigned int* consumed) {
	(void)input;
	(void)consumed;
	// the rows move as the built-in list's do, so every button stays the launcher's
	FLLayout layout;
	return layoutOf(view, &layout) == 0 ? 0 : -1;
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	FLLayout layout;
	if (layoutOf(view, &layout) != 0) return -1;
	logLayout(&layout);
	frame++;

	const MosaicEntry* selected = host->list_get(view->list, view->selected);
	// the panel first, so row decorators can see it, with art decorators under its art
	MosaicRect art = { 0, 0, 0, 0 };
	int described = 0;
	int has_art = selected ? drawPanel(screen, view, &layout, selected, &art, &described) : 0;
	drawn = 1;
	if (art.w > 0 && art.h > 0 && !described && MOSAIC_HOST_HAS(host, decorate_art))
		host->decorate_art(screen, view->selected, &art);

	int rows = FL_rows(&layout, &view->area);
	if (view->rows > 0 && view->rows < rows) rows = view->rows;
	int first = FL_window(view->start, view->selected, view->count, rows);
	int text_h = host->text_height(MOSAIC_FONT_LARGE);
	MosaicColor pill = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	MosaicColor text = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor text_selected = themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000);

	for (int slot = 0; slot < rows && first + slot < view->count; slot++) {
		int index = first + slot;
		const MosaicEntry* entry = host->list_get(view->list, index);
		if (!entry) continue;
		int is_selected = index == view->selected;
		MosaicRect row = FL_row(&layout, slot);
		if (row.w <= 0) continue;
		int shown = FL_nameShown(view->show_names, hiddenName(view, index), view->show_art, is_selected, has_art);
		MosaicRect name = { row.x, row.y, 0, row.h };
		if (shown) {
			char label[256];
			int w = host->text_truncate(MOSAIC_FONT_LARGE, host->entry_name(entry), row.w - 2 * layout.name_pad, label,
				sizeof(label));
			if (w >= 0 && text_h >= 0) {
				name = FL_name(&layout, &row, w);
				if (is_selected) host->fill_round_rect(screen, &name, name.h / 2, pill);
				host->draw_text(screen, MOSAIC_FONT_LARGE, label, name.x + layout.name_pad, row.y + (row.h - text_h + 1) / 2,
					is_selected ? text_selected : text, name.w - 2 * layout.name_pad);
			}
		}
		host->decorate_cell(screen, index, &row, name.w > 0 ? &name : NULL);
	}
	return 0;
}

static void postReturn(void) {
	// a game may have saved a state: look again, and copy its preview again
	state_for[0] = '\0';
	cacheClear();
	descReset();
}

static void tick(unsigned long now) {
	now_ms = now;
	// a frame each frame only while the description scrolls, and only while
	// the layout is still drawn (it drew the last frame asked for)
	if (!scrolling || !drawn || (long)(now - desc_since) < FL_DESC_WAIT_MS) return;
	drawn = 0;
	host->request_redraw();
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, art_get) || !MOSAIC_HOST_HAS(host, decorate_cell)) {
		host->log(ID, "this launcher can't draw a layout");
		return 1;
	}
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	prefer_preview = strcmp(host->cfg_get("prefer_preview", "on"), "off") != 0;
	position = FL_position(host->cfg_get("position", "right"));
	show_desc = strcmp(host->cfg_get("description", "off"), "on") == 0;
	state_for[0] = '\0';
	preview[0] = '\0';
	desc_surface = NULL;
	descReset();
	drawn = 0;
	char message[96];
	snprintf(message, sizeof(message), "art %s, description %s", FL_positionName(position), show_desc ? "on" : "off");
	host->log(ID, message);
	return 0;
}

static void quit(void) {
	cacheClear();
	descFree();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.post_return = postReturn,
	.tick = tick,
	.layout_init = layoutInit,
	.layout_input = layoutInput,
	.layout_render = layoutRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
