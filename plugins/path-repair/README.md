# path-repair

Keeps recents and [collections](../../docs/glossary.md#collection) pointing at games after you rename them.

## What it does

When an entry's file is gone, it looks in the same folder for the one file whose name matches with accents, case, spaces and punctuation left out, such as "Pokémon Red.gb" renamed to "Pokemon Red.gb", and points the entry at it. A renamed folder below the system folder, such as a multi-disc game's, is matched the same way.

When nothing matches, or more than one file does, the entry is left alone. Only the folders a broken entry is in are listed.

Collections, favorites included, are repaired at start, and recents as the launcher loads them.

## Turn it on

Turn on **Path repair** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Repair recents (`recents`) | on, off | on | Points a recent at its game again after a rename, as the launcher loads recents |
| Repair collections (`collections`) | on, off | on | Points entries in every collection, favorites too, at their games again after a rename, at start |

## For plugin authors

- Has no slot or service of its own. It fixes entries as the launcher loads recents and at start.
- Has no slot or service of its own.

## Credits

UnuOS.
