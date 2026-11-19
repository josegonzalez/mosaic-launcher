###########################################################
# system-info counts the Storage page on a thread of its own.

system-info: PLUGIN_CFLAGS = -pthread
system-info: PLUGIN_LIBS = -pthread
