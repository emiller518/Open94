#!/bin/bash
# Package the native shell sources + ROM into dist/ for the Mac to pull.
set -e
cd "$(dirname "$0")/.."
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/nhl94-native"
cp -r native/app native/harness "$STAGE/nhl94-native/"
rm -rf "$STAGE/nhl94-native/app/build" "$STAGE/nhl94-native/app/nhl94" \
       "$STAGE/nhl94-native/harness/build" "$STAGE/nhl94-native/harness/harness"
mkdir -p "$STAGE/nhl94-native/vendor"
cp -r native/vendor/Genesis-Plus-GX "$STAGE/nhl94-native/vendor/"
rm -rf "$STAGE/nhl94-native/vendor/Genesis-Plus-GX/.git"
cp native/README-mac.md "$STAGE/nhl94-native/README.md"
cp "dist/nhl94-build.bin" "$STAGE/nhl94-native/nhl94-build.bin"

mkdir -p dist
tar -C "$STAGE" -czf dist/nhl94-native-src.tar.gz nhl94-native
ls -la dist/nhl94-native-src.tar.gz
echo "Mac: curl -O http://192.168.0.72:8016/nhl94-native-src.tar.gz"
