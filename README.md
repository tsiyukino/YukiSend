# YukiSend

A LAN file transfer and chat application for Windows (macOS and Linux planned). Peers on the same network discover each other automatically — no accounts, no cloud, no internet dependency.

## Features

- **Peer discovery** — zero-config via UDP broadcast; peers appear and disappear automatically
- **File and folder transfer** — accept/deny flow with transfer progress
- **Image sharing** — paste from clipboard, see inline previews; click to view fullscreen
- **Text chat** — persistent history per peer, full text selection and copy
- **Per-peer settings** — save, favorite, or block peers; choose session-only or persistent storage per peer
- **System tray** — minimize to tray, desktop notifications for incoming messages

## Building

### Requirements

- Qt 6.5 or later (Core, Concurrent, Gui, Widgets, Network, Sql, Svg)
- CMake 3.21 or later
- C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+)

### Steps

```sh
cmake -S . -B build -G Ninja
cmake --build build --config Release
```

The resulting executable is at `build/src/YukiSend.exe` (Windows) or `build/src/YukiSend` (Linux/macOS).

### Running tests

```sh
ctest --test-dir build --output-on-failure
```

## Architecture

All UI is drawn directly via QPainter — no Qt stylesheets, no web views. Color, spacing, and font constants live in `src/theme/Theme.h`.

For a detailed breakdown of the network protocol, layer structure, and key design decisions, see [`docs/explanation/architecture.md`](docs/explanation/architecture.md) and the [`docs/decisions/`](docs/decisions/) directory.

```
src/
  app/      — application controller, ties network and UI together
  network/  — UDP discovery, TCP chat channel, per-file TCP transfer
  model/    — message store, peer store, settings
  ui/       — all custom-painted QWidget subclasses
  theme/    — constexpr color/spacing/font constants
  utils/    — file enumeration, image encoding helpers
tests/
docs/
  explanation/  — architecture overview
  decisions/    — dated records of non-obvious design choices
```

## Platform status

| Platform | Status |
|---|---|
| Windows 10/11 | Supported |
| macOS | Planned |
| Linux | Planned |

## License

MIT
