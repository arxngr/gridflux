#!/bin/bash
set -e

INSTALL_DIR="/usr/local/bin"
SERVICE_NAME="gridflux.service"
SERVICE_PATH="$HOME/.config/systemd/user/$SERVICE_NAME"
BUILD_DIR="build"

echo "Starting GridFlux installation..."

install_dependencies() {
    echo "Installing dependencies..."

    local dependencies="libx11-dev cmake gcc make"
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
        ubuntu | debian)
            echo "Detected Debian-based system."
            sudo apt update -y
            sudo apt install -y libx11-dev cmake gcc make
            ;;
        fedora | rhel | centos | almalinux | rocky)
            echo "Detected RHEL-based system."
            sudo dnf install -y libX11-devel cmake gcc make || sudo yum install -y libX11-devel cmake gcc make
            ;;
        arch | manjaro)
            echo "Detected Arch-based system."
            sudo pacman -Syu --noconfirm libx11 cmake gcc make
            ;;
        *)
            echo "Unsupported distribution: $ID"
            ;;
        esac
    fi
}

build_and_install() {
    echo "Building GridFlux..."
    if [ ! -f "CMakeLists.txt" ]; then
        echo "Error: Run this script from the project root (where CMakeLists.txt is)."
        exit 1
    fi

    rm -rf "$BUILD_DIR" CMakeCache.txt CMakeFiles/
    mkdir -p "$BUILD_DIR"
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .
    cmake --build "$BUILD_DIR"

    if [ ! -f "$BUILD_DIR/gridflux" ]; then
        echo "Build failed: binary not found."
        exit 1
    fi

    echo "Installing binary to $INSTALL_DIR..."
    sudo cp "$BUILD_DIR/gridflux" "$INSTALL_DIR/"
    sudo chmod +x "$INSTALL_DIR/gridflux"
}

detect_display_env() {
    echo "Detecting display environment..."

    DISPLAY_VAL=${DISPLAY:-:0}
    XAUTHORITY_VAL=${XAUTHORITY:-"$HOME/.Xauthority"}
    XDG_TYPE=${XDG_SESSION_TYPE:-x11}

    if [[ -z "$DISPLAY" ]]; then
        DISPLAY_VAL=$(loginctl show-user "$USER" | grep -oP 'Display=\K\S+' || echo ":0")
    fi

    if [[ ! -S /tmp/.X11-unix/${DISPLAY_VAL#:} ]]; then
        echo "Warning: X socket for $DISPLAY_VAL not found, fallback to :0"
        DISPLAY_VAL=":0"
    fi

    if [[ ! -f "$XAUTHORITY_VAL" ]]; then
        echo "Warning: XAUTHORITY file not found, creating file..."
        touch "$HOME/.Xauthority"
        XAUTHORITY_VAL="$HOME/.Xauthority"
    fi

    echo "DISPLAY=$DISPLAY_VAL"
    echo "XAUTHORITY=$XAUTHORITY_VAL"
    echo "XDG_SESSION_TYPE=$XDG_TYPE"
}

create_user_service() {
    echo "Creating user systemd service..."

    mkdir -p "$(dirname "$SERVICE_PATH")"

    cat >"$SERVICE_PATH" <<EOL
[Unit]
Description=GridFlux Window Manager
After=graphical-session.target
PartOf=graphical-session.target

[Service]
ExecStart=$INSTALL_DIR/gridflux
Restart=on-failure
RestartSec=3
Environment=DISPLAY=$DISPLAY_VAL
Environment=XAUTHORITY=$XAUTHORITY_VAL
Environment=XDG_SESSION_TYPE=$XDG_TYPE

[Install]
WantedBy=graphical-session.target
EOL

    systemctl --user daemon-reload
    systemctl --user enable "$SERVICE_NAME"
    systemctl --user restart "$SERVICE_NAME"

    echo "GridFlux systemd user service installed and started."
}

activate_dynamic_workspaces() {
    case "$XDG_CURRENT_DESKTOP" in
    GNOME)
        echo "Configuring dynamic workspaces for GNOME..."
        gsettings set org.gnome.mutter dynamic-workspaces true || true
        ;;
    KDE)
        echo "Configuring dynamic desktops for KDE..."
        kwriteconfig5 --file kwinrc --group Desktops --key Number 0 || true
        kwriteconfig5 --file kwinrc --group Desktops --key Current 1 || true
        ;;
    *)
        echo "Dynamic workspace setting not applicable for $XDG_CURRENT_DESKTOP"
        ;;
    esac
}

install_dependencies
build_and_install
detect_display_env
create_user_service
activate_dynamic_workspaces

echo "Installation complete. You can manage GridFlux with:"
echo "   systemctl --user status gridflux"
echo "   systemctl --user restart gridflux"
echo "GridFlux is now running in your graphical session."
