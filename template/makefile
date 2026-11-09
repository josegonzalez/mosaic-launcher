###########################################################
# Builds the plugin and its pak.
#
#   make           lib/aarch64/plugin.so, in the tg5040 toolchain (Docker or podman)
#   make desktop   lib/desktop-<os>-<arch>/plugin.so, with the host compiler
#   make pak       build/<name>.pak.zip, to unzip into Tools/<platform>/<name>.pak
#
# One aarch64 build runs on every NextUI device; tg5040's toolchain has the
# oldest sysroot, so building there keeps it compatible with all of them.
#
# With a helper/ folder, its .c files build the plugin's helper program,
# $(HELPER), beside plugin.so in each lib/<arch>/. Delete helper/ to go
# without one.
###########################################################

.PHONY: all build desktop pak clean

NAME := $(shell sed -n 's/^name=//p' plugin.txt)
ID := $(shell sed -n 's/^id=//p' plugin.txt)
CONTAINER_RUNTIME ?= $(shell command -v docker 2>/dev/null || command -v podman 2>/dev/null)
DESKTOP_ARCH := desktop-$(shell uname -s | tr A-Z a-z)-$(shell uname -m)
CFLAGS := -shared -fPIC -std=gnu99 -O2 -Wall -Werror -Isdk
LDLIBS := -ldl
HELPER := $(ID)-helper
HELPER_SRC := $(wildcard helper/*.c)
HELPER_CFLAGS := -std=gnu99 -O2 -Wall -Werror

all:
	$(CONTAINER_RUNTIME) run --rm -v $(CURDIR):/root/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		/bin/bash -c 'cd /root/workspace && make build CC=$${CROSS_COMPILE}gcc'

build:
	mkdir -p lib/aarch64
	$(CC) $(CFLAGS) src/*.c $(LDLIBS) -o lib/aarch64/plugin.so
	$(if $(HELPER_SRC),$(CC) $(HELPER_CFLAGS) $(HELPER_SRC) -o lib/aarch64/$(HELPER))

desktop:
	mkdir -p lib/$(DESKTOP_ARCH)
	$(CC) $(CFLAGS) src/*.c $(LDLIBS) -o lib/$(DESKTOP_ARCH)/plugin.so
	$(if $(HELPER_SRC),$(CC) $(HELPER_CFLAGS) $(HELPER_SRC) -o lib/$(DESKTOP_ARCH)/$(HELPER))

# The zip holds the pak's contents, not the pak directory itself. A build
# downloaded from another CI job has lost its modes, so the helper is made
# executable again.
pak:
	@test -f lib/aarch64/plugin.so || (echo "run make first" && exit 1)
	$(if $(HELPER_SRC),@test -f lib/aarch64/$(HELPER) || (echo "run make first" && exit 1))
	$(if $(HELPER_SRC),chmod +x lib/aarch64/$(HELPER))
	mkdir -p build
	rm -f "build/$(NAME).pak.zip"
	zip -r "build/$(NAME).pak.zip" plugin.txt launch.sh lib/aarch64 sdk/LICENSE

clean:
	rm -rf lib build
