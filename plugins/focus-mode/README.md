# focus-mode

Makes the first screen list only your favorites, so you reach them without scrolling past every console.

## What it does

- The first screen lists your favorites, in the favorites collection's order, then an All systems folder with every console.
- Other plugins' root folders stay off it.
- Recents, Collections and Tools stay on the [quick menu](../../docs/glossary.md#quick-menu).
- Until a favorite is on the card the first screen is stock's.

## Turn it on

Turn on **Focus mode** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). It needs [`favorites`](../favorites/README.md) on too. It can't be on together with [`all-games`](../all-games/README.md), because both take the `root` [slot](../../docs/glossary.md#slot).

## How to use

A Focus tile on the quick menu turns focus mode off and on, as does the Focus on favorites setting.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Focus on favorites (`on`) | on, off | on | The first screen lists only favorites and All systems. The Focus tile on the quick menu flips it too |

## For plugin authors

- Owns the `root` slot (`slots=root`).
- Requires `favorites` (`requires=favorites`), and reads its `favorites` entry set.
- Registers a quick menu tile.

## Credits

MinUI-Zero and UnuOS.
