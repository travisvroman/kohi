# Kohi for AI agents

This fork of Kohi is meant to be built, run and tested by AI agents. This page is the short path: how to build in a fresh machine, run the engine without a display, and read the results. The long-form docs are in [readme.md](readme.md); plans are in [ROADMAP-AI.md](ROADMAP-AI.md); changes are in [CHANGELOG-AI.md](CHANGELOG-AI.md).

Commands run from the repository root unless a step says `cd bin`.

## Build on GNU/Linux (Ubuntu 24.04)

No LunarG SDK or desktop is needed. This is exactly what CI does ([.github/workflows/ci.yml](.github/workflows/ci.yml)).

```sh
sudo apt-get install -y clang-19 cmake libx11-dev libx11-xcb-dev libxkbcommon-x11-dev libdbus-1-dev \
  libopenal-dev libassimp-dev libvulkan-dev libshaderc-dev libstdc++-14-dev

# The Makefiles call `clang` and need clang >= 19.
mkdir -p ~/clang-bin && ln -sf /usr/bin/clang-19 ~/clang-bin/clang && ln -sf /usr/bin/clang++-19 ~/clang-bin/clang++
export PATH=~/clang-bin:$PATH

# Stand-in for the Vulkan SDK, built from the distro packages. Sets VULKAN_SDK.
source .github/scripts/linux-vulkan-shim.sh

make getdeps-jolt                          # once: builds Jolt Physics into vendor/jolt
make scaffold utils-debug copy-utils       # once per clean tree
make build-debug -j"$(nproc)"              # or build-release
./import-updated-assets.sh                 # once, and after asset changes
```

Run `make clean` before switching between debug and release. Objects from the two builds have different struct layouts and mixing them causes crashes that look like engine bugs.

`./import-updated-assets.sh` may rewrite committed `.kmt` files under `testbed.kapp/assets/materials`. Restore them with `git checkout -- testbed.kapp` before committing.

## Build on Windows

See the `windows` job in [.github/workflows/ci.yml](.github/workflows/ci.yml): clang, GNU make (from cmd, not Git Bash), the LunarG Vulkan SDK, OpenAL Soft in the OpenAL 1.1 SDK layout, assimp from vcpkg with `ASSIMP` pointing at it, and `kohi.plugin.physics.jolt/deps/build_jolt.sh` run from Git Bash.

## Test

```sh
cd bin
./kohi.core.tests      # containers, allocators, strings, KSON parser
./kohi.runtime.tests   # scene entities, simulation clock
```

Each prints `Results: N passed, M failed, K skipped.` and exits non-zero if any test failed. Register new tests in `kohi.core.tests/src/main.c` or `kohi.runtime.tests/src/runtime_tests_main.c`.

## Run headless

```sh
cd bin
ALSOFT_DRIVERS=null ./testbed.kapp --config ../testbed.kapp/app_config_headless.kson
```

- `--config <path>` picks the application config.
- `--log <path>` picks the log file (default `console.log`, or `log_file` in the config). Give each instance its own when running several.
- `--seed <n>` (or `random_seed` in the config) seeds every engine RNG. The seed in use is logged as `Random seed: <n>` at startup; pass it back to repeat a run.
- `headless = true` needs no display or GPU. CI runs it on GNU/Linux and Windows. Pair it with the null renderer.
- `fixed_step_hz = 60` and lockstep mode (the headless default) give exactly one 1/60 s step per frame, as fast as the CPU allows.
- `max_frames = 600` ends the run after 600 frames.
- `ALSOFT_DRIVERS=null` lets OpenAL start with no audio device.

All options are described in [readme.md, Running headless](readme.md#running-headless).

## Read the results

- **Exit code**: 0 on a clean shutdown, 1 when the engine fails to start. It refuses to start if any asset in a manifest is missing, and lists each one; run `./import-updated-assets.sh` first.
- **Log**: everything goes to stdout and to `console.log` in the working directory (or the `--log` path). Lines are `[LEVEL]: message` with ANSI colors; strip them with `sed 's/\x1b\[[0-9;]*m//g'`.
- **Physics state hash**: at shutdown the testbed logs `Physics state hash: <16 hex digits>`. The same config and inputs give the same hash on every run and thread count. Jolt is built to be cross-platform deterministic, and CI checks the same hash on Linux and Windows. After 600 steps of the headless testbed it is `ec2e3122759e69de`.
- **Smoke check**: `.github/scripts/headless-smoke.sh <expected-hash>` runs the headless testbed and fails unless it reaches 600 steps with that hash. CI runs it on every PR.

If your change alters the simulation on purpose, update `EXPECTED_PHYSICS_HASH` in `.github/workflows/ci.yml` in the same PR and say why.

## Control a run

There is no live control channel yet; today a run is controlled through its config file and the console commands it executes. Input injection, a JSON-lines control channel and world queries are phase 1 of [ROADMAP-AI.md](ROADMAP-AI.md), and this section will cover them when they land.

## Known limits

- Several engine processes can run at once on one machine; give each its own `--log` file.
- The Vulkan renderer fails on lavapipe (software Vulkan). Use the null renderer for headless runs.
