# Kohi Engine Roadmap 
The items in this list are not in any particular order. This list will be updated occasionally as development progresses.

## Engine general:
- [x] platform layer (Windows, Linux, macOS)
  - [x] UTF-8/Wide character handling for Win32 windowing.
  - [x] Wayland support
- [x] event system
- [x] clock
- [x] testing framework
- [x] math library (vector math, etc)
- [x] Memory system 
- [x] Generic sorting function/library.
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
  - [x] queue 
  - [ ] pool 
  - [ ] bst
- [ ] quadtrees/octrees
- [x] Threads 
- [ ] Flag to force single-threaded mode.
- [x] Semaphores
- [x] Job system
  - [x] Job dependencies
  - [x] Job semaphores/signaling
- [x] ThreadPools
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
    - [ ] I18n / keyboard layouts
- [x] Conosole
  - [x] Console consumer interface
  - [x] Logger as consumer
  - [x] Debug console as consumer 
  - [x] kvars (console variables)
  - [x] Console commands
- [x] Application-level configuration
- [ ] high-level string structure library (not c-strings)
- [ ] I18n strings
- [ ] resource hot reloading
- [ ] prefabs
- [x] Custom storage format (KSON - Kohi Storage Object Notation)
  - [x] Tokenizer/parser
  - [x] ToString/FromString API
- [x] Scenes
  - [x] Base implementation
  - [x] Load from file 
  - [ ] Adjustable global scene properties
  - [x] Save to file
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
  - [x] Raycast picking
- [x] Gizmo (in-world object manipulation)
- [x] Viewports
- [x] terrain
  - [ ] binary format
  - [x] heightmap-based
  - [ ] voxel-based
    - [ ] smooth voxels
  - [x] pixel picking
  - [x] raycast picking 
  - [x] chunking/culling
    - [x] BUG: culling is currently passing all chunks always.
  - [x] LOD
    - [x] Blending between LOD levels (geometry skirts vs gap-filling, etc.)
  - [ ] tessellation
  - [ ] holes
  - [ ] collision
- [ ] volumes 
  - [ ] visibility/occlusion
  - [ ] triggers 
  - [ ] physics volumes 
  - [ ] weather
- [ ] Multi-window applications
- [ ] 0.7 Scene refactor (see notes below):
  - [x] Rename simple scene to just "scene" and move to engine core.
  - [x] Create a unique-per-system handle for each system to identify a resource. These handles would be linked to 
        a resource array of some sort and an index element within that array via a structure that holds both.
  - [x] Create new "xform" structure and system that uses handles and can manage dependencies in updates internally.
        NOTE: This system should be laid out in a data-oriented way.
  - [x] Create hierarchy graph that handles transform hierarchy and can provide a view of it. Also generating world matrices.
  - [x] Remove transform from mesh.
  - [x] Replace any and all transforms with xform handles.
  - [ ] Update systems (and create some) that use handles:
    - [x] Create xform system that uses handles
    - [ ] Create mesh system that uses handles (NOTE: maybe called "static_mesh_system"?)
    - [ ] Convert material system to use handles
    - [ ] Convert texture system to use handles (everything that _isn't_ the renderer should use handles).
    - [ ] Convert shader system to use handles (everything that _isn't_ the renderer should use handles).
    - [ ] Convert lighting system to use handles.
    - [ ] Create skybox system that uses handles.
    - [ ] Create scene system that uses handles.
  - [x] (See KSON) Refactor scene loader to a version 2 that is more expressive and allows "{}" syntax to nest objects.
  - [x] Write "(de)serialization" routines for savable resources and use those in the above loader.
          Scene Refactor notes: Refactor into node-based system using handles for various types.
          A node should contain 3 (maybe 4) things: a unique identifier, a handle id (which is a
          link into a scene-wide handle table, which itself points to an index into an array of resources),
          a potential parent handle id (which can be INVALID_ID if unused), and potentially a name.
          There would then be lists of resource types (think mesh, terrain, lights, skybox, etc) which would
          each have lookup tables of handle ids to indices into these arrays. Additionally there would be a set
          of a lookup table and transforms that would be used. Separating these would allow updates on these objects
          in cache-coherent loops as well as any sorting/dependency lookup that would need to be done.
         
          The above will require that meshes have transforms removed from them. The transform would then be
          also referenced by the _node_ instead of the mesh. This would also facilitate batching of like meshes for
          rendering in the future. Transforms would also have the parent pointer removed, and instead also use
          handles. This would eliminate issues with invalid pointers when an array of a resource (i.e. transform) is
          expanded and realloced. This should be done in a phased approach, and thus perhaps a new "xform" should be
          created, and the "transform" structure could be deprecated. Note that the resource lookup for this would
          likely be global, not just within a scene.
         
          We could then have a few different "graphs" in the scene: one for transforms, one for visibility (i.e a flat
          array of currently-visible objects would likely suffice here), and others.
         
          We might also think about, at this point, reworking the scene parser to better handle object heirarchy in a more
          expressive language fasion (perhaps using some sort of scoping syntax like "{}" to surround objects).
     
 

## Renderer:
- [ ] geometry generation (2d and 3d, e.g. cube, cylinder, etc.)
- [ ] advanced materials (WIP)
- [x] PBR Lighting model
- [ ] batch rendering (2d and 3d)
- [ ] instanced rendering
- [ ] Per-scene vertex/index buffers
- [ ] Queue-up of data uploads during scene load:
  - Notes/ steps involved: 
    - Setup a queue in the scene to hold pending mesh data.
    - For each mesh:
      - Make sure mesh is invalidated so it doesn't attempt to render. 
      - Assign a unique id for it and add it to the queue
      - Load it from disk (multithreaded, currently done but needs some changes). Save off id, size, data, offsets, etc.
      - Reserve space in buffer freelist but _don't_ upload to GPU. Save the offset in the queue as well.
      - NOTE: that this could be used to figure out how large the buffer needs to be.
    - Repeat this for all meshes.
    - In one swoop, upload all vertex and index data to GPU at once, perhaps on a separate (transfer) queue to avoid frame drops.
      - Probably the easiest way is a single vkCmdCopyBuffer call with multiple regions (1 per object), to a STAGING buffer.
      - Once this completes, perform a copy from staging to the appropriate vertex/index buffer at the beginning of the next available frame.
    - After the above completes (may need to setup a fence), validate meshes all at once, enabling rendering.
- [x] shadow maps
  - [x] PCF
  - [x] cascading shadow maps
  - [x] Adjustable Directional Light properties
    - [x] max shadow distance/fade (200/25)
    - [x] cascade split multiplier (0.91)
    - [ ] shadow mode (soft/hard shadows/none)
  - [ ] Percentage Closer Soft Shadows (PCSS)
  - [ ] Point light shadows
- [x] texture mipmapping
- [x] Specular maps (NOTE: removed in favour of PBR)
- [x] Normal maps 
- [x] Phong Lighting model (NOTE: removed in favour of PBR)
- [x] Multiple/configurable renderpass support.
- [x] Rendergraph
  - [x] Linear processing
  - [ ] Rendergraph Pass Dependencies/auto-resolution
  - [ ] Multithreading/waiting/signaling
  - [x] Forward rendering specialized rendergraph
  - [ ] Deferred rendering specialized rendergraph
  - [ ] Forward+ rendering specialized rendergraph
- [x] Forward rendering 
- [ ] Deferred rendering 
- [ ] Forward+ rendering
- [ ] Compute Shader support (frontend)

## Plugins:
 - [ ] ECS (Entity Component System)
 - [x] Audio (OpenAL plugin)
 - [ ] Vulkan Renderer Plugin (WIP)
   - [ ] multithreading
     - [ ] texture data upload
     - [ ] mesh data upload
   - [ ] pipeline statistic querying
   - [ ] compute support
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
- [ ] Split off "core" items (defines, memory, strings, containers, etc.) into a "core" or "foundation" library so they may be used without having to pull in all of the engine.
- [ ] Split off platform layers into separate libraries outside the engine.
- [x] Auto-Generated API documentation
- [ ] Documentation
- [ ] Continuous Integration
- [x] Add git tags to mark version releases (https://github.com/travisvroman/kohi/issues/174)
- [ ] Nix build compatability (https://github.com/travisvroman/kohi/issues/175)

Back to [readme](readme.md) 
