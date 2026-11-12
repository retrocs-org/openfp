#!/bin/sh
# SPDX-License-Identifier: MIT
# The game for macOS: OpenFirePower.app, universal (Apple Silicon and Intel), with SDL2
# inside, signed, in a DMG to download:
#   tools/package_macos.sh -> openfp-client-macos-universal-VERSION.{dmg,zip}
# Run it on a Mac with the Xcode command line tools (xcode-select --install) and cmake
# (brew install cmake). SDL2 comes from SDL's own release (its universal framework,
# pinned, checksum checked); Homebrew's SDL2 is for one kind of Mac only.
#
# Signing: ad hoc by default (fine on your own Macs; others get "can't be checked" the
# first time: right-click, Open). With an Apple Developer ID:
#   SIGN_ID="Developer ID Application: Your Name (TEAMID)" tools/package_macos.sh
# and to notarize too (Gatekeeper then lets it straight in), with a stored profile from
#   xcrun notarytool store-credentials ofp --apple-id YOU --team-id TEAMID:
#   SIGN_ID="..." NOTARY_PROFILE=ofp tools/package_macos.sh
set -eu
PYTHON=${PYTHON:-python3}
cd "$(dirname "$0")/.."
[ "$(uname)" = Darwin ] || { echo "package_macos.sh: on a Mac only" >&2; exit 2; }
SDL_VER=2.32.10
SDL_SHA=4a7ac31640d70214e848f994be8a12849c0f97918a7e6c2e27a40036166d1a7f
WORK=${WORK:-work/package/macos}
BUILD_DIR=${OFP_BUILD_DIR:-"$HOME/Development/builds/OpenFirePower/macOS"}
PACKAGE_DIR=${OFP_PACKAGE_DIR:-"$HOME/Development/packages/OpenFirePower/macOS"}
SDK_CACHE=${SDL2_CACHE_DIR:-"$HOME/Development/tools/macos/SDL2"}
VER=$(sed -n 's/^#define OFP_VERSION "\(.*\)"/\1/p' src/common/net.h)
mkdir -p "$WORK" "$BUILD_DIR" "$PACKAGE_DIR" "$SDK_CACHE"
WORK=$(cd "$WORK" && pwd)
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
PACKAGE_DIR=$(cd "$PACKAGE_DIR" && pwd)
SDK_CACHE=$(cd "$SDK_CACHE" && pwd)
if [ ! -d "$SDK_CACHE/SDL2.framework" ]; then
    curl -sL -o "$SDK_CACHE/sdl.dmg" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.dmg"
    echo "$SDL_SHA  $SDK_CACHE/sdl.dmg" | shasum -a 256 -c -
    MNT=$(mktemp -d)
    hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$SDK_CACHE/sdl.dmg" > /dev/null
    cp -R "$MNT/SDL2.framework" "$SDK_CACHE/"
    hdiutil detach "$MNT" > /dev/null
fi
cmake -S . -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release -DOFP_BUILD_SERVER=OFF -DOFP_MACOS_BUNDLE=ON \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
      -DSDL2_DIR="$SDK_CACHE/SDL2.framework/Resources/CMake" -DCMAKE_FRAMEWORK_PATH="$SDK_CACHE" \
      -DCMAKE_BUILD_RPATH="@executable_path/../Frameworks" -DOFP_DATA_DIR=data
cmake --build "$WORK/build" -j"$(sysctl -n hw.ncpu)" --target openfirepower
APP="$BUILD_DIR/OpenFirePower.app"
rm -rf "$APP"
cp -R "$WORK/build/openfirepower.app" "$APP"
mkdir -p "$APP/Contents/Frameworks"
cp -R "$SDK_CACHE/SDL2.framework" "$APP/Contents/Frameworks/"
# (the game looks for SDL2 beside it, inside the app)
install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP/Contents/MacOS/openfirepower" 2>/dev/null || true
cp LICENSE CREDITS.md THIRD-PARTY-NOTICES.md "$APP/Contents/Resources/"
lipo -info "$APP/Contents/MacOS/openfirepower"
SIGN=${SIGN_ID:--}
OPTS="--force --timestamp --options runtime"
[ "$SIGN" = "-" ] && OPTS="--force"
codesign $OPTS -s "$SIGN" "$APP/Contents/Frameworks/SDL2.framework"
codesign $OPTS -s "$SIGN" "$APP"
codesign --verify --deep --strict "$APP" && echo "signed: $SIGN"
if [ -n "${NOTARY_PROFILE:-}" ]; then
    [ "$SIGN" != "-" ] || { echo "Notarization requires a Developer ID signing identity" >&2; exit 2; }
    # Staple the app too: the portable ZIP must contain the same notarized app.
    ditto -c -k --keepParent "$APP" "$WORK/notary-app.zip"
    xcrun notarytool submit "$WORK/notary-app.zip" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"
fi
DMG="$PACKAGE_DIR/$("$PYTHON" scripts/release_assets.py name client macos universal "$VER" dmg)"
rm -f "$DMG"
STAGE=$(mktemp -d)
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"            # (drag it there)
hdiutil create -volname "OpenFP $VER" -srcfolder "$STAGE" -ov -format UDZO "$DMG" > /dev/null
rm -rf "$STAGE"
if [ -n "${NOTARY_PROFILE:-}" ] && [ "$SIGN" != "-" ]; then
    codesign --force --timestamp -s "$SIGN" "$DMG"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
    xcrun stapler validate "$DMG"
fi
ZIP="$PACKAGE_DIR/$("$PYTHON" scripts/release_assets.py name client macos universal "$VER" zip)"
ditto -c -k --keepParent "$APP" "$ZIP"
"$PYTHON" scripts/release_assets.py checksum "$DMG" "$ZIP"
echo "Ready app: $APP"
echo "built $ZIP"
echo "built $DMG ($(du -h "$DMG" | cut -f1))"
