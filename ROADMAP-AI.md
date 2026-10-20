# AI Engine Roadmap

This fork turns Kohi into an engine that AI agents can drive: build, run, observe and test a game without a human at the screen. Upstream's own plans stay in [TODO.md](TODO.md).

The phases are in priority order. Each one builds on the one before it.

## Done

- [x] Headless mode with a null renderer: runs with no display or GPU (#1).
- [x] Fixed-step simulation clock, separate from the render frame rate, with real-time and lockstep modes, plus pause and step controls (#2).
- [x] OpenAL shutdown crash fixed (#2).
- [x] Physics system with a Jolt backend: rigid bodies, scene entities, raycasts, deterministic state save/restore/hash (#3).
- [x] Release builds work again from a clean tree (#3).
- [x] CI on Linux and Windows, passing test suites, fork versioning and an agent guide (#4).

## 1. Agent API

An agent needs one stable way to control a run and see what happened.

- [x] Input injection: export `input_process_*` (or a new input queue) so agents can press keys and buttons, move the mouse and drive gamepads, tied to simulation steps.
- [x] Control channel: a socket or stdin/stdout protocol (JSON lines) for console commands, stepping, pausing and querying state, so an agent drives a running engine instead of editing config files.
- [x] World queries: list entities with type, name, transform and physics state; find entities by name (by tag: not yet).
- [x] Text observation: the `scene_summary` command describes a scene in plain text (#12).
- [ ] Frame capture: capture frames to images in headless runs. Plan: Vulkan on lavapipe (software Vulkan) with an offscreen or headless surface and readback; needs the custom allocator fix and a surface path first.
- [x] UI by name: list controls, read their text and state, activate them.
- [x] Non-zero exit codes on failure (0 clean, 1 failed to start, 2 stopped on an error). Structured logs: not yet.

## 2. Full determinism

The same inputs and seed must give the same run, every time, so agent experiments can be replayed and compared.

- [x] A seed for every RNG: `random_seed` in the app config or `--seed`, logged at startup and reported by the agent `status` command.
- [x] Load assets synchronously in lockstep mode, so they finish at the same step and in the same order every run.
- [x] Record and replay: `--record` saves keyboard and mouse input per step, `--replay` plays it back to the same end state. Recording agent commands beyond input: not yet.
- [x] World snapshot/restore beyond physics: entity transforms plus physics state (`world_save`, `world_restore`, `world_hash`). Game-specific state: not yet, the application would need a hook.
- [x] Determinism test in CI: `scripts/determinism_check.py` records a run with input, replays it twice and compares physics hashes and the re-recorded input.

## 3. Parallel runs

Agents run many instances at once (search, training, testing).

- [x] Replace named `sem_open` semaphores with process-local ones; parallel processes collided before.
- [x] Per-instance log path: `--log <path>` on the command line or `log_file` in the app config.
- [x] Prove two or more headless instances run side by side with identical results (six at once, same physics hash).
- [ ] Longer term: move singleton engine systems into a context, so several worlds can share one process.

## 4. Known bugs

- [x] Upstream #271: `u32_bst_delete`/`u64_bst_delete` copied the key but not the value, and callers ignored the returned root (left `scene->name_lookup` dangling).
- [x] Upstream #281: missing assets. The engine now refuses to start when any manifest asset file is missing, lists each one, and says to run the importer.
- [x] OpenAL buffers: `openal_backend_find_free_buffer`'s reclaim path indexed `sources[-1]` and handed out buffers still owned by loaded audio, and unloading never returned buffers. Buffers now go back to the free list on unload, and running out is a clear error.
- [ ] Vulkan fails on lavapipe (software Vulkan) with `VK_ERROR_OUT_OF_HOST_MEMORY` from the custom allocator.
- [ ] `prepare_frame` failure skips the frame without ending the open command list.

## 5. Release hygiene

- [x] CI on Linux and Windows: build debug and release from clean, run unit tests, run the headless demo and check its physics hash.
- [x] Make the existing test suites pass.
- [ ] Add tests for new systems (physics; the sim clock has some).
- [x] Windows: port `build_jolt.sh` and build the Jolt plugin in CI.
- [ ] Build: rebuild when the build type changes (debug and release objects currently mix unless you run `make clean`).
- [x] Versioning and changelog for the fork ([CHANGELOG-AI.md](CHANGELOG-AI.md)).
- [x] Docs for agents ([AGENTS.md](AGENTS.md)); extend it as the agent API lands.

## 6. Game features

- [ ] UI: data-driven layouts (UI files with hot reload), anchoring and resolution-independent layout, gamepad and keyboard navigation, missing widgets (slider, dropdown, progress bar, list), transitions, localization.
- [ ] Physics follow-ups: render interpolation with `sim_alpha`, collision events and triggers, character controller, constraints, mesh and heightfield colliders, collision layers in config.
- [ ] Navigation: navmesh generation and pathfinding (for example Recast/Detour).
- [ ] Save/load of game state, built on the snapshot work in phase 2.
- [ ] Gameplay scripting that agents can write and hot-reload safely.
- [ ] Immediate-mode 2D layer: rects, rounded rects, lines, circles, textured quads, nine-slice, text with measure, and scissor clipping, drawn each frame beside KUI (KUI stays as it is). Needs a draw order between the two, a shared "pointer is over a control" answer for input, and one font atlas. Ascended Tower draws all its screens this way.

## 7. Reference game: Ascended Tower

Ascended Tower (Odin, raylib) is the reference game for proving the engine. Its simulation is engine-independent and stays in Odin behind a thin C shim with a flat API. Order of work:

- [ ] Slice 0, headless: the engine calls the Odin `game.step` from `application_fixed_update`; game commands on the agent channel (load zone, move, cast, state); a simulation state hash checked next to the physics hash in CI determinism.
- [ ] Slice 1, Lowfield seen: asset manifest generator, coloured primitives with per-instance tint, tilted orthographic camera with shadows (spike first), frame capture so an agent can look at it.
- [ ] Slice 2, one fight with a HUD: the immediate-mode 2D layer above.
- [ ] Later: pose-set API for skinned models (bone limit is 64), custom shader hook and water, fullscreen/borderless/monitor window state, SSAO/FXAA as general post-processing.
