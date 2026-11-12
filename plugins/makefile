###########################################################
# Builds every plugins/<name>/ (a plugin.txt plus its .c files) into
# $(OUT)/<name>.plugin/, the layout the launcher loads:
#
#   <name>.plugin/plugin.txt
#   <name>.plugin/lib/$(ARCH)/plugin.so
#   <name>.plugin/res/              a copy of its res/, when it has one,
#                                   such as its language packs in res/lang/
#
# Devices: ARCH=aarch64 with the tg5040 toolchain, the oldest sysroot, so
# one build runs on every device. Desktop: ARCH=desktop-<os>-<arch>.
###########################################################

ARCH ?= aarch64
OUT ?= $(abspath ../build/plugins)
SDK := $(abspath ../sdk)
CC ?= cc

PLUGINS := $(patsubst %/plugin.txt,%,$(wildcard */plugin.txt))

# video-previews includes ffmpeg's headers but never links ffmpeg, loading
# it with dlopen. On devices they're the trimmed build's in $(FFMPEG_DIR)
# (`make ffmpeg-device`), whose libraries and licence go beside its
# plugin.so; on desktop, the system's from pkg-config, loaded from their
# libdir. Without them it builds without ffmpeg and says it's unavailable.
FFMPEG_DIR ?= $(abspath ../build/ffmpeg/$(ARCH))
FFMPEG_PKGS := libavformat libavcodec libavutil
ifneq (,$(wildcard $(FFMPEG_DIR)/prefix/include/libavformat/avformat.h))
FFMPEG_CFLAGS := -DVP_HAVE_FFMPEG -I$(FFMPEG_DIR)/prefix/include
FFMPEG_SHIP := $(wildcard $(FFMPEG_DIR)/ship/*)
else ifneq ($(ARCH),aarch64)
ifeq (yes,$(shell pkg-config --exists $(FFMPEG_PKGS) 2>/dev/null && echo yes))
FFMPEG_CFLAGS := -DVP_HAVE_FFMPEG $(shell pkg-config --cflags $(FFMPEG_PKGS)) \
	-DVP_FFMPEG_LIBDIR=\"$(shell pkg-config --variable=libdir libavformat)\"
endif
endif
video-previews: PLUGIN_CFLAGS = -pthread $(FFMPEG_CFLAGS)
video-previews: PLUGIN_LIBS = -pthread -ldl
video-previews: PLUGIN_SHIP = $(FFMPEG_SHIP)

.PHONY: all clean $(PLUGINS)

all: $(PLUGINS)

$(PLUGINS):
	mkdir -p $(OUT)/$@.plugin/lib/$(ARCH)
	cp $@/plugin.txt $(OUT)/$@.plugin/plugin.txt
	$(CC) -shared -fPIC -std=gnu99 -O2 -Wall -Werror -I$(SDK) $(PLUGIN_CFLAGS) $(wildcard $@/*.c) $(PLUGIN_LIBS) \
		-o $(OUT)/$@.plugin/lib/$(ARCH)/plugin.so
	$(if $(PLUGIN_SHIP),cp $(PLUGIN_SHIP) $(OUT)/$@.plugin/lib/$(ARCH)/)
	rm -rf $(OUT)/$@.plugin/res
	$(if $(wildcard $@/res),cp -R $@/res $(OUT)/$@.plugin/res)

clean:
	rm -rf $(OUT)

# A plugin that needs more than its own .c files (a vendored library, extra
# flags) has a plugin.mk beside its plugin.txt with its target's rules.
include $(wildcard */plugin.mk)
