#!/bin/bash
# Makes a minimal stand-in for the LunarG Vulkan SDK from distro packages
# (libvulkan-dev, libshaderc-dev), so CI and sandboxes can build without the
# full SDK download.
#
# Creates vendor/vulkan-shim and links vendor/vulkan/1.0/x86_64 to it, which is
# where the Makefile copies libshaderc_shared from.
#
# Usage: source .github/scripts/linux-vulkan-shim.sh   (from the repository root)
# Sourcing exports VULKAN_SDK; running it prints the export line instead.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
shim="$repo_root/vendor/vulkan-shim"
multiarch="$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)"
shaderc="/usr/lib/$multiarch/libshaderc.so.1"

if [ ! -f "$shaderc" ]; then
	echo "Missing $shaderc: install libshaderc-dev." >&2
	exit 1
fi

mkdir -p "$shim/lib" "$repo_root/vendor/vulkan/1.0"
ln -sfn /usr/include "$shim/include"
ln -sf "$shaderc" "$shim/lib/libshaderc_shared.so"
ln -sf "$shaderc" "$shim/lib/libshaderc_shared.so.1"
ln -sfn "$shim" "$repo_root/vendor/vulkan/1.0/x86_64"

if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
	export VULKAN_SDK="$shim"
	set +euo pipefail
else
	echo "export VULKAN_SDK=$shim"
fi
