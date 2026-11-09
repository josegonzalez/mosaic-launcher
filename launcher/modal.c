#include "launcher.h"
#include "kbmodel.h"

enum { MODAL_NONE, MODAL_CONFIRM, MODAL_MESSAGE, MODAL_KEYBOARD, MODAL_CHOICE };

static int kind = MODAL_NONE;
static int return_to = SCREEN_GAMELIST;
static char message[256];
static char yes_label[32];
static char title[64];
static void (*confirm_yes)(void* data);
static void (*keyboard_done)(const char* text, void* data);
static void (*choice_pick)(int index, void* data);
static void* callback_data;
static KbModel kb;
static int kb_hidden = 0; // the field shows a * for each character typed
static char* choices[MODAL_CHOICES_MAX];
static int choice_count = 0;
static int choice_selected = 0;
static int choice_start = 0; // first visible row

static void openModal(int what) {
	if (kind == MODAL_NONE) return_to = currentScreen;
	kind = what;
	currentScreen = SCREEN_MODAL;
	state.dirty = 1;
}

static void closeModal(void) {
	kind = MODAL_NONE;
	currentScreen = return_to;
	state.dirty = 1;
	folderbgchanged = 1; // the dialog cleared every layer; repaint the background
}

void Modal_confirm(const char* text, const char* yes, void (*on_yes)(void* data), void* data) {
	snprintf(message, sizeof(message), "%s", text ? text : "");
	snprintf(yes_label, sizeof(yes_label), "%s", yes && yes[0] ? yes : "OK");
	confirm_yes = on_yes;
	callback_data = data;
	openModal(MODAL_CONFIRM);
}

void Modal_message(const char* text) {
	snprintf(message, sizeof(message), "%s", text ? text : "");
	confirm_yes = NULL;
	callback_data = NULL;
	openModal(MODAL_MESSAGE);
}

void Modal_keyboard(const char* heading, const char* initial, void (*on_done)(const char* text, void* data), void* data) {
	snprintf(title, sizeof(title), "%s", heading ? heading : "");
	Kb_reset(&kb, initial);
	kb_hidden = 0;
	keyboard_done = on_done;
	callback_data = data;
	openModal(MODAL_KEYBOARD);
}

void Modal_keyboardHidden(const char* heading, void (*on_done)(const char* text, void* data), void* data) {
	Modal_keyboard(heading, "", on_done, data);
	kb_hidden = 1;
}

static void freeChoices(void) {
	for (int i = 0; i < choice_count; i++) free(choices[i]);
	choice_count = 0;
}

int Modal_choose(const char* heading, const char* const* list, int count, int selected,
		void (*on_pick)(int index, void* data), void* data) {
	if (!list || count < 1 || count > MODAL_CHOICES_MAX) return -1;
	// a choice list opened from another one's callback replaces it
	freeChoices();
	for (int i = 0; i < count; i++) choices[choice_count++] = strdup(list[i] ? list[i] : "");
	snprintf(title, sizeof(title), "%s", heading ? heading : "");
	choice_selected = selected >= 0 && selected < count ? selected : 0;
	choice_start = 0;
	choice_pick = on_pick;
	callback_data = data;
	openModal(MODAL_CHOICE);
	return 0;
}

///////////////////////////////////////
// input

static void confirmInput(void) {
	if (PAD_justPressed(BTN_A)) {
		void (*cb)(void*) = confirm_yes;
		void* data = callback_data;
		closeModal();
		if (cb) cb(data); // after closing, so it can open another dialog
	}
	else if (PAD_justPressed(BTN_B)) {
		closeModal();
	}
}

static void messageInput(void) {
	if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) closeModal();
}

static void keyboardFinish(int done) {
	char text[KB_TEXT_MAX];
	snprintf(text, sizeof(text), "%s", kb.text);
	memset(kb.text, 0, sizeof(kb.text)); // a PIN shouldn't outlive its keyboard
	void (*cb)(const char*, void*) = keyboard_done;
	void* data = callback_data;
	closeModal();
	if (cb) cb(done ? text : NULL, data);
	memset(text, 0, sizeof(text));
}

static void keyboardInput(void) {
	int changed = 1;
	if (PAD_justRepeated(BTN_UP)) Kb_move(&kb, -1, 0);
	else if (PAD_justRepeated(BTN_DOWN)) Kb_move(&kb, 1, 0);
	else if (PAD_justRepeated(BTN_LEFT)) Kb_move(&kb, 0, -1);
	else if (PAD_justRepeated(BTN_RIGHT)) Kb_move(&kb, 0, 1);
	else if (PAD_justPressed(BTN_A)) {
		if (Kb_press(&kb)) {
			keyboardFinish(1);
			return;
		}
	}
	else if (PAD_justRepeated(BTN_B)) Kb_delete(&kb);
	else if (PAD_justPressed(BTN_X)) Kb_toggleShift(&kb);
	else if (PAD_justPressed(BTN_START)) {
		keyboardFinish(1);
		return;
	}
	else if (PAD_justPressed(BTN_Y)) {
		keyboardFinish(0);
		return;
	}
	else changed = 0;
	if (changed) state.dirty = 1;
}

static void choiceFinish(int index) {
	void (*cb)(int, void*) = choice_pick;
	void* data = callback_data;
	closeModal();
	if (cb) cb(index, data);
}

static void choiceInput(void) {
	if (PAD_justRepeated(BTN_UP)) {
		choice_selected = (choice_selected - 1 + choice_count) % choice_count;
		state.dirty = 1;
	}
	else if (PAD_justRepeated(BTN_DOWN)) {
		choice_selected = (choice_selected + 1) % choice_count;
		state.dirty = 1;
	}
	else if (PAD_justPressed(BTN_A)) choiceFinish(choice_selected);
	else if (PAD_justPressed(BTN_B)) choiceFinish(-1);
}

void Modal_handleInput(void) {
	if (kind == MODAL_CONFIRM) confirmInput();
	else if (kind == MODAL_CHOICE) choiceInput();
	else if (kind == MODAL_MESSAGE) messageInput();
	else if (kind == MODAL_KEYBOARD) keyboardInput();
	else closeModal(); // nothing to show; shouldn't happen
}

///////////////////////////////////////
// drawing

static void drawCentered(TTF_Font* f, int size, const char* text, SDL_Color color, SDL_Rect cell) {
	int w, h;
	GFX_sizeText(f, text, SCALE1(size), &w, &h);
	SDL_Rect r = { cell.x + (cell.w - w) / 2, cell.y + (cell.h - h) / 2, w, h };
	GFX_blitText(f, text, SCALE1(size), color, screen, &r);
}

static void confirmRender(void) {
	SDL_Rect area = { 0, 0, screen->w, screen->h - SCALE1(PADDING + PILL_SIZE) };
	GFX_blitMessage(font.large, message, screen, &area);
	GFX_blitButtonGroup((char*[]){ "B", TR("CANCEL"), "A", TR(yes_label), NULL }, 1, screen, 1);
}

static void messageRender(void) {
	SDL_Rect area = { 0, 0, screen->w, screen->h - SCALE1(PADDING + PILL_SIZE) };
	GFX_blitMessage(font.large, message, screen, &area);
	GFX_blitButtonGroup((char*[]){ "A", TR("OK"), NULL }, 0, screen, 1);
}

static const char* specialLabel(int key) {
	switch (key) {
		case KB_KEY_SHIFT: return TR("SHIFT");
		case KB_KEY_SPACE: return TR("SPACE");
		case KB_KEY_DELETE: return TR("DELETE");
		case KB_KEY_DONE: return TR("DONE");
	}
	return "";
}

static void keyboardRender(void) {
	SDL_Color normal = uintToColour(THEME_COLOR4_255);
	SDL_Color picked = uintToColour(THEME_COLOR5_255);
	int cell = SCALE1(PILL_SIZE);

	// title and the text so far
	SDL_Rect title_rect = { SCALE1(PADDING), SCALE1(PADDING), screen->w - SCALE1(PADDING * 2), cell };
	drawCentered(font.small, FONT_SMALL, title, normal, title_rect);
	SDL_Rect field = { SCALE1(PADDING), SCALE1(PADDING) + cell + SCALE1(BUTTON_MARGIN), screen->w - SCALE1(PADDING * 2), cell };
	GFX_blitPillDark(ASSET_WHITE_PILL, screen, &field);
	char shown[KB_TEXT_MAX + 2];
	if (kb_hidden) {
		size_t len = strlen(kb.text);
		memset(shown, '*', len);
		snprintf(shown + len, sizeof(shown) - len, "_");
	}
	else snprintf(shown, sizeof(shown), "%s_", kb.text);
	drawCentered(font.large, FONT_LARGE, shown, picked, field);

	// the keys
	int grid_w = KB_COLS * cell;
	int x0 = (screen->w - grid_w) / 2;
	int y0 = field.y + cell + SCALE1(BUTTON_MARGIN * 2);
	for (int row = 0; row < KB_ROWS; row++) {
		for (int col = 0; col < KB_COLS; col++) {
			char ch = 0;
			int key = Kb_keyAt(&kb, row, col, &ch);
			int span = 1;
			if (key != KB_KEY_CHAR) {
				// draw a wide key once, from its first column
				if (col > 0 && Kb_keyAt(&kb, row, col - 1, NULL) == key) continue;
				while (col + span < KB_COLS && Kb_keyAt(&kb, row, col + span, NULL) == key) span++;
			}
			SDL_Rect r = { x0 + col * cell, y0 + row * cell, span * cell, cell };
			int selected = row == kb.row && Kb_keyAt(&kb, kb.row, kb.col, NULL) == key
				&& (key != KB_KEY_CHAR || col == kb.col);
			if (selected) GFX_blitPillDark(ASSET_WHITE_PILL, screen, &r);
			char label[8];
			if (key == KB_KEY_CHAR) snprintf(label, sizeof(label), "%c", ch);
			else snprintf(label, sizeof(label), "%s", specialLabel(key));
			if (key == KB_KEY_SHIFT && kb.shift) selected = 1;
			drawCentered(key == KB_KEY_CHAR ? font.large : font.tiny, key == KB_KEY_CHAR ? FONT_LARGE : FONT_TINY,
				label, selected ? picked : normal, r);
		}
	}

	GFX_blitButtonGroup((char*[]){ "Y", TR("CANCEL"), "X", TR("SHIFT"), NULL }, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){ "B", TR("DELETE"), "START", TR("DONE"), NULL }, 1, screen, 1);
}

static void blitRowText(TTF_Font* f, const char* text, SDL_Color color, int x, int y, int max_w) {
	if (!text[0] || max_w <= 0) return;
	SDL_Surface* s = TTF_RenderUTF8_Blended(f, text, color);
	if (!s) return;
	SDL_Rect src = { 0, 0, MIN(s->w, max_w), s->h };
	SDL_Rect dst = { x, y + ((SCALE1(PILL_SIZE) - s->h + 1) >> 1) };
	SDL_BlitSurface(s, &src, screen, &dst);
	SDL_FreeSurface(s);
}

// The title beside the battery and clock, then a row per choice with the
// selected one on a pill, as settings.c draws the Launcher pak's menus.
static void choiceRender(void) {
	SDL_Color normal = uintToColour(THEME_COLOR4_255);
	SDL_Color picked = uintToColour(THEME_COLOR5_255);
	int left = SCALE1(BUTTON_MARGIN);
	int width = screen->w - SCALE1(BUTTON_MARGIN * 2);
	int pad = SCALE1(BUTTON_PADDING);

	char shown[256];
	Text_truncate(font.large, title, shown, sizeof(shown), screen->w - SCALE1(PADDING * 2) - ow - header_w, pad * 2);
	blitRowText(font.large, shown, normal, left + pad, SCALE1(PADDING), screen->w);

	int visible = MAIN_ROW_COUNT - 1;
	if (choice_selected < choice_start) choice_start = choice_selected;
	if (choice_selected >= choice_start + visible) choice_start = choice_selected - visible + 1;

	for (int i = choice_start, j = 1; i < choice_count && j <= visible; i++, j++) {
		int y = SCALE1(PADDING + j * PILL_SIZE);
		char label[256];
		int label_w = Text_truncate(font.large, choices[i], label, sizeof(label), width, pad * 2);
		int selected = i == choice_selected;
		if (selected) GFX_blitPillDark(ASSET_WHITE_PILL, screen, &(SDL_Rect){ left, y, label_w, SCALE1(PILL_SIZE) });
		blitRowText(font.large, label, selected ? picked : normal, left + pad, y, label_w - pad * 2);
	}

	if (show_setting) Text_hardwareHints(screen, show_setting);
	GFX_blitButtonGroup((char*[]){ "B", TR("BACK"), "A", TR("SELECT"), NULL }, 1, screen, 1);
}

void Modal_render(void) {
	GFX_clearLayers(LAYER_ALL);
	if (kind == MODAL_CHOICE) choiceRender();
	else if (kind == MODAL_CONFIRM) confirmRender();
	else if (kind == MODAL_MESSAGE) messageRender();
	else if (kind == MODAL_KEYBOARD) keyboardRender();
	lastScreen = SCREEN_MODAL;
}
