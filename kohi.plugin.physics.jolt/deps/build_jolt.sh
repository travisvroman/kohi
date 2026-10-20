#!/bin/bash
# Fetches and builds Jolt Physics and its C wrapper (joltc) for kohi.plugin.physics.jolt.
#
# Output (relative to the repository root, git-ignored):
#   vendor/jolt/include/joltc.h
#   vendor/jolt/lib/libjoltc.so   (libjoltc.dylib on macOS; Jolt is linked into it statically)
#
# Versions are pinned so every machine builds the same library. Jolt is built with
# CROSS_PLATFORM_DETERMINISTIC=ON so a simulation gives identical results on every platform.
#
# Usage: kohi.plugin.physics.jolt/deps/build_jolt.sh   (from the repository root; needs git, cmake, clang)
set -euo pipefail

JOLT_TAG="v5.6.0"
JOLTC_COMMIT="2bbcf73b1bec261e0351814662922f1b4784454c" # amerkoleci/joltc, 2026-10-15, on Jolt v5.6.0

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PATCH="$REPO_ROOT/kohi.plugin.physics.jolt/deps/joltc-state-save-restore.patch"
VENDOR="$REPO_ROOT/vendor/jolt"
SRC="$VENDOR/src"
CC_BIN="${CC:-clang}"
CXX_BIN="${CXX:-clang++}"

mkdir -p "$SRC" "$VENDOR/include" "$VENDOR/lib"

if [ ! -d "$SRC/JoltPhysics/.git" ]; then
	git clone --quiet --depth 1 --branch "$JOLT_TAG" https://github.com/jrouwe/JoltPhysics.git "$SRC/JoltPhysics"
fi

if [ ! -d "$SRC/joltc/.git" ]; then
	git clone --quiet https://github.com/amerkoleci/joltc.git "$SRC/joltc"
fi
git -C "$SRC/joltc" checkout --quiet --force "$JOLTC_COMMIT"
# Adds JPH_PhysicsSystem_SaveState/RestoreState, which joltc does not expose yet.
git -C "$SRC/joltc" apply "$PATCH"

cmake -S "$SRC/joltc" -B "$SRC/build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_COMPILER="$CC_BIN" \
	-DCMAKE_CXX_COMPILER="$CXX_BIN" \
	-DJOLT_PHYSICS_ROOT="$SRC/JoltPhysics" \
	-DCROSS_PLATFORM_DETERMINISTIC=ON \
	-DJPH_BUILD_SHARED=ON \
	-DJPH_SAMPLES=OFF \
	-DJPH_USE_VK=OFF -DJPH_USE_DX12=OFF -DJPH_USE_MTL=OFF \
	> "$SRC/cmake.log"
cmake --build "$SRC/build" --parallel > "$SRC/build.log"

cp "$SRC/joltc/include/joltc.h" "$VENDOR/include/"
cp "$SRC"/build/lib/libjoltc.* "$VENDOR/lib/"
echo "Built joltc (Jolt $JOLT_TAG) into $VENDOR"
