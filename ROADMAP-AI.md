# AI Engine Roadmap

This fork turns Kohi into an engine that AI agents can drive: build, run, observe and test a game without a human at the screen. Upstream's own plans stay in [TODO.md](TODO.md).

The phases are in priority order. Each one builds on the one before it.

## Done

- [x] Headless mode with a null renderer: runs with no display or GPU (#1).
- [x] Fixed-step simulation clock, separate from the render frame rate, with real-time and lockstep modes, plus pause and step controls (#2).
- [x] OpenAL shutdown crash fixed (#2).
- [x] Physics system with a Jolt backend: rigid bodies, scene entities, raycasts, deterministic state save/restore/hash (#3).
- [x] Release builds work again from a clean tree (#3).

## 1. Agent API

An agent needs one stable way to control a run and see what happened.

- [x] Input injection: export `input_process_*` (or a new input queue) so agents can press keys and buttons, move the mouse and drive gamepads, tied to simulation steps.
- [x] Control channel: a socket or stdin/stdout protocol (JSON lines) for console commands, stepping, pausing and querying state, so an agent drives a running engine instead of editing config files.
- [ ] World queries: list entities with type, name, transform and physics state; find entities by name or tag.
- [ ] Observations: capture frames to images (Vulkan, or a software rasterizer for headless), plus a text summary of the scene for agents that can't see images.
- [ ] UI by name: list controls, read their text and state, activate them.
- [ ] Machine-readable logs and errors (structured output, non-zero exit codes on failure).

## 2. Full determinism

The same inputs and seed must give the same run, every time, so agent experiments can be replayed and compared.

- [ ] A seed in the app config for every RNG. The global RNG is time-seeded in release builds today.
- [ ] Load assets synchronously, or in a deterministic order, in lockstep mode. Async loads currently finish in varying order.
- [ ] Record and replay: save inputs per step and replay them to the same end state.
- [ ] World snapshot/restore beyond physics: scene, transforms and game state.
- [ ] Determinism test in CI: run the headless demo twice and compare state hashes.

## 3. Parallel runs

Agents run many instances at once (search, training, testing).

- [ ] Replace named `sem_open` semaphores with unnamed ones; parallel processes collide today.
- [ ] Per-instance paths for `console.log` and other output files (CLI flag or config).
- [ ] Prove two or more headless instances run side by side with identical results.
- [ ] Longer term: move singleton engine systems into a context, so several worlds can share one process.

## 4. Known bugs

- [ ] Upstream #271: `u32_bst_delete`/`u64_bst_delete` copy the key instead of the value, and callers ignore the returned root (leaves `scene->name_lookup` dangling).
- [ ] Upstream #281: segfault when assets are missing; it should fail with an error.
- [ ] OpenAL `openal_backend_find_free_buffer`: indexes `sources[i - 1]` starting at `i = 0`, loops over the buffer count instead of the source count, and unqueues several buffers into one `ALuint`. Only hit once all buffers are in use.
- [ ] `kohi.core.tests` segfaults after the first test.
- [ ] Vulkan fails on lavapipe (software Vulkan) with `VK_ERROR_OUT_OF_HOST_MEMORY` from the custom allocator.
- [ ] `prepare_frame` failure skips the frame without ending the open command list.

## 5. Release hygiene

- [ ] CI on Linux and Windows: build debug and release from clean, run unit tests, run the headless demo and check its physics hash.
- [ ] Make the existing test suites pass and add tests for new systems (sim clock, physics).
- [ ] Windows: port `build_jolt.sh` (or use CMake directly) and test the Jolt plugin build.
- [ ] Build: rebuild when the build type changes (debug and release objects currently mix unless you run `make clean`).
- [ ] Versioning and changelog for the fork.
- [ ] Docs for agents: a single page on how to run headless, control a run, and read results.

## 6. Game features

- [ ] UI: data-driven layouts (UI files with hot reload), anchoring and resolution-independent layout, gamepad and keyboard navigation, missing widgets (slider, dropdown, progress bar, list), transitions, localization.
- [ ] Physics follow-ups: render interpolation with `sim_alpha`, collision events and triggers, character controller, constraints, mesh and heightfield colliders, collision layers in config.
- [ ] Navigation: navmesh generation and pathfinding (for example Recast/Detour).
- [ ] Save/load of game state, built on the snapshot work in phase 2.
- [ ] Gameplay scripting that agents can write and hot-reload safely.
