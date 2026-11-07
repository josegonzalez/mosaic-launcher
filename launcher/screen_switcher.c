#include "launcher.h"

void Switcher_handleInput(void) {
	if (PAD_justPressed(BTN_B) || PAD_tappedSelect(now)) {
		currentScreen = SCREEN_GAMELIST;
		switcher_selected = 0;
		dirty = 1;
		folderbgchanged = 1; // The background painting code is a clusterfuck, just force a repaint here
	}
	else if (recents->count > 0 && PAD_justReleased(BTN_A)) {
		// this will drop us back into game switcher after leaving the game
		putFile(GAME_SWITCHER_PERSIST_PATH, "unused");
		startgame = 1;
		Entry *selectedEntry = entryFromRecent(recents->items[switcher_selected]);
		should_resume = can_resume;
		Entry_open(selectedEntry);
		dirty = 1;
		Entry_free(selectedEntry);
	}
	else if (recents->count > 0 && PAD_justReleased(BTN_Y)) {
		// remove
		Recent* recentEntry = recents->items[switcher_selected--];
		Array_remove(recents, recentEntry);
		Recent_free(recentEntry);
		saveRecents();
		if(switcher_selected < 0)
			switcher_selected = recents->count - 1; // wrap
		dirty = 1;
	}
	else if (PAD_justPressed(BTN_RIGHT)) {
		switcher_selected++;
		if(switcher_selected == recents->count)
			switcher_selected = 0; // wrap
		dirty = 1;
		gsanimdir = SLIDE_LEFT;
	}
	else if (PAD_justPressed(BTN_LEFT)) {
		switcher_selected--;
		if(switcher_selected < 0)
			switcher_selected = recents->count - 1; // wrap
		dirty = 1;
		gsanimdir = SLIDE_RIGHT;
	}
}

// Loads the selected recent's resume state for the next render. Draws
// nothing.
void Switcher_update(void) {
	ox = 0;
	oy = 0;

	if(recents->count > 0) {
		Entry *selectedEntry = entryFromRecent(recents->items[switcher_selected]);
		readyResume(selectedEntry);
		Entry_free(selectedEntry);
	}
}

void Switcher_render(void) {
	GFX_clearLayers(LAYER_ALL);

	// For all recents with resumable state (i.e. has savegame), show game switcher carousel
	if(recents->count > 0) {
		Entry *selectedEntry = entryFromRecent(recents->items[switcher_selected]);
		// title pill
		{
			int max_width = screen->w - SCALE1(PADDING * 2) - ow;

			char display_name[256];
			int text_width = GFX_truncateText(font.large, selectedEntry->name, display_name, max_width, SCALE1(BUTTON_PADDING*2));
			max_width = MIN(max_width, text_width);

			SDL_Surface* text;
			SDL_Color textColor = uintToColour(THEME_COLOR6_255);
			SDL_LockMutex(fontMutex);
			text = TTF_RenderUTF8_Blended(font.large, display_name, textColor);
			SDL_UnlockMutex(fontMutex);
			const int text_offset_y = (SCALE1(PILL_SIZE) - text->h + 1) >> 1;
			GFX_blitPillLight(ASSET_WHITE_PILL, screen, &(SDL_Rect){
				SCALE1(PADDING),
				SCALE1(PADDING),
				max_width,
				SCALE1(PILL_SIZE)
			});
			SDL_BlitSurface(text, &(SDL_Rect){
				0,
				0,
				max_width-SCALE1(BUTTON_PADDING*2),
				text->h
			}, screen, &(SDL_Rect){
				SCALE1(PADDING+BUTTON_PADDING),
				SCALE1(PADDING) + text_offset_y,
			});
			SDL_FreeSurface(text);
		}

		GFX_blitButtonGroup((char*[]){ "B",TR("BACK"),  NULL }, 0, screen, 0);

		if(can_resume) GFX_blitButtonGroup((char*[]){ "Y", TR("REMOVE"), "A",TR("RESUME"), NULL }, 1, screen, 1);
		else GFX_blitButtonGroup((char*[]){ "A",TR("OPEN"), NULL }, 1, screen, 1);

		if(has_preview) {
			// lotta memory churn here

			SDL_Surface* bmp = IMG_Load(preview_path);
			SDL_Surface* raw_preview = SDL_ConvertSurfaceFormat(bmp, screen->format->format, 0);
			if (raw_preview) {
				SDL_FreeSurface(bmp);
				bmp = raw_preview;
			}
			if(bmp) {
				int aw = screen->w;
				int ah = screen->h;
				int ax = 0;
				int ay = 0;

				float aspectRatio = (float)bmp->w / (float)bmp->h;
				float screenRatio = (float)screen->w / (float)screen->h;

				if (screenRatio > aspectRatio) {
					aw = (int)(screen->h * aspectRatio);
					ah = screen->h;
				} else {
					aw = screen->w;
					ah = (int)(screen->w / aspectRatio);
				}
				ax = (screen->w - aw) / 2;
				ay = (screen->h - ah) / 2;

				Transition_showPreview(bmp, (SDL_Rect){ax, ay, aw, ah}, lastScreen, gsanimdir);
				SDL_FreeSurface(bmp);  // Free after rendering
			}
		}
		else {
			SDL_Rect preview_rect = {ox,oy,screen->w,screen->h};
			SDL_Surface * tmpsur = SDL_CreateRGBSurfaceWithFormat(0,screen->w,screen->h,screen->format->BitsPerPixel,screen->format->format);
			SDL_FillRect(tmpsur, &preview_rect, THEME_COLOR7);
			Transition_showPlaceholder(tmpsur, lastScreen, gsanimdir);
			SDL_FreeSurface(tmpsur);
			GFX_blitMessage(font.large, TR("No Preview"), screen, &preview_rect);
		}
		Entry_free(selectedEntry);
	}
	else {
		SDL_Rect preview_rect = {ox,oy,screen->w,screen->h};
		SDL_FillRect(screen, &preview_rect, 0);
		GFX_blitMessage(font.large, TR("No Recents"), screen, &preview_rect);
		GFX_blitButtonGroup((char*[]){ "B",TR("BACK"), NULL }, 1, screen, 1);
	}

	GFX_flipHidden();

	if(switcherSur) SDL_FreeSurface(switcherSur);
	switcherSur = GFX_captureRendererToSurface();
	lastScreen = SCREEN_GAMESWITCHER;
}
