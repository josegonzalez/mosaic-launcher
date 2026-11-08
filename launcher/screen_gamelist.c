#include "launcher.h"

static int canResume(void* data) {
	(void)data;
	return total>0 && state.can_resume;
}
static void resumeSelected(void* data) {
	(void)data;
	Entry* entry = state.top->entries->items[state.top->selected];
	state.should_resume = 1;
	Entry_open(entry);

	state.dirty = 1;
}

static int canOpen(void* data) {
	(void)data;
	return total>0;
}
static void openSelected(void* data) {
	(void)data;
	Entry* entry = state.top->entries->items[state.top->selected];
	Entry_open(entry);
	if(entry->type == ENTRY_DIR) {
		state.animationdirection = SLIDE_LEFT;
		total = state.top->entries->count;
	}
	state.dirty = 1;

	if (total>0) readyResume(state.top->entries->items[state.top->selected]);
}

static int canGoBack(void* data) {
	(void)data;
	return state.stack->count>1;
}
static void goBack(void* data) {
	(void)data;
	closeDirectory();
	state.animationdirection = SLIDE_RIGHT;
	total = state.top->entries->count;
	state.dirty = 1;

	if (total>0) readyResume(state.top->entries->items[state.top->selected]);
}

// The previous or next letter in the letter index, as L1 and R1 jump.
static int canJump(void* data) {
	(void)data;
	return total>0;
}
static void jumpTo(int i) {
	if (i<0 || i>=state.top->alphas->count) return;
	selected = state.top->alphas->items[i];
	if (total>MAIN_ROW_COUNT) {
		state.top->start = selected;
		state.top->end = state.top->start + MAIN_ROW_COUNT;
		if (state.top->end>total) state.top->end = total;
		state.top->start = state.top->end - MAIN_ROW_COUNT;
	}
	// from an override, this runs after the frame's own selection update
	if (selected!=state.top->selected) {
		state.top->selected = selected;
		state.dirty = 1;
		readyResume(state.top->entries->items[selected]);
	}
}
static void prevLetter(void* data) {
	(void)data;
	Entry* entry = state.top->entries->items[selected];
	jumpTo(entry->alpha-1);
}
static void nextLetter(void* data) {
	(void)data;
	Entry* entry = state.top->entries->items[selected];
	jumpTo(entry->alpha+1);
}

static void openQuickMenu(void* data) {
	(void)data;
	currentScreen = SCREEN_QUICKMENU;
	QuickMenu_enter();
	state.dirty = 1;
	folderbgchanged = 1; // The background painting code is a clusterfuck, just force a repaint here
	if (!HAS_POWER_BUTTON && !state.simple_mode) PWR_enableSleep();
}
static void openSwitcher(void* data) {
	(void)data;
	currentScreen = SCREEN_GAMESWITCHER;
	state.switcher_selected = 0;
	state.dirty = 1;
}

// Stock's own triggers for MENU and SELECT (a tap), and its check for L1
// and R1, which repeat while held: not while the other shoulder is down or
// the setting overlay has the button.
static int tapped(int button) {
	return button==BTN_MENU ? PAD_tappedMenu(now) : PAD_tappedSelect(now);
}
static int shoulderAlone(int button) {
	int other = button==BTN_L1 ? BTN_R1 : BTN_L1;
	return !PAD_isPressed(other) && !PWR_ignoreSettingInput(button, show_setting);
}

static const Action resumeAction = { "gamelist.resume", "RESUME", canResume, resumeSelected };
static const Action openAction = { "gamelist.open", "OPEN", canOpen, openSelected };
static const Action backAction = { "gamelist.back", "BACK", canGoBack, goBack };
static const Action prevLetterAction = { "gamelist.prev_letter", "PREVIOUS", canJump, prevLetter };
static const Action nextLetterAction = { "gamelist.next_letter", "NEXT", canJump, nextLetter };
static const Action switcherAction = { "gamelist.switcher", "SWITCHER", NULL, openSwitcher };
static const Action quickMenuAction = { "gamelist.quick_menu", "MENU", NULL, openQuickMenu };

// The game list's own bindings, in the three places stock checked them. In
// each group, the first that fires and is available wins.
static const Binding screenBindings[] = {
	{ BTN_MENU, TRIGGER_RELEASE, "MENU", &quickMenuAction, tapped },
	{ BTN_SELECT, TRIGGER_RELEASE, "SELECT", &switcherAction, tapped },
};
static const Binding letterBindings[] = {
	{ BTN_L1, TRIGGER_REPEAT, "L1", &prevLetterAction, NULL, shoulderAlone },
	{ BTN_R1, TRIGGER_REPEAT, "R1", &nextLetterAction, NULL, shoulderAlone },
};
static const Binding bindings[] = {
	{ BTN_RESUME, TRIGGER_RELEASE, "X", &resumeAction },
	{ BTN_A, TRIGGER_PRESS, "A", &openAction },
	{ BTN_B, TRIGGER_PRESS, "B", &backAction },
};
#define COUNT(a) (int)(sizeof(a) / sizeof(a[0]))
#define SCREEN_BINDINGS COUNT(screenBindings)
#define LETTER_BINDINGS COUNT(letterBindings)
#define BINDING_COUNT COUNT(bindings)

static int isOwnAction(const Action* action) {
	return action==&resumeAction || action==&openAction || action==&backAction || action==&prevLetterAction
		|| action==&nextLetterAction || action==&switcherAction || action==&quickMenuAction;
}

// The bindings in effect: the game list's own, less the buttons plugins
// override, with the overrides after its actions, and plugins' shared
// bindings, less the overridden buttons too. With no overrides, this is the
// game list's own bindings and plugins' as they were bound.
typedef struct Effective {
	Binding screen[SCREEN_BINDINGS];
	int screen_count;
	Binding letters[LETTER_BINDINGS];
	int letter_count;
	Binding actions[BINDING_COUNT + PLUGIN_OVERRIDE_MAX];
	int action_count;
	Binding shared[PLUGIN_BINDING_MAX];
	int shared_count;
	const Binding* overrides;
	int override_count;
} Effective;

static void effective(Effective* e) {
	e->override_count = Plugins_overrides(MOSAIC_SCREEN_GAMELIST, &e->overrides);
	e->screen_count = Bindings_drop(screenBindings, SCREEN_BINDINGS, e->overrides, e->override_count, e->screen);
	e->letter_count = Bindings_drop(letterBindings, LETTER_BINDINGS, e->overrides, e->override_count, e->letters);
	e->action_count = Bindings_drop(bindings, BINDING_COUNT, e->overrides, e->override_count, e->actions);
	// a letter jump an override puts back on its shoulder keeps stock's check
	Binding* overridden = &e->actions[e->action_count];
	for (int i = 0; i < e->override_count; i++) e->actions[e->action_count++] = e->overrides[i];
	Bindings_keepChecks(overridden, e->override_count, letterBindings, LETTER_BINDINGS);
	const Binding* extra;
	int extra_count = Plugins_bindings(MOSAIC_SCREEN_GAMELIST, &extra);
	e->shared_count = Bindings_drop(extra, extra_count, e->overrides, e->override_count, e->shared);
}

// The actions a shared button offers, kept until the user picks one.
#define ENTRY_MENU_MAX 16
static const Action* menu_actions[ENTRY_MENU_MAX];
static int menu_count = 0;

static void menuPicked(int index, void* data) {
	(void)data;
	if (index < 0 || index >= menu_count) return;
	const Action* action = menu_actions[index];
	// it may have stopped being available while the menu was open
	if (Action_available(action)) Action_run(action);
}

// Two or more available actions share the pressed button: list them for the
// selected entry and run the one the user picks.
static void openEntryMenu(const Action** actions, int n) {
	const char* labels[ENTRY_MENU_MAX];
	menu_count = n < ENTRY_MENU_MAX ? n : ENTRY_MENU_MAX;
	for (int i = 0; i < menu_count; i++) {
		menu_actions[i] = actions[i];
		labels[i] = TR(actions[i]->label);
	}
	Entry* entry = total > 0 ? state.top->entries->items[state.top->selected] : NULL;
	Modal_choose(entry ? entry->name : "", labels, menu_count, 0, menuPicked, NULL);
}

// What the last GameList_update decided, for a plugin layout's view: whether
// names show, and the selected entry's thumbnail if it has one.
static bool update_show_names = true;
static char update_art[1024] = "";

// The game list as a plugin layout sees it, for a folder with entries.
static void listView(MosaicListView* v) {
	memset(v, 0, sizeof(*v));
	v->size = sizeof(*v);
	v->list = (const MosaicList*)state.top->entries;
	v->count = state.top->entries->count;
	v->selected = state.top->selected;
	v->start = state.top->start;
	v->end = state.top->end;
	v->rows = MAIN_ROW_COUNT;
	v->depth = state.stack->count;
	v->path = state.top->path;
	Entry* entry = state.top->entries->items[state.top->selected];
	v->letter = entry->alpha;
	v->letter_count = state.top->alphas->count;
	v->show_names = update_show_names;
	v->show_art = CFG_getShowGameArt() ? 1 : 0;
	v->art = update_art;
	LayoutView_area(screen->w, screen->h, screen->h - SCALE1(PADDING + PILL_SIZE), SCALE1(PADDING), ow, SCALE1(PILL_SIZE),
		&v->area, &v->status);
}

void GameList_registerActions(void) {
	for (int i = 0; i < SCREEN_BINDINGS; i++) Actions_register(screenBindings[i].action);
	for (int i = 0; i < BINDING_COUNT; i++) Actions_register(bindings[i].action);
	for (int i = 0; i < LETTER_BINDINGS; i++) Actions_register(letterBindings[i].action);
}

void GameList_handleInput(void) {
	// a plugin layout goes first, and can take buttons from what follows
	if (total>0 && Plugins_hasLayout()) {
		MosaicListView view;
		listView(&view);
		Plugins_layoutInput(&view);
		// it may have moved the selection or opened a folder
		selected = state.top->selected;
		total = state.top->entries->count;
	}

	Effective e;
	effective(&e);

	if (Bindings_dispatch(e.screen, e.screen_count)) {
		// the quick menu or the game switcher
	}
	else if (total>0) {
		if (PAD_justRepeated(BTN_UP)) {
			if (selected==0 && !PAD_justPressed(BTN_UP)) {
				// stop at top
			}
			else {
				selected -= 1;
				if (selected<0) {
					selected = total-1;
					int start = total - MAIN_ROW_COUNT;
					state.top->start = (start<0) ? 0 : start;
					state.top->end = total;
				}
				else if (selected<state.top->start) {
					state.top->start -= 1;
					state.top->end -= 1;
				}
			}
		}
		else if (PAD_justRepeated(BTN_DOWN)) {
			if (selected==total-1 && !PAD_justPressed(BTN_DOWN)) {
				// stop at bottom
			}
			else {
				selected += 1;
				if (selected>=total) {
					selected = 0;
					state.top->start = 0;
					state.top->end = (total<MAIN_ROW_COUNT) ? total : MAIN_ROW_COUNT;
				}
				else if (selected>=state.top->end) {
					state.top->start += 1;
					state.top->end += 1;
				}
			}
		}
		if (PAD_justRepeated(BTN_LEFT)) {
			selected -= MAIN_ROW_COUNT;
			if (selected<0) {
				selected = 0;
				state.top->start = 0;
				state.top->end = (total<MAIN_ROW_COUNT) ? total : MAIN_ROW_COUNT;
			}
			else if (selected<state.top->start) {
				state.top->start -= MAIN_ROW_COUNT;
				if (state.top->start<0) state.top->start = 0;
				state.top->end = state.top->start + MAIN_ROW_COUNT;
			}
		}
		else if (PAD_justRepeated(BTN_RIGHT)) {
			selected += MAIN_ROW_COUNT;
			if (selected>=total) {
				selected = total-1;
				int start = total - MAIN_ROW_COUNT;
				state.top->start = (start<0) ? 0 : start;
				state.top->end = total;
			}
			else if (selected>=state.top->end) {
				state.top->end += MAIN_ROW_COUNT;
				if (state.top->end>total) state.top->end = total;
				state.top->start = state.top->end - MAIN_ROW_COUNT;
			}
		}
	}

	// previous or next letter
	Bindings_dispatch(e.letters, e.letter_count);

	if (selected!=state.top->selected) {
		state.top->selected = selected;
		state.dirty = 1;
	}

	Entry* entry = state.top->entries->items[state.top->selected];

	if (state.dirty && total>0)
		readyResume(entry);

	// plugins' bindings rank after the game list's own and the overrides
	if (!Bindings_dispatch(e.actions, e.action_count))
		Bindings_dispatchShared(e.shared, e.shared_count, openEntryMenu);
}

// The built-in list's art for entry: its thumbnail, from plugins' art_path
// or stock's .media folder, or else the first of a box, 3D box or
// screenshot that a plugin supplies. Returns 1 with the path of the first
// that exists.
static int listArt(Entry* entry, char* out, size_t size) {
	for (int i = 0, kind; (kind = Art_listKind(i)) >= 0; i++) {
		if (!Plugins_artPath(entry, kind, out, size)) {
			if (kind != ART_THUMBNAIL) continue;
			resolveArt(ART_THUMBNAIL, entry->path, out, size);
		}
		if (exists(out)) return 1;
	}
	out[0] = '\0';
	return 0;
}

// Decides what the next render draws: whether names show, which background
// and thumbnail to load, and how much width the thumbnail takes. Draws
// nothing.
void GameList_update(void) {
	// background and game art file path stuff
	Entry* entry = state.top->entries->items[state.top->selected];
	assert(entry);
	char path_copy[1024];
	strncpy(path_copy, entry->path, sizeof(path_copy) - 1);
	path_copy[sizeof(path_copy) - 1] = '\0';

	char* rompath = dirname(path_copy);

	static int lastType = -1;

	// this is only a choice on the root folder
	list_show_entry_names = state.stack->count > 1 || CFG_getShowFolderNamesAtRoot();

	// load folder background
	char defaultBgPath[512];
	resolveArt(ART_DEFAULT_BG, NULL, defaultBgPath, sizeof(defaultBgPath));

	if(((entry->type == ENTRY_DIR || entry->type == ENTRY_ROM) && CFG_getRomsUseFolderBackground())) {
		char *newBg = entry->type == ENTRY_DIR ? entry->path:rompath;
		if((strcmp(newBg, folderBgPath) != 0 || lastType != entry->type) && sizeof(folderBgPath) != 1) {
			lastType = entry->type;
			char tmppath[512];
			strncpy(folderBgPath, newBg, sizeof(folderBgPath) - 1);
			int kind = entry->type == ENTRY_DIR ? ART_FOLDER_BG : ART_LIST_BG;
			if (!Plugins_artPath(entry, kind, tmppath, sizeof(tmppath)))
				resolveArt(kind, folderBgPath, tmppath, sizeof(tmppath));
			if(!exists(tmppath)) {
				// Safeguard: If no background is available, still render the text to leave the user a way out
				list_show_entry_names = true;
				resolveArt(ART_DEFAULT_BG, NULL, tmppath, sizeof(tmppath));
			}
			startLoadFolderBackground(tmppath, onBackgroundLoaded, NULL);
		}
	}
	else if(strcmp(defaultBgPath, folderBgPath) != 0 && exists(defaultBgPath)) {
		strncpy(folderBgPath, defaultBgPath, sizeof(folderBgPath) - 1);
		startLoadFolderBackground(defaultBgPath, onBackgroundLoaded, NULL);
	}
	else {
		// Safeguard: If no background is available, still render the text to leave the user a way out
		list_show_entry_names = true;
	}
	// load game thumbnails
	update_art[0] = '\0';
	if (total > 0) {
		if(CFG_getShowGameArt()) {
			char thumbpath[1024];
			int found = listArt(entry, thumbpath, sizeof(thumbpath));
			had_thumb = 0;
			if (Plugins_hasLayout()) {
				// a plugin layout draws its own art; none is left to come back with the built-in list
				ArtLoader_showThumb("");
			}
			else {
				ArtLoader_showThumb(found ? thumbpath : "");
				// the neighbours, ready for the next move
				int count = state.top->entries->count;
				for (int side = -1; side <= 1 && count > 1; side += 2) {
					int i = (state.top->selected + side + count) % count;
					if (side == 1 && i == (state.top->selected - 1 + count) % count) break;
					char path[1024];
					if (listArt(state.top->entries->items[i], path, sizeof(path))) ArtLoader_prefetchThumb(path);
				}
			}
			int max_w = (int)(screen->w - (screen->w * CFG_getGameArtWidth()));
			int max_h = (int)(screen->h * 0.6);
			int new_w = max_w;
			int new_h = max_h;
			if(found) {
				ox = (int)(max_w) - SCALE1(BUTTON_MARGIN*5);
				had_thumb = 1;
				snprintf(update_art, sizeof(update_art), "%s", thumbpath);
			}
			else
				ox = screen->w;
		}
	}
	update_show_names = list_show_entry_names;
}

void GameList_render(void) {
	Effective e;
	effective(&e);

	// buttons. The left group's second hint is the first plugin button with
	// an available action, or MORE when it opens a menu; stock has none.
	const char* extra_glyph = NULL;
	const char* extra_label = NULL;
	if (!Bindings_sharedHint(e.shared, e.shared_count, &extra_glyph, &extra_label)) {
		extra_glyph = NULL;
		// or an override that put a plugin's action on a button
		for (int i = 0; i < e.override_count && !extra_glyph; i++) {
			const Binding* b = &e.overrides[i];
			if (isOwnAction(b->action) || !Action_available(b->action)) continue;
			extra_glyph = b->glyph;
			extra_label = b->action->label;
		}
	}
	char* extra_label_tr = extra_glyph ? TR(extra_label) : NULL;

	// the game list's hints are on whichever buttons their actions are on
	const Binding* resume = Bindings_find(e.actions, e.action_count, &resumeAction);

	if (show_setting && !GetHDMI()) GFX_blitHardwareHints(screen, show_setting);
	// Stock shows the resume hint whenever resume state is set, even in an
	// empty folder where the action can't run, so this keeps its own check.
	else if (state.can_resume && resume) GFX_blitButtonGroup((char*[]){ (char*)resume->glyph,TR(resumeAction.label),
		(char*)extra_glyph, extra_label_tr, NULL }, 0, screen, 0);
	else GFX_blitButtonGroup((char*[]){
		BTN_SLEEP==BTN_POWER?"POWER":"MENU",
		TR(BTN_SLEEP==BTN_POWER||state.simple_mode?"SLEEP":"INFO"),
		(char*)extra_glyph, extra_label_tr,
		NULL }, 0, screen, 0);

	// back and open, whichever are available; the last one is primary
	const Binding* navigationHints[2];
	int navigation_count = 0;
	const Binding* back = Bindings_find(e.actions, e.action_count, &backAction);
	const Binding* opener = Bindings_find(e.actions, e.action_count, &openAction);
	if (back) navigationHints[navigation_count++] = back;
	if (opener) navigationHints[navigation_count++] = opener;
	char* hints[5];
	int hint_count = Bindings_hints(navigationHints, navigation_count, hints);
	if (hint_count > 0) GFX_blitButtonGroup(hints, hint_count - 1, screen, 1);

	// list
	if (total > 0) {
		selected_row = state.top->selected - state.top->start;
		previousY = state.previous_row * PILL_SIZE;
		targetY = selected_row * PILL_SIZE;

		// a plugin layout, or the built-in list if there's none or it fails
		bool was_layout = list_layout_drawn;
		list_layout_drawn = false;
		if (Plugins_hasLayout()) {
			MosaicListView view;
			listView(&view);
			list_layout_drawn = Plugins_layoutRender(screen, &view);
		}
		if (list_layout_drawn) is_scrolling = 0;
		else if (was_layout) {
			// the stock thumbnail comes back with the built-in list
			SDL_LockMutex(thumbMutex);
			thumbchanged = 1;
			SDL_UnlockMutex(thumbMutex);
		}

		SDL_Color text_color = uintToColour(THEME_COLOR4_255); // list text color
		for (int i = state.top->start, j = 0; !list_layout_drawn && i < state.top->end; i++, j++) {
			Entry* entry = state.top->entries->items[i];
			char* entry_name = entry->name;
			char* entry_unique = entry->unique;
			int available_width = MAX(0,(had_thumb ? ox + SCALE1(BUTTON_MARGIN) : screen->w - SCALE1(BUTTON_MARGIN)) - SCALE1(PADDING * 2));
			bool row_is_selected = (j == selected_row);
			bool row_is_top = (i == state.top->start);
			bool row_has_moved = (state.previous_row != selected_row || state.previous_depth != state.stack->count);
			if (row_is_top && !(had_thumb))
				available_width -= ow;

			trimSortingMeta(&entry_name);
			if (entry_unique) // Only render if a unique name exists
				trimSortingMeta(&entry_unique);

			char display_name[256];
			int text_width = GFX_getTextWidth(font.large, entry_unique ? entry_unique : entry_name, display_name, available_width, SCALE1(BUTTON_PADDING * 2));
			int max_width = MIN(available_width, text_width);

			// This spaghetti is preventing white text on white pill when volume/color temp is shown,
			// dont ask me why. This all needs to get tossed out and redone properly later.
			SDL_Color text_color = uintToColour(THEME_COLOR4_255);
			int notext = 0;
			if(!row_has_moved && row_is_selected) {
				text_color = uintToColour(THEME_COLOR5_255);
				notext = 1;
			}

			SDL_LockMutex(fontMutex);
			SDL_Surface* text = TTF_RenderUTF8_Blended(font.large, entry_name, text_color);
			SDL_Surface* text_unique = TTF_RenderUTF8_Blended(font.large, display_name, COLOR_DARK_TEXT);
			SDL_UnlockMutex(fontMutex);
			// TODO: Use actual font metrics to center, this only works in simple cases
			const int text_offset_y = (SCALE1(PILL_SIZE) - text->h + 1) >> 1;
			if (row_is_selected) {
				is_scrolling = list_show_entry_names && GFX_textShouldScroll(font.large,display_name, max_width - SCALE1(BUTTON_PADDING*2), fontMutex);
				GFX_resetScrollText();
				bool should_animate = state.previous_depth == state.stack->count;
				SDL_LockMutex(animMutex);
				if(globalpill) {
					SDL_FreeSurface(globalpill);
					globalpill=NULL;
				}
				globalpill = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, max_width, SCALE1(PILL_SIZE), FIXED_DEPTH, screen->format->format);
				GFX_blitPillDark(ASSET_WHITE_PILL, globalpill, &(SDL_Rect){0,0, max_width, SCALE1(PILL_SIZE)});
				globallpillW =  max_width;
				SDL_UnlockMutex(animMutex);
				updatePillTextSurface(entry_name, max_width, uintToColour(THEME_COLOR5_255));
				AnimTask* task = malloc(sizeof(AnimTask));
				task->startX = SCALE1(BUTTON_MARGIN);
				task->startY = SCALE1(previousY+PADDING);
				task->targetX = SCALE1(BUTTON_MARGIN);
				task->targetY = SCALE1(targetY+PADDING);
				task->targetTextY = SCALE1(PADDING + targetY) + text_offset_y;
				pilltargetTextY = task->targetTextY;
				task->move_w = max_width;
				task->move_h = SCALE1(PILL_SIZE);
				task->frames = should_animate && CFG_getMenuAnimations() ? 3:1;
				task->entry_name = strdup(notext ? " " : entry_name);
				animPill(task);
			}
			SDL_Rect text_rect = { 0, 0, max_width - SCALE1(BUTTON_PADDING*2), text->h };
			SDL_Rect dest_rect = { SCALE1(BUTTON_MARGIN + BUTTON_PADDING), SCALE1(PADDING + (j * PILL_SIZE)) + text_offset_y };

			if(list_show_entry_names) {
				SDL_BlitSurface(text_unique, &text_rect, screen, &dest_rect);
				SDL_BlitSurface(text, &text_rect, screen, &dest_rect);
			}
			SDL_FreeSurface(text_unique); // Free after use
			SDL_FreeSurface(text); // Free after use
		}
		if(lastScreen==SCREEN_GAMESWITCHER) {
			if(switcherSur) {
				// update cpu surface here first
				GFX_clearLayers(LAYER_ALL);
				folderbgchanged=1;

				GFX_flipHidden();
				GFX_animateSurface(switcherSur,0,0,0,0-screen->h,screen->w,screen->h,CFG_getMenuTransitions() ? 100:20,255,255,LAYER_BACKGROUND);
				state.animationdirection = ANIM_NONE;
			}
		}
		if(lastScreen==SCREEN_OFF) {
			GFX_animateSurfaceOpacity(blackBG,0,0,screen->w,screen->h,255,0,CFG_getMenuTransitions() ? 200:20,LAYER_THUMBNAIL);
		}

		state.previous_row = selected_row;
		state.previous_depth = state.stack->count;
	}
	else {
		list_layout_drawn = false;
		// TODO: for some reason screen's dimensions end up being 0x0 in GFX_blitMessage...
		GFX_blitMessage(font.large, TR("Empty folder"), screen, &(SDL_Rect){0,0,screen->w,screen->h}); //, NULL);
	}

	lastScreen = SCREEN_GAMELIST;
}

void LaunchFade_render(void) {
	//pilltargetY = +screen->w;
	//animationdirection = ANIM_NONE;
	GFX_clearLayers(LAYER_ALL);
	GFX_clear(screen);
	GFX_flipHidden();
	GFX_animateSurfaceOpacity(tmpOldScreen,0,0,screen->w,screen->h,255,0,CFG_getMenuTransitions() ? 150:20,LAYER_BACKGROUND);
}
