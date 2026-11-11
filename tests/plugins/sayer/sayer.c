// Owns the strings slot: translates a few of the launcher's strings and its
// own into French, logging each ask, and on its first tick logs what
// host->tr and host->tr_plural give it back. Its action, on Y, has a label
// of its own, which the game list's hint shows translated. A few of the
// Launcher pak's settings strings, and its own name, setting label and
// options, are translated too.
//
//   mode=bad    answers OPEN with text that isn't UTF-8, an error after which
//               the launcher shows English until it restarts
//   mode=crash  crashes when it's first asked for a translation

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

static const MosaicHost* host;
static int bad = 0;
static int crash = 0;
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

static void tick(unsigned long now) {
	(void)now;
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
