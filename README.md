# System Status Widget

Live system monitor showing CPU usage, memory usage, disk usage, and network I/O rates. All values are read directly from Linux kernel interfaces.

## Requirements

- Linux
- Qt 6.2+ (Widgets)
- CMake 3.21+
- C++20 compiler
- [`widget-sdk`](https://github.com/duh-dashboard/widget-sdk) installed

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=~/.local
cmake --build build
cmake --install build --prefix ~/.local
```

The plugin installs to `~/.local/lib/dashboard/plugins/`.

## Notes

Reads the following Linux-specific interfaces:

| Metric | Source |
|---|---|
| CPU usage | `/proc/stat` (delta sampling) |
| Memory usage | `/proc/meminfo` |
| Disk usage | `QStorageInfo` |
| Network I/O | `/proc/net/dev` (delta sampling) |

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).
