MUSIC := ../../plugins/music

$(BUILD)/test_music: test_music.c $(MUSIC)/music_model.c $(MUSIC)/music_model.h $(MUSIC)/music_tags.c \
		$(MUSIC)/music_tags.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. test_music.c $(MUSIC)/music_model.c $(MUSIC)/music_tags.c -o $@
