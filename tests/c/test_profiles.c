// Profiles: swapping recents, the play history and Collections in place,
// settling after a cut, clearing a profile and plugins' files' paths.

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "profiles.h"

#define CARD TEST_DIR "/card"
#define SHARED CARD "/.userdata/shared"
#define RECENTS SHARED "/.minui/recent.txt"
#define PLAY_DB SHARED "/game_logs.sqlite"
#define COLLECTIONS CARD "/Collections"
#define PARKS SHARED "/launcher/profiles"
#define STATE SHARED "/launcher/profile.txt"
#define PLATFORM_LAUNCHER CARD "/.userdata/desktop/launcher"

static const ProfileItem items[] = {
	{ RECENTS, "recent.txt" },
	{ PLAY_DB, "game_logs.sqlite" },
	{ PLAY_DB "-journal", "game_logs.sqlite-journal" },
	{ COLLECTIONS, "Collections" },
};

static char logged[4096];

static void logLine(const char* message) {
	size_t used = strlen(logged);
	snprintf(logged + used, sizeof(logged) - used, "%s\n", message);
}

static const ProfileConfig config = {
	.state_path = STATE,
	.parks_dir = PARKS,
	.items = items,
	.item_count = sizeof(items) / sizeof(items[0]),
	.userdata_dir = CARD "/.userdata",
	.log = logLine,
};

static void makeDirs(const char* path) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s/", path);
	for (char* slash = strchr(dir + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
		*slash = '\0';
		mkdir(dir, 0755);
		*slash = '/';
	}
}

static void writeFile(const char* path, const char* text) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	*strrchr(dir, '/') = '\0';
	makeDirs(dir);
	FILE* f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

// the file's text, or "-" when it isn't there
static const char* text(const char* path) {
	static char out[512];
	FILE* f = fopen(path, "r");
	if (!f) return "-";
	size_t n = fread(out, 1, sizeof(out) - 1, f);
	out[n] = '\0';
	fclose(f);
	return out;
}

static int there(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0;
}

static const char* current(void) {
	static char name[PROFILE_NAME_MAX];
	Profile_current(&config, name);
	return name;
}

int main(void) {
	Profile_removeTree(TEST_DIR "/card");

	// names
	CHECK(Profile_nameValid(""));
	CHECK(Profile_nameValid("guest"));
	CHECK(Profile_nameValid("kid-2"));
	CHECK(!Profile_nameValid(NULL));
	CHECK(!Profile_nameValid("owner"));
	CHECK(!Profile_nameValid("Guest"));
	CHECK(!Profile_nameValid("-guest"));
	CHECK(!Profile_nameValid("a/b"));
	CHECK(!Profile_nameValid(".."));
	CHECK(!Profile_nameValid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")); // 32
	CHECK(Profile_nameValid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")); // 31

	// the owner's data, and nothing switched yet
	writeFile(RECENTS, "/Roms/GB/Owner.gb\n");
	writeFile(PLAY_DB, "owner db");
	writeFile(COLLECTIONS "/Favorites.txt", "/Roms/GB/Owner.gb\n");
	CHECK_STR(current(), "");
	CHECK(Profile_settle(&config) == 0);
	CHECK(Profile_switch(&config, "") == 0);
	CHECK(!there(STATE));

	// to the guest: the owner's items parked, the guest starting with none
	CHECK(Profile_switch(&config, "owner") == -1);
	CHECK(Profile_switch(&config, "Guest") == -1);
	CHECK(Profile_switch(&config, "guest") == 0);
	CHECK_STR(current(), "guest");
	CHECK_STR(text(STATE), "guest\n");
	CHECK(!there(RECENTS));
	CHECK(!there(PLAY_DB));
	CHECK(!there(COLLECTIONS));
	CHECK_STR(text(PARKS "/owner/recent.txt"), "/Roms/GB/Owner.gb\n");
	CHECK_STR(text(PARKS "/owner/game_logs.sqlite"), "owner db");
	CHECK_STR(text(PARKS "/owner/Collections/Favorites.txt"), "/Roms/GB/Owner.gb\n");
	CHECK(!there(PARKS "/owner/game_logs.sqlite-journal"));
	CHECK(Profile_switch(&config, "guest") == 0); // already live

	// the guest plays and favourites something
	writeFile(RECENTS, "/Roms/GB/Guest.gb\n");
	writeFile(PLAY_DB, "guest db");
	writeFile(COLLECTIONS "/Favorites.txt", "/Roms/GB/Guest.gb\n");

	// back to the owner: the owner's back, the guest's kept
	CHECK(Profile_switch(&config, "") == 0);
	CHECK_STR(current(), "");
	CHECK(!there(STATE));
	CHECK_STR(text(RECENTS), "/Roms/GB/Owner.gb\n");
	CHECK_STR(text(PLAY_DB), "owner db");
	CHECK_STR(text(COLLECTIONS "/Favorites.txt"), "/Roms/GB/Owner.gb\n");
	CHECK(!there(PARKS "/owner/recent.txt"));
	CHECK_STR(text(PARKS "/guest/recent.txt"), "/Roms/GB/Guest.gb\n");
	CHECK_STR(text(PARKS "/guest/game_logs.sqlite"), "guest db");
	CHECK_STR(text(PARKS "/guest/Collections/Favorites.txt"), "/Roms/GB/Guest.gb\n");

	// and to the guest again: the guest's data as it was left
	CHECK(Profile_switch(&config, "guest") == 0);
	CHECK_STR(text(RECENTS), "/Roms/GB/Guest.gb\n");
	CHECK_STR(text(PLAY_DB), "guest db");
	CHECK_STR(text(COLLECTIONS "/Favorites.txt"), "/Roms/GB/Guest.gb\n");
	CHECK_STR(text(PARKS "/owner/recent.txt"), "/Roms/GB/Owner.gb\n");

	// a cut after the guest's items were parked but before the name was
	// saved: the guest is still live, and settling puts its items back
	rename(RECENTS, PARKS "/guest/recent.txt");
	rename(PLAY_DB, PARKS "/guest/game_logs.sqlite");
	logged[0] = '\0';
	CHECK(Profile_settle(&config) == 2);
	CHECK_STR(text(RECENTS), "/Roms/GB/Guest.gb\n");
	CHECK_STR(text(PLAY_DB), "guest db");
	CHECK(strstr(logged, "put back " RECENTS) != NULL);

	// a cut after the name was saved but before the owner's items moved in:
	// the next switch settles first, so the owner's are in place
	rename(RECENTS, PARKS "/guest/recent.txt");
	rename(PLAY_DB, PARKS "/guest/game_logs.sqlite");
	rename(COLLECTIONS, PARKS "/guest/Collections");
	unlink(STATE);
	CHECK(Profile_settle(&config) == 3);
	CHECK_STR(text(RECENTS), "/Roms/GB/Owner.gb\n");
	CHECK_STR(text(PLAY_DB), "owner db");
	CHECK_STR(text(PARKS "/guest/recent.txt"), "/Roms/GB/Guest.gb\n");

	// a park that's taken (a database made at its path meanwhile) is kept beside it
	writeFile(PARKS "/owner/game_logs.sqlite", "stray");
	logged[0] = '\0';
	CHECK(Profile_switch(&config, "guest") == 0);
	CHECK_STR(text(PARKS "/owner/game_logs.sqlite"), "stray");
	CHECK(strstr(logged, "is parked already, so the live one is kept as " PARKS "/owner/game_logs.sqlite.") != NULL);
	CHECK_STR(text(PLAY_DB), "guest db");
	Profile_removeTree(PARKS "/owner/game_logs.sqlite");
	CHECK(Profile_switch(&config, "") == 0);
	// the kept one is the owner's and stays parked; the owner starts with none
	CHECK(!there(PLAY_DB));
	CHECK(there(PARKS "/guest/game_logs.sqlite"));

	// plugins' files
	char path[512];
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "", "tags.txt", path, sizeof(path)) == 0);
	CHECK_STR(path, PLATFORM_LAUNCHER "/tags.txt");
	CHECK(!there(PLATFORM_LAUNCHER "/profiles"));
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "gamification/state.txt", path, sizeof(path)) == 0);
	CHECK_STR(path, PLATFORM_LAUNCHER "/profiles/guest/gamification/state.txt");
	CHECK(there(PLATFORM_LAUNCHER "/profiles/guest"));
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "", path, sizeof(path)) == -1);
	CHECK_STR(path, "");
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "/etc/passwd", path, sizeof(path)) == -1);
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "../tags.txt", path, sizeof(path)) == -1);
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "a/../../b", path, sizeof(path)) == -1);
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "a//b", path, sizeof(path)) == -1);
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "./b", path, sizeof(path)) == -1);
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", "dir/", path, sizeof(path)) == -1);
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "guest", ".hidden", path, sizeof(path)) == 0);
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "owner", "tags.txt", path, sizeof(path)) == -1);
	char small[16];
	CHECK(Profile_filePath(PLATFORM_LAUNCHER, "", "tags.txt", small, sizeof(small)) == -1);
	CHECK_STR(small, "");

	// clearing the guest while the owner's live: its parks and plugins' files
	writeFile(PLATFORM_LAUNCHER "/profiles/guest/tags.txt", "guest tags");
	writeFile(PLATFORM_LAUNCHER "/tags.txt", "owner tags");
	writeFile(CARD "/.userdata/tg5040/launcher/profiles/guest/tags.txt", "guest tags elsewhere");
	CHECK(Profile_clear(&config, "") == -1);
	CHECK(Profile_clear(&config, "owner") == -1);
	CHECK(Profile_clear(&config, "guest") == 0);
	CHECK(!there(PARKS "/guest"));
	CHECK(!there(PLATFORM_LAUNCHER "/profiles/guest"));
	CHECK(!there(CARD "/.userdata/tg5040/launcher/profiles/guest"));
	CHECK_STR(text(PLATFORM_LAUNCHER "/tags.txt"), "owner tags");
	CHECK_STR(text(RECENTS), "/Roms/GB/Owner.gb\n");
	CHECK_STR(text(COLLECTIONS "/Favorites.txt"), "/Roms/GB/Owner.gb\n");

	// clearing the guest while it's live: its live items, not the owner's parks
	CHECK(Profile_switch(&config, "guest") == 0);
	writeFile(RECENTS, "/Roms/GB/Guest.gb\n");
	writeFile(COLLECTIONS "/Guest.txt", "/Roms/GB/Guest.gb\n");
	writeFile(PLATFORM_LAUNCHER "/profiles/guest/tags.txt", "guest tags");
	CHECK(Profile_clear(&config, "guest") == 0);
	CHECK_STR(current(), "guest");
	CHECK(!there(RECENTS));
	CHECK(!there(COLLECTIONS));
	CHECK(!there(PLATFORM_LAUNCHER "/profiles/guest"));
	CHECK_STR(text(PARKS "/owner/recent.txt"), "/Roms/GB/Owner.gb\n");
	CHECK(Profile_switch(&config, "") == 0);
	CHECK_STR(text(RECENTS), "/Roms/GB/Owner.gb\n");
	CHECK_STR(text(COLLECTIONS "/Favorites.txt"), "/Roms/GB/Owner.gb\n");

	// removing a tree doesn't follow links out of it
	writeFile(TEST_DIR "/card/outside/keep.txt", "kept");
	makeDirs(TEST_DIR "/card/tree/sub");
	writeFile(TEST_DIR "/card/tree/sub/file.txt", "gone");
	CHECK(symlink(TEST_DIR "/card/outside", TEST_DIR "/card/tree/link") == 0);
	CHECK(Profile_removeTree(TEST_DIR "/card/tree") == 0);
	CHECK(!there(TEST_DIR "/card/tree"));
	CHECK_STR(text(TEST_DIR "/card/outside/keep.txt"), "kept");
	CHECK(Profile_removeTree(TEST_DIR "/card/no-such") == 0);

	// a bad name in the state file is the owner's
	writeFile(STATE, "../../etc\n");
	CHECK_STR(current(), "");
	unlink(STATE);

	Profile_removeTree(TEST_DIR "/card");
	CHECK_DONE();
}
