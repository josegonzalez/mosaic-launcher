# Launcher plugin template

A starting point for a plugin for [nextui-launcher](https://github.com/josegonzalez/nextui-launcher), published as its own pak through the Pak Store.

## Make it yours

1. Copy this directory into a new repo.
2. Set `id` and `name` in `plugin.txt`, and the same `id` in `src/plugin.c`.
3. Update `pak.json`: `name` (how it shows in the Pak Store and in Tools), `description`, `author`, `repo_url`, and `release_filename`, which must be `<name from plugin.txt>.pak.zip`.
4. Write the plugin in `src/`. The API is in `sdk/nextui_plugin.h`, and [PLUGINS.md](https://github.com/josegonzalez/nextui-launcher/blob/main/PLUGINS.md) explains it.

## Build

```shell
make           # lib/aarch64/plugin.so, for every NextUI device (needs Docker or podman)
make pak       # build/<name>.pak.zip
make desktop   # a build for nextui-launcher's desktop build, for trying it out
```

## Publish

Push a tag matching `version` in `pak.json` (eg. `v0.1.0`). The workflow builds the plugin, attaches the zip to a GitHub release, and the repo is ready to submit to the [Pak Store](https://github.com/LoveRetro/nextui-pak-store).

Once installed, the plugin is on by default. Opening its pak from Tools shows the Launcher's Plugins page, where it can be turned off.

## License

`sdk/` is MIT licensed. Your plugin can use any license.
