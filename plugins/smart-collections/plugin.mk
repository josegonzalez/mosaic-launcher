###########################################################
# smart-collections: builds in search's model, for its rules.

smart-collections: PLUGIN_SRCS = search/search_model.c
smart-collections: PLUGIN_CFLAGS = -Isearch
