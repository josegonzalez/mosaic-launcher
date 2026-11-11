###########################################################
# romm: HTTPS with BearSSL, its sources copied into vendor/BearSSL (see
# VERSION.txt there), compiled with its own flags (none of the plugins') into an archive
# that only romm's plugin.so links. Its licence goes beside the plugin.
# Zips with miniz, copied into vendor/miniz the same way, read-only and
# without zlib's names, its symbols hidden like BearSSL's and its licence
# beside the plugin too.

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

MINIZ := $(abspath ../vendor/miniz)
MINIZ_DEFS := -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_TIME
MINIZ_OBJ := $(abspath ../build/miniz/$(ARCH))/miniz.o
MINIZ_LICENSE := $(dir $(MINIZ_OBJ))miniz-LICENSE.txt

$(MINIZ_OBJ): $(MINIZ)/miniz.c $(MINIZ)/miniz.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -fPIC -fvisibility=hidden -w $(MINIZ_DEFS) -I$(MINIZ) -c $< -o $@

$(MINIZ_LICENSE): $(MINIZ)/LICENSE
	@mkdir -p $(dir $@)
	cp $< $@

romm: $(BEARSSL_LIB) $(BEARSSL_LICENSE) $(MINIZ_OBJ) $(MINIZ_LICENSE)
romm: PLUGIN_CFLAGS = -pthread -I$(BEARSSL)/inc -I$(MINIZ) $(MINIZ_DEFS)
romm: PLUGIN_LIBS = $(BEARSSL_LIB) $(MINIZ_OBJ) -pthread
romm: PLUGIN_SHIP = $(BEARSSL_LICENSE) $(MINIZ_LICENSE)
