#!/bin/bash
set -e

INSTALL_DIR="/usr/local/bin"
SERVICE_NAME="gridflux.service"
SERVICE_PATH="$HOME/.config/systemd/user/$SERVICE_NAME"
BUILD_DIR="build"

echo "Starting GridFlux installation..."

install_dependencies() {
    echo "Installing dependencies..."

    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
        ubuntu | debian)
            echo "Detected Debian-based system."
            sudo apt update -y
            sudo apt install -y libx11-dev libjson-c-dev cmake gcc make pkg-config
            ;;
        fedora | rhel | centos | almalinux | rocky)
            echo "Detected RHEL-based system."
            sudo dnf install -y libX11-devel json-c-devel cmake gcc make pkgconfig ||
                sudo yum install -y libX11-devel json-c-devel cmake gcc make pkgconfig
            ;;
        arch | manjaro)
            echo "Detected Arch-based system."
            sudo pacman -Syu --noconfirm libx11 json-c cmake gcc make pkgconf
            ;;
        opensuse* | sles)
            echo "Detected openSUSE/SLES system."
            sudo zypper install -y libX11-devel libjson-c-devel cmake gcc make pkg-config
            ;;
        gentoo)
            echo "Detected Gentoo system."
            sudo emerge --ask=n x11-libs/libX11 dev-libs/json-c dev-util/cmake sys-devel/gcc sys-devel/make dev-util/pkgconfig
            ;;
        alpine)
            echo "Detected Alpine system."
            sudo apk add libx11-dev json-c-dev cmake gcc make pkgconfig
            ;;
        void)
            echo "Detected Void Linux."
            sudo xbps-install -Sy libX11-devel json-c-devel cmake gcc make pkg-config
            ;;
        *)
            echo "Unsupported distribution: $ID"
            echo "Please manually install: libx11-dev(el), libjson-c-dev(el), cmake, gcc, make, pkg-config"
            read -p "Continue anyway? (y/N) " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                exit 1
            fi
            ;;
        esac
    else
        echo "Cannot detect OS. Please install dependencies manually:"
        echo "  - libx11 development files"
        echo "  - json-c development files"
        echo "  - cmake, gcc, make, pkg-config"
        read -p "Have you installed the dependencies? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi

    # Verify json-c installation
    echo "Verifying json-c installation..."
    if pkg-config --exists json-c; then
        echo "✓ json-c found: $(pkg-config --modversion json-c)"
    else
        echo "✗ json-c not found via pkg-config"
        echo "Attempting to locate manually..."
        if ldconfig -p | grep -q libjson-c.so; then
            echo "✓ json-c library found in system"
        else
            echo "✗ json-c library not found!"
            echo "Please install json-c development package for your distribution"
            exit 1
        fi
    fi
}

build_and_install() {
    echo "Building GridFlux..."

    if [ ! -f "CMakeLists.txt" ]; then
        echo "Error: Run this script from the project root (where CMakeLists.txt is)."
        exit 1
    fi

    # Clean previous build
    rm -rf "$BUILD_DIR" CMakeCache.txt CMakeFiles/
    mkdir -p "$BUILD_DIR"

    # Configure with CMake
    echo "Configuring with CMake..."
    if ! cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .; then
        echo "CMake configuration failed!"
        echo "Please check if all dependencies are installed correctly."
        exit 1
    fi

    # Build
    echo "Compiling..."
    if ! cmake --build "$BUILD_DIR" -- -j$(nproc); then
        echo "Build failed!"
        exit 1
    fi

    # Verify binary exists
    if [ ! -f "$BUILD_DIR/gridflux" ]; then
        echo "Build failed: binary not found at $BUILD_DIR/gridflux"
        exit 1
    fi

    # Check if binary is properly linked
    echo "Verifying binary dependencies..."
    if ldd "$BUILD_DIR/gridflux" | grep -q "not found"; then
        echo "Warning: Binary has missing dependencies:"
        ldd "$BUILD_DIR/gridflux" | grep "not found"
        echo "Please install missing libraries."
        exit 1
    fi

    echo "✓ Binary successfully built and verified"

    # Install
    echo "Installing binary to $INSTALL_DIR..."
    sudo cp "$BUILD_DIR/gridflux" "$INSTALL_DIR/"
    sudo chmod +x "$INSTALL_DIR/gridflux"

    echo "✓ Binary installed to $INSTALL_DIR/gridflux"
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

    echo "✓ GridFlux systemd user service installed and started."
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

create_default_config() {
    CONFIG_DIR="$HOME/.config/gridflux"
    CONFIG_FILE="$CONFIG_DIR/config.json"

    if [ ! -f "$CONFIG_FILE" ]; then
        echo "Creating default configuration..."
        mkdir -p "$CONFIG_DIR"
        cat >"$CONFIG_FILE" <<EOL
{
  "max_windows_per_workspace": 10,
  "max_workspaces": 10,
  "default_padding": 5,
  "min_window_size": 100
}
EOL
        echo "✓ Default config created at $CONFIG_FILE"
        echo "  You can edit this file to customize GridFlux behavior"
    else
        echo "✓ Config file already exists at $CONFIG_FILE"
    fi
}

# Main installation flow
install_dependencies
build_and_install
detect_display_env
create_default_config
create_user_service
activate_dynamic_workspaces

echo ""
echo "=========================================="
echo "✓ Installation complete!"
echo "=========================================="
echo ""
echo "GridFlux is now running. You can manage it with:"
echo "  systemctl --user status gridflux"
echo "  systemctl --user restart gridflux"
echo "  systemctl --user stop gridflux"
echo ""
echo "Configuration file: $HOME/.config/gridflux/config.json"
echo "Logs: journalctl --user -u gridflux -f"
echo ""
echo "GridFlux is now running in your graphical session."
