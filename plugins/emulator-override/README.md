# emulator-override

Lets you pick which emulator [pak](../../docs/glossary.md#pak) a game launches with.

## Turn it on

Turn on **Emulator override** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

Press **Y** on a game (the Emulator button setting picks another button). It shares the button with other plugins' actions there. A list opens:

- Default, the system's own pak.
- The other emulator paks on the card known to run its games: GB, GBC, SGB and MGBA for Game Boy and Game Boy Color games, MGBA for Game Boy Advance, and SFC and SUPA for each other.

Turn on List every emulator pak to list every emulator pak instead.

Picking one saves it for that game, and Default clears it. A game folder, its discs and its `.m3u` share one choice.

## Checks before launch

Before each launch the choice is checked again. The pak has to be in `Emus/<platform>/` or `.system/<platform>/paks/Emus/`, found the way stock finds a system's pak, with a `launch.sh` that's a regular file and doesn't lead out of its pak through a link. Otherwise the game launches as usual and the launcher log says why.

A valid choice replaces the launch with the pak's `launch.sh` and the game's path, quoted as stock quotes its own. So the game's saves and states go under that pak's name, as when a system folder is renamed to it. Paks launch as usual.

With another plugin that replaces launches, only the first of them in load order replaces a launch. When that's the other plugin, this plugin's `replace_launch` is refused and logged and the other's command runs. When it's `emulator-override`, the other's is refused.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Emulator button (`button`) | Y, L2, R2 | Y | The game list button that picks a game's emulator, shared with other plugins' actions on it |
| List every emulator pak (`all`) | on, off | off | On lists every emulator pak on the card, not only those known to run the system's games |

## For plugin authors

- Provides the `emulator.choice` service (`provides=emulator.choice`): ask with a game or game folder's `path` and get the pak's name and its `launch.sh`, which is empty when the pak fails the launch's check (see [plugin-api.md](../../docs/plugin-api.md)).
- Registers the `emulator-override.choose` action, which the `button` setting follows.
- Uses `replace_launch`.
- Writes the choices to `.userdata/<platform>/launcher/emulator-override.txt`: one card-relative game path, a tab and the pak's name per line.

## Credits

LodorOS.
