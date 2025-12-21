#!/usr/bin/env bash
set -e

INSTALL_DIR="/usr/local/bin"
AUTOSTART_FILE="$HOME/.config/autostart/gridflux.desktop"
CONFIG_DIR="$HOME/.config/gridflux"

KWIN_SCRIPT_NAME="gridflux-tiler"
KWIN_QML_FILE="kwin_tiler.qml"
KWIN_INSTALL_DIR="/usr/share/gridflux"

echo "=== Uninstalling GridFlux ==="

echo "Stopping GridFlux (if running)"
pkill -f "$INSTALL_DIR/gridflux" || true

if command -v qdbus >/dev/null 2>&1; then
    echo "Unloading KWin script via D-Bus"
    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.unloadScript \
        "$KWIN_SCRIPT_NAME" || true
fi

if [ -f "$AUTOSTART_FILE" ]; then
    rm -f "$AUTOSTART_FILE"
    echo "KDE autostart removed"
fi

if [ -f "$KWIN_INSTALL_DIR/$KWIN_QML_FILE" ]; then
    sudo rm -f "$KWIN_INSTALL_DIR/$KWIN_QML_FILE"
    echo "KWin script removed"
fi

if [ -f "$INSTALL_DIR/gridflux" ]; then
    sudo rm -f "$INSTALL_DIR/gridflux"
    echo "Binary removed"
fi

if [ -d "$CONFIG_DIR" ]; then
    rm -rf "$CONFIG_DIR"
    echo "Config removed"
fi

echo "=== GridFlux Uninstalled Successfully ==="
