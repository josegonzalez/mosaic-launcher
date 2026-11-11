#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "rootnames.h"

void RootName_split(const char* name, size_t* start, size_t* len) {
	// stock's sorting prefix, as trimSortingMeta skips it: digits, a
	// closing parenthesis, then blanks
	size_t at = 0;
	while (isdigit((unsigned char)name[at])) at++;
	if (name[at] == ')') {
		at++;
		while (name[at] == ' ' || name[at] == '\t') at++;
	}
	else at = 0;

	size_t end = strlen(name);
	for (size_t i = at + 1; i < end; i++) {
		if (name[i] == '(' || name[i] == '[') {
			end = i;
			break;
		}
	}
	while (end > at && isspace((unsigned char)name[end - 1])) end--;
	// a name that's all tag keeps it, as getDisplayName does
	if (end == at) end = strlen(name);
	*start = at;
	*len = end - at;
}

int RootName_translate(int kind, const char* name, RootNameTr tr, char* out, size_t size) {
	if (!name || !tr || !size) return 0;
	if (kind == ROOTNAME_ROW) {
		const char* shown = tr(NULL, name);
		if (!shown || !strcmp(shown, name)) return 0;
		if ((size_t)snprintf(NULL, 0, "%s", shown) >= size) return 0;
		snprintf(out, size, "%s", shown);
		return 1;
	}
	if (kind != ROOTNAME_SYSTEM) return 0;

	size_t start, len;
	RootName_split(name, &start, &len);
	char base[256];
	if (len >= sizeof(base)) return 0;
	memcpy(base, name + start, len);
	base[len] = '\0';
	const char* shown = tr(ROOTNAME_SYSTEMS, base);
	if (!shown || !strcmp(shown, base)) return 0;
	int need = snprintf(NULL, 0, "%.*s%s%s", (int)start, name, shown, name + start + len);
	if (need < 0 || (size_t)need >= size) return 0;
	snprintf(out, size, "%.*s%s%s", (int)start, name, shown, name + start + len);
	return 1;
}
