#!/bin/sh
set -eu

# Parameters
ARCH=$1
VERSION=$2
export ARCH VERSION
export OUTPATH=dist
#export ADD_HOOKS="self-updater.hook"
export GITHUB_REPOSITORY="https://github.com/ivan-hc/AM"
export UPINFO="gh-releases-zsync|${GITHUB_REPOSITORY%/*}|${GITHUB_REPOSITORY#*/}|latest|*$ARCH.AppImage.zsync"
export ICON=windwaker-hd-editor.png
export DESKTOP=appimage/windwaker-hd-editor.desktop
export MAIN_BIN=windwaker-hd-editor
export APPDIR="AppDir"

# Prepare files
rm -rf "$APPDIR"
mkdir -p "$APPDIR"/bin "$APPDIR"/share
# Copy AM files
cp windwaker-hd-editor "$APPDIR"/bin/.
cp windwaker-hd-editor-cli "$APPDIR"/bin/.
chmod a+x "$APPDIR"/bin/*

# Deploy dependencies
quick-sharun "$APPDIR"

# Insert env variables, 3rd line onwards
sed -i '3i\USE_HOST_XDG_CACHE_HOME=1' "$APPDIR"/AppRun.lib

# Turn AppDir into AppImage
quick-sharun --make-appimage

# Rename files
mv -f dist/*-"$ARCH".AppImage dist/windwaker-hd-editor-linux-"$ARCH".AppImage
mv -f dist/*-"$ARCH".AppImage.zsync dist/windwaker-hd-editor-linux-"$ARCH".AppImage.zsync

# Test the app for 12 seconds
quick-sharun --simple-test dist/*.AppImage

