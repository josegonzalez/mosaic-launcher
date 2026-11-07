#include "launcher.h"

void QuickMenu_handleInput(void) {
	int qm_total = qm_row == 0 ? quick->count : quickActions->count;

	if (PAD_justPressed(BTN_B) || PAD_tappedMenu(now)) {
		currentScreen = SCREEN_GAMELIST;
		folderbgchanged = 1; // The background painting code is a clusterfuck, just force a repaint here
		dirty = 1;
	}
	else if (PAD_justReleased(BTN_A)) {
		Entry *selected = qm_row == 0 ? quick->items[qm_col] : quickActions->items[qm_col];
		if(selected->type != ENTRY_DIP) {
			currentScreen = SCREEN_GAMELIST;
			total = top->entries->count;
			// prevent restoring list state, game list screen currently isnt our nav origin
			top->selected = 0;
			top->start = 0;
			top->end = top->start + MAIN_ROW_COUNT;
			restore_depth = -1;
			restore_relative = -1;
			restore_selected = 0;
			restore_start = 0;
			restore_end = 0;
		}
		Entry_open(selected);
		if (top->entries->count > 0) readyResume(top->entries->items[top->selected]);
		dirty = 1;
	}
	else if (PAD_justPressed(BTN_RIGHT)) {
		if(qm_row == 0 && qm_total > qm_slots) {
			qm_col++;
			if(qm_col >= qm_total) {
				qm_col = 0;
				qm_shift = 0;
				qm_slot = 0;
			}
			else {
				qm_slot++;
				if(qm_slot >= qm_slots) {
					qm_slot = qm_slots - 1;
					qm_shift++;
				}
			}
		}
		else {
			qm_col += 1;
			if(qm_col >= qm_total) {
				qm_col = 0;
			}
		}
		dirty = 1;
	}
	else if (PAD_justPressed(BTN_LEFT)) {
		if(qm_row == 0  && qm_total > qm_slots) {
			qm_col -= 1;
			if(qm_col < 0) {
				qm_col = qm_total - 1;
				qm_shift = qm_total - qm_slots;
				qm_slot = qm_slots - 1;
			}
			else {
				qm_slot--;
				if(qm_slot < 0) {
					qm_slot = 0;
					qm_shift--;
				}
			}
		}
		else {
			qm_col -= 1;
			if(qm_col < 0) {
				qm_col = qm_total - 1;
			}
		}
		dirty = 1;
	}
	else if(PAD_justPressed(BTN_DOWN)) {
		if(qm_row == 0) {
			qm_row = 1;
			qm_col = 0;
			dirty = 1;
		}
	}
	else if(PAD_justPressed(BTN_UP)) {
		if(qm_row == 1) {
			qm_row = 0;
			qm_col = qm_slot + qm_shift;
			dirty = 1;
		}
	}
}

void QuickMenu_render(void) {
	if(lastScreen != SCREEN_QUICKMENU) {
		GFX_clearLayers(LAYER_BACKGROUND);
		GFX_clearLayers(LAYER_THUMBNAIL);
	}

	Entry *current = qm_row == 0 ? quick->items[qm_col] : quickActions->items[qm_col];
	char newBgPath[MAX_PATH];
	char fallbackBgPath[MAX_PATH];

	char bgSubject[MAX_PATH];
	// wifi or wifi_off, bluetooth or bluetooth_off, based on state
	snprintf(bgSubject, sizeof(bgSubject), "%s%s", current->name, QuickItem_isOff(current->name) ? "_off" : "");
	resolveArt(ART_QUICK_BG, bgSubject, newBgPath, sizeof(newBgPath));
	resolveArt(ART_QUICK_FALLBACK, NULL, fallbackBgPath, sizeof(fallbackBgPath));

	// background
	if(!exists(newBgPath))
		strncpy(newBgPath, fallbackBgPath, sizeof(newBgPath) - 1);

	if(strcmp(newBgPath, folderBgPath) != 0) {
		strncpy(folderBgPath, newBgPath, sizeof(folderBgPath) - 1);
		startLoadFolderBackground(newBgPath, onBackgroundLoaded, NULL);
	}

	// buttons (duped and trimmed from below)
	if (show_setting && !GetHDMI()) GFX_blitHardwareHints(screen, show_setting);
	else GFX_blitButtonGroup((char*[]){ BTN_SLEEP==BTN_POWER?"POWER":"MENU",TR("SLEEP"),  NULL }, 0, screen, 0);

	GFX_blitButtonGroup((char*[]){ "B",TR("BACK"), "A",TR("OPEN"), NULL }, 1, screen, 1);

	if(CFG_getShowQuickswitcherUI()) {
		#define MENU_ITEM_SIZE 72 // item size, top line
		#define MENU_MARGIN_Y 32 // space between main UI elements and quick menu
		#define MENU_MARGIN_X 40 // space between main UI elements and quick menu
		#define MENU_ITEM_MARGIN 18 // space between items, top line
		#define MENU_TOGGLE_MARGIN 8 // space between items, bottom line
		#define MENU_LINE_MARGIN 8 // space between top and bottom line

		// this is flexible, not sure I like it at smaller scales than 3 though
		//int item_size = screen->h - SCALE1(PADDING + PILL_SIZE + BUTTON_MARGIN + // top pill area
		//	MENU_MARGIN_Y + MENU_LINE_MARGIN + PILL_SIZE + MENU_MARGIN_Y + // our own area
		//	BUTTON_MARGIN + PILL_SIZE + PADDING); // bottom pill area

		int item_space_y = screen->h - SCALE1(PADDING + PILL_SIZE + BUTTON_MARGIN + // top pill area
			MENU_MARGIN_Y + MENU_LINE_MARGIN + PILL_SIZE + MENU_MARGIN_Y + // our own area
			BUTTON_MARGIN + PILL_SIZE + PADDING);
		int item_size = SCALE1(MENU_ITEM_SIZE);
		int item_extra_y = item_space_y - item_size;
		int item_space_x = screen->w - SCALE1(PADDING + MENU_MARGIN_X + MENU_MARGIN_X + PADDING);
		// extra left margin for the first item in order to properly center all of them in the
		// available space
		int item_inset_x = (item_space_x - SCALE1(qm_slots * MENU_ITEM_SIZE + (qm_slots - 1) * MENU_ITEM_MARGIN)) / 2;

		// primary
		ox = SCALE1(PADDING + MENU_MARGIN_X) + item_inset_x;
		oy = SCALE1(PADDING + PILL_SIZE + BUTTON_MARGIN + MENU_MARGIN_Y) + item_extra_y / 2;
		// just to keep selection visible.
		// every display should be able to fit three items, we shift horizontally to accomodate.
		ox -= qm_shift * (item_size + SCALE1(MENU_ITEM_MARGIN));
		for (int c = 0; c < quick->count; c++)
		{
			SDL_Rect item_rect = {ox, oy, item_size, item_size};
			Entry *item = quick->items[c];

			SDL_Color text_color = uintToColour(THEME_COLOR4_255);
			uint32_t item_color = THEME_COLOR3;
			uint32_t icon_color = THEME_COLOR4;

			if(qm_row == 0 && qm_col == c) {
				text_color = uintToColour(THEME_COLOR5_255);
				item_color = THEME_COLOR1;
				icon_color = THEME_COLOR5;
			}

			GFX_blitRectColor(ASSET_STATE_BG, screen, &item_rect, item_color);

			char icon_path[MAX_PATH];
			resolveArt(ART_QUICK_ICON, item->name, icon_path, sizeof(icon_path));
			SDL_Surface* bmp = IMG_Load(icon_path);
			if(bmp) {
				SDL_Surface* converted = SDL_ConvertSurfaceFormat(bmp, screen->format->format, 0);
				if (converted) {
					SDL_FreeSurface(bmp);
					bmp = converted;
				}
			}
			if(bmp) {
				// Calculate the position to center the source surface
				int x = (item_rect.w - bmp->w) / 2;
				int y = (item_rect.h - SCALE1(FONT_TINY + BUTTON_MARGIN) - bmp->h) / 2;
				SDL_Rect destRect = { ox+x, oy+y, 0, 0 };  // width/height not required
				//SDL_BlitSurface(bmp, NULL, screen, &destRect);

				GFX_blitSurfaceColor(bmp, NULL, screen, &destRect, icon_color);
			}

			int w, h;
			GFX_sizeText(font.tiny, TR(item->name), SCALE1(FONT_TINY), &w, &h);
			SDL_Rect text_rect = {item_rect.x + (item_size - w) / 2, item_rect.y + item_size - h - SCALE1(BUTTON_MARGIN), w, h};
			GFX_blitText(font.tiny, TR(item->name), SCALE1(FONT_TINY), text_color, screen, &text_rect);

			ox += item_rect.w + SCALE1(MENU_ITEM_MARGIN);
		}

		// secondary
		ox = SCALE1(PADDING + MENU_MARGIN_X);
		ox += (screen->w - SCALE1(PADDING + MENU_MARGIN_X + MENU_MARGIN_X + PADDING) - SCALE1(quickActions->count * PILL_SIZE) - SCALE1((quickActions->count - 1) * MENU_TOGGLE_MARGIN))/2;
		oy = SCALE1(PADDING + PILL_SIZE + BUTTON_MARGIN + MENU_MARGIN_Y + MENU_LINE_MARGIN) + item_size + item_extra_y / 2;
		for (int c = 0; c < quickActions->count; c++) {
			SDL_Rect item_rect = {ox, oy, SCALE1(PILL_SIZE), SCALE1(PILL_SIZE)};
			Entry *item = quickActions->items[c];

			SDL_Color text_color = uintToColour(THEME_COLOR4_255);
			uint32_t item_color = THEME_COLOR3;
			uint32_t icon_color = THEME_COLOR4;

			if(qm_row == 1 && qm_col == c) {
				text_color = uintToColour(THEME_COLOR5_255);
				item_color = THEME_COLOR1;
				icon_color = THEME_COLOR5;
			}

			GFX_blitPillColor(ASSET_WHITE_PILL, screen, &item_rect, item_color, RGB_WHITE);

			int asset = QuickItem_asset(item->name);

			SDL_Rect rect;
			GFX_assetRect(asset, &rect);
			int x = item_rect.x;
			int y = item_rect.y;
			x += (SCALE1(PILL_SIZE) - rect.w) / 2;
			y += (SCALE1(PILL_SIZE) - rect.h) / 2;

			GFX_blitAssetColor(asset, NULL, screen, &(SDL_Rect){x,y}, icon_color);

			ox += item_rect.w + SCALE1(MENU_TOGGLE_MARGIN);
		}
	}
	lastScreen = SCREEN_QUICKMENU;
}
