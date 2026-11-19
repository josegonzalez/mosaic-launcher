// Smart collections: a folder on the root holding one collection per game
// franchise (Mario, Zelda, Sonic and so on), built from game names across
// every system, after the collections rules make: built-in ones (never
// played, played in the last N days, played over N hours, added in the
// last N days, one per decade and one per genre, each with a setting) and
// the user's own, from rules.txt in the plugin's folder in the launcher's
// userdata, a "name = query" a line. Each rule is a search query, parsed
// and matched by search's own model (search_model.c, built in), against
// every game the index has, with its .info genre and year: unplayed asks
// row-badges' games.played (recents without it), played: and hours: the
// play history, added: the game's file times, fav: and tag: the sets.
//
// Rules are matched again, without reading the index again, when the play
// data can have changed: back from a game (post_return), when the profile
// changes (launcher.profile_changed), when a set a rule reads changes
// (launcher.set_changed) and an hour after they last were; each rule's set
// that changed is announced. The index changing groups everything again.
//
// The games come from the launcher's card index (index_count and the
// rest), the first time the root is listed: systems with an emulator, two
// folders deep, without paks. Games are matched by name, the same name
// the game list shows (map.txt aliases included), against a built-in list
// of franchises plus any the "extra" setting adds. A franchise becomes a
// collection once it has "min" games. They're grouped again, and the
// lists reloaded, each time the index changes (launcher.index_ready).
//
// Y on a collection dismisses it; the plugin remembers dismissed ones in
// its .cfg. Y on the Smart Collections folder brings them all back. While
// the tags plugin offers tags.copy, Y on a collection opens a menu of
// Dismiss and Save as tag instead (the launcher's menu for two actions on
// one button); Save as tag names a new tag on the keyboard, starting from
// the collection's name, and tags.copy puts it on the collection's games
// once: the tag doesn't follow the collection afterwards.
//
// Other plugins read each collection as the entry set smart:<name> (see
// set_register), by full path, registered for every franchise as the
// launcher starts and holding the games its folder lists: none while it
// has fewer than "min" games or is dismissed. Asking one groups the
// index's games if they haven't been yet.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "smart_model.h"

#define ID "smart-collections"
#define FOLDER_NAME "Smart Collections"
#define MAX_PATH_LEN 512
#define MAX_DEPTH 2 // subfolders below a system folder whose games are grouped
#define RULES_FILE "rules.txt"
#define RULES_MAX_BYTES 65536 // of rules.txt that's read
#define DECADES_MAX 32
#define STALE_SECONDS 3600 // rules about time are matched again this long after

static const MosaicHost* host;
static char folder_path[MAX_PATH_LEN]; // <sd>/Smart Collections
static char roms_path[MAX_PATH_LEN];   // <sd>/Roms
static char rules_path[MAX_PATH_LEN];  // <userdata>/launcher/smart-collections/rules.txt
static SMCatalog catalog;
static SMIndex index_;
static SMNameSet dismissed;
static int min_games = 2;
static int scanned = 0;

// the rule settings
static int rule_on[SM_RULE_ADDED + 1];
static int rule_number[SM_RULE_ADDED + 1];
static int decades_on = 1, genres_on = 1;

static SEIndex games;          // every game the index has, for rules
static long long* added_at;    // added:'s time for each of games, 0 until asked
static SMRules user_rules;     // rules.txt's
static struct stat user_seen;  // rules.txt when it was read
static SMRules genre_rules;    // a rule per genre, from the last scan
static SMRules rules;          // every rule, in the order the folder lists them
static SMRuleHits hits;
static int matched = 0;        // hits hold rules' games
static time_t matched_at = 0;
static int rules_read_sets = 0; // a rule has fav: or tag:

// each rule's set as last announced, by name, to tell which changed
static char hashed_names[SM_MAX_RULES][SM_NAME_LEN];
static unsigned long hashed[SM_MAX_RULES];
static int hashed_count = 0;
static time_t (*now_fn)(time_t*) = time; // a test's clock

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static void say(const char* message) {
	host->log(ID, message);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

// On the card, or a game the card index has from a plugin's dir_entries,
// which opening it puts on the card, such as a multi-disc game's .m3u.
static int playable(const char* path) {
	if (exists(path)) return 1;
	MosaicIndexEntry e = { .size = sizeof(e) };
	return MOSAIC_HOST_HAS(host, index_find) && host->index_find(path, &e) >= 0 && e.added;
}

static void announce(const char* collection);

static double millis(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

///////////////////////////////////////
// the rules

static void ruleError(int line, const char* reason, const char* text, void* data) {
	(void)data;
	char message[MAX_PATH_LEN + SE_QUERY_LEN * 2 + 128];
	snprintf(message, sizeof(message), "%s line %d left out, %s: %s", rules_path, line, reason, text);
	say(message);
}

// Reads rules.txt again when it changed since it was read.
static void readUserRules(void) {
	struct stat st;
	int there = stat(rules_path, &st) == 0;
	if (there && st.st_mtime == user_seen.st_mtime && st.st_size == user_seen.st_size && st.st_ino == user_seen.st_ino) return;
	if (!there && !user_seen.st_ino && !user_seen.st_size) return;
	memset(&user_seen, 0, sizeof(user_seen));
	SM_rulesInit(&user_rules);
	if (!there) return;
	user_seen = st;
	FILE* f = fopen(rules_path, "rb");
	if (!f) return;
	char* text = malloc(RULES_MAX_BYTES + 1);
	size_t len = text ? fread(text, 1, RULES_MAX_BYTES, f) : 0;
	int more = text && fgetc(f) != EOF;
	fclose(f);
	if (!text) return;
	text[len] = '\0';
	char message[MAX_PATH_LEN + 128];
	if (more) {
		snprintf(message, sizeof(message), "%s is over %d bytes; only the first %d are read", rules_path, RULES_MAX_BYTES,
			RULES_MAX_BYTES);
		say(message);
	}
	// the built-ins that are on come first, so a line can't take their names
	SMRules all;
	SM_rulesInit(&all);
	for (int kind = SM_RULE_NEVER; kind <= SM_RULE_ADDED; kind++) {
		if (rule_on[kind]) SM_rulesAddBuiltin(&all, &catalog, kind, rule_number[kind]);
	}
	int builtins = all.count;
	int added = SM_rulesParse(&all, &catalog, text, ruleError, NULL);
	free(text);
	for (int i = builtins; i < all.count; i++) user_rules.items[user_rules.count++] = all.items[i];
	snprintf(message, sizeof(message), "%d rules from %s", added, rules_path);
	say(message);
}

// The rules, in the folder's order: the built-ins, the user's, the
// decades and the genres.
static void buildRules(void) {
	SM_rulesInit(&rules);
	for (int kind = SM_RULE_NEVER; kind <= SM_RULE_ADDED; kind++) {
		if (rule_on[kind]) SM_rulesAddBuiltin(&rules, &catalog, kind, rule_number[kind]);
	}
	for (int i = 0; i < user_rules.count; i++) {
		const SMRule* r = &user_rules.items[i];
		SM_rulesAdd(&rules, &catalog, r->kind, r->number, r->name, r->query);
	}
	char message[SM_NAME_LEN + 128];
	if (decades_on) {
		int decades[DECADES_MAX];
		int n = SM_decades(&games, decades, DECADES_MAX);
		for (int i = 0; i < n; i++) {
			char name[16], query[32];
			snprintf(name, sizeof(name), "%ds", decades[i]);
			snprintf(query, sizeof(query), "decade:%ds", decades[i]);
			if (SM_rulesAdd(&rules, &catalog, SM_RULE_DECADE, decades[i], name, query) == SM_TAKEN) {
				snprintf(message, sizeof(message), "left out the decade %s: a collection already has the name", name);
				say(message);
			}
		}
	}
	for (int i = 0; genres_on && i < genre_rules.count; i++) {
		const SMRule* r = &genre_rules.items[i];
		if (SM_rulesAdd(&rules, &catalog, r->kind, r->number, r->name, r->query) == SM_TAKEN) {
			snprintf(message, sizeof(message), "left out the genre %s: a collection already has the name", r->name);
			say(message);
		}
	}
	if (genre_rules.dropped) {
		snprintf(message, sizeof(message), "left out %d genres past the first %d", genre_rules.dropped, SM_MAX_RULES);
		say(message);
	}
	if (rules.dropped) {
		snprintf(message, sizeof(message), "left out %d rule collections past the first %d", rules.dropped, SM_MAX_RULES);
		say(message);
	}
	rules_read_sets = 0;
	for (int i = 0; i < rules.count; i++) {
		SEQuery q;
		SE_parse(rules.items[i].query, &q);
		for (int k = 0; k < q.filter_count; k++) {
			if (q.filters[k].kind == SE_FILTER_FAV || q.filters[k].kind == SE_FILTER_TAG) rules_read_sets = 1;
		}
	}
}

// The name a rule's collection shows: the built-ins' in words, with their
// number, the rest their own.
static void ruleLabel(const SMRule* r, char* out, size_t size) {
	unsigned long n = (unsigned long)r->number;
	switch (r->kind) {
	case SM_RULE_NEVER: snprintf(out, size, "%s", tr("Never played")); break;
	case SM_RULE_RECENT: snprintf(out, size, trPlural("Played in the last %d day", "Played in the last %d days", n), r->number); break;
	case SM_RULE_LONG: snprintf(out, size, trPlural("Over %d hour", "Over %d hours", n), r->number); break;
	case SM_RULE_ADDED: snprintf(out, size, trPlural("Added in the last %d day", "Added in the last %d days", n), r->number); break;
	default: snprintf(out, size, "%s", r->name); break;
	}
}

// A rule's collection shows: it has at least min games and isn't dismissed.
static int ruleShows(int r, int with_dismissed) {
	return hits.count[r] >= min_games && (with_dismissed || !SM_setHas(&dismissed, rules.items[r].name));
}

///////////////////////////////////////
// what rules ask

// a tag: filter's set, found once a match
static char tag_arg[SE_QUERY_LEN];
static char tag_set[SE_NAME_LEN + 8];
static int tag_known = 0;

static void findTag(const char* name, void* data) {
	(void)data;
	if (!tag_set[0] && SE_tagMatches(name, tag_arg)) snprintf(tag_set, sizeof(tag_set), "%s", name);
}

static int setHolds(const char* set, const SEGame* game) {
	if (host->set_has(set, game->path)) return 1;
	// a set holds a game folder by its .m3u or .cue, as stock records it
	if (game->type != MOSAIC_ENTRY_DIR) return 0;
	const char* name = strrchr(game->path, '/');
	char file[MAX_PATH_LEN * 2];
	snprintf(file, sizeof(file), "%s%s.m3u", game->path, name ? name : "");
	if (host->set_has(set, file)) return 1;
	snprintf(file, sizeof(file), "%s%s.cue", game->path, name ? name : "");
	return host->set_has(set, file);
}

// games.played's answer, or without it, whether the game or a file in its
// folder is in recents.
static int played(const SEGame* game) {
	if (MOSAIC_HOST_HAS(host, call)) {
		char args[MAX_PATH_LEN + 8], reply[64], value[8];
		snprintf(args, sizeof(args), "path=%s", game->path);
		if (host->call("games.played", args, reply, sizeof(reply)) >= 0
				&& host->event_value(reply, "played", value, sizeof(value)) > 0) return !strcmp(value, "1");
	}
	if (!MOSAIC_HOST_HAS(host, recents_count)) return 0;
	size_t len = strlen(game->path);
	int count = host->recents_count();
	for (int i = 0; i < count; i++) {
		const char* recent = host->recent_path(i);
		if (recent && (!strcmp(recent, game->path) || (!strncmp(recent, game->path, len) && recent[len] == '/'))) return 1;
	}
	return 0;
}

// played:'s last played time, hours:' seconds played and added:'s time
// the file was put on the card: the later of its modified and changed
// times, which on a FAT card is when it was copied there.
static long long askPlay(int kind, const char* path, void* data) {
	(void)data;
	if (kind == SE_FILTER_ADDED) {
		struct stat st;
		if (stat(path, &st) != 0) return 0;
		return (long long)(st.st_mtime > st.st_ctime ? st.st_mtime : st.st_ctime);
	}
	if (!MOSAIC_HOST_HAS(host, play_time)) return 0;
	return kind == SE_FILTER_PLAYED ? host->last_played(path) : host->play_time(path);
}

// what a match has asked about each game: unplayed's answers (-1 for not
// asked), so each rule doesn't ask again
typedef struct Match {
	signed char* played;
	long long now;
} Match;

static int ruleTest(const SEGame* game, int filter, const SEFilter* f, void* data) {
	(void)filter;
	Match* match = data;
	int i = (int)(game - games.games);
	switch (f->kind) {
	case SE_FILTER_FAV:
		return MOSAIC_HOST_HAS(host, is_filtered) && setHolds("favorites", game);
	case SE_FILTER_TAG:
		if (!MOSAIC_HOST_HAS(host, is_filtered) || !MOSAIC_HOST_HAS(host, set_names)) return 0;
		if (!tag_known || strcmp(tag_arg, f->arg) != 0) {
			tag_known = 1;
			snprintf(tag_arg, sizeof(tag_arg), "%s", f->arg);
			tag_set[0] = '\0';
			host->set_names(findTag, NULL);
		}
		return tag_set[0] && setHolds(tag_set, game);
	case SE_FILTER_UNPLAYED:
		if (!match->played) return !played(game);
		if (match->played[i] < 0) match->played[i] = (signed char)played(game);
		return !match->played[i];
	case SE_FILTER_SYSTEM:
		return SE_systemMatches(roms_path, game->path, f->arg);
	case SE_FILTER_ADDED:
		if (added_at && !added_at[i]) {
			added_at[i] = SE_playValue(game, game->type == MOSAIC_ENTRY_DIR, f->kind, askPlay, NULL);
			if (!added_at[i]) added_at[i] = -1;
		}
		return SE_timeMatches(f, added_at ? added_at[i] : SE_playValue(game, game->type == MOSAIC_ENTRY_DIR, f->kind, askPlay, NULL),
			match->now);
	case SE_FILTER_PLAYED:
	case SE_FILTER_HOURS:
		return SE_timeMatches(f, SE_playValue(game, game->type == MOSAIC_ENTRY_DIR, f->kind, askPlay, NULL), match->now);
	}
	return 0;
}

// Tells other plugins about each rule's set that changed since the last
// match, unless quiet, and keeps what each holds now.
static void announceRules(int quiet) {
	char names[SM_MAX_RULES][SM_NAME_LEN];
	unsigned long now[SM_MAX_RULES];
	for (int r = 0; r < rules.count; r++) {
		snprintf(names[r], SM_NAME_LEN, "%s", rules.items[r].name);
		now[r] = ruleShows(r, 0) ? SM_hitsHash(&games, hits.members[r], hits.count[r]) : SM_hitsHash(&games, NULL, 0);
	}
	unsigned long empty = SM_hitsHash(&games, NULL, 0);
	for (int r = 0; !quiet && r < rules.count; r++) {
		unsigned long was = empty;
		for (int k = 0; k < hashed_count; k++) {
			if (!strcmp(hashed_names[k], names[r])) was = hashed[k];
		}
		if (was != now[r]) announce(names[r]);
	}
	for (int k = 0; !quiet && k < hashed_count; k++) {
		int still = 0;
		for (int r = 0; r < rules.count && !still; r++) still = !strcmp(hashed_names[k], names[r]);
		if (!still && hashed[k] != empty) announce(hashed_names[k]);
	}
	memcpy(hashed_names, names, sizeof(names));
	memcpy(hashed, now, sizeof(now));
	hashed_count = rules.count;
}

// Matches every rule against the games, announcing the sets that changed
// unless quiet.
static void matchRules(int quiet) {
	double start = millis();
	readUserRules();
	buildRules();
	Match match = { calloc((size_t)(games.game_count ? games.game_count : 1), 1), (long long)now_fn(NULL) };
	if (match.played) memset(match.played, -1, (size_t)games.game_count);
	tag_known = 0;
	if (SM_rulesMatch(&rules, &games, ruleTest, &match, &hits) != 0) say("out of memory matching rules");
	free(match.played);
	matched = 1;
	matched_at = now_fn(NULL);
	announceRules(quiet);

	char message[SM_NAME_LEN + SE_QUERY_LEN + 64];
	int generated = 0;
	for (int r = 0; r < rules.count; r++) {
		if (rules.items[r].kind == SM_RULE_DECADE || rules.items[r].kind == SM_RULE_GENRE) {
			generated++;
			continue;
		}
		snprintf(message, sizeof(message), "rule %s (%s): %d games", rules.items[r].name, rules.items[r].query, hits.count[r]);
		say(message);
	}
	snprintf(message, sizeof(message), "matched %d rules, %d of them decades and genres, against %d games in %.1f ms",
		rules.count, generated, games.game_count, millis() - start);
	say(message);
}

///////////////////////////////////////
// the games, from the card index

// Registers each rule's set, smart:<name>.
static void registerRuleSets(void);

static void scan(void) {
	if (scanned) {
		if (!matched || now_fn(NULL) - matched_at >= STALE_SECONDS || now_fn(NULL) < matched_at) matchRules(0);
		return;
	}
	SM_indexFree(&index_);
	SM_indexInit(&index_);
	SE_indexFree(&games);
	free(added_at);
	added_at = NULL;
	SM_rulesInit(&genre_rules);
	SM_ruleHitsFree(&hits);
	if (!MOSAIC_HOST_HAS(host, index_refresh) || host->index_count(NULL) == MOSAIC_INDEX_NOT_READY) return;
	scanned = 1;
	int count = host->index_count(NULL);
	int odd_genres = 0;
	char odd[SE_NAME_LEN] = "";
	for (int i = 0; i < count; i++) {
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_entry(NULL, i, &e) != 0) break;
		if (e.kind != MOSAIC_INDEX_GAME || !e.emulator || e.depth > MAX_DEPTH || e.type == MOSAIC_ENTRY_PAK) continue;
		SM_indexAdd(&index_, &catalog, e.path, e.name, e.type);
		if (SE_indexAddInfo(&games, e.path, e.name, e.type, e.genre, e.year) != 0) {
			say("out of memory listing games for rules");
			break;
		}
		if (e.genre[0] && SM_genresAdd(&genre_rules, e.genre) == -3 && !odd_genres++) snprintf(odd, sizeof(odd), "%s", e.genre);
	}
	SM_indexSort(&index_, &catalog);
	SM_rulesSort(&genre_rules);
	added_at = calloc((size_t)(games.game_count ? games.game_count : 1), sizeof(long long));
	char message[SE_NAME_LEN + 160];
	if (odd_genres) {
		snprintf(message, sizeof(message), "left out %d genres with no letters or digits or a name of %d bytes or more, such as %s",
			odd_genres, SM_NAME_LEN, odd);
		say(message);
	}
	hashed_count = 0;
	matchRules(1); // the other plugins read the index again too
	registerRuleSets();

	int shown[SM_MAX_FRANCHISES], rule_count = 0;
	for (int r = 0; r < rules.count; r++) rule_count += ruleShows(r, 0);
	snprintf(message, sizeof(message), "%d games in %d collections and %d rule collections",
		index_.game_count, SM_visible(&index_, &catalog, min_games, &dismissed, shown), rule_count);
	say(message);
}

// Matches the rules again now, if the games were grouped, as the play
// data or a set may have changed.
static void rematch(void) {
	if (scanned) matchRules(0);
	else matched = 0;
}

// The index changed: group the games again when next asked, and list the
// root and the folder again.
static void indexReady(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	scanned = 0;
	matched = 0;
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
}

// Lists the folder again when it's on screen.
static void reloadFolder(void) {
	const char* current = MOSAIC_HOST_HAS(host, current_path) ? host->current_path() : NULL;
	size_t len = strlen(folder_path);
	if (current && !strncmp(current, folder_path, len) && (!current[len] || current[len] == '/')
			&& MOSAIC_HOST_HAS(host, reload_lists)) {
		host->reload_lists();
	}
}

// Another profile: its play history and recents.
static void profileChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	rematch();
}

// A set changed: the rules that read it may match other games now. Our
// own sets are left alone.
static void setChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)data;
	char set[SM_NAME_LEN + 16];
	if (!rules_read_sets || host->event_value(payload, "name", set, sizeof(set)) < 0 || !strncmp(set, "smart:", 6)) return;
	if (strcmp(set, "favorites") != 0 && strncmp(set, "tags:", 5) != 0) return;
	rematch();
	reloadFolder();
}

// Back from a game: its play data changed.
static void postReturn(void) {
	rematch();
}

///////////////////////////////////////
// paths

// A collection: a rule's or a franchise's, by its index, the other -1.
typedef struct Collection {
	int rule;
	int franchise;
} Collection;

// The collection with this name (ignoring case). Returns 1, or 0 for none.
static int findCollection(const char* name, Collection* out) {
	out->rule = SM_ruleFind(&rules, name);
	out->franchise = out->rule < 0 ? SM_find(&catalog, name) : -1;
	return out->rule >= 0 || out->franchise >= 0;
}

static const char* collectionName(const Collection* c) {
	return c->rule >= 0 ? rules.items[c->rule].name : catalog.items[c->franchise].name;
}

// The name it shows as.
static void collectionLabel(const Collection* c, char* out, size_t size) {
	if (c->rule >= 0) ruleLabel(&rules.items[c->rule], out, size);
	else snprintf(out, size, "%s", catalog.items[c->franchise].name);
}

// The collection a path under the folder names. Returns 1, or 0 for none.
static int collectionAt(const char* path, Collection* out) {
	size_t len = strlen(folder_path);
	if (!path || strncmp(path, folder_path, len) != 0 || path[len] != '/') return 0;
	return findCollection(path + len + 1, out);
}

///////////////////////////////////////
// dismissing and restoring

static void saveDismissed(void) {
	char list[SM_MAX_COLLECTIONS * SM_NAME_LEN];
	SM_setFormat(&dismissed, list, sizeof(list));
	host->cfg_set("dismissed", list);
}

// Tells other plugins the set for collection changed.
static void announce(const char* collection) {
	if (!MOSAIC_HOST_HAS(host, set_names)) return;
	char name[SM_NAME_LEN + 8];
	snprintf(name, sizeof(name), "smart:%s", collection);
	host->set_changed(name);
}

static void changed(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

// The collection waiting on the dismiss dialog, and the name it shows as.
static char pending[SM_NAME_LEN];
static char pending_label[SM_NAME_LEN * 2];

// Keeps what each rule's set holds now that one is dismissed or back, so
// the next match doesn't announce it again.
static void rehash(void) {
	for (int k = 0; k < hashed_count; k++) {
		int r = SM_ruleFind(&rules, hashed_names[k]);
		if (r < 0) continue;
		hashed[k] = ruleShows(r, 0) ? SM_hitsHash(&games, hits.members[r], hits.count[r]) : SM_hitsHash(&games, NULL, 0);
	}
}

static void dismissConfirmed(void* data) {
	(void)data;
	if (!pending[0] || !SM_setAdd(&dismissed, pending)) return;
	saveDismissed();
	rehash();
	char message[SM_NAME_LEN * 3 + 256];
	snprintf(message, sizeof(message), "dismissed %s", pending);
	say(message);
	announce(pending);
	snprintf(message, sizeof(message), tr("Dismissed %s"), pending_label);
	pending[0] = '\0';
	changed(message);
}

static int canDismiss(const char* path, void* data) {
	(void)path;
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	Collection c;
	if (!entry || host->entry_type(entry) != MOSAIC_ENTRY_DIR || !collectionAt(host->entry_path(entry), &c)) return 0;
	return !SM_setHas(&dismissed, collectionName(&c));
}

static void dismiss(const char* path, void* data) {
	(void)path;
	if (!canDismiss(path, data)) return;
	Collection c;
	collectionAt(host->entry_path(host->selected_entry()), &c);
	snprintf(pending, sizeof(pending), "%s", collectionName(&c));
	collectionLabel(&c, pending_label, sizeof(pending_label));
	if (!MOSAIC_HOST_HAS(host, confirm)) {
		dismissConfirmed(NULL);
		return;
	}
	char question[SM_NAME_LEN * 2 + 256];
	snprintf(question, sizeof(question), tr("Dismiss %s?"), pending_label);
	host->confirm(question, tr("DISMISS"), dismissConfirmed, NULL);
}

static void restoreConfirmed(void* data) {
	(void)data;
	if (!dismissed.count) return;
	for (int i = 0; i < dismissed.count; i++) {
		Collection c; // its set's name is the collection's
		announce(findCollection(dismissed.names[i], &c) ? collectionName(&c) : dismissed.names[i]);
	}
	dismissed.count = 0;
	saveDismissed();
	rehash();
	say("brought back dismissed collections");
	changed(tr("Dismissed collections are back"));
}

static int canRestore(const char* path, void* data) {
	(void)path;
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	return entry && dismissed.count > 0 && !strcmp(host->entry_path(entry), folder_path);
}

static void restore(const char* path, void* data) {
	(void)path;
	if (!canRestore(path, data)) return;
	if (!MOSAIC_HOST_HAS(host, confirm)) {
		restoreConfirmed(NULL);
		return;
	}
	char question[256];
	snprintf(question, sizeof(question), trPlural("Bring back %d dismissed collection?", "Bring back %d dismissed collections?",
		(unsigned long)dismissed.count), dismissed.count);
	host->confirm(question, tr("RESTORE"), restoreConfirmed, NULL);
}

///////////////////////////////////////
// saving as a tag

// The collection waiting on the keyboard.
static char saving[SM_NAME_LEN];

static void saveTyped(const char* text, void* data) {
	(void)data;
	if (!text || !saving[0]) return; // cancelled
	char args[SM_NAME_LEN * 4 + 32], reply[MOSAIC_CALL_MAX], value[SM_NAME_LEN * 2];
	snprintf(args, sizeof(args), "tag=%s\nset=smart:%s", text, saving);
	int rc = host->call("tags.copy", args, reply, sizeof(reply));
	char message[SM_NAME_LEN * 2 + 256];
	if (rc < 0) {
		snprintf(message, sizeof(message), "couldn't save %s as a tag: %d", saving, rc);
		say(message);
		host->toast(tr("Couldn't save the tag"));
		saving[0] = '\0';
		return;
	}
	if (host->event_value(reply, "error", value, sizeof(value)) >= 0) {
		snprintf(message, sizeof(message), "couldn't save %s as a tag: %s", saving, value);
		say(message);
		if (!strcmp(value, "exists")) snprintf(message, sizeof(message), tr("A tag named %s already exists"), text);
		else if (!strcmp(value, "name")) snprintf(message, sizeof(message), "%s", tr("Not a tag name"));
		else snprintf(message, sizeof(message), "%s", tr("Couldn't save the tag"));
		host->toast(message);
		saving[0] = '\0';
		return;
	}
	char tag[SM_NAME_LEN * 2], games_text[16];
	host->event_value(reply, "tag", tag, sizeof(tag));
	host->event_value(reply, "games", games_text, sizeof(games_text));
	int count = atoi(games_text);
	snprintf(message, sizeof(message), "saved %s as the tag %s, %d games", saving, tag, count);
	say(message);
	snprintf(message, sizeof(message), trPlural("Saved %d game as %s", "Saved %d games as %s", (unsigned long)count), count, tag);
	host->toast(message);
	saving[0] = '\0';
}

static int canSave(const char* path, void* data) {
	(void)path;
	(void)data;
	if (!MOSAIC_HOST_HAS(host, call) || !MOSAIC_HOST_HAS(host, keyboard) || !MOSAIC_HOST_HAS(host, toast)
			|| !host->has_capability("tags.copy")) return 0;
	return canDismiss(NULL, NULL); // a collection that shows
}

static void saveAsTag(const char* path, void* data) {
	if (!canSave(path, data)) return;
	Collection c;
	collectionAt(host->entry_path(host->selected_entry()), &c);
	snprintf(saving, sizeof(saving), "%s", collectionName(&c));
	char label[SM_NAME_LEN * 2];
	collectionLabel(&c, label, sizeof(label));
	host->keyboard(tr("Tag name"), label, saveTyped, NULL);
}

///////////////////////////////////////
// hooks

// Shown while any collection has enough games, even if all are dismissed,
// so they can be brought back.
static void rootEntries(MosaicList* root) {
	scan();
	int found[SM_MAX_FRANCHISES];
	int any = SM_visible(&index_, &catalog, min_games, NULL, found) > 0;
	for (int r = 0; r < rules.count && !any; r++) any = ruleShows(r, 1);
	if (any) host->list_push(root, host->entry_new(folder_path, MOSAIC_ENTRY_DIR, tr("Smart Collections")));
}

static void pushCollection(MosaicList* list, const Collection* c) {
	char dir[MAX_PATH_LEN + SM_NAME_LEN], label[SM_NAME_LEN * 2];
	snprintf(dir, sizeof(dir), "%s/%s", folder_path, collectionName(c));
	collectionLabel(c, label, sizeof(label));
	host->list_push(list, host->entry_new(dir, MOSAIC_ENTRY_DIR, label));
}

static void pushGame(MosaicList* list, const char* path, int type, const char* name) {
	if (!playable(path)) return; // deleted since the index was read
	host->list_push(list, host->entry_new(path, type, name));
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, folder_path)) {
		scan();
		MosaicList* list = host->list_new();
		// the rules' first, in their order, then the franchises' by name
		for (int r = 0; r < rules.count; r++) {
			Collection c = { r, -1 };
			if (ruleShows(r, 0)) pushCollection(list, &c);
		}
		int shown[SM_MAX_FRANCHISES];
		int n = SM_visible(&index_, &catalog, min_games, &dismissed, shown);
		for (int k = 0; k < n; k++) {
			Collection c = { -1, shown[k] };
			pushCollection(list, &c);
		}
		return list;
	}

	size_t len = strlen(folder_path);
	if (strncmp(path, folder_path, len) != 0 || path[len] != '/') return NULL;
	scan();
	MosaicList* list = host->list_new();
	Collection c;
	if (!collectionAt(path, &c)) return list;
	if (c.rule >= 0) {
		for (int k = 0; k < hits.count[c.rule]; k++) {
			const SEGame* game = &games.games[hits.members[c.rule][k]];
			pushGame(list, game->path, game->type, game->name);
		}
		return list;
	}
	int i = c.franchise;
	for (int k = 0; k < index_.member_count[i]; k++) {
		const SMGame* game = &index_.games[index_.members[i][k]];
		pushGame(list, game->path, game->type, game->name);
	}
	return list;
}

///////////////////////////////////////
// the sets

// The collection a set's name names, smart:<name>, when it shows.
// Returns 1, or 0.
static int setCollection(const char* set, Collection* out) {
	if (!set || strncmp(set, "smart:", 6) != 0) return 0;
	scan();
	if (!findCollection(set + 6, out) || SM_setHas(&dismissed, collectionName(out))) return 0;
	return out->rule >= 0 ? hits.count[out->rule] >= min_games : index_.member_count[out->franchise] >= min_games;
}

// The full path of a collection's k-th game.
static const char* memberPath(const Collection* c, int k) {
	if (c->rule >= 0) return games.games[hits.members[c->rule][k]].path;
	return index_.games[index_.members[c->franchise][k]].path;
}

static int memberCount(const Collection* c) {
	return c->rule >= 0 ? hits.count[c->rule] : index_.member_count[c->franchise];
}

static int setHas(const char* name, const char* path) {
	Collection c;
	if (!path || !setCollection(name, &c)) return 0;
	int count = memberCount(&c);
	for (int k = 0; k < count; k++) {
		if (!strcmp(memberPath(&c, k), path)) return 1;
	}
	return 0;
}

static void setList(const char* name, void (*add)(const char* path, void* ctx), void* ctx) {
	Collection c;
	if (!setCollection(name, &c)) return;
	int count = memberCount(&c);
	for (int k = 0; k < count; k++) add(memberPath(&c, k), ctx);
}

static void registerSet(const char* collection) {
	char name[SM_NAME_LEN + 8];
	snprintf(name, sizeof(name), "smart:%s", collection);
	if (host->set_register(name, setHas, setList) != 0) {
		char message[sizeof(name) + 32];
		snprintf(message, sizeof(message), "couldn't register the set %s", name);
		say(message);
	}
}

// Registering one again only replaces its callbacks, so each scan
// registers the genres and decades it finds.
static void registerRuleSets(void) {
	if (!MOSAIC_HOST_HAS(host, is_filtered)) return;
	for (int r = 0; r < rules.count; r++) registerSet(rules.items[r].name);
}

static void registerSets(void) {
	if (!MOSAIC_HOST_HAS(host, is_filtered)) return;
	for (int i = 0; i < catalog.count; i++) registerSet(catalog.items[i].name);
	registerRuleSets();
}

// A choice setting's number, or fallback when it isn't one.
static int number(const char* key, const char* fallback) {
	int n = atoi(host->cfg_get(key, fallback));
	return n > 0 ? n : atoi(fallback);
}

static int on(const char* key) {
	return strcmp(host->cfg_get(key, "on"), "off") != 0;
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/" FOLDER_NAME, host->sdcard_path);
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	snprintf(rules_path, sizeof(rules_path), "%s/launcher/" ID "/" RULES_FILE, host->userdata_path);

	SM_catalogInit(&catalog);
	if (on("builtin")) SM_catalogAddBuiltin(&catalog);
	SM_catalogAddList(&catalog, host->cfg_get("extra", ""));
	min_games = atoi(host->cfg_get("min", "2"));
	if (min_games < 1) min_games = 1;
	rule_on[SM_RULE_NEVER] = on("never");
	rule_on[SM_RULE_RECENT] = on("recent");
	rule_on[SM_RULE_LONG] = on("long");
	rule_on[SM_RULE_ADDED] = on("added");
	rule_number[SM_RULE_RECENT] = number("recent_days", "7");
	rule_number[SM_RULE_LONG] = number("long_hours", "10");
	rule_number[SM_RULE_ADDED] = number("added_days", "30");
	decades_on = on("decades");
	genres_on = on("genres");
	SM_setParse(&dismissed, host->cfg_get("dismissed", ""));
	SM_indexInit(&index_);
	SE_indexInit(&games);
	SM_ruleHitsInit(&hits);
	SM_rulesInit(&genre_rules);
	SM_rulesInit(&user_rules);
	memset(&user_seen, 0, sizeof(user_seen));
	added_at = NULL;
	scanned = 0;
	matched = 0;
	hashed_count = 0;
	readUserRules();
	buildRules(); // the built-ins and the user's, for their sets; decades and genres come with the games
	registerSets();
	if (!MOSAIC_HOST_HAS(host, index_refresh)) say("this launcher has no card index; there are no collections");
	else {
		host->subscribe("launcher.index_ready", indexReady, NULL);
		if (MOSAIC_HOST_HAS(host, profile_path)) host->subscribe("launcher.profile_changed", profileChanged, NULL);
		if (MOSAIC_HOST_HAS(host, set_names)) host->subscribe("launcher.set_changed", setChanged, NULL);
		host->index_count(NULL); // starts reading the card, so it's ready by the first listing
	}

	if (host->register_action("smart-collections.dismiss", "DISMISS", MOSAIC_ACTION_EDITS, canDismiss, dismiss, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "smart-collections.dismiss") != 0) {
		say("couldn't bind Y; collections can't be dismissed");
	}
	if (host->register_action("smart-collections.restore", "RESTORE", MOSAIC_ACTION_EDITS, canRestore, restore, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "smart-collections.restore") != 0) {
		say("couldn't bind Y; dismissed collections can't be brought back");
	}
	if (host->register_action("smart-collections.save_tag", "SAVE AS TAG", MOSAIC_ACTION_EDITS, canSave, saveAsTag, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "smart-collections.save_tag") != 0) {
		say("couldn't bind Y; collections can't be saved as tags");
	}
	return 0;
}

static void quit(void) {
	SM_indexFree(&index_);
	SE_indexFree(&games);
	SM_ruleHitsFree(&hits);
	free(added_at);
	added_at = NULL;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
