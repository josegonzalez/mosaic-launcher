// Gamification, without the host: local days and weeks, the plugin's own
// log of launches and play by day, the streak, the quests and their
// progress, the state file and the pending launch's line, the text the
// screen shows and its layout. No SDL, so it's tested on the host.

#ifndef GAMIFICATION_MODEL_H
#define GAMIFICATION_MODEL_H

#include <stddef.h>
#include <time.h>
#include "mosaic_plugin.h"

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until GM_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct GMWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} GMWords;
void GM_setWords(const GMWords* words);

#define GM_PATH_MAX 512
// days of launches and play the state keeps
#define GM_LOG_DAYS 64
// different games remembered for today
#define GM_TODAY_GAMES 32
// days asked of the play history, for the streaks
#define GM_HISTORY_DAYS 366
// a launch whose game hasn't been played for this many days revisits it
#define GM_REVISIT_DAYS 30
// a session longer than this is dropped as a clock gone wrong
#define GM_SESSION_MAX (24 * 60 * 60)

///////////////////////////////////////
// days

// The local day t falls on, as days since 1 January 1970.
int GM_dayOf(time_t t);

// When local day day starts.
time_t GM_dayStart(int day);

// 0 for Monday to 6 for Sunday.
int GM_weekday(int day);

// The week day falls in, weeks starting on Monday.
int GM_weekOf(int day);

///////////////////////////////////////
// the state

// One day of the plugin's own log, from launch and return events.
typedef struct GMDay {
	int day;
	long long seconds; // played, from the time between a launch and its return
	int launches;      // games launched
	int games;         // different games launched
	int fresh;         // launches of games never played before
	int revisits;      // launches of games not played for GM_REVISIT_DAYS
} GMDay;

// The quests, in the order the screen lists them.
enum GMQuest {
	GM_Q_PLAY,        // today: play for daily_minutes
	GM_Q_GAMES,       // today: play daily_games different games
	GM_Q_FRESH,       // today: start a game never played before
	GM_Q_REVISIT,     // today: go back to a game not played for GM_REVISIT_DAYS
	GM_Q_WEEK_DAYS,   // this week: play on week_days days
	GM_Q_WEEK_HOURS,  // this week: play for week_hours
	GM_QUESTS,
};

typedef struct GMState {
	GMDay days[GM_LOG_DAYS]; // newest first
	int day_count;
	int games_day;                          // the day games holds
	unsigned int games[GM_TODAY_GAMES];     // hashes of the different games launched on games_day
	int game_count;
	int best_streak;     // the longest streak seen
	int completed;       // quests completed in all
	int done[GM_QUESTS]; // the day or week each quest was last completed in, -1 for never
	int cheered;         // the day the streak was last cheered, -1 for never
} GMState;

void GM_init(GMState* state);

// The log's day, or NULL when it has none.
const GMDay* GM_day(const GMState* state, int day);

// Counts a launch of the game at path on day: fresh when it was never
// played before, revisit when it hadn't been for GM_REVISIT_DAYS.
void GM_launch(GMState* state, int day, const char* path, int fresh, int revisit);

// Adds seconds of play from start, split between the local days it spans.
// Nothing for seconds outside 1 to GM_SESSION_MAX.
void GM_play(GMState* state, time_t start, long long seconds);

// The quest's id in the state file, "play", "games", ...; NULL past the end.
const char* GM_questId(int quest);

// 1 for the quests that run for a week, 0 for the daily ones.
int GM_weekly(int quest);

// Writes the state as text into out. Returns its length, or -1 when it
// doesn't fit.
int GM_format(const GMState* state, char* out, size_t size);

// Reads text written by GM_format into state, after GM_init. Lines it
// doesn't know are skipped. Returns 0.
int GM_parse(GMState* state, const char* text);

///////////////////////////////////////
// the pending launch, kept in a file while the game runs

typedef struct GMLaunch {
	long long started; // wall clock time
	long long mono;    // monotonic clock seconds
	int fresh, revisit;
	char path[GM_PATH_MAX];
} GMLaunch;

// "<started> <mono> <fresh> <revisit> <path>\n". Returns 0, or -1 when it
// doesn't fit.
int GM_formatLaunch(const GMLaunch* launch, char* out, size_t size);
int GM_parseLaunch(const char* line, GMLaunch* out);

// The seconds the launch played for, from the monotonic clock now: -1
// when it went backwards or ran past GM_SESSION_MAX.
long long GM_session(const GMLaunch* launch, long long mono_now);

///////////////////////////////////////
// streaks and quests

// played[i] is the seconds played on the day i days before today (0 is
// today), each the most of the plugin's log and the play history.
// tracker is the play history's, or NULL; negatives are unknown.
void GM_played(const GMState* state, int today, const long long* tracker, int days, long long* played);

// The current streak: days in a row with at least need seconds, up to
// today. Today counts once it has them; until then the streak runs to
// yesterday, so it isn't lost before the day is out.
int GM_streak(const long long* played, int days, long long need);

// The longest run of days in a row with at least need seconds.
int GM_longest(const long long* played, int days, long long need);

// What the quests ask for.
typedef struct GMTargets {
	int daily_minutes;
	int daily_games;
	int week_days;
	int week_hours;
	long long streak_seconds; // for a day to count towards the streak and week_days
} GMTargets;

// A quest's progress.
typedef struct GMProgress {
	long long value, target; // in minutes for the play quests, else counts
	int done;
} GMProgress;

// Every quest's progress on today. played as GM_played gives it.
void GM_progress(const GMState* state, int today, const long long* played, int days, const GMTargets* targets,
	GMProgress out[GM_QUESTS]);

// Marks quests that are done and weren't yet for their day or week as
// completed, counting them, and keeps the best streak at least streak.
// Writes the ones just completed to newly and returns how many.
int GM_settle(GMState* state, int today, const GMProgress* progress, int streak, int newly[GM_QUESTS]);

///////////////////////////////////////
// text

// "Play for 30 minutes today", "Play 3 different games today", "Start a
// game you've never played", "Go back to a game you haven't played in 30
// days", "Play on 5 days this week", "Play for 5 hours this week".
void GM_title(int quest, const GMTargets* targets, char* out, size_t size);

// A quest's right side: "Done", "12 / 30m", "1 / 3", "2h 5m / 5h".
void GM_value(int quest, const GMProgress* progress, char* out, size_t size);

// "0m", "45m", "2h", "2h 5m".
void GM_duration(long long minutes, char* out, size_t size);

// "No streak", "1 day streak", "4 day streak".
void GM_streakText(int streak, char* out, size_t size);

// "Best 7 days · 12 quests done"; "Best 1 day ...", "... 1 quest done".
void GM_summary(int best, int completed, char* out, size_t size);

// The toast after a return: "Quest done: <title>" for one, "3 quests
// done" for more; else "<n> day streak" when cheer is set; "" for none.
void GM_toast(const int* newly, int count, const GMTargets* targets, int streak, int cheer, char* out, size_t size);

///////////////////////////////////////
// layout

// The first quest in view after scrolling by (-1 or 1) from first, with
// fit rows in view: never before the first quest or past the last page.
int GM_scroll(int first, int by, int fit);

// The parts of r outside hole: up to four, or r itself when they don't
// meet. 0 for an empty r.
int GM_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]);

// status and header together (either can be NULL or empty); w 0 for none.
MosaicRect GM_hole(const MosaicRect* status, const MosaicRect* header);

// The top bar for the streak in area's top left, PADDING in, PILL_SIZE
// tall, as wide as the room left of hole allows. w 0 when there's none.
MosaicRect GM_topRoom(const MosaicRect* area, const MosaicRect* hole, int scale);

// The room for the quests in area: under the top bar and hole, above the
// hint pills, PADDING in at the sides. w 0 when there's none.
MosaicRect GM_room(const MosaicRect* area, const MosaicRect* hole, int scale);

// The part of bar w wide that value of target fills: 0 for none, w when
// done or over.
int GM_barWidth(long long value, long long target, int w);

#endif
