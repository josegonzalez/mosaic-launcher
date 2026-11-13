# netplay

Host or join a game over the network, once NextUI has netplay.

Stock's minarch (the [emulator front end](../../docs/glossary.md#minarch)) has no netplay yet, so this plugin stays off for now. It's loaded only on a NextUI release that adds it. Its name on the Plugins page says "(needs NextUI netplay)".

## Turn it on

Nothing to turn on yet: its `plugin.txt` has `requires=minarch-netplay`, so the loader leaves it off and logs why, and it never binds **Y**. Once a NextUI release can load it, turn on **Netplay** in Launcher > Plugins ([how](../../docs/plugins.md#turning-plugins-on-and-off)).

## How to use

Once it can load, press **Y** on a game. A menu opens with Host a game or Join a game.

- **Join a game** asks for the host's name or IPv4 address on the keyboard, with `:port` for a port other than the Port setting. It offers the last address next time.
- A game without an emulator [pak](../../docs/glossary.md#pak) is cancelled rather than launched without netplay.
- Only launches picked with Host or Join are replaced.

## Settings

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Port (`port`) | text | 55435 | The port to host on, and to join on when the address doesn't give one |

## For plugin authors

- Requires `minarch-netplay` (`requires=minarch-netplay`).
- The game launches through its emulator pak's `launch.sh` with `--host --port <port>` or `--connect <address> --port <port>` before the game's path, each part single-quoted. Those arguments, and a `launch.sh` that passes what comes before the game to minarch, are assumptions to confirm once minarch has netplay.
- Uses `replace_launch`. With another plugin that replaces launches, such as `emulator-override`, only the first of them in load order replaces a launch. When that's the other plugin, netplay's `replace_launch` is refused, so netplay cancels the launch and says so rather than letting it start without netplay. When it's `netplay`, the netplay launch uses the system's own pak and the other's `replace_launch` is refused.
- Registers an action on **Y** and remembers the last address.

## Credits

nx-redux.
