###########################################################
# filter-presets: builds in search's model, for its queries.

filter-presets: PLUGIN_SRCS = search/search_model.c
filter-presets: PLUGIN_CFLAGS = -Isearch
