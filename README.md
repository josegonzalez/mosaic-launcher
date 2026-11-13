# mosaic-launcher

A replacement for the NextUI launcher with a plugin API, so features that today live in separate MinUI and NextUI forks can be combined as plugins.

This is an experiment. It isn't released or supported, and isn't meant for everyday use.

The bundled plugins are listed in [docs/plugins.md](docs/plugins.md), with a [glossary](docs/glossary.md) for terms new to custom firmware. Development notes are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md), and the plugin API is in [docs/plugin-api.md](docs/plugin-api.md).

## License

The launcher is derived from NextUI's `nextui.c`, so this project is under the [PolyForm Noncommercial License 1.0.0](LICENSE), the same as NextUI. The plugin SDK in `sdk/` is written from scratch and [MIT licensed](sdk/LICENSE), so plugins can choose their own license.
