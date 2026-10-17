#!/usr/bin/env bash
# Compiles and packages Fable 2 Launcher for macOS into a native .app bundle.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
MAC_DIR="$REPO/launcher/mac"
OUT_BUILD="${1:-$REPO/out/build/mac-arm64-release}"
APP_BUNDLE="$OUT_BUILD/Fable 2 Launcher.app"
CONTENTS="$APP_BUNDLE/Contents"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"

echo "[Launcher] Building Fable 2 Launcher for macOS..."
mkdir -p "$MACOS" "$RESOURCES"

# 1. Compile Swift application
echo "[Launcher] Compiling main.swift with swiftc..."
swiftc -O -parse-as-library \
  -target arm64-apple-macos14.0 \
  "$MAC_DIR/main.swift" \
  -o "$MACOS/Fable 2 Launcher"

# Copy launcher helper script and game catalog to Resources
echo "[Launcher] Bundling helper scripts and catalog..."
cp "$REPO/tools/mac/fable2_launcher.py" "$RESOURCES/"
mkdir -p "$RESOURCES/constants"
cp "$REPO/constants/game_versions.json" "$RESOURCES/constants/"

# 2. Write Info.plist
cat <<EOF > "$CONTENTS/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>Fable 2 Launcher</string>
    <key>CFBundleIdentifier</key>
    <string>org.recomp.fable2.launcher</string>
    <key>CFBundleName</key>
    <string>Fable 2 Launcher</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
</dict>
</plist>
EOF

# 3. Create AppIcon.icns from assets/app-icon.png if present
if [ -f "$REPO/assets/app-icon.png" ]; then
  echo "[Launcher] Generating AppIcon.icns..."
  ICONSET="/tmp/fable2_appicon.iconset"
  rm -rf "$ICONSET" && mkdir -p "$ICONSET"
  sips -z 16 16     "$REPO/assets/app-icon.png" --out "$ICONSET/icon_16x16.png" >/dev/null 2>&1
  sips -z 32 32     "$REPO/assets/app-icon.png" --out "$ICONSET/icon_16x16@2x.png" >/dev/null 2>&1
  sips -z 32 32     "$REPO/assets/app-icon.png" --out "$ICONSET/icon_32x32.png" >/dev/null 2>&1
  sips -z 64 64     "$REPO/assets/app-icon.png" --out "$ICONSET/icon_32x32@2x.png" >/dev/null 2>&1
  sips -z 128 128   "$REPO/assets/app-icon.png" --out "$ICONSET/icon_128x128.png" >/dev/null 2>&1
  sips -z 256 256   "$REPO/assets/app-icon.png" --out "$ICONSET/icon_128x128@2x.png" >/dev/null 2>&1
  sips -z 256 256   "$REPO/assets/app-icon.png" --out "$ICONSET/icon_256x256.png" >/dev/null 2>&1
  sips -z 512 512   "$REPO/assets/app-icon.png" --out "$ICONSET/icon_256x256@2x.png" >/dev/null 2>&1
  sips -z 512 512   "$REPO/assets/app-icon.png" --out "$ICONSET/icon_512x512.png" >/dev/null 2>&1
  sips -z 1024 1024 "$REPO/assets/app-icon.png" --out "$ICONSET/icon_512x512@2x.png" >/dev/null 2>&1
  iconutil -c icns "$ICONSET" -o "$RESOURCES/AppIcon.icns"
  rm -rf "$ICONSET"
fi

# 4. Ad-hoc code sign for local execution
echo "[Launcher] Signing .app bundle..."
codesign --force --deep --sign - "$APP_BUNDLE"

echo "[Launcher] Build successful: $APP_BUNDLE"
