
# gridflux

`gridflux` is a cross-platform window management tool and virtual workspaces that provides flexible window arrangement and workspace management capabilities. Unlike traditional tiling window managers, `gridflux` allows you to manage application windows without imposing a strict tiling paradigm, offering a balance between manual control and automated window management.

The project supports Linux (X11 with KWin integration) and Windows, with varying levels of functionality and stability across platforms.

## Layout Visualizations

Here are various layout arrangements for windows. The configurations showcase how windows can be arranged in different layouts.

https://github.com/user-attachments/assets/f7800812-4ac9-4d3d-bac0-943632cb2f20



## Platform Support

### Linux
- **X11**: Full support with complete functionality
- **KWin Integration**: Enhanced workspace management through KWin D-Bus interface
- **Status**: Stable and fully tested

### Windows  
- **Support**: Basic window management functionality
- **Status**: Stable and fully tested

---

## Installation

### Windows Download

You can download the latest Windows installer (MSI) from the [GitHub Releases](https://github.com/arxngr/gridflux/releases) page.
- [Latest Windows Installer (x64)](https://github.com/arxngr/gridflux/releases/tag/0.1.0-beta/gridflux-x64.msi)

### Dependencies

#### Linux (Debian/Ubuntu)
```bash
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config
sudo apt-get install libx11-dev libjson-c-dev libdbus-1-dev
sudo apt-get install libgtk-4-dev  # Optional, for GUI component
```

#### Linux (Fedora/CentOS/RHEL)
```bash
sudo dnf install gcc cmake pkg-config
sudo dnf install libX11-devel json-c-devel dbus-devel
sudo dnf install gtk4-devel  # Optional, for GUI component
```

#### Windows
- **Visual Studio 2019/2022** with C development tools
- **CMake** (version 3.12 or later)
- **pkg-config** (for dependency management)
- **json-c library** (can be installed via vcpkg or built from source)
- **GTK4** (optional, for GUI component)

### Build Instructions

#### Linux

Using CMake (recommended):
```bash
git clone https://github.com/arxngr/gridflux.git
cd gridflux
mkdir build
cd build
cmake ..
make
```

Using the installation script:
```bash
git clone https://github.com/arxngr/gridflux.git
cd gridflux
chmod +x scripts/install.sh
./scripts/install.sh
```

#### Windows

Using MSYS2 (recommended):
```bash
# Install dependencies first
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-pkg-config mingw-w64-x86_64-json-c
pacman -S mingw-w64-x86_64-gtk4  # Optional, for GUI component

git clone https://github.com/arxngr/gridflux.git
cd gridflux
mkdir build
cd build
cmake ..
make
```

Using the installation script:
```cmd
git clone https://github.com/arxngr/gridflux.git
cd gridflux
scripts\install.bat
```


### Development Mode

To run from the source directory without installation:
```bash
cmake -DGF_DEV_MODE=ON ..
```
This will read configuration from `./config.json` instead of system paths.

---

## Usage

GridFlux provides multiple executables:

- **gridflux**: Main daemon/service
- **gridflux-cli**: Command-line interface for window management
- **gridflux-gui**: Graphical user interface (if GTK4 is available)

### Basic Commands

```bash
# Start the main service
./gridflux

# List all windows
./gridflux-cli query windows

# List windows in specific workspace
./gridflux-cli query windows 2

# List all workspaces
./gridflux-cli query workspaces

# Count windows in workspace
./gridflux-cli query count 2

# Move window to workspace
./gridflux-cli move 12345 2

# Lock workspace
./gridflux-cli lock 3

# Unlock workspace
./gridflux-cli unlock 3
```

### Configuration

**Linux**: `~/.config/gridflux/config.json`
**Windows**: `%APPDATA%\gridflux\config.json`

---

## Platform-Specific Notes

### Linux Stability
Linux support is mature and stable, with comprehensive X11 integration and growing Wayland support through KWin. All features are thoroughly tested and production-ready.

### Windows Considerations
The Windows implementation provides core functionality but may experience stability issues compared to the Linux version. Known limitations include:

- Some UWP applications ignore window positioning commands
- Virtual desktop management is limited due to undocumented APIs
- High DPI scaling may require additional configuration
- Certain system windows may be incorrectly classified

The Windows version is functional for basic window management but requires additional development for feature and stability parity with Linux.

### macOS Status
macOS support is not available in this version.

---

## Development

This project is open-source and contributions are welcome. The codebase is organized with platform-specific implementations in `src/platform/` for each supported operating system.

### Building for Development

```bash
# Enable development mode and debug output
cmake -DGF_DEV_MODE=ON -DGF_DEBUG=ON ..
make

# Generate compile commands for IDE support
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
```

---

## Acknowledgements

- Inspired by tiling window managers like bspwm and i3
- Thanks to the X11, Windows API, and for providing robust window management APIs
- GTK4 project for the cross-platform GUI framework

---

## Alternatives

- **Linux**: [bspwm](https://github.com/baskerville/bspwm), [i3](https://i3wm.org/)
- **Windows**: [FancyWM](https://github.com/FancyWM/fancywm), [PowerToys](https://github.com/microsoft/PowerToys)

---
