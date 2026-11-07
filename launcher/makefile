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
BUILD_DIR ?= $(abspath ../build/$(PLATFORM))

include $(NEXTUI)/$(PLATFORM)/platform/makefile.env
SDL ?= SDL

###########################################################

TARGET = nextui
INCDIR = -I. -I../sdk -I$(NEXTUI)/all/common/ -I$(NEXTUI)/$(PLATFORM)/platform/
MODULES = main.c state.c containers.c library.c launch.c nav.c imgloader.c \
	screens.c screen_gamelist.c screen_switcher.c screen_quickmenu.c art.c quickitems.c quicktiles.c tr.c actions.c transitions.c \
	pluginset.c plugins.c toast.c awake.c pakhooks.c kbmodel.c modal.c \
	cfgfile.c settings.c listsort.c

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
	$(NEXTUI)/all/common/api.c \
	$(NEXTUI)/$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -DPLUGIN_ARCH=\"$(PLUGIN_ARCH)\" -std=gnu99
LDFLAGS += -lmsettings

PRODUCT = $(BUILD_DIR)/$(TARGET).elf

.PHONY: all clean

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(PRODUCT)

# libmsettings is built from the vendored tree, the same way stock does it.
# Its objects land in vendor/NextUI, which .gitmodules ignores as untracked.
$(PREFIX_LOCAL)/include/msettings.h:
	cd $(NEXTUI)/$(PLATFORM)/libmsettings && $(MAKE)
