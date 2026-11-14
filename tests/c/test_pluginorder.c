// The Launcher pak's Plugins page order.

#include <stdio.h>
#include "check.h"
#include "pluginorder.h"

static PluginManifest manifests[PLUGIN_MAX];
static PluginPageRow rows[PLUGIN_MAX + PLUGIN_GROUP_COUNT + 1];
static int count;

static void add(const char* id, const char* name, const char* slots) {
	PluginManifest* m = &manifests[count++];
	memset(m, 0, sizeof(*m));
	snprintf(m->id, sizeof(m->id), "%s", id);
	snprintf(m->name, sizeof(m->name), "%s", name);
	snprintf(m->slots, sizeof(m->slots), "%s", slots);
}

// The rows as text: "[Title]" for a header, else the plugin's id, joined
// with commas.
static const char* shown(int n) {
	static char text[1024];
	text[0] = '\0';
	for (int i = 0; i < n; i++) {
		size_t at = strlen(text);
		const char* sep = i ? "," : "";
		if (rows[i].plugin < 0) snprintf(text + at, sizeof(text) - at, "%s[%s]", sep, PluginOrder_title(rows[i].group));
		else snprintf(text + at, sizeof(text) - at, "%s%s", sep, manifests[rows[i].plugin].id);
	}
	return text;
}

static const char* order(void) {
	return shown(PluginOrder_rows(manifests, count, rows, (int)(sizeof(rows) / sizeof(rows[0]))));
}

int main(void) {
	// names compare A-Z, ignoring case and Latin letters' accents
	CHECK(PluginOrder_compareNames("apple", "Banana") < 0);
	CHECK(PluginOrder_compareNames("Banana", "apple") > 0);
	CHECK(PluginOrder_compareNames("ABC", "abc") == 0);
	CHECK(PluginOrder_compareNames("\xc3\x89" "clair", "eclair") == 0);   // Éclair
	CHECK(PluginOrder_compareNames("\xc3\xa9" "clair", "Ecluse") < 0);    // éclair
	CHECK(PluginOrder_compareNames("Stra\xc3\x9f" "e", "Strasse") < 0);   // ß is one s
	CHECK(PluginOrder_compareNames("Ma\xc3\xb1" "ana", "Mango") < 0);     // ñ is n
	CHECK(PluginOrder_compareNames("Grid", "Grid plus") < 0);
	CHECK(PluginOrder_compareNames("", "a") < 0);
	CHECK(PluginOrder_compareNames("\xc3\x97", "z") > 0);                // × stays as it is
	CHECK(PluginOrder_compareNames("\xe2\x82\xac", "z") > 0);            // and so does €

	// a plugin's group is the first slot it claims, in the slots' order
	count = 0;
	add("t", "T", "launch, transition");
	add("u", "U", "nonsense");
	add("v", "V", "");
	add("w", "W", "theme,root");
	CHECK(PluginOrder_group(&manifests[0]) == 2);
	CHECK(PluginOrder_group(&manifests[1]) == PLUGIN_GROUP_OTHER);
	CHECK(PluginOrder_group(&manifests[2]) == PLUGIN_GROUP_OTHER);
	CHECK(PluginOrder_group(&manifests[3]) == 0);
	CHECK_STR(PluginOrder_title(0), "Main menu");
	CHECK_STR(PluginOrder_title(1), "Layout");
	CHECK_STR(PluginOrder_title(5), "Home screen");
	CHECK_STR(PluginOrder_title(8), "Theme");
	CHECK_STR(PluginOrder_title(PLUGIN_GROUP_OTHER), "Other plugins");
	CHECK_STR(PluginOrder_title(PLUGIN_GROUP_COUNT), "Other plugins");

	// no plugins, no rows
	count = 0;
	CHECK_STR(order(), "");

	// with no slots, just A-Z and no header
	add("mango", "Mango", "");
	add("apple", "apple", "");
	add("banana", "Banana", "");
	CHECK_STR(order(), "apple,banana,mango");

	// grouped by slot in the slots' order, each A-Z, then the rest under
	// their own header; one with two slots is listed once, under the first
	count = 0;
	add("mango", "Mango", "");
	add("zeta", "Zeta", "layout");
	add("theme-x", "Dusk", "theme");
	add("both", "Both", "transition,launch");
	add("alpha", "alpha", "layout");
	add("lift", "Liftoff", "launch");
	add("odd", "Banana", "nonsense");
	add("apple", "apple", "");
	add("focus", "Focus", "root");
	add("home", "Home", "home");
	add("art", "Art", "background");
	add("switch", "Switch", "switcher");
	add("lang", "Words", "strings");
	CHECK_STR(order(),
		"[Main menu],focus,[Layout],alpha,zeta,[Transitions],both,[Launch animation],lift,"
		"[Background],art,[Home screen],home,[Game switcher],switch,[Language],lang,"
		"[Theme],theme-x,[Other plugins],apple,odd,mango");

	// only slot plugins: no Other header
	count = 0;
	add("b", "B", "theme");
	add("a", "A", "layout");
	CHECK_STR(order(), "[Layout],a,[Theme],b");

	// the same name is ordered by id, and accents don't count
	count = 0;
	add("grid-b", "Grid", "");
	add("grid-a", "grid", "");
	add("eclair", "\xc3\x89" "clair", "");
	add("ecluse", "ecluse", "");
	add("dot", "Dot", "");
	CHECK_STR(order(), "dot,eclair,ecluse,grid-a,grid-b");

	// rows stop at max, header included
	count = 0;
	add("a", "A", "layout");
	add("b", "B", "layout");
	add("c", "C", "");
	CHECK(PluginOrder_rows(manifests, count, rows, 2) == 2);
	CHECK_STR(shown(2), "[Layout],a");

	// every plugin the page can list, plus a header for each group
	count = 0;
	for (int i = 0; i < PLUGIN_MAX; i++) {
		char id[16];
		snprintf(id, sizeof(id), "p%02d", PLUGIN_MAX - 1 - i);
		add(id, id, i % 2 ? "" : "home");
	}
	int n = PluginOrder_rows(manifests, count, rows, (int)(sizeof(rows) / sizeof(rows[0])));
	CHECK(n == PLUGIN_MAX + 2);
	CHECK(rows[0].plugin == -1 && rows[0].group == 5);
	CHECK_STR(manifests[rows[1].plugin].id, "p01");
	CHECK(rows[PLUGIN_MAX / 2 + 1].plugin == -1 && rows[PLUGIN_MAX / 2 + 1].group == PLUGIN_GROUP_OTHER);
	CHECK_STR(manifests[rows[PLUGIN_MAX / 2 + 2].plugin].id, "p00");
	CHECK_STR(manifests[rows[n - 1].plugin].id, "p62");

	for (int i = 0; i < PLUGIN_MAX; i++) Manifest_free(&manifests[i]);
	CHECK_DONE();
}
