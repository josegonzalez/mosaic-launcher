// Music: the front end of musicd, a daemon of its own that plays music
// outside the launcher, so it can go on while the launcher exits for a
// game. The daemon ships beside this plugin.so; the plugin starts it, by
// its path with fork and exec, the first time something is played, and
// talks to it over a Unix socket (music_model.h has the protocol).
//
// music.player is a screen over the game list with the music folder's
// folders and tracks (MP3, FLAC and WAV) and what's playing. A plays a
// track, then the rest of its folder, or opens a folder; B goes back up,
// and closes it from the music folder. X pauses or plays on, L1 and R1 go
// to the previous and next track, L2 and R2 seek 10 seconds, SELECT turns
// shuffle on or off and START steps repeat through off, all and one (both
// saved as settings). Y opens music.lock, a full screen with the clock and
// the track, which takes every button but power until SELECT then START.
// The music.open action opens the player, from a quick menu tile or a
// game list button; music.lock and music.toggle are there for FN buttons.
//
// Over the other screens a now playing pill shows at the bottom for a few
// seconds after a track starts (the overlay setting). While music plays it
// holds off auto-sleep (the awake setting). Before a game or pak it pauses
// the music and lets go of the audio device, and plays on when the
// launcher comes back, unless the games setting is play. Before a sleep
// the music pauses; before a power-off the daemon quits.
//
// Tracks show their title tag where they have one (music_tags.h reads
// them), on a thread of the plugin's own, and the playing track shows its
// artist and cover art: its embedded picture, written to ART_DIR for the
// launcher's art_load, or else its folder's cover file.
//
// The daemon saves the queue and the track's place in the resume file (see
// musicd.c). At start, with no daemon running and the resume setting on,
// the plugin reads it and shows its track paused, on the player and the
// pill, without starting the daemon; X or music.toggle starts it and plays
// on from there.

#define _GNU_SOURCE // dladdr
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "music_model.h"
#include "music_tags.h"

#define ID "music"
#define PLAYER_ID ID ".player"
#define LOCK_ID ID ".lock"
#define OPEN_ACTION ID ".open"
#define LOCK_ACTION ID ".lock"
#define TOGGLE_ACTION ID ".toggle"
#define DAEMON_NAME "musicd"
#define REQUEST_MS 300 // the status line's wait for its answer
// other commands' wait: the daemon opens or closes the audio device before
// it answers, which can take a while
#define DEVICE_REQUEST_MS 3000
#define START_WAIT_MS 2000   // a started daemon's wait to answer
#define POLL_SCREEN_MS 500   // asking for the status while a screen shows
#define POLL_PLAYING_MS 1000 // while something plays or is paused
#define POLL_IDLE_MS 5000    // while the daemon runs with nothing queued
#define SEEK_MS 10000
#define DIM 140 // the lock screen's alpha over black
#define ART_DIR "/tmp/mosaic-music-art" // embedded cover art, written out for art_load

extern char** environ;

static const MosaicHost* host;
static char root[MU_PATH_MAX];
static char daemon_path[MU_PATH_MAX];
static char log_path[MU_PATH_MAX];
static char resume_path[MU_PATH_MAX]; // "" with the resume setting off
static int games_play = 0; // keep playing during games
static int awake = 1;
static int shuffle = 0, repeat = MU_REPEAT_OFF;

static int daemon_up = 0;
static MuStatus st;
static unsigned long next_poll = 0, last_now = 0;
static int awake_held = 0;
static char logged_now[MU_PATH_MAX + 64];

// the resume file's track, shown paused until it's played on
static int restored = 0, restored_pill = 0, restored_shuffle = 0;
static MuStatus restored_st;

static MuOverlay overlay;
static int overlay_shown = 0;

// the player
static char dir[MU_PATH_MAX];
static MuList list;
static int selected = 0, first_row = 0, visible_rows = 1;
static int player_open = 0;
static char logged_view[MU_PATH_MAX + MU_NAME_MAX + 64];

// the lock screen
static int lock_open = 0, unlock_due = 0;
static MuUnlock unlock;
static char lock_clock[16];

// Tags are read on a thread of their own, so a big folder doesn't hold up
// a frame: the listing's track titles one at a time, and the playing
// track's title, artist and cover art first. What's under tag_lock is
// shared with it; the rest is the main thread's.
typedef struct Label {
	char title[MU_TAG_TEXT_MAX]; // "" until it's read, or without a title tag
} Label;
static pthread_t tag_thread;
static int tag_thread_up = 0;
static pthread_mutex_t tag_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tag_wake = PTHREAD_COND_INITIALIZER;
static int tag_quit = 0;
// the listing's tracks to read, list items from job_dirs on, and their titles
static char (*job_paths)[MU_PATH_MAX] = NULL;
static int job_count = 0, job_next = 0, job_dirs = 0, job_titled = 0, job_done = 0;
static unsigned int job_gen = 0;
static Label* labels = NULL; // list.count of them
static int labels_dirty = 0;
// the playing track's tags: asked for, and the answer
static char now_want[MU_PATH_MAX];
static int now_pending = 0, now_dirty = 0;
static char now_track[MU_PATH_MAX], now_title[MU_TAG_TEXT_MAX], now_artist[MU_TAG_TEXT_MAX], now_art[MU_PATH_MAX];
static int now_embedded = 0;

// the main thread's copy of the playing track's tags
static char np_asked[MU_PATH_MAX]; // the track they were asked for
static char np_track[MU_PATH_MAX], np_title[MU_TAG_TEXT_MAX], np_artist[MU_TAG_TEXT_MAX], np_art[MU_PATH_MAX];
// its art, scaled once for where it's drawn
static MosaicSurface* art_scaled = NULL;
static char art_scaled_path[MU_PATH_MAX], art_failed[MU_PATH_MAX], art_logged[MU_PATH_MAX];
static int art_scaled_size = 0;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
	char text[2 * MU_PATH_MAX + 2 * MU_TAG_TEXT_MAX + 64]; // a track, its art and two tags
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	host->log(ID, text);
}

///////////////////////////////////////
// the daemon

// Sends a line and reads the one back. Returns 0 with the answer in
// reply, -1 when no daemon listens, -2 when it didn't answer in time.
static int request(const char* line, char* reply, size_t size, int timeout_ms) {
	if (size) reply[0] = '\0';
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
	struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", MU_SOCKET_PATH);
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
#ifdef MSG_NOSIGNAL
	int send_flags = MSG_NOSIGNAL;
#else
	int send_flags = 0;
#endif
	size_t len = strlen(line), sent = 0;
	while (sent < len) {
		ssize_t n = send(fd, line + sent, len - sent, send_flags);
		if (n <= 0) {
			close(fd);
			return -2;
		}
		sent += (size_t)n;
	}
	size_t got = 0;
	while (got + 1 < size) {
		ssize_t n = read(fd, reply + got, size - 1 - got);
		if (n <= 0) break;
		got += (size_t)n;
		if (memchr(reply, '\n', got)) break;
	}
	close(fd);
	reply[got] = '\0';
	return memchr(reply, '\n', got) ? 0 : -2;
}

static int sendCommand(int command, const char* arg, int timeout_ms) {
	char line[MU_LINE_MAX], reply[MU_LINE_MAX];
	if (MU_formatCommand(command, arg, line, sizeof(line)) < 0) return -1;
	int rc = request(line, reply, sizeof(reply), timeout_ms);
	if (rc == -1) daemon_up = 0;
	if (rc != 0) return rc;
	if (!strncmp(reply, "ok", 2)) return 0;
	char* end = strchr(reply, '\n');
	if (end) *end = '\0';
	say("the daemon said %s to %.*s", reply, (int)strcspn(line, "\n"), line);
	return -3;
}

static void logNowPlaying(void) {
	char text[sizeof(logged_now)];
	if (st.state == MU_STOPPED) snprintf(text, sizeof(text), "stopped");
	else {
		char title[MU_NAME_MAX], len[16];
		MU_trackTitle(st.track, title, sizeof(title));
		MU_formatTime(st.len_ms, len, sizeof(len));
		snprintf(text, sizeof(text), "%s, %s of %s%s", title, MU_stateName(st.state), len, st.held ? ", held" : "");
	}
	if (strcmp(text, logged_now) == 0) return;
	snprintf(logged_now, sizeof(logged_now), "%s", text);
	say("now: %s", text);
}

// Asks the daemon what it's doing.
static void askStatus(unsigned long now) {
	char reply[MU_LINE_MAX];
	MuStatus s;
	int rc = request("status\n", reply, sizeof(reply), REQUEST_MS);
	if (rc == 0 && MU_parseStatus(reply, &s)) {
		if (!daemon_up) say("the daemon is running");
		daemon_up = 1;
		restored = 0;
		st = s;
	} else {
		if (daemon_up) say("the daemon stopped answering");
		daemon_up = 0;
		if (restored) st = restored_st;
		else memset(&st, 0, sizeof(st));
	}
	logNowPlaying();
	if (MuOverlay_update(&overlay, &st, now)) overlay_shown = 0;
	unsigned long wait = player_open || lock_open ? POLL_SCREEN_MS
		: !daemon_up ? 0
		: st.state != MU_STOPPED ? POLL_PLAYING_MS
		: POLL_IDLE_MS;
	next_poll = wait ? now + wait : 0;
}

static int daemonPid(void) {
	FILE* f = fopen(MU_PID_PATH, "r");
	if (!f) return -1;
	long pid = 0;
	int ok = fscanf(f, "%ld", &pid) == 1;
	fclose(f);
	return ok && pid > 1 ? (int)pid : -1;
}

// The launcher's environment without libraries preloaded into it, such as
// the desktop tests' presser, which the daemon doesn't have the symbols
// for. NULL without memory.
static char** daemonEnv(void) {
	int count = 0;
	while (environ[count]) count++;
	char** env = malloc((size_t)(count + 1) * sizeof(char*));
	if (!env) return NULL;
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (!strncmp(environ[i], "LD_PRELOAD=", 11) || !strncmp(environ[i], "DYLD_INSERT_LIBRARIES=", 22)) continue;
		env[n++] = environ[i];
	}
	env[n] = NULL;
	return env;
}

// Starts the daemon in a session of its own, so it isn't the launcher's
// child or in its process group, with its output in the log file and no
// other file of the launcher's open. Waits for it to answer.
static int startDaemon(void) {
	struct stat sb;
	if (!daemon_path[0] || stat(daemon_path, &sb) != 0 || !S_ISREG(sb.st_mode) || access(daemon_path, X_OK) != 0) {
		say("no daemon at %s", daemon_path[0] ? daemon_path : "(unknown)");
		return -1;
	}
	char** env = daemonEnv();
	if (!env) return -1;
	int out = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
	if (out < 0) out = open("/dev/null", O_WRONLY | O_CLOEXEC);
	long max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
	char* const argv[] = { daemon_path, resume_path[0] ? resume_path : NULL, NULL };
	pid_t child = fork();
	if (child == 0) {
		setsid();
		pid_t grandchild = fork();
		if (grandchild != 0) _exit(grandchild < 0 ? 1 : 0);
		if (in >= 0) dup2(in, 0);
		if (out >= 0) {
			dup2(out, 1);
			dup2(out, 2);
		}
		for (int fd = 3; fd < max_fd; fd++) close(fd);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		execve(daemon_path, argv, env);
		_exit(127);
	}
	free(env);
	if (in >= 0) close(in);
	if (out >= 0) close(out);
	if (child < 0) {
		say("couldn't start the daemon: %s", strerror(errno));
		return -1;
	}
	int status = 0;
	while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
	}
	for (int waited = 0; waited < START_WAIT_MS; waited += 20) {
		char reply[MU_LINE_MAX];
		if (request("status\n", reply, sizeof(reply), REQUEST_MS) == 0) {
			daemon_up = 1;
			say("started the daemon, pid %i", daemonPid());
			return 0;
		}
		usleep(20000);
	}
	say("the daemon didn't answer after it started; its log is %s", log_path);
	return -1;
}

static int ensureDaemon(void) {
	if (daemon_up || sendCommand(MU_CMD_STATUS, NULL, REQUEST_MS) == 0) {
		daemon_up = 1;
		return 0;
	}
	return startDaemon();
}

// Plays a track and the rest of its folder.
static void playPath(const char* path) {
	if (ensureDaemon() != 0) {
		if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Music can't play"));
		return;
	}
	sendCommand(MU_CMD_REPEAT, MU_repeatName(repeat), REQUEST_MS);
	sendCommand(MU_CMD_SHUFFLE, shuffle ? "on" : "off", REQUEST_MS);
	restored = 0;
	if (sendCommand(MU_CMD_PLAY, path, DEVICE_REQUEST_MS) == 0) say("playing %s", path);
	else if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Music can't play that"));
	askStatus(last_now);
}

static void command(int cmd, const char* arg) {
	if (!daemon_up) return;
	sendCommand(cmd, arg, DEVICE_REQUEST_MS);
	askStatus(last_now);
}

// Starts the daemon to play on from the resume file, with the shuffle and
// repeat settings as they are now.
static void playRestored(void) {
	if (ensureDaemon() != 0) {
		if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Music can't play"));
		return;
	}
	restored = 0;
	if (sendCommand(MU_CMD_RESTORE, NULL, DEVICE_REQUEST_MS) == 0) {
		say("playing on from %s at %lld ms", restored_st.track, restored_st.pos_ms);
		sendCommand(MU_CMD_REPEAT, MU_repeatName(repeat), REQUEST_MS);
		if (shuffle != restored_shuffle) sendCommand(MU_CMD_SHUFFLE, shuffle ? "on" : "off", REQUEST_MS);
	} else if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Music can't play on"));
	askStatus(last_now);
}

// X and music.toggle: pause or play on, from the resume file when that's
// what shows.
static void playOrPause(void) {
	if (restored && !daemon_up) playRestored();
	else command(MU_CMD_TOGGLE, NULL);
}

// SELECT and START change what a restored track shows too.
static void restoredSettings(void) {
	if (!restored) return;
	restored_st.shuffle = shuffle;
	restored_st.repeat = repeat;
	if (!daemon_up) st = restored_st;
}

// Reads the resume file into what shows, paused, when its track is still
// on the card, dropping the queue's tracks that aren't.
static void readResume(void) {
	MuQueue q;
	MuQueue_init(&q, 0);
	long long pos = 0, len = 0;
	if (!MU_readResume(resume_path, &q, &pos, &len)) {
		if (access(resume_path, F_OK) == 0) {
			say("can't read %s; removed it", resume_path);
			unlink(resume_path);
		}
		return;
	}
	int count = q.count;
	if (MuQueue_dropMissing(&q, MU_isFile, NULL) < 0) {
		say("the track to resume is gone; removed %s", resume_path);
		unlink(resume_path);
		return;
	}
	memset(&restored_st, 0, sizeof(restored_st));
	restored_st.state = MU_PAUSED;
	restored_st.index = q.pos;
	restored_st.count = q.count;
	restored_st.pos_ms = pos;
	restored_st.len_ms = len;
	restored_st.shuffle = shuffle;
	restored_st.repeat = repeat;
	snprintf(restored_st.track, sizeof(restored_st.track), "%s", q.paths[MuQueue_current(&q)]);
	restored_shuffle = q.shuffle;
	MuQueue_free(&q);
	restored = restored_pill = 1;
	st = restored_st;
	say("resuming %s at %lld ms, %i of %i (%i gone), paused", st.track, pos, st.index + 1, st.count, count - st.count);
}

///////////////////////////////////////
// tags

// Removes the embedded art files left from before, keeping keep ("" for none).
static void removeArt(const char* keep) {
	DIR* d = opendir(ART_DIR);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "art-", 4) != 0 || !strcmp(e->d_name, keep)) continue;
		char path[MU_PATH_MAX];
		struct stat sb;
		if (MU_childPath(ART_DIR, e->d_name, path, sizeof(path)) && lstat(path, &sb) == 0 && S_ISREG(sb.st_mode)) {
			unlink(path);
		}
	}
	closedir(d);
}

// On the tag thread: the track's cover art, its embedded picture written
// out to ART_DIR (by a name of its own, kept while it's the one playing),
// or else its folder's cover file. Writes "" for none.
static int trackArt(const char* track, const MuTags* tags, char* out, size_t size) {
	out[0] = '\0';
	struct stat sb;
	if (tags->picture && stat(track, &sb) == 0 && MU_artDir(ART_DIR) == 0) {
		char name[64];
		MU_artName(track, (unsigned long long)sb.st_size, (long long)sb.st_mtime, tags->picture_kind, name, sizeof(name));
		removeArt(name);
		char path[MU_PATH_MAX];
		struct stat ab;
		if (MU_childPath(ART_DIR, name, path, sizeof(path))
			&& ((lstat(path, &ab) == 0 && S_ISREG(ab.st_mode) && (size_t)ab.st_size == tags->picture_size)
				|| MU_writeArt(ART_DIR, name, tags->picture, tags->picture_size) == 0)) {
			snprintf(out, size, "%s", path);
			return 1;
		}
	}
	removeArt("");
	char dir_path[MU_PATH_MAX];
	const char* slash = strrchr(track, '/');
	if (!slash) return 0;
	snprintf(dir_path, sizeof(dir_path), "%.*s", (int)(slash - track), track);
	MU_folderCover(dir_path, out, size);
	return 0;
}

static void* tagWorker(void* data) {
	(void)data;
	pthread_mutex_lock(&tag_lock);
	while (!tag_quit) {
		if (now_pending) {
			char track[MU_PATH_MAX], art[MU_PATH_MAX];
			snprintf(track, sizeof(track), "%s", now_want);
			now_pending = 0;
			pthread_mutex_unlock(&tag_lock);
			MuTags tags;
			art[0] = '\0';
			int embedded = 0;
			if (track[0]) {
				MU_readTagsFile(track, 1, &tags);
				embedded = trackArt(track, &tags, art, sizeof(art));
			} else MuTags_init(&tags);
			pthread_mutex_lock(&tag_lock);
			if (!now_pending) {
				snprintf(now_track, sizeof(now_track), "%s", track);
				snprintf(now_title, sizeof(now_title), "%s", tags.title);
				snprintf(now_artist, sizeof(now_artist), "%s", tags.artist);
				snprintf(now_art, sizeof(now_art), "%s", art);
				now_embedded = embedded;
				now_dirty = 1;
			}
			MuTags_free(&tags);
		} else if (job_next < job_count) {
			int i = job_next++;
			unsigned int gen = job_gen;
			char path[MU_PATH_MAX];
			snprintf(path, sizeof(path), "%s", job_paths[i]);
			pthread_mutex_unlock(&tag_lock);
			MuTags tags;
			MU_readTagsFile(path, 0, &tags);
			pthread_mutex_lock(&tag_lock);
			if (gen == job_gen && labels) {
				snprintf(labels[job_dirs + i].title, sizeof(labels[0].title), "%s", tags.title);
				if (tags.title[0]) job_titled++;
				if (job_next == job_count) job_done = 1;
				labels_dirty = 1;
			}
			MuTags_free(&tags);
		} else pthread_cond_wait(&tag_wake, &tag_lock);
	}
	pthread_mutex_unlock(&tag_lock);
	return NULL;
}

static void startTags(void) {
	if (tag_thread_up) return;
	tag_quit = 0;
	if (pthread_create(&tag_thread, NULL, tagWorker, NULL) == 0) tag_thread_up = 1;
	else say("couldn't start the tag thread; showing file names");
}

static void stopTags(void) {
	if (!tag_thread_up) return;
	pthread_mutex_lock(&tag_lock);
	tag_quit = 1;
	pthread_cond_broadcast(&tag_wake);
	pthread_mutex_unlock(&tag_lock);
	pthread_join(tag_thread, NULL);
	tag_thread_up = 0;
}

// Drops the listing's titles and what's left to read of them.
static void clearJob(void) {
	pthread_mutex_lock(&tag_lock);
	free(job_paths);
	free(labels);
	job_paths = NULL;
	labels = NULL;
	job_count = job_next = job_dirs = job_titled = job_done = 0;
	labels_dirty = 0;
	job_gen++;
	pthread_mutex_unlock(&tag_lock);
}

// Reads the titles of the listing's tracks.
static void readTitles(void) {
	clearJob();
	int tracks = list.count - list.dirs;
	if (tracks <= 0) return;
	startTags();
	if (!tag_thread_up) return;
	Label* l = calloc((size_t)list.count, sizeof(Label));
	char (*paths)[MU_PATH_MAX] = malloc((size_t)tracks * sizeof(*paths));
	if (!l || !paths) {
		free(l);
		free(paths);
		return;
	}
	for (int i = 0; i < tracks; i++) {
		if (!MU_childPath(dir, list.items[list.dirs + i].name, paths[i], MU_PATH_MAX)) paths[i][0] = '\0';
	}
	pthread_mutex_lock(&tag_lock);
	labels = l;
	job_paths = paths;
	job_count = tracks;
	job_dirs = list.dirs;
	pthread_cond_signal(&tag_wake);
	pthread_mutex_unlock(&tag_lock);
}

// Asks for the playing track's tags when it's changed, and takes the
// answers in. Returns 1 when something to show changed.
static int updateTags(void) {
	int changed = 0;
	const char* track = st.state != MU_STOPPED ? st.track : "";
	if (strcmp(track, np_asked) != 0) {
		snprintf(np_asked, sizeof(np_asked), "%s", track);
		np_track[0] = np_title[0] = np_artist[0] = np_art[0] = '\0';
		changed = 1;
		if (track[0]) startTags();
		if (tag_thread_up) {
			pthread_mutex_lock(&tag_lock);
			snprintf(now_want, sizeof(now_want), "%s", track);
			now_pending = 1;
			pthread_cond_signal(&tag_wake);
			pthread_mutex_unlock(&tag_lock);
		}
	}
	if (!tag_thread_up) return changed;
	int got_now = 0, got_list = 0, count = 0, titled = 0;
	pthread_mutex_lock(&tag_lock);
	if (now_dirty) {
		now_dirty = 0;
		if (!strcmp(now_track, np_asked) && now_track[0]) {
			snprintf(np_track, sizeof(np_track), "%s", now_track);
			snprintf(np_title, sizeof(np_title), "%s", now_title);
			snprintf(np_artist, sizeof(np_artist), "%s", now_artist);
			snprintf(np_art, sizeof(np_art), "%s", now_art);
			got_now = 1;
			changed = 1;
		}
	}
	int embedded = now_embedded;
	if (labels_dirty) {
		labels_dirty = 0;
		changed = 1;
		if (job_done == 1) {
			job_done = 2;
			got_list = 1;
			count = job_count;
			titled = job_titled;
		}
	}
	pthread_mutex_unlock(&tag_lock);
	if (got_now) {
		say("tags of %s: title \"%s\", artist \"%s\", art %s%s", np_track, np_title, np_artist, np_art[0] ? np_art : "none",
			embedded ? " (embedded)" : "");
	}
	if (got_list) say("read the tags of %i tracks in %s, %i with titles", count, dir, titled);
	return changed;
}

// The playing track's title (its tag's, or its file name) and artist.
static void nowTitle(char* out, size_t size) {
	if (np_title[0] && !strcmp(np_track, st.track)) snprintf(out, size, "%s", np_title);
	else MU_trackTitle(st.track, out, size);
}

static const char* nowArtist(void) {
	return np_artist[0] && !strcmp(np_track, st.track) ? np_artist : "";
}

// The playing track's art, scaled to size by size once it's loaded, or
// NULL while it loads or when there's none.
static const MosaicSurface* nowArt(int size) {
	if (!np_art[0] || size <= 0 || strcmp(np_track, st.track) != 0 || !MOSAIC_HOST_HAS(host, art_load)) return NULL;
	if (art_scaled && art_scaled_size == size && !strcmp(art_scaled_path, np_art)) return art_scaled;
	if (!strcmp(art_failed, np_art)) return NULL;
	const MosaicSurface* image = NULL;
	int rc = host->art_load(np_art, &image);
	if (rc == MOSAIC_ART_LOADING) return NULL;
	MosaicSurface* scaled = rc == MOSAIC_ART_READY ? host->surface_scale(ID, image, size, size, MOSAIC_FIT_CONTAIN) : NULL;
	if (!scaled) {
		snprintf(art_failed, sizeof(art_failed), "%s", np_art);
		say("can't show the art %s", np_art);
		return NULL;
	}
	if (art_scaled) host->surface_free(ID, art_scaled);
	art_scaled = scaled;
	art_scaled_size = size;
	snprintf(art_scaled_path, sizeof(art_scaled_path), "%s", np_art);
	if (strcmp(art_logged, np_art) != 0) {
		snprintf(art_logged, sizeof(art_logged), "%s", np_art);
		say("showing the art %s", np_art);
	}
	return art_scaled;
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

static int scale(void) {
	int s = host->screen_scale();
	return s < 1 ? 1 : s;
}

static int textCentred(MosaicSurface* screen, int font, const char* text, const MosaicRect* box, int y,
	MosaicColor color) {
	char cut[MU_PATH_MAX];
	int w = host->text_truncate(font, text, box->w, cut, sizeof(cut));
	if (w < 0) return -1;
	return host->draw_text(screen, font, cut, box->x + (box->w - w) / 2, y, color, box->w);
}

static void stateLine(char* out, size_t size) {
	char pos[16], len[16];
	MU_formatTime(st.pos_ms, pos, sizeof(pos));
	MU_formatTime(st.len_ms, len, sizeof(len));
	char repeat[128];
	snprintf(repeat, sizeof(repeat), tr("Repeat %s"), tr(MU_repeatName(st.repeat)));
	snprintf(out, size, "%s  %s / %s%s%s  %s", st.state == MU_PLAYING ? tr("Playing") : tr("Paused"), pos, len,
		st.shuffle ? "  " : "", st.shuffle ? tr("Shuffle") : "", repeat);
}

// the hole the status pill and header widgets leave in the top row
static MosaicRect hole(const MosaicScreenView* view) {
	MosaicRect a = view->status, b = view->header;
	if (b.w <= 0) return a;
	if (a.w <= 0) return b;
	int x1 = a.x < b.x ? a.x : b.x, y1 = a.y < b.y ? a.y : b.y;
	int x2 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
	int y2 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

static void logView(void) {
	char text[sizeof(logged_view)];
	if (list.count <= 0) snprintf(text, sizeof(text), "%s: empty", dir);
	else {
		snprintf(text, sizeof(text), "%s: %i of %i, %s%s", dir, selected + 1, list.count, list.items[selected].name,
			list.items[selected].is_dir ? "/" : "");
	}
	if (strcmp(text, logged_view) == 0) return;
	snprintf(logged_view, sizeof(logged_view), "%s", text);
	say("showing %s", text);
}

static int playerRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	int s = scale();
	int pad = 10 * s, pill = 30 * s, gap = 4 * s;
	int large = host->text_height(MOSAIC_FONT_LARGE), small = host->text_height(MOSAIC_FONT_SMALL);
	MosaicColor bg = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255);
	MosaicColor text = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 255);
	MosaicColor text_on = themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000, 255);
	MosaicColor fg = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255);
	MosaicColor accent = themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, 255);
	MosaicColor hint = themeColor(MOSAIC_COLOR_HINT, 0x999999, 255);
	MosaicRect top = hole(view);

	// the background, around the status pill
	MosaicRect fill = area;
	if (top.w > 0) {
		MosaicRect left = { area.x, area.y, top.x - area.x, top.y + top.h - area.y };
		MosaicRect below = { area.x, top.y + top.h, area.w, area.y + area.h - top.y - top.h };
		MosaicRect right = { top.x + top.w, area.y, area.x + area.w - top.x - top.w, top.y + top.h - area.y };
		MosaicRect above = { top.x, area.y, top.w, top.y - area.y };
		host->fill_rect(screen, &left, bg);
		host->fill_rect(screen, &below, bg);
		if (right.w > 0) host->fill_rect(screen, &right, bg);
		if (above.h > 0) host->fill_rect(screen, &above, bg);
	} else host->fill_rect(screen, &fill, bg);

	// the folder's name in the top row
	const char* slash = strrchr(dir, '/');
	const char* name = strcmp(dir, root) == 0 ? tr("Music") : slash ? slash + 1 : dir;
	int title_max = (top.w > 0 ? top.x : area.x + area.w) - area.x - 3 * pad;
	if (title_max > 0) {
		char cut[MU_NAME_MAX];
		int w = host->text_truncate(MOSAIC_FONT_LARGE, name, title_max, cut, sizeof(cut));
		if (w > 0) {
			MosaicRect rect = { area.x + pad, area.y + pad, w + 2 * pad, pill };
			host->fill_round_rect(screen, &rect, pill / 2, accent);
			host->draw_text(screen, MOSAIC_FONT_LARGE, cut, rect.x + pad, rect.y + (pill - large) / 2, fg, 0);
		}
	}

	// what's playing, above the hints
	int hints_top = area.y + area.h - pad - pill;
	const char* artist = nowArtist();
	int artist_h = artist[0] ? small + gap : 0;
	int panel_h = st.state != MU_STOPPED ? large + small + artist_h + 3 * gap + gap : 0;
	int panel_y = hints_top - gap - panel_h;
	if (panel_h) {
		MosaicRect box = { area.x + pad, panel_y, area.w - 2 * pad, panel_h };
		// the cover art at the left, its room kept while it loads
		if (np_art[0] && !strcmp(np_track, st.track)) {
			const MosaicSurface* art = nowArt(panel_h);
			MosaicRect at = { box.x, panel_y, panel_h, panel_h };
			if (art) host->blit(screen, art, NULL, &at);
			box.x += panel_h + pad;
			box.w -= panel_h + pad;
		}
		char title[MU_TAG_TEXT_MAX], line[128];
		nowTitle(title, sizeof(title));
		int y = panel_y;
		textCentred(screen, MOSAIC_FONT_LARGE, title, &box, y, fg);
		y += large + gap;
		if (artist[0]) {
			textCentred(screen, MOSAIC_FONT_SMALL, artist, &box, y, fg);
			y += small + gap;
		}
		stateLine(line, sizeof(line));
		textCentred(screen, MOSAIC_FONT_SMALL, line, &box, y, hint);
		MosaicRect bar = { box.x, y + small + gap, box.w, gap };
		host->fill_round_rect(screen, &bar, gap / 2, themeColor(MOSAIC_COLOR_HINT, 0x999999, 80));
		if (st.len_ms > 0) {
			long long pos = st.pos_ms > st.len_ms ? st.len_ms : st.pos_ms;
			bar.w = (int)(box.w * pos / st.len_ms);
			if (bar.w > 0) host->fill_round_rect(screen, &bar, gap / 2, accent);
		}
	}

	// the folder's entries
	int list_top = area.y + pad + pill + gap;
	if (top.w > 0 && top.y + top.h + gap > list_top) list_top = top.y + top.h + gap;
	int list_bottom = (panel_h ? panel_y : hints_top) - gap;
	visible_rows = (list_bottom - list_top) / pill;
	if (visible_rows < 1) visible_rows = 1;
	if (list.count <= 0) {
		MosaicRect box = { area.x + pad, list_top, area.w - 2 * pad, list_bottom - list_top };
		char message[MU_PATH_MAX + 256];
		snprintf(message, sizeof(message), tr("No music in %s"), strcmp(dir, root) == 0 ? root + strlen(host->sdcard_path) : name);
		textCentred(screen, MOSAIC_FONT_LARGE, message, &box, list_top + (box.h - large) / 2, text);
	} else {
		if (selected >= list.count) selected = list.count - 1;
		first_row = MU_window(selected, list.count, visible_rows, first_row);
		pthread_mutex_lock(&tag_lock);
		for (int r = 0; r < visible_rows && first_row + r < list.count; r++) {
			int i = first_row + r;
			char label[MU_TAG_TEXT_MAX + 2], cut[MU_TAG_TEXT_MAX + 2];
			if (list.items[i].is_dir) snprintf(label, sizeof(label), "%s/", list.items[i].name);
			else if (labels && labels[i].title[0]) snprintf(label, sizeof(label), "%s", labels[i].title);
			else MU_trackTitle(list.items[i].name, label, sizeof(label));
			int y = list_top + r * pill;
			int max = area.w - 4 * pad;
			int w = host->text_truncate(MOSAIC_FONT_LARGE, label, max, cut, sizeof(cut));
			if (w < 0) continue;
			int playing = !list.items[i].is_dir && st.state != MU_STOPPED;
			if (playing) {
				char path[MU_PATH_MAX];
				playing = MU_childPath(dir, list.items[i].name, path, sizeof(path)) && !strcmp(path, st.track);
			}
			if (i == selected) {
				MosaicRect rect = { area.x + pad, y, w + 2 * pad, pill };
				host->fill_round_rect(screen, &rect, pill / 2, fg);
			}
			host->draw_text(screen, MOSAIC_FONT_LARGE, cut, area.x + 2 * pad, y + (pill - large) / 2,
				i == selected ? text_on : playing ? accent : text, 0);
		}
		pthread_mutex_unlock(&tag_lock);
	}

	if (MOSAIC_HOST_HAS(host, draw_hints)) {
		MosaicHint left[] = { { MOSAIC_BUTTON_B, "BACK" }, { MOSAIC_BUTTON_Y, "LOCK" } };
		int on_dir = list.count > 0 && list.items[selected].is_dir;
		MosaicHint right[] = { { MOSAIC_BUTTON_X, st.state == MU_PLAYING ? "PAUSE" : "PLAY" },
			{ MOSAIC_BUTTON_A, on_dir ? "OPEN" : "PLAY" } };
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, 2, NULL);
		if (host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, list.count > 0 ? 2 : 1, NULL) != 0) {
			host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right + 1, 1, NULL);
		}
	}
	logView();
	return 0;
}

static int lockRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	int s = scale();
	int large = host->text_height(MOSAIC_FONT_LARGE), small = host->text_height(MOSAIC_FONT_SMALL);
	host->fill_rect(screen, &area, mosaic_color(0x000000, 255));
	MosaicColor fg = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, DIM);
	MosaicColor hint = themeColor(MOSAIC_COLOR_HINT, 0x999999, DIM);
	MosaicRect box = { area.x + 10 * s, area.y, area.w - 20 * s, area.h };
	int y = area.y + area.h / 2 - large * 2;
	// the cover art above the clock, dim, when there's room for it
	int art_size = y - 10 * s - (area.y + 10 * s);
	if (art_size > area.w / 3) art_size = area.w / 3;
	if (st.state != MU_STOPPED && art_size >= 32 * s) {
		const MosaicSurface* art = nowArt(art_size);
		MosaicRect at = { area.x + (area.w - art_size) / 2, y - 10 * s - art_size, art_size, art_size };
		if (art && MOSAIC_HOST_HAS(host, blit_alpha)) host->blit_alpha(screen, art, NULL, &at, DIM);
		else if (art) host->blit(screen, art, NULL, &at);
	}
	textCentred(screen, MOSAIC_FONT_LARGE, lock_clock, &box, y, fg);
	y += large * 2;
	if (st.state != MU_STOPPED) {
		char title[MU_TAG_TEXT_MAX], line[128];
		nowTitle(title, sizeof(title));
		textCentred(screen, MOSAIC_FONT_LARGE, title, &box, y, fg);
		y += large + 4 * s;
		const char* artist = nowArtist();
		if (artist[0]) {
			textCentred(screen, MOSAIC_FONT_SMALL, artist, &box, y, fg);
			y += small + 4 * s;
		}
		stateLine(line, sizeof(line));
		textCentred(screen, MOSAIC_FONT_SMALL, line, &box, y, hint);
	}
	textCentred(screen, MOSAIC_FONT_SMALL, "Locked: SELECT then START to unlock", &box,
		area.y + area.h - 10 * s - small, hint);
	return 0;
}

static int overlayRender(MosaicSurface* surface, const MosaicScreenView* view) {
	if (!MuOverlay_visible(&overlay, last_now)) return 0;
	if (view->screen && (!strcmp(view->screen, PLAYER_ID) || !strcmp(view->screen, LOCK_ID))) return 0;
	const MosaicRect area = view->area;
	int s = scale();
	int pad = 10 * s, pill = 30 * s;
	int h = host->text_height(MOSAIC_FONT_SMALL);
	char title[MU_TAG_TEXT_MAX], text[2 * MU_TAG_TEXT_MAX + 16], cut[sizeof(text)];
	nowTitle(title, sizeof(title));
	const char* artist = nowArtist();
	snprintf(text, sizeof(text), "%s: %s%s%s", st.state == MU_PLAYING ? tr("Playing") : tr("Paused"), title, artist[0] ? " - " : "",
		artist);
	int w = host->text_truncate(MOSAIC_FONT_SMALL, text, area.w / 2 - 2 * pad, cut, sizeof(cut));
	if (w <= 0) return 0;
	MosaicRect rect = { area.x + (area.w - w - 2 * pad) / 2, area.y + area.h - pad - pill, w + 2 * pad, pill };
	host->fill_round_rect(surface, &rect, pill / 2, mosaic_color(0x000000, 200));
	host->draw_text(surface, MOSAIC_FONT_SMALL, cut, rect.x + pad, rect.y + (pill - h) / 2,
		themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255), 0);
	if (!overlay_shown) {
		overlay_shown = 1;
		// the whole text, and what fit when it was cut
		say("pill over %s: %s%s%s%s", view->screen ? view->screen : "?", text, strcmp(cut, text) ? " (cut to fit: " : "",
			strcmp(cut, text) ? cut : "", strcmp(cut, text) ? ")" : "");
	}
	return 0;
}

///////////////////////////////////////
// the player

static void listDir(const char* path, const char* select_name) {
	snprintf(dir, sizeof(dir), "%s", path);
	MU_scan(dir, &list, 1);
	readTitles();
	selected = first_row = 0;
	for (int i = 0; select_name && i < list.count; i++) {
		if (!strcmp(list.items[i].name, select_name)) selected = i;
	}
	// the track playing, when it's here
	if (!select_name && st.state != MU_STOPPED) {
		for (int i = list.dirs; i < list.count; i++) {
			char p[MU_PATH_MAX];
			if (MU_childPath(dir, list.items[i].name, p, sizeof(p)) && !strcmp(p, st.track)) selected = i;
		}
	}
}

static int playerEnter(void* data) {
	(void)data;
	player_open = 1;
	logged_view[0] = '\0';
	askStatus(last_now);
	// back in the playing track's folder, when it's under the music folder
	char start[MU_PATH_MAX];
	snprintf(start, sizeof(start), "%s", root);
	size_t root_len = strlen(root);
	if (st.state != MU_STOPPED && !strncmp(st.track, root, root_len) && st.track[root_len] == '/') {
		const char* slash = strrchr(st.track, '/');
		snprintf(start, sizeof(start), "%.*s", (int)(slash - st.track), st.track);
	}
	listDir(start, NULL);
	return 0;
}

static void playerExit(void* data) {
	(void)data;
	player_open = 0;
	clearJob();
	MU_listFree(&list);
}

static int playerInput(const MosaicInput* in, void* data) {
	(void)data;
#define PRESSED(b) (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_##b))
#define REPEATED(b) (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_##b))
	int before = selected;
	if (PRESSED(B)) {
		char up[MU_PATH_MAX];
		if (!MU_parentPath(root, dir, up, sizeof(up))) return MOSAIC_SCREEN_CLOSE;
		const char* slash = strrchr(dir, '/');
		char from[MU_NAME_MAX];
		snprintf(from, sizeof(from), "%s", slash ? slash + 1 : "");
		listDir(up, from);
	} else if (PRESSED(A) && list.count > 0) {
		char path[MU_PATH_MAX];
		if (MU_childPath(dir, list.items[selected].name, path, sizeof(path))) {
			if (list.items[selected].is_dir) listDir(path, NULL);
			else playPath(path);
		}
	} else if (PRESSED(X)) {
		if (st.state != MU_STOPPED) playOrPause();
		else if (list.count > 0 && !list.items[selected].is_dir) {
			char path[MU_PATH_MAX];
			if (MU_childPath(dir, list.items[selected].name, path, sizeof(path))) playPath(path);
		}
	} else if (PRESSED(Y)) {
		if (host->open_screen(LOCK_ID) != 0) say("couldn't open the lock screen");
	} else if (PRESSED(L1)) {
		command(MU_CMD_PREV, NULL);
	} else if (PRESSED(R1)) {
		command(MU_CMD_NEXT, NULL);
	} else if (PRESSED(L2)) {
		command(MU_CMD_SEEK, "-10000");
	} else if (PRESSED(R2)) {
		command(MU_CMD_SEEK, "+10000");
	} else if (PRESSED(SELECT)) {
		shuffle = !shuffle;
		host->cfg_set(ID, "shuffle", shuffle ? "on" : "off");
		say("shuffle %s", shuffle ? "on" : "off");
		restoredSettings();
		command(MU_CMD_SHUFFLE, shuffle ? "on" : "off");
	} else if (PRESSED(START)) {
		repeat = (repeat + 1) % 3;
		host->cfg_set(ID, "repeat", MU_repeatName(repeat));
		say("repeat %s", MU_repeatName(repeat));
		restoredSettings();
		command(MU_CMD_REPEAT, MU_repeatName(repeat));
	} else if (REPEATED(UP)) {
		selected = MU_move(selected, list.count, -1, PRESSED(UP) != 0);
	} else if (REPEATED(DOWN)) {
		selected = MU_move(selected, list.count, 1, PRESSED(DOWN) != 0);
	} else if (REPEATED(LEFT)) {
		selected = MU_move(selected, list.count, -visible_rows, 0);
	} else if (REPEATED(RIGHT)) {
		selected = MU_move(selected, list.count, visible_rows, 0);
	}
	if (in->pressed || selected != before) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
#undef PRESSED
#undef REPEATED
}

static int playerAnimating(void* data) {
	(void)data;
	return 0;
}

///////////////////////////////////////
// the lock screen

static void clockText(char* out, size_t size) {
	time_t t = time(NULL);
	struct tm local;
	if (localtime_r(&t, &local)) strftime(out, size, "%H:%M", &local);
	else out[0] = '\0';
}

static int lockEnter(void* data) {
	(void)data;
	lock_open = 1;
	unlock_due = 0;
	memset(&unlock, 0, sizeof(unlock));
	clockText(lock_clock, sizeof(lock_clock));
	askStatus(last_now);
	say("locked");
	return 0;
}

static void lockExit(void* data) {
	(void)data;
	lock_open = 0;
	unlock_due = 0;
}

// every button is taken in input_filter while it shows
static int lockInput(const MosaicInput* in, void* data) {
	(void)in;
	(void)data;
	return MOSAIC_SCREEN_STAY;
}

static int inputFilter(const MosaicInput* in, unsigned int* consumed) {
	if (!lock_open) return 0;
	unsigned int select_bit = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_SELECT), start_bit = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_START);
	if (MuUnlock_frame(&unlock, in->pressed, in->held, select_bit, start_bit, last_now)) unlock_due = 1;
	*consumed = ~MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);
	return 0;
}

///////////////////////////////////////
// actions

static int alwaysAvailable(void* data) {
	(void)data;
	return 1;
}

static void openPlayer(void* data) {
	(void)data;
	if (host->open_screen(PLAYER_ID) != 0) say("couldn't open the player");
}

static void openLock(void* data) {
	(void)data;
	if (host->open_screen(LOCK_ID) != 0) say("couldn't open the lock screen");
}

static void togglePlaying(void* data) {
	(void)data;
	if (!daemon_up) askStatus(last_now);
	if (st.state == MU_STOPPED) say("nothing to pause or play on");
	else playOrPause();
}

///////////////////////////////////////
// hooks

static void tick(unsigned long now) {
	last_now = now;
	if (restored_pill) {
		restored_pill = 0;
		if (restored && !daemon_up) {
			MuOverlay_show(&overlay, &st, now);
			overlay_shown = 0;
			host->request_redraw();
		}
	}
	if (next_poll && now >= next_poll) {
		int was_visible = MuOverlay_visible(&overlay, now);
		long long pos = st.pos_ms;
		int state = st.state;
		char track[MU_PATH_MAX];
		snprintf(track, sizeof(track), "%s", st.track);
		askStatus(now);
		int changed = st.state != state || strcmp(st.track, track) != 0;
		if (changed || ((player_open || lock_open) && st.pos_ms / 1000 != pos / 1000)) host->request_redraw();
		if (was_visible != MuOverlay_visible(&overlay, now)) host->request_redraw();
	} else if (overlay_shown && !MuOverlay_visible(&overlay, now)) {
		overlay_shown = 0;
		host->request_redraw();
	}
	if (updateTags()) host->request_redraw();
	if (lock_open) {
		char clock[16];
		clockText(clock, sizeof(clock));
		if (strcmp(clock, lock_clock) != 0) {
			snprintf(lock_clock, sizeof(lock_clock), "%s", clock);
			host->request_redraw();
		}
		if (unlock_due) {
			unlock_due = 0;
			if (host->close_screen(LOCK_ID) == 0) say("unlocked");
		}
	}
	int want_awake = awake && st.state == MU_PLAYING;
	if (want_awake != awake_held && MOSAIC_HOST_HAS(host, inhibit_sleep)) {
		awake_held = want_awake;
		host->inhibit_sleep(ID, want_awake);
	}
}

static int preLaunch(const MosaicLaunch* launch) {
	(void)launch;
	if (games_play || !daemon_up || st.state != MU_PLAYING) return MOSAIC_LAUNCH_CONTINUE;
	if (sendCommand(MU_CMD_HOLD, NULL, DEVICE_REQUEST_MS) == 0) say("paused for the launch");
	else say("the daemon didn't answer the pause for the launch");
	return MOSAIC_LAUNCH_CONTINUE;
}

static void postReturn(void) {
	if (games_play) return;
	if (!daemon_up) askStatus(last_now);
	if (daemon_up && st.held && sendCommand(MU_CMD_RELEASE, NULL, DEVICE_REQUEST_MS) == 0) say("playing on after the launch");
	askStatus(last_now);
}

static void beforeSleep(void) {
	if (!daemon_up) return;
	if (sendCommand(MU_CMD_SLEEP, NULL, DEVICE_REQUEST_MS) == 0) say("paused for sleep");
}

static void afterResume(void) {
	askStatus(last_now);
}

// Asks the daemon to quit, or else stops the one whose pid it wrote, by
// that pid.
static void beforePoweroff(void) {
	int pid = daemonPid();
	if (sendCommand(MU_CMD_QUIT, NULL, DEVICE_REQUEST_MS) == 0) {
		say("stopped the daemon for the power-off");
		return;
	}
	if (pid > 1 && kill(pid, SIGTERM) == 0) say("stopped the daemon for the power-off, pid %i", pid);
}

static void findDaemon(void) {
	daemon_path[0] = '\0';
	Dl_info info;
	if (!dladdr((const void*)&findDaemon, &info) || !info.dli_fname) return;
	const char* slash = strrchr(info.dli_fname, '/');
	if (!slash) return;
	snprintf(daemon_path, sizeof(daemon_path), "%.*s/%s", (int)(slash - info.dli_fname), info.dli_fname, DAEMON_NAME);
}

static int init(const MosaicHost* h) {
	host = h;
	daemon_up = 0;
	memset(&st, 0, sizeof(st));
	MU_listInit(&list);
	player_open = lock_open = unlock_due = awake_held = 0;
	next_poll = last_now = 0;
	logged_now[0] = logged_view[0] = '\0';
	np_asked[0] = np_track[0] = np_title[0] = np_artist[0] = np_art[0] = '\0';
	art_scaled = NULL;
	art_scaled_path[0] = art_failed[0] = art_logged[0] = '\0';
	art_scaled_size = 0;
	tag_thread_up = 0;
	// embedded art left from a run before; the playing track's is written again
	removeArt("");
	if (!MOSAIC_HOST_HAS(host, open_screen) || !MOSAIC_HOST_HAS(host, hold_switch) || !MOSAIC_HOST_HAS(host, draw_text)) {
		say("this launcher has no plugin screens, overlays or input filters");
		return 1;
	}
	const char* folder = host->cfg_get(ID, "folder", "Music");
	if (folder[0] == '/') snprintf(root, sizeof(root), "%s", folder);
	else snprintf(root, sizeof(root), "%s/%s", host->sdcard_path, folder);
	size_t root_len = strlen(root);
	while (root_len > 1 && root[root_len - 1] == '/') root[--root_len] = '\0';
	games_play = !strcmp(host->cfg_get(ID, "games", "pause"), "play");
	awake = strcmp(host->cfg_get(ID, "awake", "on"), "off") != 0;
	shuffle = !strcmp(host->cfg_get(ID, "shuffle", "off"), "on");
	repeat = MU_parseRepeat(host->cfg_get(ID, "repeat", "off"));
	const char* overlay_setting = host->cfg_get(ID, "overlay", "5");
	MuOverlay_init(&overlay, MU_parseOverlay(overlay_setting));
	overlay_shown = 0;
	findDaemon();
	snprintf(log_path, sizeof(log_path), "%s/logs", host->userdata_path);
	mkdir(log_path, 0755);
	snprintf(log_path, sizeof(log_path), "%s/logs/music.txt", host->userdata_path);
	restored = restored_pill = 0;
	snprintf(resume_path, sizeof(resume_path), "%s/launcher", host->userdata_path);
	mkdir(resume_path, 0755);
	snprintf(resume_path, sizeof(resume_path), "%s/launcher/" ID, host->userdata_path);
	mkdir(resume_path, 0755);
	snprintf(resume_path, sizeof(resume_path), "%s/launcher/" ID "/" MU_RESUME_NAME, host->userdata_path);
	int resume = strcmp(host->cfg_get(ID, "resume", "on"), "off") != 0;
	if (!resume) {
		// nothing kept to play on from once it's turned back on
		unlink(resume_path);
		resume_path[0] = '\0';
	}

	static const MosaicScreenHooks player = {
		.size = sizeof(MosaicScreenHooks),
		.enter = playerEnter,
		.exit = playerExit,
		.input = playerInput,
		.render = playerRender,
		.animating = playerAnimating,
	};
	static const MosaicScreenHooks lock = {
		.size = sizeof(MosaicScreenHooks),
		.flags = MOSAIC_SCREEN_FULL,
		.enter = lockEnter,
		.exit = lockExit,
		.input = lockInput,
		.render = lockRender,
		.animating = playerAnimating,
	};
	if (host->register_screen(PLAYER_ID, &player) != 0 || host->register_screen(LOCK_ID, &lock) != 0) {
		say("couldn't register its screens");
		return 1;
	}
	if (host->register_action(OPEN_ACTION, "Music", alwaysAvailable, openPlayer, NULL) != 0
		|| host->register_action(LOCK_ACTION, "Lock screen", alwaysAvailable, openLock, NULL) != 0
		|| host->register_action(TOGGLE_ACTION, "Play or pause music", alwaysAvailable, togglePlaying, NULL) != 0) {
		say("couldn't register its actions");
		return 1;
	}
	if (strcmp(host->cfg_get(ID, "tile", "on"), "off") != 0
		&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(OPEN_ACTION, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the Music tile");
	}
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* button = host->cfg_get(ID, "button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(button, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, OPEN_ACTION) != 0) {
			say("couldn't bind the music button");
		}
	}

	say("music from %s, daemon %s, games %s, pill %s, shuffle %s, repeat %s, resume %s", root,
		daemon_path[0] && access(daemon_path, X_OK) == 0 ? daemon_path : "missing", games_play ? "play" : "pause",
		overlay_setting, shuffle ? "on" : "off", MU_repeatName(repeat), resume ? "on" : "off");
	// a daemon from before, such as one playing through a game
	askStatus(0);
	if (!daemon_up) {
		say("the daemon isn't running");
		// what was playing before a power-off
		if (resume_path[0]) readResume();
		logNowPlaying();
	}
	return 0;
}

static void quit(void) {
	// the daemon plays on; the launcher is only going away for a while
	stopTags();
	clearJob();
	MU_listFree(&list);
	if (art_scaled) host->surface_free(ID, art_scaled);
	art_scaled = NULL;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.tick = tick,
	.before_sleep = beforeSleep,
	.after_resume = afterResume,
	.before_poweroff = beforePoweroff,
	.input_filter = inputFilter,
	.overlay_render = overlayRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
