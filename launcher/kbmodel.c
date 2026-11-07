#include <stdio.h>
#include <string.h>
#include "kbmodel.h"

static const char* lower[KB_ROWS - 1] = {
	"1234567890",
	"qwertyuiop",
	"asdfghjkl'",
	"zxcvbnm,.-",
};
static const char* upper[KB_ROWS - 1] = {
	"!@#$%^&*()",
	"QWERTYUIOP",
	"ASDFGHJKL\"",
	"ZXCVBNM;:_",
};

// The bottom row: shift (2 columns), space (4), delete (2), done (2).
static int specialAt(int col) {
	if (col < 2) return KB_KEY_SHIFT;
	if (col < 6) return KB_KEY_SPACE;
	if (col < 8) return KB_KEY_DELETE;
	return KB_KEY_DONE;
}

void Kb_reset(KbModel* kb, const char* initial) {
	memset(kb, 0, sizeof(*kb));
	if (initial) snprintf(kb->text, sizeof(kb->text), "%s", initial);
	kb->row = 1; // start on the letters
}

void Kb_move(KbModel* kb, int drow, int dcol) {
	kb->row = (kb->row + drow + KB_ROWS) % KB_ROWS;
	if (kb->row == KB_ROWS - 1 && dcol) {
		// step over the rest of a wide special key
		int key = specialAt(kb->col);
		int col = kb->col;
		do {
			col = (col + dcol + KB_COLS) % KB_COLS;
		} while (specialAt(col) == key);
		kb->col = col;
		return;
	}
	kb->col = (kb->col + dcol + KB_COLS) % KB_COLS;
}

int Kb_keyAt(const KbModel* kb, int row, int col, char* ch) {
	if (row == KB_ROWS - 1) return specialAt(col);
	if (ch) *ch = (kb->shift ? upper : lower)[row][col];
	return KB_KEY_CHAR;
}

void Kb_type(KbModel* kb, char ch) {
	size_t len = strlen(kb->text);
	if (len + 1 >= sizeof(kb->text)) return;
	kb->text[len] = ch;
	kb->text[len + 1] = '\0';
}

void Kb_delete(KbModel* kb) {
	size_t len = strlen(kb->text);
	if (len) kb->text[len - 1] = '\0';
}

void Kb_toggleShift(KbModel* kb) {
	kb->shift = !kb->shift;
}

int Kb_press(KbModel* kb) {
	char ch = 0;
	switch (Kb_keyAt(kb, kb->row, kb->col, &ch)) {
		case KB_KEY_CHAR:
			Kb_type(kb, ch);
			// one capital at a time, like a phone keyboard
			kb->shift = 0;
			return 0;
		case KB_KEY_SHIFT: Kb_toggleShift(kb); return 0;
		case KB_KEY_SPACE: Kb_type(kb, ' '); return 0;
		case KB_KEY_DELETE: Kb_delete(kb); return 0;
		case KB_KEY_DONE: return 1;
	}
	return 0;
}
