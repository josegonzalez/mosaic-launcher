// An example helper program: a process of the plugin's own, beside its
// plugin.so, for work that shouldn't run inside the launcher. The plugin
// starts it with a socket as its stdin and its output in the plugin's log.
// It answers "ping" with "pong" and any other line with "?", and quits
// when the socket closes, as it does when the launcher quits or is killed.
// Replace it with your own, or delete helper/ (see README.md).

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// writes all of text to stdin's socket; 0, or -1 once the plugin is gone
static int answer(const char* text) {
	size_t len = strlen(text), sent = 0;
	while (sent < len) {
		ssize_t n = write(0, text + sent, len - sent);
		if (n <= 0) return -1;
		sent += (size_t)n;
	}
	return 0;
}

int main(void) {
	// a write after the plugin closed its end fails rather than kills
	signal(SIGPIPE, SIG_IGN);
	fprintf(stderr, "example-helper: started, pid %i\n", (int)getpid());
	char line[256];
	size_t got = 0;
	for (;;) {
		ssize_t n = read(0, line + got, sizeof(line) - 1 - got);
		if (n <= 0) break;
		got += (size_t)n;
		line[got] = '\0';
		char* end;
		while ((end = strchr(line, '\n'))) {
			*end = '\0';
			if (answer(strcmp(line, "ping") == 0 ? "pong\n" : "?\n") != 0) goto done;
			got -= (size_t)(end + 1 - line);
			memmove(line, end + 1, got + 1);
		}
		// a line too long to hold is dropped
		if (got == sizeof(line) - 1) got = 0;
	}
done:
	fprintf(stderr, "example-helper: the socket closed, quitting\n");
	return 0;
}
