#!/bin/bash
# Package the native Bike or Die 2 build as a macOS .app bundle.
set -euo pipefail
ROOT=/Users/tonyradtke/dev/bike_or_die
PK=$ROOT/vendor/PumpkinOS
OUT=${1:-$ROOT/build/Bike or Die 2.app}
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources/runtime"

# runtime tree: the pumpkin binary, its plugins, boot scripts and the seeded VFS
cp "$PK/pumpkin"                    "$OUT/Contents/Resources/runtime/"
cp -R "$PK/bin"                     "$OUT/Contents/Resources/runtime/"
cp -R "$PK/script"                  "$OUT/Contents/Resources/runtime/"
cp -R "$PK/vfs"                     "$OUT/Contents/Resources/runtime/"

cat > "$OUT/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>              <string>Bike or Die 2</string>
  <key>CFBundleDisplayName</key>       <string>Bike or Die 2</string>
  <key>CFBundleIdentifier</key>        <string>local.bikeordie2</string>
  <key>CFBundleVersion</key>           <string>2.1b</string>
  <key>CFBundleShortVersionString</key><string>2.1b</string>
  <key>CFBundlePackageType</key>       <string>APPL</string>
  <key>CFBundleExecutable</key>        <string>BikeOrDie</string>
  <key>CFBundleIconFile</key>          <string>AppIcon</string>
  <!-- The game is 320x320 nearest-neighbour pixel art scaled by an integer
       factor. A Retina backing store makes SDL's renderer output size 2x the
       window size, which its logical-size scaling does not account for, and the
       screen comes out magnified and clipped. 1x backing keeps it exact. -->
  <key>NSHighResolutionCapable</key>   <false/>
  <key>LSMinimumSystemVersion</key>    <string>11.0</string>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
PLIST

cat > "$OUT/Contents/MacOS/BikeOrDie" <<'LAUNCH'
#!/bin/bash
# Bike or Die 2 -- native launcher.
# The runtime writes into its VFS, so the first run copies the pristine tree
# into Application Support and everything after that runs from there.
set -u
HERE="$(cd "$(dirname "$0")/../Resources/runtime" && pwd)"
# Optional overrides, so the app can be configured when launched via Finder/open.
[ -f "$HOME/.bikeordie.env" ] && . "$HOME/.bikeordie.env"
DATA="$HOME/Library/Application Support/Bike or Die 2"

if [ ! -d "$DATA/vfs" ] || [ "${BOD_RESET:-0}" = "1" ]; then
  rm -rf "$DATA"
  mkdir -p "$DATA"
  cp -R "$HERE/vfs" "$DATA/vfs"
fi
mkdir -p "$DATA"
ln -sfn "$HERE/bin"    "$DATA/bin"
ln -sfn "$HERE/script" "$DATA/script"
cp -f "$HERE/pumpkin" "$DATA/pumpkin" 2>/dev/null || true

cd "$DATA"
export DYLD_LIBRARY_PATH="$HERE/bin"
# Display: the game's ARM blitter writes little-endian RGB565.
export PUMPKIN_DISPLAY_LE="${PUMPKIN_DISPLAY_LE:-1}"
export PUMPKIN_SOUND="${PUMPKIN_SOUND:-1}"
# HotSync user name; the game's registration code is keyed to it.
export PUMPKIN_USER="${PUMPKIN_USER:-PalmDB}"
# Use the statically recompiled 68k and ARM cores.
export BOD_RECOMP="${BOD_RECOMP:-1}"
export BOD_ARM_ENGINE="${BOD_ARM_ENGINE:-recomp}"
exec "$DATA/pumpkin" -d 1 -f "$DATA/pumpkin.log" -s libscriptlua "$HERE/script/bod.lua"
LAUNCH
chmod +x "$OUT/Contents/MacOS/BikeOrDie"
echo "built: $OUT"
