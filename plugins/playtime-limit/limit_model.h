#ifndef LIMIT_MODEL_H
#define LIMIT_MODEL_H

#include <stddef.h>
#include <time.h>

// The play time limit's sums: today's limit, weekday or weekend, time
// left today, extra time granted for a day, the warning threshold, the
// bedtime window, the budget bar's fill and the header widget's text. No
// SDL and no host here, so the host tests can check it on its own.

#define PL_DAY_LEN 11 // "YYYY-MM-DD" and its NUL
#define PL_CODE_LEN 64
#define PL_MINUTES_MAX (24 * 60)
#define PL_CLOCK_LEN 6 // "HH:MM" and its NUL
// a bedtime unlock further ahead than this is taken as from a clock that
// went back, and doesn't count
#define PL_UNLOCK_MAX (PL_MINUTES_MAX * 60LL)

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

// The weekend limit's setting: "same" (or NULL or "") follows weekday,
// the weekday limit in minutes, and otherwise minutes as
// PL_parseMinutes reads them, with 0 for "off" or anything else.
int PL_weekendLimit(const char* setting, int weekday);

// 1 for Saturday and Sunday, as struct tm's tm_wday counts them (0 is
// Sunday), else 0.
int PL_isWeekend(int wday);

// Today's limit in minutes: weekend on a weekend, else weekday.
int PL_todayLimit(int weekday, int weekend, int wday);

// Minutes after midnight from "HH:MM" (00:00 to 23:59, with spaces around
// it allowed), or fallback for anything else, such as "off", "" or NULL.
int PL_parseClock(const char* setting, int fallback);

// "HH:MM" for minutes after midnight, wrapped into a day.
void PL_formatClock(int minutes, char out[PL_CLOCK_LEN]);

// 1 when minutes after midnight now falls from start up to (not
// including) end, both minutes after midnight. A window whose end comes
// before its start crosses midnight, such as 21:00 to 07:00. One that
// starts and ends at the same minute, or with a start or end below 0
// (off), is empty.
int PL_inWindow(int start, int end, int now);

// 1 while a bedtime unlock until the Unix time until still counts at the
// Unix time now: until is after now, and no more than PL_UNLOCK_MAX
// seconds after it.
int PL_unlocked(long long until, long long now);

// The budget bar's filled width out of width for played seconds against
// a limit and a grant in minutes, from 0 to width. -1 with no limit (0
// or less), an unknown played (below 0) or no width.
int PL_barFill(long long played, int limit, int grant, int width);

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

// The same in other words: words[PL_WORDS_UP] at 0, else
// words[PL_WORDS_MINUTES] with the minutes ("%lldm left") or, from an hour,
// words[PL_WORDS_HOURS] with the hours and minutes ("%lldh %02lldm left").
enum { PL_WORDS_UP, PL_WORDS_MINUTES, PL_WORDS_HOURS, PL_WORDS_COUNT };
void PL_formatIn(long long left, const char* const words[PL_WORDS_COUNT], char* out, size_t size);

// The state's name in the limit.left service's reply: "off", "unknown",
// "ok", "warn" or "up".
const char* PL_stateName(int state);

// The limit.left service's reply: "state=<name>\nlimit=<minutes>\n
// granted=<minutes>\nleft=<seconds>\ntext=<text>\nbedtime=<0 or 1>\n
// bedtime_start=<HH:MM>\nbedtime_end=<HH:MM>", with limit 0, left -1
// and no text while the limit is off, and left -1 and no text while the
// history is unknown. bedtime is 1 while bedtime refuses games, and
// bedtime_start and bedtime_end are empty with a start or end below 0
// (bedtime off). Returns 0, or -1 when it doesn't fit size.
int PL_reply(int state, int limit, int granted, long long left, const char* text, int bedtime, int bedtime_start,
	int bedtime_end, char* out, size_t size);

// 1 when typed is the code, both with spaces, tabs and line breaks
// trimmed. An empty code never matches, and neither does a NULL typed
// (the keyboard was cancelled).
int PL_codeMatches(const char* code, const char* typed);

#endif
