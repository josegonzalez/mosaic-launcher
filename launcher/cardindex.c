#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "cardindex.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char sdcard[1024];
static char platform[64];
static char file[1024];
static void (*log_fn)(const char* message) = NULL;

// the main thread's
static CardIndex* raw = NULL;     // what was read from the card, which the worker checks and keeps
static CardIndex* current = NULL; // what plugins are answered from: raw, or the view made from it
static int asked = 0;             // a plugin asked: the worker has started at least once
static CardIndex* (*view_fn)(const CardIndex* raw) = NULL;
static int viewing = 0;           // in view_fn, which mustn't make another

// shared with the worker, under the lock
static pthread_t worker;
static int running = 0;           // the worker is running or not yet joined
static int finished = 0;          // it's done and waits to be joined
static CardIndex* built = NULL;   // what it built that's worth keeping, for CardIndex_poll
static int built_changed = 0;     // and whether its entries changed
static int again = 0;             // a refresh came while it ran
static volatile int cancel = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	if (!log_fn) return;
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	log_fn(message);
}

static double nowMs(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

void CardIndex_setView(CardIndex* (*view)(const CardIndex* raw)) {
	view_fn = view;
}

static void freeIndex(CardIndex* index) {
	if (!index) return;
	Index_free(index);
	free(index);
}

// Makes current again from raw, through view_fn.
static void makeView(void) {
	if (current != raw) freeIndex(current);
	current = raw;
	if (!raw || !view_fn || viewing) return;
	viewing = 1;
	CardIndex* view = view_fn(raw);
	viewing = 0;
	if (view) current = view;
}

void CardIndex_setup(const char* card, const char* plat, const char* path, void (*log)(const char* message)) {
	snprintf(sdcard, sizeof(sdcard), "%s", card ? card : "");
	snprintf(platform, sizeof(platform), "%s", plat ? plat : "");
	snprintf(file, sizeof(file), "%s", path ? path : "");
	log_fn = log;
}

static void* work(void* arg) {
	const CardIndex* prev = arg; // the main thread's raw, which it keeps until this is joined
	IndexCard card = { sdcard, platform };
	CardIndex* next = malloc(sizeof(CardIndex));
	IndexBuildStats stats;
	double start = nowMs();
	int ok = next && Index_build(next, &card, prev, &cancel, &stats) == 0;
	int keep = ok && (stats.changed || stats.restamped);
	if (ok) {
		double took = nowMs() - start;
		if (stats.changed) {
			say("index: %d entries in %d systems, %d read from the card and %d kept, in %.1f ms, %zu bytes\n", next->total,
				next->system_count, stats.systems_read, stats.systems_kept, took, Index_bytes(next));
		}
		else say("index: the card hasn't changed, %d systems read again, checked in %.1f ms\n", stats.systems_read, took);
		if (keep) {
			start = nowMs();
			if (file[0] && Index_save(next, file) != 0) say("index: couldn't save %s\n", file);
			else if (file[0]) say("index: saved in %.1f ms\n", nowMs() - start);
		}
	}
	else if (!cancel) say("index: couldn't read the card\n");
	if (next && !keep) {
		if (ok) Index_free(next);
		free(next);
		next = NULL;
	}
	pthread_mutex_lock(&lock);
	if (built) { // never left there, as poll joins first; just in case
		Index_free(built);
		free(built);
	}
	built = next;
	built_changed = ok && stats.changed;
	finished = 1;
	pthread_mutex_unlock(&lock);
	return NULL;
}

// With the lock held.
static void start(void) {
	if (running) {
		again = 1;
		return;
	}
	cancel = 0;
	finished = 0;
	again = 0;
	if (pthread_create(&worker, NULL, work, raw) != 0) {
		say("index: couldn't start its thread\n");
		return;
	}
	running = 1;
}

const CardIndex* CardIndex_get(void) {
	if (!asked) {
		asked = 1;
		char roms[1100];
		snprintf(roms, sizeof(roms), "%s/Roms", sdcard);
		CardIndex* cached = malloc(sizeof(CardIndex));
		double began = nowMs();
		if (cached && file[0] && Index_load(cached, file, roms) == 0) {
			raw = cached;
			makeView();
			say("index: %d entries read from %s in %.1f ms, checking the card\n", cached->total, file, nowMs() - began);
		}
		else {
			free(cached);
			say("index: nothing kept on the card, reading it\n");
		}
		pthread_mutex_lock(&lock);
		start();
		pthread_mutex_unlock(&lock);
	}
	return current;
}

void CardIndex_refresh(void) {
	if (!asked) return;
	pthread_mutex_lock(&lock);
	start();
	pthread_mutex_unlock(&lock);
}

int CardIndex_poll(void) {
	if (!asked) return 0;
	pthread_mutex_lock(&lock);
	int done = running && finished;
	CardIndex* next = built;
	int changed = built_changed;
	if (done) built = NULL;
	pthread_mutex_unlock(&lock);
	if (!done) return 0;
	pthread_join(worker, NULL);
	pthread_mutex_lock(&lock);
	running = 0;
	finished = 0;
	pthread_mutex_unlock(&lock);
	if (next) {
		if (current != raw) freeIndex(current);
		freeIndex(raw);
		raw = current = next;
		makeView();
	}
	pthread_mutex_lock(&lock);
	if (again) start();
	pthread_mutex_unlock(&lock);
	return next != NULL && changed;
}

void CardIndex_quit(void) {
	pthread_mutex_lock(&lock);
	int was = running;
	cancel = 1;
	pthread_mutex_unlock(&lock);
	if (was) pthread_join(worker, NULL);
	pthread_mutex_lock(&lock);
	running = 0;
	finished = 0;
	again = 0;
	if (built) {
		Index_free(built);
		free(built);
		built = NULL;
	}
	pthread_mutex_unlock(&lock);
	if (current != raw) freeIndex(current);
	freeIndex(raw);
	raw = current = NULL;
	asked = 0;
}
