# Kohi Engine

This engine is made as part of the Kohi Game Engine series on YouTube, where we make a game engine from the ground up using C and Vulkan. The series is located here: https://www.youtube.com/watch?v=dHPuU-DJoBM&list=PLv8Ddw9K0JPg1BEO-RS-0MYs423cvLVtj

The name "Kohi" (コーヒー, pronounced "koh-hee") is a simplification of the Japanese word for "coffee", which makes sense given how much I love the stuff.

![Screenshot of Kohi](/images/kohi_screenshot.png?raw=true "Screenshot of Kohi")

## .important links
- Website: https://kohiengine.com
- Forums: https://forums.kohiengine.com
- API Documentation: https://kohiengine.com/docs/

# .plan
Kohi will be a 3D engine from the start, with most of it built from scratch with explanations along the way. The series starts off as more of a tutorial to get up and running, with explanations becoming higher level as things progress to keep the pacing up.

## .platform support
Windows, Linux and macOS are all officially supported. Android and iOS runtime support may also be added down the road.

## .start
To get started, clone the repository: `https://github.com/travisvroman/kohi`.

You will need to have Clang and the Vulkan SDK installed:
 - Clang: https://releases.llvm.org/download.html
 - Vulkan SDK: https://vulkan.lunarg.com/

Note that you are free to use other compilers (such as gcc), but they are not officially supported at this time (although it shouldn't be much work to get them setup).

See the setup videos in the series for Windows or Linux for details. macOS setup
happens significantly later in the series at video 76, when support is officially added for that platform.

## .roadmap
 - Fully 3D engine with various lighting models and material types available (Basic 3d lighting, Physically-Based Rendering, etc.)
 - Multiple rendering backends:
   - Vulkan first
   - Eventually adding OpenGL and DirectX and potentially Metal
 - Suite of editor tools
 - Asset/Game Code hot-reload support
 - Physics
 - 2D/3D Sound
 - Full-fledged UI system
 - GamePad input

## .goal
The goal here is simple, to provide a resource that I wish I had when learning game development; a complete guide on building a game engine from scratch, including not only design decisions, but _why_ those decisions were made. 

Of course, there is also the goal of having a game engine capable of making games. Once the project is far enough along, making a game will be done as a series on my YouTube channel as well.

It is important to note that this engine is not, and will not be for quite a while,
production-ready. It is a learning tool and as such is not yet optimized for
use in production-quality products. This will eventually change, but for now it is
not production-ready.

## .contributions
As the project progresses, community contributions are welcome via
[Pull Requests](https://github.com/travisvroman/kohi/pulls) on GitHub.

Features should be contributed via a branch name in the format of `feature/<feature name>` where `<feature name>` is replaced with either the name of a feature or, ideally, the number of a reported feature issue (ex: `feature/80` or `feature/terrain`). 

Bug fixes _must_ be contributed via a branch name in the format of `bugfix/<issue#>` where `<issue#>` is replaced with the number of a reported feature issue (ex: `bugfix/80`). Bug fixes therefore _must_ have an associated issue created on GitHub.

Code should match the general style of the code in the repo. A code style guide will
be published here at some point in the near future.

All contributions are subject to review and may or may not be accepted, or have change requests made before being accepted.

## .support me
I am developing Kohi in my spare time. I don't have any sponsors at the moment. If you like my work, please feel free to support me over on [Patreon](https://patreon.com/travisvroman) or via [YouTube membership](https://www.youtube.com/TravisVroman/join). Your support is greatly appreciated and will be re-invested back into the project.
