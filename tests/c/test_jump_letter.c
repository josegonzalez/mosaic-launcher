// jump-letter's model: letters, a list's sections, the strip's window and
// the hold that opens it.

#include <stdio.h>
#include <string.h>
#include "check.h"
#include "jump_model.h"

static const char* letterOf(const char* name) {
	static char out[8];
	JL_letter(name, out, sizeof(out));
	return out;
}

static void test_letters_fold_case_and_accents(void) {
	CHECK_STR(letterOf("alpha"), "A");
	CHECK_STR(letterOf("Zelda"), "Z");
	// accents and ligatures fold to their letter, either case
	CHECK_STR(letterOf("\xc3\x89lite"), "E");    // Élite
	CHECK_STR(letterOf("\xc3\xa9t\xc3\xa9"), "E"); // été
	CHECK_STR(letterOf("\xc3\x91u"), "N");       // Ñu
	CHECK_STR(letterOf("\xc3\x98ystein"), "O");  // Øystein
	CHECK_STR(letterOf("\xc3\x9f"), "S");        // ß
	CHECK_STR(letterOf("\xc3\xbf"), "Y");        // ÿ
	CHECK_STR(letterOf("\xc5\x81\xc3\xb3""d\xc5\xba"), "L"); // Łódź
	CHECK_STR(letterOf("\xc5\x92uvre"), "O");    // Œuvre
	CHECK_STR(letterOf("\xc5\xbd""ivot"), "Z");  // Život
	CHECK_STR(letterOf("\xc4\xb0stanbul"), "I"); // İstanbul
	CHECK_STR(letterOf("\xc5\xbf"), "S");        // long s, the last of Latin Extended-A
	CHECK_STR(letterOf("\xc4\x80"), "A");        // Ā, the first
}

static void test_digits_symbols_and_others_are_hash(void) {
	CHECK_STR(letterOf("1942"), "#");
	CHECK_STR(letterOf("'Splosion Man"), "#");
	CHECK_STR(letterOf("[BIOS]"), "#");
	CHECK_STR(letterOf(""), "#");
	CHECK_STR(letterOf(NULL), "#");
	CHECK_STR(letterOf("\xc3\x97"), "#");        // ×
	CHECK_STR(letterOf("\xc3\x9e"), "#");        // Þ
	CHECK_STR(letterOf("\xc2\xa1Hola"), "#");    // ¡
	CHECK_STR(letterOf("\xe3\x83\x9e\xe3\x83\xaa\xe3\x82\xaa"), "#"); // マリオ
	CHECK_STR(letterOf("\xd0\x96"), "#");        // Ж
	CHECK_STR(letterOf("\xc3"), "#");            // cut short
	// too small for a letter
	char tiny[1] = { 'x' };
	JL_letter("Alpha", tiny, sizeof(tiny));
	CHECK(tiny[0] == '\0');
}

typedef struct Names {
	const char** names;
	const char** keys; // a plugin's section keys, or NULL for letters
} Names;

static void keyOf(int i, char* out, size_t size, void* data) {
	const Names* n = data;
	if (n->keys) snprintf(out, size, "%s", n->keys[i] ? n->keys[i] : "");
	else JL_letter(n->names[i], out, size);
}

static void test_sections_from_a_list(void) {
	const char* names[] = { "1942", "Alpha", "alto", "\xc3\x81rbol", "Bravo", "Delta", "Echo", "\xc3\xa9t\xc3\xa9" };
	Names n = { names, NULL };
	JLSections s;
	int at = -2;
	JL_sections(&s, 8, keyOf, &n, 5, &at);
	// only the letters there are, in the list's order, each where it starts
	CHECK(s.count == 5);
	CHECK_STR(s.keys[0], "#");
	CHECK_STR(s.keys[1], "A");
	CHECK_STR(s.keys[2], "B");
	CHECK_STR(s.keys[3], "D");
	CHECK_STR(s.keys[4], "E");
	CHECK(s.first[0] == 0 && s.first[1] == 1 && s.first[2] == 4 && s.first[3] == 5 && s.first[4] == 6);
	// Delta is in D
	CHECK(at == 3);
	JL_sections(&s, 8, keyOf, &n, 3, &at);
	CHECK(at == 1); // Árbol in A
	JL_sections(&s, 8, keyOf, &n, 8, &at);
	CHECK(at == -1); // not in the list
	JL_sections(&s, 0, keyOf, &n, 0, &at);
	CHECK(s.count == 0 && at == -1);
	JL_sections(&s, 8, keyOf, &n, 0, NULL);
	CHECK(s.count == 5);
}

static void test_sections_follow_the_list_order(void) {
	// pinned first: Zelda leads, and its letter isn't listed twice
	const char* names[] = { "Zelda", "Alpha", "Bravo", "Zulu" };
	Names n = { names, NULL };
	JLSections s;
	int at;
	JL_sections(&s, 4, keyOf, &n, 3, &at);
	CHECK(s.count == 3);
	CHECK_STR(s.keys[0], "Z");
	CHECK_STR(s.keys[1], "A");
	CHECK_STR(s.keys[2], "B");
	CHECK(s.first[0] == 0);
	CHECK(at == 0); // Zulu is in Z, which starts at Zelda
}

static void test_custom_section_keys(void) {
	// a sort by year gives its years, and an entry with none goes under #
	const char* names[] = { "Bravo", "Alpha", "Delta", "Charlie", "Echo" };
	const char* keys[] = { "1991", "1991", "1993", "1994", "" };
	Names n = { names, keys };
	JLSections s;
	int at;
	JL_sections(&s, 5, keyOf, &n, 2, &at);
	CHECK(s.count == 4);
	CHECK_STR(s.keys[0], "1991");
	CHECK_STR(s.keys[1], "1993");
	CHECK_STR(s.keys[2], "1994");
	CHECK_STR(s.keys[3], "#");
	CHECK(s.first[1] == 2 && s.first[3] == 4);
	CHECK(at == 1);
	// a long key is cut to fit
	char long_key[200];
	memset(long_key, 'p', sizeof(long_key) - 1);
	long_key[sizeof(long_key) - 1] = '\0';
	const char* long_keys[] = { long_key, "Sega" };
	Names l = { names, long_keys };
	JL_sections(&s, 2, keyOf, &l, 0, &at);
	CHECK(s.count == 2 && strlen(s.keys[0]) == JL_KEY_LEN - 1);
}

static void indexKey(int i, char* out, size_t size, void* data) {
	(void)data;
	snprintf(out, size, "%d", i);
}

static void test_sections_past_the_most(void) {
	JLSections s;
	int at;
	JL_sections(&s, JL_SECTIONS_MAX + 10, indexKey, NULL, JL_SECTIONS_MAX + 5, &at);
	// the rest go in the last, so each entry still has one
	CHECK(s.count == JL_SECTIONS_MAX);
	CHECK(at == JL_SECTIONS_MAX - 1);
}

static void test_steps_stay_in_the_strip(void) {
	CHECK(JL_step(0, -1, 5) == 0);
	CHECK(JL_step(0, 1, 5) == 1);
	CHECK(JL_step(4, 1, 5) == 4);
	CHECK(JL_step(2, -1, 5) == 1);
	CHECK(JL_step(3, 0, 0) == 0);
}

static void test_window_keeps_the_lit_one_near_the_middle(void) {
	int widths[] = { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 };
	int first, last;
	// all fit
	JL_window(widths, 10, 2, 200, 0, &first, &last);
	CHECK(first == 0 && last == 9);
	// five fit (5 * 10 + 4 * 2 = 58), around the lit one
	JL_window(widths, 10, 2, 60, 5, &first, &last);
	CHECK(first == 3 && last == 7);
	// at an end, the rest come from the other side
	JL_window(widths, 10, 2, 60, 0, &first, &last);
	CHECK(first == 0 && last == 4);
	JL_window(widths, 10, 2, 60, 9, &first, &last);
	CHECK(first == 5 && last == 9);
	// only the lit one, even when it doesn't fit
	JL_window(widths, 10, 2, 5, 4, &first, &last);
	CHECK(first == 4 && last == 4);
	JL_window(widths, 0, 2, 60, 0, &first, &last);
	CHECK(last < first);
	// a wide one leaves less room
	int mixed[] = { 10, 40, 10, 10 };
	JL_window(mixed, 4, 0, 30, 2, &first, &last);
	CHECK(first == 2 && last == 3);
}

static void test_hold_opens_jumps_and_cancels(void) {
	JLHold h = { JL_IDLE, 0, 0 };
	// a tap is left alone
	JL_holdArm(&h, 1000);
	CHECK(JL_holdFrame(&h, 1, 0, 1100) == 0 && h.state == JL_ARMED);
	CHECK(JL_holdFrame(&h, 0, 0, 1200) == 0 && h.state == JL_IDLE);
	CHECK(JL_holdRelease(&h) == 0);

	// held past the hold time it opens, once, and letting go jumps
	JL_holdArm(&h, 2000);
	CHECK(JL_holdFrame(&h, 1, 0, 2000 + JL_HOLD_MS - 1) == 0);
	CHECK(JL_holdFrame(&h, 1, 0, 2000 + JL_HOLD_MS) == 1 && h.state == JL_OPEN);
	CHECK(JL_holdFrame(&h, 1, 0, 2000 + JL_HOLD_MS + 16) == 0 && h.state == JL_OPEN);
	CHECK(JL_holdRelease(&h) == 1 && h.state == JL_IDLE);

	// B closes it, and the button's release then does nothing
	JL_holdArm(&h, 3000);
	CHECK(JL_holdFrame(&h, 1, 0, 3400) == 1);
	JL_holdCancel(&h);
	CHECK(h.state == JL_CANCELLED);
	CHECK(JL_holdFrame(&h, 1, 0, 3500) == 0 && h.state == JL_CANCELLED);
	CHECK(JL_holdFrame(&h, 0, 0, 3600) == 0 && h.state == JL_IDLE);
	JL_holdArm(&h, 4000);
	CHECK(JL_holdFrame(&h, 1, 0, 4400) == 1);
	JL_holdCancel(&h);
	CHECK(JL_holdRelease(&h) == 0 && h.state == JL_IDLE);

	// a chord, such as the lock's SELECT and START, never opens it
	JL_holdArm(&h, 5000);
	CHECK(JL_holdFrame(&h, 1, 1, 5100) == 0 && h.state == JL_IDLE);
	CHECK(JL_holdFrame(&h, 1, 0, 5400) == 0 && h.state == JL_IDLE);

	// sticky: open at once, the button's release leaves it open, B closes it
	JL_holdOpenSticky(&h);
	CHECK(h.state == JL_OPEN && h.sticky);
	CHECK(JL_holdRelease(&h) == 0 && h.state == JL_OPEN);
	JL_holdCancel(&h);
	CHECK(h.state == JL_IDLE);
	// arming again isn't sticky
	JL_holdArm(&h, 6000);
	CHECK(!h.sticky);
	// cancelling with nothing open does nothing
	h.state = JL_IDLE;
	JL_holdCancel(&h);
	CHECK(h.state == JL_IDLE);
}

int main(void) {
	test_letters_fold_case_and_accents();
	test_digits_symbols_and_others_are_hash();
	test_sections_from_a_list();
	test_sections_follow_the_list_order();
	test_custom_section_keys();
	test_sections_past_the_most();
	test_steps_stay_in_the_strip();
	test_window_keeps_the_lit_one_near_the_middle();
	test_hold_opens_jumps_and_cancels();
	CHECK_DONE();
}
