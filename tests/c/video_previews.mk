VIDEO_PREVIEWS := $(abspath ../../plugins/video-previews)

$(BUILD)/test_video_previews: test_video_previews.c $(VIDEO_PREVIEWS)/vp_model.c $(VIDEO_PREVIEWS)/vp_model.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. test_video_previews.c $(VIDEO_PREVIEWS)/vp_model.c -o $@
