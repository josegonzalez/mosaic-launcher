# all-games

Turns the first screen into one list of every game on the card, from all systems, sorted by name.

## What it does

- The list uses the names each system's list uses, `map.txt` aliases included.
- A Systems folder and other plugins' root folders come first. Recents, Collections and Tools stay on the [quick menu](../../docs/glossary.md#quick-menu).
- Subfolders are flattened two levels deep, and a folder with a `.cue` or `.m3u` named after it counts as one game.
- Games from different systems with the same name show their system's tag, such as "Tetris (GB)" and "Tetris (GBC)".
- Until a game is on the card the first screen is stock's.

The card is scanned once per start, and again only when a folder it read changes.

## Turn it on

Turn on **All games** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). It can't be on together with [`focus-mode`](../focus-mode/README.md), because both take the `root` [slot](../../docs/glossary.md#slot).

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Show the system on shared names (`tags`) | on, off | on | Adds the system tag to games with the same name. Turn it off to leave that to stock or [`title-disambiguation`](../title-disambiguation/README.md) |

## For plugin authors

- Owns the `root` slot (`slots=root`), so it conflicts with any other plugin that owns it.

## Credits

Xisrith.
