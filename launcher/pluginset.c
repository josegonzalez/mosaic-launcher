#include <ctype.h>
#include <stdarg.h>
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

// 1 when src was cut to fit.
static int copyField(char* dst, size_t size, const char* src) {
	return (size_t)snprintf(dst, size, "%s", src) >= size;
}

// items, of count items of size bytes, with room for one more: itself, or
// a bigger copy. NULL when out of memory, leaving items as they were.
static void* grow(void* items, int* room, int count, size_t size) {
	if (count < *room) return items;
	int more = *room ? *room * 2 : 16;
	void* grown = realloc(items, (size_t)more * size);
	if (grown) *room = more;
	return grown;
}

static void (*parse_log)(const char* message) = NULL;

void Manifest_setLog(void (*log)(const char* message)) {
	parse_log = log;
}

__attribute__((format(printf, 1, 2)))
static void warn(const char* format, ...) {
	if (!parse_log) return;
	char message[640];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	parse_log(message);
}

// What a parse warns about: whose file it is, or NULL to say nothing.
typedef struct Parse {
	const char* who; // eg. "favorites's plugin.txt"
	void* data;
} Parse;

// Copies a field, warning when it's cut.
static void keepField(const Parse* parse, char* dst, size_t size, const char* src, const char* what) {
	if (copyField(dst, size, src) && parse->who) warn("%s: %s is over %zu bytes, cut to fit", parse->who, what, size - 1);
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

// Calls fn for each non-empty, trimmed key=value line. A line longer than
// MANIFEST_LINE_MAX is cut to fit, with a warning.
#define MANIFEST_LINE_MAX 511
static void eachLine(const char* text, void (*fn)(const char* key, const char* value, const Parse* parse), const Parse* parse) {
	if (!text) return;
	const char* p = text;
	while (*p) {
		const char* end = strchr(p, '\n');
		if (!end) end = p + strlen(p);
		char line[MANIFEST_LINE_MAX + 1];
		size_t len = (size_t)(end - p);
		if (len >= sizeof(line)) {
			len = sizeof(line) - 1;
			if (parse->who) warn("%s: a line is over %d bytes, cut to fit: %.40s...", parse->who, MANIFEST_LINE_MAX, p);
		}
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
		fn(key, value, parse);
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

// The ids after the flags field's button: markers, comma-separated. Ones
// that don't fit are left out, and counted in the return.
static int settingButtons(const char* field, char* out, size_t size) {
	out[0] = '\0';
	size_t used = 0;
	int dropped = 0;
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
			else dropped++;
		}
		p = end;
	}
	return dropped;
}

// setting.<key>=<type>|<label>|<default>[|<options>[|<flags>]]
static void manifestSetting(PluginManifest* m, const char* key, const char* value, const Parse* parse) {
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
	char what[96];
	snprintf(what, sizeof(what), "setting.%.40s's key", key);
	keepField(parse, s->key, sizeof(s->key), key, what);
	snprintf(what, sizeof(what), "setting.%.40s's label", key);
	keepField(parse, s->label, sizeof(s->label), parts[1], what);
	snprintf(what, sizeof(what), "setting.%.40s's default", key);
	keepField(parse, s->fallback, sizeof(s->fallback), parts[2], what);
	snprintf(what, sizeof(what), "setting.%.40s's options", key);
	keepField(parse, s->options, sizeof(s->options), parts[3] ? parts[3] : "", what);
	s->flags = parts[4] ? settingFlags(parts[4], s->type) : 0;
	int dropped = settingButtons(parts[4] ? parts[4] : "", s->buttons, sizeof(s->buttons));
	if (dropped && parse->who) {
		warn("%s: setting.%.40s's button: markers are over %zu bytes, leaving out %d", parse->who, key, sizeof(s->buttons) - 1,
			dropped);
	}
	if (s->type == SETTING_CHOICE && !s->options[0]) return; // nothing to choose from
	PluginSetting* grown = realloc(m->settings, (m->setting_count + 1) * sizeof(*grown));
	if (!grown) {
		if (parse->who) warn("%s: out of memory, leaving out setting.%.40s", parse->who, key);
		return;
	}
	m->settings = grown;
	m->settings[m->setting_count++] = setting;
}

static void manifestField(const char* key, const char* value, const Parse* parse) {
	PluginManifest* m = parse->data;
	if (!strncmp(key, "setting.", 8)) {
		manifestSetting(m, key + 8, value, parse);
		return;
	}
	char* field = NULL;
	size_t size = 0;
	if (!strcmp(key, "api")) m->api = atoi(value);
#define FIELD(name) else if (!strcmp(key, #name)) { field = m->name; size = sizeof(m->name); }
	FIELD(id)
	FIELD(name)
	FIELD(slots)
	FIELD(requires)
	FIELD(conflicts)
	FIELD(optional)
	FIELD(provides)
	FIELD(ambient)
	FIELD(description)
#undef FIELD
	if (field) keepField(parse, field, size, value, key);
}

// Only the id, to name the plugin in what the parse warns about.
static void idField(const char* key, const char* value, const Parse* parse) {
	if (!strcmp(key, "id")) copyField(parse->data, sizeof(((PluginManifest*)0)->id), value);
}

int Manifest_parse(const char* text, PluginManifest* out) {
	char dir[sizeof(out->dir)];
	int bundled = out->bundled;
	copyField(dir, sizeof(dir), out->dir);
	Manifest_free(out);
	memset(out, 0, sizeof(*out));
	copyField(out->dir, sizeof(out->dir), dir);
	out->bundled = bundled;

	char id[sizeof(out->id)] = "";
	Parse quiet = { NULL, id };
	eachLine(text, idField, &quiet);
	char who[sizeof(dir) + 32];
	if (id[0]) snprintf(who, sizeof(who), "%s's plugin.txt", id);
	else if (dir[0]) snprintf(who, sizeof(who), "%s/plugin.txt", dir);
	else snprintf(who, sizeof(who), "a plugin.txt");
	Parse parse = { who, out };
	eachLine(text, manifestField, &parse);
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

int ManifestList_add(ManifestList* list, const char* text, const char* dir, int bundled) {
	PluginManifest* items = grow(list->items, &list->room, list->count, sizeof(PluginManifest));
	if (!items) {
		warn("out of memory, so %s isn't loaded", dir);
		return -2;
	}
	list->items = items;
	PluginManifest* m = &list->items[list->count];
	memset(m, 0, sizeof(*m));
	if (copyField(m->dir, sizeof(m->dir), dir)) {
		warn("%.80s...: its path is over %zu bytes, so it isn't loaded", dir, sizeof(m->dir) - 1);
		return -3;
	}
	m->bundled = bundled;
	if (Manifest_parse(text, m) != 0) return -1;
	list->count++;
	return 0;
}

void ManifestList_free(ManifestList* list) {
	for (int i = 0; i < list->count; i++) Manifest_free(&list->items[i]);
	free(list->items);
	memset(list, 0, sizeof(*list));
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
	if (m->description[0]) fn(m->description, data);
	for (int i = 0; i < m->setting_count; i++) {
		const PluginSetting* s = &m->settings[i];
		if (s->label[0]) fn(s->label, data);
		if (s->type != SETTING_CHOICE) continue;
		char option[sizeof(s->options)];
		for (int j = 0; Manifest_option(s->options, j, option, sizeof(option)); j++) fn(option, data);
	}
}

///////////////////////////////////////

// One <id>=on|off line, and one slot.<name>=<id> line.
typedef struct OnChoice {
	char id[64];
	int on; // 1 on, 0 off
} OnChoice;
typedef struct SlotChoice {
	char name[64];
	char owner[64];
} SlotChoice;

// plugins.txt, with as many lines as it has.
typedef struct Choices {
	OnChoice* order; // in the order plugins.txt lists them
	int count;
	int room;
	SlotChoice* slots;
	int slot_count;
	int slot_room;
} Choices;

static void choicesFree(Choices* c) {
	free(c->order);
	free(c->slots);
	memset(c, 0, sizeof(*c));
}

static void choiceLine(const char* key, const char* value, const Parse* parse) {
	Choices* c = parse->data;
	char what[96];
	if (!strncmp(key, "slot.", 5)) {
		SlotChoice* slots = grow(c->slots, &c->slot_room, c->slot_count, sizeof(SlotChoice));
		if (!slots) {
			if (parse->who) warn("%s: out of memory, leaving out %.40s", parse->who, key);
			return;
		}
		c->slots = slots;
		SlotChoice* slot = &c->slots[c->slot_count++];
		snprintf(what, sizeof(what), "%.40s's slot name", key);
		keepField(parse, slot->name, sizeof(slot->name), key + 5, what);
		snprintf(what, sizeof(what), "%.40s's plugin id", key);
		keepField(parse, slot->owner, sizeof(slot->owner), value, what);
		return;
	}
	int on;
	if (!strcmp(value, "on")) on = 1;
	else if (!strcmp(value, "off")) on = 0;
	else return;
	for (int i = 0; i < c->count; i++) {
		if (!strcmp(c->order[i].id, key)) {
			c->order[i].on = on; // a later line for the same id wins
			return;
		}
	}
	OnChoice* order = grow(c->order, &c->room, c->count, sizeof(OnChoice));
	if (!order) {
		if (parse->who) warn("%s: out of memory, leaving out %.40s", parse->who, key);
		return;
	}
	c->order = order;
	snprintf(what, sizeof(what), "the plugin id %.40s", key);
	keepField(parse, c->order[c->count].id, sizeof(c->order[0].id), key, what);
	c->order[c->count].on = on;
	c->count++;
}

static int choiceFor(const Choices* c, const char* id) {
	for (int i = 0; i < c->count; i++) {
		if (!strcmp(c->order[i].id, id)) return c->order[i].on;
	}
	return -1;
}

static const char* slotOwner(const Choices* c, const char* slot) {
	for (int i = c->slot_count - 1; i >= 0; i--) {
		if (!strcmp(c->slots[i].name, slot)) return c->slots[i].owner;
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
	// quiet, as the Plugins page asks for each plugin it draws
	Choices c = { 0 };
	Parse parse = { NULL, &c };
	eachLine(choices, choiceLine, &parse);
	int choice = choiceFor(&c, m->id);
	choicesFree(&c);
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
		char line[MANIFEST_LINE_MAX + 1];
		size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
		if (copy < len) warn("plugins.txt: a line is over %d bytes, cut to fit: %.40s...", MANIFEST_LINE_MAX, p);
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

static int orderOptional(PluginDecision* out, int n);

int PluginSet_resolve(const PluginManifest* manifests, int count, const char* choices, PluginDecision* out) {
	if (count <= 0) return 0;
	Choices c = { 0 };
	Parse parse = { "plugins.txt", &c };
	eachLine(choices, choiceLine, &parse);
	char* placed = calloc((size_t)count, 1);
	if (!placed) {
		warn("out of memory deciding which plugins load, so none do");
		choicesFree(&c);
		return 0;
	}

	// Order: ids plugins.txt lists, in its order, then the rest as found.
	int n = 0;
	for (int i = 0; i < c.count; i++) {
		for (int j = 0; j < count; j++) {
			if (!placed[j] && !strcmp(manifests[j].id, c.order[i].id)) {
				placed[j] = 1;
				out[n++].manifest = &manifests[j];
				break; // only the first copy of a duplicate id goes here
			}
		}
	}
	for (int j = 0; j < count; j++) {
		if (!placed[j]) out[n++].manifest = &manifests[j];
	}
	free(placed);

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

	choicesFree(&c);
	if (orderOptional(out, n) != 0) {
		warn("out of memory putting plugins after the plugins their optional= names, so they load in their usual order");
	}
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
// 0, or -1 when out of memory, leaving the order as it was.
static int orderOptional(PluginDecision* out, int n) {
	if (n <= 0) return 0;
	PluginDecision* rest = malloc(sizeof(PluginDecision) * (size_t)n);
	if (!rest) return -1;
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
		for (int i = pick; i + 1 < rest_count; i++) rest[i] = rest[i + 1];
		rest_count--;
	}
	free(rest);
	return 0;
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
