// Which plugins load, and in what order.

#include <stdio.h>
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

static int loads(int n, const char* id) {
	const PluginDecision* d = decision(n, id);
	return d && d->load;
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

	CHECK_DONE();
}
