// musicd: the music plugin's daemon. It plays a queue of tracks through
// SDL's audio queue, outside the launcher, so the music goes on while the
// launcher exits for a game, and takes one-line commands on a Unix socket
// (music_model.h has the protocol). The plugin starts it by its path and
// is its front end; nothing else talks to it.
//
// One thread: it waits on the socket, a little at a time while playing,
// and keeps about a quarter of a second decoded ahead in SDL's queue. The
// audio device, and SDL's audio with it, is open only while something
// plays: a pause closes it after a while, and hold (before a game) and
// sleep (before the device sleeps) close it at once, so a game's own audio
// can have the device. Stopped for 10 minutes with no command, it exits.
//
// Only one runs: a second one finds the first answering on the socket and
// exits. It writes its pid to MU_PID_PATH, and removes that and the socket
// when it exits, on quit, SIGTERM, SIGINT or SIGHUP. It logs to stderr,
// which the plugin points at a log file.

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "decoder.h"
#include "music_model.h"

#define AHEAD_MS 250            // decoded ahead in SDL's queue
#define CHUNK_FRAMES 2048       // decoded at a time
#define PLAYING_WAIT_MS 10      // the socket's wait while playing
#define IDLE_WAIT_MS 500        // and otherwise
#define PAUSE_CLOSE_MS 10000    // a pause closes the device after this
#define IDLE_EXIT_MS 600000     // stopped this long with no command, it exits
#define RESTART_MS 3000         // prev past this goes back to the track's start
#define CLIENT_TIMEOUT_MS 500

static volatile sig_atomic_t quitting = 0;
static MuQueue queue;
static MuDecoder decoder;
static int state = MU_STOPPED;
static int held = 0;
static SDL_AudioDeviceID device = 0;
static unsigned int device_rate = 0, device_channels = 0;
static unsigned long long base_frame = 0; // where decoding started, after an open or seek
static unsigned long long fed = 0;        // frames queued since then
static int track_done = 0;                // the decoder is at the end
static unsigned long paused_at = 0, last_command = 0;
static short chunk[CHUNK_FRAMES * 2];

static unsigned long nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000 + (unsigned long)(ts.tv_nsec / 1000000);
}

static void say(const char* format, ...) {
	char text[MU_LINE_MAX];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	time_t t = time(NULL);
	struct tm local;
	char when[32] = "";
	if (localtime_r(&t, &local)) strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
	fprintf(stderr, "%s musicd: %s\n", when, text);
	fflush(stderr);
}

static void onSignal(int sig) {
	(void)sig;
	quitting = 1;
}

///////////////////////////////////////
// the audio device

static void closeDevice(void) {
	if (!device) return;
	SDL_CloseAudioDevice(device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	device = 0;
	device_rate = device_channels = 0;
}

static int openDevice(unsigned int rate, unsigned int channels) {
	if (device && device_rate == rate && device_channels == channels) return 0;
	closeDevice();
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		say("can't start SDL's audio: %s", SDL_GetError());
		return -1;
	}
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = (int)rate;
	want.format = AUDIO_S16SYS;
	want.channels = (Uint8)channels;
	want.samples = 1024;
	device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!device) {
		say("can't open the audio device: %s", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return -1;
	}
	device_rate = rate;
	device_channels = channels;
	return 0;
}

static unsigned int frameBytes(void) {
	return decoder.channels * (unsigned int)sizeof(short);
}

static unsigned long long queuedFrames(void) {
	return device && decoder.kind ? SDL_GetQueuedAudioSize(device) / frameBytes() : 0;
}

// The frame playing now.
static unsigned long long playedFrame(void) {
	unsigned long long queued = queuedFrames();
	return base_frame + (fed > queued ? fed - queued : 0);
}

static long long frameMs(unsigned long long frame) {
	return decoder.rate ? (long long)(frame * 1000 / decoder.rate) : 0;
}

// Drops what's queued, carrying on decoding from frame.
static void restartAt(unsigned long long frame) {
	if (device) SDL_ClearQueuedAudio(device);
	if (decoder.frames && frame > decoder.frames) frame = decoder.frames;
	if (decoder.kind && MuDecoder_seek(&decoder, frame) != 0) say("can't seek to %llu", frame);
	base_frame = frame;
	fed = 0;
	track_done = 0;
}

// Closes the device, keeping the place, for a pause, hold or sleep.
static void letGo(const char* why) {
	if (!device) return;
	restartAt(playedFrame());
	closeDevice();
	say("closed the audio device: %s", why);
}

///////////////////////////////////////
// playing

static void stopPlaying(const char* why) {
	MuDecoder_close(&decoder);
	closeDevice();
	MuQueue_free(&queue);
	state = MU_STOPPED;
	held = 0;
	base_frame = fed = 0;
	track_done = 0;
	say("stopped: %s", why);
}

// Opens the queue's current track and plays it, or the next one that
// opens. Returns 0, or -1 having stopped.
static int playCurrent(void) {
	for (int tries = 0; tries < queue.count; tries++) {
		int index = MuQueue_current(&queue);
		if (index < 0) break;
		const char* path = queue.paths[index];
		MuDecoder_close(&decoder);
		if (MuDecoder_open(&decoder, path) != 0) {
			say("can't decode %s", path);
		} else if (decoder.channels > 2) {
			say("can't play %s: %u channels", path, decoder.channels);
			MuDecoder_close(&decoder);
		} else if (openDevice(decoder.rate, decoder.channels) != 0) {
			MuDecoder_close(&decoder);
			stopPlaying("no audio device");
			return -1;
		} else {
			if (device) SDL_ClearQueuedAudio(device);
			base_frame = fed = 0;
			track_done = 0;
			state = MU_PLAYING;
			held = 0;
			SDL_PauseAudioDevice(device, 0);
			char length[16];
			MU_formatTime(frameMs(decoder.frames), length, sizeof(length));
			say("playing %s (%u Hz, %u channels, %s), %i of %i", path, decoder.rate, decoder.channels, length,
				queue.pos + 1, queue.count);
			return 0;
		}
		if (MuQueue_next(&queue, 1) < 0) break;
	}
	stopPlaying("nothing in the queue would play");
	return -1;
}

static void feed(void) {
	if (!device || !decoder.kind) return;
	unsigned int bytes = frameBytes();
	unsigned long long ahead = (unsigned long long)decoder.rate * AHEAD_MS / 1000;
	while (!track_done && queuedFrames() < ahead) {
		unsigned long long n = MuDecoder_read(&decoder, chunk, CHUNK_FRAMES);
		if (!n) {
			track_done = 1;
			break;
		}
		if (SDL_QueueAudio(device, chunk, (Uint32)(n * bytes)) != 0) {
			say("can't queue audio: %s", SDL_GetError());
			break;
		}
		fed += n;
	}
	if (track_done && queuedFrames() == 0) {
		if (MuQueue_next(&queue, 0) < 0) stopPlaying("the end of the queue");
		else playCurrent();
	}
}

static void pausePlaying(void) {
	if (state != MU_PLAYING) return;
	if (device) SDL_PauseAudioDevice(device, 1);
	state = MU_PAUSED;
	paused_at = nowMs();
	say("paused at %lld ms", frameMs(playedFrame()));
}

static int resumePlaying(void) {
	if (state != MU_PAUSED) return state == MU_PLAYING ? 0 : -1;
	if (openDevice(decoder.rate, decoder.channels) != 0) return -1;
	SDL_PauseAudioDevice(device, 0);
	state = MU_PLAYING;
	held = 0;
	say("playing on at %lld ms", frameMs(playedFrame()));
	return 0;
}

// play <path>: a track and the rest of its folder after it, or a folder
// from its first track.
static int playPath(const char* path, char* error, size_t size) {
	struct stat st;
	size_t len = strlen(path);
	if (len >= MU_PATH_MAX) {
		snprintf(error, size, "path too long");
		return -1;
	}
	if (!len || stat(path, &st) != 0) {
		snprintf(error, size, "no such file");
		return -1;
	}
	char dir[MU_PATH_MAX];
	const char* name = NULL;
	if (!S_ISDIR(st.st_mode)) {
		const char* slash = strrchr(path, '/');
		if (!slash || slash == path) {
			snprintf(error, size, "not a full path");
			return -1;
		}
		len = (size_t)(slash - path);
		name = slash + 1;
	}
	memcpy(dir, path, len);
	dir[len] = '\0';
	MuList list;
	MU_listInit(&list);
	MU_scan(dir, &list, 0);
	int start = list.count ? 0 : -1;
	if (name) {
		start = -1;
		for (int i = 0; i < list.count; i++) {
			if (!strcmp(list.items[i].name, name)) start = i;
		}
	}
	int rc = start < 0 ? -1 : MuQueue_set(&queue, dir, &list, start);
	MU_listFree(&list);
	if (rc != 0) {
		snprintf(error, size, name ? "not a track" : "no tracks");
		return -1;
	}
	if (playCurrent() != 0) {
		snprintf(error, size, "it wouldn't play");
		return -1;
	}
	return 0;
}

static void status(MuStatus* s) {
	memset(s, 0, sizeof(*s));
	s->state = state;
	s->shuffle = queue.shuffle;
	s->repeat = queue.repeat;
	s->held = held;
	if (state == MU_STOPPED) return;
	s->index = queue.pos;
	s->count = queue.count;
	s->pos_ms = frameMs(playedFrame());
	s->len_ms = frameMs(decoder.frames);
	int index = MuQueue_current(&queue);
	if (index >= 0) snprintf(s->track, sizeof(s->track), "%s", queue.paths[index]);
}

///////////////////////////////////////
// commands

// Runs one command, writing the reply line. Returns 0, or 1 to quit.
static int run(const char* line, char* reply, size_t size) {
	char arg[MU_LINE_MAX], error[64] = "";
	int command = MU_parseCommand(line, arg, sizeof(arg));
	last_command = nowMs();
	int playing = state == MU_PLAYING;
	switch (command) {
	case MU_CMD_STATUS: {
		MuStatus s;
		status(&s);
		if (MU_formatStatus(&s, reply, size) < 0) snprintf(reply, size, "error status too long\n");
		return 0;
	}
	case MU_CMD_PLAY:
		playPath(arg, error, sizeof(error));
		break;
	case MU_CMD_PAUSE:
		pausePlaying();
		break;
	case MU_CMD_RESUME:
		if (resumePlaying() != 0) snprintf(error, sizeof(error), "nothing to play on");
		break;
	case MU_CMD_TOGGLE:
		if (playing) pausePlaying();
		else if (resumePlaying() != 0) snprintf(error, sizeof(error), "nothing to play on");
		break;
	case MU_CMD_NEXT:
		if (state == MU_STOPPED) snprintf(error, sizeof(error), "nothing playing");
		else if (MuQueue_next(&queue, 1) < 0) stopPlaying("next at the end of the queue");
		else playCurrent();
		break;
	case MU_CMD_PREV:
		if (state == MU_STOPPED) snprintf(error, sizeof(error), "nothing playing");
		else if (frameMs(playedFrame()) > RESTART_MS) restartAt(0);
		else {
			MuQueue_prev(&queue);
			playCurrent();
		}
		break;
	case MU_CMD_STOP:
		if (state != MU_STOPPED) stopPlaying("asked to");
		break;
	case MU_CMD_SEEK: {
		long long to;
		if (state == MU_STOPPED) snprintf(error, sizeof(error), "nothing playing");
		else if (!MU_parseSeek(arg, frameMs(playedFrame()), frameMs(decoder.frames), &to)) {
			snprintf(error, sizeof(error), "bad seek");
		} else restartAt((unsigned long long)to * decoder.rate / 1000);
		break;
	}
	case MU_CMD_SHUFFLE:
		MuQueue_setShuffle(&queue, !strcmp(arg, "on"));
		say("shuffle %s", queue.shuffle ? "on" : "off");
		break;
	case MU_CMD_REPEAT:
		queue.repeat = MU_parseRepeat(arg);
		say("repeat %s", MU_repeatName(queue.repeat));
		break;
	case MU_CMD_HOLD:
		if (playing) {
			pausePlaying();
			held = 1;
			letGo("held for a game");
		}
		break;
	case MU_CMD_RELEASE:
		if (held && state == MU_PAUSED) {
			held = 0;
			if (resumePlaying() != 0) snprintf(error, sizeof(error), "can't play on");
		}
		held = 0;
		break;
	case MU_CMD_SLEEP:
		pausePlaying();
		held = 0;
		letGo("the device is going to sleep");
		break;
	case MU_CMD_QUIT:
		snprintf(reply, size, "ok\n");
		return 1;
	default:
		snprintf(error, sizeof(error), "unknown command");
		break;
	}
	if (error[0]) snprintf(reply, size, "error %s\n", error);
	else snprintf(reply, size, "ok\n");
	return 0;
}

// Reads one line from a client, answers it and closes it. Returns 1 to quit.
static int serve(int listener) {
	int client = accept(listener, NULL, NULL);
	if (client < 0) return 0;
	// a BSD accept takes the listener's O_NONBLOCK
	int flags = fcntl(client, F_GETFL);
	if (flags >= 0) fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
	struct timeval tv = { 0, CLIENT_TIMEOUT_MS * 1000 };
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	char line[MU_LINE_MAX];
	size_t len = 0;
	while (len < sizeof(line) - 1) {
		ssize_t n = read(client, line + len, sizeof(line) - 1 - len);
		if (n <= 0) break;
		len += (size_t)n;
		if (memchr(line, '\n', len)) break;
	}
	line[len] = '\0';
	char* end = strchr(line, '\n');
	int quit = 0;
	if (end) {
		*end = '\0';
		char reply[MU_LINE_MAX + 64];
		quit = run(line, reply, sizeof(reply));
		ssize_t ignored = write(client, reply, strlen(reply));
		(void)ignored;
	}
	close(client);
	return quit;
}

///////////////////////////////////////
// starting and stopping

static int connectExisting(void) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return 0;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", MU_SOCKET_PATH);
	int ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
	close(fd);
	return ok;
}

static int listenOn(void) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", MU_SOCKET_PATH);
	unlink(MU_SOCKET_PATH);
	mode_t old = umask(077);
	int rc = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	umask(old);
	if (rc != 0 || listen(fd, 8) != 0) {
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

static void writePid(void) {
	char tmp[] = MU_PID_PATH ".tmp";
	FILE* f = fopen(tmp, "w");
	if (!f) return;
	fprintf(f, "%ld\n", (long)getpid());
	if (fclose(f) == 0) rename(tmp, MU_PID_PATH);
	else unlink(tmp);
}

// Removes the pid file only while it's still this daemon's.
static void removePid(void) {
	FILE* f = fopen(MU_PID_PATH, "r");
	if (!f) return;
	long pid = 0;
	int ok = fscanf(f, "%ld", &pid) == 1;
	fclose(f);
	if (ok && pid == (long)getpid()) unlink(MU_PID_PATH);
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	signal(SIGPIPE, SIG_IGN);
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = onSignal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	if (connectExisting()) {
		say("another is already running");
		return 0;
	}
	int listener = listenOn();
	if (listener < 0) {
		say("can't listen on %s: %s", MU_SOCKET_PATH, strerror(errno));
		return 1;
	}
	writePid();
	SDL_SetMainReady();
	MuQueue_init(&queue, (unsigned int)time(NULL));
	last_command = nowMs();
	say("started, pid %ld, on %s", (long)getpid(), MU_SOCKET_PATH);

	while (!quitting) {
		struct pollfd p = { listener, POLLIN, 0 };
		int ready = poll(&p, 1, state == MU_PLAYING ? PLAYING_WAIT_MS : IDLE_WAIT_MS);
		if (ready > 0 && (p.revents & POLLIN) && serve(listener)) break;
		unsigned long now = nowMs();
		if (state == MU_PLAYING) feed();
		else if (state == MU_PAUSED && device && now - paused_at >= PAUSE_CLOSE_MS) letGo("paused a while");
		else if (state == MU_STOPPED && now - last_command >= IDLE_EXIT_MS) {
			say("stopped for %i minutes", IDLE_EXIT_MS / 60000);
			break;
		}
	}

	MuDecoder_close(&decoder);
	closeDevice();
	MuQueue_free(&queue);
	close(listener);
	unlink(MU_SOCKET_PATH);
	removePid();
	SDL_Quit();
	say("quit");
	return 0;
}
