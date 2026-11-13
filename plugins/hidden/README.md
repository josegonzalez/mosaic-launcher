# hidden

Hides the games you pick from every list.

## Turn it on

Turn on **Hidden games** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

- On the game list, press **Y** (the Hide button setting picks another) to hide the selected game or [pak](../../docs/glossary.md#pak).
- A Hidden folder on the first screen, shown while something is hidden, lists them. In it the same button unhides the selected one.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Hide button (`button`) | Y, L2, R2, SELECT | Y | The game list button that hides the selected game, and unhides it in the Hidden folder |

## For plugin authors

- Provides the `hidden` entry set (`provides=sets.hidden`), which also has whatever is in a hidden folder. It's announced as changed (`launcher.set_changed`) each time something is hidden or unhidden.
- Registers the actions `hidden.hide` and `hidden.unhide`, which the `button` setting follows.
- The list is `.userdata/<platform>/launcher/hidden.txt`, one full path per line.

## Credits

MyMinUI.
