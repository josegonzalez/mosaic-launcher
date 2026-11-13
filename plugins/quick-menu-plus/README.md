# quick-menu-plus

Changes the [quick menu](../../docs/glossary.md#quick-menu): renames or hides its tiles, and adds tiles that flip stock's settings.

## Turn it on

Turn on **Quick menu plus** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## What it does

- **Rename tiles:** the Tile labels setting renames the top row's built-in tiles with `name=label` pairs, such as `Recents=Played, Games=Library`.
- **Hide tiles:** the Hidden tiles setting hides tiles by name, such as `Pak Store, Bluetooth`, or a plugin tile by its action id, such as `random-game.tile`.
- **Toggle tiles:** the Toggle tiles setting picks which of stock NextUI's on/off settings get a tile that flips them, as stock's Settings app does. The choices are `save`, `load`, `screenshot` and `adjustments` (the in-game notifications, the default), `clock`, `battery` (the battery percentage) and `haptics`.
- **Stay awake tile:** holds off auto-sleep while it's on, until the device reboots or powers off.
- **Reboot to stock tile:** asks first, then turns the launcher off with `manage.sh turn-off` and reboots, so the device comes back on stock NextUI's launcher. Turn Launcher back on from Tools.

All of these tiles go in the top row.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Tile labels (`labels`) | text | empty | New labels for the top row's built-in tiles, as `name=label` pairs, such as `Recents=Played, Games=Library` |
| Hidden tiles (`hide`) | text | empty | Tiles to hide, by name or a plugin tile's action id, such as `Pak Store, Bluetooth` |
| Toggle tiles (`toggles`) | text | `save, load, screenshot, adjustments` | Stock settings with a toggle tile, from `save`, `load`, `screenshot`, `adjustments`, `clock`, `battery` and `haptics` |
| Stay awake tile on the quick menu (`awake_tile`) | on, off | on | A Stay awake tile that holds off auto-sleep while it's on |
| Reboot to stock tile on the quick menu (`stock_tile`) | on, off | on | A Reboot to stock tile that turns the launcher off and reboots into stock NextUI |

## For plugin authors

- Hides tiles by name or by a plugin tile's action id, and registers its own tiles.
- Calls `manage.sh turn-off` for Reboot to stock.

## Credits

Tile labels and hidden tiles are from Xisrith, quick toggles from nx-redux, and Reboot to stock from hp7hao.
