###########################################################
# mosaic-launcher
#
# Runs on the host (macOS or Linux). Device builds happen inside the
# LoveRetro toolchain images; the desktop build uses the host compiler.
###########################################################

.PHONY: all desktop run-desktop plugins-desktop plugins-device ffmpeg-device build-% devices pak test smoke test-plugins test-profiles lint pot drift clean

PLATFORMS ?= tg5040 tg5050 h700
NEXTUI := vendor/NextUI
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

UNAME_S := $(shell uname -s)
export UNAME_S

CONTAINER_RUNTIME ?= $(shell command -v docker 2>/dev/null || command -v podman 2>/dev/null)

all: devices pak

###########################################################
# desktop

# The desktop platform expects a suffix-less gcc. Upstream symlinks Homebrew's
# gcc into /usr/local/bin with sudo; we keep the symlinks inside build/.
HOST_BIN := $(CURDIR)/build/host-bin
ifeq ($(UNAME_S),Darwin)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
HOST_GCC := $(firstword $(wildcard $(BREW_PREFIX)/opt/gcc/bin/gcc-[0-9]*))
DESKTOP_PREFIX := $(BREW_PREFIX)
DESKTOP_LIBRARY_PATH := DYLD_LIBRARY_PATH
else
HOST_GCC := $(shell command -v gcc)
DESKTOP_PREFIX := /usr
DESKTOP_LIBRARY_PATH := LD_LIBRARY_PATH
endif
DESKTOP_PREFIX_LOCAL := /var/tmp/nextui

$(HOST_BIN)/gcc:
	@test -n "$(HOST_GCC)" || (echo "gcc not found; on macOS run: brew install gcc" && exit 1)
	mkdir -p $(HOST_BIN)
	ln -sf $(HOST_GCC) $(HOST_BIN)/gcc

desktop: $(HOST_BIN)/gcc
	$(MAKE) -C launcher PLATFORM=desktop CROSS_COMPILE=$(HOST_BIN)/ \
		PREFIX=$(DESKTOP_PREFIX) PREFIX_LOCAL=$(DESKTOP_PREFIX_LOCAL)

DESKTOP_PLUGIN_ARCH := desktop-$(shell uname -s | tr A-Z a-z)-$(shell uname -m)

# The bundled plugins, for loading into the desktop build.
plugins-desktop:
	$(MAKE) -C plugins ARCH=$(DESKTOP_PLUGIN_ARCH) OUT=$(CURDIR)/build/desktop-plugins

# Runs the desktop build against the faux SD card at /var/tmp/nextui/sdcard
# that upstream's prepare script creates from the skeleton, with the bundled
# plugins available (turn them on in .userdata/desktop/launcher/plugins.txt).
run-desktop: desktop plugins-desktop
	bash $(NEXTUI)/workspace/desktop/prepare_fake_sd_root.sh
	MOSAIC_LAUNCHER_PLUGIN_PATH=$(CURDIR)/build/desktop-plugins \
		$(DESKTOP_LIBRARY_PATH)=$(DESKTOP_PREFIX_LOCAL)/lib ./build/desktop/nextui.elf

###########################################################
# devices

build-%:
	@test -n "$(CONTAINER_RUNTIME)" || (echo "docker or podman is required" && exit 1)
	$(CONTAINER_RUNTIME) run --rm -v $(CURDIR):/root/workspace \
		ghcr.io/loveretro/$*-toolchain:latest \
		/bin/bash -c 'cd /root/workspace && make -C launcher PLATFORM=$*'

# One aarch64 build of the plugins, in the oldest sysroot, for every device.
plugins-device:
	@test -n "$(CONTAINER_RUNTIME)" || (echo "docker or podman is required" && exit 1)
	$(CONTAINER_RUNTIME) run --rm -v $(CURDIR):/root/workspace \
		ghcr.io/loveretro/tg5040-toolchain:latest \
		/bin/bash -c 'cd /root/workspace && make -C plugins ARCH=aarch64 CC=$${CROSS_COMPILE}gcc OUT=/root/workspace/build/plugins'

# The trimmed LGPL ffmpeg video-previews loads, built once in the oldest
# sysroot from a pinned tarball (stamped, so it's a no-op once built), for
# plugins-device to put beside the plugin. Without it the plugin builds
# without ffmpeg and says it's unavailable.
ffmpeg-device:
	@test -n "$(CONTAINER_RUNTIME)" || (echo "docker or podman is required" && exit 1)
	$(CONTAINER_RUNTIME) run --rm -v $(CURDIR):/root/workspace \
		ghcr.io/loveretro/tg5040-toolchain:latest \
		/bin/bash -c 'cd /root/workspace && sh plugins/video-previews/ffmpeg/build.sh build/ffmpeg/aarch64'

devices: $(addprefix build-,$(PLATFORMS))
	$(MAKE) ffmpeg-device
	$(MAKE) plugins-device

###########################################################
# packaging

pak:
	./scripts/package.sh "$(VERSION)" $(PLATFORMS)

###########################################################
# checks

test:
	bats tests
	$(MAKE) -C tests/c

# Starts the desktop build for a few seconds, then the Launcher pak's
# settings screens; fails if either exits on its own or ignores SIGTERM.
FAKE_CARD = /var/tmp/nextui/sdcard
smoke: desktop
	./scripts/desktop-smoke.sh build/smoke.log
	SDCARD_PATH=$(FAKE_CARD) USERDATA_PATH=$(FAKE_CARD)/.userdata/desktop SYSTEM_PATH=$(FAKE_CARD)/.system/desktop \
		PLATFORM=desktop MOSAIC_LAUNCHER_PAK_DIR=$(CURDIR)/pak MOSAIC_LAUNCHER_RUN_DIR=$(CURDIR)/build/run \
		MOSAIC_LAUNCHER_PLUGIN_PATH=$(CURDIR)/build/plugins \
		./scripts/desktop-smoke.sh build/smoke-settings.log 4 --settings

# Loads test plugins (one good, one crashing) into the desktop build. With
# SHARD=<i>/<n>, runs only that shard's cases.
test-plugins: desktop
	./tests/plugins/run.sh

# Runs the bundled plugins together in profiles built from their plugin.txt
# files: everything on, then each exclusive slot rotated through its owners,
# until every owner has been loaded. Fails if the launcher or its plugin settings exit on their own,
# or the loader refuses a plugin the profile didn't turn off.
test-profiles: desktop plugins-desktop
	./scripts/plugin-profiles.sh

lint:
	shellcheck -x pak/launch.sh pak/bin/*.sh scripts/*.sh tests/plugins/run.sh template/launch.sh plugins/video-previews/ffmpeg/build.sh
	@# the cases set variables run.sh's helpers read, such as plugin and log, and read ones it sets
	shellcheck -x -e SC2034,SC2153,SC2154 tests/plugins/cases/*.sh
	@# PLUGINS.md's bundled plugins table is made from plugins/*/README.md
	./scripts/plugin-table.sh --check
	@# the template ships its own copy of the SDK; it must match
	cmp sdk/mosaic_plugin.h template/sdk/mosaic_plugin.h
	cmp sdk/LICENSE template/sdk/LICENSE
	@# po/launcher.pot must list every TR() string, and each plugin's
	@# po/<id>.pot its plugin.txt's name, setting labels and options and its tr() strings
	./scripts/pot.sh --check

# Writes po/launcher.pot, the gettext template of the launcher's strings,
# and each bundled plugin's (and the template's) po/<id>.pot.
pot:
	./scripts/pot.sh

# What changed in upstream's launcher between two NextUI tags, for porting.
drift:
	./scripts/upstream-drift.sh $(FROM) $(TO)

clean:
	rm -rf build
