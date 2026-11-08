#ifndef SCREENS_H
#define SCREENS_H

// The three built-in screens and the state the main loop shares with them.
// Defined in screens.c.

#include <stdbool.h>
#include "api.h"

typedef struct Screen {
	void (*handleInput)(void);
	// Decides what render draws (art to load, layout, resume state) and
	// draws nothing, so a different render can reuse it. Runs right before
	// render on every dirty frame.
	void (*update)(void);
	void (*render)(void);
	// While a game or pak is about to launch, every screen except the quick
	// menu gives way to the launch fade.
	int rendersWhileLaunching;
} Screen;

// The top of plugins' open screens, whichever it is (see plugins.h). Not a
// real view, like SCREEN_MODAL.
#define SCREEN_PLUGIN 101

// Unknown ids get the game list, like stock's input and render chains.
const Screen* Screen_get(int id);

// Registers every screen's actions so FN buttons can bind to them.
void Screens_init(void);
void GameList_registerActions(void);

void QuickMenu_setup(void); // once, after Menu_init
void QuickMenu_enter(void); // puts the cursor back on the first tile
void QuickMenu_refit(void); // keeps the cursor on a tile after the rows change
void QuickMenu_handleInput(void);
void QuickMenu_update(void);
void QuickMenu_render(void);
void Switcher_handleInput(void);
void Switcher_update(void);
void Switcher_render(void);
void GameList_handleInput(void);
void GameList_update(void);
void GameList_render(void);
void LaunchFade_render(void);
void PluginScreen_handleInput(void);
void PluginScreen_render(void);
// Plugins' overlays over what was just drawn, on the topmost layer. Call
// once a frame is drawn, before it's flipped.
void Overlays_render(void);
void Overlays_quit(void);

// main loop
extern int currentScreen;
extern int lastScreen;
extern int show_setting; // 1=brightness,2=volume
extern unsigned long now;
extern int selected;
extern int total;
extern int gsanimdir;
extern SDL_Surface* blackBG;

// one dirty render pass
extern SDL_Surface* tmpOldScreen;
extern SDL_Surface* switcherSur;
extern int ow;
extern int header_w; // how far the status file pill and plugins' header widgets reach left of the status pill, or 0


// game list
extern int selected_row;
extern float targetY;
extern float previousY;
extern int is_scrolling;
extern bool list_show_entry_names;
// The selected row's name shows, so its pill and scrolling name do: names
// show and a plugin's hide_name didn't hide it, or it has no art to stand
// in for it.
extern bool list_show_selected_name;
// The last frame with entries was drawn by a plugin layout, so the stock
// pill, scrolling name and thumbnail stay off.
extern bool list_layout_drawn;
extern char folderBgPath[1024];
extern int globallpillW;

#endif
