<div align="center">

<img width="96" height="96" alt="gridflux logo" src="https://github.com/user-attachments/assets/eedf2502-573f-4ff8-a5e4-d85ec2773177" />

# GridFlux

**Automatic window organisation without the tiling religion.**

GridFlux sits between a tiling window manager and a floating one. It arranges your
windows into virtual workspaces automatically, then stays out of your way.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey)](#platform-support)
[![Release](https://img.shields.io/github/v/release/arxngr/gridflux)](https://github.com/arxngr/gridflux/releases)

[Getting Started](#installation) · [The Control Panel](#the-control-panel) · [Configuration](#configuration) · [CLI Reference](#cli-reference) · [Contributing](#contributing)

</div>

---

## Why GridFlux

Most tiling window managers are powerful but demanding — you can spend more time
configuring layouts than getting work done. Most floating managers give you no
automation at all. GridFlux fills the gap: windows are sorted into virtual
workspaces and arranged into a clean grid automatically, while you keep full
manual control when you want it.

There is no layout language to learn and no rules to memorise. Open your apps and
get to work; move things around by hand whenever you need to.

---

## Features

**Window management**

- **Automatic workspace assignment** — new windows land in the right workspace on
  their own, no keyboard shortcut required.
- **Grid layout engine** — windows tile cleanly without overlapping, with
  configurable padding.
- **Virtual workspaces** — workspaces are created and removed on demand as your
  window count changes.
- **Move windows anywhere** — send a window to another workspace from the CLI, by
  dragging it in the control panel, or automatically through a rule.

**Control and customisation**

- **Workspace rules** — pin an application to a specific workspace, so it always
  opens where you expect.
- **Workspace locking** — lock a workspace and no windows can move in or out.
- **Workspace switching** — jump between workspaces with `Ctrl + Win + Left/Right`.
- **Window borders** — an optional coloured border marks managed windows; the
  colour is configurable.
- **Live configuration** — edit `config.json` and changes apply immediately, with
  no restart.

**Three ways to drive it**

- A background **daemon** that does the arranging.
- A **command-line tool** for scripting and quick actions.
- A **graphical control panel** for a visual overview and drag-and-drop.

---

## The control panel

The GUI (`gridflux-gui`, built on GTK4) is a single, compact window for watching
and steering GridFlux without touching the command line.

- **Server toggle** — start or stop the daemon and see its state (Running /
  Stopped) at a glance.
- **Workspace cards** — every workspace is a card showing its number, how many
  windows it holds and how many slots are free, the applications inside it (by
  friendly name and icon), and a small tiling preview of the current layout.
- **Drag to move** — drag an application from one workspace card onto another to
  move its window there.
- **Inline lock** — lock or unlock a workspace directly on its card.
- **Rules** — add a rule by picking an application and a target workspace, and
  review the current rules grouped by workspace, each removable in one click.
- **Settings** — adjust the maximum windows per workspace, the workspace limit,
  and the border colour, and toggle borders on or off.

Workspaces pinned by a rule are protected: they stay locked, can't be unlocked
from the panel, and their windows aren't dragged out — the rule keeps them in
place.

---

## Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux (X11) | Stable | Full feature support, production ready |
| Windows 10 / 11 | Stable | Core features supported; some UWP apps are limited |
| Linux (Wayland) | Not supported | Wayland compositors manage windows themselves |
| macOS | Not supported | Not available yet |

---

## Installation

### Linux

```bash
git clone https://github.com/arxngr/gridflux.git
cd gridflux
cmake -B build && cmake --build build
```

Dependencies (Debian / Ubuntu):

```bash
sudo apt-get install build-essential cmake pkg-config \
    libx11-dev libjson-c-dev libdbus-1-dev
# Optional, for the control panel:
sudo apt-get install libgtk-4-dev
```

Dependencies (Fedora):

```bash
sudo dnf install gcc cmake pkg-config \
    libX11-devel json-c-devel dbus-devel
# Optional, for the control panel:
sudo dnf install gtk4-devel
```

Or run the install script:

```bash
chmod +x scripts/install.sh && ./scripts/install.sh
```

### Windows

Download the latest MSI installer from the
[releases page](https://github.com/arxngr/gridflux/releases/latest), or build from
source with MSYS2 (UCRT64):

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-pkg-config mingw-w64-ucrt-x86_64-json-c \
          mingw-w64-ucrt-x86_64-gtk4

git clone https://github.com/arxngr/gridflux.git
cd gridflux
cmake -B build && cmake --build build
```

---

## Usage

GridFlux ships three binaries:

| Binary | Purpose |
|--------|---------|
| `gridflux` | The daemon that arranges windows. Run this first. |
| `gridflux-cli` | Command-line control. |
| `gridflux-gui` | The graphical control panel (requires GTK4). |

Start the daemon, then open the control panel or use the CLI:

```bash
./gridflux &
./gridflux-gui        # or drive it from the CLI below
```

In the control panel you can also start and stop the daemon with the server
toggle, so launching `gridflux-gui` alone is enough to get going.

---

## Keybindings

| Shortcut | Action |
|----------|--------|
| `Ctrl + Win + Left` | Switch to the previous workspace |
| `Ctrl + Win + Right` | Switch to the next workspace |

---

## CLI reference

```bash
# Workspaces
gridflux-cli query workspaces       # list workspaces and their state
gridflux-cli lock 2                 # lock workspace 2 (no windows in or out)
gridflux-cli unlock 2               # unlock workspace 2
gridflux-cli swipe left             # switch to the workspace on the left
gridflux-cli swipe right            # switch to the workspace on the right

# Windows
gridflux-cli query windows          # list all tracked windows
gridflux-cli query windows 2        # list windows in workspace 2
gridflux-cli move 0x1a2b3c 2        # move a window (by ID) to workspace 2

# Rules
gridflux-cli rule add firefox 1     # pin an application to a workspace
gridflux-cli rule remove firefox    # remove a rule
```

---

## Configuration

- **Linux:** `~/.config/gridflux/config.json`
- **Windows:** `%APPDATA%\gridflux\config.json`

```json
{
  "max_windows_per_workspace": 4,
  "max_workspaces": 10,
  "default_padding": 10,
  "min_window_size": 100,
  "border_color": 16031786,
  "enable_borders": true,
  "locked_workspaces": [],
  "window_rules": [
    { "wm_class": "Steam", "workspace_id": 4 },
    { "wm_class": "Spotify", "workspace_id": 5 }
  ]
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `max_windows_per_workspace` | `4` | How many windows fit in a workspace before a new one is used |
| `max_workspaces` | `10` | Maximum number of virtual workspaces |
| `default_padding` | `10` | Gap between windows, in pixels |
| `min_window_size` | `100` | Minimum window dimension, in pixels |
| `border_color` | orange | Managed-window border colour (RGB integer) |
| `enable_borders` | `true` | Draw coloured borders on managed windows |
| `locked_workspaces` | `[]` | Workspace IDs to lock on startup |
| `window_rules` | `[]` | Rules of the form `{ "wm_class": "...", "workspace_id": N }` |

The daemon watches this file and applies changes immediately — no restart needed.
The same options are available from the control panel's Settings and Rules
dialogs.

---

## Development

```bash
# Dev mode — reads config.json from the current directory
cmake -B build -DGF_DEV_MODE=ON && cmake --build build

# With debug logging
cmake -B build -DGF_DEV_MODE=ON -DGF_DEBUG=ON && cmake --build build

# Export compile_commands.json for editor / LSP support
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Source code is formatted with clang-format (version 18, GNU style with Allman
braces); the configuration lives in `.clang-format` and is checked in CI.

---

## Contributing

Contributions are welcome. For bug fixes, open a pull request directly. For new
features, open an issue first so we can discuss the approach.

The areas that would benefit most from help right now:

- Wayland support (wlroots protocol)
- Test coverage for the layout engine

---

## Acknowledgements

Inspired by [bspwm](https://github.com/baskerville/bspwm) and
[i3](https://i3wm.org/). Built on X11, Win32, and GTK4.

---

<div align="center">

If GridFlux saves you time, a star helps others find the project.

</div>
