#ifndef PLUGINS_H
#define PLUGINS_H

// Finding, loading and calling plugins. See sdk/mosaic_plugin.h for the API
// they're written against.
//
// Plugins live in a directory holding plugin.txt and lib/<arch>/plugin.so:
//
//   Launcher.pak/plugins/<name>.plugin/   bundled, off unless turned on
//   Tools/<platform>/<name>.pak/          installed separately, on by default
//
// MOSAIC_LAUNCHER_PLUGIN_PATH, a colon-separated list of directories holding
// such plugin directories, replaces the bundled location (for desktop runs
// and tests).

#include <stddef.h>
#include "actions.h"
#include "mosaic_plugin.h"
#include "pluginset.h"
#include "launchcmd.h"
#include "layoutmodel.h"

#define PLUGIN_STATE_DIR USERDATA_PATH "/launcher"
#define PLUGIN_CHOICES_PATH PLUGIN_STATE_DIR "/plugins.txt"
#define PIN_PATH PLUGIN_STATE_DIR "/pin.txt" // the launcher's PIN; see pin.h
#define ROOT_SLOT "root" // the exclusive slot root_replace belongs to
#define LAYOUT_SLOT "layout" // the exclusive slot layout_init, layout_input and layout_render belong to
#define TRANSITION_SLOT "transition" // the exclusive slot asked how folders push and pop and screens change
#define LAUNCH_SLOT "launch" // the exclusive slot asked how a launch begins
#define BACKGROUND_SLOT "background" // the exclusive slot background_render belongs to

// True when the plugin's struct is new enough to have this field. A plugin
// built against an older header has a smaller struct, and reading past its
// end would be reading garbage.
#define Plugin_has(p, field) ((p)->size >= offsetof(MosaicPlugin, field) + sizeof((p)->field))

// The loaded plugins, in load order.
const MosaicPlugin* Plugins_at(int i);

// Bindings plugins added to a screen (an enum MosaicScreen), after the
// screen's own. Returns how many, and points *bindings at them.
int Plugins_bindings(int screen, const Binding** bindings);
int Plugins_bindButton(int screen, int button, int trigger, const char* action_id);

#define PLUGIN_BINDING_MAX 16
#define PLUGIN_OVERRIDE_MAX 16

// Bindings plugins put on a screen's buttons in place of the screen's own
// (override_button). Each takes its button from the screen's own bindings
// and from plugins' bind_button ones. One with a NULL action leaves the
// button doing nothing. Returns how many, and points *bindings at them.
int Plugins_overrides(int screen, const Binding** bindings);

///////////////////////////////////////
// Hooks, called from the launcher. Each runs every loaded plugin's hook in
// load order; with no plugins loaded they change nothing.

// Appends plugins' entries to the root list.
void Plugins_rootEntries(Array* root);

// The root slot owner's list in place of the stock root, or NULL to build
// stock's. Sets *with_entries when the owner wants plugins' root_entries
// added to it.
Array* Plugins_rootReplace(int* with_entries);

// The first plugin list for a path it owns, or NULL.
Array* Plugins_virtualDir(const char* path);

// Drops entries any plugin hides. Takes ownership of entries.
Array* Plugins_filter(Array* entries, const char* dir_path);

// Lets plugins rename entries. Returns 1 if any name changed.
int Plugins_displayNames(Array* entries);

// Sorts entries by plugins' sort_compare chain, keeping the current order
// where none has an opinion.
void Plugins_sort(Array* entries, const char* dir_path);

// Shows plugins the whole list, which they can rename entries in. Returns 1
// if any name changed.
int Plugins_listReady(Array* entries, const char* dir_path);

// Asks plugins' hide_name about each entry: one byte per entry, 1 where
// one hid its name, or NULL when none did. The caller frees it.
unsigned char* Plugins_hideNames(Array* entries, const char* dir_path);

// Asks plugins before a launch. Returns 1 to go ahead, 0 if one cancelled.
// Going ahead also arranges for post_return on the next start.
int Plugins_preLaunch(const char* path, int type);

// The command a plugin's pre_launch replaced the last launch with, or NULL
// to launch as stock does.
const LaunchCmd* Plugins_launchReplacement(void);

// The first plugin's image for an entry, for ART_THUMBNAIL, ART_FOLDER_BG,
// ART_LIST_BG or one of the kinds only plugins supply (see art.h). Returns
// 1 if a plugin answered.
int Plugins_artPath(const Entry* entry, int kind, char* out, size_t size);

// 1 while any plugin generates art.
int Plugins_hasArtSurface(void);

// The first plugin's generated image for an entry, for a kind as
// Plugins_artPath takes, now the launcher's (DRAW_HOST, read-only) for the
// caller to keep and free, or NULL. One that returns a surface it didn't
// make logs and is no longer asked. Returns NULL from inside an art_surface.
struct SDL_Surface* Plugins_artSurface(const Entry* entry, int kind);

// The first plugin's new full path for a recent whose file is missing.
// Returns 1 if a plugin answered.
int Plugins_repairPath(const char* missing, char* out, size_t size);

// 1 while the layout slot's owner draws the game list.
int Plugins_hasLayout(void);

// The layout's input for this frame, before the game list's own: the
// buttons it consumed are taken off the pad's presses, repeats and
// releases for the rest of the frame. Does nothing without a layout.
void Plugins_layoutInput(const MosaicListView* view);

// Lends surface to the layout, clipped to view->area, to draw the game
// list on. Returns 1 if it drew, or 0 to draw the built-in list: there's no
// layout, it declined, or it failed, which logs and turns it off. When it
// drew, *selected has the last cell it passed to decorate_cell for the
// selected entry, or is empty.
int Plugins_layoutRender(struct SDL_Surface* surface, const MosaicListView* view, ListSelection* selected);

// 1 while the background slot's owner is asked to draw.
int Plugins_hasBackground(void);

// Lends surface, filled with stock's background number serial, to the
// background slot's owner with the selected entry (NULL in an empty
// folder). Returns 1 if it drew, or 0 for stock's: there's no owner, the
// surface is already lent, or it failed, which logs and turns it off.
int Plugins_backgroundRender(struct SDL_Surface* surface, const Entry* selected, unsigned serial);

// The owner wasn't asked this frame, so stock's background shows.
void Plugins_backgroundSkip(void);

// What goes on the background layer, an enum BackgroundShow (see
// layoutmodel.h), on or off the game list, with stock's background now
// number serial.
int Plugins_backgroundShow(int on_gamelist, unsigned serial);

// 1 while any plugin decorates rows.
int Plugins_hasDecorators(void);

// Runs every plugin's decorate_row on a row of the built-in list, on surface
// clipped to row->rect, filling in row->size. Does nothing without
// decorators. One that fails logs and is turned off.
void Plugins_decorateRow(struct SDL_Surface* surface, const Entry* entry, MosaicRow* row);

// 1 while any plugin decorates art.
int Plugins_hasArtDecorators(void);

// Runs every plugin's decorate_art on the built-in list's thumbnail at art,
// on surface clipped to below, the free space under it. Does nothing
// without art decorators or room. One that fails logs and is turned off.
void Plugins_decorateArt(struct SDL_Surface* surface, const Entry* entry, const MosaicRect* art, const MosaicRect* below);

// The surface a plugin's art_replace shows in place of the selected entry's
// art, on the built-in list or under a layout, or NULL. Main thread only.
struct SDL_Surface* Plugins_artReplaced(void);

// Once a frame, before the frame is drawn: takes a plugin's art_replace
// surface away once the selection has moved, the game list is left, it
// no longer shows art there, or the surface is no longer the
// plugin's, marking the thumbnail changed.
void Plugins_artReplaceCheck(void);

// Plugins' header widgets on surface, placed in row (see layoutmodel.h) in
// load order. One whose header_width or header_widget fails logs and is
// left out from then on.
void Plugins_header(struct SDL_Surface* surface, HeaderRow* row);

// 1 while a slot's owner is asked about kind (an enum MosaicTransitionKind):
// never for pushes, pops and screen changes with stock's Menu transitions off.
int Plugins_hasTransition(int kind);

// Asks the owner how to draw a kind of transition between two frames (a
// stock_ms long animation for stock), lending it both read-only. Returns
// MOSAIC_STYLE_STOCK (also with no owner or after an error, which logs and
// turns its transitions off), MOSAIC_STYLE_OFF, or MOSAIC_STYLE_CUSTOM with
// the transition, its duration capped, in *out. old_sel and new_sel are
// where the game list's selection is in each frame, or NULL for unknown.
// Custom then draws with Plugins_transitionFrame and ends with
// Plugins_transitionEnd.
int Plugins_transitionBegin(int kind, struct SDL_Surface* old_frame, struct SDL_Surface* new_frame, int stock_ms,
	const ListSelection* old_sel, const ListSelection* new_sel, MosaicTransition* out);

// The owner's transition_frame on surface, lent for the call, at progress.
// Returns 0, or -1 after an error, which logs and turns its transitions
// off; end it then too.
int Plugins_transitionFrame(struct SDL_Surface* surface, float progress);

// Takes the frames back.
void Plugins_transitionEnd(void);

// Plugins' input_filter, in load order, right after the pad is read: the
// buttons they consumed are taken off its presses, repeats and releases for
// the rest of the frame, and a power press they took still powers off when
// held as long as stock's. Does nothing without filters.
void Plugins_inputFilter(unsigned long now);
void Plugins_tick(unsigned long now);

// Runs plugins' selection_changed once the game list's selection settles
// on another entry. Called every frame after input, before drawing.
void Plugins_selection(unsigned long now);

// Plugins' screens (see screens.h for SCREEN_PLUGIN, the one the top of
// them shows as). The top screen's input, once a frame while it shows: a
// close, an error or an input hook's MOSAIC_SCREEN_CLOSE goes back to what
// it opened over.
void Plugins_screenInput(void);

// Lends surface to the top screen's render, with view.
void Plugins_screenRender(struct SDL_Surface* surface, const MosaicScreenView* view);

// The top screen's MosaicScreenHooks flags, or 0 with none open.
int Plugins_screenFlags(void);

// What shows, as host->current_screen returns it.
const char* Plugins_currentScreen(void);

// Once a frame after input: closes the open screens, running their exit,
// once a built-in screen opened over them, such as from an FN button.
void Plugins_screensCheck(void);

// Closes every open screen, running their exit, as for a launch or quit.
void Plugins_screensClose(void);

// 1 when plugins' overlays draw over screen (a SCREEN_* id): one has
// overlay_render, and screen takes them.
int Plugins_overlaysOver(int screen);

// Every plugin's overlay_render, in load order, on surface, lent for the
// call. One that fails logs and is left out from then on.
void Plugins_overlays(struct SDL_Surface* surface, const MosaicScreenView* view);

void Plugins_beforeSleep(void);
void Plugins_afterResume(void);
void Plugins_beforePoweroff(void);

// Loads plugins unless they're paused after a crash. Call once, before
// Menu_init, since building the root list already runs plugin hooks.
void Plugins_init(void);

// Call after the first frame is on screen. Until then, a crash leaves a
// marker that pauses plugins on the next start.
void Plugins_started(void);

void Plugins_quit(void);

// Every plugin found, loading or not, without loading any. The settings
// screens use this to list them.
int Plugins_discover(const PluginManifest** out);

// Launcher.pak's directory, from where this binary runs, or
// MOSAIC_LAUNCHER_PAK_DIR when set. Returns 0 if it can't tell.
int Plugins_pakDir(char* out, size_t size);

// Creates PLUGIN_STATE_DIR, and its parents, if they're missing.
void Plugins_ensureStateDir(void);

// How many plugins loaded.
int Plugins_count(void);

// Moves a PIN simple mode kept in its own cfg to PIN_PATH (Pin_adopt).
void Plugins_adoptPin(void);

#endif
