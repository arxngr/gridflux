#!/usr/bin/env bash
set -e

INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="$HOME/.config/gridflux"

KWIN_SCRIPT_NAME="gridflux-tiler"
KWIN_QML_FILE="kwin_tiler.qml"
KWIN_INSTALL_DIR="/usr/share/gridflux"

SERVICE_FILE="$HOME/.config/systemd/user/gridflux.service"

GNOME_EXT_UUID="gridflux@gridflux.dev"
GNOME_EXT_DIR="$HOME/.local/share/gnome-shell/extensions/$GNOME_EXT_UUID"

echo "=== Uninstalling GridFlux ==="

if [ -f "$SERVICE_FILE" ]; then
    systemctl --user stop gridflux.service || true
    systemctl --user disable gridflux.service || true
    rm -f "$SERVICE_FILE"
    systemctl --user daemon-reload
fi

pkill -f "$INSTALL_DIR/gridflux" || true

if command -v qdbus >/dev/null 2>&1; then
    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.unloadScript \
        "$KWIN_SCRIPT_NAME" || true
fi

if [ -f "$KWIN_INSTALL_DIR/$KWIN_QML_FILE" ]; then
    sudo rm -f "$KWIN_INSTALL_DIR/$KWIN_QML_FILE"
fi

if command -v gnome-extensions >/dev/null 2>&1; then
    if gnome-extensions list | grep -q "$GNOME_EXT_UUID"; then
        gnome-extensions disable "$GNOME_EXT_UUID" || true
    fi
fi

if [ -d "$GNOME_EXT_DIR" ]; then
    rm -rf "$GNOME_EXT_DIR"
fi

if [ -n "$XDG_CURRENT_DESKTOP" ] && echo "$XDG_CURRENT_DESKTOP" | grep -qi gnome; then
    if [ "${XDG_SESSION_TYPE:-}" = "x11" ]; then
        busctl --user call org.gnome.Shell /org/gnome/Shell \
            org.gnome.Shell Eval s 'Meta.restart("gridflux uninstall")' || true
    fi
fi

if [ -f "$INSTALL_DIR/gridflux" ]; then
    sudo rm -f "$INSTALL_DIR/gridflux"
fi

if [ -d "$CONFIG_DIR" ]; then
    rm -rf "$CONFIG_DIR"
fi

echo "=== GridFlux Uninstalled Successfully ==="
