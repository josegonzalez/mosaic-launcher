# favorites

Adds a Favorites folder on the first screen, and a button to add or remove a favorite.

## Turn it on

Turn on **Favorites** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

On the game list, press **Y** to add or remove the selected game or [pak](../../docs/glossary.md#pak). Favorites show in the Favorites folder.

With Pin to top on, favorites also sort first in whichever folder they're in, except Recently Played, [collections](../../docs/glossary.md#collection) and Favorites itself.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Collection (`collection`) | text | Favorites | The collection in `Collections/` that holds favorites, and the folder's name on the root. Other plugins can read it |
| Pin to top (`pin`) | on, off | off | Sorts favorites first in every folder but Recently Played, collections and Favorites |

## For plugin authors

- Provides the `favorites` entry set (`provides=sets.favorites`). It's announced as changed (`launcher.set_changed`) each time a favorite is added or removed.
- `collection` is a `public` setting, so other plugins can read it with `cfg_read`.
- Stores favorites in the collection file in `Collections/`.
- Its model code is in `pin_model.c`.

## Credits

Pin to top is from kUI and nx-redux.
