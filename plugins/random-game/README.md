# random-game

Picks a game at random and launches it.

## Turn it on

Turn on **Random game** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

- On the game list, press **Y** to pick from the system you're in, or from every system on the first screen.
- A Random game row on the first screen and a Random tile on the [quick menu](../../docs/glossary.md#quick-menu) pick from every system.

Every game is as likely as any other, so bigger systems come up more often. With Prefer unplayed games on (the default), games not in Recently Played are four times as likely. Games the [`hidden`](../hidden/README.md) plugin hides are never picked.

## Screenshots

The Random game row on the first screen, with the default settings.

![The Random game row on the first screen, with the default settings](docs/default.png)

Random tile on the quick menu on.

![Random tile on the quick menu on](docs/tile.png)

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Prefer unplayed games (`unplayed`) | on, off | on | Games not in Recently Played are four times as likely |
| Random game on the root (`root`) | on, off | on | A Random game row on the first screen that picks from every system |
| Random tile on the quick menu (`tile`) | on, off | off | A Random tile on the quick menu that picks from every system |

## For plugin authors

- Registers the `random-game.pick` action and the `random-game.tile` quick menu tile.
- Reads the `hidden` entry set.

## Credits

casualducko and kUI.
