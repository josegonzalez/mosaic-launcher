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

// Manifest_strings' texts, each followed by ;
static char strings[512];
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
	// name, then each label and a choice's options, but no defaults or
	// text values
	PluginManifest t = { .bundled = 1 };
	CHECK(Manifest_parse("id=tr\nname=Translated\nsetting.on=toggle|Turn it on|off\n"
		"setting.pick=choice|Pick one|b|a, b,,c\nsetting.note=text|Note|typed\nsetting.bad=choice|Empty|x|\n"
		"setting.q=choice|\"Quoted\" \\ (%)|n|n\n", &t) == 0);
	strings[0] = '\0';
	Manifest_strings(&t, append, NULL);
	CHECK_STR(strings, "Translated;Turn it on;Pick one;a;b;c;Note;\"Quoted\" \\ (%);n;");
	PluginManifest bare = { .bundled = 1 };
	CHECK(Manifest_parse("id=bare\n", &bare) == 0);
	strings[0] = '\0';
	Manifest_strings(&bare, append, NULL);
	CHECK_STR(strings, "bare;"); // the name defaults to the id

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

	CHECK_DONE();
}
