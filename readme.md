# Kohi Game Engine

<img src="images/kohi_wordmark_colour.png?raw=true" alt="Kohi Logo" height=100/>

This engine is made as part of the Kohi Game Engine series on YouTube (now live-streamed on Twitch), where we make a game engine from the ground up using C and Vulkan.

As of v0.13.0, it is written exclusively in C99 (POSIX.1-2008 on GNU/Linux and MacOS).

The name **Kohi** (コーヒー, pronounced "koh-hee") is a simplification of the Japanese word for "coffee", which makes sense given how much I love the stuff.

The original YouTube series is located here: https://www.youtube.com/watch?v=dHPuU-DJoBM&list=PLv8Ddw9K0JPg1BEO-RS-0MYs423cvLVtj.

The Twitch stream happens here: https://twitch.tv/travisvroman.

![Screenshot of Kohi](/images/kohi_screenshot_2.png?raw=true "Screenshot of Kohi")
![Screenshot of Kohi](/images/kohi_screenshot.png?raw=true "Screenshot of Kohi")

## Important Links

### Socials

[![Twitch](/images/media_icon_twitch_name.png?raw=true)](https://twitch.tv/travisvroman)
[![YouTube](/images/media_icon_yt_name.png?raw=true)](https://youtube.com/travisvroman)
[![Patreon](/images/media_icon_patreon_name.png?raw=true)](https://patreon.com/travisvroman)
[![Twitter](/images/media_icon_tw_name.png?raw=true)](https://twitter.com/travisvroman)

- Discord: https://discord.com/invite/YBMH9Em

### Project Links

- Website: https://kohiengine.com
- API Documentation: https://kohiengine.com/docs/

## Goal

**IMPORTANT: This engine is not, and will not be for quite a while, production-ready. It is a learning tool and as such is not yet optimized for use in production-quality products. This will eventually change, but for now it is _not_ production-ready.**

The goal here is simple, to provide a resource that I wish I had when learning game development; a complete guide on building a game engine from scratch,
including not only design decisions, but _why_ those decisions were made. It should also be noted that, while this is _a_ way of building a game engine,
it is not _the only_ way to build one and sometimes not even the _correct_ way to build one. There is a lot of trial-and-error along the way, complete with
deep dives. Sometimes we do things the wrong way to illustrate _why_ it's the wrong way before then fixing it. Sometimes things work, sometimes they don't.
The idea is to try things and explore along the way. It is not, and never will be, a competitor to the Big Three (Unity, Unreal, Godot). It's an engine for programmers, by a programmer.

Kohi has been a 3D engine from the start, with most of it built from scratch.

The series starts off as more of a tutorial to get up and running, with explanations becoming higher level as things progress to keep the pacing up. We eventually
transition to live-streaming (simulcast on Twitch and YouTube), with most coding being done on stream with some offline work that is less interesting being done offline.

Ultimately, there is also the goal of having a game engine capable of making games. Once the project is far enough along, making a game will be done as a
series on my Twitch/YouTube channels as well.

## Platform support

Windows, GNU/Linux and macOS are all officially supported. Other platforms such as Android and iOS runtime support may also be added down the road.

## Prerequisites

While the highest effort is made to reduce dependencies, each platform has things that _must_ be installed for this to work.

Immediately after cloning, run `make setup`. If you don't have make, run the `get_deps_<platform>.sh|bat` first, then try again.

### Prerequisites for Windows

NOTE: This project _does not_ work under WSL, nor will it in the forseeable future. Don't bother trying it. Even if you do get it working, it won't be supported.

Running `make setup` now gathers the required dependencies automatically for Windows via the `get_deps_win32.bat` script.

NOTE: Assimp must still be installed this way for now on Windows. More work will be done to automate this in the future.

- Assimp: Go here to get an installer: `https://www.assimp.org/`, then downloads. Yes it takes you to an itch.io page, but it's legit.
  - After the installer runs, reboot, then add the install dir to PATH (probably `C:\Program Files\Assimp`)
  - Also create a system-level environment variable called ASSIMP with this same path as its value. Work will be done in the future to remove this requirement.

### Prerequisites for GNU/Linux

Update!

Running `make setup` now gathers the required dependencies automatically for Debian/Ubuntu, Arch, and Fedora via the `get_deps_linux.sh` script. Other distros to come in the future.

NOTE: Wayland not natively supported (yet)

### Prerequisites for macOS

Running `make setup` now gathers the required dependencies automatically for MacOS via the `get_deps_macos.sh` script.

The Vulkan SDK

NOTE: for the time being, this still needs manual installation.

- Vulkan SDK: download from https://vulkan.lunarg.com/
  - NOTE: Ensure the 'global install' option is selected, which places a copy of the files in `/usr/local/lib`.

NOTE: If you want a newer version of clang, you can also do `brew install llvm` and point to it when compiling.

NOTE: On newer macOS environments, `/usr/local/lib` isn't automaticaly searched for when resolving dependencies. A quick fix for now is to symlink any missing libraries in the
`bin` folder so they resolve. This will be fixed in a future version of the engine. To do this, simply run the following:

For vulkan:
`ln -s /usr/local/lib/vulkan.1.dylib vulkan.1.dylib`

For shaderc:
`ln -s /usr/local/lib/libshaderc_shared.1.dylib libshaderc_shared.1.dylib`

**Only do this when and where needed.**

### Cross-Platform Prerequisites:

## Audio Plugin Prerequisites

The audio plugin requires an installatiion of OpenAL.

- GNU/Linux: use a package manager to install OpenAL, if not already installed via `make setup`.
- macOS: use a homebrew to install OpenAL-Soft, if not already installed via `make setup`. Note on M1 macs this installs to `/opt/homebrew/opt/openal-soft/`, where the `include`, `lib`, and `'bin` directories can be found. The `build-all.sh` script accounts for this version of the install.
- Windows: If not already installed via `make setup`, Install the SDK from here: https://www.openal.org/downloads/

## Physics Plugin Prerequisites

`kohi.plugin.physics.jolt` uses [Jolt Physics](https://github.com/jrouwe/JoltPhysics) through its C wrapper [joltc](https://github.com/amerkoleci/joltc). `make setup` (or `make getdeps-jolt`) runs `kohi.plugin.physics.jolt/deps/build_jolt.sh`, which needs git, CMake and clang. It fetches pinned versions of both, patches joltc to expose state save/restore, and builds them into `vendor/jolt`. The script is bash, so on Windows it needs Git Bash or WSL; the Windows build of the plugin is untested.

# Start

To get started, get all of the prerequisites for your current platform (see above).
NOTE: Kohi's tools (i.e. importers) now requires Assimp. The runtime does not require this, however. In the future the tools will likely be split into a separate repo to avoid this dependency in the main repo.
After this, clone the repository recursively: `git clone --recurse-submodules https://github.com/travisvroman/kohi`.

Note that you are free to use other compilers (such as gcc), but they are not officially supported at this time (although it shouldn't be much work to get them setup).

See the setup videos in the series for Windows or GNU/Linux for details. macOS setup happens significantly later in the series at video 76, when support is officially added for that platform.

# Building

## Initial build

The first time you build, it's a good idea to run the 'setup' first. There are things in place to _try_ checking for this,
but at the moment they don't always work. This needs to be done once (unless there is a major update that specifies it should
be done again).

- All that needs to be done for this is to run `make setup` from the project root.

## General builds

There are 2 build types available, Debug and Release. Debug includes debug symbols and is optimal for development and exploration, while Release is ideal for performance. There is also a "clean" available to clean out the built files, which is useful when switching between Debug/Release, or when strange linking errors occur because of missing files (i.e. switching branches).

On all platforms, you can simply browse to the root directory of the project in a terminal/command prompt and run either `make all-debug`, `make all-release`, or `make clean`.
There are alternative shell script (macOS and GNU/Linux) and batch files (Windows) available for convenience here as well.

## Asset Imports

There are a number of assets across several modules and plugins with the engine and the Testbed project which must be imported for Testbed to run. They can be run individually by using the launch options within VSCode such as "Testbed - Import updated assets from manifest". If not using VSCode, they may be run from the command line from the base folder:

`./bin/kohi.tools importmanifest <path-to-manifest> [options]`

So, to re-import only assets which have been updated or not-yet imported, you could run with the following options:

`./bin/kohi.tools importmanifest testbed.kapp/asset_manifest.kson --updated-only`

You can also run all module/plugin asset imports at once by running the `import-updated-assets.sh` (MacOS and GNU/Linux) or `import-updated-assets.bat` (Windows).
There is also a task available in the VSCode config for this called "Import ALL updated assets for all modules".

# Running

At the moment, "Testbed" is the executable that uses Kohi. It should be run with the working directory of `/bin`. In command prompt/Powershell in Windows, or a terminal in and GNU/Linux and macOS, `cd bin` to get into the bin folder, then run `testbed.exe` on Windows or just `testbed` for GNU/Linux and macOS.

## Running headless

Kohi can run without a display or GPU, which is useful for CI, servers and AI agents that drive the engine through its state rather than its pixels. `testbed.kapp/app_config_headless.kson` is a ready-made example:

```sh
cd bin
ALSOFT_DRIVERS=null ./testbed.kapp --config ../testbed.kapp/app_config_headless.kson
```

`--config <path>` picks the application config to load. The headless options are top-level application config properties:

| Property | Default | Effect |
| --- | --- | --- |
| `headless` | `false` | Skip the display server. Windows become virtual: they have a size for render targets, but no OS window and no input events. GNU/Linux only for now. |
| `fixed_step_hz` | `0` (off) | Run the simulation in fixed steps of `1 / fixed_step_hz` seconds. See [Fixed-step simulation](#fixed-step-simulation). |
| `simulation_mode` | `"lockstep"` when headless, else `"realtime"` | How fixed steps are scheduled. |
| `max_steps_per_frame` | `8` | Real-time mode only: the most fixed steps one frame may run. |
| `max_frames` | `0` (unlimited) | Shut down cleanly after this many frames. |

Pair `headless` with the null renderer (`kohi.plugin.renderer.null`), which accepts every renderer call and draws nothing. Its buffers live in CPU memory, so engine systems that write to mapped buffers behave as usual. Textures hold no pixels and read back as zeroes. OpenAL still opens an audio device, so `ALSOFT_DRIVERS=null` is needed on machines without one.

## Fixed-step simulation

With `fixed_step_hz` set, the simulation advances in fixed steps that do not depend on the render frame rate. Each frame the engine works out how many steps to run, runs them, and then does the per-frame update and rendering. A frame that skips rendering (for example while the window is resizing) still runs its simulation steps.

- **`realtime`**: wall-clock time collects in an accumulator and is released in whole steps, so the simulation keeps real-time speed at any frame rate. `frame_data.sim_alpha` is the leftover fraction of a step, for interpolating rendered positions. If a frame takes longer than `max_steps_per_frame` steps, the extra time is dropped and the simulation falls behind instead of stalling.
- **`lockstep`**: exactly one step per frame and no wall-clock reads, so headless runs go as fast as the CPU allows and repeat exactly. Per-frame timelines also advance by the step length in this mode.

Applications put frame-rate independent logic in an optional exported `application_fixed_update(app, step_seconds, frame_data)`. It runs before `application_update`, once per step. Skeletal animation also advances in fixed steps.

The simulation can be paused and stepped from code (`engine_simulation_pause_set`, `engine_simulation_steps_request`, `engine_simulation_step_count_get`, `engine_simulation_time_get` in `core/engine.h`) or from the console with `sim_pause`, `sim_resume`, `sim_step [count]` and `sim_status`.

## Agent channel

Start the engine with `--agent` to control it from another program, such as an AI agent or a test harness. Each line on stdin is a JSON request and gets exactly one JSON reply line on stdout, in order. Logs move to stderr, so stdout carries only replies. The simulation starts paused and advances only when asked, so this needs `fixed_step_hz`. Closing stdin shuts the engine down.

```
$ cd bin && ALSOFT_DRIVERS=null ./testbed.kapp --agent --config ../testbed.kapp/app_config_headless.kson
{"id": 1, "cmd": "step", "count": 60}
{"id":1,"ok":true,"step":60,"time":1,"step_seconds":0.016666666666666666,"paused":true}
{"id": 2, "cmd": "key", "key": "w", "pressed": true}
{"id":2,"ok":true}
{"id": 3, "cmd": "jump"}
{"id":3,"ok":false,"error":"unknown command 'jump'; send {\"cmd\": \"commands\"} for the list"}
```

| Command | Arguments | Does |
| --- | --- | --- |
| `step` | `count` (default 1) | Runs that many simulation steps, then replies with the status. Later requests wait until the steps have run. |
| `status` | | Step count, simulated time, step length, paused. |
| `pause`, `resume` | | Stops, or restarts, the simulation's own clock. |
| `key` | `key` (name such as `"a"`, `"space"`, `"esc"`), `pressed` | Presses or releases a key, exactly as OS input would. |
| `mouse_button` | `button` (`left`, `right`, `middle`), `pressed` | Presses or releases a mouse button. |
| `mouse_move` | `x`, `y` (window pixels) | Moves the mouse. |
| `mouse_wheel` | `delta` | Scrolls. |
| `console` | `line` | Runs a console command, such as `physics_state_save`. |
| `physics_hash` | | Hash of the full physics state. |
| `scenes` | | Live scenes and whether each is loaded. A scene finishes loading during the first frames, so step until it reports `loaded`. |
| `entities` | `scene` (default: newest loaded) | Every entity with name, type, parent, world position, rotation, scale, and for physics bodies motion, velocity and whether it is awake. |
| `entity` | `entity` (name), `scene` | One entity, same fields. |
| `physics_impulse` | `entity`, `x`, `y`, `z` | Applies an impulse to a dynamic or kinematic body. |
| `physics_velocity_set` | `entity`, `x`, `y`, `z` | Sets a body's linear velocity. |
| `ui_controls` | `visible_only` | UI controls with name, type, screen rect, visible, active, focused, and text or checked state where the control has one. |
| `ui_click` | `control` (name) | Clicks a visible, active control at its centre, through the same handler a mouse click uses. |
| `ui_text_set` | `control`, `text` | Sets a textbox's text. |
| `commands` | | Lists every command with a short description. |
| `quit` | | Shuts the engine down. |

The process exits with 0 on a clean shutdown, 1 if the engine failed to start, and 2 if it stopped because of an error, so a harness can tell a crash-free run from a failed one without parsing logs. `id` is optional and is echoed back as sent. Requests are flat JSON objects. Applications and plugins add their own commands with `agent_command_register` (`core/agent_channel.h`). `scripts/agent_demo.py` is a small Python client that saves, restores and replays the physics state and checks that the hash matches.

## Physics

The physics system is optional and starts only when the app config has a `physics` system entry, with its backend plugin loaded:

```
systems = [
	{
		name = "physics"
		config = {
			backend_plugin_name = "kohi.plugin.physics.jolt"
			gravity = "0 -9.81 0"   // default
			max_bodies = 4096       // default
			thread_count = 0        // worker threads for the physics step; 0 runs it on the engine thread
		}
	}
	{
		name = "plugin_system"
		config = { plugins = [ { name = "kohi.plugin.physics.jolt" } ... ] }
	}
]
```

Physics advances once per fixed simulation step, right after `application_fixed_update`, so it never depends on the render frame rate. Set `fixed_step_hz`; without it physics steps with the frame time and results vary with frame rate.

Rigid bodies are scene entities of type `physics_body`. The entity follows its body on every scene update. Bodies always sit at the scene root, and their scale is ignored.

```
{
	name = "crate"
	type = "physics_body"
	transform = "0 2 0 0 0 0 1 1 1 1"
	shape_type = "box"            // sphere (radius), box (half_extents) or capsule (radius, half_height, along Y)
	half_extents = "0.5 0.5 0.5"
	motion = "dynamic"            // static, kinematic or dynamic
	mass = 0                      // 0 computes mass from the shape
	friction = 0.2
	restitution = 0
	linear_damping = 0.05
	angular_damping = 0.05
}
```

From code, `kscene_add_physics_body` adds one and `kscene_physics_body_get` returns the body behind an entity for the `kphysics_*` functions in `physics/kphysics_system.h` (velocities, forces, impulses, raycasts).

The simulation is deterministic: Jolt is built with `CROSS_PLATFORM_DETERMINISTIC`, so the same inputs give the same results on every run, thread count and platform. `kphysics_state_save`/`kphysics_state_restore` snapshot and roll back the whole simulation (bodies must be the same, created in the same order), and `kphysics_state_hash` gives a 64-bit hash of it for comparing runs. The console commands `physics_state_hash`, `physics_state_save` and `physics_state_restore` do the same. The headless testbed config loads `physics_scene` and logs the hash at shutdown, so two runs can be compared directly.

# Project Structure

This structure breakdown is based on the root folder of the repository. Some files/folders are omitted from this description as they aren't important to the overall picture.

- `kohi.core` - Shared library/.dll/.so/.dylib. Contains types, containers, string lib, math lib, utils, etc. as well as the platform layer (Win32, GNU/Linux, macOS).
- `kohi.core.tests` - A small collection of unit tests for the core library. Needs to be expanded.
- `kohi.runtime` - Shared library/.dll/.so/.dylib. Contains the core engine logic as well as many of the core engine systems.
- `kohi.runtime.tests` - A small collection of unit tests for the runtime library. Needs to be expanded.
- `kohi.plugin.audio.openal` - Shared library/.dll/.so/.dylib. Contains the audio plugin which uses OpenAL as the audio backend.
- `kohi.plugin.renderer.vulkan` - Shared library/.dll/.so/.dylib. Contains the Vulkan renderer plugin, which serves as the renderer backend to the engine for Vulkan.
- `kohi.plugin.renderer.null` - Shared library/.dll/.so/.dylib. A renderer backend that draws nothing and needs no GPU, for headless runs.
- `kohi.plugin.physics.jolt` - Shared library/.dll/.so/.dylib. The physics backend, using Jolt Physics. Its dependencies build into `vendor/jolt`.
- `kohi.plugin.ui.kui` - Shared library/.dll/.so/.dylib. Contains the Kohi UI, which contains a general-use collection of controls such as buttons, labels, textboxes, etc. This is a retained-mode UI.
- `testbed.kapp` - Application/.exe. The consuming application executable, loads up testbed.klib, configures/uses plugins and other Kohi libraries.
- `testbed.klib` = Shared library/.dll/.so/.dylib. Contains the application code (or "game code") specific to the application. Hot-reloadable.
- `kohi.tools` - A collection of command-line tools. Contains utilities such as "importmanifest".
- `utils` - A collection of build utilities required to build Kohi. These only require the C compiler, so no additional dependencies are needed.
- `.vscode` A folder containing VS Code-specific project setup.

## Roadmap

See [here](TODO.md).

## Contributions

As the project progresses, community contributions are welcome via
[Pull Requests](https://github.com/travisvroman/kohi/pulls) on GitHub.

Features should be contributed via a branch name in the format of `feature/<feature name>` where `<feature name>` is replaced with either the name of a feature or, ideally, the number of a reported feature issue (ex: `feature/80` or `feature/terrain`).

Bug fixes _must_ be contributed via a branch name in the format of `bugfix/<issue#>` where `<issue#>` is replaced with the number of a reported feature issue (ex: `bugfix/80`). Bug fixes therefore _must_ have an associated issue created on GitHub.

Code should match the general style of the code in the repo. A code style guide will
be published here at some point in the near future.

All contributions are subject to review and may or may not be accepted, or have change requests made before being accepted.

## Support Me

I am developing Kohi in my spare time. I don't have any sponsors at the moment. If you like my work, please feel free to support me in these places:

- [Patreon](https://patreon.com/travisvroman)
- [Ko-fi](https://ko-fi.com/travisvroman)
- [YouTube membership](https://www.youtube.com/TravisVroman/join)
- [Subscribe on Twitch](https://www.twitch.tv/products/travisvroman)
- [Sponsor via GitHub](https://github.com/sponsors/travisvroman)

I would also greatly appreciate follows/subscriptions on YouTube and Twitch. Please spread the word!

Your support is greatly appreciated and will be re-invested back into the project.
