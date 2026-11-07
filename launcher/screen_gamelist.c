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

static const Action resumeAction = { "gamelist.resume", "RESUME", canResume, resumeSelected };
static const Action openAction = { "gamelist.open", "OPEN", canOpen, openSelected };
static const Action backAction = { "gamelist.back", "BACK", canGoBack, goBack };

// In priority order: the first that fires and is available wins.
static const Binding bindings[] = {
	{ BTN_RESUME, TRIGGER_RELEASE, "X", &resumeAction },
	{ BTN_A, TRIGGER_PRESS, "A", &openAction },
	{ BTN_B, TRIGGER_PRESS, "B", &backAction },
};
#define BINDING_COUNT (int)(sizeof(bindings) / sizeof(bindings[0]))

// The right-hand hint group, in display order.
static const Binding* const navigationHints[] = { &bindings[2], &bindings[1] };

void GameList_registerActions(void) {
	for (int i = 0; i < BINDING_COUNT; i++) Actions_register(bindings[i].action);
}

void GameList_handleInput(void) {
	if (PAD_tappedMenu(now)) {
		currentScreen = SCREEN_QUICKMENU;
		QuickMenu_enter();
		state.dirty = 1;
		folderbgchanged = 1; // The background painting code is a clusterfuck, just force a repaint here
		if (!HAS_POWER_BUTTON && !state.simple_mode) PWR_enableSleep();
	}
	else if (PAD_tappedSelect(now)) {
		currentScreen = SCREEN_GAMESWITCHER;
		state.switcher_selected = 0;
		state.dirty = 1;
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

	if (PAD_justRepeated(BTN_L1) && !PAD_isPressed(BTN_R1) && !PWR_ignoreSettingInput(BTN_L1, show_setting)) { // previous alpha
		Entry* entry = state.top->entries->items[selected];
		int i = entry->alpha-1;
		if (i>=0) {
			selected = state.top->alphas->items[i];
			if (total>MAIN_ROW_COUNT) {
				state.top->start = selected;
				state.top->end = state.top->start + MAIN_ROW_COUNT;
				if (state.top->end>total) state.top->end = total;
				state.top->start = state.top->end - MAIN_ROW_COUNT;
			}
		}
	}
	else if (PAD_justRepeated(BTN_R1) && !PAD_isPressed(BTN_L1) && !PWR_ignoreSettingInput(BTN_R1, show_setting)) { // next alpha
		Entry* entry = state.top->entries->items[selected];
		int i = entry->alpha+1;
		if (i<state.top->alphas->count) {
			selected = state.top->alphas->items[i];
			if (total>MAIN_ROW_COUNT) {
				state.top->start = selected;
				state.top->end = state.top->start + MAIN_ROW_COUNT;
				if (state.top->end>total) state.top->end = total;
				state.top->start = state.top->end - MAIN_ROW_COUNT;
			}
		}
	}

	if (selected!=state.top->selected) {
		state.top->selected = selected;
		state.dirty = 1;
	}

	Entry* entry = state.top->entries->items[state.top->selected];

	if (state.dirty && total>0)
		readyResume(entry);

	Bindings_dispatch(bindings, BINDING_COUNT);
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
			if (entry->type == ENTRY_DIR)
				resolveArt(ART_FOLDER_BG, folderBgPath, tmppath, sizeof(tmppath));
			else if (entry->type == ENTRY_ROM)
				resolveArt(ART_LIST_BG, folderBgPath, tmppath, sizeof(tmppath));
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
	if (total > 0) {
		if(CFG_getShowGameArt()) {
			char thumbpath[1024];
			resolveArt(ART_THUMBNAIL, entry->path, thumbpath, sizeof(thumbpath));
			had_thumb = 0;
			startLoadThumb(thumbpath, onThumbLoaded, NULL);
			int max_w = (int)(screen->w - (screen->w * CFG_getGameArtWidth()));
			int max_h = (int)(screen->h * 0.6);
			int new_w = max_w;
			int new_h = max_h;
			if(exists(thumbpath)) {
				ox = (int)(max_w) - SCALE1(BUTTON_MARGIN*5);
				had_thumb = 1;
			}
			else
				ox = screen->w;
		}
	}
}

void GameList_render(void) {
	// buttons
	if (show_setting && !GetHDMI()) GFX_blitHardwareHints(screen, show_setting);
	// Stock shows the resume hint whenever resume state is set, even in an
	// empty folder where the action can't run, so this keeps its own check.
	else if (state.can_resume) GFX_blitButtonGroup((char*[]){ (char*)bindings[0].glyph,TR(resumeAction.label),  NULL }, 0, screen, 0);
	else GFX_blitButtonGroup((char*[]){
		BTN_SLEEP==BTN_POWER?"POWER":"MENU",
		TR(BTN_SLEEP==BTN_POWER||state.simple_mode?"SLEEP":"INFO"),
		NULL }, 0, screen, 0);

	// back and open, whichever are available; the last one is primary
	char* hints[5];
	int hint_count = Bindings_hints(navigationHints, 2, hints);
	if (hint_count > 0) GFX_blitButtonGroup(hints, hint_count - 1, screen, 1);

	// list
	if (total > 0) {
		selected_row = state.top->selected - state.top->start;
		previousY = state.previous_row * PILL_SIZE;
		targetY = selected_row * PILL_SIZE;
		SDL_Color text_color = uintToColour(THEME_COLOR4_255); // list text color
		for (int i = state.top->start, j = 0; i < state.top->end; i++, j++) {
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
