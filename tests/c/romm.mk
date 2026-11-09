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

$(ROMM_BEARSSL_OBJ)/libbearssl.a: $(ROMM_BEARSSL_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/test_romm: test_romm.c $(wildcard $(ROMM)/*.c $(ROMM)/*.h) $(SDK)/mosaic_plugin.h check.h $(ROMM_BEARSSL_OBJ)/libbearssl.a
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(ROMM) -I$(ROMM_BEARSSL)/inc -DFIXTURES=\"$(abspath fixtures/romm)\" \
		test_romm.c $(ROMM)/romm_model.c $(ROMM)/romm_net.c $(ROMM_BEARSSL_OBJ)/libbearssl.a -o $@
