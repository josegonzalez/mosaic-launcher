# led-status

Colours the device's LEDs by the battery level or by a theme colour, while the launcher is on screen.

It needs LEDs, so on the desktop build, or with a launcher without LED colours, it does nothing (on desktop it still logs what the LEDs would show each time that changes). Its name on the Plugins page says "(unsupported on desktop)".

## Turn it on

Turn on **LED status** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## Modes

The LED colour follows setting picks the mode.

| Mode | What the LEDs show |
|---|---|
| `auto` (the default) | The theme's `led-status.colour` key: a colour as `0xRRGGBB` or `#RRGGBB`, the name of a theme colour as the Theme colour setting takes it, or `battery`. Without a key, or with one it doesn't take, the battery as in `battery` mode. The key is read again when the theme changes, such as with [`theme-plus`](../theme-plus/README.md)'s picker, and the LEDs change at once |
| `battery` | Every LED red at or below the red level (20% by default), yellow at or below the yellow level (50% by default), and green above. The battery is read every 5 seconds. With the red level at or above the yellow one there's no yellow |
| `theme` | The stock theme colour the Theme colour setting names |

The `battery` and `theme` modes are your own and ignore the theme's key.

A reading of 0 or outside 0 to 100 keeps the colour it had. A colour is left for a higher one only once the battery is 2 points past its level, so readings around a level don't flicker.

Each LED keeps the brightness your LED settings give it. Stock's charging, critical and low battery, mute and sleep lights still take priority, and the plugin's colour comes back after them. A game sets the LEDs its own way, and turning the plugin off gives your own LED settings back. When another plugin's LED colour is on, it logs that once and tries again only when its colour changes.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| LED colour follows (`mode`) | auto, battery, theme | auto | `auto`: the theme's LED colour (its `led-status.colour`), or the battery without one. `battery`: red, yellow or green by the battery level. `theme`: one of stock's theme colours |
| Red at or below, in % (`low_at`) | 10, 15, 20, 25, 30 | 20 | The battery level at or below which the LEDs are red |
| Yellow at or below, in % (`mid_at`) | 30, 40, 50, 60, 70 | 50 | The battery level at or below which the LEDs are yellow |
| Theme colour (`theme_color`) | main, accent, accent2, list_text, list_text_selected, hint, background | accent | The stock theme colour used in `theme` mode |

## For plugin authors

- Reads the `led-status.colour` theme key with `theme_value` (see [plugin-api.md](../../docs/plugin-api.md)).
- Uses the host's LED colour call. On a platform without LEDs it loads but logs that it's unsupported and sets nothing.

## Credits

ProClifo and DucLQ92.
