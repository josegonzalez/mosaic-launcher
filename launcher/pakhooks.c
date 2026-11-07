#include <sys/wait.h>
#include "launcher.h"

#define MAX_HOOKS 32

static char hooks[MAX_HOOKS][512];
static int hook_count = 0;

static int byName(const void* a, const void* b) {
	return strcasecmp((const char*)a, (const char*)b);
}

void PakHooks_init(void) {
	hook_count = 0;
	DIR* dh = opendir(TOOLS_PATH);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL && hook_count < MAX_HOOKS) {
		if (hide(dp->d_name) || !suffixMatch(".pak", dp->d_name)) continue;
		char script[512];
		snprintf(script, sizeof(script), "%s/%s/pre-launch.sh", TOOLS_PATH, dp->d_name);
		if (exists(script)) snprintf(hooks[hook_count++], sizeof(hooks[0]), "%s", script);
	}
	closedir(dh);
	qsort(hooks, hook_count, sizeof(hooks[0]), byName);
}

int PakHooks_preLaunch(const char* path, int type) {
	if (hook_count == 0) return 1;

	setenv("HOOK_PHASE", "pre", 1);
	setenv("HOOK_TYPE", type == ENTRY_PAK ? "pak" : "rom", 1);
	setenv("HOOK_ROM_PATH", type == ENTRY_PAK ? "" : path, 1);
	setenv("HOOK_PAK_PATH", type == ENTRY_PAK ? path : "", 1);

	int go_ahead = 1;
	for (int i = 0; i < hook_count && go_ahead; i++) {
		char quoted[600];
		snprintf(quoted, sizeof(quoted), "%s", hooks[i]);
		char cmd[1400];
		// escapeSingleQuotes rewrites in place, so it gets its own copy
		snprintf(cmd, sizeof(cmd), "sh '%s' >/dev/null 2>&1", escapeSingleQuotes(quoted));
		int status = system(cmd);
		int code = (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
		if (code == PAK_HOOK_CANCEL) {
			LOG_info("pakhooks: %s cancelled the launch of %s\n", hooks[i], path);
			go_ahead = 0;
		}
		else if (code != 0) {
			LOG_warn("pakhooks: %s failed (%i), launching anyway\n", hooks[i], code);
		}
	}

	unsetenv("HOOK_PHASE");
	unsetenv("HOOK_TYPE");
	unsetenv("HOOK_ROM_PATH");
	unsetenv("HOOK_PAK_PATH");
	return go_ahead;
}
