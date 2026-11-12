// Writes the gettext template of one plugin to stdout: its plugin.txt's
// name, setting labels and choice options, read by the launcher's own
// parser (Manifest_strings), and the strings xgettext found in its code
// (-c <file>), with no msgctxt, as the Launcher pak's Plugins page and
// host->tr ask for them with the plugin's id and no context. Entries are
// sorted by text (bytewise), with file-only locations, so the template
// changes only when a string does. Built and run by scripts/pot.sh.
//
//   plugin-pot plugin.txt -c ../../build/pot/code/a.pot > po/a.pot

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pluginset.h"

#define ENTRIES_MAX 4096
#define FILES_MAX 32 // locations on one of xgettext's entries

typedef struct Entry {
	char text[512];
	char plural[512]; // "" for none
	char file[512];
	int c_format;
} Entry;

static Entry entries[ENTRIES_MAX];
static int entry_count = 0;
static const char* reading; // the file whose strings are being added

static void addEntry(const char* text, const char* plural, const char* file, int c_format) {
	if (!text[0]) return;
	if (entry_count >= ENTRIES_MAX) {
		fprintf(stderr, "plugin-pot: more than %d strings\n", ENTRIES_MAX);
		exit(1);
	}
	Entry* e = &entries[entry_count++];
	snprintf(e->text, sizeof(e->text), "%s", text);
	snprintf(e->plural, sizeof(e->plural), "%s", plural ? plural : "");
	snprintf(e->file, sizeof(e->file), "%s", file);
	e->c_format = c_format;
}

static void add(const char* text, void* data) {
	(void)data;
	addEntry(text, NULL, reading, 0);
}

static int compare(const void* a, const void* b) {
	const Entry* x = a;
	const Entry* y = b;
	int c = strcmp(x->text, y->text);
	if (!c) c = strcmp(x->file, y->file);
	return c;
}

static char* readFile(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	size_t size = 0, room = 4096;
	char* text = malloc(room);
	size_t got;
	while ((got = fread(text + size, 1, room - size - 1, f)) > 0) {
		size += got;
		if (room - size < 2) text = realloc(text, room *= 2);
	}
	fclose(f);
	text[size] = '\0';
	return text;
}

// Appends the C string that starts at the quote in line to out, without
// its escapes.
static void unquote(const char* line, char* out, size_t size) {
	size_t len = strlen(out);
	const char* p = strchr(line, '"');
	if (!p) return;
	for (p++; *p && *p != '"' && len + 1 < size; p++) {
		char c = *p;
		if (c == '\\' && p[1]) {
			p++;
			c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
		}
		out[len++] = c;
	}
	out[len] = '\0';
}

// The entries of a template xgettext wrote (with --no-wrap): each one's
// locations, c-format flag, msgid and msgid_plural. Its header is left out.
static int readCode(const char* path) {
	char* text = readFile(path);
	if (!text) {
		fprintf(stderr, "plugin-pot: can't read %s\n", path);
		return -1;
	}
	char files[FILES_MAX][512];
	int file_count = 0, c_format = 0, started = 0;
	char msgid[512] = "", plural[512] = "";
	char* field = NULL; // where a continuation line goes
	size_t field_size = 0;
	char* line = text;
	while (line) {
		char* next = strchr(line, '\n');
		if (next) *next++ = '\0';
		if (!line[0]) {
			// an entry ends at a blank line
			for (int i = 0; started && i < file_count; i++) addEntry(msgid, plural[0] ? plural : NULL, files[i], c_format);
			file_count = c_format = started = 0;
			msgid[0] = plural[0] = '\0';
			field = NULL;
		}
		else if (!strncmp(line, "#:", 2)) {
			for (char* f = strtok(line + 2, " "); f && file_count < FILES_MAX; f = strtok(NULL, " ")) {
				int seen = 0;
				for (int i = 0; i < file_count; i++) seen |= !strcmp(files[i], f);
				if (!seen) snprintf(files[file_count++], sizeof(files[0]), "%s", f);
			}
		}
		else if (!strncmp(line, "#,", 2)) {
			for (char* f = strtok(line + 2, " ,"); f; f = strtok(NULL, " ,")) c_format |= !strcmp(f, "c-format");
		}
		else if (!strncmp(line, "msgid_plural ", 13)) {
			field = plural;
			field_size = sizeof(plural);
			unquote(line, field, field_size);
		}
		else if (!strncmp(line, "msgid ", 6)) {
			started = 1;
			field = msgid;
			field_size = sizeof(msgid);
			unquote(line, field, field_size);
		}
		else if (!strncmp(line, "msgstr", 6) || !strncmp(line, "msgctxt", 7)) field = NULL;
		else if (line[0] == '"' && field) unquote(line, field, field_size);
		line = next;
	}
	for (int i = 0; started && i < file_count; i++) addEntry(msgid, plural[0] ? plural : NULL, files[i], c_format);
	free(text);
	return 0;
}

// A C string as gettext writes it.
static void quoted(const char* s) {
	putchar('"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') printf("\\%c", *s);
		else if (*s == '\t') fputs("\\t", stdout);
		else if (*s == '\n') fputs("\\n", stdout);
		else putchar(*s);
	}
	putchar('"');
}

int main(int argc, char* argv[]) {
	static PluginManifest m;
	if (argc != 2 && !(argc == 4 && !strcmp(argv[2], "-c"))) {
		fprintf(stderr, "usage: plugin-pot <plugin.txt> [-c <xgettext's template of its code>]\n");
		return 1;
	}
	char* text = readFile(argv[1]);
	if (!text) {
		fprintf(stderr, "plugin-pot: can't read %s\n", argv[1]);
		return 1;
	}
	if (Manifest_parse(text, &m) != 0) {
		fprintf(stderr, "plugin-pot: %s has no id\n", argv[1]);
		return 1;
	}
	free(text);
	reading = argv[1];
	Manifest_strings(&m, add, NULL);
	if (argc == 4 && readCode(argv[3]) != 0) return 1;
	qsort(entries, entry_count, sizeof(entries[0]), compare);
	int plurals = 0;
	for (int i = 0; i < entry_count; i++) plurals |= entries[i].plural[0] != '\0';

	printf("# The %s plugin's name, setting labels and choice options, from its\n"
		"# plugin.txt, and the fixed text its code hands the launcher. Written\n"
		"# by make pot.\n", m.id);
	fputs("#, fuzzy\n"
		"msgid \"\"\n"
		"msgstr \"\"\n", stdout);
	printf("\"Project-Id-Version: %s\\n\"\n", m.id);
	fputs("\"Report-Msgid-Bugs-To: https://github.com/josegonzalez/mosaic-launcher/issues\\n\"\n"
		"\"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\\n\"\n"
		"\"Last-Translator: FULL NAME <EMAIL@ADDRESS>\\n\"\n"
		"\"Language-Team: LANGUAGE <LL@li.org>\\n\"\n"
		"\"Language: \\n\"\n"
		"\"MIME-Version: 1.0\\n\"\n"
		"\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
		"\"Content-Transfer-Encoding: 8bit\\n\"\n", stdout);
	if (plurals) fputs("\"Plural-Forms: nplurals=INTEGER; plural=EXPRESSION;\\n\"\n", stdout);
	for (int i = 0; i < entry_count; ) {
		// one entry for each text, naming every file it's in
		int j = i;
		int c_format = 0;
		const char* plural = "";
		while (j < entry_count && !strcmp(entries[j].text, entries[i].text)) {
			c_format |= entries[j].c_format;
			if (entries[j].plural[0]) plural = entries[j].plural;
			j++;
		}
		printf("\n#:");
		for (int k = i; k < j; k++) {
			if (k > i && !strcmp(entries[k].file, entries[k - 1].file)) continue;
			printf(" %s", entries[k].file);
		}
		if (c_format) printf("\n#, c-format");
		printf("\nmsgid ");
		quoted(entries[i].text);
		if (plural[0]) {
			printf("\nmsgid_plural ");
			quoted(plural);
			printf("\nmsgstr[0] \"\"\nmsgstr[1] \"\"\n");
		}
		else printf("\nmsgstr \"\"\n");
		i = j;
	}
	return 0;
}
