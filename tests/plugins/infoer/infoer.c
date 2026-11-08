// A plugin that reads .info sidecars through entry_info. Its display_name
// names each game in <sd>/Infoer by the name= its sidecar gives, so the
// sidecars reach a display hook. Once the lists are up it opens Infoer and
// logs what each game's sidecar says, then rewrites Alpha's sidecar and
// asks again: the same entry keeps what it read, and a new entry for the
// same game reads the new file. It also logs the refusals for bad
// arguments.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "infoer"

static const MosaicHost* host;
static char folder[512];
static int ticks = 0;
static int named = 0; // display_name calls that found a name

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int inFolder(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	size_t n = strlen(folder);
	return path && !strncmp(path, folder, n) && path[n] == '/';
}

static int displayName(const MosaicEntry* entry, char* out, size_t size) {
	if (!inFolder(entry)) return 0;
	if (host->entry_info(entry, "name", out, size) != 1 || !out[0]) return 0;
	named++;
	return 1;
}

static void logList(void) {
	const MosaicList* list = host->current_list();
	int count = host->list_count(list);
	for (int i = 0; i < count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		char year[16], genre[32], missing[8] = "x";
		int has_year = host->entry_info(entry, "Year", year, sizeof(year));
		host->entry_info(entry, "genre", genre, sizeof(genre));
		int has_missing = host->entry_info(entry, "players", missing, sizeof(missing));
		say("game %s year %i \"%s\" genre \"%s\" players %i \"%s\"", host->entry_name(entry), has_year, year, genre,
			has_missing, missing);
	}
}

static const MosaicEntry* find(const char* name) {
	const MosaicList* list = host->current_list();
	for (int i = 0; i < host->list_count(list); i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		if (!strcmp(host->entry_name(entry), name)) return entry;
	}
	return NULL;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks != 4) return;

	say("named %i", named);
	logList();
	const MosaicEntry* alpha = find("Alpha Prime");
	if (!alpha) {
		say("no Alpha Prime");
		return;
	}
	char value[16];
	char small[4];
	say("bad args %i %i %i %i", host->entry_info(NULL, "year", value, sizeof(value)),
		host->entry_info(alpha, NULL, value, sizeof(value)), host->entry_info(alpha, "year", NULL, 4),
		host->entry_info(alpha, "year", value, 0));
	int cut = host->entry_info(alpha, "name", small, sizeof(small));
	say("cut %i \"%s\"", cut, small);

	// a new sidecar: the entry keeps what it read, a new entry reads it
	char path[700];
	snprintf(path, sizeof(path), "%s/.media/Alpha.info", folder);
	FILE* f = fopen(path, "w");
	if (f) {
		fputs("year=2000\n", f);
		fclose(f);
	}
	host->entry_info(alpha, "year", value, sizeof(value));
	say("kept %s", value);
	MosaicEntry* fresh = host->entry_new(host->entry_path(alpha), MOSAIC_ENTRY_ROM, NULL); // left for the run
	host->entry_info(fresh, "year", value, sizeof(value));
	say("fresh %s", value);
	say("done");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, entry_info)) return 1;
	snprintf(folder, sizeof(folder), "%s/Infoer", host->sdcard_path);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.display_name = displayName,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
