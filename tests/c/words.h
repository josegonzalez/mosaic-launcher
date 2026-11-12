#ifndef WORDS_H
#define WORDS_H

// A stand-in for a plugin's tr() and trPlural() helpers over a strings
// slot's owner, for checking that a model builds its text from the words
// it's given. fake_words holds pairs (English, translation) and
// fake_plurals triples (English singular, translated singular, translated
// plural), each ended by NULL; anything else stays English, and plural
// forms follow English's rule, as Spanish's does.

#include <string.h>

static const char* const* fake_words = NULL;
static const char* const* fake_plurals = NULL;

__attribute__((unused)) static const char* fakeTr(const char* text) {
	for (const char* const* w = fake_words; w && w[0]; w += 2) {
		if (!strcmp(w[0], text)) return w[1];
	}
	return text;
}

__attribute__((unused)) static const char* fakePlural(const char* singular, const char* plural, unsigned long n) {
	for (const char* const* w = fake_plurals; w && w[0]; w += 3) {
		if (!strcmp(w[0], singular)) return n == 1 ? w[1] : w[2];
	}
	return n == 1 ? singular : plural;
}

#endif
