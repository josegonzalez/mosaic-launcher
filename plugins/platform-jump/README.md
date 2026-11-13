# platform-jump

Makes **L1** and **R1** jump between systems instead of letters.

## How to use

| Where | What **L1** and **R1** do |
|---|---|
| Inside a system, or any folder in it | Open the previous or next system in the first screen's order, as if you'd backed out and opened it there, so **B** goes back to the first screen with that system selected |
| Recently Played, collections, Favorites and other lists with games from several systems (the Jump between systems in collections setting) | Move to the first game of the previous or next system, taking systems in the order they first appear in the list |
| The first screen | Jump letters, unless Jump between systems on the root is on. Then they move between the system rows, past Recently Played, Collections, Tools and other plugins' rows |
| Everywhere else, and where there's only one system to go to | Jump letters, as stock does |

Past the last system they wrap around to the first (the Wrap around setting). Both kinds of jump repeat while the shoulder is held, at stock's pace, so holding **R1** inside a system steps through the systems. On a launcher without `override_repeat` they go once per press.

With a first screen another plugin replaced, such as [`all-games`](../all-games/README.md)', the systems come from the next list of systems shown, such as its Systems folder, and **B** from a system it opened goes back through the Roms folder.

## Turn it on

Turn on **Platform jump** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

It takes **L1** and **R1**, so other plugins' bindings on them don't run and the shared-button menu never opens on them. If another plugin earlier in load order already overrides either one, platform jump doesn't load. It works with [`button-remap`](../button-remap/README.md) as described there.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Jump between systems on the root (`root`) | on, off | off | On the first screen, **L1** and **R1** move between the system rows instead of letters |
| Jump between systems in collections (`lists`) | on, off | on | In Recently Played, collections and other lists with several systems, **L1** and **R1** move to the next system's games |
| Wrap around (`wrap`) | on, off | on | Past the last system, **R1** goes back to the first and **L1** the other way |

## For plugin authors

- Takes L1 and R1 with `override_button`, and repeats with `override_repeat`.
- Has no slot or service of its own.

## Credits

kUI.
