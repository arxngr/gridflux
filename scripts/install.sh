#!/usr/bin/env bash
# install.sh - Improved GridFlux Installer
set -euo pipefail

trap 'echo "ERROR: Installation failed at line $LINENO"; exit 1' ERR

INSTALL_DIR="/usr/local/bin"
BUILD_DIR="build"
KWIN_SCRIPT_NAME="gridflux-tiler"
KWIN_QML_FILE="main.qml"
KWIN_INSTALL_DIR="/usr/share/kwin/scripts/$KWIN_SCRIPT_NAME"
SERVICE_FILE="$HOME/.config/systemd/user/gridflux.service"
GNOME_EXT_UUID="gridflux@gridflux.dev"
GNOME_EXT_DIR="$HOME/.local/share/gnome-shell/extensions/$GNOME_EXT_UUID"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}✓${NC} $1"; }
log_warn() { echo -e "${YELLOW}⚠${NC} $1"; }
log_error() { echo -e "${RED}✗${NC} $1"; }

echo "=== GridFlux Installation Started ==="

# Check if running on X11 or Wayland
check_display_server() {
    if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
        log_error "No display server detected (X11 or Wayland required)"
        exit 1
    fi

    if [ -n "${WAYLAND_DISPLAY:-}" ]; then
        log_warn "Wayland detected - X11 recommended for full functionality"
    fi
}

detect_desktop() {
    DESKTOP="${XDG_CURRENT_DESKTOP:-Unknown}"
    echo "Detected DE: $DESKTOP"
    [[ "$DESKTOP" == *"KDE"* || "$KDE_FULL_SESSION" == "true" ]] && IS_KDE=1 || IS_KDE=0
    [[ "$DESKTOP" == *"GNOME"* ]] && IS_GNOME=1 || IS_GNOME=0
}

remove_existing_service() {
    if [ -f "$SERVICE_FILE" ]; then
        log_info "Stopping existing systemd service..."
        systemctl --user stop gridflux.service 2>/dev/null || true
        systemctl --user disable gridflux.service 2>/dev/null || true
        rm -f "$SERVICE_FILE"
        systemctl --user daemon-reload
        log_info "Existing service removed"
    fi
}

install_dependencies() {
    echo "Installing dependencies..."
    . /etc/os-release
    case "$ID" in
    ubuntu | debian)
        echo "Installing dependencies for Ubuntu/Debian..."
        sudo apt update
        sudo apt install -y libx11-dev libjson-c-dev libdbus-1-dev cmake gcc make pkg-config
        sudo apt install -y libgtk-4-dev libglib2.0-dev
        ;;
    fedora | rhel | centos | almalinux | rocky)
        echo "Installing dependencies for Fedora/RHEL..."
        sudo dnf install -y libX11-devel json-c-devel dbus-devel cmake gcc make pkgconfig
        sudo dnf install -y gtk4-devel glib2-devel
        ;;
    arch | manjaro)
        echo "Installing dependencies for Arch/Manjaro..."
        sudo pacman -Syu --noconfirm
        sudo pacman -S --noconfirm libx11 json-c dbus cmake gcc make pkgconf
        sudo pacman -S --noconfirm gtk4 glib2
        ;;
    *)
        log_error "Unsupported distro"
        echo "Please install: libx11-dev, libjson-c-dev, libdbus-1-dev, cmake, gcc, make, pkg-config"
        echo "For GUI: libgtk-4-dev, libglib2.0-dev"
        exit 1
        ;;
    esac
}

build_and_install() {
    echo "Building GridFlux..."

    # Clean previous builds
    [ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"
    [ -f CMakeCache.txt ] && rm -f CMakeCache.txt

    # Build
    if ! cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .; then
        log_error "CMake configuration failed"
        exit 1
    fi

    if ! cmake --build "$BUILD_DIR" -- -j$(nproc); then
        log_error "Build failed"
        exit 1
    fi

    echo "Installing binaries..."

    # Install main binary
    if [ ! -f "$BUILD_DIR/gridflux" ]; then
        log_error "gridflux binary not found after build"
        exit 1
    fi

    if ! sudo install -Dm755 "$BUILD_DIR/gridflux" "$INSTALL_DIR/gridflux"; then
        log_error "Failed to install gridflux binary"
        exit 1
    fi
    log_info "Binary installed: $INSTALL_DIR/gridflux"

    # Install GUI if available
    if [ -f "$BUILD_DIR/gridflux-gui" ]; then
        sudo install -Dm755 "$BUILD_DIR/gridflux-gui" "$INSTALL_DIR/gridflux-gui"
        log_info "GUI installed: $INSTALL_DIR/gridflux-gui"
    else
        log_warn "GUI not built - GTK4 dependencies missing"
    fi

    # Install CLI if available
    if [ -f "$BUILD_DIR/gridflux-cli" ]; then
        sudo install -Dm755 "$BUILD_DIR/gridflux-cli" "$INSTALL_DIR/gridflux-cli"
        log_info "CLI installed: $INSTALL_DIR/gridflux-cli"
    else
        log_warn "CLI not built - dependencies missing"
    fi
}

unload_kwin_script() {
    if ! command -v qdbus &>/dev/null; then
        log_warn "qdbus not found - skipping KWin script unload"
        return 0
    fi

    local loaded=$(qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadedScripts 2>/dev/null | grep -i "$KWIN_SCRIPT_NAME" || echo "")

    if [ -n "$loaded" ]; then
        log_info "Unloading existing KWin script..."
        qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$KWIN_SCRIPT_NAME" 2>/dev/null || true
    fi
}

install_kwin_script() {
    echo "Installing KWin script..."
    unload_kwin_script

    # Verify source files exist
    local files_ok=true
    [ -f "src/platform/unix/kwin/$KWIN_QML_FILE" ] || {
        log_error "main.qml not found"
        files_ok=false
    }
    [ -f "src/platform/unix/kwin/code/tiler.js" ] || {
        log_error "tiler.js not found"
        files_ok=false
    }
    [ -f "src/platform/unix/kwin/metadata.json" ] || {
        log_error "metadata.json not found"
        files_ok=false
    }

    if [ "$files_ok" = false ]; then
        return 1
    fi

    # Create and install files
    sudo mkdir -p "$KWIN_INSTALL_DIR/contents/ui"
    sudo cp "src/platform/unix/kwin/$KWIN_QML_FILE" "$KWIN_INSTALL_DIR/contents/ui/main.qml"
    sudo cp "src/platform/unix/kwin/code/tiler.js" "$KWIN_INSTALL_DIR/contents/ui/tiler.js"
    sudo cp "src/platform/unix/kwin/metadata.json" "$KWIN_INSTALL_DIR/metadata.json"

    # Set permissions
    sudo chmod 644 "$KWIN_INSTALL_DIR/contents/ui/main.qml"
    sudo chmod 644 "$KWIN_INSTALL_DIR/contents/ui/tiler.js"
    sudo chmod 644 "$KWIN_INSTALL_DIR/metadata.json"
    sudo chmod 755 "$KWIN_INSTALL_DIR"
    sudo chmod 755 "$KWIN_INSTALL_DIR/contents"
    sudo chmod 755 "$KWIN_INSTALL_DIR/contents/ui"

    log_info "KWin script directory structure created"

    # Verify files
    if [ ! -f "$KWIN_INSTALL_DIR/contents/ui/main.qml" ] ||
        [ ! -f "$KWIN_INSTALL_DIR/contents/ui/tiler.js" ] ||
        [ ! -f "$KWIN_INSTALL_DIR/metadata.json" ]; then
        log_error "Some KWin script files are missing"
        return 1
    fi

    log_info "KWin script files verified"

    # Try to register with KWin
    if command -v qdbus &>/dev/null; then
        sleep 1
        if qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadDeclarativeScript \
            "$KWIN_INSTALL_DIR" "$KWIN_SCRIPT_NAME" 2>/dev/null; then
            log_info "Script registered with KWin"
        else
            log_warn "Failed to register script with D-Bus (may load on next KWin restart)"
        fi

        sleep 1
        local script_status=$(qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.isScriptLoaded "$KWIN_SCRIPT_NAME" 2>/dev/null || echo "false")
        if [[ "$script_status" == "true" ]]; then
            log_info "KWin script loaded successfully"
        else
            log_warn "KWin script not loaded yet - check logs or restart KWin"
        fi
    fi
}

install_gnome_extension() {
    echo "Installing GNOME Shell extension..."

    # Verify files exist
    if [ ! -f "src/platform/unix/mutter/extension.js" ] ||
        [ ! -f "src/platform/unix/mutter/metadata.json" ]; then
        log_error "GNOME extension files not found"
        return 1
    fi

    mkdir -p "$GNOME_EXT_DIR"
    cp src/platform/unix/mutter/extension.js "$GNOME_EXT_DIR/"
    cp src/platform/unix/mutter/metadata.json "$GNOME_EXT_DIR/"

    log_info "GNOME extension files installed"

    if command -v gnome-extensions >/dev/null 2>&1; then
        if gnome-extensions enable "$GNOME_EXT_UUID" 2>/dev/null; then
            log_info "GNOME extension enabled"
        else
            log_warn "Failed to enable GNOME extension (enable manually in settings)"
        fi
    else
        log_warn "gnome-extensions tool not found - enable '$GNOME_EXT_UUID' manually"
    fi
}

install_systemd_service() {
    local SERVICE_DIR="$HOME/.config/systemd/user"
    echo "Installing systemd user service..."
    mkdir -p "$SERVICE_DIR"

    if [[ $IS_KDE -eq 1 ]]; then
        cat >"$SERVICE_FILE" <<'EOF'
[Unit]
Description=GridFlux Window Tiler
After=graphical-session.target plasma-plasmashell.service
Wants=plasma-plasmashell.service
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/local/bin/gridflux
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
EOF
    else
        cat >"$SERVICE_FILE" <<'EOF'
[Unit]
Description=GridFlux Window Tiler
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/local/bin/gridflux
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
EOF
    fi

    systemctl --user daemon-reload
    systemctl --user enable gridflux.service
    systemctl --user start gridflux
    log_info "Systemd service installed and started"
}

create_desktop_entry() {
    echo "Creating desktop entry..."
    mkdir -p "$HOME/.local/share/applications"
    cat >"$HOME/.local/share/applications/gridflux-gui.desktop" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=GridFlux Control Panel
Comment=Virtual workspace & Window management tool
Exec=/usr/local/bin/gridflux-gui
Icon=gridflux
Terminal=false
Categories=System;Settings;
Keywords=window;manager;tiling;grid;layout;workspace;
StartupNotify=true
StartupWMClass=GridFlux
EOF
    log_info "Desktop entry created"
    update-desktop-database ~/.local/share/applications 2>/dev/null || true
}

install_icons() {
    echo "Installing icons..."
    ICON_DIR="$HOME/.local/share/icons/hicolor"

    for size in 16 32 48; do
        if [ -f "icons/gridflux-${size}.png" ]; then
            mkdir -p "$ICON_DIR/${size}x${size}/apps"
            cp "icons/gridflux-${size}.png" "$ICON_DIR/${size}x${size}/apps/gridflux.png"
            log_info "${size}x${size} icon installed"
        fi
    done

    # Update icon cache
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
    fi
}

create_default_config() {
    mkdir -p "$HOME/.config/gridflux"
    if [ ! -f "$HOME/.config/gridflux/config.json" ]; then
        cat >"$HOME/.config/gridflux/config.json" <<EOF
{
  "max_windows_per_workspace": 10,
  "max_workspaces": 10,
  "default_padding": 5,
  "min_window_size": 100
}
EOF
        log_info "Default config created"
    else
        log_warn "Config already exists - preserving"
    fi
}

# Main installation flow
check_display_server
detect_desktop
remove_existing_service
install_dependencies
build_and_install
create_default_config
create_desktop_entry
install_icons

if [[ $IS_KDE -eq 1 ]]; then
    log_info "KDE detected - installing KWin integration"
    install_kwin_script
    install_systemd_service
elif [[ $IS_GNOME -eq 1 ]]; then
    log_info "GNOME detected - installing GNOME Shell extension"
    install_gnome_extension
    install_systemd_service
else
    log_warn "Unknown desktop - installing generic service"
    install_systemd_service
fi

echo ""
echo "=== Installation Complete ==="
echo ""
echo "📦 Installed Components:"
echo "  • gridflux        - Main window manager"
[ -f "$INSTALL_DIR/gridflux-gui" ] && echo "  • gridflux-gui    - GUI Control Panel"
[ -f "$INSTALL_DIR/gridflux-cli" ] && echo "  • gridflux-cli    - Command-line interface"
echo ""
echo "🚀 Quick Start:"
echo "  systemctl --user status gridflux"
echo ""
echo "🔧 Configuration: $HOME/.config/gridflux/config.json"
