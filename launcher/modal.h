#ifndef MODAL_H
#define MODAL_H

// Full-screen dialogs plugins can open: a yes/no confirmation, a notice, an
// on-screen keyboard and a list of choices. While one is open it's the current screen; closing it
// goes back to wherever the user was.

// Not a real view, so it sits well clear of config.h's SCREEN_* ids.
#define SCREEN_MODAL 100

// Shows message with B to cancel and A to go ahead. on_yes runs after the
// dialog closes, and only if the user chose A.
void Modal_confirm(const char* message, const char* yes_label, void (*on_yes)(void* data), void* data);

// Shows message with A to close.
void Modal_message(const char* message);

// Shows the keyboard with initial text. on_done runs after it closes, with
// the text, or with NULL if the user cancelled.
void Modal_keyboard(const char* title, const char* initial, void (*on_done)(const char* text, void* data), void* data);

// Shows a list of choices under title, drawn like the Launcher pak's menus,
// starting on selected. on_pick runs after it closes, with the index chosen
// with A, or -1 if the user backed out with B. The strings are copied.
// Returns 0, or -1 for no choices or more than MODAL_CHOICES_MAX.
#define MODAL_CHOICES_MAX 64
int Modal_choose(const char* title, const char* const* choices, int count, int selected,
	void (*on_pick)(int index, void* data), void* data);

void Modal_handleInput(void);
void Modal_render(void);

#endif
