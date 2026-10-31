#!/bin/sh

set -eu

ARCH=$(uname -m)

echo "Initializing the build container..."
echo "---------------------------------------------------------------"
pacman-key --init
pacman -Syy --noconfirm archlinux-keyring
pacman -Syu --noconfirm

echo "Installing package dependencies..."
echo "---------------------------------------------------------------"
# The required set is present on every architecture (Arch Linux ports).
pacman -S --noconfirm --needed \
    base-devel ca-certificates curl git jq patchelf tar zip unzip wget xz \
    glfw-x11 libx11 libxext libxcursor libxi libxinerama libxrandr mesa

# Optional packages may not exist on every architecture port.
# xorg-server-xvfb is only used by quick-sharun's strace mode.
pacman -S --noconfirm --needed 7zip file xorg-server-xvfb \
	|| echo "  WARNING: some optional packages are not available on $ARCH"

echo "Installing quick-sharun..."
echo "---------------------------------------------------------------"
wget -q -O /usr/local/bin/quick-sharun \
	https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/refs/heads/main/useful-tools/quick-sharun.sh
	#https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/f6627df7a7aa48c9c614999fa46fb5707ca9b611/useful-tools/quick-sharun.sh
chmod a+x /usr/local/bin/quick-sharun

echo "Getting debloated packages..."
wget -q -O /usr/local/bin/get-debloated-pkgs \
	https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/refs/heads/main/useful-tools/get-debloated-pkgs.sh
chmod a+x /usr/local/bin/get-debloated-pkgs

# Installs a debloated MESA, Vulkan, Qt, GTK, libicudata, and more
get-debloated-pkgs --add-mesa --prefer-nano

echo "CONTAINER IS READY!"
