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
    echo "→ Stopping systemd service..."
    systemctl --user stop gridflux.service 2>/dev/null || true
    systemctl --user disable gridflux.service 2>/dev/null || true
    rm -f "$SERVICE_FILE"
    systemctl --user daemon-reload
    echo "  ✓ Systemd service removed"
fi

echo "→ Removing GridFlux processes..."
pkill -f "$INSTALL_DIR/gridflux" 2>/dev/null || true
pkill -f "$INSTALL_DIR/gridflux-gui" 2>/dev/null || true
pkill -f "$INSTALL_DIR/gridflux-cli" 2>/dev/null || true
sleep 1
echo "  ✓ Processes stopped"

if command -v qdbus >/dev/null 2>&1; then
    echo "→ Removing KWin script..."
    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.unloadScript \
        "$KWIN_SCRIPT_NAME" 2>/dev/null || true

    if [ -f "$KWIN_INSTALL_DIR/$KWIN_QML_FILE" ]; then
        sudo rm -f "$KWIN_INSTALL_DIR/$KWIN_QML_FILE"
        echo "  ✓ KWin script removed"
    fi
fi

echo "→ Removing GNOME Shell extension..."

if [ -d "$GNOME_EXT_DIR" ]; then
    if command -v gnome-extensions >/dev/null 2>&1; then
        if gnome-extensions list 2>/dev/null | grep -q "$GNOME_EXT_UUID"; then
            echo "  • Disabling extension..."
            gnome-extensions disable "$GNOME_EXT_UUID" 2>/dev/null || true
            sleep 1
        fi
    fi

    echo "  • Removing extension files..."
    rm -rf "$GNOME_EXT_DIR"
    echo "  ✓ GNOME extension removed"

    if [ -n "$XDG_CURRENT_DESKTOP" ] && echo "$XDG_CURRENT_DESKTOP" | grep -qi gnome; then
        if [ "${XDG_SESSION_TYPE:-}" = "x11" ]; then
            echo "  • Restarting GNOME Shell..."
            busctl --user call org.gnome.Shell /org/gnome/Shell \
                org.gnome.Shell Eval s 'Meta.restart("GridFlux uninstalled")' 2>/dev/null || {
                echo "  ⚠ Could not restart GNOME Shell automatically"
                echo "  → Please press Alt+F2, type 'r', and press Enter to restart GNOME Shell"
            }
        else
            echo "  ⚠ Running on Wayland - please log out and log back in to complete removal"
        fi
    fi
else
    echo "  • GNOME extension not found (already removed or not installed)"
fi

if [ -f "$INSTALL_DIR/gridflux" ]; then
    echo "→ Removing GridFlux binary..."
    sudo rm -f "$INSTALL_DIR/gridflux"
    echo "  ✓ Binary removed"
fi

if [ -f "$INSTALL_DIR/gridflux-gui" ]; then
    echo "→ Removing GUI binary..."
    sudo rm -f "$INSTALL_DIR/gridflux-gui"
    echo "  ✓ GUI removed"
fi

if [ -f "$INSTALL_DIR/gridflux-cli" ]; then
    echo "→ Removing CLI binary..."
    sudo rm -f "$INSTALL_DIR/gridflux-cli"
    echo "  ✓ CLI removed"
fi

if [ -d "$CONFIG_DIR" ]; then
    echo "→ Removing configuration directory..."
    rm -rf "$CONFIG_DIR"
    echo "  ✓ Configuration removed"
fi

if [ -f "$HOME/.local/share/applications/gridflux-gui.desktop" ]; then
    echo "→ Removing desktop entry..."
    rm -f "$HOME/.local/share/applications/gridflux-gui.desktop"
    echo "  ✓ Desktop entry removed"
    update-desktop-database ~/.local/share/applications 2>/dev/null || true
fi

if [ -d "$KWIN_INSTALL_DIR" ]; then
    if [ -z "$(ls -A "$KWIN_INSTALL_DIR")" ]; then
        sudo rm -rf "$KWIN_INSTALL_DIR"
        echo "  ✓ KWin installation directory removed"
    fi
fi

echo ""
echo "=== GridFlux Uninstalled Successfully ==="
echo ""
echo "🗑️ Removed Components:"
echo "  • gridflux        - Main window manager"
echo "  • gridflux-gui    - GUI Control Panel"
echo "  • gridflux-cli    - Command-line interface"
echo "  • Desktop entry   - Application menu shortcut"
echo "  • Configuration    - Settings and data"
echo "  • KWin script     - Window tiling integration"
echo "  • GNOME extension - Shell integration (if applicable)"
echo "  • Systemd service  - Auto-start service"
echo ""
echo "🧹 Cleanup complete!"
echo ""

if [ -n "$XDG_CURRENT_DESKTOP" ]; then
    if echo "$XDG_CURRENT_DESKTOP" | grep -qi gnome; then
        if [ "${XDG_SESSION_TYPE:-}" = "wayland" ]; then
            echo "⚠ GNOME on Wayland detected:"
            echo "  → Please log out and log back in to complete the uninstallation"
        fi
    elif echo "$XDG_CURRENT_DESKTOP" | grep -qi kde; then
        echo "⚠ KDE Plasma detected:"
        echo "  → You may need to restart KWin or log out/in for changes to take effect"
    fi
fi

echo ""
echo "Thank you for using GridFlux!"
