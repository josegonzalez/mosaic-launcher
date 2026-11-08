#include "raprogress.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#endif
#include "rahash.h"

#define BUCKETS 1024
#define CACHE_FILE_MAX (4 * 1024 * 1024)
#define MUTED_FILE_MAX (64 * 1024)
#define PATH_MAX_LEN 512
#define WORKER_NICE 19

enum { QUEUED, DONE };

typedef struct Entry {
	char* path;
	int state;
	int result; // 1, 0 or RA_PROGRESS_UNKNOWN once done
	char hash[33];
	RASummary summary;
	struct Entry* next;
} Entry;

// The hash cache, read from its file once, and only by the worker.
typedef struct Known {
	char* path; // relative to the card
	char hash[33];
	long long size, mtime;
	struct Known* next;
} Known;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int started = 0;
static int stopping = 0;
static volatile int cancel = 0;
static int news = 0;
static int full_logged = 0;

static char card[PATH_MAX_LEN], roms[PATH_MAX_LEN], cache_file[PATH_MAX_LEN];
static char ra_dir[256]; // short enough for the file names under it
static void (*say)(const char* message);

static Entry* table[BUCKETS];
static int entry_count = 0;
static Entry* queue[RA_PROGRESS_QUEUE];
static int queue_head = 0, queue_count = 0;

// worker only
static Known* known[BUCKETS];
static int known_loaded = 0;
static int known_lines = 0, known_count = 0;
static uint8_t* ledger = NULL;
static size_t ledger_size = 0;
static long long ledger_mtime = -1, ledger_stat_size = -1;

__attribute__((format(printf, 1, 2)))
static void note(const char* fmt, ...) {
	if (!say) return;
	char message[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	say(message);
}

static unsigned bucketOf(const char* text) {
	unsigned h = 2166136261u;
	for (const unsigned char* c = (const unsigned char*)text; *c; c++) h = (h ^ *c) * 16777619u;
	return h % BUCKETS;
}

void RAProgress_configure(const char* card_root, const char* roms_path, const char* ra, const char* cache,
	void (*log)(const char* message)) {
	RAProgress_quit();
	snprintf(card, sizeof(card), "%s", card_root ? card_root : "");
	snprintf(roms, sizeof(roms), "%s", roms_path ? roms_path : "");
	snprintf(ra_dir, sizeof(ra_dir), "%s", ra ? ra : "");
	snprintf(cache_file, sizeof(cache_file), "%s", cache ? cache : "");
	say = log;
	RAHash_setLog(log);
}

///////////////////////////////////////
// Files

static uint8_t* readFile(const char* path, size_t max, size_t* size) {
	*size = 0;
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (unsigned long long)st.st_size > max) return NULL;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	size_t want = (size_t)st.st_size;
	uint8_t* data = malloc(want + 1);
	size_t got = data ? fread(data, 1, want, f) : 0;
	fclose(f);
	if (!data || got != want) {
		free(data);
		return NULL;
	}
	data[want] = '\0';
	*size = want;
	return data;
}

static int isFile(const char* path, struct stat* st) {
	return stat(path, st) == 0 && S_ISREG(st->st_mode);
}

// The m3u's first line that isn't empty, as a path beside it, the way the
// launcher picks the disc it starts.
static int firstDisc(const char* m3u, char* out, size_t size) {
	FILE* f = fopen(m3u, "r");
	if (!f) return 0;
	char line[256];
	int found = 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0]) continue;
		const char* slash = strrchr(m3u, '/');
		int dir = slash ? (int)(slash - m3u + 1) : 0;
		found = snprintf(out, size, "%.*s%s", dir, m3u, line) < (int)size;
		break;
	}
	fclose(f);
	return found;
}

// What minarch is handed for a path, as openRom works it out: a folder's
// own m3u, or the first disc in it, in place of the folder or that m3u.
static void resolve(const char* path, char* out, size_t size) {
	snprintf(out, size, "%s", path);
	struct stat st;
	char m3u[PATH_MAX_LEN];
	if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
		const char* name = strrchr(path, '/');
		name = name ? name + 1 : path;
		if (snprintf(m3u, sizeof(m3u), "%s/%s.m3u", path, name) >= (int)sizeof(m3u)) return;
		if (isFile(m3u, &st)) firstDisc(m3u, out, size);
		return;
	}
	size_t length = strlen(path);
	if (length < 4 || strcasecmp(path + length - 4, ".m3u")) return;
	// the folder's m3u is <folder>/<folder name>.m3u
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path) return;
	const char* parent = slash - 1;
	while (parent > path && *parent != '/') parent--;
	if (*parent == '/') parent++;
	size_t folder = (size_t)(slash - parent);
	if (strlen(slash + 1) == folder + 4 && !strncmp(slash + 1, parent, folder)) firstDisc(path, out, size);
}

static const char* relative(const char* path) {
	size_t n = strlen(card);
	if (n && !strncmp(path, card, n) && path[n] == '/') return path + n + 1;
	return path;
}

///////////////////////////////////////
// The hash cache file

static Known* findKnown(const char* rel) {
	for (Known* k = known[bucketOf(rel)]; k; k = k->next)
		if (!strcmp(k->path, rel)) return k;
	return NULL;
}

static void remember(const char* rel, const char* hash, long long size, long long mtime) {
	Known* k = findKnown(rel);
	if (!k) {
		if (known_count >= RA_PROGRESS_ENTRIES * 4) return;
		k = calloc(1, sizeof(Known));
		if (!k || !(k->path = strdup(rel))) {
			free(k);
			return;
		}
		unsigned b = bucketOf(rel);
		k->next = known[b];
		known[b] = k;
		known_count++;
	}
	snprintf(k->hash, sizeof(k->hash), "%s", hash);
	k->size = size;
	k->mtime = mtime;
}

static void ensureParent(const char* path) {
	char dir[PATH_MAX_LEN];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	if (!slash || slash == dir) return;
	*slash = '\0';
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) note("retroachievements: can't make %s: %s", dir, strerror(errno));
}

// Rewritten without the lines later ones replaced.
static void compact(void) {
	char tmp[PATH_MAX_LEN + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", cache_file);
	FILE* f = fopen(tmp, "w");
	if (!f) return;
	int ok = 1;
	for (int b = 0; b < BUCKETS && ok; b++) {
		for (Known* k = known[b]; k && ok; k = k->next) {
			char line[PATH_MAX_LEN + 96];
			if (RA_formatCacheLine(k->hash, k->size, k->mtime, k->path, line, sizeof(line))) ok = fputs(line, f) >= 0;
		}
	}
	if (fclose(f) != 0) ok = 0;
	if (ok && rename(tmp, cache_file) == 0) {
		note("retroachievements: compacted %s from %i lines to %i", cache_file, known_lines, known_count);
		known_lines = known_count;
	}
	else unlink(tmp);
}

static void loadKnown(void) {
	if (known_loaded) return;
	known_loaded = 1;
	size_t size;
	char* text = (char*)readFile(cache_file, CACHE_FILE_MAX, &size);
	if (!text) return;
	char* line = text;
	while (line < text + size) {
		char* end = memchr(line, '\n', (size_t)(text + size - line));
		if (end) *end = '\0';
		char hash[33], path[PATH_MAX_LEN];
		long long file_size, mtime;
		if (RA_parseCacheLine(line, hash, &file_size, &mtime, path, sizeof(path))) {
			remember(path, hash, file_size, mtime);
			known_lines++;
		}
		if (!end) break;
		line = end + 1;
	}
	free(text);
	if (known_lines > known_count * 2 + 64) compact();
}

static void saveKnown(const char* rel, const char* hash, long long size, long long mtime) {
	remember(rel, hash, size, mtime);
	char line[PATH_MAX_LEN + 96];
	if (!RA_formatCacheLine(hash, size, mtime, rel, line, sizeof(line))) return;
	ensureParent(cache_file);
	FILE* f = fopen(cache_file, "a");
	if (!f) {
		note("retroachievements: can't write %s: %s", cache_file, strerror(errno));
		return;
	}
	fputs(line, f);
	fclose(f);
	known_lines++;
}

///////////////////////////////////////
// minarch's files for a hash

static void readLedger(void) {
	char path[PATH_MAX_LEN];
	snprintf(path, sizeof(path), "%s/offline/ledger.bin", ra_dir);
	struct stat st;
	long long mtime = -1, size = -1;
	if (isFile(path, &st)) {
		mtime = (long long)st.st_mtime;
		size = (long long)st.st_size;
	}
	if (mtime == ledger_mtime && size == ledger_stat_size) return;
	free(ledger);
	ledger = NULL;
	ledger_size = 0;
	ledger_mtime = mtime;
	ledger_stat_size = size;
	if (size < 0) return;
	ledger = readFile(path, RA_LEDGER_MAX, &ledger_size);
	if (!ledger) note("retroachievements: ignoring %s: over %i bytes or unreadable", path, RA_LEDGER_MAX);
}

static int readFramed(const char* name, const char* hash, RAGame* game, int sets) {
	char path[PATH_MAX_LEN];
	snprintf(path, sizeof(path), "%s/offline/cache/%s_%s.bin", ra_dir, name, hash);
	size_t size;
	uint8_t* data = readFile(path, 4 + RA_FRAMED_MAX + 32, &size);
	if (!data) return 0;
	const uint8_t* body;
	size_t body_size;
	int ok = RA_unframe(data, size, &body, &body_size);
	if (!ok) note("retroachievements: ignoring %s: bad length or checksum", path);
	else if (sets) ok = RA_parseSets((const char*)body, body_size, game);
	else ok = RA_parseSession((const char*)body, body_size, game);
	free(data);
	return ok;
}

// 1 with the summary when the card has a set for the hash.
static int readProgress(const char* hash, RASummary* out) {
	RAGame game;
	RA_gameInit(&game);
	if (!readFramed("achievementsets", hash, &game, 1) && !readFramed("patch", hash, &game, 1)) {
		RA_gameFree(&game);
		return 0;
	}
	readFramed("startsession", hash, &game, 0);
	readLedger();
	if (ledger && RA_parseLedger(ledger, ledger_size, hash, &game) < 0)
		note("retroachievements: ignoring the ledger: not whole records");
	char path[PATH_MAX_LEN];
	snprintf(path, sizeof(path), "%s/muted/%s.txt", ra_dir, hash);
	size_t size;
	char* muted = (char*)readFile(path, MUTED_FILE_MAX, &size);
	if (muted) RA_parseMuted(muted, size, &game);
	free(muted);
	RA_gameFinish(&game);
	RA_summarize(&game, out);
	RA_gameFree(&game);
	return 1;
}

typedef struct Guess {
	RASummary summary;
	int found;
} Guess;

static int knownHash(const char* hash, void* ctx) {
	Guess* guess = ctx;
	char lower[33];
	for (int i = 0; i < 32 && hash[i]; i++) lower[i] = (char)(hash[i] >= 'A' && hash[i] <= 'F' ? hash[i] - 'A' + 'a' : hash[i]);
	lower[32] = '\0';
	if (!RA_isHash(lower)) return 0;
	guess->found = readProgress(lower, &guess->summary);
	return guess->found;
}

static long long millis(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Works out one game. Returns 1, 0 or RA_PROGRESS_UNKNOWN for a hash cut
// short by RAProgress_quit.
static int work(const char* path, char hash[33], RASummary* summary) {
	hash[0] = '\0';
	memset(summary, 0, sizeof(*summary));
	char target[PATH_MAX_LEN];
	resolve(path, target, sizeof(target));
	struct stat st;
	if (!isFile(target, &st)) return 0;
	char tag[64];
	RA_tagOf(roms, target, tag, sizeof(tag));
	int console = RA_consoleFor(tag, target);
	const char* rel = relative(target);
	loadKnown();
	const Known* k = findKnown(rel);
	if (k && k->size == (long long)st.st_size && k->mtime == (long long)st.st_mtime) {
		if (!strcmp(k->hash, "-")) return 0;
		snprintf(hash, 33, "%s", k->hash);
		return readProgress(hash, summary);
	}
	long long start = millis();
	if (console == RC_CONSOLE_UNKNOWN) {
		// rcheevos guesses from the file, and minarch takes the guess the
		// server knows; here, the one the card has a set for. Not cached,
		// since a later play can make another guess the right one.
		Guess guess;
		memset(&guess, 0, sizeof(guess));
		int ok = RAHash_game(target, console, knownHash, &guess, hash);
		if (cancel) return RA_PROGRESS_UNKNOWN;
		if (!ok) {
			hash[0] = '\0';
			return 0;
		}
		*summary = guess.summary;
		note("retroachievements: %s is %s, guessed in %lld ms", rel, hash, millis() - start);
		return 1;
	}
	int ok = RAHash_game(target, console, NULL, NULL, hash);
	if (cancel) return RA_PROGRESS_UNKNOWN;
	if (!ok) hash[0] = '\0';
	saveKnown(rel, ok ? hash : "-", (long long)st.st_size, (long long)st.st_mtime);
	note("retroachievements: %s is %s, hashed as console %i in %lld ms", rel, ok ? hash : "unhashable", console,
		millis() - start);
	return ok ? readProgress(hash, summary) : 0;
}

static void* run(void* unused) {
	(void)unused;
#ifdef __linux__
	// only this thread: on Linux each thread has its own nice value
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), WORKER_NICE);
#endif
	pthread_mutex_lock(&lock);
	for (;;) {
		while (!queue_count && !stopping) pthread_cond_wait(&wake, &lock);
		if (stopping) break;
		Entry* entry = queue[queue_head];
		queue_head = (queue_head + 1) % RA_PROGRESS_QUEUE;
		queue_count--;
		char path[PATH_MAX_LEN];
		snprintf(path, sizeof(path), "%s", entry->path);
		pthread_mutex_unlock(&lock);

		char hash[33];
		RASummary summary;
		int result = work(path, hash, &summary);

		pthread_mutex_lock(&lock);
		if (result == RA_PROGRESS_UNKNOWN) break;
		entry->result = result;
		entry->summary = summary;
		snprintf(entry->hash, sizeof(entry->hash), "%s", hash);
		entry->state = DONE;
		news = 1;
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

///////////////////////////////////////
// Asking, from the main thread

static Entry* find(const char* path) {
	for (Entry* e = table[bucketOf(path)]; e; e = e->next)
		if (!strcmp(e->path, path)) return e;
	return NULL;
}

// Under the lock. The entry for path, queued if it's new.
static Entry* ask(const char* path) {
	Entry* e = find(path);
	if (e) return e;
	if (!started || stopping) return NULL;
	if (entry_count >= RA_PROGRESS_ENTRIES) {
		if (!full_logged) note("retroachievements: asked about more than %i games, the rest stay unknown", RA_PROGRESS_ENTRIES);
		full_logged = 1;
		return NULL;
	}
	if (queue_count >= RA_PROGRESS_QUEUE) return NULL;
	e = calloc(1, sizeof(Entry));
	if (!e || !(e->path = strdup(path))) {
		free(e);
		return NULL;
	}
	e->state = QUEUED;
	e->result = RA_PROGRESS_UNKNOWN;
	unsigned b = bucketOf(path);
	e->next = table[b];
	table[b] = e;
	entry_count++;
	queue[(queue_head + queue_count) % RA_PROGRESS_QUEUE] = e;
	queue_count++;
	pthread_cond_signal(&wake);
	return e;
}

static int start(void) {
	if (started) return 1;
	if (!ra_dir[0] || stopping) return 0;
	cancel = 0;
	RAHash_setCancel(&cancel);
	if (pthread_create(&worker, NULL, run, NULL) != 0) {
		note("retroachievements: can't start the worker: %s", strerror(errno));
		stopping = 1; // don't try every frame
		return 0;
	}
	started = 1;
	note("retroachievements: reading progress from %s, hashes cached in %s", ra_dir, cache_file);
	return 1;
}

static int answer(const char* path, Entry* copy) {
	if (!path || !path[0] || strlen(path) >= PATH_MAX_LEN) return RA_PROGRESS_UNKNOWN;
	if (!start()) return RA_PROGRESS_UNKNOWN;
	pthread_mutex_lock(&lock);
	Entry* e = ask(path);
	int result = e && e->state == DONE ? e->result : RA_PROGRESS_UNKNOWN;
	if (result != RA_PROGRESS_UNKNOWN) *copy = *e;
	pthread_mutex_unlock(&lock);
	return result;
}

int RAProgress_get(const char* path, RASummary* out) {
	Entry copy;
	int result = answer(path, &copy);
	if (result == 1 && out) *out = copy.summary;
	return result;
}

int RAProgress_hash(const char* path, char* out, size_t size) {
	Entry copy;
	int result = answer(path, &copy);
	if (result == RA_PROGRESS_UNKNOWN) return result;
	if (!copy.hash[0]) return 0;
	if (out && size) snprintf(out, size, "%s", copy.hash);
	return 1;
}

int RAProgress_news(void) {
	if (!started) return 0;
	pthread_mutex_lock(&lock);
	int any = news;
	news = 0;
	pthread_mutex_unlock(&lock);
	return any;
}

void RAProgress_quit(void) {
	if (started) {
		pthread_mutex_lock(&lock);
		stopping = 1;
		cancel = 1;
		pthread_cond_signal(&wake);
		pthread_mutex_unlock(&lock);
		pthread_join(worker, NULL);
		started = 0;
	}
	RAHash_setCancel(NULL);
	for (int b = 0; b < BUCKETS; b++) {
		while (table[b]) {
			Entry* e = table[b];
			table[b] = e->next;
			free(e->path);
			free(e);
		}
		while (known[b]) {
			Known* k = known[b];
			known[b] = k->next;
			free(k->path);
			free(k);
		}
	}
	entry_count = queue_head = queue_count = 0;
	known_loaded = known_lines = known_count = 0;
	free(ledger);
	ledger = NULL;
	ledger_size = 0;
	ledger_mtime = ledger_stat_size = -1;
	stopping = 0;
	cancel = 0;
	news = 0;
	full_logged = 0;
}
