#ifndef CALLER_H
#define CALLER_H

// Which plugin's code the launcher is running on this thread: set around
// every hook and callback it calls, so host calls such as cfg_get know
// whose settings to use without the plugin naming itself. Other threads,
// such as a plugin's own, start with none.

typedef struct CallerScope {
	const char* was; // the caller before this one, put back at the end
	int once;
} CallerScope;

// The id of the plugin being run on this thread, or NULL for none.
const char* Caller_current(void);

// Starts running plugin_id's code (NULL for the launcher's own), and ends
// it, putting the one before back. Used through AS_CALLER.
CallerScope Caller_scope(const char* plugin_id);
void Caller_end(CallerScope* scope);

// Runs the statement after it as plugin_id's code, eg.
// AS_CALLER(id) rc = plugin->tick(now);
// The one before comes back however the statement ends, a return or break
// in it too.
#define AS_CALLER(plugin_id) \
	for (CallerScope caller_ __attribute__((cleanup(Caller_end))) = Caller_scope(plugin_id); caller_.once; caller_.once = 0)

#endif
