###########################################################
# Builds the launcher against the pinned NextUI checkout in
# ../vendor/NextUI. Mirrors vendor/NextUI/workspace/all/nextui/makefile
# so the binary links the same common and platform code as stock.
###########################################################

ifeq (,$(PLATFORM))
$(error please specify PLATFORM, eg. make PLATFORM=tg5040)
endif

ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

###########################################################

NEXTUI ?= $(abspath ../vendor/NextUI/workspace)
RCHEEVOS ?= $(abspath ../vendor/rcheevos)
LIBCHDR ?= $(abspath ../vendor/libchdr)
BUILD_DIR ?= $(abspath ../build/$(PLATFORM))

include $(NEXTUI)/$(PLATFORM)/platform/makefile.env
SDL ?= SDL

###########################################################

TARGET = nextui
INCDIR = -I. -I../sdk -I$(NEXTUI)/all/common/ -I$(NEXTUI)/$(PLATFORM)/platform/ \
	-I$(RCHEEVOS)/include -I$(LIBCHDR)/include
MODULES = main.c state.c containers.c library.c launch.c nav.c imgloader.c \
	screens.c screen_gamelist.c screen_switcher.c screen_quickmenu.c art.c quickitems.c quicktiles.c tr.c trmodel.c text.c actions.c transitions.c \
	pluginset.c plugins.c toast.c awake.c power.c powerwatch.c pakhooks.c kbmodel.c modal.c \
	cfgfile.c settings.c listsort.c launchcmd.c stocktoggles.c leds.c bluetooth.c \
	drawmodel.c plugindraw.c layoutmodel.c artcache.c artloader.c selmodel.c \
	statusmodel.c statuspill.c infomodel.c transmodel.c background.c playmodel.c playhistory.c \
	inputmodel.c pin.c ramodel.c rahash.c rachd.c raprogress.c audiomodel.c audio.c screenmodel.c screen_plugin.c hintmodel.c wifi.c \
	theme.c thememodel.c fallback.c fallbackmodel.c

# Which lib/<arch>/plugin.so a plugin directory provides for this build. Every
# current device is aarch64, so one plugin build covers all of them.
ifeq ($(PLATFORM),desktop)
PLUGIN_ARCH ?= desktop-$(shell uname -s | tr A-Z a-z)-$(shell uname -m)
else
PLUGIN_ARCH ?= aarch64
endif
SOURCE = $(MODULES) \
	$(NEXTUI)/all/common/scaler.c \
	$(NEXTUI)/all/common/utils.c \
	$(NEXTUI)/all/common/config.c \
	$(API_C) \
	$(NEXTUI)/$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -DPLUGIN_ARCH=\"$(PLUGIN_ARCH)\" -std=gnu99
LDFLAGS += -lmsettings

PRODUCT = $(BUILD_DIR)/$(TARGET).elf

# The launcher's own build of stock's api.c: the vendored file with
# patches/api.c.patch, written to the build folder, so the vendored tree
# (and minarch and the Settings pak built from it) stays stock's. With
# uisizes.h, included ahead of every file of nextui.elf, it turns stock's
# UI sizes into runtime values whose defaults are stock's, and lets the
# theme slot's owner supply colours and the font. A NextUI bump that the
# patch no longer applies to fails here; scripts/upstream-drift.sh shows
# it with upstream's changes to api.c. fallbackttf.h, included ahead of
# that, sends every file's SDL_ttf text calls through fallback.c, for the
# fallback font.
API_C = $(BUILD_DIR)/api.c
UISIZES = -include $(CURDIR)/fallbackttf.h -include $(CURDIR)/uisizes.h

# rcheevos and libchdr, for RetroAchievements hashing; its rules mustn't
# become the default goal
.DEFAULT_GOAL := all
VENDOR_OBJ = $(BUILD_DIR)/vendor
include vendor.mk

.PHONY: all clean

all: $(PREFIX_LOCAL)/include/msettings.h $(VENDOR_OBJECTS) $(API_C)
	mkdir -p $(BUILD_DIR)
	$(CC) $(UISIZES) $(SOURCE) $(VENDOR_OBJECTS) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)

$(API_C): $(NEXTUI)/all/common/api.c patches/api.c.patch
	mkdir -p $(BUILD_DIR)
	patch -s -o $@ $(NEXTUI)/all/common/api.c patches/api.c.patch || { rm -f $@; exit 1; }

clean:
	rm -f $(PRODUCT) $(API_C)
	rm -rf $(VENDOR_OBJ)

# libmsettings is built from the vendored tree, the same way stock does it.
# Its objects land in vendor/NextUI, which .gitmodules ignores as untracked.
$(PREFIX_LOCAL)/include/msettings.h:
	cd $(NEXTUI)/$(PLATFORM)/libmsettings && $(MAKE)
