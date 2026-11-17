// Plugins' entry_open: only games and paks are asked about, and the first
// plugin that opens one itself takes it.

#include "check.h"
#include "mosaic_plugin.h"
#include "openmodel.h"

// each plugin's answer, and how many were asked
static int answers[4];
static int asked;

static int ask(int index, void* data) {
	(void)data;
	asked++;
	return answers[index];
}

static int first(int a, int b, int c, int d) {
	answers[0] = a;
	answers[1] = b;
	answers[2] = c;
	answers[3] = d;
	asked = 0;
	return OpenModel_first(4, ask, NULL);
}

int main(void) {
	CHECK(OpenModel_asks(MOSAIC_ENTRY_ROM));
	CHECK(OpenModel_asks(MOSAIC_ENTRY_PAK));
	CHECK(!OpenModel_asks(MOSAIC_ENTRY_DIR));
	CHECK(!OpenModel_asks(MOSAIC_ENTRY_TOGGLE));
	CHECK(!OpenModel_asks(-1));

	// none takes it: every plugin is asked
	CHECK(first(0, 0, 0, 0) == -1);
	CHECK(asked == 4);
	// the first that takes it stops the asking
	CHECK(first(0, 1, 1, 0) == 1);
	CHECK(asked == 2);
	CHECK(first(1, 0, 0, 0) == 0);
	CHECK(asked == 1);
	// only 1 is yes
	CHECK(first(-1, 2, 0, 1) == 3);
	CHECK(asked == 4);
	// no plugins, or nothing to ask with
	CHECK(OpenModel_first(0, ask, NULL) == -1);
	CHECK(OpenModel_first(4, NULL, NULL) == -1);
	CHECK_DONE();
}
