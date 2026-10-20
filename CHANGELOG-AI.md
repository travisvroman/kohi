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

### Fixed
- OpenAL shutdown crash (#2).
- Release builds from a clean tree (#3).
- `kohi.core.tests` crashed after the first test; both test suites now pass and exit non-zero on failure (#4).
- `array_*_destroy` left the length, stride and data pointer set (#4).
- `dynamic_allocator_header_size` ignored the debug memory guards (#4).
- Windows builds again: `platform_win32.c` compile errors, a parallel-make link race, a dangling pointer in `kthread_create` that crashed on startup, and log output lost when stdout is redirected (#4).
- The headless testbed gives the same physics hash on GNU/Linux and Windows (#4).
