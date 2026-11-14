// Localization: owns the strings slot, translating the launcher and other
// plugins from gettext .po language packs, read by po_model.c.
//
// The "language" setting picks a pack by its code, eg. "es" or "pt_BR"
// ("other" takes the code from the "code" setting); "off", the default,
// translates nothing. A code with a region falls back to its language's
// pack, "pt" for "pt_BR". The launcher's strings are in <code>.po in the
// "folder" setting's folder on the card (Languages by default) and in this
// plugin's res/lang/launcher/; each plugin's are in its own
// res/lang/<code>.po and in <folder>/<plugin id>/<code>.po, without a
// msgctxt for its id. A pack in the folder comes first, string by string,
// so it can change or add to the bundled one. A pack that doesn't parse is
// skipped whole, so only its own strings show in English, and the launcher
// log says which line is wrong. A plugin's packs are read the first time
// one of its strings is asked for, as plugin_info lists nothing in init.
//
// The localization.map action writes display names into each system
// folder's map.txt from names lists in <folder>/<code>/<TAG>.txt, matching
// file names loosely (names_model.c). It asks before writing anything, and
// only adds lines: a map.txt's own lines stay as they are.
//
// The fallback font, for characters the UI font has no glyphs for, is the
// "font" setting's file (relative to the card, or a full path), or with it
// empty <folder>/<code>.ttf or .otf beside the user's pack, when there's
// one. The launcher checks it's a font on the card; a theme's wins.
//
// With a language on, it checks the UI font (the theme's, or stock's) has
// every character the language's translations use, the launcher's and
// the loaded plugins' (chars_model.c), once at start and again when the
// theme changes. When it lacks any, a toast says so, once for each
// language and font until one of them changes, and a note says so on the
// Launcher pak's pages of this plugin and of the theme slot's owner.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // dladdr
#endif
#include <dirent.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "chars_model.h"
#include "names_model.h"
#include "po_model.h"

#define ID "localization"
#define MAP_ACTION ID ".map"
#define CODE_MAX 32
#define PACK_MAX (8 * 1024 * 1024)
#define NAMES_MAX (8 * 1024 * 1024)
#define MAP_MAX (4 * 1024 * 1024)
#define FOLDER_FILES_MAX 20000
#define LAUNCHER_DIR "lang/launcher" // the launcher's packs, in res/
#define PLUGIN_DIR "lang"            // a plugin's packs, in its res/

// snprintf into the array buf, 1 when it fit: a path cut short is never used
#define FITS(buf, ...) (snprintf((buf), sizeof(buf), __VA_ARGS__) < (int)sizeof(buf))

static const MosaicHost* host;
static char code[CODE_MAX]; // the language in use, or "" for off
static char folder[512];    // the user's packs, a full path
static char res_dir[600];   // this plugin's res/
static PoCatalog* user_pack;    // the launcher's strings, the user's
static PoCatalog* bundled_pack; // and the bundled ones

// A plugin's packs, read the first time its strings are asked for.
typedef struct PluginPacks {
	char id[64];
	char res[600];
	int read;
	PoCatalog* user;
	PoCatalog* bundled;
} PluginPacks;
static PluginPacks* plugin_packs;
static int plugin_count = -1; // -1 until plugin_info lists them
static int tile_on = 0;
static char font[700]; // the fallback font, or "" for none
static CmChars chars;  // the characters the translations use, once gathered
static int gathered = 0;
static int started = 0;     // the first tick checked the font
static int note_checked = 0;
static char note[256];      // the font check's note, or "" when the font has them all
static char warn_key[128];  // and what it was about

static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	host->log(ID, line);
}

// The plugin's res/ folder: lib/<arch>/plugin.so's plugin directory's.
static void findRes(void) {
	res_dir[0] = '\0';
#ifdef LOCALIZATION_TEST_RES
	snprintf(res_dir, sizeof(res_dir), "%s", LOCALIZATION_TEST_RES);
#else
	Dl_info info;
	if (!dladdr((const void*)&findRes, &info) || !info.dli_fname) return;
	char path[512];
	snprintf(path, sizeof(path), "%s", info.dli_fname);
	for (int i = 0; i < 3; i++) {
		char* slash = strrchr(path, '/');
		if (!slash) return;
		*slash = '\0';
	}
	if (!FITS(res_dir, "%s/res", path)) res_dir[0] = '\0';
#endif
}

// 1 when c is a language code a file can be named after: letters, digits,
// '_', '-' and '@', starting with a letter.
static int validCode(const char* c) {
	if (!c || !((c[0] >= 'a' && c[0] <= 'z') || (c[0] >= 'A' && c[0] <= 'Z'))) return 0;
	size_t len = strlen(c);
	if (len >= CODE_MAX) return 0;
	for (const char* p = c; *p; p++) {
		char x = *p;
		if (!((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || (x >= '0' && x <= '9') || x == '_' || x == '-' || x == '@')) return 0;
	}
	return 1;
}

// The codes a pack for code can be named, best first: "pt_BR", then "pt".
static int candidates(const char* c, char out[2][CODE_MAX]) {
	snprintf(out[0], CODE_MAX, "%s", c);
	size_t lang = strcspn(c, "_-@");
	if (!c[lang]) return 1;
	snprintf(out[1], CODE_MAX, "%.*s", (int)lang, c);
	return 2;
}

// A regular file's contents, NUL-terminated, or NULL when it's missing,
// isn't a regular file or is over limit bytes. *missing says which.
static char* readFile(const char* path, size_t limit, size_t* len, int* missing) {
	struct stat st;
	*missing = 0;
	if (stat(path, &st) != 0) {
		*missing = 1;
		return NULL;
	}
	if (!S_ISREG(st.st_mode) || st.st_size < 0 || (size_t)st.st_size > limit) return NULL;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	char* text = malloc((size_t)st.st_size + 1);
	size_t n = text ? fread(text, 1, (size_t)st.st_size, f) : 0;
	fclose(f);
	if (!text) return NULL;
	text[n] = '\0';
	if (len) *len = n;
	return text;
}

// The first pack for code in dir that reads, or NULL; what's wrong with
// one that doesn't is logged.
static PoCatalog* loadPack(const char* dir, const char* where) {
	char names[2][CODE_MAX];
	int count = candidates(code, names);
	for (int i = 0; i < count; i++) {
		char path[700];
		if (!FITS(path, "%s/%s.po", dir, names[i])) continue;
		size_t len = 0;
		int missing;
		char* text = readFile(path, PACK_MAX, &len, &missing);
		if (!text) {
			if (!missing) say("%s pack %s isn't a regular file of up to 8 MB, skipped", where, path);
			continue;
		}
		char err[240];
		PoCatalog* pack = Po_parse(text, len, err, sizeof(err));
		free(text);
		if (!pack) {
			say("%s pack %s is refused, %s", where, path, err);
			continue;
		}
		say("%s pack %s read, %i translated", where, path, pack->count);
		if (pack->note[0]) say("%s pack %s: %s", where, path, pack->note);
		return pack;
	}
	return NULL;
}

// 1 when id can name a folder: letters, digits, '.', '_' and '-', not
// starting with '.'.
static int validId(const char* id) {
	if (!id[0] || id[0] == '.') return 0;
	for (const char* p = id; *p; p++) {
		char x = *p;
		if (!((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || (x >= '0' && x <= '9') || x == '_' || x == '-' || x == '.')) return 0;
	}
	return 1;
}

static void freePlugins(void) {
	for (int i = 0; i < plugin_count; i++) {
		Po_free(plugin_packs[i].user);
		Po_free(plugin_packs[i].bundled);
	}
	free(plugin_packs);
	plugin_packs = NULL;
	plugin_count = -1;
}

// The plugins plugin_info lists, once it lists them: it lists none until
// every plugin has loaded, this one too.
static void listPlugins(void) {
	if (plugin_count >= 0 || !MOSAIC_HOST_HAS(host, plugin_info)) return;
	int count = host->plugins_count();
	if (count <= 0) return;
	plugin_packs = calloc((size_t)count, sizeof(PluginPacks));
	if (!plugin_packs) return;
	plugin_count = 0;
	for (int i = 0; i < count; i++) {
		MosaicPluginInfo info = { .size = sizeof(info) };
		if (host->plugin_info(i, &info) != 0) break;
		PluginPacks* p = &plugin_packs[plugin_count++];
		snprintf(p->id, sizeof(p->id), "%s", info.id);
		snprintf(p->res, sizeof(p->res), "%s", info.res);
	}
}

// The packs for plugin_id's strings, read on its first ask, or NULL for a
// plugin that isn't listed.
static PluginPacks* pluginPacks(const char* plugin_id) {
	listPlugins();
	for (int i = 0; i < plugin_count; i++) {
		PluginPacks* p = &plugin_packs[i];
		if (strcmp(p->id, plugin_id) != 0) continue;
		if (!p->read) {
			p->read = 1;
			char dir[700];
			if (validId(p->id) && FITS(dir, "%s/%s", folder, p->id)) p->user = loadPack(dir, "user");
			if (p->res[0] && FITS(dir, "%s/" PLUGIN_DIR, p->res)) p->bundled = loadPack(dir, "bundled");
		}
		return p;
	}
	return NULL;
}

// The launcher's strings are in its packs, in their contexts, and each
// plugin's in its own packs, in its own contexts.
static void packsFor(const char* plugin_id, PoCatalog** user, PoCatalog** bundled) {
	*user = *bundled = NULL;
	if (!code[0]) return;
	if (!plugin_id) {
		*user = user_pack;
		*bundled = bundled_pack;
		return;
	}
	PluginPacks* p = pluginPacks(plugin_id);
	if (!p) return;
	*user = p->user;
	*bundled = p->bundled;
}

static const char* translate(const char* plugin_id, const char* context, const char* text) {
	PoCatalog *user, *bundled;
	packsFor(plugin_id, &user, &bundled);
	const char* got = user ? Po_find(user, context, text) : NULL;
	if (!got && bundled) got = Po_find(bundled, context, text);
	return got;
}

static const char* translatePlural(const char* plugin_id, const char* context, const char* singular, const char* plural,
		unsigned long n) {
	(void)plural;
	PoCatalog *user, *bundled;
	packsFor(plugin_id, &user, &bundled);
	const char* got = user ? Po_findPlural(user, context, singular, n) : NULL;
	if (!got && bundled) got = Po_findPlural(bundled, context, singular, n);
	return got;
}

// Works out the fallback font from the settings and the language.
static void findFont(void) {
	font[0] = '\0';
	const char* f = host->cfg_get("font", "");
	if (f[0]) {
		if (f[0] == '/') snprintf(font, sizeof(font), "%s", f);
		else snprintf(font, sizeof(font), "%s/%s", host->sdcard_path, f);
		say("fallback font %s, from the font setting", font);
		return;
	}
	if (!code[0]) return;
	char names[2][CODE_MAX];
	int count = candidates(code, names);
	static const char* const extensions[] = { "ttf", "otf" };
	for (int i = 0; i < count; i++) {
		for (int j = 0; j < 2; j++) {
			struct stat st;
			if (FITS(font, "%s/%s.%s", folder, names[i], extensions[j]) && stat(font, &st) == 0 && S_ISREG(st.st_mode)) {
				say("fallback font %s, beside the language's pack", font);
				return;
			}
		}
	}
	font[0] = '\0';
}

static const char* fallbackFont(void) {
	return font[0] ? font : NULL;
}

///////////////////////////////////////
// map.txt

typedef struct MapWrite {
	char path[700];
	char* text;
	int existed;
	int added;
} MapWrite;

static MapWrite* writes;
static int write_count;

static void freeWrites(void) {
	for (int i = 0; i < write_count; i++) free(writes[i].text);
	free(writes);
	writes = NULL;
	write_count = 0;
}

static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
	say("toast: %s", message);
}

// The folder of names lists for the language, or 0 when there's none.
static int namesDir(char* out, size_t size) {
	char names[2][CODE_MAX];
	int count = code[0] ? candidates(code, names) : 0;
	for (int i = 0; i < count; i++) {
		struct stat st;
		snprintf(out, size, "%s/%s", folder, names[i]);
		if (stat(out, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
	}
	out[0] = '\0';
	return 0;
}

typedef struct FolderFile {
	char* name;
	int dir;
} FolderFile;

static int byName(const void* a, const void* b) {
	return strcmp(((const FolderFile*)a)->name, ((const FolderFile*)b)->name);
}

// Works out the new map.txt for one system folder, adding it to writes
// when it gains names.
static void planFolder(const char* system, const char* name, const NmNames* names) {
	FolderFile* found = NULL;
	int count = 0, cap = 0;
	DIR* d = opendir(system);
	if (!d) return;
	struct dirent* de;
	while ((de = readdir(d)) && count < FOLDER_FILES_MAX) {
		if (de->d_name[0] == '.' || !strcmp(de->d_name, "map.txt")) continue;
		char path[1024];
		struct stat st;
		if (!FITS(path, "%s/%s", system, de->d_name) || stat(path, &st) != 0) continue;
		if (count >= cap) {
			int next = cap ? cap * 2 : 64;
			FolderFile* grown = realloc(found, (size_t)next * sizeof(FolderFile));
			if (!grown) break;
			found = grown;
			cap = next;
		}
		found[count].name = strdup(de->d_name);
		if (!found[count].name) break;
		found[count].dir = S_ISDIR(st.st_mode) ? 1 : 0;
		count++;
	}
	closedir(d);
	if (count) qsort(found, (size_t)count, sizeof(FolderFile), byName);
	const char** files = malloc((size_t)(count ? count : 1) * sizeof(char*));
	int* dirs = malloc((size_t)(count ? count : 1) * sizeof(int));
	int found_count = count;
	if (!files || !dirs) count = 0;
	for (int i = 0; i < count; i++) {
		files[i] = found[i].name;
		dirs[i] = found[i].dir;
	}

	char map[700];
	if (!FITS(map, "%s/map.txt", system)) {
		for (int i = 0; i < found_count; i++) free(found[i].name);
		free(found);
		free(files);
		free(dirs);
		return;
	}
	struct stat st;
	int existed = lstat(map, &st) == 0;
	char* existing = NULL;
	int ok = 1;
	if (existed) {
		int missing;
		if (!S_ISREG(st.st_mode)) {
			say("%s/map.txt isn't a regular file, so it's left alone", name);
			ok = 0;
		}
		else if (!(existing = readFile(map, MAP_MAX, NULL, &missing))) {
			say("%s/map.txt can't be read, so it's left alone", name);
			ok = 0;
		}
	}
	int added = 0;
	char* text = ok && count ? Nm_merge(existing, files, dirs, count, names, &added) : NULL;
	free(existing);
	for (int i = 0; i < found_count; i++) free(found[i].name);
	free(found);
	free(files);
	free(dirs);
	if (!text || !added) {
		free(text);
		return;
	}
	MapWrite* w = realloc(writes, (size_t)(write_count + 1) * sizeof(MapWrite));
	if (!w) {
		free(text);
		return;
	}
	writes = w;
	MapWrite* out = &writes[write_count++];
	if (!FITS(out->path, "%s", map)) out->path[0] = '\0';
	out->text = text;
	out->existed = existed;
	out->added = added;
	say("%s: %s map.txt, %i to add", name, existed ? "its" : "a new", added);
}

// Writes a map.txt through a new <path>.tmp, renamed into place. Returns 0,
// 1 when something is already at the temporary name (a file or a link,
// left alone), or -1.
static int writeOne(const MapWrite* w) {
	char tmp[720];
	if (!w->path[0] || !FITS(tmp, "%s.tmp", w->path)) return -1;
	int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) return errno == EEXIST || errno == ELOOP ? 1 : -1;
	const char* p = w->text;
	size_t left = strlen(w->text);
	int ok = 1;
	while (ok && left) {
		ssize_t n = write(fd, p, left);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) ok = 0;
		else {
			p += n;
			left -= (size_t)n;
		}
	}
	ok = close(fd) == 0 && ok;
	if (!ok || rename(tmp, w->path) != 0) {
		int saved = errno;
		unlink(tmp);
		errno = saved;
		return -1;
	}
	return 0;
}

static void writeMaps(void* data) {
	(void)data;
	int folders = 0, added = 0;
	for (int i = 0; i < write_count; i++) {
		int wrote = writeOne(&writes[i]);
		if (wrote == 1) {
			say("%s.tmp is already there, so map.txt isn't written", writes[i].path);
			continue;
		}
		if (wrote != 0) {
			say("couldn't write %s: %s", writes[i].path, strerror(errno));
			continue;
		}
		say("wrote %s, %i added", writes[i].path, writes[i].added);
		folders++;
		added += writes[i].added;
	}
	freeWrites();
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	char message[256];
	snprintf(message, sizeof(message), trPlural("Added names to map.txt in %d folder", "Added names to map.txt in %d folders", (unsigned long)folders), folders);
	toast(message);
	say("map.txt names added: %i, in folders: %i", added, folders);
}

static int mapAvailable(const char* path, void* data) {
	(void)path;
	(void)data;
	return code[0] != '\0';
}

static void runMap(const char* path, void* data) {
	(void)path;
	(void)data;
	freeWrites();
	char dir[700];
	if (!code[0] || !namesDir(dir, sizeof(dir))) {
		say("no names lists for %s in %s", code[0] ? code : "off", folder);
		toast(tr("No names lists for this language"));
		return;
	}
	char roms[600];
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	DIR* d = opendir(roms);
	if (!d) {
		toast(tr("No names to add to map.txt"));
		return;
	}
	struct dirent* de;
	while ((de = readdir(d))) {
		char tag[64], system[1024], list[1024];
		if (de->d_name[0] == '.' || Nm_tag(de->d_name, tag, sizeof(tag)) < 0) continue;
		if (!FITS(system, "%s/%s", roms, de->d_name)) continue;
		struct stat st;
		if (stat(system, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		if (!FITS(list, "%s/%s.txt", dir, tag)) continue;
		int missing;
		char* text = readFile(list, NAMES_MAX, NULL, &missing);
		if (!text) continue;
		NmNames names = { 0 };
		int read = Nm_namesRead(&names, text);
		free(text);
		if (read > 0) planFolder(system, de->d_name, &names);
		Nm_namesFree(&names);
	}
	closedir(d);
	if (!write_count) {
		toast(tr("No names to add to map.txt"));
		return;
	}
	int replaced = 0;
	for (int i = 0; i < write_count; i++) replaced += writes[i].existed;
	char message[512];
	char first[256];
	snprintf(first, sizeof(first), trPlural("Add names to map.txt in %d folder?", "Add names to map.txt in %d folders?", (unsigned long)write_count), write_count);
	if (!replaced || !FITS(message, "%s\n%s", first, tr("Lines already in a map.txt are kept."))) snprintf(message, sizeof(message), "%s", first);
	say("asking to write %i map.txt files, %i of them already there", write_count, replaced);
	host->confirm(message, tr("WRITE"), writeMaps, NULL);
}

///////////////////////////////////////
// the font check

// Every character the language's translations use: the launcher's packs
// and each loaded plugin's, read now if they weren't yet.
static void gatherChars(void) {
	if (gathered) return;
	gathered = 1;
	int ok = Cm_addCatalog(&chars, user_pack) == 0 && Cm_addCatalog(&chars, bundled_pack) == 0;
	listPlugins();
	for (int i = 0; ok && i < plugin_count; i++) {
		PluginPacks* p = pluginPacks(plugin_packs[i].id);
		ok = p && Cm_addCatalog(&chars, p->user) == 0 && Cm_addCatalog(&chars, p->bundled) == 0;
	}
	if (!ok) say("no memory to check the font");
}

// Checks the UI font for the translations' characters, setting the note
// and its key. Returns how many it lacks, or -1 when the launcher couldn't
// say.
static int fontCheck(void) {
	note[0] = warn_key[0] = '\0';
	if (!code[0] || !MOSAIC_HOST_HAS(host, font_missing)) return 0;
	gatherChars();
	if (!chars.len) return 0;
	char missing[128]; // whole characters, the first that fit
	int count = host->font_missing(chars.text, missing, sizeof(missing));
	if (count < 0) {
		say("couldn't check the font");
		return -1;
	}
	if (count == 0) {
		say("the font has every character %s's translations use", code);
		return 0;
	}
	say("the font lacks %i characters %s's translations use: %s", count, code, missing);
	Cm_key(code, count, missing, warn_key, sizeof(warn_key));
	snprintf(note, sizeof(note), trPlural("Font lacks %d character of this language", "Font lacks %d characters of this language",
		(unsigned long)count), count);
	return count;
}

// In the launcher: checks the font, with a toast when that's new.
static void fontWarn(void) {
	int count = fontCheck();
	if (count < 0) return;
	char saved[128];
	snprintf(saved, sizeof(saved), "%s", host->cfg_get("warned", ""));
	if (!Cm_warnDue(count, warn_key, saved)) {
		if (count > 0) say("already warned about this language and font");
		else if (saved[0]) host->cfg_set("warned", "");
		return;
	}
	host->cfg_set("warned", warn_key);
	toast(note);
}

static void themeChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	fontWarn();
}

static void tick(unsigned long now) {
	(void)now;
	if (started) return;
	started = 1;
	fontWarn();
}

// The Launcher pak's pages: the note on this plugin's page and the theme
// slot's owner's, the only other plugin they load.
static int settingsNote(const char* plugin_id, char* out, size_t size) {
	if (!plugin_id || (strcmp(plugin_id, ID) != 0 && !(MOSAIC_HOST_HAS(host, plugin_loaded) && host->plugin_loaded(plugin_id)))) {
		return 0;
	}
	if (!note_checked) {
		note_checked = 1;
		fontCheck();
	}
	snprintf(out, size, "%s", note);
	return 0;
}

// With the language off, the next warning shows even for the same
// language and font as the last.
static void forgetWarning(void) {
	if (host->cfg_get("warned", "")[0]) host->cfg_set("warned", "");
}

///////////////////////////////////////
// the plugin

static int init(const MosaicHost* h) {
	host = h;
	code[0] = '\0';
	user_pack = bundled_pack = NULL;
	plugin_packs = NULL;
	plugin_count = -1;
	write_count = 0;
	writes = NULL;
	gathered = started = note_checked = 0;
	note[0] = warn_key[0] = '\0';
	findRes();

	const char* f = host->cfg_get("folder", "Languages");
	if (f[0] == '/') snprintf(folder, sizeof(folder), "%s", f);
	else snprintf(folder, sizeof(folder), "%s/%s", host->sdcard_path, f);
	size_t len = strlen(folder);
	while (len > 1 && folder[len - 1] == '/') folder[--len] = '\0';

	const char* language = host->cfg_get("language", "off");
	if (!strcmp(language, "other")) language = host->cfg_get("code", "");
	if (!language[0] || !strcmp(language, "off")) {
		say("language off, so nothing is translated");
		findFont();
		forgetWarning();
		return 0;
	}
	if (!validCode(language)) {
		say("\"%.40s\" isn't a language code, so nothing is translated", language);
		findFont();
		forgetWarning();
		return 0;
	}
	snprintf(code, sizeof(code), "%s", language);
	findFont();
	user_pack = loadPack(folder, "user");
	char launcher_dir[700];
	if (FITS(launcher_dir, "%s/" LAUNCHER_DIR, res_dir)) bundled_pack = loadPack(launcher_dir, "bundled");
	else launcher_dir[0] = '\0';
	if (!user_pack && !bundled_pack) say("no pack for %s in %s or %s, so the launcher's strings stay English", code, folder, launcher_dir);
	say("language %s", code);
	if (MOSAIC_HOST_HAS(host, subscribe) && host->subscribe("launcher.theme_changed", themeChanged, NULL) != 0) {
		say("couldn't subscribe to launcher.theme_changed");
	}

	if (host->register_action(MAP_ACTION, "Write map.txt names", 0, mapAvailable, runMap, NULL) != 0) {
		say("couldn't register " MAP_ACTION);
		return 0;
	}
	tile_on = strcmp(host->cfg_get("tile", "off"), "on") == 0;
	if (tile_on && (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(MAP_ACTION, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the map.txt tile");
	}
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* name = host->cfg_get("button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, MAP_ACTION) != 0) {
			say("couldn't bind the map.txt button");
		}
	}
	return 0;
}

static void quit(void) {
	Po_free(user_pack);
	Po_free(bundled_pack);
	user_pack = bundled_pack = NULL;
	freePlugins();
	freeWrites();
	Cm_free(&chars);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.translate = translate,
	.translate_plural = translatePlural,
	.fallback_font = fallbackFont,
	.tick = tick,
	.settings_note = settingsNote,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
