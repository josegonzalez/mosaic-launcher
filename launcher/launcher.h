#ifndef LAUNCHER_H
#define LAUNCHER_H

// Everything a launcher module needs: the system and NextUI headers the
// original single-file nextui.c pulled in, plus every launcher module.

#include <stdio.h>
#include <stdlib.h>
#include <msettings.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>  // For dirname()
#include "defines.h"
#include "api.h"
#include "utils.h"
#include "config.h"
#include <sys/resource.h>
#include <pthread.h>
#include <assert.h>
#include <limits.h>

#include "containers.h"
#include "library.h"
#include "state.h"
#include "launch.h"
#include "nav.h"
#include "imgloader.h"
#include "screens.h"
#include "art.h"
#include "quickitems.h"
#include "tr.h"
#include "actions.h"
#include "transitions.h"
#include "plugins.h"
#include "toast.h"
#include "awake.h"
#include "pakhooks.h"
#include "modal.h"
#include "cfgfile.h"
#include "settings.h"
#include "listsort.h"

#endif
