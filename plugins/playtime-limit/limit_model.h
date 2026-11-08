#ifndef LIMIT_MODEL_H
#define LIMIT_MODEL_H

#include <stddef.h>
#include <time.h>

// The play time limit's sums: time left today, extra time granted for a
// day, the warning threshold and the header widget's text. No SDL and no
// host here, so the host tests can check it on its own.

#define PL_DAY_LEN 11 // "YYYY-MM-DD" and its NUL
#define PL_CODE_LEN 64
#define PL_MINUTES_MAX (24 * 60)

// Where today stands, for refusing launches and drawing the widget.
enum PlState {
	PL_OFF,     // no daily limit set
	PL_UNKNOWN, // a limit, but the play history can't be read
	PL_OK,      // time left, more than the warning threshold
	PL_WARN,    // time left, at or under the warning threshold
	PL_UP,      // no time left
};

// Minutes from a setting: a whole number from 1 to PL_MINUTES_MAX, with
// spaces around it allowed, or fallback for anything else, such as "off",
// "" or NULL.
int PL_parseMinutes(const char* setting, int fallback);

// The local day local falls on, as "YYYY-MM-DD".
void PL_dayKey(const struct tm* local, char out[PL_DAY_LEN]);

// The minutes granted for today: grant_minutes when grant_day is today,
// and 0 for any other day, so a grant runs out at midnight.
int PL_grantToday(const char* grant_day, const char* grant_minutes, const char* today);

// Today's grant after extra more minutes, starting from 0 when the last
// grant was for another day, up to PL_MINUTES_MAX.
int PL_addGrant(const char* grant_day, const char* grant_minutes, const char* today, int extra);

// Seconds left today with a limit and a grant in minutes, after played
// seconds, never below 0. -1 with no limit (0 or less) or when played is
// unknown (below 0).
long long PL_secondsLeft(int limit, int grant, long long played);

// An enum PlState for a limit in minutes, the seconds played (below 0
// for unknown), the seconds left from PL_secondsLeft and the warning
// threshold in minutes (0 for none).
int PL_state(int limit, long long played, long long left, int warn);

// Seconds left as the widget shows them: whole minutes, rounded up, so
// it only shows 0 once the time is up.
long long PL_shownMinutes(long long left);

// The widget's text for seconds left: "45m left", "1h 05m left", or
// "Time's up" at 0.
void PL_format(long long left, char* out, size_t size);

// 1 when typed is the code, both with spaces, tabs and line breaks
// trimmed. An empty code never matches, and neither does a NULL typed
// (the keyboard was cancelled).
int PL_codeMatches(const char* code, const char* typed);

#endif
