# simple-mode

A simple mode for kids or a shared device: it hides Tools and the settings, so nothing gets changed by accident.

## What it does

While it's on:

- The Tools folder is gone from every list (the first screen, Recently Played and collections).
- Tools [paks](../../docs/glossary.md#pak) don't launch from the game switcher or an [FN button](../../docs/glossary.md#fn-button).
- The [quick menu](../../docs/glossary.md#quick-menu) drops its Tools and Pak Store tiles, plus Settings, Wifi, Bluetooth and Reboot with Hide settings tiles on (the default).

It's separate from stock NextUI's own simple mode file, which it never touches.

## Turn it on

Turn on **Simple mode** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## Leaving simple mode

A Simple mode tile on the quick menu turns it on, and off again after a confirm, or after typing the launcher's PIN when one is set (the PIN row on the Plugins page; see [the PIN](../../docs/plugin-api.md#the-pin)). The launcher keeps the PIN and checks it; the plugin never sees it.

A forgotten PIN is cleared by deleting `.userdata/<platform>/launcher/pin.txt` on a computer. Simple mode can also be left by setting `on=off` in `simple-mode.cfg` or `simple-mode=off` in `plugins.txt`.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Simple mode on (`on`) | on, off | on | Hides Tools and its tiles. The Simple mode tile on the quick menu turns it off, after the launcher's PIN when one is set |
| Hide settings tiles (`settings`) | on, off | on | Also hides Settings, Wifi, Bluetooth and Reboot |

## For plugin authors

- Hides quick menu tiles with `hide_quick_tile`, and asks the launcher to check the PIN with `ask_pin`; the plugin never sees the PIN.
- Writes `on` to its own `simple-mode.cfg`.

## Credits

Xisrith, MinUI-Zero, nx-redux (PIN), kUI and Nocturne.
