// Guest profile: a separate set of recents, favorites, collections and
// play history for lending the device, kept for next time.
//
// A Guest tile on the quick menu (the "tile" setting) switches to the
// guest's profile and back, as does the guest-profile.toggle action, which
// the "button" setting puts on a game list button and an FN button can
// run. Leaving guest mode asks for nothing. While it's on, a GUEST mark
// shows in the header (the "mark" setting). A Clear guest tile (the
// "clear_tile" setting), the guest-profile.clear action, asks first and
// then removes the guest's data, so the next guest starts empty. The
// tile's label is short of "Clear guest data" so it fits the tile.
//
// The launcher does the switching (host->profile_switch, with
// profile.guest in plugin.txt): it swaps recents, the play history and
// the Collections folder in place, and other plugins keep their own data
// for each profile with profile_path. Saves and states are never switched.
// Guest mode stays on through restarts and games until it's switched off;
// with this plugin off, the launcher puts the owner's data back as it
// starts.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "guest_model.h"

#define ID "guest-profile"
#define TOGGLE_ID ID ".toggle"
#define CLEAR_ID ID ".clear"
#define FONT MOSAIC_FONT_SMALL

static const MosaicHost* host;
static int mark_on = 1;
static int mark_logged = 0; // the mark's width, last logged
static const char* mark_text = "GUEST"; // this frame's, translated

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int guest(void) {
	return GP_isGuest(host->profile_name());
}

///////////////////////////////////////
// the actions

static int always(const char* path, void* data) {
	(void)path;
	(void)data;
	return 1;
}

static int isOn(void* data) {
	(void)data;
	return guest();
}

static void toggle(const char* path, void* data) {
	(void)path;
	(void)data;
	int to_guest = !guest();
	if (host->profile_switch(to_guest ? GP_PROFILE : "") != 0) {
		say(to_guest ? "couldn't switch to the guest" : "couldn't switch back to the owner");
		toast(tr("Couldn't switch profiles"));
		return;
	}
	say(to_guest ? "guest on" : "guest off");
	toast(to_guest ? tr("Guest profile on") : tr("Guest profile off"));
	host->request_redraw();
}

static void cleared(void* data) {
	(void)data;
	if (host->profile_clear(GP_PROFILE) != 0) {
		say("couldn't clear all of the guest's data");
		toast(tr("Couldn't clear guest data"));
		return;
	}
	say("cleared the guest's data");
	toast(tr("Guest data cleared"));
	host->request_redraw();
}

static void clearData(const char* path, void* data) {
	(void)path;
	(void)data;
	host->confirm(tr("Clear the guest's recents, favorites,\ncollections and play history?\nSaves and states stay."),
		tr("CLEAR"), cleared, NULL);
}

///////////////////////////////////////
// the header mark

static int pad(void) {
	int s = host->screen_scale();
	return 12 * (s > 0 ? s : 1); // stock's BUTTON_PADDING, as its pills use
}

static int headerWidth(int max_width, int height) {
	(void)height;
	if (!mark_on || !guest()) return 0;
	// translated here: the strings slot's owner is in place only once every plugin's init has run
	mark_text = tr("GUEST");
	int width = GP_markWidth(host->text_width(FONT, mark_text), pad(), max_width);
	if (width != mark_logged) {
		char message[96];
		if (width) snprintf(message, sizeof(message), "mark %i wide", width);
		else snprintf(message, sizeof(message), "no room for the mark in %i", max_width);
		say(message);
		mark_logged = width;
	}
	return width;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	int pill = host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	host->fill_round_rect(screen, rect, rect->h / 2, mosaic_color((unsigned int)pill, 255));
	int y = rect->y + (rect->h - host->text_height(FONT)) / 2;
	host->draw_text(screen, FONT, mark_text, rect->x + pad(), y, mosaic_color((unsigned int)ink, 255), rect->w - pad() * 2);
	return 0;
}

///////////////////////////////////////
// hooks

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, profile_path) || !MOSAIC_HOST_HAS(host, quick_tile) || !MOSAIC_HOST_HAS(host, confirm)) {
		say("unsupported: this launcher has no profiles");
		return 1;
	}
	mark_on = strcmp(host->cfg_get("mark", "on"), "off") != 0;
	if (mark_on && (!MOSAIC_HOST_HAS(host, decorate_cell) || !MOSAIC_HOST_HAS(host, theme_color))) {
		say("this launcher has no header widgets, so there's no guest mark");
		mark_on = 0;
	}

	if (host->register_action(TOGGLE_ID, "Guest", 0, always, toggle, NULL) != 0) {
		say("couldn't register " TOGGLE_ID);
		return 1;
	}
	if (host->register_action(CLEAR_ID, "Clear guest", MOSAIC_ACTION_EDITS, always, clearData, NULL) != 0) say("couldn't register " CLEAR_ID);
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& host->quick_tile(TOGGLE_ID, MOSAIC_QUICK_TOP, NULL, NULL, isOn) != 0) {
		say("couldn't add the Guest tile");
	}
	if (strcmp(host->cfg_get("clear_tile", "on"), "off") != 0
			&& host->quick_tile(CLEAR_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0) {
		say("couldn't add the Clear guest tile");
	}
	int button = GP_button(host->cfg_get("button", "none"));
	if (button >= 0 && host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, TOGGLE_ID) != 0) {
		say("couldn't bind the Guest button");
	}
	say(guest() ? "the guest's profile is live" : "the owner's profile is live");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.header_width = headerWidth,
	.header_widget = headerWidget,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
