// Owns the strings slot: translates a few of the launcher's strings and its
// own into French, logging each ask, and on its first tick logs what
// host->tr and host->tr_plural give it back. Its action, on Y, has a label
// of its own, which the game list's hint shows translated. A few of the
// Launcher pak's settings strings, and its own name, setting label and
// options, are translated too, as are Recently Played, the Game Boy system
// and a few of the bundled quick-menu-plus's and theme-plus's strings.
//
//   mode=bad    answers OPEN with text that isn't UTF-8, an error after which
//               the launcher shows English until it restarts
//   mode=crash  crashes when it's first asked for a translation
//   mode=root   logs the root list's names and paths and the Game Boy
//               row's art, then runs quick-menu-plus.awake (a toast) and
//               theme-plus.pick (a choose), and logs "root done"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

static const MosaicHost* host;
static int bad = 0;
static int crash = 0;
static int root = 0;
static int ticked = 0;

static void say(const char* format, const char* a, const char* b) {
	char message[512];
	snprintf(message, sizeof(message), format, a, b);
	host->log("sayer", message);
}

static const char* translate(const char* context, const char* text) {
	say("asked %s %s", context ? context : "-", text);
	if (crash) {
		say("crashing%s%s", "", "");
		abort();
	}
	if (!context) {
		if (!strcmp(text, "OPEN")) return bad ? "OUV\xff" : "OUVRIR";
		if (!strcmp(text, "BACK")) return "RETOUR";
		if (!strcmp(text, "SLEEP")) return "VEILLE";
		if (!strcmp(text, "INFO")) return "INFOS";
		if (!strcmp(text, "%s done")) return "%s fini";
		if (!strcmp(text, "Plugins")) return "Extensions";
		if (!strcmp(text, "Language")) return "Langue";
		if (!strcmp(text, "Status")) return "Statut";
		if (!strcmp(text, "Recently Played")) return "Joués récemment";
		return NULL;
	}
	if (!strcmp(context, "sayer")) {
		if (!strcmp(text, "Sayer")) return "Diseur";
		if (!strcmp(text, "Greeting")) return "Salutation";
		if (!strcmp(text, "Hello")) return "Bonjour";
		if (!strcmp(text, "Open")) return "Ouvert";
		if (!strcmp(text, "SHOUT")) return "CRIER";
		if (!strcmp(text, "%d games")) return "%s jeux"; // refused: not the same conversions
		return NULL;
	}
	if (!strcmp(context, "sayer.button") && !strcmp(text, "Open")) return "Ouvrir";
	if (!strcmp(context, "systems") && !strcmp(text, "Game Boy")) return "Vieille Game Boy"; // sorts after Pocket Colour, but stays before it
	if (!strcmp(context, "quick-menu-plus") && !strcmp(text, "Staying awake")) return "Reste éveillé";
	if (!strcmp(context, "theme-plus")) {
		if (!strcmp(text, "Theme")) return "Thème";
		if (!strcmp(text, "Stock")) return "D'origine";
	}
	return NULL;
}

// French: 0 and 1 are singular.
static const char* translatePlural(const char* context, const char* singular, const char* plural, unsigned long n) {
	char count[32];
	snprintf(count, sizeof(count), "%lu", n);
	say("asked plural %s %s", singular, count);
	(void)plural;
	if (!context || strcmp(context, "sayer") || strcmp(singular, "%d game")) return NULL;
	return n <= 1 ? "%d jeu" : "%d jeux";
}

static void shout(void* data) {
	(void)data;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// mode=root, a step each tick: the root's rows once it's listed, the Game
// Boy row's art once it's in, then the toast and the choose a while apart.
static int root_step = 0;
static unsigned long root_at = 0;
static int art_asks = 0;

static void rootTick(unsigned long now) {
	const char* path = host->current_path();
	const MosaicList* list = host->current_list();
	if (root_step == 0) {
		if (!path || strcmp(path, host->sdcard_path) || !list || host->list_count(list) == 0) return;
		for (int i = 0; i < host->list_count(list); i++) {
			const MosaicEntry* e = host->list_get(list, i);
			say("root row %s at %s", host->entry_name(e), host->entry_path(e));
		}
		root_step = 1;
	}
	if (root_step == 1) {
		const MosaicEntry* gb = NULL;
		for (int i = 0; list && i < host->list_count(list); i++) {
			const MosaicEntry* e = host->list_get(list, i);
			if (endsWith(host->entry_path(e), "/Roms/Game Boy (GB)")) gb = e;
		}
		if (!gb) {
			say("root has no Game Boy row%s%s", "", "");
			root_step = 2;
			root_at = now;
			return;
		}
		char art[1024] = "";
		host->art_find(gb, MOSAIC_ART_THUMBNAIL, art, sizeof(art));
		const MosaicSurface* image = NULL;
		int got = host->art_get(gb, MOSAIC_ART_THUMBNAIL, &image);
		if (got == MOSAIC_ART_LOADING && ++art_asks < 300) return;
		char line[1200];
		snprintf(line, sizeof(line), "%s, %s", got == MOSAIC_ART_READY ? "ready" : got == MOSAIC_ART_LOADING ? "loading" : "missing", art);
		say("root art for %s: %s", host->entry_name(gb), line);
		root_step = 2;
		root_at = now;
	}
	if (root_step == 2 && now - root_at >= 500) {
		say("ran quick-menu-plus.awake %s%s", host->run_action("quick-menu-plus.awake") == 0 ? "0" : "-1", "");
		root_step = 3;
		root_at = now;
	}
	if (root_step == 3 && now - root_at >= 1000) {
		say("ran theme-plus.pick %s%s", host->run_action("theme-plus.pick") == 0 ? "0" : "-1", "");
		root_step = 4;
		root_at = now;
	}
	if (root_step == 4 && now - root_at >= 1000) {
		say("root done%s%s", "", "");
		root_step = 5;
	}
}

static void tick(unsigned long now) {
	if (root) rootTick(now);
	if (ticked++) return;
	say("tr Hello = %s%s", host->tr("sayer", NULL, "Hello"), "");
	say("tr Open = %s, button %s", host->tr("sayer", NULL, "Open"), host->tr("sayer", "sayer.button", "Open"));
	say("tr Missing = %s%s", host->tr("sayer", NULL, "Missing"), "");
	say("tr refused = %s%s", host->tr("sayer", NULL, "%d games"), "");
	say("plural 0 = %s, 2 = %s", host->tr_plural("sayer", NULL, "%d game", "%d games", 0),
		host->tr_plural("sayer", NULL, "%d game", "%d games", 2));
	say("plural other 0 = %s%s", host->tr_plural("sayer", NULL, "%d dir", "%d dirs", 0), "");
	say("launcher OPEN = %s%s", host->tr("sayer", "", "OPEN"), "");
	say("done%s%s", "", "");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, tr)) {
		host->log("sayer", "no tr");
		return 1;
	}
	bad = !strcmp(host->cfg_get("sayer", "mode", ""), "bad");
	crash = !strcmp(host->cfg_get("sayer", "mode", ""), "crash");
	root = !strcmp(host->cfg_get("sayer", "mode", ""), "root");
	// from init, before the slot is settled: English
	say("init tr Hello = %s%s", host->tr("sayer", NULL, "Hello"), "");
	host->register_action("sayer.shout", "SHOUT", NULL, shout, NULL);
	host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "sayer.shout");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = "sayer",
	.init = init,
	.tick = tick,
	.translate = translate,
	.translate_plural = translatePlural,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
