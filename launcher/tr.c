#include <stdio.h>
#include <string.h>
#include "tr.h"
#include "trmodel.h"

static TrOwner owner;
static int owned = 0;
static int failed = 0; // the owner's answer was an error, so English shows until restart
static int asking = 0; // while the owner's translate runs, when its own asks get English
static int full_logged = 0;
static TrCache cache;

static void note(const char* format, const char* a, const char* b) {
	if (!owner.log) return;
	char line[512];
	snprintf(line, sizeof(line), format, owner.id ? owner.id : "?", a, b);
	owner.log(line);
}

void Tr_setOwner(const TrOwner* next) {
	TrCache_clear(&cache);
	failed = 0;
	full_logged = 0;
	owned = next && next->translate;
	if (owned) owner = *next;
	else memset(&owner, 0, sizeof(owner));
}

int Tr_active(void) {
	return owned && !failed;
}

// The translation of text (plural NULL) or of a plural's forms for n, or
// english.
static char* lookup(const char* context, const char* text, const char* plural, unsigned long n, const char* english) {
	if (!Tr_active() || asking) return (char*)english;
	char key[2048];
	if (Tr_key(context, text, plural, n, key, sizeof(key)) < 0) return (char*)english;
	const TrCacheEntry* hit = TrCache_find(&cache, key);
	if (hit) return hit->value ? hit->value : (char*)english;

	asking = 1;
	const char* got;
	if (!plural) got = owner.translate(context, text);
	else if (owner.translate_plural) got = owner.translate_plural(context, text, plural, n);
	else got = owner.translate(context, english);
	asking = 0;

	int check = Tr_check(text, plural, got);
	if (check == TR_UTF8) {
		failed = 1;
		note("plugins: %s translated \"%s\" to text that %s, showing English until the launcher restarts", text, Tr_reason(check));
		return (char*)english;
	}
	if (check != TR_OK && check != TR_NONE) note("plugins: %s's translation of \"%s\" is refused, as %s", text, Tr_reason(check));
	hit = TrCache_put(&cache, key, check == TR_OK ? got : NULL);
	if (!hit) {
		if (!full_logged) note("plugins: %s has more strings than the launcher keeps, so \"%s\" and the rest show in English", text, NULL);
		full_logged = 1;
		return (char*)english;
	}
	return hit->value ? hit->value : (char*)english;
}

char* TR(const char* text) {
	return TRC(NULL, text);
}

char* TRC(const char* context, const char* text) {
	if (!text) return NULL;
	return lookup(context, text, NULL, 0, text);
}

char* TRN(const char* singular, const char* plural, unsigned long n) {
	return TRCN(NULL, singular, plural, n);
}

char* TRCN(const char* context, const char* singular, const char* plural, unsigned long n) {
	const char* english = Tr_english(singular, plural, n);
	if (!singular || !plural) return (char*)english;
	return lookup(context, singular, plural, n, english);
}
