###########################################################
# romm: HTTPS with BearSSL, its sources copied into vendor/BearSSL (see
# VERSION.txt there), compiled with its own flags (none of the plugins') into an archive
# that only romm's plugin.so links. Its licence goes beside the plugin.

BEARSSL := $(abspath ../vendor/BearSSL)
BEARSSL_OBJ := $(abspath ../build/bearssl/$(ARCH))
BEARSSL_SOURCES := $(wildcard $(BEARSSL)/src/*.c $(BEARSSL)/src/*/*.c)
BEARSSL_OBJECTS := $(patsubst $(BEARSSL)/src/%.c,$(BEARSSL_OBJ)/%.o,$(BEARSSL_SOURCES))
BEARSSL_LIB := $(BEARSSL_OBJ)/libbearssl.a
BEARSSL_LICENSE := $(BEARSSL_OBJ)/BearSSL-LICENSE.txt
# the cross toolchain's own ar beside its gcc
BEARSSL_AR := $(if $(filter %gcc,$(CC)),$(patsubst %gcc,%ar,$(CC)),ar)

$(BEARSSL_OBJ)/%.o: $(BEARSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -fPIC -fvisibility=hidden -w -I$(BEARSSL)/inc -I$(BEARSSL)/src -c $< -o $@

$(BEARSSL_LIB): $(BEARSSL_OBJECTS)
	rm -f $@
	$(BEARSSL_AR) rcs $@ $^

$(BEARSSL_LICENSE): $(BEARSSL)/LICENSE.txt
	@mkdir -p $(dir $@)
	cp $< $@

romm: $(BEARSSL_LIB) $(BEARSSL_LICENSE)
romm: PLUGIN_CFLAGS = -pthread -I$(BEARSSL)/inc
romm: PLUGIN_LIBS = $(BEARSSL_LIB) -pthread
romm: PLUGIN_SHIP = $(BEARSSL_LICENSE)
