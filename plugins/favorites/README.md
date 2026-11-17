# favorites

Adds a Favorites folder on the first screen, and a button to add or remove a favorite.

## Turn it on

Turn on **Favorites** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

On the game list, press **Y** to add or remove the selected game or [pak](../../docs/glossary.md#pak). Favorites show in the Favorites folder.

With Pin to top on, favorites also sort first in whichever folder they're in, except Recently Played, [collections](../../docs/glossary.md#collection) and Favorites itself.

## Screenshots

The Favorites folder on the first screen, with the default settings.

![The Favorites folder on the first screen, with the default settings](docs/default.png)

The Favorites folder.

![The Favorites folder](docs/folder.png)

Pin to top on: µCity, a favorite, comes first in Game Boy Color.

![Pin to top on: µCity, a favorite, comes first in Game Boy Color](docs/pin.png)

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Collection (`collection`) | text | Favorites | The collection in `Collections/` that holds favorites, and the folder's name on the root |
| Pin to top (`pin`) | on, off | off | Sorts favorites first in every folder but Recently Played, collections and Favorites |

## For plugin authors

- Provides the `favorites` entry set (`provides=sets.favorites`). It's announced as changed (`launcher.set_changed`) each time a favorite is added or removed.
- Provides the `favorites.setup` service (`provides=favorites.setup`), which [`first-run`](../first-run/README.md) sets the collection through. With no arguments it gives the Collection setting now as `collection=<name>` and, as its choices, that name and the other collections in `Collections/` by name, as `collection.<i>=<name>` and `collection.label.<i>=<name>` from 0, as many as fit, with `collection.count=<n>`, and `restart=1`, as a new collection is used from the next time the launcher starts. `collection=<name>` saves the setting first; a name that can't be a file in `Collections/` (empty, starting with `.`, or with a `/` or a control character) gives `error=collection` and saves nothing. Other keys are ignored.
- Provides the `favorites.move` service (`provides=favorites.move`), which points a favorite at a game's new path, such as after its files moved. Call it with `from=<full path>` and `to=<full path>`. The favorite keeps its place; when `to` is a favorite already, the one at `from` just goes. It replies `moved=1`, `moved=0` when `from` isn't a favorite, or `error=path` (a path off the card) or `error=save`. [`multi-disc`](../multi-disc/README.md) calls it.
- Other plugins read favorites through the entry set rather than the collection file: [`search`](../search/README.md), [`random-game`](../random-game/README.md), [`focus-mode`](../focus-mode/README.md), [`row-badges`](../row-badges/README.md), [`home-dashboard`](../home-dashboard/README.md) and [`screensaver`](../screensaver/README.md).
- Stores favorites in the collection file in `Collections/`.
- Its model code is in `pin_model.c`.

## Credits

Pin to top is from kUI and nx-redux.
