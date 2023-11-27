# Kohi Game Engine

<img src="images/kohi_wordmark_colour.png?raw=true" alt="Kohi Logo" height=100/>

This engine is made as part of the Kohi Game Engine series on YouTube (now live-streamed on Twitch), where we make a game engine from the ground up using C and Vulkan.

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

# Plan
Kohi has been a 3D engine from the start, with most of it built from scratch. There will be many explanations and explorations along the way. The series starts off as more of a tutorial to get up and running, with explanations becoming higher level as things progress to keep the pacing up. We eventually transition to live-streaming on Twitch, with most coding being done on stream with some offline work that is less interesting being done offline.

## Platform support
Windows, Linux and macOS are all officially supported. Android and iOS runtime support may also be added down the road.

## Prerequisites
While the highest effort is made to reduce dependencies, each platform has things that _must_ be installed for this to work.

### Prerequisites for Windows
 - Make for Windows: https://gnuwin32.sourceforge.net/packages/make.htm (Yes, the last update was in 2006. But if ain't broke, why fix it?)
 - Visual Studio Community (acts as backend for clang), 2019+ is fine: https://visualstudio.microsoft.com/vs/community/
 - Git for Windows: https://gitforwindows.org/

### Prerequisites for Linux
Install these via package manager:
 - `git`
 - `make`
 - `libx11-dev`
 - `libxkbcommon-x11-dev`
 - `libx11-xcb-dev`

### Prerequisites for macOS
Install these via homebrew or other package manager:
 - `git`
 - `make`

### Cross-Platform Prerequisites:
You will need to have Clang 13+ and the Vulkan SDK installed:
 - Clang: https://releases.llvm.org/download.html
 - Vulkan SDK: https://vulkan.lunarg.com/

## Audio Plugin Prerequisites
The audio plugin requires an installatiion of OpenAL.
 - Linux: use a package manager to install OpenAL, if not already installed (i.e. `sudo apt install openal` for Ubuntu or `sudo pacman -S openal` on Arch)
 - macOS: install openal-soft via homebrew: `brew install openal-soft`. Note on M1 macs this installs to `/opt/homebrew/opt/openal-soft/`, where the `include`, `lib`, and `'bin` directories can be found. The `build-all.sh` script accounts for this version of the install.
 - Windows: Install the SDK from here: https://www.openal.org/downloads/

# Start
To get started, get all of the prerequisites for your current platform (see above). After this, clone the repository: `git clone https://github.com/travisvroman/kohi`.

Note that you are free to use other compilers (such as gcc), but they are not officially supported at this time (although it shouldn't be much work to get them setup).

See the setup videos in the series for Windows or Linux for details. macOS setup happens significantly later in the series at video 76, when support is officially added for that platform.

# Building
There are 2 build types available, Debug and Release. Debug includes debug symbols and is optimal for development and exploration, while Release is ideal for performance. There is also a "clean" available to clean out the built files, which is useful when switching between Debug/Release, or when strange linking errors occur because of missing files (i.e. switching branches).

## Building: Windows
Open up a command prompt or Powershell instance and run the `build-debug.bat` file for a debug build, or `build-release.bat` for a release build. There is also a `clean.bat` available.

## Building: Linux/macOS
Open up a terminal  and run the `build-debug.sh` file for a debug build, or `build-release.sh` for a release build. There is also a `clean.sh` available.

# Running
At the moment, "Testbed" is the executable that uses Kohi. It should be run with the working directory of `/bin`. In command prompt/Powershell in Windows, or a terminal in Linux/macOS, `cd bin` to get into the bin folder, then run `testbed.exe` on Windows or just `testbed` for Linux/macOS.

# Project Structure
This structure breakdown is based on the root folder of the repository. Some files/folders are omitted from this description as they aren't important to the overall picture.

- `engine` - Shared library/.dll. Contains the core engine logic as well as the platform layer (Win32, Linux, macOS).
- `assets` - Contains all assets to be loaded by applications (such as testbed).
- `plugin_audio_openal` - Shared library/.dll. Contains the audio plugin which uses OpenAL.
- `standard_ui` - Shared library/.dll. Contains the Kohi Standard UI, which contains a general-use collection of controls such as buttons, labels, textboxes, etc.
- `vulkan_renderer` - Shared library/.dll. Contains the Vulkan renderer plugin, which serves as the renderer backend to the engine for Vulkan.
- `testbed_lib` = Shared library/.dll. Contains the application code (or "game code") specific to the application. Hot-reloadable.
- `testbed` - Application/.exe. The consuming application executable, loads up testbed_lib, configures/uses plugins and other Kohi libraries.
- `versiongen` - Application/.exe. A small utility which generates a version using passed-in major and minor version numbers, and auto-generated build and revision numbers based on date and time. Used to version builds of Kohi and plugins.
- `tests` - A small collection of unit tests. Needs to be expanded.
- `tools` - A collection of command-line tools. Mostly empty at the moment, but will be expended when editor development begins.
- `.vscode` A folder containing VS Code-specific project setup.

## Roadmap
See [here](TODO.md).

## Goal
The goal here is simple, to provide a resource that I wish I had when learning game development; a complete guide on building a game engine from scratch, including not only design decisions, but _why_ those decisions were made. It should also be noted that, while this is _a_ way of building a game engine, it is not _the only_ way to build one.

Of course, there is also the goal of having a game engine capable of making games. Once the project is far enough along, making a game will be done as a series on my YouTube channel as well.

It is important to note that this engine is not, and will not be for quite a while, production-ready. It is a learning tool and as such is not yet optimized for use in production-quality products. This will eventually change, but for now it is _not_ production-ready.

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
