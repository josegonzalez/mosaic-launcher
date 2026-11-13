# core-prefetch

Warms a game's [core](../../docs/glossary.md#core) before it launches, so it starts faster.

It needs `posix_fadvise`, so on the desktop build, or on macOS, it loads but logs that it's unsupported and does nothing. Its name on the Plugins page says "(unsupported on desktop)".

## Turn it on

Turn on **Core prefetch** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## What it does

Just before a game launches it:

1. Finds the emulator [pak](../../docs/glossary.md#pak) the game launches with, the way stock does. When [`emulator-override`](../emulator-override/README.md) is loaded and its choice is a pak on the card, it uses that one.
2. Reads that pak's `launch.sh` for its core. This works for scripts that set `EMU_EXE` and run `"$CORES_PATH/${EMU_EXE}_libretro.so"`, as stock's paks all do, with the core in `.system/<platform>/cores/` or, with `CORES_PATH=$(dirname "$0")`, in the pak itself. For any other script it says in the launcher log that it can't tell which core runs.
3. Asks the kernel to read the core, and then the game (the first disc of a `.m3u`, and the first file a `.cue` names, when Warm the game too is on), into memory, a 1 MB chunk at a time. The reading goes on while the launcher exits and the pak's script starts.

It only opens files read-only, and only regular files on the card, with links resolved. It never writes anything, starts a process or cancels a launch. Paks aren't warmed.

It stops at the most-to-read-ahead limit across the files, and starts no chunk once the longest-wait time has passed since the launch began, so that's about how long it can add to a launch.

With another plugin that replaces launches earlier in load order than `emulator-override`, it can warm a different core than the one that runs.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Warm the game too (`rom`) | on, off | on | Off warms only the core, not the game |
| Most to read ahead, in MB (`max_mb`) | 4, 8, 16, 32, 64 | 16 | The most read ahead per launch, across the core and the game |
| Longest it waits, in ms (`max_ms`) | 10, 20, 50, 100 | 20 | No new read starts after this long in `pre_launch` |

## For plugin authors

- Works in the `pre_launch` hook, using `posix_fadvise(POSIX_FADV_WILLNEED)`.
- Reads `launch.sh` of the emulator pak and the choice of `emulator-override` (its `emulator.choice` service) when that plugin is loaded.
- Writes nothing.

## Credits

MinUI-Zero.
