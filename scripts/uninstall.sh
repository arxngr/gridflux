#!/usr/bin/env bash
set -e

INSTALL_DIR="/usr/local/bin"
SERVICE_NAME="gridflux.service"
SERVICE_PATH="$HOME/.config/systemd/user/$SERVICE_NAME"
AUTOSTART_FILE="$HOME/.config/autostart/gridflux.desktop"
CONFIG_DIR="$HOME/.config/gridflux"
BUILD_DIR="build"

echo "=== Uninstalling GridFlux ==="

if systemctl --user list-units --full -all | grep -q "$SERVICE_NAME"; then
  systemctl --user stop "$SERVICE_NAME" || true
  systemctl --user disable "$SERVICE_NAME" || true
fi

if [ -f "$SERVICE_PATH" ]; then
  rm -f "$SERVICE_PATH"
  systemctl --user daemon-reload
fi

if [ -f "$AUTOSTART_FILE" ]; then
  rm -f "$AUTOSTART_FILE"
fi

if [ -f "$INSTALL_DIR/gridflux" ]; then
  sudo rm -f "$INSTALL_DIR/gridflux"
fi

if [ -d "$CONFIG_DIR" ]; then
  rm -rf "$CONFIG_DIR"
fi

if [ -d "$BUILD_DIR" ]; then
  rm -rf "$BUILD_DIR"
fi

echo "=== GridFlux Uninstalled Successfully ==="
