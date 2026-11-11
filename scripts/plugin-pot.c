// Writes the gettext template of the plugin.txt files named on the command
// line to stdout: each plugin's name, setting labels and choice options,
// read by the launcher's own parser (Manifest_strings) and under msgctxt
// set to the plugin's id, as the Launcher pak's Plugins page translates
// them. Entries are sorted by context, then text (bytewise), with
// file-only locations, so the template changes only when a string does.
// Built and run by scripts/pot.sh.
//
//   plugin-pot plugins/*/plugin.txt > po/plugins.pot

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pluginset.h"

#define ENTRIES_MAX 4096

typedef struct Entry {
	char context[64];
	char text[256];
	char file[512];
} Entry;

static Entry entries[ENTRIES_MAX];
static int entry_count = 0;
static const char* reading; // the file whose strings are being added
static const PluginManifest* manifest;

static void add(const char* text, void* data) {
	(void)data;
	if (!text[0]) return;
	if (entry_count >= ENTRIES_MAX) {
		fprintf(stderr, "plugin-pot: more than %d strings\n", ENTRIES_MAX);
		exit(1);
	}
	Entry* e = &entries[entry_count++];
	snprintf(e->context, sizeof(e->context), "%s", manifest->id);
	snprintf(e->text, sizeof(e->text), "%s", text);
	snprintf(e->file, sizeof(e->file), "%s", reading);
}

static int compare(const void* a, const void* b) {
	const Entry* x = a;
	const Entry* y = b;
	int c = strcmp(x->context, y->context);
	if (!c) c = strcmp(x->text, y->text);
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

// A C string as gettext writes it.
static void quoted(const char* s) {
	putchar('"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') printf("\\%c", *s);
		else if (*s == '\t') fputs("\\t", stdout);
		else putchar(*s);
	}
	putchar('"');
}

int main(int argc, char* argv[]) {
	static PluginManifest m;
	for (int i = 1; i < argc; i++) {
		char* text = readFile(argv[i]);
		if (!text) {
			fprintf(stderr, "plugin-pot: can't read %s\n", argv[i]);
			return 1;
		}
		memset(&m, 0, sizeof(m));
		if (Manifest_parse(text, &m) != 0) {
			fprintf(stderr, "plugin-pot: %s has no id\n", argv[i]);
			return 1;
		}
		free(text);
		reading = argv[i];
		manifest = &m;
		Manifest_strings(&m, add, NULL);
	}
	qsort(entries, entry_count, sizeof(entries[0]), compare);

	fputs("# The bundled plugins' names, setting labels and choice options, from\n"
		"# their plugin.txt files, under msgctxt set to the plugin's id. Written\n"
		"# by make pot.\n"
		"#, fuzzy\n"
		"msgid \"\"\n"
		"msgstr \"\"\n"
		"\"Project-Id-Version: mosaic-launcher\\n\"\n"
		"\"Report-Msgid-Bugs-To: https://github.com/josegonzalez/mosaic-launcher/issues\\n\"\n"
		"\"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\\n\"\n"
		"\"Last-Translator: FULL NAME <EMAIL@ADDRESS>\\n\"\n"
		"\"Language-Team: LANGUAGE <LL@li.org>\\n\"\n"
		"\"Language: \\n\"\n"
		"\"MIME-Version: 1.0\\n\"\n"
		"\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
		"\"Content-Transfer-Encoding: 8bit\\n\"\n", stdout);
	for (int i = 0; i < entry_count; ) {
		// one entry for each context and text, naming every file it's in
		int j = i;
		while (j < entry_count && !strcmp(entries[j].context, entries[i].context) && !strcmp(entries[j].text, entries[i].text)) j++;
		printf("\n#:");
		for (int k = i; k < j; k++) {
			if (k > i && !strcmp(entries[k].file, entries[k - 1].file)) continue;
			printf(" %s", entries[k].file);
		}
		printf("\nmsgctxt ");
		quoted(entries[i].context);
		printf("\nmsgid ");
		quoted(entries[i].text);
		printf("\nmsgstr \"\"\n");
		i = j;
	}
	return 0;
}
