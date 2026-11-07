# Launcher plugin template

A starting point for a plugin for [nextui-launcher](https://github.com/josegonzalez/nextui-launcher), packaged as its own pak.

## Make it yours

1. Copy this directory into a new repo.
2. Set `id` and `name` in `plugin.txt`, and the same `id` in `src/plugin.c`.
3. Write the plugin in `src/`. The API is in `sdk/nextui_plugin.h`, and [PLUGINS.md](https://github.com/josegonzalez/nextui-launcher/blob/main/PLUGINS.md) explains it.

## Build

```shell
make           # lib/aarch64/plugin.so, for every NextUI device (needs Docker or podman)
make pak       # build/<name>.pak.zip
make desktop   # a build for nextui-launcher's desktop build, for trying it out
```

## Install

Unzip `build/<name>.pak.zip` into `Tools/<platform>/<name>.pak/` on the SD card. The CI workflow builds the same zip on every push. Once installed, the plugin is on by default. Opening its pak from Tools shows the Launcher's Plugins page, where it can be turned off.

## License

`sdk/` is MIT licensed. Your plugin can use any license.
