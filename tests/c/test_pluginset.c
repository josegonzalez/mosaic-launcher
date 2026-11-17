// Which plugins load, and in what order.

#include <stdio.h>
#include <stdlib.h>
#include "check.h"
#include "pluginset.h"

static PluginManifest manifests[8];
static PluginDecision out[8];
static int count;

static void add(const char* text, int bundled) {
	PluginManifest* m = &manifests[count++];
	m->bundled = bundled;
	snprintf(m->dir, sizeof(m->dir), "/plugins/%d", count);
	CHECK(Manifest_parse(text, m) == 0);
}

static void reset(void) {
	count = 0;
}

static const PluginDecision* decision(int n, const char* id) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(out[i].manifest->id, id)) return &out[i];
	}
	return NULL;
}

// The loading plugins' ids, in load order, each followed by ;
static const char* order(int n) {
	static char ids[256];
	ids[0] = '\0';
	for (int i = 0; i < n; i++) {
		if (out[i].load) snprintf(ids + strlen(ids), sizeof(ids) - strlen(ids), "%s;", out[i].manifest->id);
	}
	return ids;
}

static int indexOf(int n, const char* id) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(out[i].manifest->id, id)) return i;
	}
	return -1;
}

static int loads(int n, const char* id) {
	const PluginDecision* d = decision(n, id);
	return d && d->load;
}

// What the parser warned about, each followed by a newline.
static char warned[8192];
static int warnings;
static void warning(const char* message) {
	snprintf(warned + strlen(warned), sizeof(warned) - strlen(warned), "%s\n", message);
	warnings++;
}
static void unwarn(void) {
	warned[0] = '\0';
	warnings = 0;
}

// Manifest_strings' texts, each followed by ;
static char strings[2048];
static void append(const char* text, void* data) {
	(void)data;
	snprintf(strings + strlen(strings), sizeof(strings) - strlen(strings), "%s;", text);
}

int main(void) {
	// manifest parsing
	PluginManifest m = { .bundled = 1 };
	snprintf(m.dir, sizeof(m.dir), "/x");
	CHECK(Manifest_parse("# a plugin\n id = favorites \r\nname=Favorites\napi=1\nslots=root\nrequires=a, b\n", &m) == 0);
	CHECK_STR(m.id, "favorites");
	CHECK_STR(m.name, "Favorites");
	CHECK(m.api == 1);
	CHECK_STR(m.dir, "/x"); // kept
	CHECK(m.bundled == 1);  // kept
	CHECK(csvHas(m.requires, "b"));
	CHECK(!csvHas(m.requires, "c"));
	CHECK(Manifest_parse("name=No Id\n", &m) == -1);
	CHECK(Manifest_parse("id=solo\n", &m) == 0);
	CHECK_STR(m.name, "solo"); // name defaults to id

	// the resource folder plugin_info gives, for a bundled plugin and one
	// installed as its own pak
	char res[64];
	snprintf(m.dir, sizeof(m.dir), "/pak/plugins/solo.plugin");
	CHECK(Manifest_res(&m, res, sizeof(res)) == 0);
	CHECK_STR(res, "/pak/plugins/solo.plugin/res");
	snprintf(m.dir, sizeof(m.dir), "/sd/Tools/tg5040/Solo.pak");
	CHECK(Manifest_res(&m, res, sizeof(res)) == 0);
	CHECK_STR(res, "/sd/Tools/tg5040/Solo.pak/res");
	char small[8];
	CHECK(Manifest_res(&m, small, sizeof(small)) == -1);
	CHECK_STR(small, "");

	// defaults: bundled off, separately installed on
	reset();
	add("id=bundled", 1);
	add("id=pak", 0);
	int n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(n == 2);
	CHECK(!loads(n, "bundled"));
	CHECK(loads(n, "pak"));
	CHECK_STR(decision(n, "bundled")->reason, "turned off");

	// overrides and order: listed ids first, in plugins.txt order
	n = PluginSet_resolve(manifests, count, "pak=on\nbundled=on\n", out);
	CHECK_STR(out[0].manifest->id, "pak");
	CHECK_STR(out[1].manifest->id, "bundled");
	CHECK(loads(n, "bundled"));
	n = PluginSet_resolve(manifests, count, "pak=off\n", out);
	CHECK(!loads(n, "pak"));

	// duplicate ids: the first found wins
	reset();
	add("id=same", 0);
	add("id=same", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(out[0].load);
	CHECK(!out[1].load);

	// exclusive slots: first claimant, unless the user picked another
	reset();
	add("id=grid\nslots=layout", 0);
	add("id=carousel\nslots=layout", 0);
	add("id=other\nslots=home", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(loads(n, "grid"));
	CHECK(!loads(n, "carousel"));
	CHECK(loads(n, "other"));
	CHECK_STR(decision(n, "carousel")->reason, "the layout slot belongs to grid");
	n = PluginSet_resolve(manifests, count, "slot.layout=carousel\n", out);
	CHECK(!loads(n, "grid"));
	CHECK(loads(n, "carousel"));
	// a pick that's turned off doesn't hold the slot
	n = PluginSet_resolve(manifests, count, "carousel=off\nslot.layout=carousel\n", out);
	CHECK(loads(n, "grid"));

	// conflicts, declared on either side: the later one gives way
	reset();
	add("id=focus\nconflicts=allgames", 0);
	add("id=allgames", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(loads(n, "focus"));
	CHECK(!loads(n, "allgames"));
	n = PluginSet_resolve(manifests, count, "allgames=on\nfocus=on\n", out);
	CHECK(loads(n, "allgames"));
	CHECK(!loads(n, "focus"));

	// requirements, including ones stranded by another refusal
	reset();
	add("id=favorites", 0);
	add("id=focus\nrequires=favorites", 0);
	add("id=kids\nrequires=focus", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(loads(n, "favorites") && loads(n, "focus") && loads(n, "kids"));
	n = PluginSet_resolve(manifests, count, "favorites=off\n", out);
	CHECK(!loads(n, "focus"));
	CHECK(!loads(n, "kids"));
	CHECK_STR(decision(n, "focus")->reason, "requires favorites, which isn't loading");

	// settings declared in plugin.txt
	PluginManifest s = { .bundled = 1 };
	CHECK(Manifest_parse(
		"id=fav\n"
		"setting.collection=text|Collection|Favorites\n"
		"setting.order=choice|Order|newest|newest,oldest\n"
		"setting.badge=toggle|Badge|on\n"
		"setting.broken=choice|No options|x\n"
		"setting.short=text|Missing default\n"
		"setting.odd=dial|Unknown type|1\n", &s) == 0);
	CHECK(s.setting_count == 3);
	CHECK_STR(s.settings[0].key, "collection");
	CHECK(s.settings[0].type == SETTING_TEXT);
	CHECK_STR(s.settings[0].label, "Collection");
	CHECK_STR(s.settings[0].fallback, "Favorites");
	CHECK(s.settings[1].type == SETTING_CHOICE);
	CHECK_STR(s.settings[1].options, "newest,oldest");
	CHECK(s.settings[2].type == SETTING_TOGGLE);
	CHECK(s.settings[0].flags == 0 && s.settings[1].flags == 0 && s.settings[2].flags == 0);
	CHECK(!Manifest_isProtected(&s));

	// protected and secret markers, in a fifth field
	PluginManifest g = { .bundled = 1 };
	CHECK(Manifest_parse(
		"id=guarded\n"
		"setting.limit=choice|Limit|off|off,15,30|protected\n"
		"setting.code=text|Code|||protected, secret\n"
		"setting.note=text|Note|||secret\n"
		"setting.badge=toggle|Badge|on||secret,protected\n"
		"setting.later=toggle|Later|on||glow\n"
		"setting.plain=choice|Plain|a|a,b\n"
		"setting.extra=choice|Extra|a|a,b|protected|more\n", &g) == 0);
	CHECK(g.setting_count == 7);
	CHECK(g.settings[0].flags == SETTING_PROTECTED);
	CHECK_STR(g.settings[0].options, "off,15,30");
	CHECK(g.settings[1].flags == (SETTING_PROTECTED | SETTING_SECRET));
	CHECK_STR(g.settings[1].fallback, "");
	CHECK_STR(g.settings[1].options, "");
	CHECK(g.settings[2].flags == SETTING_SECRET);
	CHECK(g.settings[3].flags == SETTING_PROTECTED); // secret is for text only
	CHECK(g.settings[4].flags == 0);                 // unknown markers are left for newer launchers
	CHECK(g.settings[5].flags == 0);
	CHECK(g.settings[6].flags == SETTING_PROTECTED); // a sixth field is ignored
	CHECK_STR(g.settings[6].options, "a,b");
	CHECK(Manifest_isProtected(&g));
	PluginManifest only_secret = { .bundled = 1 };
	CHECK(Manifest_parse("id=s\nsetting.note=text|Note|||secret\n", &only_secret) == 0);
	CHECK(!Manifest_isProtected(&only_secret));

	// button:<action id> marks the setting a plugin puts that action's button
	// by, for action_info; other flags still count, and unknown words don't
	PluginManifest marked = { .bundled = 1 };
	CHECK(Manifest_parse(
		"id=marked\n"
		"setting.button=choice|Button|Y|none,Y,L2| button:marked.hide , button:marked.unhide,public\n"
		"setting.extra=choice|Extra|L2|L2,R2|protected,button:other.run,button:\n"
		"setting.plain=toggle|Plain|on\n", &marked) == 0);
	CHECK(marked.setting_count == 3);
	CHECK_STR(marked.settings[0].buttons, "marked.hide,marked.unhide");
	CHECK_STR(marked.settings[0].options, "none,Y,L2");
	CHECK(marked.settings[0].flags == SETTING_PUBLIC);
	CHECK_STR(marked.settings[1].buttons, "other.run"); // an empty id is left out
	CHECK(marked.settings[1].flags == SETTING_PROTECTED);
	CHECK_STR(marked.settings[2].buttons, "");
	CHECK_STR(Manifest_buttonSetting(&marked, "marked.unhide"), "button");
	CHECK_STR(Manifest_buttonSetting(&marked, "other.run"), "extra");
	CHECK(Manifest_buttonSetting(&marked, "marked") == NULL);
	CHECK(Manifest_buttonSetting(&marked, "") == NULL);
	CHECK(Manifest_buttonSetting(&marked, NULL) == NULL);
	CHECK(Manifest_buttonSetting(NULL, "marked.hide") == NULL);

	// public, for other plugins' cfg_read, but never on a protected or secret setting
	PluginManifest pub = { .bundled = 1 };
	CHECK(Manifest_parse(
		"id=pub\n"
		"setting.dot=toggle|Dot|off||public\n"
		"setting.name=text|Name|Favorites||public\n"
		"setting.format=choice|Format|a|a,b|public\n"
		"setting.limit=choice|Limit|off|off,15|protected,public\n"
		"setting.code=text|Code|||public,secret\n"
		"setting.plain=toggle|Plain|on\n", &pub) == 0);
	CHECK(pub.setting_count == 6);
	CHECK(pub.settings[0].flags == SETTING_PUBLIC);
	CHECK(pub.settings[1].flags == SETTING_PUBLIC);
	CHECK(pub.settings[2].flags == SETTING_PUBLIC);
	CHECK(pub.settings[3].flags == SETTING_PROTECTED);
	CHECK(pub.settings[4].flags == SETTING_SECRET);
	// the Plugins page shows and edits a public setting like any other
	CHECK(pub.settings[1].type == SETTING_TEXT);
	CHECK_STR(pub.settings[1].label, "Name");
	CHECK_STR(pub.settings[1].fallback, "Favorites");
	CHECK_STR(pub.settings[2].options, "a,b");
	CHECK(Manifest_isProtected(&pub)); // limit still asks for the PIN
	CHECK(Manifest_isPublic(&pub, "dot") && Manifest_isPublic(&pub, "name") && Manifest_isPublic(&pub, "format"));
	CHECK(!Manifest_isPublic(&pub, "limit") && !Manifest_isPublic(&pub, "code"));
	CHECK(!Manifest_isPublic(&pub, "plain"));
	CHECK(!Manifest_isPublic(&pub, "unlisted"));
	CHECK(!Manifest_isPublic(&pub, NULL) && !Manifest_isPublic(NULL, "dot"));

	// on or off, by the same rule as loading
	CHECK(!PluginSet_isOn(NULL, &s)); // bundled starts off
	CHECK(PluginSet_isOn("fav=on\n", &s));
	CHECK(!PluginSet_isOn("fav=on\nfav=off\n", &s));
	s.bundled = 0;
	CHECK(PluginSet_isOn("other=off\n", &s)); // installed starts on

	// setting a choice keeps load order, and adds new ones at the end
	char* set = PluginSet_setChoice("grid=on\nfav=off\nslot.layout=grid\n", "fav", 1);
	CHECK_STR(set, "grid=on\nfav=on\nslot.layout=grid\n");
	free(set);
	set = PluginSet_setChoice("grid=on", "fav", 0);
	CHECK_STR(set, "grid=on\nfav=off\n");
	free(set);
	set = PluginSet_setChoice(NULL, "fav", 1);
	CHECK_STR(set, "fav=on\n");
	free(set);
	set = PluginSet_setChoice("fav=on\nx=on\n fav = off\n", "fav", 0); // duplicates collapse
	CHECK_STR(set, "fav=off\nx=on\n");
	free(set);

	// a choice's options, as the Plugins page lists them
	char option[64];
	CHECK(Manifest_option("none, Y,,L2 , ", 0, option, sizeof(option)));
	CHECK_STR(option, "none");
	CHECK(Manifest_option("none, Y,,L2 , ", 1, option, sizeof(option)));
	CHECK_STR(option, "Y");
	CHECK(Manifest_option("none, Y,,L2 , ", 2, option, sizeof(option)));
	CHECK_STR(option, "L2 "); // only leading spaces go, as the page has always shown them
	CHECK(!Manifest_option("none, Y,,L2 , ", 3, option, sizeof(option)));
	CHECK(!Manifest_option("", 0, option, sizeof(option)));
	CHECK(!Manifest_option("a", -1, option, sizeof(option)));

	// the text the Plugins page shows, for make pot and translating: the
	// name, the description, then each label and a choice's options, but
	// no defaults or text values
	PluginManifest t = { .bundled = 1 };
	CHECK(Manifest_parse("id=tr\nname=Translated\ndescription=Says hello.\nsetting.on=toggle|Turn it on|off\n"
		"setting.pick=choice|Pick one|b|a, b,,c\nsetting.note=text|Note|typed\nsetting.bad=choice|Empty|x|\n"
		"setting.q=choice|\"Quoted\" \\ (%)|n|n\n", &t) == 0);
	strings[0] = '\0';
	Manifest_strings(&t, append, NULL);
	CHECK_STR(strings, "Translated;Says hello.;Turn it on;Pick one;a;b;c;Note;\"Quoted\" \\ (%);n;");
	PluginManifest bare = { .bundled = 1 };
	CHECK(Manifest_parse("id=bare\n", &bare) == 0);
	strings[0] = '\0';
	Manifest_strings(&bare, append, NULL);
	CHECK_STR(strings, "bare;"); // the name defaults to the id

	// a plugin keeps every setting its plugin.txt lists, however many
	char many[8192] = "id=many\n";
	for (int i = 0; i < 40; i++) {
		snprintf(many + strlen(many), sizeof(many) - strlen(many),
			"setting.s%d=choice|Setting %d|a|a,b%s\n", i, i, i == 39 ? "|public" : "");
	}
	PluginManifest big = { .bundled = 1 };
	CHECK(Manifest_parse(many, &big) == 0);
	CHECK(big.setting_count == 40);
	CHECK_STR(big.settings[0].key, "s0");
	CHECK_STR(big.settings[8].label, "Setting 8");
	CHECK_STR(big.settings[39].key, "s39");
	CHECK_STR(big.settings[39].options, "a,b");
	CHECK(Manifest_isPublic(&big, "s39"));
	strings[0] = '\0';
	Manifest_strings(&big, append, NULL);
	CHECK(strstr(strings, "Setting 39;a;b;") != NULL);
	// parsing again replaces them, and a missing id leaves none
	CHECK(Manifest_parse("id=many\nsetting.one=toggle|One|on\n", &big) == 0);
	CHECK(big.setting_count == 1);
	CHECK_STR(big.settings[0].key, "one");
	CHECK(Manifest_parse(many + strlen("id=many\n"), &big) == -1);
	CHECK(big.setting_count == 0 && big.settings == NULL);
	CHECK(Manifest_parse(many, &big) == 0);
	Manifest_free(&big);
	CHECK(big.setting_count == 0 && big.settings == NULL);
	Manifest_free(&big); // twice is fine

	// the Launcher pak's settings load only the strings and theme slots'
	// owners: one claiming either, or both, or with another slot too, but
	// not one turned off, one that lost its slot, or one that claims
	// neither, nor what they require
	reset();
	add("id=words\nslots=strings\nrequires=helper", 0);
	add("id=helper", 0);
	add("id=looks\nslots=layout, theme", 0);
	add("id=looks2\nslots=theme", 0);
	add("id=grid\nslots=layout", 0);
	add("id=off\nslots=strings", 1);
	add("id=plain", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(loads(n, "helper") && !loads(n, "grid") && loads(n, "plain") && !loads(n, "looks2"));
	CHECK(PluginSet_onlySlots(out, n, "strings,theme", "settings only") == 2);
	CHECK(loads(n, "words"));
	CHECK(loads(n, "looks"));
	CHECK(!loads(n, "helper"));
	CHECK_STR(decision(n, "helper")->reason, "settings only");
	CHECK(!loads(n, "grid"));
	CHECK(!loads(n, "plain"));
	CHECK_STR(decision(n, "plain")->reason, "settings only");
	CHECK_STR(decision(n, "looks2")->reason, "the theme slot belongs to looks");
	CHECK(!loads(n, "off"));
	CHECK_STR(decision(n, "off")->reason, "turned off");
	// the user's pick for a slot is the one kept
	n = PluginSet_resolve(manifests, count, "slot.theme=looks2\n", out);
	CHECK(PluginSet_onlySlots(out, n, "strings,theme", "settings only") == 2);
	CHECK(loads(n, "looks2"));
	CHECK(!loads(n, "looks"));
	// no owners: nothing loads
	n = PluginSet_resolve(manifests, count, "words=off\nlooks=off\nlooks2=off\n", out);
	CHECK(PluginSet_onlySlots(out, n, "strings,theme", "settings only") == 0);
	for (int i = 0; i < n; i++) CHECK(!out[i].load);

	// provides=, optional= and description=
	CHECK(Manifest_parse("id=fav\nprovides= sets.favorites , events.fav.added\noptional=a,b\ndescription= Keeps favorites \n", &m) == 0);
	CHECK(csvHas(m.provides, "sets.favorites"));
	CHECK(csvHas(m.provides, "events.fav.added"));
	CHECK(!csvHas(m.provides, "sets"));
	CHECK(csvHas(m.optional, "b"));
	CHECK_STR(m.description, "Keeps favorites");
	CHECK(Manifest_parse("id=bare\n", &m) == 0);
	CHECK_STR(m.provides, "");
	CHECK_STR(m.optional, "");
	CHECK_STR(m.description, "");

	// optional=: after the named plugins when they load, the rest keeping
	// their order
	reset();
	add("id=dash\noptional=fav", 0);
	add("id=plain", 0);
	add("id=fav", 0);
	add("id=late", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK_STR(order(n), "plain;fav;dash;late;");
	CHECK(PluginSet_orderBroken(out, n, indexOf(n, "dash")) == NULL);
	// plugins.txt's order first, then optional=
	n = PluginSet_resolve(manifests, count, "late=on\ndash=on\nfav=on\n", out);
	CHECK_STR(order(n), "late;fav;dash;plain;");
	// fine without it: off, missing or refused
	n = PluginSet_resolve(manifests, count, "fav=off\n", out);
	CHECK(loads(n, "dash"));
	CHECK_STR(order(n), "dash;plain;late;");
	reset();
	add("id=dash\noptional=fav,missing", 0);
	add("id=fav\nconflicts=dash", 0);
	add("id=other", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	// the conflict is decided in the usual order, before optional= moves anything
	CHECK(loads(n, "dash") && !loads(n, "fav"));
	CHECK_STR(decision(n, "fav")->reason, "conflicts with dash");
	CHECK_STR(order(n), "dash;other;");
	// it never loads a plugin for it, unlike requires=
	reset();
	add("id=dash\noptional=fav", 0);
	add("id=fav", 1);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(loads(n, "dash") && !loads(n, "fav"));
	// a chain, and requires= alone doesn't order
	reset();
	add("id=a\noptional=b", 0);
	add("id=b\noptional=c", 0);
	add("id=c", 0);
	add("id=d\nrequires=e", 0);
	add("id=e", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK_STR(order(n), "c;b;a;d;e;");
	// naming itself is ignored
	reset();
	add("id=self\noptional=self", 0);
	add("id=next", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK_STR(order(n), "self;next;");
	CHECK(PluginSet_orderBroken(out, n, 0) == NULL);
	// a cycle loads in the usual order, and says so
	reset();
	add("id=x\noptional=y", 0);
	add("id=y\noptional=x", 0);
	add("id=z\noptional=x", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK_STR(order(n), "x;y;z;");
	CHECK_STR(PluginSet_orderBroken(out, n, indexOf(n, "x")), "y");
	CHECK(PluginSet_orderBroken(out, n, indexOf(n, "y")) == NULL);
	CHECK(PluginSet_orderBroken(out, n, indexOf(n, "z")) == NULL);
	CHECK(PluginSet_orderBroken(out, n, -1) == NULL);
	CHECK(PluginSet_orderBroken(out, n, n) == NULL);
	// a plugin that's off keeps its place among the decisions, with no wait
	reset();
	add("id=x\noptional=y", 1);
	add("id=y", 0);
	n = PluginSet_resolve(manifests, count, NULL, out);
	CHECK(n == 2 && !loads(n, "x"));
	CHECK_STR(out[0].manifest->id, "x");

	// as many plugins as there are: more than the 64 the launcher once stopped at
	enum { MANY = 130 };
	ManifestList list = { 0 };
	for (int i = 0; i < MANY; i++) {
		char text[64], dir[32];
		snprintf(text, sizeof(text), "id=p%03d\nsetting.s=toggle|S|on\n", i);
		snprintf(dir, sizeof(dir), "/plugins/p%03d", i);
		CHECK(ManifestList_add(&list, text, dir, i % 2) == 0);
	}
	CHECK(list.count == MANY && list.room >= MANY);
	CHECK_STR(list.items[0].id, "p000");
	CHECK_STR(list.items[MANY - 1].id, "p129");
	CHECK_STR(list.items[MANY - 1].dir, "/plugins/p129");
	CHECK(list.items[MANY - 1].bundled == 1 && list.items[MANY - 1].setting_count == 1);
	// one without an id, or with a path too long for its dir, isn't added
	CHECK(ManifestList_add(&list, "name=nobody\n", "/plugins/nobody", 0) == -1);
	char long_dir[600];
	memset(long_dir, 'd', sizeof(long_dir) - 1);
	long_dir[sizeof(long_dir) - 1] = '\0';
	CHECK(ManifestList_add(&list, "id=far\n", long_dir, 0) == -3);
	CHECK(list.count == MANY);
	// every one is decided, and plugins.txt's lines for all of them count:
	// the last listed loads first, and the bundled ones turned on load
	char* choices = calloc(MANY, 16);
	for (int i = MANY - 1; i >= 0; i--) snprintf(choices + strlen(choices), MANY * 16 - strlen(choices), "p%03d=on\n", i);
	PluginDecision* decided = calloc(MANY, sizeof(PluginDecision));
	n = PluginSet_resolve(list.items, list.count, choices, decided);
	CHECK(n == MANY);
	CHECK_STR(decided[0].manifest->id, "p129");
	CHECK_STR(decided[MANY - 1].manifest->id, "p000");
	int loading = 0;
	for (int i = 0; i < n; i++) loading += decided[i].load;
	CHECK(loading == MANY);
	CHECK(PluginSet_isOn(choices, &list.items[MANY - 1]));
	free(choices);
	n = PluginSet_resolve(list.items, list.count, "p129=off\n", decided);
	CHECK(n == MANY && !decided[0].load && decided[0].manifest == &list.items[MANY - 1]);
	CHECK_STR(decided[1].manifest->id, "p000");
	free(decided);
	ManifestList_free(&list);
	CHECK(list.items == NULL && list.count == 0 && list.room == 0);
	ManifestList_free(&list); // twice is fine
	CHECK(PluginSet_resolve(NULL, 0, "x=on\n", NULL) == 0);

	// what's cut to fit or left out is said, naming the plugin, once each
	Manifest_setLog(warning);
	unwarn();
	PluginManifest cut = { .dir = "/plugins/cut" };
	char text[4096];
	char filler[600];
	memset(filler, 'x', sizeof(filler) - 1);
	filler[sizeof(filler) - 1] = '\0';
	snprintf(text, sizeof(text),
		"name=%.200s\n"
		"setting.big=choice|Big|a|a,%.300s\n"
		"setting.%.60s=toggle|Key|on\n"
		"setting.btn=toggle|Btn|on||button:%.130s,button:%.130s\n"
		"description=%s\n"
		"id=cut\n"
		"setting.fine=toggle|Fine|on\n",
		filler, filler, filler, filler, filler + 200, filler);
	CHECK(Manifest_parse(text, &cut) == 0);
	CHECK(strlen(cut.name) == sizeof(cut.name) - 1);
	CHECK(cut.setting_count == 4);
	CHECK(strlen(cut.settings[0].options) == sizeof(cut.settings[0].options) - 1);
	CHECK(strlen(cut.settings[1].key) == sizeof(cut.settings[1].key) - 1);
	CHECK(strlen(cut.settings[2].buttons) == 130);
	CHECK(strlen(cut.description) == sizeof(cut.description) - 1);
	CHECK_STR(cut.settings[3].key, "fine");
	CHECK(warnings == 6);
	CHECK(strstr(warned, "cut's plugin.txt: name is over 127 bytes, cut to fit\n") != NULL);
	CHECK(strstr(warned, "cut's plugin.txt: setting.big's options is over 255 bytes, cut to fit\n") != NULL);
	CHECK(strstr(warned, "cut's plugin.txt: setting.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx's key is over 47 bytes, cut to fit\n") != NULL);
	CHECK(strstr(warned, "cut's plugin.txt: setting.btn's button: markers are over 255 bytes, leaving out 1\n") != NULL);
	CHECK(strstr(warned, "cut's plugin.txt: a line is over 511 bytes, cut to fit: description=xxxxxxxxxxxxxxxxxxxxxxxxxxxx...\n") != NULL);
	CHECK(strstr(warned, "cut's plugin.txt: description is over 255 bytes, cut to fit\n") != NULL);
	// without an id, the dir names it
	unwarn();
	snprintf(text, sizeof(text), "name=%.200s\n", filler);
	CHECK(Manifest_parse(text, &cut) == -1);
	CHECK_STR(warned, "/plugins/cut/plugin.txt: name is over 127 bytes, cut to fit\n");
	// a plugins.txt line or id that's cut, as the launcher decides
	unwarn();
	reset();
	add("id=fav", 0);
	snprintf(text, sizeof(text), "%.70s=off\nslot.theme=%.70s\n%s=on\nfav=off\n", filler, filler, filler);
	n = PluginSet_resolve(manifests, count, text, out);
	CHECK(n == 1 && !loads(n, "fav"));
	CHECK(warnings == 3);
	CHECK(strstr(warned, "plugins.txt: the plugin id xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx is over 63 bytes, cut to fit\n") != NULL);
	CHECK(strstr(warned, "plugins.txt: slot.theme's plugin id is over 63 bytes, cut to fit\n") != NULL);
	CHECK(strstr(warned, "plugins.txt: a line is over 511 bytes, cut to fit: xxxx") != NULL);
	// checking one plugin's choice says nothing, as the Plugins page asks for each
	unwarn();
	CHECK(PluginSet_isOn(text, &manifests[0]) == 0);
	CHECK(warnings == 0);
	// writing plugins.txt back says what it cuts
	char* rewritten = PluginSet_setChoice(text, "fav", 1);
	CHECK(warnings == 1 && strstr(warned, "plugins.txt: a line is over 511 bytes, cut to fit") != NULL);
	free(rewritten);
	// a manifest that fits says nothing, and with no log nothing is said
	unwarn();
	CHECK(Manifest_parse("id=fits\nname=Fits\nsetting.a=choice|A|x|x,y|button:go\n", &cut) == 0);
	CHECK(warnings == 0);
	Manifest_setLog(NULL);
	snprintf(text, sizeof(text), "id=quiet\nname=%.200s\n", filler);
	CHECK(Manifest_parse(text, &cut) == 0);
	CHECK(warnings == 0);
	Manifest_free(&cut);

	for (int i = 0; i < (int)(sizeof(manifests) / sizeof(manifests[0])); i++) Manifest_free(&manifests[i]);
	Manifest_free(&m);
	Manifest_free(&s);
	Manifest_free(&g);
	Manifest_free(&only_secret);
	Manifest_free(&marked);
	Manifest_free(&pub);
	Manifest_free(&t);
	Manifest_free(&bare);
	CHECK_DONE();
}
