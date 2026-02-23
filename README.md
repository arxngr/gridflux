<div align="center">

<img width="96" height="96" alt="gridflux logo" src="https://github.com/user-attachments/assets/eedf2502-573f-4ff8-a5e4-d85ec2773177" />

# gridflux

**Automatic window organisation without the tiling religion.**

GridFlux sits between a tiling WM and a floating WM — it arranges your windows into virtual workspaces automatically, then stays out of your way.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey)](#platform-support)
[![Release](https://img.shields.io/github/v/release/arxngr/gridflux)](https://github.com/arxngr/gridflux/releases)

[**Getting Started**](#installation) · [**Configuration**](#configuration) · [**CLI Reference**](#cli-reference) · [**Contributing**](#contributing)

</div>

---

## The problem

Most tiling window managers are powerful but demanding — you spend more time configuring layouts than getting work done. Most floating WMs give you no automation at all. GridFlux fills the gap: windows are automatically sorted into virtual workspaces and arranged into a sensible grid, but you keep full manual control when you need it.

No config language to learn. No layout rules to memorize. Just open apps and get to work.

---

## See it in action

https://github.com/user-attachments/assets/f7800812-4ac9-4d3d-bac0-943632cb2f20

*Windows being automatically arranged across workspaces as applications open and close.*

---

## What it does

- **Auto workspace assignment** — new windows land in the right workspace automatically, no keyboard shortcuts required
- **Grid layout engine** — windows tile cleanly without overlapping, with configurable padding
- **Virtual workspaces** — workspaces are created and destroyed on demand as your window count changes
- **Workspace locking** — pin a workspace so no windows can be moved in or out
- **3-finger swipe gestures** — cycle through maximised windows on Linux without touching the keyboard
- **Window border highlights** — coloured borders make the focused window obvious at a glance
- **Hot-reload config** — change settings in `config.json` and they apply immediately, no restart
- **Three interfaces** — daemon, CLI, and GUI — use whichever fits your workflow

---

## Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux (X11) | ✅ Stable | Full feature support, production ready |
| Windows 10/11 | ✅ Stable | Core features work, some UWP limitations |
| macOS | 🚧 Planned | Not available yet |

---

## Installation

### Linux

```bash
git clone https://github.com/arxngr/gridflux.git
cd gridflux
cmake -B build && cmake --build build
```

**Dependencies (Debian/Ubuntu):**
```bash
sudo apt-get install build-essential cmake pkg-config \
    libx11-dev libjson-c-dev libdbus-1-dev
# Optional — for the GUI
sudo apt-get install libgtk-4-dev
```

**Dependencies (Fedora):**
```bash
sudo dnf install gcc cmake pkg-config \
    libX11-devel json-c-devel dbus-devel
# Optional — for the GUI
sudo dnf install gtk4-devel
```

Or use the install script:
```bash
chmod +x scripts/install.sh && ./scripts/install.sh
```

### Windows

Download the [latest MSI installer](https://github.com/arxngr/gridflux/releases/tag/0.1.0-beta/gridflux-x64.msi) from the releases page, or build from source with MSYS2:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-pkg-config mingw-w64-x86_64-json-c

git clone https://github.com/arxngr/gridflux.git
cd gridflux
cmake -B build && cmake --build build
```

---

## Usage

GridFlux ships three binaries:

| Binary | Purpose |
|--------|---------|
| `gridflux` | Main daemon — run this first |
| `gridflux-cli` | Command-line control |
| `gridflux-gui` | Graphical interface (requires GTK4) |

Start the daemon, then use the CLI or GUI to interact with it:

```bash
./gridflux &
./gridflux-gui   # or use the CLI below
```

---

## CLI reference

```bash
# Workspaces
gridflux-cli query workspaces       # list all workspaces and their state
gridflux-cli lock 2                 # lock workspace 2 (no windows in or out)
gridflux-cli unlock 2               # unlock workspace 2

# Windows
gridflux-cli query windows          # list all tracked windows
gridflux-cli query windows 2        # list windows in workspace 2
gridflux-cli query count 2          # count windows in workspace 2
gridflux-cli move 0x1a2b3c 2        # move window by ID to workspace 2
```

---

## Configuration

**Linux:** `~/.config/gridflux/config.json`
**Windows:** `%APPDATA%\gridflux\config.json`

```json
{
  "max_windows_per_workspace": 4,
  "max_workspaces": 10,
  "default_padding": 10,
  "min_window_size": 100,
  "border_color": 16031786,
  "enable_borders": true,
  "locked_workspaces": []
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `max_windows_per_workspace` | `4` | How many windows fit in one workspace before overflow |
| `max_workspaces` | `10` | Maximum number of virtual workspaces |
| `default_padding` | `10` | Gap between windows in pixels |
| `min_window_size` | `100` | Minimum window dimension in pixels |
| `border_color` | orange | Active window border colour (RGB integer) |
| `enable_borders` | `true` | Show coloured borders on managed windows |
| `locked_workspaces` | `[]` | List of workspace IDs to lock on startup |

Changes to this file are picked up immediately — no restart needed.

---

## Development

```bash
# Build with dev mode (reads config.json from current directory)
cmake -B build -DGF_DEV_MODE=ON && cmake --build build

# Build with debug output
cmake -B build -DGF_DEV_MODE=ON -DGF_DEBUG=ON && cmake --build build

# Generate compile_commands.json for IDE/LSP support
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

---

## Contributing

Contributions are welcome. For bug fixes, open a PR directly. For new features, open an issue first so we can discuss the approach.

Areas that would benefit most from help right now:

- Wayland support (wlroots / wl-roots protocol)
- Test coverage for the layout engine

---

## Acknowledgements

Inspired by [bspwm](https://github.com/baskerville/bspwm) and [i3](https://i3wm.org/). Built on X11, Win32, and GTK4.

---

<div align="center">

If GridFlux saves you time, consider leaving a ⭐ — it helps others find the project.

</div>
