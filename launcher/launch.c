#include "launcher.h"

void queueNext(char* cmd) {
	LOG_info("cmd: %s\n", cmd);
	putFile("/tmp/next", cmd);
	state.quit = 1;
}

// based on https://stackoverflow.com/a/31775567/145965
int replaceString(char *line, const char *search, const char *replace) {
   char *sp; // start of pattern
   if ((sp = strstr(line, search)) == NULL) {
      return 0;
   }
   int count = 1;
   int sLen = strlen(search);
   int rLen = strlen(replace);
   if (sLen > rLen) {
      // move from right to left
      char *src = sp + sLen;
      char *dst = sp + rLen;
      while((*dst = *src) != '\0') { dst++; src++; }
   } else if (sLen < rLen) {
      // move from left to right
      int tLen = strlen(sp) - sLen;
      char *stop = sp + rLen;
      char *src = sp + sLen + tLen;
      char *dst = sp + rLen + tLen;
      while(dst >= stop) { *dst = *src; dst--; src--; }
   }
   memcpy(sp, replace, rLen);
   count += replaceString(sp + rLen, search, replace);
   return count;
}
char* escapeSingleQuotes(char* str) {
	// why not call replaceString directly?
	// call points require the modified string be returned
	// but replaceString is recursive and depends on its
	// own return value (but does it need to?)
	replaceString(str, "'", "'\\''");
	return str;
}
void readyResumePath(char* rom_path, int type) {
	char* tmp;
	state.can_resume = 0;
	state.has_preview = 0;
	char path[256];
	strcpy(path, rom_path);

	if (!prefixMatch(ROMS_PATH, path)) return;

	char auto_path[256];
	if (type==ENTRY_DIR) {
		if (!hasCue(path, auto_path)) { // no cue?
			tmp = strrchr(auto_path, '.') + 1; // extension
			strcpy(tmp, "m3u"); // replace with m3u
			if (!exists(auto_path)) return; // no m3u
		}
		strcpy(path, auto_path); // cue or m3u if one exists
	}

	if (!suffixMatch(".m3u", path)) {
		char m3u_path[256];
		if (hasM3u(path, m3u_path)) {
			// change path to m3u path
			strcpy(path, m3u_path);
		}
	}

	char emu_name[256];
	getEmuName(path, emu_name);

	char rom_file[256];
	tmp = strrchr(path, '/') + 1;
	strcpy(rom_file, tmp);

	sprintf(state.slot_path, "%s/.minui/%s/%s.txt", SHARED_USERDATA_PATH, emu_name, rom_file); // /.userdata/.minui/<EMU>/<romname>.ext.txt
	state.can_resume = exists(state.slot_path);

	// slot_path contains a single integer representing the last used slot
	if (state.can_resume) {
		char slot[16];
		getFile(state.slot_path, slot, 16);
		int s = atoi(slot);
		sprintf(state.preview_path, "%s/.minui/%s/%s.%0d.bmp", SHARED_USERDATA_PATH, emu_name, rom_file, s); // /.userdata/.minui/<EMU>/<romname>.ext.<n>.bmp
		state.has_preview = exists(state.preview_path);
	}
}
void readyResume(Entry* entry) {
	readyResumePath(entry->path, entry->type);
}
int autoResume(void) {
	// NOTE: bypasses recents

	if (!exists(AUTO_RESUME_PATH)) return 0;

	char path[256];
	getFile(AUTO_RESUME_PATH, path, 256);
	unlink(AUTO_RESUME_PATH);
	sync();

	// make sure rom still exists
	char sd_path[256];
	sprintf(sd_path, "%s%s", SDCARD_PATH, path);
	if (!exists(sd_path)) return 0;

	// make sure emu still exists
	char emu_name[256];
	getEmuName(sd_path, emu_name);

	char emu_path[256];
	getEmuPath(emu_name, emu_path);

	if (!exists(emu_path)) return 0;

	// putFile(LAST_PATH, FAUX_RECENT_PATH); // saveLast() will crash here because top is NULL

	char act[256];
	sprintf(act, "gametimectl.elf start '%s'", escapeSingleQuotes(sd_path));
	system(act);

	char cmd[256];
	// dont escape sd_path again because it was already escaped for gametimectl and function modifies input str aswell
	sprintf(cmd, "'%s' '%s'", escapeSingleQuotes(emu_path), sd_path);
	putInt(RESUME_SLOT_PATH, AUTO_RESUME_SLOT);
	queueNext(cmd);
	return 1;
}

// Queues the command a plugin's pre_launch replaced this launch with, if
// one did. target is the game's path, or NULL for a pak. Returns 0 to queue
// stock's command instead.
static int queueReplacement(const char* target) {
	const LaunchCmd* replacement = Plugins_launchReplacement();
	if (!replacement) return 0;
	char cmd[LAUNCH_CMD_SIZE];
	if (LaunchCmd_format(replacement, target, cmd, sizeof(cmd)) != 0) {
		LOG_warn("plugins: the command replacing this launch is too long, launching as before\n");
		return 0;
	}
	queueNext(cmd);
	return 1;
}

int openPak(char* path) {
	if (!Plugins_preLaunch(path, ENTRY_PAK)) return 0;

	// NOTE: escapeSingleQuotes() modifies the passed string
	// so we need to save the path before we call that
	// if (prefixMatch(ROMS_PATH, path)) {
	// 	addRecent(path);
	// }
	saveLast(path);
	if (queueReplacement(NULL)) return 1;

	char cmd[256];
	sprintf(cmd, "'%s/launch.sh'", escapeSingleQuotes(path));
	queueNext(cmd);
	return 1;
}

// Run the action bound to a user-assignable button (0 == FN1, 1 == FN2).
// Returns 1 if something was launched.
int runFnAction(int index) {
	const char* action = CFG_getFnAction(index);
	if (!action || !action[0]) return 0; // unassigned

	// a registered launcher action, eg. "action:gamelist.back"
	size_t action_prefix_len = strlen(FN_ACTION_ACTION_PREFIX);
	if (strncmp(action, FN_ACTION_ACTION_PREFIX, action_prefix_len)==0) {
		const Action* bound = Actions_find(action + action_prefix_len);
		if (!Action_available(bound)) return 0;
		Action_run(bound);
		return 1;
	}

	// otherwise "launch a Tools pak"; ignore kinds we don't know so a card
	// configured on a newer build just does nothing here instead of misfiring
	size_t prefix_len = strlen(FN_ACTION_PAK_PREFIX);
	if (strncmp(action, FN_ACTION_PAK_PREFIX, prefix_len)!=0) return 0;

	const char* rel = action + prefix_len;
	if (!rel[0]) return 0;

	char pak_path[256];
	snprintf(pak_path, sizeof(pak_path), "%s/Tools/%s/%s", SDCARD_PATH, PLATFORM, rel);

	char launch_path[256];
	snprintf(launch_path, sizeof(launch_path), "%s/launch.sh", pak_path);
	if (!exists(launch_path)) return 0; // stale binding, eg. the pak was deleted
	if (!Plugins_preLaunch(pak_path, ENTRY_PAK)) return 0;

	// unlike openPak() we save where the user *is*, not the pak itself, so exiting
	// the pak comes back to the same spot in the list
	if (state.top && state.top->entries->count>0) {
		Entry* entry = state.top->entries->items[state.top->selected];
		saveLast(entry->path);
	}
	if (queueReplacement(NULL)) return 1;

	char cmd[256];
	// NOTE: escapeSingleQuotes() modifies pak_path in place
	snprintf(cmd, sizeof(cmd), "'%s/launch.sh'", escapeSingleQuotes(pak_path));
	queueNext(cmd);
	return 1;
}
int openRom(char* path, char* last) {
	LOG_info("openRom(%s,%s)\n", path, last);

	// before anything is recorded: recents, resume slot, play time
	if (!Plugins_preLaunch(path, ENTRY_ROM)) {
		state.should_resume = 0;
		return 0;
	}

	char sd_path[256];
	strcpy(sd_path, path);

	char m3u_path[256];
	int has_m3u = hasM3u(sd_path, m3u_path);

	char recent_path[256];
	strcpy(recent_path, has_m3u ? m3u_path : sd_path);

	if (has_m3u && suffixMatch(".m3u", sd_path)) {
		getFirstDisc(m3u_path, sd_path);
	}

	char emu_name[256];
	getEmuName(sd_path, emu_name);

	if (state.should_resume) {
		char slot[16];
		getFile(state.slot_path, slot, 16);
		putFile(RESUME_SLOT_PATH, slot);
		state.should_resume = 0;

		if (has_m3u) {
			char rom_file[256];
			strcpy(rom_file, strrchr(m3u_path, '/') + 1);

			// get disc for state
			char disc_path_path[256];
			sprintf(disc_path_path, "%s/.minui/%s/%s.%s.txt", SHARED_USERDATA_PATH, emu_name, rom_file, slot); // /.userdata/arm-480/.minui/<EMU>/<romname>.ext.0.txt

			if (exists(disc_path_path)) {
				// switch to disc path
				char disc_path[256];
				getFile(disc_path_path, disc_path, 256);
				if (disc_path[0]=='/') strcpy(sd_path, disc_path); // absolute
				else { // relative
					strcpy(sd_path, m3u_path);
					char* tmp = strrchr(sd_path, '/') + 1;
					strcpy(tmp, disc_path);
				}
			}
		}
	}
	else putInt(RESUME_SLOT_PATH,8); // resume hidden default state

	char emu_path[256];
	getEmuPath(emu_name, emu_path);

	// NOTE: escapeSingleQuotes() modifies the passed string
	// so we need to save the path before we call that
	addRecent(recent_path, state.recent_alias); // yiiikes
	saveLast(last==NULL ? sd_path : last);
	char target[256]; // before escapeSingleQuotes rewrites sd_path
	strcpy(target, sd_path);
	char act[256];
	sprintf(act, "gametimectl.elf start '%s'", escapeSingleQuotes(sd_path));
	system(act);
	if (queueReplacement(target)) return 1;
	char cmd[256];
	// dont escape sd_path again because it was already escaped for gametimectl and function modifies input str aswell
	sprintf(cmd, "'%s' '%s'", escapeSingleQuotes(emu_path), sd_path);
	queueNext(cmd);
	return 1;
}
