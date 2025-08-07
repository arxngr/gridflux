#!/bin/bash

set -e

USER_NAME=$(whoami)
COMPUTER_NAME=$(hostname)
INSTALL_DIR="/usr/local/bin"
SERVICE_FILE="/etc/systemd/system/gridflux.service"

install_dependencies() {
    echo "Installing dependencies..."

    local dependencies="libx11-dev cmake gcc make"

    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
        ubuntu | debian)
            echo "Detected Debian-based system."
            for dep in $dependencies; do
                if ! dpkg -s "$dep" &>/dev/null; then
                    echo "Installing $dep..."
                    sudo apt install -y "$dep"
                else
                    echo "$dep already installed."
                fi
            done
            ;;
        fedora | rhel | centos | almalinux | rocky)
            echo "Detected RHEL-based system."
            sudo dnf install -y libX11-devel cmake gcc make || sudo yum install -y libX11-devel cmake gcc make
            ;;
        arch | manjaro)
            echo "Detected Arch-based system."
            sudo pacman -Syu --noconfirm
            sudo pacman -S --noconfirm libx11 cmake gcc make
            ;;
        *)
            echo "Unsupported distribution: $ID"
            exit 1
            ;;
        esac
    else
        echo "Cannot detect OS. Install dependencies manually."
        exit 1
    fi
}

build_and_install() {
    echo "Building gridflux..."

    # Ensure we're in the right directory
    if [ ! -f "CMakeLists.txt" ]; then
        echo "Error: CMakeLists.txt not found. Make sure you're running this script from the project root."
        exit 1
    fi

    # Clean any existing build artifacts
    echo "Cleaning previous build artifacts..."
    rm -rf build/
    rm -f CMakeCache.txt
    rm -rf CMakeFiles/

    mkdir -p build

    echo "Configuring build..."
    if ! cmake -B build -DCMAKE_BUILD_TYPE=Release .; then
        echo "Error: CMake configuration failed"
        exit 1
    fi

    echo "Compiling..."
    if ! cmake --build build; then
        echo "Error: Build failed"
        exit 1
    fi

    # Check if binary was created
    if [ ! -f "build/gridflux" ]; then
        echo "Error: Binary 'build/gridflux' was not created"
        exit 1
    fi

    echo "Installing binary..."
    sudo cp build/gridflux "$INSTALL_DIR/"
    sudo chmod +x "$INSTALL_DIR/gridflux"

    echo "Build and installation completed successfully!"
}

create_systemd_service() {
    echo "Setting up systemd service..."

    if [ -f "$SERVICE_FILE" ]; then
        echo "Removing existing service..."
        sudo systemctl stop gridflux.service || true
        sudo systemctl disable gridflux.service || true
        sudo rm -f "$SERVICE_FILE"
    fi

    sudo tee "$SERVICE_FILE" >/dev/null <<EOL
[Unit]
Description=GridFlux Window Manager
After=graphical.target

[Service]
ExecStart=$INSTALL_DIR/gridflux
User=$USER_NAME
Environment=DISPLAY=:1
Environment=XDG_SESSION_TYPE=$XDG_SESSION_TYPE
Restart=always
RestartSec=3

[Install]
WantedBy=default.target
EOL

    sudo systemctl daemon-reload
    sudo systemctl enable gridflux.service
    sudo systemctl start gridflux.service
}

activate_dynamic_workspaces() {
    case "$XDG_CURRENT_DESKTOP" in
    GNOME)
        echo "Enabling dynamic workspaces on GNOME..."
        gsettings set org.gnome.mutter dynamic-workspaces true
        ;;
    KDE)
        echo "Enabling dynamic desktops on KDE..."
        kwriteconfig5 --file kwinrc --group Desktops --key Number 0
        kwriteconfig5 --file kwinrc --group Desktops --key Current 1
        ;;
    *)
        echo "Dynamic workspaces not supported on $XDG_CURRENT_DESKTOP"
        ;;
    esac
}

echo "Starting GridFlux installation..."

install_dependencies
build_and_install
create_systemd_service
activate_dynamic_workspaces

echo "✅ GridFlux installed and running!"
