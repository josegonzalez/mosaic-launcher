#include "report.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "plugincfg.h"
#include "reportmodel.h"

#define REPORTS_DIR "Reports"
#define SETTINGS_MAX (64 * 1024) // a settings file, or version.txt, read whole up to this
#define MARGIN (256 * 1024)      // free space kept beyond a report's size

typedef struct LogFile {
	char path[1000];
	char name[256]; // in the zip, under logs/
	size_t size;
} LogFile;

typedef struct Job {
	const ReportSource* src;
	Redactor redactor;
	ZipWriter zip;
	FILE* file;
	char folder[96]; // mosaic-<date>, the zip's one folder
	PluginCfg* cfgs; // each manifest's
	LogFile* logs;
	int log_count, log_room, logs_left_out;
	int replaced;
} Job;

static void say(const ReportSource* src, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(const ReportSource* src, const char* fmt, ...) {
	if (!src->log) return;
	char message[768];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	src->log(message);
}

///////////////////////////////////////
// reading

// A regular file's size, or -1.
static long long fileSize(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
	return (long long)st.st_size;
}

// Up to max bytes from the end of a regular file, NUL-terminated, to free,
// or NULL. cut is set when the start was left out.
static char* readTail(const char* path, size_t max, size_t* len, int* cut) {
	*len = 0;
	*cut = 0;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	struct stat st;
	if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
		fclose(f);
		return NULL;
	}
	size_t size = (size_t)st.st_size;
	if (size > max) {
		*cut = 1;
		if (fseek(f, (long)(size - max), SEEK_SET) != 0) {
			fclose(f);
			return NULL;
		}
		size = max;
	}
	char* data = malloc(size + 1);
	if (!data) {
		fclose(f);
		return NULL;
	}
	*len = fread(data, 1, size, f);
	data[*len] = '\0';
	fclose(f);
	return data;
}

// The first line of a file, trimmed, or fallback.
static void firstLine(const char* path, int line, const char* fallback, char* out, size_t size) {
	size_t len;
	int cut;
	char* text = readTail(path, SETTINGS_MAX, &len, &cut);
	snprintf(out, size, "%s", fallback);
	if (!text) return;
	char* p = text;
	for (int i = 0; i < line && p; i++) {
		p = strchr(p, '\n');
		if (p) p++;
	}
	if (p) {
		p[strcspn(p, "\r\n")] = '\0';
		if (p[0]) snprintf(out, size, "%s", p);
	}
	free(text);
}

// A plugin.txt's version= line, which only reports read.
static void manifestVersion(const PluginManifest* m, char* out, size_t size) {
	char path[640];
	snprintf(path, sizeof(path), "%s/plugin.txt", m->dir);
	size_t len;
	int cut;
	char* text = readTail(path, SETTINGS_MAX, &len, &cut);
	out[0] = '\0';
	for (char* line = text; line && *line;) {
		char* next = strchr(line, '\n');
		if (next) *next++ = '\0';
		line[strcspn(line, "\r")] = '\0';
		if (!strncmp(line, "version=", 8)) snprintf(out, size, "%s", line + 8);
		line = next;
	}
	free(text);
}

///////////////////////////////////////
// writing

static int fileWrite(void* ctx, const void* data, size_t len) {
	return fwrite(data, 1, len, ctx) == len ? 0 : -1;
}

// Adds text to the zip as name in its folder, with private details taken out.
static void addText(Job* job, const char* name, const char* text, size_t len) {
	ReportBuf out = { 0 };
	ReportBuf_add(&out, "", 0);
	job->replaced += Redact(&job->redactor, text, len, &out);
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", job->folder, name);
	if (out.failed) job->zip.failed = 1;
	else Zip_add(&job->zip, path, out.data, out.len, job->src->now);
	ReportBuf_free(&out);
}

static void addBuf(Job* job, const char* name, ReportBuf* text) {
	if (text->failed) job->zip.failed = 1;
	addText(job, name, text->data ? text->data : "", text->len);
}

///////////////////////////////////////
// what goes in

static void addLog(Job* job, const char* path, const char* name) {
	for (int i = 0; i < job->log_count; i++) {
		if (!strcmp(job->logs[i].path, path)) return;
	}
	long long size = fileSize(path);
	if (size < 0) return;
	if (job->log_count >= REPORT_LOGS_MAX) {
		job->logs_left_out++;
		return;
	}
	if (job->log_count == job->log_room) {
		int room = job->log_room ? job->log_room * 2 : 16;
		LogFile* grown = realloc(job->logs, (size_t)room * sizeof(LogFile));
		if (!grown) {
			job->logs_left_out++;
			return;
		}
		job->logs = grown;
		job->log_room = room;
	}
	LogFile* l = &job->logs[job->log_count++];
	snprintf(l->path, sizeof(l->path), "%s", path);
	snprintf(l->name, sizeof(l->name), "%s", name);
	l->size = (size_t)size;
}

static int byName(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

// Every .txt in a plugin's own log folder, such as tasks', by name.
static void addLogDir(Job* job, const char* logs, const char* id) {
	char dir[600];
	snprintf(dir, sizeof(dir), "%s/%s", logs, id);
	DIR* dh = opendir(dir);
	if (!dh) return;
	char** names = NULL;
	int count = 0, room = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		size_t n = strlen(dp->d_name);
		if (dp->d_name[0] == '.' || n < 5 || strcmp(dp->d_name + n - 4, ".txt") != 0) continue;
		if (count == room) {
			room = room ? room * 2 : 16;
			char** grown = realloc(names, (size_t)room * sizeof(char*));
			if (!grown) break;
			names = grown;
		}
		names[count] = strdup(dp->d_name);
		if (names[count]) count++;
	}
	closedir(dh);
	if (count) qsort(names, (size_t)count, sizeof(char*), byName);
	for (int i = 0; i < count; i++) {
		char path[1000], name[256];
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		snprintf(name, sizeof(name), "%s/%s", id, names[i]);
		addLog(job, path, name);
		free(names[i]);
	}
	free(names);
}

// The launcher's log, the one wrapper.sh kept from the last run that
// crashed, each plugin's own (logs/<id>.txt and logs/<id>/*.txt) and
// sync's Syncthing log.
static void findLogs(Job* job) {
	const ReportSource* src = job->src;
	char logs[512], path[640];
	snprintf(logs, sizeof(logs), "%s/logs", src->userdata);
	static const char* const named[] = { "nextui.txt", "nextui-crash.txt" };
	for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", logs, named[i]);
		addLog(job, path, named[i]);
	}
	for (int i = 0; i < src->manifest_count; i++) {
		const char* id = src->manifests[i].id;
		char name[128];
		snprintf(name, sizeof(name), "%s.txt", id);
		snprintf(path, sizeof(path), "%s/%s", logs, name);
		addLog(job, path, name);
		addLogDir(job, logs, id);
	}
	snprintf(path, sizeof(path), "%s/syncthing.txt", logs);
	addLog(job, path, "syncthing.txt");
}

static void sizeText(unsigned long long bytes, char* out, size_t size) {
	if (bytes >= 1024ULL * 1024 * 1024) snprintf(out, size, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
	else if (bytes >= 1024ULL * 1024) snprintf(out, size, "%.1f MB", bytes / (1024.0 * 1024));
	else if (bytes >= 1024) snprintf(out, size, "%llu KB", bytes / 1024);
	else snprintf(out, size, "%llu bytes", bytes);
}

static void addReadme(Job* job) {
	const ReportSource* src = job->src;
	ReportBuf b = { 0 };
	ReportBuf_printf(&b,
		"A bug report from mosaic-launcher, made by %s.\n\n"
		"info.txt     the device, NextUI's and the launcher's versions, storage and every plugin\n"
		"games.txt    each system folder with how many games it has, without file names\n"
		"settings/    plugins.txt, the Launcher page's settings and each plugin's settings\n"
		"logs/        the launcher's log and plugins' own logs\n\n"
		"Taken out: settings plugin.txt marks protected or secret (and their values wherever they show),\n"
		"keys a plugin's cfg has that its plugin.txt doesn't list, user names and passwords in URLs,\n"
		"tokens, API keys, passwords and cookies%s.\n",
		src->made_by,
		src->keep_network ? ""
			: ", IP and MAC addresses, Wi-Fi network names and Syncthing device IDs");
	if (src->keep_network) {
		ReportBuf_printf(&b, "Kept, as the keep network details setting is on: IP and MAC addresses, Wi-Fi network names\n"
			"and Syncthing device IDs.\n");
	}
	ReportBuf_printf(&b, "Logs over %i KB have only their last %i KB.\n", REPORT_LOG_MAX / 1024, REPORT_LOG_MAX / 1024);
	addBuf(job, "README.txt", &b);
	ReportBuf_free(&b);
}

static void addInfo(Job* job) {
	const ReportSource* src = job->src;
	ReportBuf b = { 0 };
	struct tm tm;
	localtime_r(&src->now, &tm);
	char when[64];
	strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
	char path[640], release[128], hash[128], launcher[128];
	snprintf(path, sizeof(path), "%s/.system/version.txt", src->sdcard);
	firstLine(path, 0, "unknown", release, sizeof(release));
	firstLine(path, 1, "unknown", hash, sizeof(hash));
	if (src->pak) {
		snprintf(path, sizeof(path), "%s/VERSION", src->pak);
		firstLine(path, 0, "unknown", launcher, sizeof(launcher));
	} else {
		snprintf(launcher, sizeof(launcher), "unknown");
	}
	ReportBuf_printf(&b, "made: %s, by %s\n", when, src->made_by);
	ReportBuf_printf(&b, "device: %s (%s)\n", src->model, src->platform);
	if (src->os_version && src->os_version[0]) ReportBuf_printf(&b, "os: %s\n", src->os_version);
	ReportBuf_printf(&b, "nextui: %s (%s)\n", release, hash);
	ReportBuf_printf(&b, "launcher: %s\n", launcher);
	ReportBuf_printf(&b, "plugin api: %i\n", MOSAIC_API_VERSION);
	ReportBuf_printf(&b, "profile: %s\n", src->profile && src->profile[0] ? src->profile : "the owner's");
	ReportBuf_printf(&b, "theme: %s\n", src->theme);
	struct statvfs st;
	if (statvfs(src->sdcard, &st) == 0) {
		unsigned long long unit = st.f_frsize ? st.f_frsize : st.f_bsize;
		char free_text[32], total_text[32];
		sizeText((unsigned long long)st.f_bavail * unit, free_text, sizeof(free_text));
		sizeText((unsigned long long)st.f_blocks * unit, total_text, sizeof(total_text));
		ReportBuf_printf(&b, "storage: %s free of %s\n", free_text, total_text);
	} else {
		ReportBuf_printf(&b, "storage: unavailable\n");
	}
	static const char* const markers[] = { "plugins_paused", "loading", "crashes", "fallback" };
	ReportBuf_printf(&b, "crash guard:");
	int any = 0;
	for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", src->state, markers[i]);
		if (fileSize(path) < 0) continue;
		char value[32];
		firstLine(path, 0, "", value, sizeof(value));
		ReportBuf_printf(&b, " %s%s%s", markers[i], value[0] ? "=" : "", value);
		any = 1;
	}
	ReportBuf_printf(&b, "%s\n", any ? "" : " clear");
	ReportBuf_printf(&b, "network details: %s\n", src->keep_network ? "kept" : "taken out");

	ReportBuf_printf(&b, "\nplugins (%i):\n", src->manifest_count);
	for (int i = 0; i < src->manifest_count; i++) {
		const PluginManifest* m = &src->manifests[i];
		char version[128];
		manifestVersion(m, version, sizeof(version));
		if (!version[0]) snprintf(version, sizeof(version), "%s", m->bundled ? launcher : "not given");
		ReportBuf_printf(&b, "%s, %s, version %s, api %i, %s: %s\n", m->id, m->bundled ? "bundled" : "own pak", version,
			m->api, m->name, src->states[i]);
	}

	ReportBuf_printf(&b, "\nlogs (%i):\n", job->log_count);
	for (int i = 0; i < job->log_count; i++) {
		const LogFile* l = &job->logs[i];
		char size[32];
		sizeText(l->size, size, sizeof(size));
		ReportBuf_printf(&b, "%s, %s%s\n", l->name, size, l->size > REPORT_LOG_MAX ? ", only its end" : "");
	}
	if (job->logs_left_out) {
		ReportBuf_printf(&b, "left out: %i more %s past the %i a report holds\n", job->logs_left_out,
			job->logs_left_out == 1 ? "log" : "logs", REPORT_LOGS_MAX);
	}
	if (job->redactor.dropped) {
		ReportBuf_printf(&b, "warning: %i protected or secret %s past the %i looked for weren't searched for in the logs\n",
			job->redactor.dropped, job->redactor.dropped == 1 ? "value" : "values", REPORT_LITERALS_MAX);
	}
	addBuf(job, "info.txt", &b);
	ReportBuf_free(&b);
}

static void addSettings(Job* job) {
	const ReportSource* src = job->src;
	static const struct {
		const char* file;
		const char* name;
	} files[] = { { "plugins.txt", "settings/plugins.txt" }, { "settings.txt", "settings/launcher.txt" } };
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		char path[640];
		snprintf(path, sizeof(path), "%s/%s", src->state, files[i].file);
		size_t len;
		int cut;
		char* text = readTail(path, SETTINGS_MAX, &len, &cut);
		if (!text) continue;
		size_t start = cut ? Report_lineStart(text, len) : 0;
		addText(job, files[i].name, text + start, len - start);
		free(text);
	}
	for (int i = 0; i < src->manifest_count; i++) {
		PluginCfg* cfg = &job->cfgs[i];
		if (!cfg->count) continue;
		ReportBuf b = { 0 };
		Report_settings(&src->manifests[i], cfg->keys, cfg->values, cfg->count, &b);
		char name[128];
		snprintf(name, sizeof(name), "settings/%s.cfg", src->manifests[i].id);
		addBuf(job, name, &b);
		ReportBuf_free(&b);
	}
}

static void addLogs(Job* job) {
	for (int i = 0; i < job->log_count && !job->zip.failed; i++) {
		const LogFile* l = &job->logs[i];
		size_t len;
		int cut;
		char* text = readTail(l->path, REPORT_LOG_MAX, &len, &cut);
		if (!text) continue;
		size_t start = cut ? Report_lineStart(text, len) : 0;
		ReportBuf b = { 0 };
		if (cut) {
			ReportBuf_printf(&b, "[bug report: only the last %i KB of %zu KB]\n", REPORT_LOG_MAX / 1024, l->size / 1024);
		}
		ReportBuf_add(&b, text + start, len - start);
		free(text);
		char name[300];
		snprintf(name, sizeof(name), "logs/%s", l->name);
		addBuf(job, name, &b);
		ReportBuf_free(&b);
	}
}

///////////////////////////////////////
// the file

static int uniqueName(const ReportSource* src, const char* dir, char* name, size_t size) {
	struct tm tm;
	localtime_r(&src->now, &tm);
	char base[64];
	Report_name(&tm, base, sizeof(base));
	for (int i = 1; i < 100; i++) {
		if (i == 1) snprintf(name, size, "%s", base);
		else snprintf(name, size, "%s-%i", base, i);
		char path[700];
		snprintf(path, sizeof(path), "%s/%s.zip", dir, name);
		if (access(path, F_OK) != 0) return 0;
	}
	return -1;
}

// Keeps the newest REPORT_KEEP reports, saying which it removed.
static void prune(const ReportSource* src, const char* dir) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	char** names = NULL;
	int count = 0, room = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (!Report_isReport(dp->d_name)) continue;
		if (count == room) {
			room = room ? room * 2 : 16;
			char** grown = realloc(names, (size_t)room * sizeof(char*));
			if (!grown) break;
			names = grown;
		}
		names[count] = strdup(dp->d_name);
		if (names[count]) count++;
	}
	closedir(dh);
	int* drop = count ? calloc((size_t)count, sizeof(int)) : NULL;
	if (drop && Report_prune(names, count, REPORT_KEEP, drop)) {
		for (int i = 0; i < count; i++) {
			if (!drop[i]) continue;
			char path[700];
			snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
			if (unlink(path) == 0) {
				say(src, "warning: removed the oldest report, " REPORTS_DIR "/%s, keeping the newest %i", names[i], REPORT_KEEP);
			}
			else say(src, "warning: couldn't remove the old report " REPORTS_DIR "/%s", names[i]);
		}
	}
	free(drop);
	for (int i = 0; i < count; i++) free(names[i]);
	free(names);
}

static void jobFree(Job* job) {
	if (job->cfgs) {
		for (int i = 0; i < job->src->manifest_count; i++) PluginCfg_free(&job->cfgs[i]);
	}
	free(job->cfgs);
	free(job->logs);
	Zip_free(&job->zip);
	Redactor_free(&job->redactor);
}

int Report_make(const ReportSource* src, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	Job job;
	memset(&job, 0, sizeof(job));
	job.src = src;
	Redactor_init(&job.redactor, src->keep_network);

	// what to take out of everything, before anything's written
	job.cfgs = src->manifest_count ? calloc((size_t)src->manifest_count, sizeof(PluginCfg)) : NULL;
	if (src->manifest_count && !job.cfgs) {
		say(src, "out of memory");
		jobFree(&job);
		return -1;
	}
	for (int i = 0; i < src->manifest_count; i++) {
		char path[640];
		snprintf(path, sizeof(path), "%s/%s.cfg", src->state, src->manifests[i].id);
		PluginCfg_init(&job.cfgs[i], path);
		PluginCfg_load(&job.cfgs[i]);
		Report_secrets(&src->manifests[i], job.cfgs[i].keys, job.cfgs[i].values, job.cfgs[i].count, &job.redactor);
	}
	if (src->wifi_name && src->wifi_name[0]) Redactor_add(&job.redactor, src->wifi_name, 1);
	findLogs(&job);
	if (job.logs_left_out) {
		say(src, "warning: %i logs left out, past the %i a report holds", job.logs_left_out, REPORT_LOGS_MAX);
	}
	if (job.redactor.dropped) {
		say(src, "warning: %i protected or secret values past the %i looked for aren't searched for in the logs",
			job.redactor.dropped, REPORT_LITERALS_MAX);
	}

	// room for it
	char dir[600];
	snprintf(dir, sizeof(dir), "%s/" REPORTS_DIR, src->sdcard);
	unsigned long long need = MARGIN;
	for (int i = 0; i < job.log_count; i++) {
		need += job.logs[i].size > REPORT_LOG_MAX ? REPORT_LOG_MAX : job.logs[i].size;
	}
	struct statvfs st;
	if (statvfs(src->sdcard, &st) == 0) {
		unsigned long long unit = st.f_frsize ? st.f_frsize : st.f_bsize;
		if ((unsigned long long)st.f_bavail * unit < need) {
			say(src, "not enough room on the card: a report needs about %llu KB", need / 1024);
			jobFree(&job);
			return -1;
		}
	}
	mkdir(dir, 0755);
	char name[96];
	if (uniqueName(src, dir, name, sizeof(name)) != 0) {
		say(src, "couldn't find a free name in %s", dir);
		jobFree(&job);
		return -1;
	}
	snprintf(job.folder, sizeof(job.folder), "%s", name);
	char tmp[720], final[720];
	snprintf(tmp, sizeof(tmp), "%s/.%s.zip.tmp", dir, name);
	snprintf(final, sizeof(final), "%s/%s.zip", dir, name);
	job.file = fopen(tmp, "wb");
	if (!job.file) {
		say(src, "couldn't write %s", tmp);
		jobFree(&job);
		return -1;
	}
	Zip_init(&job.zip, fileWrite, job.file);

	addReadme(&job);
	addInfo(&job);
	ReportBuf games = { 0 };
	Report_games(src->index, &games);
	addBuf(&job, "games.txt", &games);
	ReportBuf_free(&games);
	addSettings(&job);
	addLogs(&job);

	int rc = Zip_finish(&job.zip);
	if (fflush(job.file) != 0 || fsync(fileno(job.file)) != 0) rc = -1;
	if (fclose(job.file) != 0) rc = -1;
	if (rc == 0 && rename(tmp, final) != 0) rc = -1;
	if (rc != 0) {
		unlink(tmp);
		say(src, "couldn't write %s", final);
		jobFree(&job);
		return -1;
	}
	say(src, "saved " REPORTS_DIR "/%s.zip: %i files, %i logs, %i private details taken out", name, job.zip.count,
		job.log_count, job.replaced);
	if (out) snprintf(out, size, REPORTS_DIR "/%s.zip", name);
	jobFree(&job);
	prune(src, dir);
	return 0;
}
