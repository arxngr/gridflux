#!/usr/bin/env bash
set -e

INSTALL_DIR="/usr/local/bin"
SERVICE_NAME="gridflux.service"
SERVICE_PATH="$HOME/.config/systemd/user/$SERVICE_NAME"
BUILD_DIR="build"
AUTOSTART_FILE="$HOME/.config/autostart/gridflux.desktop"

echo "=== GridFlux Installation Started ==="

detect_desktop() {
    DESKTOP="${XDG_CURRENT_DESKTOP:-Unknown}"
    SESSION_TYPE="${XDG_SESSION_TYPE:-x11}"

    echo "Detected DE: $DESKTOP"
    echo "Session type: $SESSION_TYPE"

    if [[ "$DESKTOP" == *"KDE"* ]] || [[ "$KDE_FULL_SESSION" == "true" ]]; then
        IS_KDE=1
    else
        IS_KDE=0
    fi

    if [[ "$DESKTOP" == *"GNOME"* ]]; then
        IS_GNOME=1
    else
        IS_GNOME=0
    fi
}

install_dependencies() {
    echo "Installing dependencies..."

    . /etc/os-release

    case "$ID" in
        ubuntu|debian)
            sudo apt update -y
            sudo apt install -y libx11-dev libjson-c-dev cmake gcc make pkg-config
            ;;
        fedora|rhel|centos|almalinux|rocky)
            sudo dnf install -y libX11-devel json-c-devel cmake gcc make pkgconfig
            ;;
        arch|manjaro)
            sudo pacman -Syu --noconfirm libx11 json-c cmake gcc make pkgconf
            ;;
        opensuse*|sles)
            sudo zypper install -y libX11-devel libjson-c-devel cmake gcc make pkg-config
            ;;
        alpine)
            sudo apk add libx11-dev json-c-dev cmake gcc make pkgconfig
            ;;
        *)
            echo "Unsupported distro: $ID"
            echo "Install manually: libx11-dev libjson-c-dev cmake gcc make pkg-config"
            ;;
    esac

    echo "Verifying json-c..."
    if ! pkg-config --exists json-c; then
        echo "json-c missing — cannot continue."
        exit 1
    fi
}

build_and_install() {
    echo "Building GridFlux..."

    rm -rf "$BUILD_DIR" CMakeCache.txt CMakeFiles/
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .
    cmake --build "$BUILD_DIR" -- -j$(nproc)

    sudo install -Dm755 "$BUILD_DIR/gridflux" "$INSTALL_DIR/gridflux"
    echo "Binary installed: $INSTALL_DIR/gridflux"
}

install_kde_autostart() {
    echo "Installing KDE autostart entry..."

    mkdir -p "$(dirname "$AUTOSTART_FILE")"

    cat > "$AUTOSTART_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=GridFlux
Comment=KDE-safe GridFlux tiling assistant
Exec=sh -c 'sleep 7 && $INSTALL_DIR/gridflux'
OnlyShowIn=KDE;
X-KDE-autostart-after=plasmashell
Terminal=false
StartupNotify=false
EOF

    echo "✓ KDE autostart installed at $AUTOSTART_FILE"
}

install_gnome_service() {
    echo "Installing GNOME systemd user service..."

    mkdir -p "$(dirname "$SERVICE_PATH")"

    cat >"$SERVICE_PATH" <<EOF
[Unit]
Description=GridFlux Window Tiling Service
After=graphical-session.target

[Service]
ExecStart=$INSTALL_DIR/gridflux
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
EOF

    systemctl --user daemon-reload
    systemctl --user enable --now "$SERVICE_NAME"

    echo "✓ GNOME systemd service enabled."
}

create_default_config() {
    CONFIG_DIR="$HOME/.config/gridflux"
    CONFIG_FILE="$CONFIG_DIR/config.json"

    mkdir -p "$CONFIG_DIR"

    if [[ ! -f "$CONFIG_FILE" ]]; then
        cat > "$CONFIG_FILE" <<EOF
{
  "max_windows_per_workspace": 10,
  "max_workspaces": 10,
  "default_padding": 5,
  "min_window_size": 100
}
EOF
        echo "✓ Created config: $CONFIG_FILE"
    else
        echo "✓ Config already exists."
    fi
}

detect_desktop
install_dependencies
build_and_install
create_default_config

if [[ $IS_KDE -eq 1 ]]; then
    echo "KDE detected — using KDE autostart (NO systemd)."
    install_kde_autostart
elif [[ $IS_GNOME -eq 1 ]]; then
    echo "GNOME detected — installing systemd service."
    install_gnome_service
else
    echo "Unknown desktop environment — not installing autostart."
    echo "Start manually with: gridflux &"
fi

echo ""
echo "=== Installation Complete ==="
echo "Config file: ~/.config/gridflux/config.json"
echo "KDE autostart: ~/.config/autostart/gridflux.desktop"
echo "GNOME service: systemctl --user status gridflux"

