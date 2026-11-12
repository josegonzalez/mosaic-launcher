// tr.c and trmodel.c: the strings slot's checks, cache and fallbacks, with a
// fake owner.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "tr.h"
#include "trmodel.h"

static void testConversions(void) {
	char out[16];
	CHECK(Tr_conversions("Hello", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(Tr_conversions("%s done", out, sizeof(out)) == 1);
	CHECK_STR(out, "s");
	CHECK(Tr_conversions("%s %lld%%", out, sizeof(out)) == 2);
	CHECK_STR(out, "sL");
	// flags, widths and precisions take nothing, but * does
	CHECK(Tr_conversions("%-08.3f %+d %#x %5s %.*s", out, sizeof(out)) == 6);
	CHECK_STR(out, "fiis*s");
	// sizes of integer
	CHECK(Tr_conversions("%hhd %hd %ld %lu %zu %jd %td %Lf %p %c", out, sizeof(out)) == 10);
	CHECK_STR(out, "Hhllzjt" "Fpi");
	// refused: positions, %n, and a % that starts nothing
	CHECK(Tr_conversions("%1$s", out, sizeof(out)) == -1);
	CHECK(Tr_conversions("%n", out, sizeof(out)) == -1);
	CHECK(Tr_conversions("%lln", out, sizeof(out)) == -1);
	CHECK(Tr_conversions("100%", out, sizeof(out)) == -1);
	CHECK(Tr_conversions("%y", out, sizeof(out)) == -1);
	CHECK(Tr_conversions("%Ld", out, sizeof(out)) == -1);
	CHECK(Tr_conversions("%ls", out, sizeof(out)) == -1);
	// too many for out
	char two[2];
	CHECK(Tr_conversions("%d %d", two, sizeof(two)) == -1);
}

static void testUtf8(void) {
	CHECK(Tr_utf8("plain"));
	CHECK(Tr_utf8("Caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80"));
	CHECK(!Tr_utf8("\xc3"));             // cut short
	CHECK(!Tr_utf8("\xe6\x97"));         // cut short
	CHECK(!Tr_utf8("\x80"));             // a stray continuation
	CHECK(!Tr_utf8("\xc0\xaf"));         // overlong
	CHECK(!Tr_utf8("\xe0\x80\xaf"));     // overlong
	CHECK(!Tr_utf8("\xed\xa0\x80"));     // a surrogate
	CHECK(!Tr_utf8("\xf4\x90\x80\x80")); // past U+10FFFF
	CHECK(!Tr_utf8("\xff"));
}

static void testCheck(void) {
	CHECK(Tr_check("Open", NULL, "Abrir") == TR_OK);
	CHECK(Tr_check("Open", NULL, NULL) == TR_NONE);
	CHECK(Tr_check("Open", NULL, "") == TR_EMPTY);
	CHECK(Tr_check("Open", NULL, "\xc3") == TR_UTF8);

	char big[TR_MAX_BYTES + 2];
	memset(big, 'a', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	CHECK(Tr_check("Open", NULL, big) == TR_LONG);
	char line[TR_MAX_LINE + 2];
	memset(line, 'a', sizeof(line) - 1);
	line[sizeof(line) - 1] = '\0';
	CHECK(Tr_check("Open", NULL, line) == TR_LINE);
	line[TR_MAX_LINE] = '\0';
	CHECK(Tr_check("Open", NULL, line) == TR_OK);
	CHECK(Tr_check("Open", NULL, "1\n2\n3\n4\n5\n6\n7\n8") == TR_OK);
	CHECK(Tr_check("Open", NULL, "1\n2\n3\n4\n5\n6\n7\n8\n9") == TR_LINES);

	// formats need the same conversions, in order
	CHECK(Tr_check("%s done", NULL, "%s fertig") == TR_OK);
	CHECK(Tr_check("%s done", NULL, "fertig: %s") == TR_OK);
	CHECK(Tr_check("%s done", NULL, "fertig") == TR_FORMAT);
	CHECK(Tr_check("%s done", NULL, "%s %s") == TR_FORMAT);
	CHECK(Tr_check("%s done", NULL, "%d") == TR_FORMAT);
	CHECK(Tr_check("%s done", NULL, "%n%s") == TR_FORMAT);
	CHECK(Tr_check("%s done", NULL, "%1$s") == TR_FORMAT);
	CHECK(Tr_check("%s done", NULL, "%s 100%") == TR_FORMAT);
	CHECK(Tr_check("%s %lld%%", NULL, "%s : %lld %%") == TR_OK);
	CHECK(Tr_check("%s %lld%%", NULL, "%s %d%%") == TR_FORMAT);
	// %i for %d, and widths, are the same argument
	CHECK(Tr_check("Disc %i", NULL, "Disque %d") == TR_OK);
	CHECK(Tr_check("Disc %i", NULL, "Disque %2d") == TR_OK);
	CHECK(Tr_check("Disc %i", NULL, "Disque %*d") == TR_FORMAT);

	// plain English: a stray % is fine, a conversion isn't
	CHECK(Tr_check("Battery", NULL, "Batterie 100%") == TR_OK);
	CHECK(Tr_check("Battery", NULL, "Batterie %%") == TR_OK);
	CHECK(Tr_check("Battery", NULL, "Batterie %s") == TR_FORMAT);
	CHECK(Tr_check("Battery", NULL, "Batterie %n") == TR_FORMAT);
	CHECK(Tr_check("Battery", NULL, "Batterie %1$s") == TR_FORMAT);

	// a plural's form may match either English form
	CHECK(Tr_check("%d file", "%d files", "%d Datei") == TR_OK);
	CHECK(Tr_check("One file", "%d files", "eine Datei") == TR_OK);
	CHECK(Tr_check("One file", "%d files", "%d Dateien") == TR_OK);
	CHECK(Tr_check("One file", "%d files", "%s Dateien") == TR_FORMAT);
	CHECK(Tr_check("%d file", "%d files", "Dateien") == TR_FORMAT);

	for (int i = TR_OK; i <= TR_UTF8; i++) CHECK(strcmp(Tr_reason(i), "?") != 0);
}

static void testKey(void) {
	char a[64], b[64];
	CHECK(Tr_key(NULL, NULL, "Open", NULL, 0, a, sizeof(a)) == 0);
	CHECK(Tr_key(NULL, "button", "Open", NULL, 0, b, sizeof(b)) == 0);
	CHECK(strcmp(a, b) != 0);
	CHECK(Tr_key(NULL, "", "Open", NULL, 0, b, sizeof(b)) == 0);
	CHECK(strcmp(a, b) != 0);
	CHECK(Tr_key(NULL, NULL, "%d file", "%d files", 1, a, sizeof(a)) == 0);
	CHECK(Tr_key(NULL, NULL, "%d file", "%d files", 2, b, sizeof(b)) == 0);
	CHECK(strcmp(a, b) != 0);
	// a plugin's strings are apart from the launcher's, other plugins' and
	// a context named like the plugin
	char c[64];
	CHECK(Tr_key("hello", NULL, "Open", NULL, 0, a, sizeof(a)) == 0);
	CHECK(Tr_key(NULL, "hello", "Open", NULL, 0, b, sizeof(b)) == 0);
	CHECK(strcmp(a, b) != 0);
	CHECK(Tr_key("other", NULL, "Open", NULL, 0, c, sizeof(c)) == 0);
	CHECK(strcmp(a, c) != 0);
	CHECK(Tr_key("hello", "button", "Open", NULL, 0, c, sizeof(c)) == 0);
	CHECK(strcmp(a, c) != 0 && strcmp(b, c) != 0);
	char small[4];
	CHECK(Tr_key(NULL, NULL, "Open", NULL, 0, small, sizeof(small)) == -1);
	CHECK(Tr_key(NULL, NULL, NULL, NULL, 0, a, sizeof(a)) == -1);

	CHECK_STR(Tr_english("one", "many", 1), "one");
	CHECK_STR(Tr_english("one", "many", 0), "many");
	CHECK_STR(Tr_english("one", "many", 2), "many");
}

static TrCache cache;

static void testCache(void) {
	CHECK(TrCache_find(&cache, "a") == NULL);
	const TrCacheEntry* e = TrCache_put(&cache, "a", "A");
	CHECK(e && !strcmp(e->value, "A"));
	CHECK(TrCache_find(&cache, "a") == e);
	// the first answer stays
	CHECK(TrCache_put(&cache, "a", "B") == e);
	CHECK_STR(e->value, "A");
	e = TrCache_put(&cache, "b", NULL);
	CHECK(e && e->value == NULL);
	CHECK(TrCache_find(&cache, "b") == e);

	char key[16];
	for (int i = cache.count; i < TR_CACHE_MAX; i++) {
		snprintf(key, sizeof(key), "k%i", i);
		CHECK(TrCache_put(&cache, key, key) != NULL);
	}
	CHECK(cache.count == TR_CACHE_MAX);
	CHECK(TrCache_put(&cache, "one more", "x") == NULL);
	CHECK(TrCache_find(&cache, "k100") != NULL);
	TrCache_clear(&cache);
	CHECK(cache.count == 0);
	CHECK(TrCache_find(&cache, "a") == NULL);
}

///////////////////////////////////////
// TR with a fake owner

static int asked = 0;
static int plural_asked = 0;
static char logged[512];
static int logs = 0;
static const char* inner = NULL; // what TR gave the owner's own ask
static char volatile_answer[32];

static char seen[128]; // the last ask's plugin id and context, "-" for NULL

static void see(const char* plugin_id, const char* context) {
	snprintf(seen, sizeof(seen), "%s %s", plugin_id ? plugin_id : "-", context ? context : "-");
}

static const char* translate(const char* plugin_id, const char* context, const char* text) {
	asked++;
	see(plugin_id, context);
	if (plugin_id) {
		if (strcmp(plugin_id, "hello") || strcmp(text, "Open")) return NULL;
		return context ? (!strcmp(context, "button") ? "Knopf" : NULL) : "Öffnen";
	}
	if (!strcmp(text, "Ask myself")) {
		inner = TR("Open");
		return "Mich";
	}
	if (context && !strcmp(context, "button") && !strcmp(text, "Open")) return "Ouvrir";
	if (context && !strcmp(context, "state") && !strcmp(text, "Open")) return "Ouvert";
	if (context) return NULL;
	if (!strcmp(text, "Open")) {
		// the host copies, so this can change after the call
		snprintf(volatile_answer, sizeof(volatile_answer), "%s", "Abrir");
		return volatile_answer;
	}
	if (!strcmp(text, "%s done")) return "%s fertig";
	if (!strcmp(text, "%s failed")) return "%d kaputt";
	if (!strcmp(text, "Empty")) return "";
	if (!strcmp(text, "Bad")) return "\xc3";
	if (!strcmp(text, "%d file")) return "%d Datei";
	if (!strcmp(text, "%d files")) return "%d Dateien";
	return NULL;
}

// Russian-style: one, few, many.
static const char* translatePlural(const char* plugin_id, const char* context, const char* singular, const char* plural,
		unsigned long n) {
	(void)plural;
	plural_asked++;
	see(plugin_id, context);
	if (plugin_id && !strcmp(singular, "%d file")) return "%d plugin";
	if (strcmp(singular, "%d file")) return NULL;
	if (n % 10 == 1 && n % 100 != 11) return "%d fail";
	if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20)) return "%d faila";
	return "%d failov";
}

static void logLine(const char* message) {
	snprintf(logged, sizeof(logged), "%s", message);
	logs++;
}

static void testNoOwner(void) {
	Tr_setOwner(NULL);
	const char* open = "Open";
	CHECK(TR(open) == open);
	CHECK(TRC("button", open) == open);
	CHECK(TR(NULL) == NULL);
	const char* one = "%d file";
	const char* many = "%d files";
	CHECK(TRN(one, many, 1) == one);
	CHECK(TRN(one, many, 0) == many);
	CHECK(TRN(one, many, 2) == many);
	CHECK(TRCN("x", one, many, 1) == one);
	CHECK(Tr_plugin("hello", NULL, open) == open);
	CHECK(Tr_plugin("hello", "button", open) == open);
	CHECK(Tr_plugin("hello", NULL, NULL) == NULL);
	CHECK(Tr_pluginPlural("hello", NULL, one, many, 1) == one);
	CHECK(Tr_pluginPlural("hello", "x", one, many, 2) == many);
	CHECK(!Tr_active());
	// an owner without translate is no owner
	TrOwner none = { .translate = NULL, .id = "none" };
	Tr_setOwner(&none);
	CHECK(!Tr_active());
	CHECK(TR(open) == open);
}

static void testOwner(void) {
	TrOwner owner = { .translate = translate, .translate_plural = translatePlural, .id = "fake", .log = logLine };
	Tr_setOwner(&owner);
	CHECK(Tr_active());
	asked = 0;
	CHECK_STR(TR("Open"), "Abrir");
	CHECK(asked == 1);
	// asked once, and the host's copy outlives the owner's buffer
	strcpy(volatile_answer, "changed");
	CHECK_STR(TR("Open"), "Abrir");
	CHECK(asked == 1);
	char* first = TR("Open");
	CHECK(TR("Open") == first);

	// unknown text and NULL answers are English, and asked once
	const char* unknown = "Unknown";
	CHECK(TR(unknown) == unknown);
	CHECK(TR(unknown) == unknown);
	CHECK(asked == 2);

	// contexts tell the same English apart
	CHECK_STR(TRC("button", "Open"), "Ouvrir");
	CHECK_STR(TRC("state", "Open"), "Ouvert");
	CHECK_STR(TR("Open"), "Abrir");
	CHECK(TRC("other", "Open")[0] == 'O');
	CHECK_STR(TRC("other", "Open"), "Open");

	// formats: the same conversions are used, others refused and logged
	CHECK_STR(TR("%s done"), "%s fertig");
	logs = 0;
	CHECK_STR(TR("%s failed"), "%s failed");
	CHECK(logs == 1);
	CHECK(strstr(logged, "fake") && strstr(logged, "%s failed") && strstr(logged, "printf"));
	CHECK_STR(TR("%s failed"), "%s failed");
	CHECK(logs == 1);
	CHECK_STR(TR("Empty"), "Empty");

	// plurals by the owner's rules
	plural_asked = 0;
	CHECK_STR(TRN("%d file", "%d files", 1), "%d fail");
	CHECK_STR(TRN("%d file", "%d files", 3), "%d faila");
	CHECK_STR(TRN("%d file", "%d files", 5), "%d failov");
	CHECK_STR(TRN("%d file", "%d files", 21), "%d fail");
	CHECK_STR(TRN("%d file", "%d files", 3), "%d faila");
	CHECK(plural_asked == 4);
	// and unknown ones by English's
	CHECK_STR(TRN("%d dir", "%d dirs", 1), "%d dir");
	CHECK_STR(TRN("%d dir", "%d dirs", 2), "%d dirs");

	// the owner asking TR itself gets English
	inner = NULL;
	CHECK_STR(TR("Ask myself"), "Mich");
	CHECK(inner && !strcmp(inner, "Open"));
}

static void testPluginText(void) {
	TrOwner owner = { .translate = translate, .translate_plural = translatePlural, .id = "fake", .log = logLine };
	Tr_setOwner(&owner);
	// the launcher's own text has no plugin id, in its context or none
	CHECK_STR(TR("Open"), "Abrir");
	CHECK_STR(seen, "- -");
	CHECK_STR(TRC("button", "Open"), "Ouvrir");
	CHECK_STR(seen, "- button");
	// a plugin's has its id, with no context or its own
	CHECK_STR(Tr_plugin("hello", NULL, "Open"), "Öffnen");
	CHECK_STR(seen, "hello -");
	CHECK_STR(Tr_plugin("hello", "button", "Open"), "Knopf");
	CHECK_STR(seen, "hello button");
	// a launcher context named like a plugin isn't that plugin's text
	CHECK_STR(TRC("hello", "Open"), "Open");
	CHECK_STR(seen, "- hello");
	// another plugin's Open is its own, and each is cached apart
	CHECK_STR(Tr_plugin("other", NULL, "Open"), "Open");
	CHECK_STR(seen, "other -");
	asked = 0;
	CHECK_STR(Tr_plugin("hello", NULL, "Open"), "Öffnen");
	CHECK_STR(Tr_plugin("hello", "button", "Open"), "Knopf");
	CHECK_STR(TR("Open"), "Abrir");
	CHECK(asked == 0);
	// plurals too
	CHECK_STR(Tr_pluginPlural("hello", "count", "%d file", "%d files", 2), "%d plugin");
	CHECK_STR(seen, "hello count");
	Tr_setOwner(NULL);
}

static void testNoPlural(void) {
	// without translate_plural, translate is asked for English's form
	TrOwner owner = { .translate = translate, .id = "fake", .log = logLine };
	Tr_setOwner(&owner);
	CHECK_STR(TRN("%d file", "%d files", 1), "%d Datei");
	CHECK_STR(TRN("%d file", "%d files", 0), "%d Dateien");
	CHECK_STR(TRN("%d file", "%d files", 7), "%d Dateien");
}

static void testFailure(void) {
	TrOwner owner = { .translate = translate, .translate_plural = translatePlural, .id = "fake", .log = logLine };
	Tr_setOwner(&owner);
	CHECK_STR(TR("Open"), "Abrir");
	logs = 0;
	// invalid UTF-8 turns the slot off: English for everything from then on
	CHECK_STR(TR("Bad"), "Bad");
	CHECK(logs == 1);
	CHECK(strstr(logged, "UTF-8") && strstr(logged, "restarts"));
	CHECK(!Tr_active());
	asked = 0;
	CHECK_STR(TR("Open"), "Open");
	CHECK_STR(TR("%s done"), "%s done");
	CHECK_STR(TRN("%d file", "%d files", 3), "%d files");
	CHECK(asked == 0);
	// a new owner starts afresh
	Tr_setOwner(&owner);
	CHECK(Tr_active());
	CHECK_STR(TR("Open"), "Abrir");
	Tr_setOwner(NULL);
	CHECK_STR(TR("Open"), "Open");
}

static const char* echo(const char* plugin_id, const char* context, const char* text) {
	(void)plugin_id;
	(void)context;
	return text;
}

static void testFull(void) {
	TrOwner owner = { .translate = echo, .id = "echo", .log = logLine };
	Tr_setOwner(&owner);
	char text[16];
	for (int i = 0; i < TR_CACHE_MAX; i++) {
		snprintf(text, sizeof(text), "t%i", i);
		char* got = TR(text);
		CHECK(got != text && !strcmp(got, text));
	}
	// past the cap, English, logged once
	logs = 0;
	const char* more = "one more";
	CHECK(TR(more) == more);
	CHECK(TR("and another") != NULL);
	CHECK(logs == 1);
	CHECK(strstr(logged, "more strings"));
	Tr_setOwner(NULL);
}

int main(void) {
	testConversions();
	testUtf8();
	testCheck();
	testKey();
	testCache();
	testNoOwner();
	testOwner();
	testPluginText();
	testNoPlural();
	testFailure();
	testFull();
	CHECK_DONE();
}
