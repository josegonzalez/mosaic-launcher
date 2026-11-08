###########################################################
# RetroAchievements hashing: the parts of rcheevos and libchdr that
# minarch hashes games with, at the commits minarch builds, compiled with
# their own flags (none of the launcher's) and linked in as objects.
# minarch builds libchdr with cmake; the same sources and options are
# listed here so every toolchain builds it without cmake. The launcher and
# the unit tests include this, with RCHEEVOS, LIBCHDR, VENDOR_OBJ and CC
# set. It has no targets of its own, only the rules for VENDOR_OBJECTS.

VENDOR_CFLAGS = -O2 -std=gnu99 -fPIC -w # their warnings are theirs

RHASH_SOURCES = $(addprefix $(RCHEEVOS)/src/rhash/,aes.c cdreader.c hash.c hash_disc.c hash_encrypted.c \
	hash_rom.c hash_zip.c md5.c)
RHASH_OBJECTS = $(patsubst $(RCHEEVOS)/src/rhash/%.c,$(VENDOR_OBJ)/rhash/%.o,$(RHASH_SOURCES))
RHASH_CFLAGS = -I$(RCHEEVOS)/include -I$(RCHEEVOS)/src -DRC_DISABLE_LUA

LZMA = $(LIBCHDR)/deps/lzma-26.02
MINIZ = $(LIBCHDR)/deps/miniz-3.1.2
ZSTD = $(LIBCHDR)/deps/zstd-1.5.7
CHDR_SOURCES = $(wildcard $(LIBCHDR)/src/libchdr_*.c)
CHDR_OBJECTS = $(patsubst $(LIBCHDR)/src/%.c,$(VENDOR_OBJ)/chdr/%.o,$(CHDR_SOURCES)) \
	$(VENDOR_OBJ)/chdr/LzmaDec.o $(VENDOR_OBJ)/chdr/miniz.o $(VENDOR_OBJ)/chdr/zstddeclib.o
# libchdr's CMakeLists defaults: raw sectors, subcode, block CRCs, its own
# miniz for zlib, its own zstd, and dr_flac
CHDR_CFLAGS = -I$(LIBCHDR)/include -I$(LZMA)/include -I$(MINIZ) -I$(ZSTD) \
	-DWANT_RAW_DATA_SECTOR=1 -DWANT_SUBCODE=1 -DVERIFY_BLOCK_CRC=1 -DLOWRAM_TARGET=0 \
	-DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_STDIO -DMINIZ_NO_TIME

VENDOR_OBJECTS = $(RHASH_OBJECTS) $(CHDR_OBJECTS)

$(VENDOR_OBJ)/rhash/%.o: $(RCHEEVOS)/src/rhash/%.c
	mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(RHASH_CFLAGS) -c $< -o $@

$(VENDOR_OBJ)/chdr/%.o: $(LIBCHDR)/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(CHDR_CFLAGS) -c $< -o $@

$(VENDOR_OBJ)/chdr/LzmaDec.o: $(LZMA)/src/LzmaDec.c
	mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(CHDR_CFLAGS) -c $< -o $@

$(VENDOR_OBJ)/chdr/miniz.o: $(MINIZ)/miniz.c
	mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(CHDR_CFLAGS) -c $< -o $@

$(VENDOR_OBJ)/chdr/zstddeclib.o: $(ZSTD)/zstddeclib.c
	mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(CHDR_CFLAGS) -c $< -o $@
