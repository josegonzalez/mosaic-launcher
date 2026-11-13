# bt-reconnect

Reconnects your last Bluetooth device when the launcher starts, and after the device wakes from sleep.

Stock doesn't record which device was last connected, so while the launcher is on screen this plugin saves the connected device's address whenever it changes.

It needs Bluetooth, so on the desktop build, or with a launcher without the Bluetooth calls, it loads but logs that it's unsupported and does nothing. Its name on the Plugins page says "(unsupported on desktop)".

## Turn it on

Turn on **Bluetooth reconnect** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How it works

At start (which includes coming back from a game) and after the device wakes, with Bluetooth on and a saved device:

1. It waits 8 seconds for the launcher's first look at what's connected.
2. If nothing is connected by then, it asks the launcher to connect the saved device. The launcher stops that after 15 seconds.
3. A try that hasn't connected 20 seconds after asking failed. It tries up to 3 times, waiting 10 seconds before the second try and 20 before the third, so it gives up about 100 seconds in.

A device connecting (any device) ends it, as does Bluetooth turning off, or sleeping in the middle of it. It never tries while Bluetooth is off, and it doesn't turn Bluetooth on.

To forget the device, clear the Device to reconnect setting; the next device that connects is saved. You can also type an address there.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Reconnect at start (`on_start`) | on, off | on | Reconnects when the launcher starts |
| Reconnect after sleep (`on_resume`) | on, off | on | Reconnects after the device wakes |
| Device to reconnect (`last`) | text | empty | The last connected device, saved whenever it changes. Clear it to forget the device |

With both toggles off it does nothing, not even saving the device.

## For plugin authors

- The launcher runs the connect, as `bluetoothctl connect`; the plugin never runs a program.
- Reads and writes its own `last` setting.

## Credits

dalexanco.
