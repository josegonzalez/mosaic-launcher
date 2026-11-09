###########################################################
# music: its daemon, musicd, goes beside its plugin.so. It links SDL2 for
# audio, as the launcher does (from pkg-config, the toolchain's on
# devices), and decodes with the dr_libs headers copied into
# vendor/dr_libs (see VERSION.txt there), whose code is compiled on its
# own with its warnings off. The plugin itself is only music.c and
# music_model.c; the daemon's sources are in daemon/, which the plugins'
# own rule doesn't compile.

MUSIC_DR := $(abspath ../vendor/dr_libs)
MUSIC_OBJ := $(abspath ../build/music/$(ARCH))
MUSIC_DR_FLAGS := -DDR_FLAC_NO_OGG -DDR_FLAC_NO_WCHAR -DDR_WAV_NO_WCHAR
MUSIC_SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
MUSIC_SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
MUSICD := $(MUSIC_OBJ)/musicd
MUSIC_LICENSE := $(MUSIC_OBJ)/dr_libs-LICENSE.txt

$(MUSIC_OBJ)/dr_libs.o: music/daemon/dr_libs.c $(wildcard $(MUSIC_DR)/*.h)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu99 -O2 -w $(MUSIC_DR_FLAGS) -I$(MUSIC_DR) -c $< -o $@

$(MUSICD): music/daemon/musicd.c music/daemon/decoder.c music/daemon/decoder.h music/music_model.c music/music_model.h \
		$(MUSIC_OBJ)/dr_libs.o
	@mkdir -p $(dir $@)
	$(CC) -std=gnu99 -O2 -Wall -Werror $(MUSIC_DR_FLAGS) -Imusic -Imusic/daemon -I$(MUSIC_DR) $(MUSIC_SDL_CFLAGS) \
		music/daemon/musicd.c music/daemon/decoder.c music/music_model.c $(MUSIC_OBJ)/dr_libs.o \
		$(MUSIC_SDL_LIBS) -lm -o $@

$(MUSIC_LICENSE): $(MUSIC_DR)/LICENSE
	@mkdir -p $(dir $@)
	cp $< $@

music: $(MUSICD) $(MUSIC_LICENSE)
music: PLUGIN_SHIP = $(MUSICD) $(MUSIC_LICENSE)
