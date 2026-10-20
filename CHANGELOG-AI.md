# AI fork changelog

Changes in this fork (Raudlauk/Claude-kohi, branch `ai/main`) on top of upstream Kohi. Upstream's own history is in [CHANGELOG](CHANGELOG).

## Versioning

- The fork has its own version, `MAJOR.MINOR.PATCH` in [VERSION-AI](VERSION-AI) (the version being worked toward), separate from the upstream engine version in each module's `version.txt` (which stays at the upstream base, currently v0.13.0).
- Until 1.0.0, a MINOR bump may break the agent-facing behavior (config keys, console commands, log lines, the control channel). PATCH bumps do not.
- Releases are tagged `ai-vX.Y.Z` on `ai/main`.
- Every PR adds a line under the top heading. A release tags it, marks the heading with the date, then opens the next version in `VERSION-AI`.

## 0.1.0 (unreleased)

### Added
- Headless mode and a null renderer: runs with no display or GPU (#1).
- Fixed-step simulation clock with real-time and lockstep modes, pause and step (#2).
- Physics system with a Jolt backend: rigid bodies, raycasts, deterministic state save, restore and hash (#3).
- CI on Linux and Windows: debug and release builds, unit tests, and a headless run checked against a known physics hash (#4).
- `AGENTS.md`: how agents build, run and read results (#4).
- Agent control channel (`--agent`): JSON lines over stdin and stdout for stepping, input injection, console commands and physics hashes (#5).
- Agent commands for scenes, entities and physics bodies, and for UI controls by name (#6).
- Several engine processes can run at once: job semaphores are process-local, and `--log <path>` or `log_file` sets the log file (#7).
- `random_seed` app config key and `--seed` flag seed every engine RNG; the seed is logged and returned by the agent `status` command (#8).
- Lockstep mode runs jobs synchronously, so asset loads finish at the same step and in the same order on every run (#9).
- `--record` and `--replay` save and play back keyboard and mouse input per simulation step; `scripts/determinism_check.py` (run in CI) checks that a recorded run replays to the same physics hash. In the testbed physics scene, the arrow keys push the ball (#10).
- Agent command `scene_summary`: a plain-text description of a scene for agents that cannot see images (#12).
- Frame capture: the Vulkan renderer runs headless on a `VK_EXT_headless_surface` (lavapipe works with no GPU), and the agent command `screenshot` saves the last frame as a PNG. `scripts/frame_capture_demo.py` runs in CI on Linux (#14).
- Agent commands `world_save`, `world_restore` and `world_hash` snapshot and compare entity transforms together with the physics state (#11).
- Save games: `kscene_save_game` and `kscene_load_game` (agent commands `save_game` and `load_game`) write the scene, the physics state and any game state registered with `ksave_hook_register` to a checksummed `.ksav` file that a later process can load; `scripts/save_game_check.py` runs in CI (#15).
- KUI progress bar and slider controls (`kui_progress_bar_create`, `kui_slider_create`); the agent `ui_controls` lists their `value` and `ui_value_set` sets it; `scripts/ui_widgets_check.py` runs in CI (#16).
- KUI anchoring and resolution-independent layout: `kui_control_anchor_set` places a control relative to its parent, `kui_system_screen_size_set` and `kui_system_reference_size_set` (agent commands `ui_screen_size`, `ui_reference_size`) scale the whole UI to fit the window; `scripts/ui_layout_check.py` runs in CI (#17).
- KUI keyboard navigation: tab, arrows, enter/space and escape move focus and press controls (`kui_system_navigate`, which a gamepad layer can call too), with a focus highlight. Buttons with text, checkboxes and sliders are navigable; a slider uses left and right. Agent command `ui_navigate`; `scripts/ui_navigation_check.py` runs in CI (#18).
- KUI list and dropdown controls (`kui_list_create`, `kui_dropdown_create`) that work with the mouse, the wheel and keyboard navigation; the agent command `ui_select` picks an item and `ui_controls` lists `selected`, `items` and `open`; `scripts/ui_select_check.py` runs in CI
- Grid pathfinding: `nav_grid` with deterministic A* (`world/nav_grid.h`), unit tests, agent commands `nav_path` and `nav_block` for the grid the application registers (the testbed has a demo grid), and `scripts/nav_path_check.py` in CI

### Fixed
- The Vulkan renderer failed on lavapipe with `VK_ERROR_OUT_OF_HOST_MEMORY`: the custom allocator returned null for zero-size requests, which Mesa makes for empty barrier arrays (#14).
- OpenAL shutdown crash (#2).
- Release builds from a clean tree (#3).
- `kohi.core.tests` crashed after the first test; both test suites now pass and exit non-zero on failure (#4).
- `array_*_destroy` left the length, stride and data pointer set (#4).
- `dynamic_allocator_header_size` ignored the debug memory guards (#4).
- Windows builds again: `platform_win32.c` compile errors, a parallel-make link race, a dangling pointer in `kthread_create` that crashed on startup, and log output lost when stdout is redirected (#4).
- The headless testbed gives the same physics hash on GNU/Linux and Windows (#4).
- `console_command_execute` always reported failure (#5).
- The process exited 0 after a failed simulation step or a fatal renderer error; it now exits 2 (#6).
- BST delete lost values and left lookup roots dangling (upstream #271); the engine refuses to start when assets are missing, and exits 1 instead of SIGTRAP when start fails (upstream #281); OpenAL buffers are returned on unload and no longer handed to two owners (#7).
- Identifiers were seeded from the clock even in debug builds (#8).
