// fallbackmodel.c: UTF-8 decoding, splitting text into UI and fallback
// font runs, their widths, and where wrapped text breaks.

#include <stdint.h>
#include <string.h>
#include "check.h"
#include "fallbackmodel.h"

// A UI font with ASCII and Latin-1 only, and a fallback with everything
// from U+3000 up (CJK, kana, Hangul) and the combining marks.
static int asked;
static int pick(uint32_t cp, void* data) {
	(void)data;
	asked++;
	if (cp < 0x100) return FALLBACK_UI;
	return FALLBACK_SECOND;
}

// The runs of text as "<font>:<bytes>" pairs, such as "0:3 1:6".
static const char* runs(const char* text) {
	static char out[256];
	out[0] = '\0';
	size_t len = strlen(text);
	size_t at = 0;
	while (at < len) {
		int font;
		size_t n = FallbackModel_run(text, len, at, pick, NULL, &font);
		if (!n) break;
		char one[32];
		snprintf(one, sizeof(one), "%s%d:%zu", out[0] ? " " : "", font, n);
		strncat(out, one, sizeof(out) - strlen(out) - 1);
		at += n;
	}
	return out;
}

static void testDecode(void) {
	uint32_t cp;
	CHECK(FallbackModel_decode("A", 1, &cp) == 1 && cp == 'A');
	CHECK(FallbackModel_decode("\xC3\xA9", 2, &cp) == 2 && cp == 0xE9);            // é
	CHECK(FallbackModel_decode("\xE6\x97\xA5", 3, &cp) == 3 && cp == 0x65E5);       // 日
	CHECK(FallbackModel_decode("\xED\x95\x9C", 3, &cp) == 3 && cp == 0xD55C);       // 한
	CHECK(FallbackModel_decode("\xF0\xA0\x80\x80", 4, &cp) == 4 && cp == 0x20000);  // CJK extension B
	// malformed: a lone continuation byte, a cut off sequence, an overlong
	// form, a surrogate and past U+10FFFF are one byte each
	CHECK(FallbackModel_decode("\x80", 1, &cp) == 1 && cp == FALLBACK_BAD);
	CHECK(FallbackModel_decode("\xE6\x97", 2, &cp) == 1 && cp == FALLBACK_BAD);
	CHECK(FallbackModel_decode("\xE6\x97" "A", 3, &cp) == 1 && cp == FALLBACK_BAD);
	CHECK(FallbackModel_decode("\xC0\xAF", 2, &cp) == 1 && cp == FALLBACK_BAD);
	CHECK(FallbackModel_decode("\xED\xA0\x80", 3, &cp) == 1 && cp == FALLBACK_BAD);
	CHECK(FallbackModel_decode("\xF4\x90\x80\x80", 4, &cp) == 1 && cp == FALLBACK_BAD);
	CHECK(FallbackModel_decode("\xFF", 1, &cp) == 1 && cp == FALLBACK_BAD);
}

static void testJoins(void) {
	CHECK(FallbackModel_joins(0x0301));  // combining acute
	CHECK(FallbackModel_joins(0x3099));  // combining kana voiced mark
	CHECK(FallbackModel_joins(0xFE0F));  // variation selector 16
	CHECK(FallbackModel_joins(0x200D));  // zero width joiner
	CHECK(!FallbackModel_joins('a'));
	CHECK(!FallbackModel_joins(0x65E5));
}

static void testRuns(void) {
	// all ASCII is one UI run, asked once per character
	asked = 0;
	CHECK_STR(runs("Hello"), "0:5");
	CHECK(asked == 5);
	CHECK_STR(runs(""), "");
	// CJK alone, mixed, and back again
	CHECK_STR(runs("\xE6\x97\xA5\xE6\x9C\xAC"), "1:6");                 // 日本
	CHECK_STR(runs("Mario \xE6\x97\xA5\xE6\x9C\xAC!"), "0:6 1:6 0:1"); // Mario 日本!
	CHECK_STR(runs("\xED\x95\x9C" "A\xEA\xB8\x80"), "1:3 0:1 1:3");     // 한A글
	// a combining mark stays with the character before it, in either font
	CHECK_STR(runs("e\xCC\x81" "x"), "0:4");                            // é as e + U+0301, then x
	CHECK_STR(runs("\xE3\x81\x8B\xE3\x82\x99" "a"), "1:6 0:1");        // か + U+3099, then a
	// but one that starts the text goes to its own font
	CHECK_STR(runs("\xCC\x81" "a"), "1:2 0:1");
	// malformed bytes go to the UI font, as SDL_ttf would draw them alone
	CHECK_STR(runs("\xE6\x97\xA5\x80\xE6\x97\xA5"), "1:3 0:1 1:3");
	// the length bounds the text, not its end
	int font = -1;
	CHECK(FallbackModel_run("AB\xE6\x97\xA5", 2, 0, pick, NULL, &font) == 2 && font == FALLBACK_UI);
	CHECK(FallbackModel_run("AB", 2, 2, pick, NULL, &font) == 0);
}

// UI characters are 5 wide, fallback ones 10; "!" can't be measured
static int runWidth(int font, const char* text, size_t len, void* data) {
	(void)data;
	int chars = 0;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '!') return -1;
		if (((unsigned char)text[i] & 0xC0) != 0x80) chars++;
	}
	return chars * (font == FALLBACK_UI ? 5 : 10);
}

static void testWidths(void) {
	const char* mixed = "ab\xE6\x97\xA5\xE6\x9C\xAC" "c";
	CHECK(FallbackModel_width(mixed, strlen(mixed), pick, runWidth, NULL) == 5 * 3 + 10 * 2);
	CHECK(FallbackModel_width("abc", 3, pick, runWidth, NULL) == 15);
	CHECK(FallbackModel_width("", 0, pick, runWidth, NULL) == 0);
	CHECK(FallbackModel_width("a!", 2, pick, runWidth, NULL) == -1);
}

static int measure(const char* text, size_t len, void* data) {
	return FallbackModel_width(text, len, pick, runWidth, data);
}

// the lines text wraps to at max_width, joined by "|"
static const char* wrapped(const char* text, int max_width) {
	static char out[256];
	out[0] = '\0';
	size_t len = strlen(text);
	size_t at = 0;
	int lines = 0;
	while (at < len && lines < 20) {
		size_t next;
		size_t n = FallbackModel_line(text + at, len - at, max_width, measure, NULL, &next);
		if (lines++) strncat(out, "|", sizeof(out) - strlen(out) - 1);
		strncat(out, text + at, n < sizeof(out) - strlen(out) - 1 ? n : sizeof(out) - strlen(out) - 1);
		at += next;
	}
	return out;
}

static void testLines(void) {
	// at spaces, dropping the space broken at
	CHECK_STR(wrapped("one two three", 40), "one two|three");
	CHECK_STR(wrapped("one two three", 25), "one|two|three");
	CHECK_STR(wrapped("one   two", 20), "one|two");
	// newlines always break
	CHECK_STR(wrapped("one\ntwo", 100), "one|two");
	CHECK_STR(wrapped("one\n\ntwo", 0), "one||two");
	// a word wider than the line breaks between characters
	CHECK_STR(wrapped("abcdefgh", 20), "abcd|efgh");
	// CJK, with no spaces, breaks between characters, never inside one
	CHECK_STR(wrapped("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 20), "\xE6\x97\xA5\xE6\x9C\xAC|\xE8\xAA\x9E");
	// at least one character a line, however narrow
	CHECK_STR(wrapped("\xE6\x97\xA5\xE6\x9C\xAC", 5), "\xE6\x97\xA5|\xE6\x9C\xAC");
	// no width: only newlines break
	CHECK_STR(wrapped("one two three", 0), "one two three");
	// what fits stays one line
	CHECK_STR(wrapped("one two", 35), "one two");
}

int main(void) {
	testDecode();
	testJoins();
	testRuns();
	testWidths();
	testLines();
	CHECK_DONE();
}
