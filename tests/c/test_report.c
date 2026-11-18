// Bug report bundles written to a card in a temporary folder: the zip's
// files, what's taken out, the logs cut to fit, and the newest reports
// kept.

#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "report.h"
#include "reportmodel.h"

static char card[256], userdata[300], state[320], logged[8192];

static void reportLog(const char* message) {
	size_t used = strlen(logged);
	snprintf(logged + used, sizeof(logged) - used, "%s\n", message);
}

static void mkdirs(const char* path) {
	char p[1024];
	snprintf(p, sizeof(p), "%s", path);
	for (char* s = p + 1; *s; s++) {
		if (*s != '/') continue;
		*s = '\0';
		mkdir(p, 0755);
		*s = '/';
	}
	mkdir(p, 0755);
}

static void put(const char* dir, const char* name, const char* text) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE* f = fopen(path, "wb");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

// The whole zip at path, to free.
static unsigned char* slurp(const char* path, size_t* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char* data = malloc((size_t)n + 1);
	*len = fread(data, 1, (size_t)n, f);
	fclose(f);
	data[*len] = '\0';
	return data;
}

static unsigned get16(const unsigned char* p) {
	return p[0] | (unsigned)p[1] << 8;
}

static uint32_t get32(const unsigned char* p) {
	return get16(p) | (uint32_t)get16(p + 2) << 16;
}

// The zip's file names, without its folder, one a line, and its files'
// text one after the other, after checking each file's CRC.
static int unzip(const char* path, char* names, size_t names_size, char** text) {
	size_t len;
	unsigned char* z = slurp(path, &len);
	if (!z || len < 22) return -1;
	const unsigned char* end = z + len - 22;
	if (get32(end) != 0x06054b50u) return -1;
	int count = (int)get16(end + 8);
	uint32_t cd = get32(end + 16);
	names[0] = '\0';
	ReportBuf all = { 0 };
	for (int i = 0; i < count; i++) {
		const unsigned char* h = z + cd;
		if (get32(h) != 0x02014b50u) return -1;
		unsigned name_len = get16(h + 28);
		uint32_t size = get32(h + 24), crc = get32(h + 16), local = get32(h + 42);
		char name[300];
		snprintf(name, sizeof(name), "%.*s", (int)name_len, (const char*)h + 46);
		const char* slash = strchr(name, '/');
		size_t used = strlen(names);
		snprintf(names + used, names_size - used, "%s\n", slash ? slash + 1 : name);
		const unsigned char* data = z + local + 30 + get16(z + local + 26);
		if (Zip_crc32(0, data, size) != crc) return -1;
		ReportBuf_add(&all, data, size);
		cd += 46 + name_len;
	}
	*text = all.data ? all.data : strdup("");
	free(z);
	return count;
}

static int reports(const char* dir) {
	DIR* dh = opendir(dir);
	int n = 0;
	struct dirent* dp;
	while (dh && (dp = readdir(dh)) != NULL) n += Report_isReport(dp->d_name);
	if (dh) closedir(dh);
	return n;
}

static PluginManifest manifests[2];
static const char* states[2] = { "on, loaded", "off: plugins.txt turns it off" };

static ReportSource source(int keep_network, time_t now) {
	ReportSource src = {
		.sdcard = card,
		.userdata = userdata,
		.state = state,
		.platform = "desktop",
		.model = "Desktop",
		.os_version = "1.2.3",
		.pak = NULL,
		.profile = "",
		.theme = "stock",
		.made_by = "the test",
		.manifests = manifests,
		.states = states,
		.manifest_count = 2,
		.index = NULL,
		.wifi_name = "Home Net",
		.keep_network = keep_network,
		.now = now,
		.log = reportLog,
	};
	return src;
}

int main(void) {
	snprintf(card, sizeof(card), "%s/report-card", TEST_DIR);
	char cmd[1200];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", card);
	CHECK(system(cmd) == 0);
	snprintf(userdata, sizeof(userdata), "%s/.userdata/desktop", card);
	snprintf(state, sizeof(state), "%s/launcher", userdata);
	char logs[800], tasks[900], system_dir[800], reports_dir[800];
	snprintf(logs, sizeof(logs), "%s/logs", userdata);
	snprintf(tasks, sizeof(tasks), "%s/tasks", logs);
	snprintf(system_dir, sizeof(system_dir), "%s/.system", card);
	snprintf(reports_dir, sizeof(reports_dir), "%s/Reports", card);
	mkdirs(state);
	mkdirs(tasks);
	mkdirs(system_dir);

	CHECK(Manifest_parse("id=romm\nname=RomM\napi=1\nsetting.url=text|Server|||protected\n"
		"setting.password=text|Password|||protected,secret\nsetting.button=choice|Button|none|none,Y\n", &manifests[0]) == 0);
	snprintf(manifests[0].dir, sizeof(manifests[0].dir), "%s/romm.plugin", card);
	manifests[0].bundled = 1;
	CHECK(Manifest_parse("id=tasks\nname=Tasks\napi=1\n", &manifests[1]) == 0);
	snprintf(manifests[1].dir, sizeof(manifests[1].dir), "%s/tasks.plugin", card);
	mkdirs(manifests[1].dir);
	put(manifests[1].dir, "plugin.txt", "id=tasks\nname=Tasks\napi=1\nversion=2.1\n");

	put(system_dir, "version.txt", "NextUI 6.7.0\nabc1234\n");
	put(state, "plugins.txt", "romm=on\ntasks=off\n");
	put(state, "settings.txt", "lock=on\n");
	put(state, "romm.cfg", "url=http://192.168.1.5:8080\npassword=hunter2hunter2\nbutton=Y\ntoken=zzz\n");
	put(state, "crashes", "1\n");
	put(logs, "nextui.txt", "[INFO] romm: hunter2hunter2 at http://192.168.1.5:8080 on Home Net 10.1.1.1\n");
	put(tasks, "Backup.txt", "copied to 172.16.0.9\n");
	put(tasks, ".hidden.txt", "skipped\n");
	// a log past the cap: only its end goes in
	char big_path[1000];
	snprintf(big_path, sizeof(big_path), "%s/romm.txt", logs);
	FILE* f = fopen(big_path, "wb");
	for (int i = 0; f && i < REPORT_LOG_MAX / 16 + 100; i++) fprintf(f, "line %09d ok\n", i);
	if (f) fclose(f);

	// ten older reports and a file that isn't one
	mkdirs(reports_dir);
	for (int i = 0; i < REPORT_KEEP; i++) {
		char name[64];
		snprintf(name, sizeof(name), "mosaic-2000-01-01-0000%02d.zip", i);
		put(reports_dir, name, "");
	}
	put(reports_dir, "notes.txt", "mine");

	struct tm when = { 0 };
	when.tm_year = 2026 - 1900;
	when.tm_mon = 10;
	when.tm_mday = 17;
	when.tm_hour = 15;
	when.tm_min = 30;
	when.tm_sec = 12;
	when.tm_isdst = -1;
	time_t now = mktime(&when);
	ReportSource src = source(0, now);
	char out[256];
	CHECK(Report_make(&src, out, sizeof(out)) == 0);
	CHECK_STR(out, "Reports/mosaic-2026-11-17-153012.zip");
	CHECK(strstr(logged, "saved Reports/mosaic-2026-11-17-153012.zip") != NULL);
	CHECK(strstr(logged, "warning: removed the oldest report, Reports/mosaic-2000-01-01-000000.zip, keeping the newest 10") != NULL);
	CHECK(reports(reports_dir) == REPORT_KEEP);
	char path[1000];
	snprintf(path, sizeof(path), "%s/notes.txt", reports_dir);
	CHECK(access(path, F_OK) == 0);

	snprintf(path, sizeof(path), "%s/%s", card, out);
	char names[2048], *text = NULL;
	CHECK(unzip(path, names, sizeof(names), &text) == 9);
	CHECK_STR(names, "README.txt\ninfo.txt\ngames.txt\nsettings/plugins.txt\nsettings/launcher.txt\nsettings/romm.cfg\n"
		"logs/nextui.txt\nlogs/romm.txt\nlogs/tasks/Backup.txt\n");
	CHECK(text != NULL);
	if (text) {
		CHECK(strstr(text, "hunter2hunter2") == NULL);
		CHECK(strstr(text, "192.168.1.5") == NULL);
		CHECK(strstr(text, "10.1.1.1") == NULL);
		CHECK(strstr(text, "172.16.0.9") == NULL);
		CHECK(strstr(text, "Home Net") == NULL);
		CHECK(strstr(text, "zzz") == NULL);
		CHECK(strstr(text, "[INFO] romm: <redacted> at <redacted> on <wifi> <ip>\n") != NULL);
		CHECK(strstr(text, "copied to <ip>\n") != NULL);
		CHECK(strstr(text, "# romm\nbutton=Y\n# left out, protected or secret in plugin.txt: url, password\n"
			"# left out: 1 key plugin.txt doesn't list\n") != NULL);
		CHECK(strstr(text, "made: 2026-11-17 15:30:12, by the test\n") != NULL);
		CHECK(strstr(text, "device: Desktop (desktop)\nos: 1.2.3\nnextui: NextUI 6.7.0 (abc1234)\nlauncher: unknown\n") != NULL);
		CHECK(strstr(text, "crash guard: crashes=1\n") != NULL);
		CHECK(strstr(text, "network details: taken out\n") != NULL);
		CHECK(strstr(text, "romm, bundled, version unknown, api 1, RomM: on, loaded\n") != NULL);
		CHECK(strstr(text, "tasks, own pak, version 2.1, api 1, Tasks: off: plugins.txt turns it off\n") != NULL);
		CHECK(strstr(text, "romm.txt, 1.1 MB, only its end\n") != NULL);
		CHECK(strstr(text, "tasks/Backup.txt, 21 bytes\n") != NULL);
		CHECK(strstr(text, "The card index hasn't been read yet") != NULL);
		CHECK(strstr(text, "[bug report: only the last 1024 KB of ") != NULL);
		CHECK(strstr(text, "line 000000000 ok") == NULL);
		char last[64];
		snprintf(last, sizeof(last), "line %09d ok\n", REPORT_LOG_MAX / 16 + 99);
		CHECK(strstr(text, last) != NULL);
		CHECK(strstr(text, "skipped") == NULL);
		free(text);
	}

	// again in the same second, keeping network details
	logged[0] = '\0';
	src = source(1, now);
	CHECK(Report_make(&src, out, sizeof(out)) == 0);
	CHECK_STR(out, "Reports/mosaic-2026-11-17-153012-2.zip");
	CHECK(reports(reports_dir) == REPORT_KEEP);
	snprintf(path, sizeof(path), "%s/%s", card, out);
	CHECK(unzip(path, names, sizeof(names), &text) == 9);
	if (text) {
		CHECK(strstr(text, "on Home Net 10.1.1.1\n") != NULL);
		CHECK(strstr(text, "copied to 172.16.0.9\n") != NULL);
		CHECK(strstr(text, "hunter2hunter2") == NULL);
		CHECK(strstr(text, "network details: kept\n") != NULL);
		free(text);
	}
	// the two just made are the newest, so they stay
	snprintf(path, sizeof(path), "%s/Reports/mosaic-2026-11-17-153012.zip", card);
	CHECK(access(path, F_OK) == 0);

	// a Reports folder that can't be written: nothing is
	logged[0] = '\0';
	snprintf(path, sizeof(path), "%s/Reports", card);
	snprintf(cmd, sizeof(cmd), "chmod 555 '%s'", path);
	CHECK(system(cmd) == 0);
	src = source(0, now + 60);
	if (geteuid() != 0) {
		CHECK(Report_make(&src, out, sizeof(out)) == -1);
		CHECK_STR(out, "");
		CHECK(strstr(logged, "couldn't write") != NULL);
	}
	snprintf(cmd, sizeof(cmd), "chmod 755 '%s'", path);
	CHECK(system(cmd) == 0);

	Manifest_free(&manifests[0]);
	Manifest_free(&manifests[1]);
	CHECK_DONE();
}
