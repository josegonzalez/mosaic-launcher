#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pluginset.h"

static void trim(char* s) {
	char* start = s;
	while (*start && isspace((unsigned char)*start)) start++;
	if (start != s) memmove(s, start, strlen(start) + 1);
	size_t len = strlen(s);
	while (len && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static void copyField(char* dst, size_t size, const char* src) {
	snprintf(dst, size, "%s", src);
}

int csvHas(const char* csv, const char* id) {
	if (!csv || !id || !id[0]) return 0;
	size_t id_len = strlen(id);
	const char* p = csv;
	while (*p) {
		while (*p == ',' || isspace((unsigned char)*p)) p++;
		const char* end = p;
		while (*end && *end != ',') end++;
		const char* item_end = end;
		while (item_end > p && isspace((unsigned char)item_end[-1])) item_end--;
		if ((size_t)(item_end - p) == id_len && strncmp(p, id, id_len) == 0) return 1;
		p = end;
	}
	return 0;
}

// Calls fn for each non-empty, trimmed key=value line.
static void eachLine(const char* text, void (*fn)(const char* key, const char* value, void* data), void* data) {
	if (!text) return;
	const char* p = text;
	while (*p) {
		const char* end = strchr(p, '\n');
		if (!end) end = p + strlen(p);
		char line[512];
		size_t len = (size_t)(end - p);
		if (len >= sizeof(line)) len = sizeof(line) - 1;
		memcpy(line, p, len);
		line[len] = '\0';
		p = *end ? end + 1 : end;

		trim(line);
		if (!line[0] || line[0] == '#') continue;
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char* key = line;
		char* value = eq + 1;
		trim(key);
		trim(value);
		fn(key, value, data);
	}
}

static void manifestField(const char* key, const char* value, void* data) {
	PluginManifest* m = data;
	if (!strcmp(key, "id")) copyField(m->id, sizeof(m->id), value);
	else if (!strcmp(key, "name")) copyField(m->name, sizeof(m->name), value);
	else if (!strcmp(key, "api")) m->api = atoi(value);
	else if (!strcmp(key, "slots")) copyField(m->slots, sizeof(m->slots), value);
	else if (!strcmp(key, "requires")) copyField(m->requires, sizeof(m->requires), value);
	else if (!strcmp(key, "conflicts")) copyField(m->conflicts, sizeof(m->conflicts), value);
}

int Manifest_parse(const char* text, PluginManifest* out) {
	char dir[sizeof(out->dir)];
	int bundled = out->bundled;
	copyField(dir, sizeof(dir), out->dir);
	memset(out, 0, sizeof(*out));
	copyField(out->dir, sizeof(out->dir), dir);
	out->bundled = bundled;

	eachLine(text, manifestField, out);
	if (!out->id[0]) return -1;
	if (!out->name[0]) copyField(out->name, sizeof(out->name), out->id);
	return 0;
}

///////////////////////////////////////

typedef struct Choices {
	char order[PLUGIN_MAX][64]; // ids in the order plugins.txt lists them
	int on[PLUGIN_MAX];         // 1 on, 0 off, per order entry
	int count;
	char slot_names[PLUGIN_MAX][64];
	char slot_owners[PLUGIN_MAX][64];
	int slot_count;
} Choices;

static void choiceLine(const char* key, const char* value, void* data) {
	Choices* c = data;
	if (!strncmp(key, "slot.", 5)) {
		if (c->slot_count >= PLUGIN_MAX) return;
		copyField(c->slot_names[c->slot_count], sizeof(c->slot_names[0]), key + 5);
		copyField(c->slot_owners[c->slot_count], sizeof(c->slot_owners[0]), value);
		c->slot_count++;
		return;
	}
	int on;
	if (!strcmp(value, "on")) on = 1;
	else if (!strcmp(value, "off")) on = 0;
	else return;
	for (int i = 0; i < c->count; i++) {
		if (!strcmp(c->order[i], key)) {
			c->on[i] = on; // a later line for the same id wins
			return;
		}
	}
	if (c->count >= PLUGIN_MAX) return;
	copyField(c->order[c->count], sizeof(c->order[0]), key);
	c->on[c->count] = on;
	c->count++;
}

static int choiceFor(const Choices* c, const char* id) {
	for (int i = 0; i < c->count; i++) {
		if (!strcmp(c->order[i], id)) return c->on[i];
	}
	return -1;
}

static const char* slotOwner(const Choices* c, const char* slot) {
	for (int i = c->slot_count - 1; i >= 0; i--) {
		if (!strcmp(c->slot_names[i], slot)) return c->slot_owners[i];
	}
	return NULL;
}

static void refuse(PluginDecision* d, const char* why) {
	d->load = 0;
	copyField(d->reason, sizeof(d->reason), why);
}

static PluginDecision* findLoading(PluginDecision* out, int count, const char* id) {
	for (int i = 0; i < count; i++) {
		if (out[i].load && !strcmp(out[i].manifest->id, id)) return &out[i];
	}
	return NULL;
}

int PluginSet_resolve(const PluginManifest* manifests, int count, const char* choices, PluginDecision* out) {
	static Choices c;
	memset(&c, 0, sizeof(c));
	eachLine(choices, choiceLine, &c);
	if (count > PLUGIN_MAX) count = PLUGIN_MAX;

	// Order: ids plugins.txt lists, in its order, then the rest as found.
	int n = 0;
	int placed[PLUGIN_MAX] = {0};
	for (int i = 0; i < c.count; i++) {
		for (int j = 0; j < count; j++) {
			if (!placed[j] && !strcmp(manifests[j].id, c.order[i])) {
				placed[j] = 1;
				out[n++].manifest = &manifests[j];
				break; // only the first copy of a duplicate id goes here
			}
		}
	}
	for (int j = 0; j < count; j++) {
		if (!placed[j]) out[n++].manifest = &manifests[j];
	}

	// On or off, and duplicates.
	for (int i = 0; i < n; i++) {
		PluginDecision* d = &out[i];
		const PluginManifest* m = d->manifest;
		d->reason[0] = '\0';
		int choice = choiceFor(&c, m->id);
		d->load = choice == -1 ? !m->bundled : choice;
		if (!d->load) {
			refuse(d, "turned off");
			continue;
		}
		for (int j = 0; j < i; j++) {
			if (!strcmp(out[j].manifest->id, m->id)) {
				char why[160];
				snprintf(why, sizeof(why), "another plugin with id %s was found first in %s", m->id, out[j].manifest->dir);
				refuse(d, why);
				break;
			}
		}
	}

	// Exclusive slots: the user's pick if it's on, otherwise the first.
	for (int i = 0; i < n; i++) {
		PluginDecision* d = &out[i];
		if (!d->load || !d->manifest->slots[0]) continue;
		char slots[256];
		copyField(slots, sizeof(slots), d->manifest->slots);
		for (char* slot = strtok(slots, ","); slot && d->load; slot = strtok(NULL, ",")) {
			trim(slot);
			if (!slot[0]) continue;
			const char* chosen = slotOwner(&c, slot);
			int chosen_loads = chosen && findLoading(out, n, chosen) && csvHas(findLoading(out, n, chosen)->manifest->slots, slot);
			char why[160];
			if (chosen_loads && strcmp(chosen, d->manifest->id) != 0) {
				snprintf(why, sizeof(why), "the %s slot belongs to %s", slot, chosen);
				refuse(d, why);
				break;
			}
			if (chosen_loads) continue;
			for (int j = 0; j < i; j++) {
				if (out[j].load && csvHas(out[j].manifest->slots, slot)) {
					snprintf(why, sizeof(why), "the %s slot belongs to %s", slot, out[j].manifest->id);
					refuse(d, why);
					break;
				}
			}
		}
	}

	// Conflicts, either direction: the later one gives way.
	for (int i = 0; i < n; i++) {
		PluginDecision* d = &out[i];
		if (!d->load) continue;
		for (int j = 0; j < i; j++) {
			if (!out[j].load) continue;
			const PluginManifest* a = out[j].manifest;
			const PluginManifest* b = d->manifest;
			if (csvHas(a->conflicts, b->id) || csvHas(b->conflicts, a->id)) {
				char why[160];
				snprintf(why, sizeof(why), "conflicts with %s", a->id);
				refuse(d, why);
				break;
			}
		}
	}

	// Requirements, until nothing changes: refusing one can strand another.
	int changed = 1;
	while (changed) {
		changed = 0;
		for (int i = 0; i < n; i++) {
			PluginDecision* d = &out[i];
			if (!d->load || !d->manifest->requires[0]) continue;
			char reqs[256];
			copyField(reqs, sizeof(reqs), d->manifest->requires);
			for (char* req = strtok(reqs, ","); req; req = strtok(NULL, ",")) {
				trim(req);
				if (!req[0] || findLoading(out, n, req)) continue;
				char why[160];
				snprintf(why, sizeof(why), "requires %s, which isn't loading", req);
				refuse(d, why);
				changed = 1;
				break;
			}
		}
	}

	return n;
}
