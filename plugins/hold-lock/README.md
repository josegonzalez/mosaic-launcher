# hold-lock

Locks every button, so a device in your pocket or in a kid's hands doesn't change anything by accident.

## What it does

It locks every button the launcher sees, on the game list, the game switcher, the quick menu and the dialogs. A padlock shows in the header beside the status pill while it's locked, in the theme's accent with its hint colour.

What it doesn't lock:

- A long power press still powers off, but a short one doesn't sleep.
- The volume keys, and the brightness with MENU held, are changed by stock's keymon outside the launcher.
- The Launcher pak's settings screen and games.

The lock isn't kept when the launcher restarts.

## Turn it on

Turn on **Hold lock** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

Hold the lock chord for the hold time (SELECT and START for 1 second by default) to lock. Hold them again to unlock. Buttons still held from the unlock are taken until they're let go, so the chord's own releases don't reach the screen.

With Lock with the hold switch on, the hold switch on the side of the tg5040 and tg5050 locks while it's on, and the chord can't unlock it until it's switched off. It's stock's mute switch, so it still mutes as stock's Mute settings say. On the h700 and desktop, which have no switch, the plugin logs that once and only the chord locks.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Lock chord (`chord`) | select+start, l1+r1, l2+r2, off | select+start | Held for the hold time, locks every button, and again unlocks them. `off` for none |
| Hold the chord for, in seconds (`hold`) | 1, 2, 3 | 1 | How long to hold the chord |
| Lock with the hold switch (`switch`) | on, off | off | The switch on the side of the tg5040 and tg5050 locks while it's on. Unsupported on the h700 and desktop, which have none |

## For plugin authors

- Takes the buttons while locked, and draws the padlock in the header.
- Has no slot, service or file of its own.

## Credits

westo27 and Bramley.
