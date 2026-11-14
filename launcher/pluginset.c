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

// The comma-separated flags field: protected, secret and public. Unknown
// words are left for newer launchers, secret only means something on text,
// and a protected or secret setting is never public.
static int settingFlags(const char* field, int type) {
	int flags = 0;
	if (csvHas(field, "protected")) flags |= SETTING_PROTECTED;
	if (type == SETTING_TEXT && csvHas(field, "secret")) flags |= SETTING_SECRET;
	if (!flags && csvHas(field, "public")) flags |= SETTING_PUBLIC;
	return flags;
}

// The ids after the flags field's button: markers, comma-separated, cut
// to fit.
static void settingButtons(const char* field, char* out, size_t size) {
	out[0] = '\0';
	size_t used = 0;
	const char* p = field;
	while (*p) {
		while (*p == ',' || isspace((unsigned char)*p)) p++;
		const char* end = p;
		while (*end && *end != ',') end++;
		const char* item_end = end;
		while (item_end > p && isspace((unsigned char)item_end[-1])) item_end--;
		if (item_end - p > 7 && !strncmp(p, "button:", 7)) {
			size_t len = (size_t)(item_end - p - 7);
			if (used + len + (used ? 1 : 0) < size) {
				if (used) out[used++] = ',';
				memcpy(out + used, p + 7, len);
				used += len;
				out[used] = '\0';
			}
		}
		p = end;
	}
}

// setting.<key>=<type>|<label>|<default>[|<options>[|<flags>]]
static void manifestSetting(PluginManifest* m, const char* key, const char* value) {
	if (!key[0]) return;
	char fields[512];
	copyField(fields, sizeof(fields), value);
	char* parts[5] = { NULL, NULL, NULL, NULL, NULL };
	int n = 0;
	char* p = fields;
	while (n < 5) {
		parts[n++] = p;
		char* bar = strchr(p, '|');
		if (!bar) break;
		*bar = '\0';
		p = bar + 1;
	}
	if (n < 3) return; // type, label and default are required

	PluginSetting setting;
	PluginSetting* s = &setting;
	memset(s, 0, sizeof(*s));
	if (!strcmp(parts[0], "toggle")) s->type = SETTING_TOGGLE;
	else if (!strcmp(parts[0], "choice")) s->type = SETTING_CHOICE;
	else if (!strcmp(parts[0], "text")) s->type = SETTING_TEXT;
	else return;
	copyField(s->key, sizeof(s->key), key);
	copyField(s->label, sizeof(s->label), parts[1]);
	copyField(s->fallback, sizeof(s->fallback), parts[2]);
	copyField(s->options, sizeof(s->options), parts[3] ? parts[3] : "");
	s->flags = parts[4] ? settingFlags(parts[4], s->type) : 0;
	settingButtons(parts[4] ? parts[4] : "", s->buttons, sizeof(s->buttons));
	if (s->type == SETTING_CHOICE && !s->options[0]) return; // nothing to choose from
	PluginSetting* grown = realloc(m->settings, (m->setting_count + 1) * sizeof(*grown));
	if (!grown) return;
	m->settings = grown;
	m->settings[m->setting_count++] = setting;
}

static void manifestField(const char* key, const char* value, void* data) {
	PluginManifest* m = data;
	if (!strncmp(key, "setting.", 8)) {
		manifestSetting(m, key + 8, value);
		return;
	}
	if (!strcmp(key, "id")) copyField(m->id, sizeof(m->id), value);
	else if (!strcmp(key, "name")) copyField(m->name, sizeof(m->name), value);
	else if (!strcmp(key, "api")) m->api = atoi(value);
	else if (!strcmp(key, "slots")) copyField(m->slots, sizeof(m->slots), value);
	else if (!strcmp(key, "requires")) copyField(m->requires, sizeof(m->requires), value);
	else if (!strcmp(key, "conflicts")) copyField(m->conflicts, sizeof(m->conflicts), value);
	else if (!strcmp(key, "optional")) copyField(m->optional, sizeof(m->optional), value);
	else if (!strcmp(key, "provides")) copyField(m->provides, sizeof(m->provides), value);
	else if (!strcmp(key, "description")) copyField(m->description, sizeof(m->description), value);
}

int Manifest_parse(const char* text, PluginManifest* out) {
	char dir[sizeof(out->dir)];
	int bundled = out->bundled;
	copyField(dir, sizeof(dir), out->dir);
	Manifest_free(out);
	memset(out, 0, sizeof(*out));
	copyField(out->dir, sizeof(out->dir), dir);
	out->bundled = bundled;

	eachLine(text, manifestField, out);
	if (!out->id[0]) {
		Manifest_free(out);
		return -1;
	}
	if (!out->name[0]) copyField(out->name, sizeof(out->name), out->id);
	return 0;
}

void Manifest_free(PluginManifest* m) {
	free(m->settings);
	m->settings = NULL;
	m->setting_count = 0;
}

int Manifest_res(const PluginManifest* m, char* out, size_t size) {
	if (!out || size == 0) return -1;
	int len = snprintf(out, size, "%s/res", m->dir);
	if (len < 0 || (size_t)len >= size) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}

int Manifest_isProtected(const PluginManifest* m) {
	for (int i = 0; i < m->setting_count; i++) {
		if (m->settings[i].flags & SETTING_PROTECTED) return 1;
	}
	return 0;
}

int Manifest_isPublic(const PluginManifest* m, const char* key) {
	if (!m || !key) return 0;
	for (int i = 0; i < m->setting_count; i++) {
		if (!strcmp(m->settings[i].key, key)) return (m->settings[i].flags & SETTING_PUBLIC) != 0;
	}
	return 0;
}

const char* Manifest_buttonSetting(const PluginManifest* m, const char* action_id) {
	if (!m || !action_id || !action_id[0]) return NULL;
	for (int i = 0; i < m->setting_count; i++) {
		if (csvHas(m->settings[i].buttons, action_id)) return m->settings[i].key;
	}
	return NULL;
}

int Manifest_option(const char* options, int index, char* out, size_t size) {
	if (index < 0 || !options) return 0;
	const char* p = options;
	int at = 0;
	while (*p) {
		while (*p == ' ') p++;
		const char* end = strchr(p, ',');
		if (!end) end = p + strlen(p);
		if (end > p) {
			if (at++ == index) {
				snprintf(out, size, "%.*s", (int)(end - p), p);
				return 1;
			}
		}
		p = *end ? end + 1 : end;
	}
	return 0;
}

void Manifest_strings(const PluginManifest* m, void (*fn)(const char* text, void* data), void* data) {
	fn(m->name, data);
	for (int i = 0; i < m->setting_count; i++) {
		const PluginSetting* s = &m->settings[i];
		if (s->label[0]) fn(s->label, data);
		if (s->type != SETTING_CHOICE) continue;
		char option[sizeof(s->options)];
		for (int j = 0; Manifest_option(s->options, j, option, sizeof(option)); j++) fn(option, data);
	}
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

int PluginSet_isOn(const char* choices, const PluginManifest* m) {
	static Choices c;
	memset(&c, 0, sizeof(c));
	eachLine(choices, choiceLine, &c);
	int choice = choiceFor(&c, m->id);
	return choice == -1 ? !m->bundled : choice;
}

char* PluginSet_setChoice(const char* choices, const char* id, int on) {
	size_t room = (choices ? strlen(choices) : 0) + strlen(id) + 8;
	char* out = malloc(room);
	out[0] = '\0';
	int replaced = 0;

	const char* p = choices ? choices : "";
	while (*p) {
		const char* end = strchr(p, '\n');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		char line[512];
		size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
		memcpy(line, p, copy);
		line[copy] = '\0';
		p += len + (end ? 1 : 0);

		char key[sizeof(line)];
		copyField(key, sizeof(key), line);
		char* eq = strchr(key, '=');
		if (eq) *eq = '\0';
		trim(key);
		if (eq && !strcmp(key, id)) {
			if (replaced) continue; // drop later duplicates; the first keeps its place
			snprintf(line, sizeof(line), "%s=%s", id, on ? "on" : "off");
			replaced = 1;
		}
		strcat(out, line);
		strcat(out, "\n");
	}
	if (!replaced) {
		strcat(out, id);
		strcat(out, on ? "=on\n" : "=off\n");
	}
	return out;
}

static void orderOptional(PluginDecision* out, int n);

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
				snprintf(why, sizeof(why), "another plugin with id %.40s was found first in %.60s", m->id, out[j].manifest->dir);
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
				snprintf(why, sizeof(why), "the %.40s slot belongs to %.40s", slot, chosen);
				refuse(d, why);
				break;
			}
			if (chosen_loads) continue;
			for (int j = 0; j < i; j++) {
				if (out[j].load && csvHas(out[j].manifest->slots, slot)) {
					snprintf(why, sizeof(why), "the %.40s slot belongs to %.40s", slot, out[j].manifest->id);
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
				snprintf(why, sizeof(why), "conflicts with %.60s", a->id);
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
				snprintf(why, sizeof(why), "requires %.60s, which isn't loading", req);
				refuse(d, why);
				changed = 1;
				break;
			}
		}
	}

	orderOptional(out, n);
	return n;
}

// Whether d waits for a loading plugin that isn't placed yet: one its
// optional= names, other than itself.
static int waitsFor(const PluginDecision* d, const PluginDecision* rest, int rest_count) {
	if (!d->load || !d->manifest->optional[0]) return 0;
	for (int i = 0; i < rest_count; i++) {
		const PluginDecision* other = &rest[i];
		if (other == d || !other->load || !strcmp(other->manifest->id, d->manifest->id)) continue;
		if (csvHas(d->manifest->optional, other->manifest->id)) return 1;
	}
	return 0;
}

// Moves each loading plugin after the loading plugins its optional= names:
// each place goes to the first plugin, in the order so far, that waits for
// none of those left, or, when every one left waits (they name each other),
// to the first of them.
static void orderOptional(PluginDecision* out, int n) {
	static PluginDecision rest[PLUGIN_MAX];
	int rest_count = n;
	memcpy(rest, out, sizeof(PluginDecision) * (size_t)n);
	for (int placed = 0; placed < n; placed++) {
		int pick = 0;
		for (int i = 0; i < rest_count; i++) {
			if (!waitsFor(&rest[i], rest, rest_count)) {
				pick = i;
				break;
			}
		}
		out[placed] = rest[pick];
		memmove(&rest[pick], &rest[pick + 1], sizeof(PluginDecision) * (size_t)(rest_count - pick - 1));
		rest_count--;
	}
}

const char* PluginSet_orderBroken(const PluginDecision* out, int count, int index) {
	if (index < 0 || index >= count) return NULL;
	const PluginDecision* d = &out[index];
	if (!d->load || !d->manifest->optional[0]) return NULL;
	for (int i = index + 1; i < count; i++) {
		if (!out[i].load || !strcmp(out[i].manifest->id, d->manifest->id)) continue;
		if (csvHas(d->manifest->optional, out[i].manifest->id)) return out[i].manifest->id;
	}
	return NULL;
}

int PluginSet_onlySlots(PluginDecision* out, int count, const char* slots, const char* why) {
	int kept = 0;
	for (int i = 0; i < count; i++) {
		PluginDecision* d = &out[i];
		if (!d->load) continue;
		int claims = 0;
		char mine[256];
		copyField(mine, sizeof(mine), d->manifest->slots);
		for (char* slot = strtok(mine, ","); slot && !claims; slot = strtok(NULL, ",")) {
			trim(slot);
			claims = csvHas(slots, slot);
		}
		if (claims) kept++;
		else refuse(d, why);
	}
	return kept;
}
