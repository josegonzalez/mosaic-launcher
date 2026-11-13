// The metadata band, without the host: which .info keys it shows, in what
// order, how each value is tidied, and how many of them fit a width. No
// SDL and no file access, so it's tested on the host.

#ifndef BAND_MODEL_H
#define BAND_MODEL_H

#include <stddef.h>

// The fields the band can show, each an .info key.
enum MBField {
	MB_YEAR,
	MB_PUBLISHER,
	MB_GENRE,
	MB_DEVELOPER,
	MB_PLAYERS,
	MB_FIELD_COUNT,
};

#define MB_VALUE_SIZE 128 // the longest value kept, with its terminator

// The .info key for a field, or NULL for one that isn't.
const char* MB_key(int field);

// Reads a comma separated list of keys, such as "year,publisher,genre",
// into fields in that order, matching keys without case and skipping
// spaces around them, unknown keys and repeats. Writes up to
// MB_FIELD_COUNT fields and returns how many.
int MB_fields(const char* list, int* fields);

// Tidies a field's value for the band: trims spaces, cuts it to size, and
// for the year keeps the first four digits in a row when it has them, as
// in "1994-11-21" or "19941121T000000". Returns 1, or 0 for a value
// that's empty once trimmed (writing "").
int MB_clean(int field, const char* value, char* out, size_t size);

typedef int (*MBMeasure)(const char* text, void* data);

// Joins values (count of them, in order, empty ones left out) with sep
// between them into out. When the line is wider than max_width by
// measure, it leaves out the last value until it fits; the first value
// stays even when it alone is too wide, for the caller to truncate.
// Returns how many values it joined, 0 when every value is empty, or -1
// for a bad argument.
int MB_join(const char* const* values, int count, const char* sep, int max_width, MBMeasure measure, void* data,
	char* out, size_t size);

///////////////////////////////////////
// the info.format service

#define MB_INFO_FILE_MAX 4096 // a sidecar's bytes, at most, as the launcher reads them

// Where path's .info sidecar is, as the launcher's entry_info finds it:
// <its folder>/.media/<its name less its last extension>.info.
void MB_infoPath(const char* path, char* out, size_t size);

// The value of key, matched without case, in len bytes of a sidecar, read
// as the launcher's entry_info reads it: key=value lines, blank lines,
// lines starting with # and lines whose key isn't 1 to 31 letters,
// digits, '_', '-' or '.' skipped, the last line with the key winning,
// and the value trimmed, with control characters turned to spaces.
// Writes it, cut to size, and returns 1, or writes "" and returns 0 when
// the key isn't there, or the text is over MB_INFO_FILE_MAX bytes or has
// a NUL.
int MB_infoValue(const char* text, size_t len, const char* key, char* out, size_t size);

// The service's reply for count fields (an enum MBField each) and their
// raw values: count=<how many have a value once tidied by MB_clean>, then
// <key>=<tidied value> for each of those, in order, and
// line=<them joined with sep>, which with max_width 0 or more has the
// last ones left out until it fits, as MB_join does by measure. Returns
// 0, or -1 for a bad argument or a reply that doesn't fit size.
int MB_reply(const int* fields, int count, const char* const* raw, const char* sep, int max_width, MBMeasure measure,
	void* data, char* out, size_t size);

#endif
