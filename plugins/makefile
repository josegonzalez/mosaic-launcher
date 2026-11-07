###########################################################
# Builds every plugins/<name>/ (a plugin.txt plus its .c files) into
# $(OUT)/<name>.plugin/, the layout the launcher loads:
#
#   <name>.plugin/plugin.txt
#   <name>.plugin/lib/$(ARCH)/plugin.so
#
# Devices: ARCH=aarch64 with the tg5040 toolchain, the oldest sysroot, so
# one build runs on every device. Desktop: ARCH=desktop-<os>-<arch>.
###########################################################

ARCH ?= aarch64
OUT ?= $(abspath ../build/plugins)
SDK := $(abspath ../sdk)
CC ?= cc

PLUGINS := $(patsubst %/plugin.txt,%,$(wildcard */plugin.txt))

.PHONY: all clean $(PLUGINS)

all: $(PLUGINS)

$(PLUGINS):
	mkdir -p $(OUT)/$@.plugin/lib/$(ARCH)
	cp $@/plugin.txt $(OUT)/$@.plugin/plugin.txt
	$(CC) -shared -fPIC -std=gnu99 -O2 -Wall -Werror -I$(SDK) $(wildcard $@/*.c) -o $(OUT)/$@.plugin/lib/$(ARCH)/plugin.so

clean:
	rm -rf $(OUT)
