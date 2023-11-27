# Kohi Engine Roadmap 
The items in this list are not in any particular order. This list will be updated occasionally as development progresses.

## Engine general:
- [x] platform layer (Windows, Linux, macOS)
- [x] event system
- [x] clock
- [x] testing framework
- [x] math library (vector math, etc)
- [x] Memory system 
- [ ] Allocators:
  - [x] linear allocator
  - [x] dynamic allocator (variable-size allocations)
  - [ ] pool allocator
- [x] Systems manager
- [x] Resource system 
- [x] Resource Loaders:
  - [x] binary
  - [x] text
  - [x] image
  - [x] material 
  - [x] bitmap font 
  - [x] system font 
  - [x] scene
- [ ] mobile runtime support (Android, iOS)
- [ ] SIMD
- [ ] Containers:
  - [x] stack
  - [x] hashtable
  - [x] freelist
  - [x] dynamic arrays  
  - [x] ring buffer
  - [ ] queue 
  - [ ] pool 
  - [ ] bst
- [ ] quadtrees/octrees
- [x] Threads 
- [x] Job system
- [ ] Multi-threaded logger
- [x] Textures 
  - [ ] binary file format (.kbt)
- [x] Renderable (writeable) textures 
- [x] Static geometry 
- [x] Materials 
- [ ] billboards
- [ ] particles
- [ ] Input:
  - [x] desktop
  - [ ] touch
  - [ ] gamepad
  - [x] keymaps/keybindings
- [x] Conosole
  - [x] Console consumer interface
  - [x] Logger as consumer
  - [x] Debug console as consumer 
  - [x] kvars (console variables)
  - [x] Console commands
- [x] Application-level configuration
- [ ] high-level string structure library (not c-strings)
- [ ] resource hot reloading
- [ ] prefabs
- [ ] Simple Scenes
  - [x] Base implementation
  - [x] Load from file 
  - [ ] Save to file
- [x] Renderer System (front-end/backend plugin architecture)
- [x] Audio System (front-end)
- [ ] Physics System (front-end)
- [ ] networking
- [ ] profiling
- [ ] timeline system
- [ ] skeletal animation system
- [x] skybox
- [ ] skysphere (i.e dynamic day/night cycles)
- [ ] water plane
- [x] Raycasting
- [x] Object picking 
  - [x] Pixel-perfect picking 
  - [x] Racast picking
- [x] Gizmo (in-world object manipulation)
- [x] Viewports
- [ ] terrain
  - [x] heightmap-based
  - [x] pixel picking
  - [x] raycast picking 
  - [ ] chunking/culling
  - [ ] LOD/tessellation
  - [ ] holes
  - [ ] collision
- [ ] volumes 
  - [ ] visibility/occlusion
  - [ ] triggers 
  - [ ] physics volumes 
  - [ ] weather
- [ ] Multi-window applications

## Renderer:
- [ ] geometry generation (2d and 3d, e.g. cube, cylinder, etc.)
- [ ] advanced materials (WIP)
- [ ] PBR
- [ ] batch rendering (2d and 3d)
- [ ] shadow maps
- [x] texture mipmapping
- [x] Specular maps 
- [x] Normal maps 
- [x] Phong Lighting model 
- [x] Multiple/configurable renderpass support.
- [x] Rendergraph

## Plugins:
 - [ ] ECS (Entity Component System)
 - [x] Audio (OpenAL plugin)
 - [ ] Vulkan Renderer Plugin
   - [ ] multithreading
 - [ ] Direct3D Renderer Plugin 
   - [ ] multithreading
 - [ ] Metal Renderer Plugin 
 - [ ] OpenGL Renderer Plugin 


## Standard UI: (separated section because number of controls)
- [x] Standard UI system
- [ ] Layering
- [ ] UI file format
- [ ] Load/Save UIs
- [ ] UI Editor (as a plugin to the editor)
- [ ] control focus (tab order?)
- [ ] docking
- [ ] drag and drop support
- [ ] UI Controls (one of the few engine-level areas that uses OOP):
  * [x] Base control - all controls come from this
  * [x] panel
  * [ ] image box
  * [ ] viewport control (world/scenes will be switched to use these as render targets)
  * [ ] rich text control (system text w/ multicolour, bold/italic, etc. and bitmap text with multicolour only)
  * [x] button
  * [ ] checkbox
  * [ ] radio buttons
  * [ ] tabs
  * [ ] windows/modals (complete with resize, min/max/restore, open/close, etc.)
  * [ ] resizable multi-panels
  * [ ] scrollbar
  * [ ] scroll container
  * [x] textbox/textarea
  * [x] In-game debug console

## Editor 
- [ ] Editor application and 'runtime' executable
  - [ ] World editor
  - [ ] UI editor
  - [ ] editor logic library (dll/.so) hot reload
- [ ] Move .obj, .mtl import logic to editor (output to binary .ksm format).
- [ ] Move texture import logic to editor (output to binary .kbt format).
- [ ] DDS/KTX texture format imports
- [ ] FBX model imports 

## Other items:
- [x] Auto-Generated API documentation
- [ ] Documentation

Back to [readme](readme.md) 
