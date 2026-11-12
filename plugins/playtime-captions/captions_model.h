// Play time captions, without the host: the caption's parts (play time,
// last played, sessions, RetroAchievements progress) as text, in local time, fitted into a width by
// trying the compact format and then dropping parts, and where it goes in
// a row. No SDL and no file access, so it's tested on the host.

#ifndef CAPTIONS_MODEL_H
#define CAPTIONS_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until PC_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct PCWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} PCWords;
void PC_setWords(const PCWords* words);

// How the parts read.
enum PCFormat {
	PC_FORMAT_RELATIVE, // "2h 15m · 3 days ago · 4 sessions"
	PC_FORMAT_COMPACT,  // "2h · 3d · 4x"
	PC_FORMAT_DATE,     // "2h 15m · Nov 5 · 4 sessions", the last played day as a date
};

// The parts a caption can have, as bits, shown in this order.
enum PCPart {
	PC_PART_TIME = 1,     // play time in all
	PC_PART_LAST = 2,     // when it was last played
	PC_PART_SESSIONS = 4, // how many sessions
	PC_PART_ACHIEVEMENTS = 8, // RetroAchievements unlocked of the game's total
};

// What the play history says about a game, as the host's play_time,
// last_played and play_count return it, and its RetroAchievements
// progress, as host->achievements fills it in (cheevos_total 0 for none).
typedef struct PCStats {
	long long time;
	long long last;
	int count;
	int cheevos;
	int cheevos_total;
} PCStats;

// What the user picked.
typedef struct PCOptions {
	int parts;  // PC_PART_* bits
	int format; // an enum PCFormat
	int never;  // 1 to say "Never played" on games the history hasn't seen
} PCOptions;

// The format a setting names: "relative", "compact" or "date", or
// PC_FORMAT_RELATIVE for anything else.
int PC_formatOf(const char* name);

// Each part as text. A duration under a minute is "<1m".
void PC_duration(long long seconds, int format, char* out, size_t size);
// when and now are Unix times, compared as days in local time.
void PC_lastPlayed(long long when, long long now, int format, char* out, size_t size);
void PC_sessions(int count, int format, char* out, size_t size);
// "3/12 achievements", or "3/12" compact.
void PC_achievements(int unlocked, int total, int format, char* out, size_t size);
// Local calendar days from when to now: 0 the same day, 1 the day before,
// and 0 when when is after now.
int PC_daysBetween(long long when, long long now);

// The first max_parts of the parts picked, joined with " · ". Returns how
// many parts it wrote. A game with unknown history gets no play parts, and
// one the history hasn't seen gets "Never played" in their place when
// never is set, or none; the achievements part follows either way, when
// it's picked and the game has a total. 0 (writing "") for nothing.
int PC_build(const PCStats* stats, long long now, int parts, int format, int never, int max_parts, char* out,
	size_t size);

// The caption that fits max_width, as measure says how wide text is:
// every part in the picked format, then in the compact format, then the
// same with fewer parts, dropping them from the end. Returns its width, or
// 0 (writing "") when nothing fits or there's nothing to say.
int PC_fit(const PCStats* stats, long long now, const PCOptions* options, int max_width,
	int (*measure)(const char* text, void* ctx), void* ctx, char* out, size_t size);

// Where a caption text_h tall may go in a row: right-aligned in the box
// it writes to out, which ends margin short of rect's right. With a name,
// it's on the name's line, starting reserve after the name's end, which
// keeps room for row-badges' dots there. Without one, it's along the
// bottom, margin above it, and starts margin from the left, below the top
// corner rows (row-badges' corner dots) of the row. Returns 1, or 0 when
// there's no room.
int PC_room(const MosaicRect* rect, const MosaicRect* name, int reserve, int corner, int margin, int text_h,
	MosaicRect* out);

// fg moved toward bg by percent, from 0 (fg) to 100 (bg), as 0xRRGGBB.
int PC_mute(int fg, int bg, int percent);

#endif
