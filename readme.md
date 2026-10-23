# Kohi Game Engine

<img src="images/kohi_wordmark_colour.png?raw=true" alt="Kohi Logo" height=100/>

This engine is made as part of the Kohi Game Engine series on YouTube (now live-streamed on Twitch), where we make a game engine from the ground up using C and Vulkan.

As of v0.13.0, it is written exclusively in C99 (POSIX.1-2008 on GNU/Linux and MacOS).

The name **Kohi** (コーヒー, pronounced "koh-hee") is a simplification of the Japanese word for "coffee", which makes sense given how much I love the stuff.

The original YouTube series is located here: https://www.youtube.com/watch?v=dHPuU-DJoBM&list=PLv8Ddw9K0JPg1BEO-RS-0MYs423cvLVtj.

The Twitch stream happens here: https://twitch.tv/travisvroman.

# <span style="color:#ff6600">IMPORTANT UPDATE FOR SOURCE CONTROL!</span>

<span style="color:#ff6600">**The repository for Kohi is being migrated away from GitHub (and Git) to a new Subversion/SVN server.**</span>

<ins>From this point forward, the GitHub repo will simply act as a mirror of the SVN repo, and will only
be updated periodically.</ins> Therefore, the SVN repo will now be the _single source of truth_.

**If you are looking for the bleeding-edge code, look at the SVN repo, in `trunk`.**

I've written an article with the why and how [here](https://travisvroman.com/articles/svn.html).

Note that this repo _isn't going anywhere_, and will be updated, It's just not the primary anymore.

See [Project Links](#project-links) below.

Also See [Contributions](#contributions) below.

![Screenshot of Kohi](/images/kohi_screenshot_2.png?raw=true "Screenshot of Kohi")
![Screenshot of Kohi](/images/kohi_screenshot.png?raw=true "Screenshot of Kohi")

## Important Links

<a name="project-links"></a>

### Project Links

- [![Subversion](https://img.shields.io/badge/Kohi%20on%20SVN--green.svg?style=social&logo=subversion)](https://svn.kohiengine.com/svn/kohi) The bleeding edge code is now here! See `trunk`.
- [![GitHub](https://img.shields.io/badge/Kohi%20on%20GitHub--green.svg?style=social&logo=Github)](https://github.com/travisvroman/kohi) GitHub is now treated as a mirror and is updated from SVN every so often.
- [![Website](https://img.shields.io/badge/Website-0066ff)](https://kohiengine.com) Additional info about the engine.
- [![Forum](https://img.shields.io/badge/Forum-ff6600)](https://forum.kohiengine.com) Report issues or bugs with the engine here.
- [![Bugtracking](https://img.shields.io/badge/Bug%20Tracking-ff0066)](https://forum.kohiengine.com) Discussions about the engine.
- [![API Documentation](https://img.shields.io/badge/API%20Documentation-00ff66)](https://kohiengine.com/docs/) Auto-generated code documentation.

### Socials

[![Twitter](https://img.shields.io/badge/%40travisvroman--blue.svg?style=social&logo=x)](https://twitter.com/travisvroman)
[![Instagram](https://img.shields.io/badge/travisvroman--red.svg?style=social&logo=Instagram)](https://www.instagram.com/travisvroman)
[![Youtube](https://img.shields.io/badge/TravisVroman--red.svg?style=social&logo=youtube)](https://www.youtube.com/travisvroman)
[![Discord](https://img.shields.io/badge/TravisVroman%20Server--blue.svg?style=social&logo=Discord)](https://discord.gg/YBMH9Em)
[![Patreon](https://img.shields.io/badge/travisvroman--green.svg?style=social&logo=Patreon)](https://patreon.com/travisvroman)

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

Windows, GNU/Linux and macOS desktops are all officially supported. Other platforms such as Android and iOS runtime support may also be added down the road.

### A note on compilers

This codebase is written to work with the most recent versions of clang. Note that you are free to use other compilers (such as gcc), but they are not officially supported at this time (although it shouldn't be much work to get them setup). A MSVC/Visual Studio solution setup will not be included with this project.

<a name="initial_setup_and_building"></a>

## Initial Setup and Building

While the highest effort is made to reduce dependencies, each platform has things that _must_ be installed for this to work.

### Windows

0. **Install Version Control** You will need **_one_** of these programs:
   - <u>Subversion/SVN via TortoiseSVN</u> (recommended):
     - `winget install -e --id TortoiseSVN.TortoiseSVN`
   - <u>Git For Windows</u>:
     - `winget install git.git`
1. **Get The Code:** checkout or clone the repo
   - Subversion (recommended): checkout the repository: `svn checkout https://svn.kohiengine.com/svn/kohi/trunk kohi`
   - Git (mirror only): clone the repository recursively: `git clone --recurse-submodules https://github.com/travisvroman/kohi`.
2. **First-time setup:** Immediately after checkout/cloning, run `make setup`. If you don't have Make installed, run `get_deps_win32.bat` first, which will install it for you, then try running `get_deps_win32.bat` again. _This process needs to be done once, unless you download an update that requires it be run again._
3. **Get Assimp:** NOTE: Unfortunately, Assimp must still be installed this way for now on Windows. More work will be done to automate this in the future. It is required for asset imports.
   - Go here to get an installer: `https://www.assimp.org/`, then downloads. Yes it takes you to an itch.io page, but it's legit.
   - After the installer runs, reboot, then add the install dir to PATH (probably `C:\Program Files\Assimp`)
   - Also create a system-level environment variable called ASSIMP with this same path as its value. Work will be done in the future to remove this requirement.
4. **Build:** Run `build-debug.bat` to build the source code and wait for it to complete. Debug is the recommended build until you are ready to release your project. To run a release build, execute `build-release.bat` instead.
5. **Import All Assets:** Finally, you will need to run `import-updated-assets.bat` to convert all assets from their source formats to the Kohi-native ones. This will need to be done for any assets that are imported into a project as well.

#### Additional notes on Windows

- This project _does not_ work under WSL, nor will it in the forseeable future. Don't bother trying it. Even if you do get it working, it won't be supported.
- See the [setup video for Windows](https://www.youtube.com/watch?v=F6_WdnzQIQ4) for details.

### GNU/Linux

0. **Install Version Control:** You will need **_one_** of these programs:
   - <u>Subversion/SVN</u> (recommended):
     - Fedora/RHEL: `sudo dnf install subversion` or `sudo yum install subversion`
     - Arch: `sudo pacman -S subversion`
     - Ubuntu/Debian: `sudo apt-get install -y subversion`
   - <u>Git</u>:
     - Fedora/RHEL: `sudo dnf install git` or `sudo yum install git`
     - Arch: `sudo pacman -S git`
     - Ubuntu/Debian: `sudo apt-get install -y git`
1. **Get The Code:** checkout or clone the repo
   - Subversion (recommended): checkout the repository: `svn checkout https://svn.kohiengine.com/svn/kohi/trunk kohi`
   - Git (mirror only): clone the repository recursively: `git clone --recurse-submodules https://github.com/travisvroman/kohi`.
2. **First-time setup:** Immediately after checkout/cloning, run `make setup`. If you don't have Make installed, run `get_deps_linux.sh` first, which will install it for you, then try running `get_deps_linux.sh` again. _This process needs to be done once, unless you download an update that requires it be run again._
3. **Build:** Run `build-debug.sh` to build the source code and wait for it to complete. Debug is the recommended build until you are ready to release your project. To run a release build, execute `build-release.sh` instead.
4. **Import All Assets:** Finally, you will need to run `import-updated-assets.sh` to convert all assets from their source formats to the Kohi-native ones. This will need to be done for any assets that are imported into a project as well.

#### Additional notes on GNU/Linux

- Wayland is not natively supported (yet), but Kohi runs fine via xwayland.
- See the [setup video for GNU/Linux](https://www.youtube.com/watch?v=NIv1mygBOcg) for details.

### macOS

0. **Install Version Control:** You will need **_one_** of these programs:
   - <u>Subversion/SVN</u> (recommended):
     - `sudo brew install subversion`
   - <u>Git</u>:
     - `sudo brew install git`
1. **Get The Code:** checkout or clone the repo
   - Subversion (recommended): checkout the repository: `svn checkout https://svn.kohiengine.com/svn/kohi/trunk kohi`
   - Git (mirror only): clone the repository recursively: `git clone --recurse-submodules https://github.com/travisvroman/kohi`.
2. **First-time setup:** Immediately after checkout/cloning, run `make setup`. If you don't have Make installed, run `get_deps_macos.sh` first, which will install it for you, then try running `get_deps_macos.sh` again. _This process needs to be done once, unless you download an update that requires it be run again._
3. **Vulkan SDK:** NOTE: For the time being, this still requires manual installation on macOS.
   - <b>IMPORTANT:</b> Ensure the 'global install' option is selected, which places a copy of the files in `/usr/local/lib`.
   - Download and install it from https://vulkan.lunarg.com/
4. **Build:** Run `build-debug.sh` to build the source code and wait for it to complete. Debug is the recommended build until you are ready to release your project. To run a release build, execute `build-release.sh` instead.
5. **Import All Assets:** Finally, you will need to run `import-updated-assets.sh` to convert all assets from their source formats to the Kohi-native ones. This will need to be done for any assets that are imported into a project as well.

#### Additional notes on macOS

- If you want a newer version of clang, you can also do `brew install llvm` and point to it when compiling.
- On newer macOS environments, `/usr/local/lib` isn't automaticaly searched for when resolving dependencies. A quick fix for now is to symlink any missing libraries in the `bin` folder so they resolve. This will be fixed in a future version of the engine. **Only do this if you have problems with this:**
  - For vulkan: run `ln -s /usr/local/lib/vulkan.1.dylib vulkan.1.dylib`
  - For shaderc: run `ln -s /usr/local/lib/libshaderc_shared.1.dylib libshaderc_shared.1.dylib`
- macOS setup happens significantly later in the series at [video 76](https://www.youtube.com/watch?v=g9tvP4qf5sA), when support is officially added for that platform.

# Running the engine

Once everything in [Initial Setup and Building](#initial_setup_and_building) is finished, you will want to run the Testbed, which is Kohi's test driver application and example of how a game/application uses the engine. There are a few ways to do this:

- <b>From the console:</b> From the project folder, `cd bin` and then run `testbed.kapp` (Linux/macOS) or `testbed.kapp.exe` (Windows). **IMPORTANT! You _must_ be within the `bin` working directory for pathing to work properly when loading assets!**
- <b>From VSCode:</b> If you use VS Code, you can simply select the `Launch TestBed` option from the debugger and press `F5`. A full VS Code configuration is included with the project.

# Building

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

# Project Structure

This structure breakdown is based on the root folder of the repository. Some files/folders are omitted from this description as they aren't important to the overall picture.

- `kohi.core` - Shared library/.dll/.so/.dylib. Contains types, containers, string lib, math lib, utils, etc. as well as the platform layer (Win32, GNU/Linux, macOS).
- `kohi.core.tests` - A small collection of unit tests for the core library. Needs to be expanded.
- `kohi.runtime` - Shared library/.dll/.so/.dylib. Contains the core engine logic as well as many of the core engine systems.
- `kohi.runtime.tests` - A small collection of unit tests for the runtime library. Needs to be expanded.
- `kohi.plugin.audio.openal` - Shared library/.dll/.so/.dylib. Contains the audio plugin which uses OpenAL as the audio backend.
- `kohi.plugin.renderer.vulkan` - Shared library/.dll/.so/.dylib. Contains the Vulkan renderer plugin, which serves as the renderer backend to the engine for Vulkan.
- `kohi.plugin.ui.kui` - Shared library/.dll/.so/.dylib. Contains the Kohi UI, which contains a general-use collection of controls such as buttons, labels, textboxes, etc. This is a retained-mode UI.
- `testbed.kapp` - Application/.exe. The consuming application executable, loads up testbed.klib, configures/uses plugins and other Kohi libraries.
- `testbed.klib` = Shared library/.dll/.so/.dylib. Contains the application code (or "game code") specific to the application. Hot-reloadable.
- `kohi.tools` - A collection of command-line tools. Contains utilities such as "importmanifest".
- `utils` - A collection of build utilities required to build Kohi. These only require the C compiler, so no additional dependencies are needed.
- `.vscode` A folder containing VS Code-specific project setup.

## Roadmap

See [here](TODO.md).

<a name="contributions"></a>

## Contributions

<span style="color:#ff6600;">**IMPORTANT UPDATE!**</span>

**Issues and Pull Requests will no longer be accepted or reviewed via GitHub.**

Instead, they must be submitted [here](https://bugs.kohiengine.com/) instead. All prior issues
located on the GitHub repo were moved and are now tracked there.

The process for submitting contributions has been detailed [here](https://travisvroman.com/articles/how_to_submit_patches.html).

To contribute a fix or enhancement, submit a patch file as an attachment to a new issue [here](https://bugs.kohiengine.com/). It will be reviewed at some point whereafter it will either be:

- Accepted as-is
- Changes are requested before it is accepted
- Declined with a reason.

Code submitted must match the general style of the code in the repo. A code style guide will
be published here at some point in the near future.

NOTE: that it will not be possible any longer for GitHub users to be associated with "contributors"
on that repo. However, I'm more than happy to include you in a file that stays along with the code
if you specify how you'd like to appear.

## Support Me

I am developing Kohi in my spare time. I don't have any sponsors at the moment. If you like my work, please feel free to support me in these places:

- [Patreon](https://patreon.com/travisvroman)
- [Ko-fi](https://ko-fi.com/travisvroman)
- [YouTube membership](https://www.youtube.com/TravisVroman/join)
- [Subscribe on Twitch](https://www.twitch.tv/products/travisvroman)
- [Sponsor via GitHub](https://github.com/sponsors/travisvroman)

I would also greatly appreciate follows/subscriptions on YouTube and Twitch. Please spread the word!

Your support is greatly appreciated and will be re-invested back into the project.
