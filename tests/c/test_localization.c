// The localization plugin: its .po reader and Plural-Forms evaluator, its
// names lists and map.txt merging, and the plugin against a fake host.

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/localization/localization.c"

#define CARD TEST_DIR "/card"
#define RES LOCALIZATION_TEST_RES
#define LANGS CARD "/Languages"

static PoCatalog* parse(const char* text, char* err, size_t size) {
	return Po_parse(text, strlen(text), err, size);
}

// 1 when text is refused with an error naming line
static int refusedAt(const char* text, int line) {
	char err[256], want[32];
	PoCatalog* c = parse(text, err, sizeof(err));
	if (c) {
		Po_free(c);
		fprintf(stderr, "accepted: %s\n", text);
		return 0;
	}
	snprintf(want, sizeof(want), "line %i:", line);
	if (strncmp(err, want, strlen(want)) != 0) {
		fprintf(stderr, "error \"%s\", want %s\n", err, want);
		return 0;
	}
	return 1;
}

static unsigned long evalOr(const char* expr, unsigned long n, unsigned long fallback) {
	PoPlural rule;
	unsigned long out;
	if (PoPlural_compile(expr, &rule) < 0) return fallback;
	if (PoPlural_eval(&rule, n, &out) < 0) return fallback;
	return out;
}

static int compiles(const char* expr) {
	PoPlural rule;
	return PoPlural_compile(expr, &rule) == 0;
}

#define RU "n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2"
#define AR "n==0 ? 0 : n==1 ? 1 : n==2 ? 2 : n%100>=3 && n%100<=10 ? 3 : n%100>=11 ? 4 : 5"
#define PL "(n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2)"

static void testPlural(void) {
	CHECK(evalOr("n != 1", 1, 9) == 0);
	CHECK(evalOr("n != 1", 0, 9) == 1);
	CHECK(evalOr("(n != 1)", 2, 9) == 1);
	CHECK(evalOr("n>1", 1, 9) == 0);
	CHECK(evalOr("n>1", 0, 9) == 0);
	CHECK(evalOr("0", 5, 9) == 0);
	CHECK(evalOr("!n", 0, 9) == 1);
	CHECK(evalOr("!!n", 7, 9) == 1);
	CHECK(evalOr("1 + 2 * 3", 0, 9) == 7);
	CHECK(evalOr("(1 + 2) * 3", 0, 9) == 9);
	CHECK(evalOr("10 - 2 - 3", 0, 99) == 5);
	CHECK(evalOr("7 / 2", 0, 9) == 3);
	CHECK(evalOr("7 % 4", 0, 9) == 3);
	CHECK(evalOr("n<=1", 1, 9) == 1);
	CHECK(evalOr("n>=2", 1, 9) == 0);
	CHECK(evalOr("n==0||n==1", 0, 9) == 1);
	CHECK(evalOr("n==0||n==1", 2, 9) == 0);
	CHECK(evalOr("n && 0", 5, 9) == 0);
	CHECK(evalOr("n == 1 ? 0 : n == 2 ? 1 : 2", 2, 9) == 1); // right-associative
	CHECK(evalOr("n == 1 ? 0 : n == 2 ? 1 : 2", 3, 9) == 2);
	CHECK(evalOr("1 < 2 == 1", 0, 9) == 1);
	// short-circuit: the division by zero isn't evaluated
	CHECK(evalOr("0 && 1/0", 0, 9) == 0);
	CHECK(evalOr("1 || 1/0", 0, 9) == 1);
	CHECK(evalOr("n ? 1 : 1/0", 1, 9) == 1);

	// Russian, Arabic and Polish
	unsigned long ru[][2] = { { 1, 0 }, { 2, 1 }, { 5, 2 }, { 11, 2 }, { 21, 0 }, { 22, 1 }, { 111, 2 }, { 0, 2 } };
	for (size_t i = 0; i < sizeof(ru) / sizeof(ru[0]); i++) CHECK(evalOr(RU, ru[i][0], 9) == ru[i][1]);
	unsigned long ar[][2] = { { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 10, 3 }, { 11, 4 }, { 99, 4 }, { 100, 5 }, { 102, 5 } };
	for (size_t i = 0; i < sizeof(ar) / sizeof(ar[0]); i++) CHECK(evalOr(AR, ar[i][0], 9) == ar[i][1]);
	CHECK(evalOr(PL, 1, 9) == 0);
	CHECK(evalOr(PL, 3, 9) == 1);
	CHECK(evalOr(PL, 12, 9) == 2);
	CHECK(evalOr(PL, 25, 9) == 2);

	// a division by zero fails
	CHECK(evalOr("n / 0", 3, 9) == 9);
	CHECK(evalOr("n % (n - n)", 3, 9) == 9);
	// the expression ends at a ';'
	CHECK(compiles("n != 1; garbage"));

	// nothing outside the subset
	const char* bad[] = { "", " ", "n +", "foo", "n = 1", "nn", "n1", "n & 1", "n | 1", "(n", "n)", "1 ? 2", "1 ? 2 :",
		"-1", "~n", "n << 1", "system(1)", "n != 1 n", "n++", "1.5", "0x10", "99999999999999999999999999",
		"n ? : 1", "()", "n,1", "\"n\"", "n == 1 ?? 0" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		if (compiles(bad[i])) fprintf(stderr, "compiled: %s\n", bad[i]);
		CHECK(!compiles(bad[i]));
	}
	// nesting and size are bounded
	char deep[512];
	size_t k = 0;
	for (int i = 0; i < 100; i++) deep[k++] = '(';
	deep[k++] = 'n';
	for (int i = 0; i < 100; i++) deep[k++] = ')';
	deep[k] = '\0';
	CHECK(!compiles(deep));
	char wide[2048] = "n";
	for (int i = 0; i < 200; i++) strcat(wide, "+1");
	CHECK(!compiles(wide));
	char nots[512];
	k = 0;
	for (int i = 0; i < 100; i++) nots[k++] = '!';
	nots[k++] = 'n';
	nots[k] = '\0';
	CHECK(!compiles(nots));
	CHECK(!compiles(NULL));

	// headers
	PoPlural rule;
	int nplurals = 0;
	char why[120];
	CHECK(Po_pluralForms("Language: ru\nPlural-Forms: nplurals=3; plural=" RU ";\n", &nplurals, &rule, why, sizeof(why)) == 0);
	CHECK(nplurals == 3);
	unsigned long got;
	CHECK(PoPlural_eval(&rule, 22, &got) == 0 && got == 1);
	CHECK(Po_pluralForms("plural-forms:plural=n>1;nplurals=2", &nplurals, &rule, why, sizeof(why)) == 0);
	CHECK(nplurals == 2);
	CHECK(Po_pluralForms("Language: es\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK_STR(why, "it has no Plural-Forms");
	CHECK(Po_pluralForms("Plural-Forms: nplurals=0; plural=0;\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK(Po_pluralForms("Plural-Forms: nplurals=11; plural=n;\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK(Po_pluralForms("Plural-Forms: nplurals=99999999999999; plural=n;\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK(Po_pluralForms("Plural-Forms: nplurals=2; plural=n = 1;\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK_STR(why, "its plural expression isn't one gettext allows");
	CHECK(Po_pluralForms("Plural-Forms: nplurals=2;\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK(Po_pluralForms("Plural-Forms: plural=n;\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK(Po_pluralForms("Plural-Forms: nplurals=2; plural=n; exec=1;\n", &nplurals, &rule, why, sizeof(why)) < 0);
	CHECK(Po_pluralForms(NULL, &nplurals, &rule, why, sizeof(why)) < 0);
}

#define HEADER(forms) \
	"msgid \"\"\n" \
	"msgstr \"\"\n" \
	"\"Content-Type: text/plain; charset=UTF-8\\n\"\n" \
	"\"Plural-Forms: " forms "\\n\"\n\n"

static void testParse(void) {
	char err[256];
	const char* text =
		"# a comment\n"
		HEADER("nplurals=3; plural=" RU ";")
		"#: launcher/x.c\n"
		"msgid \"OPEN\"\n"
		"msgstr \"ОТКРЫТЬ\"\n"
		"\n"
		"msgid \"\"\n"
		"\"two \"\n"
		"\"lines\"\n"
		"msgstr \"\"\n"
		"\"dos \"\n"
		"\"líneas\"\n"
		"\n"
		"#, fuzzy\n"
		"msgid \"Fuzzy\"\n"
		"msgstr \"skipped\"\n"
		"\n"
		"#, c-format, fuzzy\n"
		"msgid \"Fuzzy too\"\n"
		"msgstr \"skipped\"\n"
		"\n"
		"#~ msgid \"Old\"\n"
		"#~ msgstr \"obsolete\"\n"
		"\n"
		"msgid \"Untranslated\"\n"
		"msgstr \"\"\n"
		"\n"
		"msgctxt \"hello\"\n"
		"msgid \"OPEN\"\n"
		"msgstr \"Открыто\"\n"
		"\n"
		"msgctxt \"\"\n"
		"msgid \"OPEN\"\n"
		"msgstr \"empty context\"\n"
		"\n"
		"msgid \"Escapes\"\n"
		"msgstr \"a\\nb\\tc\\\"d\\\\e\\x41\\101\\'\\?\"\n"
		"\n"
		"#, c-format\n"
		"msgctxt \"hello\"\n"
		"msgid \"%d game\"\n"
		"msgid_plural \"%d games\"\n"
		"msgstr[0] \"%d игра\"\n"
		"msgstr[1] \"%d игры\"\n"
		"msgstr[2] \"%d игр\"\n"
		"\n"
		"msgid \"%d dir\"\n"
		"msgid_plural \"%d dirs\"\n"
		"msgstr[0] \"%d папка\"\n"
		"msgstr[1] \"\"\n"
		"msgstr[2] \"%d папок\"\n"
		"\n"
		"   msgid    \"Spaced\"   \n"
		"\tmsgstr\t\"ok\"\t\n";
	PoCatalog* c = parse(text, err, sizeof(err));
	CHECK(c != NULL);
	if (!c) {
		fprintf(stderr, "%s\n", err);
		return;
	}
	CHECK(!c->english_rule);
	CHECK(c->nplurals == 3);
	CHECK_STR(Po_find(c, NULL, "OPEN"), "ОТКРЫТЬ");
	CHECK_STR(Po_find(c, "hello", "OPEN"), "Открыто");
	CHECK_STR(Po_find(c, "", "OPEN"), "empty context");
	CHECK(Po_find(c, "other", "OPEN") == NULL);
	CHECK_STR(Po_find(c, NULL, "two lines"), "dos líneas");
	CHECK(Po_find(c, NULL, "Fuzzy") == NULL);
	CHECK(Po_find(c, NULL, "Fuzzy too") == NULL);
	CHECK(Po_find(c, NULL, "Old") == NULL);
	CHECK(Po_find(c, NULL, "Untranslated") == NULL);
	CHECK(Po_find(c, NULL, "") == NULL); // the header isn't a translation
	CHECK_STR(Po_find(c, NULL, "Escapes"), "a\nb\tc\"d\\eAA'?");
	CHECK_STR(Po_find(c, NULL, "Spaced"), "ok");
	CHECK_STR(Po_findPlural(c, "hello", "%d game", 1), "%d игра");
	CHECK_STR(Po_findPlural(c, "hello", "%d game", 3), "%d игры");
	CHECK_STR(Po_findPlural(c, "hello", "%d game", 5), "%d игр");
	CHECK_STR(Po_findPlural(c, "hello", "%d game", 21), "%d игра");
	CHECK_STR(Po_find(c, "hello", "%d game"), "%d игра");
	CHECK(Po_findPlural(c, NULL, "%d game", 1) == NULL);
	// an empty form is untranslated
	CHECK(Po_findPlural(c, NULL, "%d dir", 2) == NULL);
	CHECK_STR(Po_findPlural(c, NULL, "%d dir", 5), "%d папок");
	// a string with no plural answers any n
	CHECK_STR(Po_findPlural(c, NULL, "OPEN", 5), "ОТКРЫТЬ");
	CHECK(c->count == 8);
	Po_free(c);

	// CRLF, a BOM, a fuzzy header (a .pot's) still giving Plural-Forms, and no trailing newline
	c = parse("\xEF\xBB\xBF#, fuzzy\r\nmsgid \"\"\r\nmsgstr \"Plural-Forms: nplurals=2; plural=n>1;\\n\"\r\n\r\nmsgid \"a\"\r\nmsgstr \"b\"", err, sizeof(err));
	CHECK(c != NULL);
	if (c) {
		CHECK_STR(Po_find(c, NULL, "a"), "b");
		CHECK(!c->english_rule && c->nplurals == 2);
		Po_free(c);
	}

	// no header, a bad Plural-Forms, and rules that fail pick as English
	const char* forms[] = { NULL, "nplurals=2; plural=n = 1;", "nplurals=2; plural=n/0;", "nplurals=2; plural=5;" };
	for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
		char file[512];
		const char* body = "msgid \"%d x\"\nmsgid_plural \"%d xs\"\nmsgstr[0] \"one\"\nmsgstr[1] \"many\"\n";
		if (forms[i]) snprintf(file, sizeof(file), "msgid \"\"\nmsgstr \"Plural-Forms: %s\\n\"\n\n%s", forms[i], body);
		else snprintf(file, sizeof(file), "%s", body);
		c = parse(file, err, sizeof(err));
		CHECK(c != NULL);
		if (!c) continue;
		CHECK(i < 2 ? c->english_rule && c->note[0] : !c->english_rule);
		CHECK_STR(Po_findPlural(c, NULL, "%d x", 1), "one");
		CHECK_STR(Po_findPlural(c, NULL, "%d x", 0), "many");
		CHECK_STR(Po_findPlural(c, NULL, "%d x", 7), "many");
		Po_free(c);
	}
	c = parse("msgid \"a\"\nmsgstr \"b\"\n", err, sizeof(err));
	CHECK(c && !strcmp(c->note, "there's no header, so plurals use English's rule"));
	Po_free(c);

	// charsets
	c = parse("msgid \"\"\nmsgstr \"Content-Type: text/plain; charset=CHARSET\\n\"\n", err, sizeof(err));
	CHECK(c != NULL);
	Po_free(c);
	c = parse("msgid \"\"\nmsgstr \"Content-Type: text/plain; charset=utf-8\\n\"\n", err, sizeof(err));
	CHECK(c != NULL);
	Po_free(c);

	// lots of entries
	size_t size = 5000 * 48;
	char* many = malloc(size);
	size_t at = 0;
	for (int i = 0; i < 5000; i++) at += (size_t)snprintf(many + at, size - at, "msgid \"s%i\"\nmsgstr \"t%i\"\n\n", i, i);
	c = Po_parse(many, at, err, sizeof(err));
	CHECK(c && c->count == 5000);
	if (c) {
		CHECK_STR(Po_find(c, NULL, "s0"), "t0");
		CHECK_STR(Po_find(c, NULL, "s4999"), "t4999");
		CHECK(Po_find(c, NULL, "s5000") == NULL);
		Po_free(c);
	}
	free(many);
}

static void testMalformed(void) {
	CHECK(refusedAt("msgid \"a\nmsgstr \"b\"\n", 1));                       // no closing quote
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"b\" x\n", 2));                   // text after the string
	CHECK(refusedAt("msgid \"a\"\nmsgstring \"b\"\n", 2));                  // unknown keyword
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"\\q\"\n", 2));                   // unknown escape
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"\\0\"\n", 2));                   // a NUL
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"\\x\"\n", 2));                   // \x with no digits
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"\\777\"\n", 2));                 // past \377
	CHECK(refusedAt("msgstr \"b\"\n", 1));                                  // msgstr first
	CHECK(refusedAt("msgid \"a\"\nmsgid_plural \"as\"\nmsgstr[1] \"x\"\n", 3)); // out of order
	CHECK(refusedAt("msgid \"a\"\nmsgstr[0] \"x\"\n", 2));                  // no msgid_plural
	CHECK(refusedAt("msgid \"a\"\nmsgid_plural \"as\"\nmsgstr \"x\"\n", 3)); // plain msgstr on a plural
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"x\"\nmsgstr \"y\"\n", 3));       // a second msgstr
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"x\"\n\nmsgid \"a\"\nmsgstr \"y\"\n", 4)); // twice
	CHECK(refusedAt("msgid \"\"\nmsgstr \"\"\n\nmsgid \"\"\nmsgstr \"\"\n", 4)); // a second header
	CHECK(refusedAt("msgid \"a\"\n", 1));                                   // no msgstr at the end
	CHECK(refusedAt("msgid \"a\"\nmsgid \"b\"\nmsgstr \"x\"\n", 2));        // no msgstr in between
	CHECK(refusedAt("msgctxt \"c\"\nmsgctxt \"d\"\nmsgid \"a\"\nmsgstr \"x\"\n", 2));
	CHECK(refusedAt("msgctxt \"c\"\n", 1));                                 // msgctxt alone
	CHECK(refusedAt("\"floating\"\n", 1));                                  // no keyword
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"x\"\n\n\"floating\"\n", 4));
	CHECK(refusedAt("msgid \"a\"\nmsgstr[0 \"x\"\n", 2));
	CHECK(refusedAt("msgid \"a\"\nmsgstr[] \"x\"\n", 2));
	CHECK(refusedAt("msgid \"a\"\nmsgid_plural \"b\"\nmsgstr[0] \"0\"\nmsgstr[1] \"1\"\nmsgstr[2] \"2\"\nmsgstr[3] \"3\"\nmsgstr[4] \"4\"\n"
		"msgstr[5] \"5\"\nmsgstr[6] \"6\"\nmsgstr[7] \"7\"\nmsgstr[8] \"8\"\nmsgstr[9] \"9\"\nmsgstr[10] \"10\"\n", 13));
	CHECK(refusedAt("msgid \"a\"\nmsgstr x\n", 2));                         // no string
	CHECK(refusedAt("msgid\n", 1));
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"ok\"\n\nmsgid \"b\"\nmsgstr \"\xC3\x28\"\n", 5)); // not UTF-8
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"\\xff\"\n", 1));                 // an escape that isn't UTF-8
	CHECK(refusedAt("msgid \"\"\nmsgstr \"Content-Type: text/plain; charset=ISO-8859-1\\n\"\n", 1));
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"\xED\xA0\x80\"\n", 2));          // a surrogate
	CHECK(refusedAt("msgid \"a\"\nmsgstr \"\xC0\x80\"\n", 2));              // overlong

	char err[256];
	// a NUL in the file
	CHECK(Po_parse("msgid \"a\"\0msgstr \"b\"\n", 20, err, sizeof(err)) == NULL);
	CHECK(Po_parse(NULL, 0, err, sizeof(err)) == NULL);
	// a string over the limit
	size_t size = PO_MAX_STRING + 64;
	char* big = malloc(size);
	memcpy(big, "msgid \"a\"\nmsgstr \"", 18);
	memset(big + 18, 'x', PO_MAX_STRING + 1);
	memcpy(big + 18 + PO_MAX_STRING + 1, "\"\n", 3);
	CHECK(Po_parse(big, strlen(big), err, sizeof(err)) == NULL);
	free(big);
	// an empty file is a catalog with nothing in it
	PoCatalog* c = Po_parse("", 0, err, sizeof(err));
	CHECK(c && c->count == 0 && Po_find(c, NULL, "a") == NULL);
	Po_free(c);
	CHECK(Po_find(NULL, NULL, "a") == NULL);
}

static void testNames(void) {
	char key[NM_KEY_MAX];
	CHECK(Nm_key("Super Mario Land (World) (Rev 1).gb", 1, key, sizeof(key)) == 0);
	CHECK_STR(key, "supermarioland");
	CHECK(Nm_key("Super Mario Land", 0, key, sizeof(key)) == 0);
	CHECK_STR(key, "supermarioland");
	CHECK(Nm_key("Legend of Zelda, The - Link's Awakening [!].gbc", 1, key, sizeof(key)) == 0);
	CHECK_STR(key, "legendofzeldathelinksawakening");
	CHECK(Nm_key("The Legend of Zelda: Link's Awakening", 0, key, sizeof(key)) == 0);
	CHECK_STR(key, "legendofzeldalinksawakening");
	CHECK(Nm_key("Zelda, The", 0, key, sizeof(key)) == 0);
	CHECK_STR(key, "zelda");
	CHECK(Nm_key("Mario & Luigi", 0, key, sizeof(key)) == 0);
	CHECK_STR(key, "marioandluigi");
	CHECK(Nm_key("ポケモン (Japan).gb", 1, key, sizeof(key)) == 0);
	CHECK_STR(key, "ポケモン");
	CHECK(Nm_key("Dr. Mario", 0, key, sizeof(key)) == 0); // no extension to strip
	CHECK_STR(key, "drmario");
	CHECK(Nm_key("Dr. Mario (World)", 1, key, sizeof(key)) == 0);
	CHECK_STR(key, "drmario");
	CHECK(Nm_key("(Beta)", 0, key, sizeof(key)) < 0);
	CHECK(Nm_key("...", 0, key, sizeof(key)) < 0);
	CHECK(Nm_key("Game.Boy (USA)", 1, key, sizeof(key)) == 0); // not an extension
	CHECK_STR(key, "gameboy");
	CHECK(Nm_key("abc", 0, key, 3) < 0);

	char tag[16];
	CHECK(Nm_tag("Game Boy (GB)", tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "GB");
	CHECK(Nm_tag("Sega (Japan) (MD)", tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "MD");
	CHECK(Nm_tag("Game Boy", tag, sizeof(tag)) < 0);
	CHECK(Nm_tag("Bad ()", tag, sizeof(tag)) < 0);
	CHECK(Nm_tag("Bad (..)", tag, sizeof(tag)) < 0);
	CHECK(Nm_tag("Bad (a/b)", tag, sizeof(tag)) < 0);
	CHECK(Nm_tag("Bad (open", tag, sizeof(tag)) < 0);

	NmNames names = { 0 };
	CHECK(Nm_namesRead(&names,
		"# English\tLocal\n"
		"Super Mario Land (World)\tスーパーマリオランド\r\n"
		"super mario land\tsecond, ignored\n"
		"Tetris\t  テトリス  \n"
		"no tab here\n"
		"Empty\t\n"
		"\n"
		"Bad\t\xC3\x28\n"
		"(only tags)\tx\n"
		"Extra\tcolumns\tignored") == 3);
	CHECK(Nm_namesRead(&names, "Tetris\tnot this\nKirby\tカービィ\n") == 1);
	CHECK(names.count == 4);
	CHECK_STR(Nm_namesFind(&names, "supermarioland"), "スーパーマリオランド");
	CHECK_STR(Nm_namesFind(&names, "tetris"), "テトリス");
	CHECK_STR(Nm_namesFind(&names, "extra"), "columns");
	CHECK_STR(Nm_namesFind(&names, "kirby"), "カービィ");
	CHECK(Nm_namesFind(&names, "notthere") == NULL);

	const char* files[] = { "Kirby (USA).gb", "Super Mario Land (World).gb", "Tetris (Rev 1).gb", "Unknown.gb", "Tetris.Folder" };
	int dirs[] = { 0, 0, 0, 0, 1 };
	int added = -1;
	char* out = Nm_merge(NULL, files, dirs, 5, &names, &added);
	CHECK(added == 3);
	CHECK_STR(out, "Kirby (USA).gb\tカービィ\nSuper Mario Land (World).gb\tスーパーマリオランド\nTetris (Rev 1).gb\tテトリス\n");
	free(out);
	// a map.txt's own lines stay, including one for a file it renames itself
	out = Nm_merge("Tetris (Rev 1).gb\tMy Tetris\n# note", files, dirs, 5, &names, &added);
	CHECK(added == 2);
	CHECK_STR(out, "Tetris (Rev 1).gb\tMy Tetris\n# note\nKirby (USA).gb\tカービィ\nSuper Mario Land (World).gb\tスーパーマリオランド\n");
	free(out);
	out = Nm_merge("Kirby (USA).gb\tK\nSuper Mario Land (World).gb\tS\nTetris (Rev 1).gb\tT\n", files, dirs, 4, &names, &added);
	CHECK(added == 0);
	CHECK_STR(out, "Kirby (USA).gb\tK\nSuper Mario Land (World).gb\tS\nTetris (Rev 1).gb\tT\n");
	free(out);
	// a folder keeps its dot; the same file twice is added once
	const char* twice[] = { "Tetris.dir", "Tetris.dir", "Tetris" };
	int dir_flags[] = { 1, 1, 0 };
	out = Nm_merge("", twice, dir_flags, 3, &names, &added);
	CHECK(added == 1);
	CHECK_STR(out, "Tetris\tテトリス\n");
	free(out);
	Nm_namesFree(&names);
	CHECK(names.count == 0);
}

///////////////////////////////////////
// the plugin

static char cfg_language[64], cfg_code[64], cfg_folder[256], cfg_button[16], cfg_tile[8], cfg_font[256];
static char logged[16384];
static char toasted[256];
static char asked[512];
static char asked_yes[64];
static void (*answer_yes)(void*) = NULL;
static int reloads = 0;
static int registered = 0;
static int bound = -1;
static int tiles = 0;
static void (*action_run)(void*) = NULL;

static void fakeLog(const char* id, const char* m) {
	(void)id;
	size_t len = strlen(logged);
	snprintf(logged + len, sizeof(logged) - len, "%s\n", m);
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "language") && cfg_language[0]) return cfg_language;
	if (!strcmp(key, "code") && cfg_code[0]) return cfg_code;
	if (!strcmp(key, "folder") && cfg_folder[0]) return cfg_folder;
	if (!strcmp(key, "button") && cfg_button[0]) return cfg_button;
	if (!strcmp(key, "tile") && cfg_tile[0]) return cfg_tile;
	if (!strcmp(key, "font") && cfg_font[0]) return cfg_font;
	return fallback;
}
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)label; (void)available; (void)data;
	if (strcmp(id, MAP_ACTION)) return -1;
	registered++;
	action_run = run;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* action) {
	(void)screen; (void)trigger; (void)action;
	bound = button;
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)data;
	snprintf(asked, sizeof(asked), "%s", message);
	snprintf(asked_yes, sizeof(asked_yes), "%s", yes);
	answer_yes = on_yes;
}
static void fakeReload(void) { reloads++; }
static int fakeQuickTile(const char* id, int row, const char* icon, const char* off, int (*is_on)(void*)) {
	(void)id; (void)row; (void)icon; (void)off; (void)is_on;
	tiles++;
	return 0;
}
// the host's tr, answered by the plugin itself, as the launcher would once it owns the slot
static const char* fakeTr(const char* id, const char* context, const char* text) {
	const char* got = translate(context ? context : id, text);
	return got ? got : text;
}
static const char* fakeTrPlural(const char* id, const char* context, const char* singular, const char* plural, unsigned long n) {
	const char* got = translatePlural(context ? context : id, singular, plural, n);
	return got ? got : (n == 1 ? singular : plural);
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.sdcard_path = CARD,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.confirm = fakeConfirm,
	.reload_lists = fakeReload,
	.quick_tile = fakeQuickTile,
	.tr = fakeTr,
	.tr_plural = fakeTrPlural,
};

static void writeFile(const char* path, const char* text) {
	FILE* f = fopen(path, "wb");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static char* slurp(const char* path) {
	int missing;
	return readFile(path, 1 << 20, NULL, &missing);
}

static void start(const char* language, const char* code_setting) {
	quit();
	logged[0] = toasted[0] = asked[0] = '\0';
	answer_yes = NULL;
	registered = tiles = 0;
	bound = -1;
	snprintf(cfg_language, sizeof(cfg_language), "%s", language ? language : "");
	snprintf(cfg_code, sizeof(cfg_code), "%s", code_setting ? code_setting : "");
	CHECK(init(&fake) == 0);
}

static int saidLine(const char* text) {
	return strstr(logged, text) != NULL;
}

static void testPlugin(void) {
	CHECK(system("rm -rf '" TEST_DIR "'") == 0);
	mkdir(TEST_DIR, 0755);
	mkdir(CARD, 0755);
	mkdir(RES, 0755);
	mkdir(LANGS, 0755);
	writeFile(RES "/xx.po", HEADER("nplurals=3; plural=n==1 ? 0 : n==2 ? 1 : 2;")
		"msgid \"OPEN\"\nmsgstr \"XOPEN\"\n\nmsgid \"BACK\"\nmsgstr \"XBACK\"\n\n"
		"msgctxt \"localization\"\nmsgid \"WRITE\"\nmsgstr \"XWRITE\"\n\n"
		"msgctxt \"localization\"\nmsgid \"Add names to map.txt in %d folder?\"\nmsgid_plural \"Add names to map.txt in %d folders?\"\n"
		"msgstr[0] \"X1 %d?\"\nmsgstr[1] \"X2 %d?\"\nmsgstr[2] \"Xn %d?\"\n");
	writeFile(RES "/pt.po", "msgid \"OPEN\"\nmsgstr \"ABRIR pt\"\n");

	// off by default, and nothing registered
	start(NULL, NULL);
	CHECK(saidLine("language off, so nothing is translated"));
	CHECK(translate(NULL, "OPEN") == NULL);
	CHECK(translatePlural(NULL, "%d game", "%d games", 2) == NULL);
	CHECK(registered == 0);

	// the bundled pack
	start("xx", NULL);
	CHECK(saidLine("bundled pack " RES "/xx.po read, 4 translated"));
	CHECK(saidLine("language xx"));
	CHECK_STR(translate(NULL, "OPEN"), "XOPEN");
	CHECK(translate("someone", "OPEN") == NULL);
	CHECK(translate(NULL, "Missing") == NULL);
	CHECK_STR(translatePlural("localization", "Add names to map.txt in %d folder?", "Add names to map.txt in %d folders?", 2), "X2 %d?");
	CHECK(registered == 1 && bound == -1 && tiles == 0);

	// a user pack comes first, string by string, and adds its own
	writeFile(LANGS "/xx.po", "msgid \"OPEN\"\nmsgstr \"USER OPEN\"\n\nmsgid \"NEW\"\nmsgstr \"USER NEW\"\n");
	start("xx", NULL);
	CHECK(saidLine("user pack " LANGS "/xx.po read, 2 translated"));
	CHECK(saidLine("user pack " LANGS "/xx.po: there's no header, so plurals use English's rule"));
	CHECK_STR(translate(NULL, "OPEN"), "USER OPEN");
	CHECK_STR(translate(NULL, "NEW"), "USER NEW");
	CHECK_STR(translate(NULL, "BACK"), "XBACK");
	CHECK_STR(translatePlural("localization", "Add names to map.txt in %d folder?", "", 7), "Xn %d?");

	// a broken user pack is skipped whole, and the bundled one stays
	writeFile(LANGS "/xx.po", "msgid \"OPEN\"\nmsgstr \"USER OPEN\"\n\nmsgid \"NEW\nmsgstr \"x\"\n");
	start("xx", NULL);
	CHECK(saidLine("user pack " LANGS "/xx.po is refused, line 4: a string with no closing quote"));
	CHECK_STR(translate(NULL, "OPEN"), "XOPEN");
	CHECK(translate(NULL, "NEW") == NULL);
	unlink(LANGS "/xx.po");

	// a region falls back to its language, and other takes the code setting
	start("pt_BR", NULL);
	CHECK_STR(translate(NULL, "OPEN"), "ABRIR pt");
	start("other", "pt-PT");
	CHECK_STR(translate(NULL, "OPEN"), "ABRIR pt");
	start("other", NULL);
	CHECK(saidLine("language off"));
	start("other", "../../etc/passwd");
	CHECK(saidLine("isn't a language code"));
	CHECK(translate(NULL, "OPEN") == NULL);
	start("zz", NULL);
	CHECK(saidLine("no pack for zz in " LANGS " or " RES ", so nothing is translated"));
	CHECK(translate(NULL, "OPEN") == NULL);
	CHECK(registered == 1); // the map.txt writer still works with names lists alone

	// a pack that's a folder
	mkdir(LANGS "/qq.po", 0755);
	start("qq", NULL);
	CHECK(saidLine("isn't a regular file"));

	// the fallback font: none by default, the setting's on or off the
	// language, or else one beside the language's pack
	start("xx", NULL);
	CHECK(fallbackFont() == NULL);
	snprintf(cfg_font, sizeof(cfg_font), "Fonts/cjk.ttf");
	start(NULL, NULL);
	CHECK_STR(fallbackFont(), CARD "/Fonts/cjk.ttf");
	CHECK(saidLine("fallback font " CARD "/Fonts/cjk.ttf, from the font setting"));
	snprintf(cfg_font, sizeof(cfg_font), "/elsewhere/cjk.otf");
	start("xx", NULL);
	CHECK_STR(fallbackFont(), "/elsewhere/cjk.otf");
	cfg_font[0] = '\0';
	writeFile(LANGS "/xx.ttf", "");
	start("xx", NULL);
	CHECK_STR(fallbackFont(), LANGS "/xx.ttf");
	CHECK(saidLine("fallback font " LANGS "/xx.ttf, beside the language's pack"));
	start(NULL, NULL);
	CHECK(fallbackFont() == NULL);
	writeFile(LANGS "/pt.otf", "");
	start("pt_BR", NULL);
	CHECK_STR(fallbackFont(), LANGS "/pt.otf");
	writeFile(LANGS "/pt_BR.ttf", "");
	start("pt_BR", NULL);
	CHECK_STR(fallbackFont(), LANGS "/pt_BR.ttf");
	mkdir(LANGS "/yy.ttf", 0755);
	start("yy", NULL);
	CHECK(fallbackFont() == NULL);
	unlink(LANGS "/xx.ttf");
	CHECK(plugin.fallback_font == fallbackFont);

	// the button and the tile
	snprintf(cfg_button, sizeof(cfg_button), "L2");
	snprintf(cfg_tile, sizeof(cfg_tile), "on");
	start("xx", NULL);
	CHECK(bound == MOSAIC_BUTTON_L2 && tiles == 1);
	cfg_button[0] = cfg_tile[0] = '\0';

	// map.txt: Game Boy has no map.txt, Game Boy Color has one of the user's,
	// GBA has no names list, and SNES's map.txt is a link, left alone
	mkdir(CARD "/Roms", 0755);
	mkdir(CARD "/Roms/Game Boy (GB)", 0755);
	mkdir(CARD "/Roms/Game Boy Color (GBC)", 0755);
	mkdir(CARD "/Roms/Game Boy Advance (GBA)", 0755);
	mkdir(CARD "/Roms/Super Nintendo (SFC)", 0755);
	writeFile(CARD "/Roms/Game Boy (GB)/Tetris (World).gb", "");
	writeFile(CARD "/Roms/Game Boy (GB)/Unknown.gb", "");
	writeFile(CARD "/Roms/Game Boy (GB)/.hidden.gb", "");
	writeFile(CARD "/Roms/Game Boy Color (GBC)/Zelda, The (USA).gbc", "");
	writeFile(CARD "/Roms/Game Boy Color (GBC)/Wario Land 3.gbc", "");
	writeFile(CARD "/Roms/Game Boy Color (GBC)/map.txt", "Wario Land 3.gbc\tMy Wario\n");
	writeFile(CARD "/Roms/Game Boy Advance (GBA)/Kirby.gba", "");
	writeFile(CARD "/Roms/Super Nintendo (SFC)/Mario.sfc", "");
	writeFile(TEST_DIR "/elsewhere.txt", "keep\n");
	CHECK(symlink(TEST_DIR "/elsewhere.txt", CARD "/Roms/Super Nintendo (SFC)/map.txt") == 0);

	// no names lists yet
	start("xx", NULL);
	action_run(NULL);
	CHECK_STR(toasted, "No names lists for this language");
	CHECK(asked[0] == '\0');

	mkdir(LANGS "/xx", 0755);
	writeFile(LANGS "/xx/GB.txt", "Tetris\tテトリス\n");
	writeFile(LANGS "/xx/GBC.txt", "The Zelda\tゼルダ\nWario Land 3\tワリオランド3\n");
	writeFile(LANGS "/xx/SFC.txt", "Mario\tマリオ\n");
	action_run(NULL);
	CHECK_STR(asked, "X2 2?\nLines already in a map.txt are kept.");
	CHECK_STR(asked_yes, "XWRITE");
	CHECK(saidLine("Super Nintendo (SFC)/map.txt isn't a regular file, so it's left alone"));
	// nothing is written before the answer
	struct stat st;
	CHECK(stat(CARD "/Roms/Game Boy (GB)/map.txt", &st) != 0);
	char* gbc = slurp(CARD "/Roms/Game Boy Color (GBC)/map.txt");
	CHECK(gbc && !strcmp(gbc, "Wario Land 3.gbc\tMy Wario\n"));
	free(gbc);

	// a run that's cancelled leaves them; the next one asks again
	action_run(NULL);
	CHECK(answer_yes != NULL);
	answer_yes(NULL);
	char* gb = slurp(CARD "/Roms/Game Boy (GB)/map.txt");
	CHECK(gb && !strcmp(gb, "Tetris (World).gb\tテトリス\n"));
	free(gb);
	gbc = slurp(CARD "/Roms/Game Boy Color (GBC)/map.txt");
	CHECK(gbc && !strcmp(gbc, "Wario Land 3.gbc\tMy Wario\nZelda, The (USA).gbc\tゼルダ\n"));
	free(gbc);
	char* link = slurp(TEST_DIR "/elsewhere.txt");
	CHECK(link && !strcmp(link, "keep\n"));
	free(link);
	CHECK(stat(CARD "/Roms/Game Boy Advance (GBA)/map.txt", &st) != 0);
	CHECK(stat(CARD "/Roms/Game Boy (GB)/map.txt.tmp", &st) != 0);
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Added names to map.txt in 2 folders");

	// with everything named, there's nothing to ask
	asked[0] = '\0';
	action_run(NULL);
	CHECK(asked[0] == '\0');
	CHECK_STR(toasted, "No names to add to map.txt");

	// a new folder alone doesn't mention kept lines
	unlink(CARD "/Roms/Game Boy (GB)/map.txt");
	unlink(CARD "/Roms/Game Boy Color (GBC)/map.txt");
	writeFile(CARD "/Roms/Game Boy Color (GBC)/map.txt", "Wario Land 3.gbc\tMy Wario\nZelda, The (USA).gbc\tZ\n");
	action_run(NULL);
	CHECK_STR(asked, "X1 1?");
	quit();
}

// The bundled packs read, and every string they translate is the
// launcher's (in po/launcher.pot) or the plugin's own.
static void testBundled(void) {
	char err[256];
	int missing;
	size_t len = 0;
	char* es = readFile(REPO_DIR "/plugins/localization/res/es.po", PACK_MAX, &len, &missing);
	CHECK(es != NULL);
	if (!es) return;
	PoCatalog* pack = Po_parse(es, len, err, sizeof(err));
	free(es);
	CHECK(pack != NULL);
	if (!pack) {
		fprintf(stderr, "es.po: %s\n", err);
		return;
	}
	CHECK(!pack->english_rule && pack->nplurals == 2);

	// the template, with every msgstr filled in so its entries are kept
	char* pot = readFile(REPO_DIR "/po/launcher.pot", PACK_MAX, &len, &missing);
	CHECK(pot != NULL);
	if (!pot) {
		Po_free(pack);
		return;
	}
	char* filled = malloc(len * 2 + 1);
	size_t at = 0;
	for (const char* line = pot; *line; ) {
		const char* end = strchr(line, '\n');
		size_t n = end ? (size_t)(end - line + 1) : strlen(line);
		if (!strncmp(line, "msgstr \"\"\n", 10)) {
			memcpy(filled + at, "msgstr \"x\"\n", 11);
			at += 11;
		}
		else {
			memcpy(filled + at, line, n);
			at += n;
		}
		line += n;
	}
	free(pot);
	PoCatalog* launcher = Po_parse(filled, at, err, sizeof(err));
	free(filled);
	CHECK(launcher != NULL);
	if (!launcher) fprintf(stderr, "launcher.pot: %s\n", err);

	static const char* const own[] = {
		"Write map.txt names", "WRITE", "Lines already in a map.txt are kept.", "No names lists for this language",
		"No names to add to map.txt", "Add names to map.txt in %d folder?", "Added names to map.txt in %d folder",
	};
	for (int i = 0; launcher && i < pack->count; i++) {
		const PoEntry* e = &pack->entries[i];
		if (!e->context) {
			if (!Po_find(launcher, NULL, e->id)) fprintf(stderr, "es.po: \"%s\" isn't in launcher.pot\n", e->id);
			CHECK(Po_find(launcher, NULL, e->id) != NULL);
			continue;
		}
		CHECK_STR(e->context, ID);
		int known = 0;
		for (size_t j = 0; j < sizeof(own) / sizeof(own[0]); j++) known |= !strcmp(own[j], e->id);
		if (!known) fprintf(stderr, "es.po: \"%s\" isn't one of the plugin's strings\n", e->id);
		CHECK(known);
	}
	for (size_t j = 0; j < sizeof(own) / sizeof(own[0]); j++) CHECK(Po_find(pack, ID, own[j]) != NULL);
	CHECK_STR(Po_findPlural(pack, ID, "Add names to map.txt in %d folder?", 1), "¿Añadir nombres a map.txt en %d carpeta?");
	CHECK_STR(Po_findPlural(pack, ID, "Add names to map.txt in %d folder?", 0), "¿Añadir nombres a map.txt en %d carpetas?");
	Po_free(launcher);
	Po_free(pack);
}

int main(void) {
	testPlural();
	testParse();
	testMalformed();
	testNames();
	testPlugin();
	testBundled();
	CHECK_DONE();
}
