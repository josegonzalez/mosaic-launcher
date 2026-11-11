// The root list's names as the strings slot's owner translates them: a
// system's name between stock's sorting prefix and its tag, and the
// launcher's own rows whole.

#include <string.h>
#include "check.h"
#include "rootnames.h"

static char asked[256];

static const char* fakeTr(const char* context, const char* text) {
	snprintf(asked, sizeof(asked), "%s|%s", context ? context : "-", text);
	if (!context) {
		if (!strcmp(text, "Recently Played")) return "Joués récemment";
		if (!strcmp(text, "Same")) return "Same";
		return text;
	}
	if (!strcmp(context, ROOTNAME_SYSTEMS)) {
		if (!strcmp(text, "Game Boy")) return "Game Boy FR";
		if (!strcmp(text, "Pokémon mini")) return "Pokémon mini FR";
		if (!strcmp(text, "Long")) return "A translation far longer than the room it has";
	}
	return text;
}

static void split(const char* name, const char* want_base) {
	size_t start, len;
	RootName_split(name, &start, &len);
	char got[256];
	snprintf(got, sizeof(got), "%.*s", (int)len, name + start);
	CHECK_STR(got, want_base);
}

static void test_split(void) {
	split("Game Boy (GB)", "Game Boy");
	split("Game Boy", "Game Boy");
	split("Super Nintendo Entertainment System (SFC)", "Super Nintendo Entertainment System");
	split("Pokémon mini (PKM)", "Pokémon mini");
	split("TurboGrafx-16 (PCE)", "TurboGrafx-16");
	// stock's sorting prefix stays out of it
	split("001) Game Boy (GB)", "Game Boy");
	split("12)Sega CD (SEGACD)", "Sega CD");
	// digits without a parenthesis are the name
	split("32X (32X)", "32X");
	// square brackets are tags too, and only the first starts it
	split("Arcade [FBN] (Neo)", "Arcade");
	// a name that's all tag keeps it
	split("(GB)", "(GB)");
	split("", "");

	size_t start, len;
	RootName_split("001) Game Boy (GB)", &start, &len);
	CHECK(start == 5 && len == 8);
}

static void test_translate_rows_and_systems(void) {
	char out[64];
	CHECK(RootName_translate(ROOTNAME_ROW, "Recently Played", fakeTr, out, sizeof(out)) == 1);
	CHECK_STR(out, "Joués récemment");
	CHECK_STR(asked, "-|Recently Played");

	// a system keeps its prefix and tag
	CHECK(RootName_translate(ROOTNAME_SYSTEM, "Game Boy", fakeTr, out, sizeof(out)) == 1);
	CHECK_STR(out, "Game Boy FR");
	CHECK_STR(asked, "systems|Game Boy");
	CHECK(RootName_translate(ROOTNAME_SYSTEM, "Game Boy (GB)", fakeTr, out, sizeof(out)) == 1);
	CHECK_STR(out, "Game Boy FR (GB)");
	CHECK_STR(asked, "systems|Game Boy");
	CHECK(RootName_translate(ROOTNAME_SYSTEM, "02) Pokémon mini (PKM)", fakeTr, out, sizeof(out)) == 1);
	CHECK_STR(out, "02) Pokémon mini FR (PKM)");

	// no translation, the same text, or one that doesn't fit leave out as it was
	strcpy(out, "kept");
	CHECK(RootName_translate(ROOTNAME_ROW, "Tools", fakeTr, out, sizeof(out)) == 0);
	CHECK(RootName_translate(ROOTNAME_ROW, "Same", fakeTr, out, sizeof(out)) == 0);
	CHECK(RootName_translate(ROOTNAME_SYSTEM, "Sega CD (SEGACD)", fakeTr, out, sizeof(out)) == 0);
	CHECK(RootName_translate(ROOTNAME_SYSTEM, "Long (L)", fakeTr, out, 20) == 0);
	CHECK(RootName_translate(ROOTNAME_ROW, "Recently Played", fakeTr, out, 8) == 0);
	CHECK_STR(out, "kept");

	// rows aren't systems: the whole name in no context
	CHECK(RootName_translate(ROOTNAME_ROW, "Game Boy (GB)", fakeTr, out, sizeof(out)) == 0);
	CHECK_STR(asked, "-|Game Boy (GB)");
	CHECK(RootName_translate(-1, "Game Boy", fakeTr, out, sizeof(out)) == 0);
	CHECK(RootName_translate(ROOTNAME_ROW, NULL, fakeTr, out, sizeof(out)) == 0);
	CHECK(RootName_translate(ROOTNAME_ROW, "Recently Played", NULL, out, sizeof(out)) == 0);
}

int main(void) {
	test_split();
	test_translate_rows_and_systems();
	CHECK_DONE();
}
