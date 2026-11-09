#ifndef LAUNCHER_TR_H
#define LAUNCHER_TR_H

// Every piece of fixed UI text goes through TR() where it's drawn, so the
// strings slot's owner can translate it. With no owner it returns its
// input.
//
// Translate at the draw site, never in data: names like "Recents" also key
// art lookups (Recents@3x.png) and must stay as they are. Button identifiers
// ("A", "MENU") and game names are not UI text.
//
// The key is the English text itself, as gettext's msgid. TRC adds a
// context (msgctxt) for English that means different things in different
// places, TRN picks a plural's form for a count (English rules without an
// owner: n == 1 picks singular), and TRCN does both. TR_MARK marks English
// kept in data, such as an action's label, for `make pot`; it's translated
// with TR where it's drawn. The results are the cache's copies, or the
// English passed in; use them before the launcher quits. Main thread only.

char* TR(const char* text);
char* TRC(const char* context, const char* text);
char* TRN(const char* singular, const char* plural, unsigned long n);
char* TRCN(const char* context, const char* singular, const char* plural, unsigned long n);
#define TR_MARK(text) (text)

// The strings slot's owner, as the plugin loader hands it over.
typedef struct TrOwner {
	const char* (*translate)(const char* context, const char* text);
	const char* (*translate_plural)(const char* context, const char* singular, const char* plural, unsigned long n);
	const char* id;                   // for logs
	void (*log)(const char* message); // a line for the log, without its newline
} TrOwner;

// Takes owner's hooks from now on, or none for NULL, forgetting every
// cached answer.
void Tr_setOwner(const TrOwner* owner);

// 1 while an owner is translating: there's one and it hasn't failed.
int Tr_active(void);

#endif
