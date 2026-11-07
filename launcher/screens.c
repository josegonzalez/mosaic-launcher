#include "launcher.h"

static const Screen gamelist = {
	.handleInput = GameList_handleInput,
	.render = GameList_render,
};
static const Screen switcher = {
	.handleInput = Switcher_handleInput,
	.render = Switcher_render,
};
static const Screen quickmenu = {
	.handleInput = QuickMenu_handleInput,
	.render = QuickMenu_render,
	.rendersWhileLaunching = 1,
};

void Screens_init(void) {
	GameList_registerActions();
}

const Screen* Screen_get(int id) {
	switch (id) {
		case SCREEN_QUICKMENU: return &quickmenu;
		case SCREEN_GAMESWITCHER: return &switcher;
		default: return &gamelist;
	}
}

int currentScreen;
int lastScreen;
int show_setting;
unsigned long now;
int selected;
int total;
int gsanimdir;
SDL_Surface* blackBG;

SDL_Surface* tmpOldScreen;
SDL_Surface* switcherSur;
int ow;

int qm_row;
int qm_col;
int qm_slot;
int qm_shift;
int qm_slots;

int selected_row;
float targetY;
float previousY;
int is_scrolling;
bool list_show_entry_names;
char folderBgPath[1024];
int globallpillW = 0;
