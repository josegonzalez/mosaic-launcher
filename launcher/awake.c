#include "launcher.h"

#define MAX_HOLDERS 16
#define FILE_CHECK_MS 1000

static char holders[MAX_HOLDERS][64];
static int holder_count = 0;
static int file_held = 0;
static int applied = 0; // 1 while we've turned auto-sleep off
static unsigned long next_file_check = 0;

void Awake_set(const char* who, int on) {
	if (!who) return;
	for (int i = 0; i < holder_count; i++) {
		if (strcmp(holders[i], who) != 0) continue;
		if (!on) {
			memmove(&holders[i], &holders[i + 1], (size_t)(holder_count - i - 1) * sizeof(holders[0]));
			holder_count--;
		}
		return;
	}
	if (on && holder_count < MAX_HOLDERS) {
		snprintf(holders[holder_count++], sizeof(holders[0]), "%s", who);
	}
}

void Awake_tick(unsigned long now) {
	if (now >= next_file_check) {
		file_held = exists(STAY_AWAKE_PATH) || exists(STAY_ALIVE_PATH);
		next_file_check = now + FILE_CHECK_MS;
	}

	int want = file_held || holder_count > 0;
	if (want == applied) return;
	applied = want;
	if (want) {
		LOG_info("awake: holding off auto-sleep\n");
		PWR_disableAutosleep();
	}
	else {
		LOG_info("awake: auto-sleep allowed again\n");
		PWR_enableAutosleep();
	}
}
