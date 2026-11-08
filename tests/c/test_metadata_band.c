// The metadata band's model: its fields, how values are tidied, and how
// many fit a width.

#include "check.h"
#include "../../plugins/metadata-band/band_model.h"

// ten pixels a byte, so widths are easy to work out
static int measure(const char* text, void* data) {
	(void)data;
	return (int)strlen(text) * 10;
}

static const char* joined(const char* const* values, int count, int max_width, int* n) {
	static char out[64];
	*n = MB_join(values, count, " - ", max_width, measure, NULL, out, sizeof(out));
	return out;
}

int main(void) {
	// keys
	CHECK_STR(MB_key(MB_YEAR), "year");
	CHECK_STR(MB_key(MB_PUBLISHER), "publisher");
	CHECK_STR(MB_key(MB_GENRE), "genre");
	CHECK_STR(MB_key(MB_DEVELOPER), "developer");
	CHECK_STR(MB_key(MB_PLAYERS), "players");
	CHECK(MB_key(-1) == NULL && MB_key(MB_FIELD_COUNT) == NULL);

	// field lists
	int fields[MB_FIELD_COUNT];
	CHECK(MB_fields("year,publisher,genre", fields) == 3);
	CHECK(fields[0] == MB_YEAR && fields[1] == MB_PUBLISHER && fields[2] == MB_GENRE);
	CHECK(MB_fields(" Genre , YEAR ,,bogus, genre,players ", fields) == 3);
	CHECK(fields[0] == MB_GENRE && fields[1] == MB_YEAR && fields[2] == MB_PLAYERS);
	CHECK(MB_fields("developer,players,year,publisher,genre,year", fields) == 5);
	CHECK(fields[4] == MB_GENRE);
	CHECK(MB_fields("", fields) == 0);
	CHECK(MB_fields(" , ,", fields) == 0);
	CHECK(MB_fields("nothing", fields) == 0);
	CHECK(MB_fields(NULL, fields) == 0);

	// tidying values
	char out[16];
	CHECK(MB_clean(MB_PUBLISHER, "  Nintendo \t", out, sizeof(out)) == 1);
	CHECK_STR(out, "Nintendo");
	CHECK(MB_clean(MB_GENRE, "   ", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(MB_clean(MB_GENRE, NULL, out, sizeof(out)) == 0);
	CHECK(MB_clean(MB_YEAR, "1994", out, sizeof(out)) == 1);
	CHECK_STR(out, "1994");
	CHECK(MB_clean(MB_YEAR, "1994-11-21", out, sizeof(out)) == 1);
	CHECK_STR(out, "1994");
	CHECK(MB_clean(MB_YEAR, "19941121T000000", out, sizeof(out)) == 1);
	CHECK_STR(out, "1994");
	CHECK(MB_clean(MB_YEAR, "11/21/1994", out, sizeof(out)) == 1);
	CHECK_STR(out, "1994");
	// a year without four digits stays as it is
	CHECK(MB_clean(MB_YEAR, " '94 ", out, sizeof(out)) == 1);
	CHECK_STR(out, "'94");
	// only the year is cut to its digits
	CHECK(MB_clean(MB_PLAYERS, "1-2", out, sizeof(out)) == 1);
	CHECK_STR(out, "1-2");
	CHECK(MB_clean(MB_DEVELOPER, "Studio 1994 Ltd", out, sizeof(out)) == 1);
	CHECK_STR(out, "Studio 1994 Ltd");
	// too long: cut to fit, between characters, without a trailing space
	CHECK(MB_clean(MB_PUBLISHER, "Hudson Soft Company", out, sizeof(out)) == 1);
	CHECK_STR(out, "Hudson Soft Com");
	CHECK(MB_clean(MB_PUBLISHER, "Hudson Soft  Co", out, 13) == 1);
	CHECK_STR(out, "Hudson Soft");
	CHECK(MB_clean(MB_GENRE, "Ac\xc3\xa7\xc3\xa3o", out, 5) == 1); // "Ação" in 4 bytes
	CHECK_STR(out, "Ac\xc3\xa7");
	CHECK(MB_clean(MB_GENRE, "x", out, 0) == 0);

	// joining to fit
	int n;
	const char* all[] = { "1994", "Nintendo", "Platformer" };
	CHECK_STR(joined(all, 3, 1000, &n), "1994 - Nintendo - Platformer");
	CHECK(n == 3);
	// 28 bytes is 280 pixels; without the genre it's 15
	CHECK_STR(joined(all, 3, 280, &n), "1994 - Nintendo - Platformer");
	CHECK_STR(joined(all, 3, 279, &n), "1994 - Nintendo");
	CHECK(n == 2);
	CHECK_STR(joined(all, 3, 150, &n), "1994 - Nintendo");
	CHECK_STR(joined(all, 3, 149, &n), "1994");
	CHECK(n == 1);
	// the first stays even when it's too wide
	CHECK_STR(joined(all, 3, 10, &n), "1994");
	CHECK(n == 1);
	// empty ones are left out
	const char* gaps[] = { "", "Capcom", NULL, "Fighting" };
	CHECK_STR(joined(gaps, 4, 1000, &n), "Capcom - Fighting");
	CHECK(n == 2);
	const char* none[] = { "", NULL, "" };
	CHECK_STR(joined(none, 3, 1000, &n), "");
	CHECK(n == 0);
	CHECK_STR(joined(none, 0, 1000, &n), "");
	CHECK(n == 0);
	// what doesn't fit in out keeps the values that did
	char small[12];
	CHECK(MB_join(all, 3, " - ", 1000, measure, NULL, small, sizeof(small)) == 1);
	CHECK_STR(small, "1994");
	CHECK(MB_join(all, 3, " - ", 1000, measure, NULL, small, 3) == 1);
	CHECK_STR(small, "19");
	// bad arguments
	CHECK(MB_join(NULL, 3, " - ", 10, measure, NULL, small, sizeof(small)) == -1);
	CHECK(MB_join(all, 3, NULL, 10, measure, NULL, small, sizeof(small)) == -1);
	CHECK(MB_join(all, 3, " - ", 10, NULL, NULL, small, sizeof(small)) == -1);
	CHECK(MB_join(all, 3, " - ", 10, measure, NULL, NULL, sizeof(small)) == -1);
	CHECK(MB_join(all, 3, " - ", 10, measure, NULL, small, 0) == -1);
	CHECK(MB_join(all, -1, " - ", 10, measure, NULL, small, sizeof(small)) == -1);

	CHECK_DONE();
}
