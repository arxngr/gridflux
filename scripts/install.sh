#!/usr/bin/env bash
set -e
INSTALL_DIR="/usr/local/bin"
BUILD_DIR="build"
KWIN_SCRIPT_NAME="gridflux-tiler"
KWIN_QML_FILE="main.qml"
KWIN_INSTALL_DIR="/usr/share/gridflux"
SERVICE_FILE="$HOME/.config/systemd/user/gridflux.service"

GNOME_EXT_UUID="gridflux@gridflux.dev"
GNOME_EXT_DIR="$HOME/.local/share/gnome-shell/extensions/$GNOME_EXT_UUID"

echo "=== GridFlux Installation Started ==="

detect_desktop() {
    DESKTOP="${XDG_CURRENT_DESKTOP:-Unknown}"
    echo "Detected DE: $DESKTOP"
    [[ "$DESKTOP" == *"KDE"* || "$KDE_FULL_SESSION" == "true" ]] && IS_KDE=1 || IS_KDE=0
    [[ "$DESKTOP" == *"GNOME"* ]] && IS_GNOME=1 || IS_GNOME=0
}

remove_existing_service() {
    if [ -f "$SERVICE_FILE" ]; then
        echo "Existing systemd service found, removing..."
        systemctl --user stop gridflux.service 2>/dev/null || true
        systemctl --user disable gridflux.service 2>/dev/null || true
        rm -f "$SERVICE_FILE"
        systemctl --user daemon-reload
        echo "✓ Existing service removed"
    fi
}

install_dependencies() {
    echo "Installing dependencies..."
    . /etc/os-release
    case "$ID" in
    ubuntu | debian)
        echo "Installing dependencies for Ubuntu/Debian..."
        sudo apt install -y libx11-dev libjson-c-dev libdbus-1-dev cmake gcc make pkg-config
        echo "Installing GUI dependencies..."
        sudo apt install -y libgtk-4-dev libglib2.0-dev
        ;;
    fedora | rhel | centos | almalinux | rocky)
        echo "Installing dependencies for Fedora/RHEL..."
        sudo dnf install -y libX11-devel json-c-devel dbus-devel cmake gcc make pkgconfig
        echo "Installing GUI dependencies..."
        sudo dnf install -y gtk4-devel glib2-devel
        ;;
    arch | manjaro)
        echo "Installing dependencies for Arch/Manjaro..."
        sudo pacman -Syu --noconfirm
        sudo pacman -S --noconfirm libx11 json-c dbus cmake gcc make pkgconf
        echo "Installing GUI dependencies..."
        sudo pacman -S --noconfirm gtk4 glib2
        ;;
    *)
        echo "Unsupported distro — install libX11, json-c, dbus manually"
        echo "For GUI, also install GTK4 development packages"
        ;;
    esac
}

build_and_install() {
    echo "Building GridFlux..."
    rm -rf "$BUILD_DIR" CMakeCache.txt CMakeFiles/
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .
    cmake --build "$BUILD_DIR" -- -j$(nproc)

    echo "Installing binaries..."
    sudo install -Dm755 "$BUILD_DIR/gridflux" "$INSTALL_DIR/gridflux"
    echo "✓ Binary installed: $INSTALL_DIR/gridflux"

    # Install GUI if GTK4 is available
    if [ -f "$BUILD_DIR/gridflux-gui" ]; then
        sudo install -Dm755 "$BUILD_DIR/gridflux-gui" "$INSTALL_DIR/gridflux-gui"
        echo "✓ GUI installed: $INSTALL_DIR/gridflux-gui"
    else
        echo "⚠ GUI not built - GTK4 dependencies missing"
    fi

    # Install CLI if available
    if [ -f "$BUILD_DIR/gridflux-cli" ]; then
        sudo install -Dm755 "$BUILD_DIR/gridflux-cli" "$INSTALL_DIR/gridflux-cli"
        echo "✓ CLI installed: $INSTALL_DIR/gridflux-cli"
    else
        echo "⚠ CLI not built - dependencies missing"
    fi
}

install_systemd_service() {
    local SERVICE_DIR="$HOME/.config/systemd/user"
    echo "Installing systemd user service..."
    mkdir -p "$SERVICE_DIR"
    if [[ $IS_KDE -eq 1 ]]; then
        cat >"$SERVICE_FILE" <<EOF
[Unit]
Description=GridFlux Window Tiler
After=graphical-session.target plasma-plasmashell.service
Wants=plasma-plasmashell.service
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=$INSTALL_DIR/gridflux
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
EOF
    else
        cat >"$SERVICE_FILE" <<EOF
[Unit]
Description=GridFlux Window Tiler
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=$INSTALL_DIR/gridflux
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
EOF
    fi
    systemctl --user daemon-reload
    systemctl --user enable gridflux.service
    systemctl --user start gridflux
    echo "✓ Systemd service installed and enabled"
    echo "  Start now: systemctl --user start gridflux"
    echo "  Check status: systemctl --user status gridflux"
}

create_desktop_entry() {
    echo "Creating desktop entry for GUI..."
    mkdir -p "$HOME/.local/share/applications"
    cat >"$HOME/.local/share/applications/gridflux-gui.desktop" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=GridFlux Control Panel
Comment=Virtual workspace & Window management tool for arranging and managing windows
Exec=$INSTALL_DIR/gridflux-gui
Icon=gridflux
Terminal=false
Categories=System;Settings;
Keywords=window;manager;tiling;grid;layout;workspace;
StartupNotify=true
StartupWMClass=GridFlux
EOF
    echo "✓ Desktop entry created: $HOME/.local/share/applications/gridflux-gui.desktop"
    update-desktop-database ~/.local/share/applications 2>/dev/null || true
}

install_icons() {
    echo "Installing GridFlux icons..."

    # Create icon directories
    ICON_DIR="$HOME/.local/share/icons/hicolor"
    mkdir -p "$ICON_DIR/16x16/apps"
    mkdir -p "$ICON_DIR/32x32/apps"
    mkdir -p "$ICON_DIR/48x48/apps"

    # Copy icons (using absolute path from script location)
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

    if [ -f "$PROJECT_ROOT/icons/gridflux-16.png" ]; then
        cp "$PROJECT_ROOT/icons/gridflux-16.png" "$ICON_DIR/16x16/apps/gridflux.png"
        echo "✓ 16x16 icon installed"
    fi

    if [ -f "$PROJECT_ROOT/icons/gridflux-32.png" ]; then
        cp "$PROJECT_ROOT/icons/gridflux-32.png" "$ICON_DIR/32x32/apps/gridflux.png"
        echo "✓ 32x32 icon installed"
    fi

    if [ -f "$PROJECT_ROOT/icons/gridflux-48.png" ]; then
        cp "$PROJECT_ROOT/icons/gridflux-48.png" "$ICON_DIR/48x48/apps/gridflux.png"
        echo "✓ 48x48 icon installed"
    fi

    # Update desktop entry to use our icon
    if [ -f "$HOME/.local/share/applications/gridflux-gui.desktop" ]; then
        sed -i 's|Icon=applications-system|Icon=gridflux|g' "$HOME/.local/share/applications/gridflux-gui.desktop"
        echo "✓ Desktop entry updated with custom icon"
    fi

    # Create desktop shortcut
    if [ -d "$HOME/Desktop" ]; then
        cat >"$HOME/Desktop/GridFlux.desktop" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=GridFlux
Comment=Virtual workspace & Window management tool for arranging and managing windows
Exec=$INSTALL_DIR/gridflux-gui
Icon=gridflux
Terminal=false
Categories=System;Utility;
StartupNotify=true
StartupWMClass=GridFlux
EOF
        chmod +x "$HOME/Desktop/GridFlux.desktop"
        echo "✓ Desktop shortcut created at $HOME/Desktop/GridFlux.desktop"
    fi

    # Update icon cache
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
    fi

    echo "✓ Icons installation complete"
}

create_default_config() {
    mkdir -p "$HOME/.config/gridflux"
    cat >"$HOME/.config/gridflux/config.json" <<EOF
{
  "max_windows_per_workspace": 10,
  "max_workspaces": 10,
  "default_padding": 5,
  "min_window_size": 100
}
EOF
    echo "Default config created"
}

detect_desktop
remove_existing_service
install_dependencies
build_and_install
create_default_config
create_desktop_entry
install_icons

if [[ $IS_KDE -eq 1 ]]; then
    echo "KDE detected — installing KWin integration"
    install_systemd_service
elif [[ $IS_GNOME -eq 1 ]]; then
    echo "GNOME detected — installing GNOME Shell extension"
    install_systemd_service
else
    echo "Unknown desktop — manual start required"
    install_systemd_service
fi

echo ""
echo "=== Installation Complete ==="
echo ""
echo "📦 Installed Components:"
echo "  • gridflux        - Main window manager"
echo "  • gridflux-gui    - GUI Control Panel"
echo "  • gridflux-cli    - Command-line interface"
echo ""
echo "🚀 Usage:"
echo "  • Start GUI:     gridflux-gui"
echo "  • Start CLI:     gridflux-cli"
echo "  • Start service: systemctl --user start gridflux"
echo ""
echo "📋 Desktop Entry: GridFlux Control Panel (in applications menu)"
echo "🖥️  Desktop Shortcut: GridFlux (on your desktop)"
echo ""
echo "🔧 Configuration: $HOME/.config/gridflux/config.json"
echo ""
echo "To check if it's running:"
echo "  systemctl --user status gridflux"
