#!/usr/bin/env bash
set -e
INSTALL_DIR="/usr/local/bin"
BUILD_DIR="build"
KWIN_SCRIPT_NAME="gridflux-tiler"
KWIN_QML_FILE="kwin_tiler.qml"
KWIN_INSTALL_DIR="/usr/share/kwin/scripts/gridflux"
SERVICE_FILE="$HOME/.config/systemd/user/gridflux.service"

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

unload_kwin_script() {
    echo "Checking for existing KWin script..."
    local script_id=$(qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadedScripts 2>/dev/null | grep -i "$KWIN_SCRIPT_NAME" || true)
    if [ -n "$script_id" ]; then
        echo "Unloading existing KWin script..."
        qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$KWIN_SCRIPT_NAME" 2>/dev/null || true
        echo "✓ Existing KWin script unloaded"
    fi
}

install_kwin_script() {
    echo "Installing KWin script..."
    unload_kwin_script
    sudo install -Dm644 \
        "src/platform/kwin/metadata.json" \
        "$KWIN_INSTALL_DIR/metadata.json"
    sudo install -Dm644 \
        "src/platform/kwin/main.qml" \
        "$KWIN_INSTALL_DIR/contents/ui/main.qml"
    sudo install -Dm644 \
        "src/platform/kwin/code/tiler.js" \
        "$KWIN_INSTALL_DIR/contents/ui/tiler.js"
    echo "✓ KWin script installed to $KWIN_INSTALL_DIR"
    echo "Registering script with KWin via D-Bus..."
    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.loadDeclarativeScript \
        "$KWIN_INSTALL_DIR/contents/ui/main.qml" \
        "$KWIN_SCRIPT_NAME" || true
    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.start || true
    echo "KWin script loaded"
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

if [[ $IS_KDE -eq 1 ]]; then
    echo "KDE detected — installing KWin integration"
    install_kwin_script
    install_systemd_service
elif [[ $IS_GNOME -eq 1 ]]; then
    echo "GNOME detected — no KWin integration"
    install_systemd_service
else
    echo "Unknown desktop — installing systemd service only"
    install_systemd_service
fi

echo "=== Installation Complete ==="
echo ""
echo "To check if it's running:"
echo "  systemctl --user status gridflux"
echo ""
echo "To view logs:"
echo "  journalctl --user -u gridflux -f"
