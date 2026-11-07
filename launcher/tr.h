#ifndef LAUNCHER_TR_H
#define LAUNCHER_TR_H

// Every piece of fixed UI text goes through TR() where it's drawn, so a
// translation can replace it later. For now it returns its input.
//
// Translate at the draw site, never in data: names like "Recents" also key
// art lookups (Recents@3x.png) and must stay as they are. Button identifiers
// ("A", "MENU") and game names are not UI text.

char* TR(const char* text);

#endif
