#ifndef CHECK_H
#define CHECK_H

// A tiny test harness: each CHECK prints the failing line and counts it,
// and CHECK_DONE() turns the count into the exit code.

#include <stdio.h>
#include <string.h>

static int check_failures = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		check_failures++; \
	} \
} while (0)

#define CHECK_STR(got, want) do { \
	const char* check_got = (got); \
	const char* check_want = (want); \
	if (strcmp(check_got, check_want) != 0) { \
		fprintf(stderr, "%s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, check_got, check_want); \
		check_failures++; \
	} \
} while (0)

#define CHECK_DONE() do { \
	if (check_failures) fprintf(stderr, "%d check(s) failed\n", check_failures); \
	else printf("ok %s\n", __FILE__); \
	return check_failures ? 1 : 0; \
} while (0)

#endif
