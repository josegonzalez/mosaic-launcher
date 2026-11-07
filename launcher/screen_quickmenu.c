#include "launcher.h"

// cursor: row 0 is the top row of folders, row 1 the shortcuts and toggles
static int qm_row;
static int qm_col;
// how the top row scrolls when it has more entries than fit
static int qm_slot;
static int qm_shift;
static int qm_slots;

void QuickMenu_setup(void) {
	qm_row = 0;
	qm_col = 0;
	qm_slot = 0;
	qm_shift = 0;
	qm_slots = QUICK_SWITCHER_COUNT > state.quick->count ? state.quick->count : QUICK_SWITCHER_COUNT;
}

void QuickMenu_enter(void) {
	qm_col = 0;
	qm_row = 0;
	qm_shift = 0;
	qm_slot = 0;
	// plugins can hide every top tile
	if (state.quick->count == 0 && state.quickActions->count > 0) qm_row = 1;
}

void QuickMenu_refit(void) {
	qm_slots = QUICK_SWITCHER_COUNT > state.quick->count ? state.quick->count : QUICK_SWITCHER_COUNT;
	if (qm_row == 0 && state.quick->count == 0 && state.quickActions->count > 0) qm_row = 1;
	if (qm_row == 1 && state.quickActions->count == 0) qm_row = 0;
	int count = qm_row == 0 ? state.quick->count : state.quickActions->count;
	if (qm_col >= count) qm_col = count > 0 ? count - 1 : 0;
	if (qm_row == 0) {
		// keep the scrolled top row's window around the cursor
		if (qm_shift > count - qm_slots) qm_shift = MAX(0, count - qm_slots);
		if (qm_col < qm_shift) qm_shift = qm_col;
		if (qm_col >= qm_shift + qm_slots) qm_shift = qm_col - qm_slots + 1;
		qm_slot = qm_col - qm_shift;
	}
}

// The selected tile, or NULL when its row is empty.
static Entry* selectedTile(void) {
	Array* row = qm_row == 0 ? state.quick : state.quickActions;
	return qm_col < row->count ? row->items[qm_col] : NULL;
}

void QuickMenu_handleInput(void) {
	int qm_total = qm_row == 0 ? state.quick->count : state.quickActions->count;

	if (PAD_justPressed(BTN_B) || PAD_tappedMenu(now)) {
		currentScreen = SCREEN_GAMELIST;
		folderbgchanged = 1; // The background painting code is a clusterfuck, just force a repaint here
		state.dirty = 1;
	}
	else if (PAD_justReleased(BTN_A) && selectedTile()) {
		Entry *selected = selectedTile();
		Directory* was_top = state.top;
		int was_depth = state.stack->count;
		if(selected->type != ENTRY_DIP) {
			currentScreen = SCREEN_GAMELIST;
			total = state.top->entries->count;
			// prevent restoring list state, game list screen currently isnt our nav origin
			state.top->selected = 0;
			state.top->start = 0;
			state.top->end = state.top->start + MAIN_ROW_COUNT;
			state.restore_depth = -1;
			state.restore_relative = -1;
			state.restore_selected = 0;
			state.restore_start = 0;
			state.restore_end = 0;
		}
		Entry_open(selected);
		// a plugin tile that opened a folder shows it; stock's toggles never do
		if (selected->type == ENTRY_DIP && currentScreen == SCREEN_QUICKMENU
				&& (state.top != was_top || state.stack->count != was_depth)) {
			currentScreen = SCREEN_GAMELIST;
			folderbgchanged = 1;
		}
		if (state.top->entries->count > 0) readyResume(state.top->entries->items[state.top->selected]);
		state.dirty = 1;
	}
	else if (PAD_justPressed(BTN_RIGHT) && qm_total > 0) {
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
		state.dirty = 1;
	}
	else if (PAD_justPressed(BTN_LEFT) && qm_total > 0) {
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
		state.dirty = 1;
	}
	else if(PAD_justPressed(BTN_DOWN)) {
		if(qm_row == 0 && state.quickActions->count > 0) {
			qm_row = 1;
			qm_col = 0;
			state.dirty = 1;
		}
	}
	else if(PAD_justPressed(BTN_UP)) {
		if(qm_row == 1 && state.quick->count > 0) {
			qm_row = 0;
			qm_col = qm_slot + qm_shift;
			state.dirty = 1;
		}
	}
}

// Picks and starts loading the selected tile's background. Draws nothing;
// the loaded image is drawn by main()'s background flush.
void QuickMenu_update(void) {
	Entry *current = selectedTile();
	if (!current) return;
	char newBgPath[MAX_PATH];
	char fallbackBgPath[MAX_PATH];

	char bgSubject[MAX_PATH];
	// wifi or wifi_off, bluetooth or bluetooth_off, based on state
	snprintf(bgSubject, sizeof(bgSubject), "%s%s", current->name, QuickItem_isOff(current) ? "_off" : "");
	resolveArt(ART_QUICK_BG, bgSubject, newBgPath, sizeof(newBgPath));
	resolveArt(ART_QUICK_FALLBACK, NULL, fallbackBgPath, sizeof(fallbackBgPath));

	// background
	if(!exists(newBgPath))
		strncpy(newBgPath, fallbackBgPath, sizeof(newBgPath) - 1);

	if(strcmp(newBgPath, folderBgPath) != 0) {
		strncpy(folderBgPath, newBgPath, sizeof(folderBgPath) - 1);
		startLoadFolderBackground(newBgPath, onBackgroundLoaded, NULL);
	}
}

// A plugin tile's PNG for its on/off state, or "" for none.
static const char* tileIcon(const QuickTile* tile) {
	const char* icon = QuickTile_isOff(tile) && tile->icon_off ? tile->icon_off : tile->icon;
	return icon ? icon : "";
}

// A plugin tile on the bottom row: its PNG centered on the pill, or its
// label when it has none.
static void blitTileIcon(const QuickTile* tile, Entry* item, SDL_Rect rect, uint32_t icon_color, SDL_Color text_color) {
	const char* path = tileIcon(tile);
	SDL_Surface* bmp = path[0] ? IMG_Load(path) : NULL;
	if (bmp) {
		SDL_Surface* converted = SDL_ConvertSurfaceFormat(bmp, screen->format->format, 0);
		if (converted) {
			SDL_FreeSurface(bmp);
			bmp = converted;
		}
		SDL_Rect dst = { rect.x + (rect.w - bmp->w) / 2, rect.y + (rect.h - bmp->h) / 2, 0, 0 };
		GFX_blitSurfaceColor(bmp, NULL, screen, &dst, icon_color);
		SDL_FreeSurface(bmp);
		return;
	}
	char label[64];
	int w = GFX_truncateText(font.tiny, TR(item->name), label, rect.w, 0);
	int h = TTF_FontHeight(font.tiny);
	SDL_Rect text_rect = { rect.x + (rect.w - w) / 2, rect.y + (rect.h - h) / 2, w, h };
	GFX_blitText(font.tiny, label, SCALE1(FONT_TINY), text_color, screen, &text_rect);
}

void QuickMenu_render(void) {
	if(lastScreen != SCREEN_QUICKMENU) {
		GFX_clearLayers(LAYER_BACKGROUND);
		GFX_clearLayers(LAYER_THUMBNAIL);
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
		for (int c = 0; c < state.quick->count; c++)
		{
			SDL_Rect item_rect = {ox, oy, item_size, item_size};
			Entry *item = state.quick->items[c];

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
			const QuickTile* tile = QuickTiles_forEntry(item);
			if (tile) snprintf(icon_path, sizeof(icon_path), "%s", tileIcon(tile));
			else resolveArt(ART_QUICK_ICON, item->name, icon_path, sizeof(icon_path));
			SDL_Surface* bmp = icon_path[0] ? IMG_Load(icon_path) : NULL;
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
			const char* label = QuickTiles_label(item);
			char* text = label == item->name ? TR(item->name) : (char*)label;
			GFX_sizeText(font.tiny, text, SCALE1(FONT_TINY), &w, &h);
			SDL_Rect text_rect = {item_rect.x + (item_size - w) / 2, item_rect.y + item_size - h - SCALE1(BUTTON_MARGIN), w, h};
			GFX_blitText(font.tiny, text, SCALE1(FONT_TINY), text_color, screen, &text_rect);

			ox += item_rect.w + SCALE1(MENU_ITEM_MARGIN);
		}

		// secondary
		ox = SCALE1(PADDING + MENU_MARGIN_X);
		ox += (screen->w - SCALE1(PADDING + MENU_MARGIN_X + MENU_MARGIN_X + PADDING) - SCALE1(state.quickActions->count * PILL_SIZE) - SCALE1((state.quickActions->count - 1) * MENU_TOGGLE_MARGIN))/2;
		oy = SCALE1(PADDING + PILL_SIZE + BUTTON_MARGIN + MENU_MARGIN_Y + MENU_LINE_MARGIN) + item_size + item_extra_y / 2;
		for (int c = 0; c < state.quickActions->count; c++) {
			SDL_Rect item_rect = {ox, oy, SCALE1(PILL_SIZE), SCALE1(PILL_SIZE)};
			Entry *item = state.quickActions->items[c];

			SDL_Color text_color = uintToColour(THEME_COLOR4_255);
			uint32_t item_color = THEME_COLOR3;
			uint32_t icon_color = THEME_COLOR4;

			if(qm_row == 1 && qm_col == c) {
				text_color = uintToColour(THEME_COLOR5_255);
				item_color = THEME_COLOR1;
				icon_color = THEME_COLOR5;
			}

			GFX_blitPillColor(ASSET_WHITE_PILL, screen, &item_rect, item_color, RGB_WHITE);

			const QuickTile* tile = QuickTiles_forEntry(item);
			if (tile) {
				blitTileIcon(tile, item, item_rect, icon_color, text_color);
				ox += item_rect.w + SCALE1(MENU_TOGGLE_MARGIN);
				continue;
			}

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
