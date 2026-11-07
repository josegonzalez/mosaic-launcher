#ifndef KBMODEL_H
#define KBMODEL_H

// The on-screen keyboard's state, apart from drawing it: which key is
// highlighted, shift, and the text typed so far.

#define KB_ROWS 5
#define KB_COLS 10
#define KB_TEXT_MAX 128

// The last row holds the special keys, each spanning several columns.
enum KbSpecial {
	KB_KEY_CHAR,
	KB_KEY_SHIFT,
	KB_KEY_SPACE,
	KB_KEY_DELETE,
	KB_KEY_DONE,
};

typedef struct KbModel {
	int row;
	int col;
	int shift;
	char text[KB_TEXT_MAX];
} KbModel;

void Kb_reset(KbModel* kb, const char* initial);

// Moves the highlight, wrapping at the edges.
void Kb_move(KbModel* kb, int drow, int dcol);

// What the key at row/col is: a character (returned in *ch) or a special key.
int Kb_keyAt(const KbModel* kb, int row, int col, char* ch);

// Presses the highlighted key. Returns 1 when that key was Done.
int Kb_press(KbModel* kb);

void Kb_type(KbModel* kb, char ch);
void Kb_delete(KbModel* kb);
void Kb_toggleShift(KbModel* kb);

#endif
