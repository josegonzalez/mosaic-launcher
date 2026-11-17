// Jump letter: holding a button on the game list, SELECT by default,
// shows a strip of the sections the list has, its first letters in its
// own order (# for digits and symbols), with the selected entry's
// section lit. While it's held the d-pad moves along the strip, letting
// go jumps to the first entry of the section it's on, and B closes the
// strip without jumping. A quick tap still does what the button did, as
// the strip opens only once it's held past stock's tap.
//
// A plugin that orders the list (sort_compare) can give its own sections,
// such as years or publishers, through section_key; host->entry_section
// asks it, and without an answer the section is the first letter of the
// name the list shows, accents folded to their letter.
//
// The "button" setting puts the jump-letter.strip action on the game
// list, only once every plugin has loaded (the first tick) and only when
// no other plugin has an action on that button there; SELECT's own tap,
// the game switcher, doesn't count, as the hold leaves it be. The action
// run some other way, such as from an FN button, opens the strip until A
// jumps or B closes it.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "jump_model.h"

#define ID "jump-letter"
#define ACTION_ID ID ".strip"
#define FONT MOSAIC_FONT_LARGE
#define NAME_FONT MOSAIC_FONT_SMALL
#define PATH_LEN 1024

static const MosaicHost* host;
static int bound = 0;  // the button was looked at, on the first tick
static int button = -1; // the button it's on, or -1
static unsigned long last_now = 0;

// this frame's button, as the input filter saw it
static int pressed_now = 0, held_now = 0, chord_now = 0;

static JLHold hold;
static JLSections sections;
static int at = 0, opened_at = 0; // the lit section, and the one it opened on
static char opened_dir[PATH_LEN]; // the folder it opened in
static int pending = -1; // the entry to jump to on the next tick

static void say(const char* message) {
	host->log(ID, message);
}

static int onGameList(void) {
	const char* screen = host->current_screen();
	return screen && !strcmp(screen, "gamelist");
}

///////////////////////////////////////
// sections

static void keyOf(int i, char* out, size_t size, void* data) {
	const MosaicList* list = data;
	const MosaicEntry* entry = host->list_get(list, i);
	const char* dir = host->current_path();
	if (MOSAIC_HOST_HAS(host, entry_section) && host->entry_section(entry, dir, out, size) == 1) return;
	JL_letter(host->entry_name(entry), out, size);
}

// The strip for the list showing, lit on the selected entry's section.
static int openStrip(int sticky) {
	const MosaicList* list = host->current_list();
	const char* dir = host->current_path();
	int count = list ? host->list_count(list) : 0;
	if (count <= 0 || !dir) return 0;
	int lit = -1;
	JL_sections(&sections, count, keyOf, (void*)list, host->selected_index(), &lit);
	at = opened_at = lit < 0 ? 0 : lit;
	snprintf(opened_dir, sizeof(opened_dir), "%s", dir);
	if (sticky) JL_holdOpenSticky(&hold);
	char message[PATH_LEN + 256];
	int n = snprintf(message, sizeof(message), "strip %s:", dir);
	for (int k = 0; k < sections.count && n > 0 && n < (int)sizeof(message); k++) {
		n += snprintf(message + n, sizeof(message) - (size_t)n, "%s %s", k ? "," : "", sections.keys[k]);
	}
	if (n > 0 && n < (int)sizeof(message)) snprintf(message + n, sizeof(message) - (size_t)n, " at %s", sections.keys[at]);
	say(message);
	host->request_redraw();
	return 1;
}

static void closeStrip(const char* why) {
	hold.state = JL_IDLE;
	say(why);
	host->request_redraw();
}

// Jumps to the lit section's first entry, on the next tick, unless it's
// still the one it opened on, so a hold that only changed the colour
// temperature (SELECT and the volume keys) moves nothing.
static void jump(void) {
	const char* dir = host->current_path();
	char message[JL_KEY_LEN + 32];
	if (!dir || strcmp(dir, opened_dir) != 0) snprintf(message, sizeof(message), "the folder changed: no jump");
	else if (at == opened_at) snprintf(message, sizeof(message), "stayed on %s", sections.keys[at]);
	else {
		pending = sections.first[at];
		snprintf(message, sizeof(message), "jump to %s", sections.keys[at]);
	}
	say(message);
	host->request_redraw();
}

static void move(int delta) {
	int to = JL_step(at, delta, sections.count);
	if (to == at) return;
	at = to;
	char message[JL_KEY_LEN + 8];
	snprintf(message, sizeof(message), "at %s", sections.keys[at]);
	say(message);
	host->request_redraw();
}

///////////////////////////////////////
// the action and its button

static int canStrip(const char* path, void* data) {
	(void)path;
	(void)data;
	const MosaicList* list = host->current_list();
	return onGameList() && list && host->list_count(list) > 0;
}

static void runStrip(const char* path, void* data) {
	(void)path;
	(void)data;
	if (hold.state != JL_IDLE) return;
	// its own button's press arms the hold; anything else opens it until A or B
	if (pressed_now) JL_holdArm(&hold, last_now);
	else openStrip(1);
}

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "SELECT", MOSAIC_BUTTON_SELECT },
	{ "Y", MOSAIC_BUTTON_Y },
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
};

// The action another plugin has on button on the game list, into who, or
// 0 when there's none. The game list's own SELECT tap stays as it is.
static int taken(int b, char* who, size_t size) {
	if (!MOSAIC_HOST_HAS(host, action_info)) return 0;
	int count = host->actions_count();
	for (int i = 0; i < count; i++) {
		MosaicActionInfo info;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (host->action_info(i, &info) != 0) continue;
		int n = info.binding_count < MOSAIC_ACTION_BINDINGS ? info.binding_count : MOSAIC_ACTION_BINDINGS;
		for (int k = 0; k < n; k++) {
			const MosaicActionBinding* binding = &info.bindings[k];
			if (binding->screen != MOSAIC_SCREEN_GAMELIST || binding->button != b) continue;
			if (binding->by == MOSAIC_BIND_SCREEN && !strcmp(info.id, "gamelist.switcher")) continue;
			snprintf(who, size, "%s", info.id);
			return 1;
		}
	}
	return 0;
}

static void bindSetting(void) {
	const char* name = host->cfg_get("button", "SELECT");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		char who[64], message[192];
		if (taken(buttons[i].button, who, sizeof(who))) {
			snprintf(message, sizeof(message), "%s is %s's on the game list, so " ACTION_ID " isn't on it", name, who);
		}
		else if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
			snprintf(message, sizeof(message), "couldn't put " ACTION_ID " on %s", name);
		}
		else {
			button = buttons[i].button;
			snprintf(message, sizeof(message), ACTION_ID " on %s", name);
		}
		say(message);
	}
}

///////////////////////////////////////
// hooks

#define BIT(b) MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_##b)
#define DPAD (BIT(UP) | BIT(DOWN) | BIT(LEFT) | BIT(RIGHT))
// what the strip takes while it shows: all but MENU, power and the FN buttons
#define STRIP_TAKES (DPAD | BIT(A) | BIT(B) | BIT(X) | BIT(Y) | BIT(L1) | BIT(R1) | BIT(L2) | BIT(R2) | BIT(START) | BIT(SELECT))
// what else held with the button makes a chord rather than a hold
#define CHORDS (STRIP_TAKES & ~DPAD)

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	unsigned int mine = button >= 0 ? MOSAIC_BUTTON_BIT(button) : 0;
	pressed_now = (input->pressed & mine) != 0;
	held_now = (input->held & mine) != 0;
	chord_now = (input->held & CHORDS & ~mine) != 0;
	if (hold.state == JL_CANCELLED) {
		// B closed it: its button's release is still the strip's
		*consumed |= mine;
		if (!held_now) hold.state = JL_IDLE;
		return 0;
	}
	if (hold.state != JL_OPEN) return 0;
	if (!onGameList()) {
		closeStrip("left the game list: closed");
		return 0;
	}
	*consumed |= STRIP_TAKES;
	if (input->repeated & (BIT(LEFT) | BIT(UP))) move(-1);
	if (input->repeated & (BIT(RIGHT) | BIT(DOWN))) move(1);
	if (input->pressed & BIT(B)) {
		JL_holdCancel(&hold);
		if (hold.state == JL_CANCELLED && !held_now) hold.state = JL_IDLE;
		say("cancelled");
		host->request_redraw();
	}
	else if (hold.sticky && (input->pressed & BIT(A))) {
		hold.state = JL_IDLE;
		jump();
	}
	else if (!hold.sticky && (input->released & mine)) {
		if (JL_holdRelease(&hold)) jump();
	}
	else if (!hold.sticky && !held_now) {
		// let go without a release reaching it, such as under the lock
		closeStrip("let go unseen: closed");
	}
	return 0;
}

static void tick(unsigned long now) {
	last_now = now;
	if (!bound) {
		// once every plugin has bound its own buttons
		bound = 1;
		bindSetting();
	}
	if (pending >= 0) {
		int to = pending;
		pending = -1;
		if (host->select_index(to) != 0) say("couldn't jump");
	}
	if (hold.state == JL_ARMED && !onGameList()) hold.state = JL_IDLE;
	if (JL_holdFrame(&hold, held_now, chord_now, now) && !openStrip(0)) hold.state = JL_IDLE;
}

static void beforeSleep(void) {
	if (hold.state == JL_OPEN) closeStrip("asleep: closed");
	hold.state = JL_IDLE;
}

static int pad(void) {
	int scale = host->screen_scale();
	return 12 * (scale > 0 ? scale : 1); // stock's BUTTON_PADDING, as its pills use
}

static int overlayRender(MosaicSurface* overlay, const MosaicScreenView* view) {
	if (hold.state != JL_OPEN || !view->screen || strcmp(view->screen, "gamelist") != 0 || sections.count <= 0) return 0;
	int accent = host->theme_color(MOSAIC_COLOR_ACCENT);
	int text = host->theme_color(MOSAIC_COLOR_LIST_TEXT);
	if (accent < 0 || text < 0) return -1;
	const MosaicRect* area = &view->area;
	int p = pad();
	int line = host->text_height(FONT);
	if (line <= 0) return -1;

	// the list dimmed, and a band across the middle
	host->fill_rect(overlay, NULL, mosaic_color(0x000000, 160));
	int band_h = line + p * 2;
	MosaicRect band = { area->x + p, area->y + (area->h - band_h) / 2, area->w - p * 2, band_h };
	host->fill_round_rect(overlay, &band, band_h / 2, mosaic_color(0x000000, 224));
	host->fill_round_rect(overlay, &band, band_h / 2, mosaic_color((unsigned int)text, 40));

	// the sections that fit, around the lit one
	int widths[JL_SECTIONS_MAX];
	int max_item = band.w / 3;
	for (int k = 0; k < sections.count; k++) {
		int w = host->text_width(FONT, sections.keys[k]);
		if (w < 0) return -1;
		w += p * 2;
		widths[k] = w < max_item ? w : max_item;
	}
	int first, last;
	int inner = band.w - p * 2;
	JL_window(widths, sections.count, 0, inner, at, &first, &last);
	int used = 0;
	for (int k = first; k <= last; k++) used += widths[k];
	int x = band.x + p + (inner - used) / 2;
	int pill_h = line + p / 2;
	for (int k = first; k <= last; k++) {
		MosaicRect cell = { x, band.y + (band_h - pill_h) / 2, widths[k], pill_h };
		if (k == at) host->fill_round_rect(overlay, &cell, pill_h / 2, mosaic_color((unsigned int)accent, 255));
		int text_y = band.y + (band_h - line) / 2;
		int text_w = host->text_width(FONT, sections.keys[k]);
		int text_x = cell.x + (cell.w - (text_w < cell.w - p ? text_w : cell.w - p)) / 2;
		host->draw_text(overlay, FONT, sections.keys[k], text_x, text_y,
			mosaic_color((unsigned int)text, 255), cell.w - p);
		x += widths[k];
	}

	// where letting go lands, under the band
	const MosaicList* list = host->current_list();
	if (list && sections.first[at] < host->list_count(list)) {
		const char* name = host->entry_name(host->list_get(list, sections.first[at]));
		int name_h = host->text_height(NAME_FONT);
		int name_w = host->text_width(NAME_FONT, name ? name : "");
		int room = band.w - p * 2;
		if (name && name_h > 0 && name_w >= 0) {
			if (name_w > room) name_w = room;
			host->draw_text(overlay, NAME_FONT, name, area->x + (area->w - name_w) / 2, band.y + band_h + p / 2,
				mosaic_color((unsigned int)text, 255), room);
		}
	}
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	bound = 0;
	button = -1;
	pending = -1;
	hold.state = JL_IDLE;
	sections.count = 0;
	if (!MOSAIC_HOST_HAS(host, hold_switch) || !MOSAIC_HOST_HAS(host, open_screen)
			|| !MOSAIC_HOST_HAS(host, action_info)) {
		say("unsupported: this launcher has no input filters, overlays or action list");
		return 1;
	}
	if (host->register_action(ACTION_ID, "AZ", 0, canStrip, runStrip, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.before_sleep = beforeSleep,
	.input_filter = inputFilter,
	.overlay_render = overlayRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
