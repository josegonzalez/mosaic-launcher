###########################################################
# nextui-launcher
#
# Runs on the host (macOS or Linux). Device builds happen inside the
# LoveRetro toolchain images; the desktop build uses the host compiler.
###########################################################

.PHONY: all desktop run-desktop build-% devices pak release test smoke lint drift clean

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

# Runs the desktop build against the faux SD card at /var/tmp/nextui/sdcard
# that upstream's prepare script creates from the skeleton.
run-desktop: desktop
	bash $(NEXTUI)/workspace/desktop/prepare_fake_sd_root.sh
	$(DESKTOP_LIBRARY_PATH)=$(DESKTOP_PREFIX_LOCAL)/lib ./build/desktop/nextui.elf

###########################################################
# devices

build-%:
	@test -n "$(CONTAINER_RUNTIME)" || (echo "docker or podman is required" && exit 1)
	$(CONTAINER_RUNTIME) run --rm -v $(CURDIR):/root/workspace \
		ghcr.io/loveretro/$*-toolchain:latest \
		/bin/bash -c 'cd /root/workspace && make -C launcher PLATFORM=$*'

devices: $(addprefix build-,$(PLATFORMS))

###########################################################
# packaging

pak:
	./scripts/package.sh "$(VERSION)" $(PLATFORMS)

###########################################################
# checks

test:
	bats tests

# Starts the desktop build for a few seconds; fails if it exits on its own.
smoke: desktop
	./scripts/desktop-smoke.sh build/smoke.log

lint:
	shellcheck -x pak/launch.sh pak/bin/*.sh scripts/*.sh

# What changed in upstream's launcher between two NextUI tags, for porting.
drift:
	./scripts/upstream-drift.sh $(FROM) $(TO)

clean:
	rm -rf build
