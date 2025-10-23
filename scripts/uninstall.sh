#!/bin/bash
set -e

INSTALL_DIR="/usr/local/bin"
SERVICE_NAME="gridflux.service"
SERVICE_PATH="$HOME/.config/systemd/user/$SERVICE_NAME"
BUILD_DIR="build"

echo "Uninstalling GridFlux..."

if systemctl --user list-units --full -all | grep -q "$SERVICE_NAME"; then
    echo "Stopping user systemd service..."
    systemctl --user stop "$SERVICE_NAME" || true
    systemctl --user disable "$SERVICE_NAME" || true
    if [ -f "$SERVICE_PATH" ]; then
        echo "Removing user systemd service file..."
        rm -f "$SERVICE_PATH"
    fi
    systemctl --user daemon-reload
else
    echo "User systemd service not found."
fi

if [ -f "$INSTALL_DIR/gridflux" ]; then
    echo "Removing binary from $INSTALL_DIR..."
    sudo rm -f "$INSTALL_DIR/gridflux"
else
    echo "Binary not found in $INSTALL_DIR."
fi

if [ -d "$BUILD_DIR" ]; then
    echo "Removing build directory..."
    rm -rf "$BUILD_DIR"
fi

echo "GridFlux uninstalled successfully!"
echo "You may also want to remove dynamic workspace settings manually if desired."
