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

#endif
