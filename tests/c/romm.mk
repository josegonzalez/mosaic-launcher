# romm's model and HTTP client, with BearSSL built as the plugin builds it
# (plugins/romm/plugin.mk) into its own folder here.
ROMM := ../../plugins/romm
ROMM_BEARSSL := $(abspath ../../vendor/BearSSL)
ROMM_BEARSSL_OBJ := $(BUILD)/bearssl
ROMM_BEARSSL_OBJECTS := $(patsubst $(ROMM_BEARSSL)/src/%.c,$(ROMM_BEARSSL_OBJ)/%.o,\
	$(wildcard $(ROMM_BEARSSL)/src/*.c $(ROMM_BEARSSL)/src/*/*.c))

$(ROMM_BEARSSL_OBJ)/%.o: $(ROMM_BEARSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -fPIC -w -I$(ROMM_BEARSSL)/inc -I$(ROMM_BEARSSL)/src -c $< -o $@

ROMM_MINIZ := $(abspath ../../vendor/miniz)
ROMM_MINIZ_DEFS := -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_TIME

$(BUILD)/miniz/miniz.o: $(ROMM_MINIZ)/miniz.c $(ROMM_MINIZ)/miniz.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -fPIC -w $(ROMM_MINIZ_DEFS) -I$(ROMM_MINIZ) -c $< -o $@

$(ROMM_BEARSSL_OBJ)/libbearssl.a: $(ROMM_BEARSSL_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/test_romm: test_romm.c $(wildcard $(ROMM)/*.c $(ROMM)/*.h) $(SDK)/mosaic_plugin.h check.h words.h $(ROMM_BEARSSL_OBJ)/libbearssl.a $(BUILD)/miniz/miniz.o
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(ROMM) -I$(ROMM_BEARSSL)/inc -I$(ROMM_MINIZ) $(ROMM_MINIZ_DEFS) \
		-DFIXTURES=\"$(abspath fixtures/romm)\" test_romm.c $(ROMM)/romm_model.c $(ROMM)/romm_net.c \
		$(ROMM)/romm_zip.c $(ROMM_BEARSSL_OBJ)/libbearssl.a $(BUILD)/miniz/miniz.o -o $@
