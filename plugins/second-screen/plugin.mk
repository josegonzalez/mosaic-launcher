###########################################################
# second-screen: its window is second-screen-display, a separate SDL2
# program the plugin starts, built from display/ (not part of plugin.so)
# and put beside plugin.so. Only desktop builds get it, from the system's
# SDL2 through pkg-config: no device has a second display, so there the
# plugin declines to start, and so it does on a desktop without SDL2.

SS_DIR := $(abspath second-screen)
SS_DISPLAY := $(abspath ../build/second-screen/$(ARCH))/second-screen-display
ifneq ($(ARCH),aarch64)
SS_SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SS_SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

$(SS_DISPLAY): $(SS_DIR)/display/display.c $(SS_DIR)/ss_model.c $(SS_DIR)/ss_model.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu99 -O2 -Wall -Werror -I$(SDK) -I$(SS_DIR) $(SS_SDL_CFLAGS) $(SS_DIR)/display/display.c \
		$(SS_DIR)/ss_model.c $(SS_SDL_LIBS) -o $@

second-screen: PLUGIN_LIBS = -ldl
ifneq (,$(SS_SDL_LIBS))
second-screen: $(SS_DISPLAY)
second-screen: PLUGIN_SHIP = $(SS_DISPLAY)
endif
