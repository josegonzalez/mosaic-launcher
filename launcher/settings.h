#ifndef SETTINGS_H
#define SETTINGS_H

// The Launcher pak's screens: turning the launcher on or off, the crash
// guard, and plugins and their settings. Launcher.pak/launch.sh runs
// nextui.elf --settings, or --settings plugins to open the plugin list.
// Drawn the same way as the game list.

// Not a real view, so it sits clear of config.h's SCREEN_* ids.
#define SCREEN_SETTINGS 101

// Runs the settings screens until the user backs out. args are what came
// after --settings.
int Settings_main(int argc, char* argv[]);

#endif
