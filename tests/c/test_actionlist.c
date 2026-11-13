// The buttons host->action_info lists for each action: the ones in
// effect, with an override taking a button from the game list's own and
// bind_button's, in order, counting past what fits, and copied into a
// plugin's struct only as far as it knows.

#include <stddef.h>
#include "check.h"
#include "actionlist.h"

#define GL MOSAIC_SCREEN_GAMELIST

static void test_drop(void) {
	ActionBind binds[] = {
		{ "gamelist.open", MOSAIC_BIND_SCREEN, GL, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, NULL, NULL },
		{ "gamelist.resume", MOSAIC_BIND_SCREEN, GL, MOSAIC_BUTTON_X, MOSAIC_TRIGGER_RELEASE, NULL, NULL },
		{ "gamelist.back", MOSAIC_BIND_SCREEN, GL, MOSAIC_BUTTON_B, MOSAIC_TRIGGER_PRESS, NULL, NULL },
		// swaps A and X, on other triggers than the game list's
		{ "gamelist.resume", MOSAIC_BIND_OVERRIDE, GL, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_RELEASE, "remap", NULL },
		{ "gamelist.open", MOSAIC_BIND_OVERRIDE, GL, MOSAIC_BUTTON_X, MOSAIC_TRIGGER_PRESS, "remap", NULL },
		{ "fav.toggle", MOSAIC_BIND_BUTTON, GL, MOSAIC_BUTTON_X, MOSAIC_TRIGGER_PRESS, "fav", "button" },
		{ "fav.toggle", MOSAIC_BIND_BUTTON, GL, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "fav", "button" },
		// an FN button isn't on a screen, so no override takes it
		{ "gamelist.back", MOSAIC_BIND_FN_USER, -1, MOSAIC_BUTTON_FN1, MOSAIC_TRIGGER_PRESS, NULL, NULL },
	};
	int n = ActionList_dropOverridden(binds, 8);
	CHECK(n == 5);
	CHECK_STR(binds[0].action_id, "gamelist.back");
	CHECK(binds[0].by == MOSAIC_BIND_SCREEN);
	CHECK(binds[1].by == MOSAIC_BIND_OVERRIDE && binds[1].button == MOSAIC_BUTTON_A);
	CHECK(binds[2].by == MOSAIC_BIND_OVERRIDE && binds[2].button == MOSAIC_BUTTON_X);
	CHECK(binds[3].by == MOSAIC_BIND_BUTTON && binds[3].button == MOSAIC_BUTTON_Y);
	CHECK(binds[4].by == MOSAIC_BIND_FN_USER);

	// an override on another screen takes nothing here
	ActionBind other[] = {
		{ "gamelist.open", MOSAIC_BIND_SCREEN, GL, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, NULL, NULL },
		{ "x.run", MOSAIC_BIND_OVERRIDE, MOSAIC_SCREEN_SWITCHER, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "x", NULL },
		// a NULL action still takes the button
		{ NULL, MOSAIC_BIND_OVERRIDE, GL, MOSAIC_BUTTON_B, MOSAIC_TRIGGER_PRESS, "x", NULL },
		{ "gamelist.back", MOSAIC_BIND_SCREEN, GL, MOSAIC_BUTTON_B, MOSAIC_TRIGGER_PRESS, NULL, NULL },
	};
	CHECK(ActionList_dropOverridden(other, 4) == 3);
	CHECK_STR(other[0].action_id, "gamelist.open");
	CHECK(other[2].action_id == NULL);
	CHECK(ActionList_dropOverridden(other, 0) == 0);
}

static void test_fill(void) {
	ActionBind binds[] = {
		{ "romm.flashback", MOSAIC_BIND_BUTTON, GL, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "romm", "button" },
		{ "other.run", MOSAIC_BIND_BUTTON, GL, MOSAIC_BUTTON_L2, MOSAIC_TRIGGER_PRESS, "other", NULL },
		{ "romm.flashback", MOSAIC_BIND_FN_PLUGIN, -1, MOSAIC_BUTTON_FN3, MOSAIC_TRIGGER_PRESS, "binder", NULL },
	};
	MosaicActionInfo info;
	ActionList_fill(&info, "romm.flashback", "FLASHBACK", "romm", 1, binds, 3);
	CHECK(info.size == sizeof(info));
	CHECK_STR(info.id, "romm.flashback");
	CHECK_STR(info.label, "FLASHBACK");
	CHECK_STR(info.plugin_id, "romm");
	CHECK(info.takes_path == 1);
	CHECK(info.binding_count == 2);
	CHECK(info.bindings[0].by == MOSAIC_BIND_BUTTON && info.bindings[0].screen == GL);
	CHECK(info.bindings[0].button == MOSAIC_BUTTON_Y && info.bindings[0].trigger == MOSAIC_TRIGGER_PRESS);
	CHECK_STR(info.bindings[0].plugin_id, "romm");
	CHECK_STR(info.bindings[0].setting, "button");
	CHECK(info.bindings[1].by == MOSAIC_BIND_FN_PLUGIN && info.bindings[1].screen == -1);
	CHECK_STR(info.bindings[1].plugin_id, "binder");
	CHECK_STR(info.bindings[1].setting, "");

	// the launcher's own, with no buttons
	ActionList_fill(&info, "gamelist.quick_menu", "MENU", NULL, 0, binds, 3);
	CHECK_STR(info.plugin_id, "");
	CHECK(info.takes_path == 0 && info.binding_count == 0);

	// more than fit are counted
	ActionBind many[MOSAIC_ACTION_BINDINGS + 3];
	for (int i = 0; i < MOSAIC_ACTION_BINDINGS + 3; i++) {
		many[i] = (ActionBind){ "x.run", MOSAIC_BIND_OVERRIDE, GL, i, MOSAIC_TRIGGER_PRESS, "x", NULL };
	}
	ActionList_fill(&info, "x.run", NULL, "x", 0, many, MOSAIC_ACTION_BINDINGS + 3);
	CHECK(info.binding_count == MOSAIC_ACTION_BINDINGS + 3);
	CHECK(info.bindings[MOSAIC_ACTION_BINDINGS - 1].button == MOSAIC_ACTION_BINDINGS - 1);
	CHECK_STR(info.label, "");
}

static void test_copy(void) {
	MosaicActionInfo info;
	ActionBind bind = { "a.b", MOSAIC_BIND_BUTTON, GL, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "a", NULL };
	ActionList_fill(&info, "a.b", "AB", "a", 0, &bind, 1);

	MosaicActionInfo out = { .size = sizeof(out) };
	CHECK(ActionList_copy(&info, &out) == 0);
	CHECK(out.size == sizeof(out) && out.binding_count == 1);
	CHECK_STR(out.id, "a.b");

	// a plugin built when the struct ended at plugin_id gets only that much
	MosaicActionInfo older;
	memset(&older, 0x7f, sizeof(older));
	older.size = offsetof(MosaicActionInfo, takes_path);
	CHECK(ActionList_copy(&info, &older) == 0);
	CHECK(older.size == offsetof(MosaicActionInfo, takes_path));
	CHECK_STR(older.plugin_id, "a");
	CHECK(older.takes_path == 0x7f7f7f7f); // left as it was

	MosaicActionInfo tiny = { .size = 1 };
	CHECK(ActionList_copy(&info, &tiny) == -1);
	CHECK(ActionList_copy(&info, NULL) == -1);
}

int main(void) {
	test_drop();
	test_fill();
	test_copy();
	CHECK_DONE();
}
