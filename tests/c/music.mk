MUSIC := ../../plugins/music

$(BUILD)/test_music: test_music.c $(MUSIC)/music_model.c $(MUSIC)/music_model.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. test_music.c $(MUSIC)/music_model.c -o $@
