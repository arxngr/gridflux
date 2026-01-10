#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
PACKAGE_DIR="$PROJECT_ROOT/packages"

echo "=== GridFlux Package Builder ==="

# Clean previous builds
rm -rf "$BUILD_DIR"
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

# Configure and build
echo "→ Configuring build..."
cd "$PROJECT_ROOT"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "→ Building project..."
cmake --build "$BUILD_DIR" -- -j$(nproc)

# Create packages
echo "→ Creating packages..."
cd "$BUILD_DIR"

# DEB package (Ubuntu/Debian)
if command -v dpkg-deb >/dev/null 2>&1; then
    echo "  • Creating DEB package..."
    cpack -G DEB
    mv *.deb "$PACKAGE_DIR/" 2>/dev/null || true
    echo "  ✓ DEB package created"
fi

# RPM package (Fedora/RHEL/CentOS)
if command -v rpmbuild >/dev/null 2>&1; then
    echo "  • Creating RPM package..."
    cpack -G RPM
    mv *.rpm "$PACKAGE_DIR/" 2>/dev/null || true
    echo "  ✓ RPM package created"
fi

# Archive package (universal)
echo "  • Creating TAR archive..."
mkdir -p "$PACKAGE_DIR/gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")"
cp -r "$BUILD_DIR"/gridflux* "$PACKAGE_DIR/gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")/" 2>/dev/null || true
cp -r "$PROJECT_ROOT"/{README.md,LICENSE,scripts,icons} "$PACKAGE_DIR/gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")/" 2>/dev/null || true

# Add service setup script for packages
cat > "$PACKAGE_DIR/gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")/setup-service.sh" << 'EOF'
#!/bin/bash
set -e
INSTALL_DIR="/usr/local/bin"
SERVICE_FILE="$HOME/.config/systemd/user/gridflux.service"

echo "Setting up GridFlux service..."

# Create systemd service
mkdir -p "$(dirname "$SERVICE_FILE")"
cat > "$SERVICE_FILE" << 'EOL'
[Unit]
Description=GridFlux Window Tiler
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=$INSTALL_DIR/gridflux
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
EOL

# Enable and start service
systemctl --user daemon-reload
systemctl --user enable gridflux.service
systemctl --user start gridflux

echo "✓ GridFlux service is now running!"
echo "Check status: systemctl --user status gridflux"
EOF

chmod +x "$PACKAGE_DIR/gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")/setup-service.sh"

cd "$PACKAGE_DIR"
tar -czf "gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")-$(uname -s)-$(uname -m).tar.gz" "gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")/"
rm -rf "gridflux-$(git describe --tags --always 2>/dev/null || echo "2.0.0")"
echo "  ✓ TAR archive created"

echo ""
echo "=== Packaging Complete ==="
echo "Package directory: $PACKAGE_DIR"
ls -la "$PACKAGE_DIR"

echo ""
echo "📦 Available Packages:"
for file in "$PACKAGE_DIR"/*; do
    if [ -f "$file" ]; then
        case "$file" in
            *.deb) echo "  • DEB: $(basename "$file") - For Ubuntu/Debian systems" ;;
            *.rpm) echo "  • RPM: $(basename "$file") - For Fedora/RHEL/CentOS systems" ;;
            *.tar.gz) echo "  • TAR: $(basename "$file") - Universal archive" ;;
        esac
    fi
done

echo ""
echo "🚀 Installation Instructions:"
echo "  DEB: sudo dpkg -i gridflux_*.deb && ./gridflux-*/setup-service.sh"
echo "  RPM: sudo rpm -i gridflux-*.rpm && ./gridflux-*/setup-service.sh"
echo "  TAR: tar -xzf gridflux-*.tar.gz && cd gridflux-* && sudo ./scripts/install.sh"
