#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "profiles.h"

#define PROFILE_PATH_MAX 1024

static void say(const ProfileConfig* config, const char* message) {
	if (config->log) config->log(message);
}

static int there(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0;
}

// path's folders, as mkdir -p would make them, without path itself
static void makeParents(const char* path) {
	char dir[PROFILE_PATH_MAX];
	if (snprintf(dir, sizeof(dir), "%s", path) >= (int)sizeof(dir)) return;
	for (char* slash = strchr(dir + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
		*slash = '\0';
		mkdir(dir, 0755);
		*slash = '/';
	}
}

int Profile_nameValid(const char* name) {
	if (!name) return 0;
	size_t len = strlen(name);
	if (len == 0) return 1;
	if (len >= PROFILE_NAME_MAX || name[0] == '-' || !strcmp(name, PROFILE_OWNER_DIR)) return 0;
	for (const char* c = name; *c; c++) {
		if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return 0;
	}
	return 1;
}

void Profile_current(const ProfileConfig* config, char out[PROFILE_NAME_MAX]) {
	out[0] = '\0';
	FILE* f = fopen(config->state_path, "r");
	if (!f) return;
	char line[PROFILE_NAME_MAX * 2] = "";
	if (!fgets(line, sizeof(line), f)) line[0] = '\0';
	fclose(f);
	line[strcspn(line, "\r\n")] = '\0';
	// a valid name fits
	if (Profile_nameValid(line)) memcpy(out, line, strlen(line) + 1);
}

static int parkPath(const ProfileConfig* config, const char* name, const ProfileItem* item, char* out, size_t size) {
	int n = snprintf(out, size, "%s/%s/%s", config->parks_dir, name[0] ? name : PROFILE_OWNER_DIR, item->park);
	return n > 0 && (size_t)n < size ? 0 : -1;
}

static int move(const ProfileConfig* config, const char* from, const char* to) {
	makeParents(to);
	if (rename(from, to) == 0) return 0;
	char message[PROFILE_PATH_MAX * 2 + 64];
	snprintf(message, sizeof(message), "couldn't move %s to %s: %s", from, to, strerror(errno));
	say(config, message);
	return -1;
}

int Profile_settle(const ProfileConfig* config) {
	char name[PROFILE_NAME_MAX];
	Profile_current(config, name);
	int moved = 0, failed = 0;
	for (int i = 0; i < config->item_count; i++) {
		char park[PROFILE_PATH_MAX];
		if (parkPath(config, name, &config->items[i], park, sizeof(park)) != 0) continue;
		if (there(config->items[i].live) || !there(park)) continue;
		if (move(config, park, config->items[i].live) != 0) failed = 1;
		else {
			char message[PROFILE_PATH_MAX + 64];
			snprintf(message, sizeof(message), "put back %s, which a switch left parked", config->items[i].live);
			say(config, message);
			moved++;
		}
	}
	return failed ? -1 : moved;
}

static int saveName(const ProfileConfig* config, const char* name) {
	if (!name[0]) {
		if (unlink(config->state_path) == 0 || errno == ENOENT) return 0;
		return -1;
	}
	makeParents(config->state_path);
	char tmp[PROFILE_PATH_MAX];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", config->state_path) >= (int)sizeof(tmp)) return -1;
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	int ok = fprintf(f, "%s\n", name) > 0;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, config->state_path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

// A park that's taken: <park>.<time>, or -1 when that's taken too.
static int keptPath(const char* park, char* out, size_t size) {
	int n = snprintf(out, size, "%s.%lld", park, (long long)time(NULL));
	return n > 0 && (size_t)n < size && !there(out) ? 0 : -1;
}

int Profile_switch(const ProfileConfig* config, const char* name) {
	if (!Profile_nameValid(name) || config->item_count > PROFILE_ITEMS_MAX) return -1;
	if (Profile_settle(config) < 0) return -1;
	char from[PROFILE_NAME_MAX];
	Profile_current(config, from);
	if (!strcmp(from, name)) return 0;

	// the live items, parked under the profile they belong to
	char parked[PROFILE_ITEMS_MAX][PROFILE_PATH_MAX];
	int count = 0, failed = 0;
	for (int i = 0; i < config->item_count && !failed; i++) {
		parked[i][0] = '\0';
		if (!there(config->items[i].live)) continue;
		char park[PROFILE_PATH_MAX];
		if (parkPath(config, from, &config->items[i], park, sizeof(park)) != 0) {
			failed = 1;
			break;
		}
		if (there(park)) {
			char kept[PROFILE_PATH_MAX];
			if (keptPath(park, kept, sizeof(kept)) != 0) {
				failed = 1;
				break;
			}
			char message[PROFILE_PATH_MAX * 2 + 64];
			snprintf(message, sizeof(message), "%s is parked already, so the live one is kept as %s", park, kept);
			say(config, message);
			snprintf(park, sizeof(park), "%s", kept);
		}
		if (move(config, config->items[i].live, park) != 0) failed = 1;
		else {
			snprintf(parked[i], sizeof(parked[i]), "%s", park);
			count = i + 1;
		}
	}
	if (!failed && saveName(config, name) != 0) {
		say(config, "couldn't save which profile is live");
		failed = 1;
	}
	if (failed) {
		// back as it was
		for (int i = 0; i < count; i++) {
			if (parked[i][0]) move(config, parked[i], config->items[i].live);
		}
		return -1;
	}

	// what the switch is to, in place; what's missing is moved by the next settle
	for (int i = 0; i < config->item_count; i++) {
		char park[PROFILE_PATH_MAX];
		if (parkPath(config, name, &config->items[i], park, sizeof(park)) != 0 || !there(park)) continue;
		if (there(config->items[i].live)) {
			char message[PROFILE_PATH_MAX + 64];
			snprintf(message, sizeof(message), "%s came back meanwhile, so %s stays parked", config->items[i].live, park);
			say(config, message);
			continue;
		}
		if (move(config, park, config->items[i].live) != 0) failed = 1;
	}
	return failed ? -1 : 0;
}

int Profile_removeTree(const char* path) {
	struct stat st;
	if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode)) return unlink(path);
	DIR* dh = opendir(path);
	if (!dh) return -1;
	int failed = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;
		char child[PROFILE_PATH_MAX];
		if (snprintf(child, sizeof(child), "%s/%s", path, dp->d_name) >= (int)sizeof(child) || Profile_removeTree(child) != 0) failed = 1;
	}
	closedir(dh);
	if (rmdir(path) != 0) failed = 1;
	return failed ? -1 : 0;
}

int Profile_clear(const ProfileConfig* config, const char* name) {
	if (!name || !name[0] || !Profile_nameValid(name)) return -1;
	int failed = 0;
	char live[PROFILE_NAME_MAX];
	Profile_current(config, live);
	if (!strcmp(live, name)) {
		for (int i = 0; i < config->item_count; i++) {
			if (Profile_removeTree(config->items[i].live) != 0) failed = 1;
		}
	}
	DIR* dh = opendir(config->userdata_dir);
	if (dh) {
		struct dirent* dp;
		while ((dp = readdir(dh)) != NULL) {
			if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;
			char dir[PROFILE_PATH_MAX];
			if (snprintf(dir, sizeof(dir), "%s/%s/launcher/profiles/%s", config->userdata_dir, dp->d_name, name) >= (int)sizeof(dir)) {
				failed = 1;
				continue;
			}
			if (Profile_removeTree(dir) != 0) failed = 1;
		}
		closedir(dh);
	}
	// the parks, wherever they are
	char parks[PROFILE_PATH_MAX];
	if (snprintf(parks, sizeof(parks), "%s/%s", config->parks_dir, name) >= (int)sizeof(parks) || Profile_removeTree(parks) != 0) failed = 1;
	if (failed) say(config, "couldn't remove all of a profile's data");
	return failed ? -1 : 0;
}

int Profile_filePath(const char* launcher_dir, const char* name, const char* file, char* out, size_t size) {
	if (!out || !size) return -1;
	out[0] = '\0';
	if (!launcher_dir || !file || !file[0] || file[0] == '/' || !Profile_nameValid(name)) return -1;
	for (const char* part = file; *part;) {
		size_t len = strcspn(part, "/");
		if (len == 0 || (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.')) return -1;
		part += len;
		if (*part == '/') part++;
	}
	if (file[strlen(file) - 1] == '/') return -1;
	int n;
	if (!name[0]) n = snprintf(out, size, "%s/%s", launcher_dir, file);
	else {
		char dir[PROFILE_PATH_MAX];
		if (snprintf(dir, sizeof(dir), "%s/profiles/%s", launcher_dir, name) >= (int)sizeof(dir)) return -1;
		n = snprintf(out, size, "%s/%s", dir, file);
		if (n > 0 && (size_t)n < size) {
			makeParents(dir);
			mkdir(dir, 0755);
		}
	}
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}
