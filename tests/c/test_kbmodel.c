// The on-screen keyboard, without drawing it.

#include "check.h"
#include "kbmodel.h"

static void typeKey(KbModel* kb, int row, int col) {
	kb->row = row;
	kb->col = col;
	Kb_press(kb);
}

int main(void) {
	KbModel kb;
	Kb_reset(&kb, "Fav");
	CHECK_STR(kb.text, "Fav");
	CHECK(kb.row == 1 && kb.col == 0);

	// q is row 1 col 0; shift gives one capital, then back to lower case
	Kb_toggleShift(&kb);
	typeKey(&kb, 1, 0);
	typeKey(&kb, 1, 0);
	CHECK_STR(kb.text, "FavQq");

	// digits, and their shifted symbols
	typeKey(&kb, 0, 0);
	Kb_toggleShift(&kb);
	typeKey(&kb, 0, 0);
	CHECK_STR(kb.text, "FavQq1!");

	// the bottom row: space, delete, done
	typeKey(&kb, 4, 3);
	CHECK_STR(kb.text, "FavQq1! ");
	typeKey(&kb, 4, 6);
	typeKey(&kb, 4, 6);
	CHECK_STR(kb.text, "FavQq1");
	kb.row = 4;
	kb.col = 9;
	CHECK(Kb_press(&kb) == 1);
	CHECK_STR(kb.text, "FavQq1"); // done doesn't type anything

	// moving wraps, and steps over wide keys on the bottom row
	Kb_reset(&kb, "");
	Kb_move(&kb, -1, 0);
	CHECK(kb.row == 0);
	Kb_move(&kb, -1, 0);
	CHECK(kb.row == 4);
	kb.col = 0;
	Kb_move(&kb, 0, 1);
	CHECK(Kb_keyAt(&kb, kb.row, kb.col, NULL) == KB_KEY_SPACE);
	Kb_move(&kb, 0, 1);
	CHECK(Kb_keyAt(&kb, kb.row, kb.col, NULL) == KB_KEY_DELETE);
	Kb_move(&kb, 0, 1);
	CHECK(Kb_keyAt(&kb, kb.row, kb.col, NULL) == KB_KEY_DONE);
	Kb_move(&kb, 0, 1);
	CHECK(Kb_keyAt(&kb, kb.row, kb.col, NULL) == KB_KEY_SHIFT);
	Kb_move(&kb, 0, -1);
	CHECK(Kb_keyAt(&kb, kb.row, kb.col, NULL) == KB_KEY_DONE);
	kb.row = 1;
	kb.col = 9;
	Kb_move(&kb, 0, 1);
	CHECK(kb.col == 0);

	// deleting an empty field, and filling a full one, are both safe
	Kb_reset(&kb, "");
	Kb_delete(&kb);
	CHECK_STR(kb.text, "");
	for (int i = 0; i < KB_TEXT_MAX + 10; i++) Kb_type(&kb, 'a');
	CHECK(strlen(kb.text) == KB_TEXT_MAX - 1);

	CHECK_DONE();
}
