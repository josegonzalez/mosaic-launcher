// Multi-disc: loose "(Disc N)" files in a system folder show as one game,
// without moving them.
//
// disc_model.h says which files are the discs of one game. dir_entries
// adds a row for each such game, at its .m3u's path beside the discs, and
// filter hides the discs; once the .m3u is on the card it's the folder's
// own row. The first time the game opens (entry_open), the plugin writes
// the .m3u, listing the discs in order, if there's none, and the launcher
// launches it. That first time only, the saves and states minarch kept
// under the first disc's name are copied to the .m3u's name, where none is
// there already, and the first disc's line in recents and its favorite
// move to the .m3u; the .m3u paths it's done this for are kept in
// .userdata/<platform>/launcher/multi-disc.txt. art_alias shows the first
// disc's art for a game with none of its own.
//
// minarch only changes discs for a game in stock's own layout, a folder
// holding the discs and an .m3u named after it, so Combine discs (on Y,
// after a confirm) moves a game's discs, with their tracks, into a folder
// of that name, with its .m3u, and points recents, favorites, tags and play
// time at the new paths. Stock then shows the folder as the game, and the
// plugin leaves it alone, as it does every folder already in that layout.
//
// Nothing is ever overwritten or deleted: an .m3u, save or state already
// there is kept, copies are made only where there's nothing, and Combine
// discs stops when the folder, or anything it would write, is there.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "disc_model.h"
#include "mosaic_plugin.h"

#define ID "multi-disc"
#define PATH_LEN 1024
#define FOLDERS 4 // folders kept read
#define M3U_MAX 8192
// an action label the launcher translates where it draws it, for make pot
#define TR_MARK(text) (text)

static const MosaicHost* host;
static char roms_path[PATH_LEN];
static char done_path[PATH_LEN]; // <userdata>/launcher/multi-disc.txt

static void say(const char* message) {
	host->log(ID, message);
}

__attribute__((format(printf, 1, 2)))
static void sayf(const char* fmt, ...) {
	char message[PATH_LEN * 3];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	say(message);
}

static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int exists(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0;
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// The folder a path is in, into out; 0 when it has none.
static int parentOf(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path || (size_t)(slash - path) >= size) return 0;
	snprintf(out, size, "%.*s", (int)(slash - path), path);
	return 1;
}

// 1 for a folder in a system folder under Roms.
static int underRoms(const char* dir) {
	size_t n = strlen(roms_path);
	return dir && !strncmp(dir, roms_path, n) && dir[n] == '/' && dir[n + 1];
}

// <dir>/<name> into out. Returns 1, or 0 when it doesn't fit, writing "".
static int join(char* out, size_t size, const char* dir, const char* name) {
	size_t a = strlen(dir), b = strlen(name);
	if (!size) return 0;
	out[0] = '\0';
	if (a + 1 + b >= size) return 0;
	memcpy(out, dir, a);
	out[a] = '/';
	memcpy(out + a + 1, name, b + 1);
	return 1;
}

///////////////////////////////////////
// folders, read once while they don't change

typedef struct Folder {
	char path[PATH_LEN];
	time_t mtime;
	unsigned long used; // when it was last asked for, to drop the oldest
	char** names;       // its files
	int count;
	MDGroups groups;
} Folder;

static Folder folders[FOLDERS];
static unsigned long uses = 0;

static void forget(Folder* f) {
	for (int i = 0; i < f->count; i++) free(f->names[i]);
	free(f->names);
	MD_groupsFree(&f->groups);
	memset(f, 0, sizeof(*f));
}

static void forgetAll(void) {
	for (int i = 0; i < FOLDERS; i++) forget(&folders[i]);
}

static int readNames(Folder* f) {
	DIR* dh = opendir(f->path);
	if (!dh) return 0;
	int cap = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.') continue;
		int file = dp->d_type == DT_REG;
		if (dp->d_type == DT_UNKNOWN || dp->d_type == DT_LNK) {
			char path[PATH_LEN];
			file = join(path, sizeof(path), f->path, dp->d_name) && isFile(path);
		}
		if (!file) continue;
		if (f->count == cap) {
			int next = cap ? cap * 2 : 64;
			char** bigger = realloc(f->names, sizeof(char*) * (size_t)next);
			if (!bigger) break;
			f->names = bigger;
			cap = next;
		}
		if ((f->names[f->count] = strdup(dp->d_name)) != NULL) f->count++;
	}
	closedir(dh);
	return 1;
}

// A folder under Roms with what's in it, or NULL. One already read is used
// while its time is the same, once that time is more than 3 seconds old,
// as FAT's 2 second clock wouldn't show a second change.
static Folder* folderAt(const char* dir) {
	if (!underRoms(dir) || strlen(dir) >= PATH_LEN) return NULL;
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;
	time_t now = time(NULL);
	Folder* oldest = &folders[0];
	for (int i = 0; i < FOLDERS; i++) {
		Folder* f = &folders[i];
		if (f->path[0] && !strcmp(f->path, dir)) {
			if (f->mtime == st.st_mtime && now - st.st_mtime > 3) {
				f->used = ++uses;
				return f;
			}
			oldest = f;
			break;
		}
		if (!f->path[0] || f->used < oldest->used) oldest = f;
	}
	Folder* f = oldest;
	forget(f);
	snprintf(f->path, sizeof(f->path), "%s", dir);
	f->mtime = st.st_mtime;
	f->used = ++uses;
	if (!readNames(f)) {
		forget(f);
		return NULL;
	}
	const char* own = strrchr(dir, '/') + 1;
	// a folder in stock's own layout is stock's
	if (!MD_stockFolder(own, (const char* const*)f->names, f->count)) {
		if (MD_group((const char* const*)f->names, f->count, &f->groups) < 0) say("out of memory reading a folder");
	}
	return f;
}

// The game whose .m3u is at path, and its folder, or -1.
static int gameAt(const char* path, Folder** out) {
	char dir[PATH_LEN];
	if (!path || !endsWith(path, ".m3u") || !parentOf(path, dir, sizeof(dir))) return -1;
	Folder* f = folderAt(dir);
	if (!f) return -1;
	*out = f;
	return MD_findGroup(&f->groups, strrchr(path, '/') + 1);
}

static const char* discName(const Folder* f, const MDGroup* g, int i) {
	return f->names[g->discs[i]];
}

///////////////////////////////////////
// the list

static void dirEntries(MosaicList* list, const char* dir_path) {
	if (!underRoms(dir_path)) return;
	// every folder the list was read from: a system's can be several
	char seen[FOLDERS * 2][PATH_LEN];
	int seen_count = 0;
	int count = host->list_count(list);
	for (int i = 0; i < count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		char dir[PATH_LEN];
		if (host->entry_type(entry) != MOSAIC_ENTRY_ROM || !parentOf(host->entry_path(entry), dir, sizeof(dir))) continue;
		int known = 0;
		for (int k = 0; k < seen_count && !known; k++) known = !strcmp(seen[k], dir);
		if (known || seen_count == (int)(sizeof(seen) / sizeof(seen[0]))) continue;
		snprintf(seen[seen_count++], PATH_LEN, "%s", dir);
	}
	for (int k = 0; k < seen_count; k++) {
		Folder* f = folderAt(seen[k]);
		if (!f) continue;
		for (int g = 0; g < f->groups.count; g++) {
			char m3u[PATH_LEN];
			if (!join(m3u, sizeof(m3u), f->path, f->groups.items[g].m3u) || exists(m3u)) continue;
			host->list_push(list, host->entry_new(m3u, MOSAIC_ENTRY_ROM, NULL));
		}
	}
}

// The discs of a game it groups, in a system's folders.
static int filter(const MosaicEntry* entry, const char* dir_path) {
	if (!underRoms(dir_path) || host->entry_type(entry) != MOSAIC_ENTRY_ROM) return 1;
	const char* path = host->entry_path(entry);
	char dir[PATH_LEN];
	if (!parentOf(path, dir, sizeof(dir))) return 1;
	Folder* f = folderAt(dir);
	if (!f || !f->groups.count) return 1;
	return MD_findDisc(&f->groups, (const char* const*)f->names, strrchr(path, '/') + 1) < 0;
}

static int artAlias(const MosaicEntry* entry, char* out, size_t size) {
	Folder* f;
	int g = gameAt(host->entry_path(entry), &f);
	if (g < 0) return 0;
	return join(out, size, f->path, discName(f, &f->groups.items[g], 0));
}

///////////////////////////////////////
// the first launch

// 1 when this card-relative .m3u path is in the list of games done.
static int done(const char* rel) {
	FILE* fp = fopen(done_path, "r");
	if (!fp) return 0;
	char line[PATH_LEN];
	int found = 0;
	while (!found && fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\r\n")] = '\0';
		found = !strcmp(line, rel);
	}
	fclose(fp);
	return found;
}

static void markDone(const char* rel) {
	char dir[PATH_LEN];
	snprintf(dir, sizeof(dir), "%s/launcher", host->userdata_path);
	mkdir(dir, 0755);
	FILE* fp = fopen(done_path, "a");
	if (!fp) {
		say("couldn't save the list of games done");
		return;
	}
	fprintf(fp, "%s\n", rel);
	fclose(fp);
}

static const char* relative(const char* path) {
	return path + strlen(host->sdcard_path);
}

// Writes a new file, never over one that's there. Returns 0, or -1.
static int writeNew(const char* path, const char* text, size_t len) {
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) return -1;
	ssize_t wrote = write(fd, text, len);
	int ok = wrote == (ssize_t)len && fsync(fd) == 0;
	if (close(fd) != 0) ok = 0;
	if (!ok) unlink(path); // only ever the file just made
	return ok ? 0 : -1;
}

// Copies from to a new file to. Returns 1 when it copied, 0 when there's
// nothing to copy or to is there already, -1 when the copy failed.
static int copyNew(const char* from, const char* to) {
	if (!isFile(from) || exists(to)) return 0;
	int in = open(from, O_RDONLY);
	if (in < 0) return -1;
	int out = open(to, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (out < 0) {
		close(in);
		return errno == EEXIST ? 0 : -1;
	}
	char buffer[16384];
	ssize_t n;
	int ok = 1;
	while (ok && (n = read(in, buffer, sizeof(buffer))) != 0) {
		if (n < 0) ok = 0;
		else ok = write(out, buffer, (size_t)n) == n;
	}
	close(in);
	if (fsync(out) != 0) ok = 0;
	if (close(out) != 0) ok = 0;
	if (!ok) unlink(to);
	return ok ? 1 : -1;
}

// The tag minarch files a system's saves under: its folder's last (...),
// or its whole name.
static int systemTag(const char* path, char* out, size_t size) {
	size_t n = strlen(roms_path);
	if (strncmp(path, roms_path, n) != 0 || path[n] != '/') return 0;
	const char* system = path + n + 1;
	size_t len = strcspn(system, "/");
	char name[256];
	if (!len || len >= sizeof(name)) return 0;
	snprintf(name, sizeof(name), "%.*s", (int)len, system);
	char* open_paren = strrchr(name, '(');
	if (open_paren) {
		char* close_paren = strchr(open_paren, ')');
		if (!close_paren || close_paren == open_paren + 1) return 0;
		*close_paren = '\0';
		if (snprintf(out, size, "%s", open_paren + 1) >= (int)size) return 0;
	}
	else if (snprintf(out, size, "%s", name) >= (int)size) return 0;
	return out[0] != '\0';
}

// Copies the first disc's saves and states to the .m3u's names. Returns
// how many it copied.
static int copySaves(const char* any_path, const char* disc, const char* m3u) {
	char tag[128];
	if (!systemTag(any_path, tag, sizeof(tag))) return 0;
	char saves[PATH_LEN], shared[PATH_LEN], slots[PATH_LEN];
	if (snprintf(saves, sizeof(saves), "%s/Saves/%s", host->sdcard_path, tag) >= (int)sizeof(saves)
			|| snprintf(shared, sizeof(shared), "%s/.userdata/shared", host->sdcard_path) >= (int)sizeof(shared)
			|| snprintf(slots, sizeof(slots), "%s/.minui/%s", shared, tag) >= (int)sizeof(slots)) return 0;

	// every <tag>-<core> folder
	char states[16][PATH_LEN];
	int state_count = 0;
	DIR* dh = opendir(shared);
	struct dirent* dp;
	size_t tag_len = strlen(tag);
	while (dh && state_count < 16 && (dp = readdir(dh)) != NULL) {
		if (strncmp(dp->d_name, tag, tag_len) != 0 || dp->d_name[tag_len] != '-' || !dp->d_name[tag_len + 1]) continue;
		struct stat st;
		if (join(states[state_count], PATH_LEN, shared, dp->d_name) && stat(states[state_count], &st) == 0 && S_ISDIR(st.st_mode)) state_count++;
	}
	if (dh) closedir(dh);

	MDCopy copies[MD_COPIES_MAX];
	int count = MD_saveCopies(disc, m3u, copies, MD_COPIES_MAX);
	int copied = 0;
	for (int i = 0; i < count; i++) {
		int places = copies[i].place == MD_STATES ? state_count : 1;
		for (int p = 0; p < places; p++) {
			const char* dir = copies[i].place == MD_SAVES ? saves : copies[i].place == MD_SLOTS ? slots : states[p];
			char from[PATH_LEN], to[PATH_LEN];
			if (!join(from, sizeof(from), dir, copies[i].from) || !join(to, sizeof(to), dir, copies[i].to)) continue;
			int result = copyNew(from, to);
			if (result > 0) {
				copied++;
				sayf("copied %s to %s", relative(from), copies[i].to);
			}
			else if (result < 0) sayf("couldn't copy %s to %s", relative(from), copies[i].to);
		}
	}
	return copied;
}

// Points recents at from to to, dropping one that would then be there
// twice.
static void moveRecent(const char* from, const char* to) {
	if (!MOSAIC_HOST_HAS(host, recent_set)) return;
	for (int i = host->recents_count() - 1; i >= 0; i--) {
		const char* recent = host->recent_path(i);
		if (!recent || strcmp(recent, from) != 0) continue;
		if (host->recent_set(i, to) == 0) sayf("recent %s is now %s", relative(from), relative(to));
		else if (host->recent_remove(i) == 0) sayf("recent %s was there twice as %s", relative(from), relative(to));
	}
}

// Asks a plugin's move service to move a path, when one is on.
static void moveIn(const char* service, const char* from, const char* to) {
	if (!MOSAIC_HOST_HAS(host, call) || !MOSAIC_HOST_HAS(host, has_capability) || !host->has_capability(service)) return;
	char args[PATH_LEN * 2 + 16], reply[MOSAIC_CALL_MAX];
	snprintf(args, sizeof(args), "from=%s\nto=%s", from, to);
	if (host->call(service, args, reply, sizeof(reply)) < 0) sayf("%s couldn't move %s", service, relative(from));
}

// Once per game: the first disc's saves and states copied, and its recent
// and favorite moved, to the .m3u.
static void carryOver(const Folder* f, const MDGroup* g, const char* m3u_path) {
	const char* rel = relative(m3u_path);
	if (done(rel)) return;
	char disc_path[PATH_LEN];
	if (!join(disc_path, sizeof(disc_path), f->path, discName(f, g, 0))) return;
	int copied = copySaves(m3u_path, discName(f, g, 0), g->m3u);
	moveRecent(disc_path, m3u_path);
	moveIn("favorites.move", disc_path, m3u_path);
	markDone(rel);
	sayf("carried %s over to %s, %i save and state files copied", discName(f, g, 0), g->m3u, copied);
}

static int entryOpen(const char* path, int type) {
	if (type != MOSAIC_ENTRY_ROM) return 0;
	Folder* f;
	int index = gameAt(path, &f);
	if (index < 0) return 0;
	MDGroup group = f->groups.items[index]; // a copy: writing changes the folder
	char m3u_path[PATH_LEN];
	snprintf(m3u_path, sizeof(m3u_path), "%s", path);
	if (!exists(m3u_path)) {
		char text[M3U_MAX];
		int len = MD_m3uText(&group, (const char* const*)f->names, text, sizeof(text));
		if (len < 0 || writeNew(m3u_path, text, (size_t)len) != 0) {
			sayf("couldn't write %s", relative(m3u_path));
			char message[PATH_LEN];
			snprintf(message, sizeof(message), tr("Couldn't write %s"), group.m3u);
			toast(message);
			return 1; // nothing to launch
		}
		sayf("wrote %s with %i discs", relative(m3u_path), group.count);
	}
	carryOver(f, &group, m3u_path);
	return 0;
}

///////////////////////////////////////
// Combine discs

// The game the confirm is asking about.
static char combining[PATH_LEN];

static int selectedGame(Folder** f) {
	const MosaicEntry* entry = host->selected_entry();
	if (!entry || host->entry_type(entry) != MOSAIC_ENTRY_ROM) return -1;
	return gameAt(host->entry_path(entry), f);
}

static int canCombine(const char* path, void* data) {
	(void)path;
	(void)data;
	Folder* f;
	return selectedGame(&f) >= 0;
}

// stock's thumbnail for a game file or folder: <dir>/.media/<name up to its last dot>.png
static int thumbnail(const char* dir, const char* name, char* out, size_t size) {
	const char* dot = strrchr(name, '.');
	int len = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
	int n = snprintf(out, size, "%s/.media/%.*s.png", dir, len, name);
	return n >= 0 && (size_t)n < size;
}

typedef struct Move {
	char from[PATH_LEN];
	char to[PATH_LEN];
} Move;

static void putBack(const Move* moves, int count, const char* folder) {
	for (int i = count - 1; i >= 0; i--) {
		if (rename(moves[i].to, moves[i].from) != 0) sayf("couldn't put %s back", relative(moves[i].from));
	}
	rmdir(folder);
}

static void refuse(const char* log, const char* shown, const char* name) {
	sayf("not combining %s: %s", name, log);
	char message[PATH_LEN];
	snprintf(message, sizeof(message), tr(shown), name);
	toast(message);
}

static void combine(const char* m3u_path) {
	Folder* f;
	int index = gameAt(m3u_path, &f);
	if (index < 0) return;
	MDGroup group = f->groups.items[index];
	char dir[PATH_LEN];
	snprintf(dir, sizeof(dir), "%s", f->path);
	char base[MD_NAME_LEN];
	snprintf(base, sizeof(base), "%.*s", (int)(strlen(group.m3u) - 4), group.m3u);
	char folder[PATH_LEN], new_m3u[PATH_LEN];
	if (!join(folder, sizeof(folder), dir, base) || !join(new_m3u, sizeof(new_m3u), folder, group.m3u)) return;
	if (exists(folder)) {
		refuse("the folder is there already", TR_MARK("%s is there already"), base);
		return;
	}

	// the discs and the files that go with them
	Move* moves = calloc(MD_DISCS_MAX * 8, sizeof(Move));
	const char* names[MD_DISCS_MAX * 8];
	int count = 0;
	if (!moves) return;
	for (int i = 0; i < f->count && count < MD_DISCS_MAX * 8; i++) {
		const char* name = f->names[i];
		int take = 0;
		for (int d = 0; d < group.count && !take; d++) take = !strcmp(name, discName(f, &group, d)) || MD_companion(discName(f, &group, d), name);
		if (take && MD_findDisc(&f->groups, (const char* const*)f->names, name) >= 0) {
			take = MD_findDisc(&f->groups, (const char* const*)f->names, name) == index;
		}
		if (!take) continue;
		if (!join(moves[count].from, PATH_LEN, dir, name) || !join(moves[count].to, PATH_LEN, folder, name)) {
			free(moves);
			return;
		}
		names[count++] = name;
	}

	// the .m3u: the loose one again for the folder, or a new one
	char text[M3U_MAX];
	int len;
	int had_m3u = isFile(m3u_path);
	if (had_m3u) {
		char loose[M3U_MAX];
		FILE* fp = fopen(m3u_path, "r");
		size_t got = fp ? fread(loose, 1, sizeof(loose) - 1, fp) : 0;
		if (fp) fclose(fp);
		loose[got] = '\0';
		len = MD_m3uMoved(loose, names, count, text, sizeof(text));
		if (len < 0) {
			free(moves);
			refuse("its .m3u names other files", TR_MARK("%s.m3u names other files"), base);
			return;
		}
	}
	else len = MD_m3uText(&group, (const char* const*)f->names, text, sizeof(text));
	if (len < 0) {
		free(moves);
		return;
	}

	// the saves the .m3u's name keeps, as minarch names them in the folder too
	int was_done = done(relative(m3u_path));
	if (!was_done) copySaves(m3u_path, discName(f, &group, 0), group.m3u);

	if (mkdir(folder, 0755) != 0) {
		free(moves);
		refuse("the folder couldn't be made", TR_MARK("Couldn't make %s"), base);
		return;
	}
	int moved = 0;
	for (; moved < count; moved++) {
		if (exists(moves[moved].to) || rename(moves[moved].from, moves[moved].to) != 0) break;
	}
	if (moved < count || writeNew(new_m3u, text, (size_t)len) != 0) {
		putBack(moves, moved, folder);
		free(moves);
		refuse("a file couldn't be moved", TR_MARK("Couldn't combine %s"), base);
		forgetAll();
		return;
	}
	if (had_m3u && unlink(m3u_path) != 0) sayf("couldn't remove %s after copying it into the folder", relative(m3u_path));
	sayf("combined %i discs and %i files of theirs into %s", group.count, count - group.count, relative(folder));

	// the folder's art, from the first disc's when it has none
	char art[PATH_LEN], disc_art[PATH_LEN];
	if (thumbnail(dir, base, art, sizeof(art)) && thumbnail(dir, discName(f, &group, 0), disc_art, sizeof(disc_art))
			&& copyNew(disc_art, art) > 0) sayf("copied %s's art for the folder", discName(f, &group, 0));

	// recents, favorites, tags and play time at the new paths
	char first_new[PATH_LEN];
	if (!join(first_new, sizeof(first_new), folder, discName(f, &group, 0))) first_new[0] = '\0';
	for (int d = 0; d < group.count; d++) {
		char from[PATH_LEN], to[PATH_LEN];
		if (!join(from, sizeof(from), dir, discName(f, &group, d)) || !join(to, sizeof(to), folder, discName(f, &group, d))) continue;
		// the first disc stands for the game, as at its first launch, once
		const char* target = d == 0 && !was_done ? new_m3u : to;
		moveRecent(from, target);
		moveIn("favorites.move", from, target);
		moveIn("tags.move", from, target);
		if (MOSAIC_HOST_HAS(host, play_move)) host->play_move(from, to);
	}
	moveRecent(m3u_path, new_m3u);
	moveIn("favorites.move", m3u_path, new_m3u);
	moveIn("tags.move", m3u_path, new_m3u);
	// stock tracks a game in a folder by its first disc
	if (first_new[0] && MOSAIC_HOST_HAS(host, play_move)) host->play_move(m3u_path, first_new);
	free(moves);

	forgetAll();
	char message[PATH_LEN];
	snprintf(message, sizeof(message), tr("Combined into %s"), base);
	toast(message);
	if (MOSAIC_HOST_HAS(host, index_refresh)) host->index_refresh();
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

static void combineConfirmed(void* data) {
	(void)data;
	if (combining[0]) combine(combining);
	combining[0] = '\0';
}

static void combineAsk(const char* path, void* data) {
	(void)path;
	(void)data;
	Folder* f;
	int index = selectedGame(&f);
	if (index < 0) return;
	if (!join(combining, sizeof(combining), f->path, f->groups.items[index].m3u)) return;
	char base[MD_NAME_LEN];
	snprintf(base, sizeof(base), "%.*s", (int)(strlen(f->groups.items[index].m3u) - 4), f->groups.items[index].m3u);
	char question[PATH_LEN];
	snprintf(question, sizeof(question), tr("Move the discs of %.120s into a folder of their own?"), base);
	host->confirm(question, tr("COMBINE"), combineConfirmed, NULL);
}

///////////////////////////////////////

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "Y", MOSAIC_BUTTON_Y },
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
};

static int init(const MosaicHost* h) {
	host = h;
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	snprintf(done_path, sizeof(done_path), "%s/launcher/" ID ".txt", host->userdata_path);
	memset(folders, 0, sizeof(folders));
	combining[0] = '\0';

	const char* name = host->cfg_get("button", "Y");
	int button = MOSAIC_BUTTON_Y;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (!strcmp(name, buttons[i].name)) button = buttons[i].button;
	}
	if (!MOSAIC_HOST_HAS(host, confirm)) say("this launcher has no confirm dialog; Combine discs is off");
	else if (host->register_action(ID ".combine", TR_MARK("COMBINE"), 0, canCombine, combineAsk, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, ID ".combine") != 0) {
		say("couldn't bind Combine discs");
	}
	return 0;
}

static void quit(void) {
	forgetAll();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.filter = filter,
	.entry_open = entryOpen,
	.dir_entries = dirEntries,
	.art_alias = artAlias,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
