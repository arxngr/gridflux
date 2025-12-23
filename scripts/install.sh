#!/usr/bin/env bash
set -e

INSTALL_DIR="/usr/local/bin"
BUILD_DIR="build"
AUTOSTART_FILE="$HOME/.config/autostart/gridflux.desktop"

KWIN_SCRIPT_NAME="gridflux-tiler"
KWIN_QML_FILE="kwin_tiler.qml"
KWIN_INSTALL_DIR="/usr/share/gridflux"

echo "=== GridFlux Installation Started ==="

detect_desktop() {
    DESKTOP="${XDG_CURRENT_DESKTOP:-Unknown}"

    echo "Detected DE: $DESKTOP"

    [[ "$DESKTOP" == *"KDE"* || "$KDE_FULL_SESSION" == "true" ]] && IS_KDE=1 || IS_KDE=0
    [[ "$DESKTOP" == *"GNOME"* ]] && IS_GNOME=1 || IS_GNOME=0
}

install_dependencies() {
    echo "Installing dependencies..."

    . /etc/os-release

    case "$ID" in
    ubuntu | debian)
        sudo apt install -y libx11-dev libjson-c-dev libdbus-1-dev cmake gcc make pkg-config
        ;;
    fedora | rhel | centos | almalinux | rocky)
        sudo dnf install -y libX11-devel json-c-devel dbus-devel cmake gcc make pkgconfig
        ;;
    arch | manjaro)
        sudo pacman -Syu --noconfirm libx11 json-c dbus cmake gcc make pkgconf
        ;;
    *)
        echo "Unsupported distro — install libX11, json-c, dbus manually"
        ;;
    esac
}

build_and_install() {
    echo "Building GridFlux..."

    rm -rf "$BUILD_DIR" CMakeCache.txt CMakeFiles/
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .
    cmake --build "$BUILD_DIR" -- -j$(nproc)

    sudo install -Dm755 "$BUILD_DIR/gridflux" "$INSTALL_DIR/gridflux"
    echo "Binary installed: $INSTALL_DIR/gridflux"
}

install_kwin_script() {
    echo "Installing KWin script..."

    sudo install -Dm644 \
        "src/platform/kwin/$KWIN_QML_FILE" \
        "$KWIN_INSTALL_DIR/$KWIN_QML_FILE"

    echo "✓ KWin script installed to $KWIN_INSTALL_DIR"

    echo "Registering script with KWin via D-Bus..."

    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.loadDeclarativeScript \
        "$KWIN_INSTALL_DIR/$KWIN_QML_FILE" \
        "$KWIN_SCRIPT_NAME" || true

    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.start || true

    echo "KWin script loaded"
}

install_kde_autostart() {
    mkdir -p "$(dirname "$AUTOSTART_FILE")"

    cat >"$AUTOSTART_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=GridFlux
Exec=$INSTALL_DIR/gridflux
OnlyShowIn=KDE;
X-KDE-autostart-after=plasmashell
Terminal=false
EOF

    echo "KDE autostart installed"
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
install_dependencies
build_and_install
create_default_config

if [[ $IS_KDE -eq 1 ]]; then
    echo "KDE detected — installing KWin integration"
    install_kwin_script
    install_kde_autostart
elif [[ $IS_GNOME -eq 1 ]]; then
    echo "GNOME detected — no KWin integration"
else
    echo "Unknown desktop — manual start required"
fi

echo "=== Installation Complete ==="
