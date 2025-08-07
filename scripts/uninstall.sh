#!/bin/bash

set -e

INSTALL_DIR="/usr/local/bin"
SERVICE_FILE="/etc/systemd/system/gridflux.service"

echo "Uninstalling GridFlux..."

# Stop and disable systemd service
if systemctl list-units --full -all | grep -q "gridflux.service"; then
    echo "Stopping systemd service..."
    sudo systemctl stop gridflux.service || true
    sudo systemctl disable gridflux.service || true
    sudo rm -f "$SERVICE_FILE"
    sudo systemctl daemon-reload
else
    echo "Systemd service not found."
fi

# Remove binary
if [ -f "$INSTALL_DIR/gridflux" ]; then
    echo "Removing binary from $INSTALL_DIR..."
    sudo rm -f "$INSTALL_DIR/gridflux"
else
    echo "Binary not found in $INSTALL_DIR."
fi

# Optionally remove build artifacts
if [ -d "build" ]; then
    echo "Removing build directory..."
    rm -rf build
fi

echo "✅ GridFlux uninstalled successfully."
