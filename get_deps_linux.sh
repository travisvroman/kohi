#!/bin/bash

if [ "$(id -u)" == "0" ]; then
	echo -e "\nget_deps_linux should not be run as root."
	echo -e "Please run this as a user with admin privileges.\n"
	exit 1
fi

detect_package_manager() {
	if command -v dnf >/dev/null 2>&1; then
		pkg_mgr_cmd="dnf"
		pkg_mgr_type="rpm"
	elif command -v yum >/dev/null 2>&1; then
		pkg_mgr_cmd="yum"
		pkg_mgr_type="rpm"
	elif command -v apt-get >/dev/null 2>&1; then
		pkg_mgr_cmd="apt-get"
		pkg_mgr_type="debian"
	elif command -v zypper >/dev/null 2>&1; then
		pkg_mgr_cmd="zypper"
		pkg_mgr_type="rpm"
	elif command -v pacman >/dev/null 2>&1; then
		pkg_mgr_cmd="pacman"
		pkg_mgr_type="arch"
	else
		echo "No supported package manager found. You will need to resolve dependencies on your own.\n"
		exit 1
	fi

	echo "Detected package manager: $pkg_mgr_cmd"
}

detect_package_manager

# Determine Distro and Version
. /etc/os-release

if [ $ID == "ubuntu" ]; then
	linux_ver=${VERSION_ID:0:2}
elif [[ $ID == "rhel" || $ID == "centos" ]]; then
	linux_ver=${VERSION_ID:0:1}
elif [ $ID == "ol" ]; then
	linux_ver=${VERSION_ID:0:1}
elif [ $ID == "fedora" ]; then
	linux_ver=${VERSION_ID}
fi

install_fedora_deps() {
	local fedora_ver=$1
	echo "Installing dependencies for Fedora $fedora_ver..."

	# Update package cache
	sudo dnf check-update >/dev/null 2>&1 || true

	if ! sudo $pkg_mgr_cmd install git make clang libX11-devel libxcb-devel libxkbcommon-devel systemd-devel openal-soft-devel vulkan-headers libshaderc-devel vulkan-validation-layers assimp-devel; then
		echo "Error: Failed to install dependencies for Fedora."
		return 1
	fi

	return 0
}

install_debian_deps() {

	# Update package cache
	sudo apt-get update >/dev/null 2>&1 || true

	if ! sudo apt-get install -y llvm git make libx11-dev libxkbcommon-x11-dev libx11-xcb-dev assimp openal; then
		echo "Error: Failed to install dependencies for Debian/Ubuntu."
		return 1
	fi

	return 0
}

install_arch_deps() {
	# Update package cache
	sudo pacman -Syu >/dev/null 2>&1 || true

	# TODO: This is definitely not the entire package list required for Arch...
	if ! sudo pacman -S llvm git make libx11 libxkbcommon-x11 pkgconf xcb-util xcb-util-keysyms assimp openal; then
		echo "Error: Failed to install dependencies for Debian/Ubuntu."
		return 1
	fi
}

if [ $pkg_mgr_type == "debian" ]; then
	install_debian_deps
elif [ $pkg_mgr_type == "rpm" ]; then 
	install_fedora_deps
elif [ $pkg_mgr_type == "arch" ]; then 
	install_arch_deps
fi

echo "Dependencies installed successfully."
