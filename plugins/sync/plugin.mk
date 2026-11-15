###########################################################
# sync: the programs it runs go beside its plugin.so. On devices they're
# rclone, Syncthing, rsync and Dropbear's dbclient from `make
# sync-tools-device` (plugins/sync/tools/build.sh, pinned and checksummed
# downloads), with their licences; without them the plugin builds and
# says its program isn't there. On desktop, links to the computer's own
# rclone, rsync, syncthing and ssh, for those it has, so tests need no
# downloads.

SYNC_TOOLS ?= $(abspath ../build/sync-tools/$(ARCH))
ifeq ($(ARCH),aarch64)
SYNC_SHIP := $(wildcard $(SYNC_TOOLS)/ship/*)
SYNC_LINKS :=
else
SYNC_SHIP :=
SYNC_LINKS := $(foreach tool,rclone rsync syncthing ssh,$(firstword $(shell command -v $(tool) 2>/dev/null)))
endif

.PHONY: sync-links
sync-links:
	mkdir -p $(OUT)/sync.plugin/lib/$(ARCH)
	$(foreach tool,$(SYNC_LINKS),ln -sf $(tool) $(OUT)/sync.plugin/lib/$(ARCH)/$(notdir $(tool));)

sync: sync-links
sync: PLUGIN_CFLAGS = -pthread
sync: PLUGIN_LIBS = -pthread $(if $(filter Linux,$(shell uname -s)),-ldl)
sync: PLUGIN_SHIP = $(SYNC_SHIP)
