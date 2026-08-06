#!/bin/bash

if [ "$(id -u)" == "0" ]; then
	echo -e "\nget_deps_macos should not be run as root."
	echo -e "Please run this as a user with admin privileges.\n"
	exit 1
fi

detect_package_manager() {
	if command -v brew >/dev/null 2>&1; then
		pkg_mgr_cmd="brew"
		pkg_mgr_type="homebrew"
	else
		echo "No supported package manager found. You will need to resolve dependencies on your own.\n"
		exit 1
	fi

	echo "Detected package manager: $pkg_mgr_cmd"
}

detect_package_manager

install_macos_deps() {
	local fedora_ver=$1
	echo "Installing dependencies for macOS..."

	# Ensure xcode is installed, which will install other dev dependencies required.
	xcode-select --install

	# Update package cache
	sudo brew update >/dev/null 2>&1 || true

	if ! sudo brew install git make assimp openal-soft; then
		echo "Error: Failed to install dependencies for Fedora."
		return 1
	fi

	return 0
}

install_macos_deps

echo "Dependencies installed successfully."
