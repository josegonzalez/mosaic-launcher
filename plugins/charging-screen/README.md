# charging-screen

Shows a full screen with the battery level while the device charges and you aren't using it. It says whether the device is charging or charged, drawn dim on black.

It needs a battery, so on the desktop build it loads but logs that it's unsupported and does nothing. Its name on the Plugins page says "(unsupported on desktop)".

## Turn it on

Turn on **Charging screen** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How it behaves

- It shows while the device charges and the game list or the home screen has been left alone for the idle time.
- It opens only over the game list or the home [slot](../../docs/glossary.md#slot)'s screen, not over the quick menu, the game switcher or other plugins' screens, which are what you were looking at.
- It's drawn again only when the level, the state or the clock's minute changes.
- Any button closes it, back to where it opened. That press is taken until it's let go, so it doesn't also act on the screen under it.
- The power button still sleeps the device, and waking closes the screen.
- It closes by itself when the charger comes out.
- It never holds off sleep: stock's launcher never turns the screen off or sleeps on its own while charging, with or without this screen, and its screen timeout counts from when the charger comes out.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Show after idle on a charger, in seconds (`idle`) | off, 30, 60, 120, 300 | 60 | How long the game list or home is left alone on a charger before the screen shows |
| Clock (`clock`) | off, 24h, 12h | 24h | Shows the time above the battery |

## For plugin authors

- `idle` is a `public` setting, so other plugins can read it with `cfg_read`.
- On a platform without a battery it loads but does nothing.

## Credits

MinUI-Zero.
