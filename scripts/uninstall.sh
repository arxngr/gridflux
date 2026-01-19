#!/usr/bin/env bash
set -e

INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="$HOME/.config/gridflux"
KWIN_SCRIPT_NAME="gridflux-tiler"
KWIN_INSTALL_DIR="/usr/share/kwin/scripts/$KWIN_SCRIPT_NAME"
OLD_KWIN_INSTALL_DIR="/usr/share/gridflux"
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

echo "→ Removing binaries..."
if [ -f "$INSTALL_DIR/gridflux" ]; then
    sudo rm -f "$INSTALL_DIR/gridflux"
    echo "  ✓ Binary removed: $INSTALL_DIR/gridflux"
fi

if [ -f "$INSTALL_DIR/gridflux-gui" ]; then
    sudo rm -f "$INSTALL_DIR/gridflux-gui"
    echo "  ✓ GUI removed: $INSTALL_DIR/gridflux-gui"
fi

if [ -f "$INSTALL_DIR/gridflux-cli" ]; then
    sudo rm -f "$INSTALL_DIR/gridflux-cli"
    echo "  ✓ CLI removed: $INSTALL_DIR/gridflux-cli"
fi

echo "→ Removing desktop entries and icons..."
if [ -f "$HOME/.local/share/applications/gridflux-gui.desktop" ]; then
    rm -f "$HOME/.local/share/applications/gridflux-gui.desktop"
    echo "  ✓ Desktop entry removed"
fi

if [ -f "$HOME/Desktop/GridFlux.desktop" ]; then
    rm -f "$HOME/Desktop/GridFlux.desktop"
    echo "  ✓ Desktop shortcut removed"
fi

# Remove icons
ICON_DIR="$HOME/.local/share/icons/hicolor"
if [ -f "$ICON_DIR/16x16/apps/gridflux.png" ]; then
    rm -f "$ICON_DIR/16x16/apps/gridflux.png"
    echo "  ✓ 16x16 icon removed"
fi

if [ -f "$ICON_DIR/32x32/apps/gridflux.png" ]; then
    rm -f "$ICON_DIR/32x32/apps/gridflux.png"
    echo "  ✓ 32x32 icon removed"
fi

if [ -f "$ICON_DIR/48x48/apps/gridflux.png" ]; then
    rm -f "$ICON_DIR/48x48/apps/gridflux.png"
    echo "  ✓ 48x48 icon removed"
fi

# Update icon cache
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
fi

if command -v qdbus >/dev/null 2>&1; then
    echo "→ Removing KWin script..."
    # Unload the script first
    qdbus org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.unloadScript \
        "$KWIN_SCRIPT_NAME" 2>/dev/null || true
    
    # Remove the script directory
    if [ -d "$KWIN_INSTALL_DIR" ]; then
        sudo rm -rf "$KWIN_INSTALL_DIR"
        echo "  ✓ KWin script removed from $KWIN_INSTALL_DIR"
    fi
    
    # Also remove old installation location if it exists
    if [ -f "$OLD_KWIN_INSTALL_DIR/$KWIN_QML_FILE" ]; then
        sudo rm -f "$OLD_KWIN_INSTALL_DIR/$KWIN_QML_FILE"
        echo "  ✓ Old KWin script file removed"
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

    update-desktop-database ~/.local/share/applications 2>/dev/null || true
    echo "✓ Icon cache updated"
    
    echo ""
    echo "=== GridFlux Uninstallation Complete ==="
    echo "Note: Configuration files in $HOME/.config/gridflux/ were preserved."
    echo "To remove config files, run: rm -rf $HOME/.config/gridflux"

echo ""
echo "🗑️ Removed Components:"
echo "  • gridflux        - Main window manager"
echo "  • gridflux-gui    - GUI Control Panel"
echo "  • gridflux-cli    - Command-line interface"
echo "  • Desktop entry   - Application menu shortcut"
echo "  • Desktop shortcut - Desktop launcher"
echo "  • Icons           - All icon sizes (16x16, 32x32, 48x48)"
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
            echo "  → Please log out and log back in to complete uninstallation"
        fi
    elif echo "$XDG_CURRENT_DESKTOP" | grep -qi kde; then
        echo "⚠ KDE Plasma detected:"
        echo "  → You may need to restart KWin or log out/in for changes to take effect"
    fi
fi

echo ""
echo "Thank you for using GridFlux!"
