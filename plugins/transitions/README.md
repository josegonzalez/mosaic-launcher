# transitions

Picks how folders open and close and how the game switcher comes in.

The launcher's Menu transitions setting must be on for any of this to run. With it off there are no menu transitions, whatever the style.

## Turn it on

Turn on **Transition styles** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)). It owns the `transition` [slot](../../docs/glossary.md#slot). Launches stay stock's, as the `launch` slot isn't this plugin's (see [`launch-animation`](../launch-animation/README.md)).

## Styles

| Style | What it does |
|---|---|
| `slide` (the default) | A push slides the old list out to the left as the new one comes in from the right. A pop slides them the other way. The game switcher slides down from the top over the game list |
| `fade` | The new screen fades in over the old one |
| `slide_fade` | The new screen fades in as it moves a quarter of the way into place, from the right for a push or the top for the game switcher. For a pop the old list fades out as it moves a quarter of the way to the right |
| `stock` | Stock's transitions |
| `off` | None |

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Style (`style`) | stock, slide, fade, slide_fade, off | slide | How folders push and pop and the game switcher comes in |
| Speed (`speed`) | fast, normal, slow | normal | `fast` is 150 ms, `normal` 250 ms and `slow` 500 ms |
| Ease (`ease`) | linear, in, out, in_out | out | How the motion eases. `out` is as stock's slides |

## For plugin authors

- Owns the `transition` slot (`slots=transition`), using the `transition_*` hooks.

## Credits

radther and Nocturne.
