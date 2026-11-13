# second-screen

Shows the game list's selection in a second window. It's experimental, and desktop only.

No NextUI device has a second display. On tg5040, tg5050 and h700 it declines to start and logs why, and its name on the Plugins page says "(experimental, desktop only)".

## Turn it on

Turn on **Second screen** in Launcher > Plugins on the desktop build ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## What it shows

Each time the selection settles, the second window shows:

- The entry's name, wrapped onto up to three lines.
- Its thumbnail or, without one, its box art.
- Whether it's a game, folder or [pak](../../docs/glossary.md#pak).
- The year, genre, publisher, developer and players from its `.info` sidecar (`<folder>/.media/<name>.info`) where it has them. A year written as a date shows as its first four digits.
- "Empty folder" for an empty folder.

In a window at least 4:3 wide the art is a square on the left with the text beside it. In a narrower one the art is on top and the text under it.

Closing the second window stops it until the launcher restarts, and it closes when the launcher quits.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Window size (`size`) | 480x320, 640x480, 800x480, 480x640 | 640x480 | The second window's size, width by height |

## For plugin authors

- The window is a separate program, `second-screen-display`, which the plugin starts from beside its `plugin.so` and which needs SDL2. It's built only for desktop, and without it the plugin declines to start (see "Helper programs" in [plugin-api.md](../../docs/plugin-api.md)).
- The launcher's own window, input and frames are left as they are: the plugin draws each frame on a surface of its own in the launcher's fonts and theme, reads it back a few rows a frame, and hands it over in a file under `/tmp`.
- The window's program logs to `.userdata/desktop/logs/second-screen.txt`.

## Credits

tapionallanaire.
