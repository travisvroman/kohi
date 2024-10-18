# Kohi Engine Roadmap

The items in this list are not in any particular order. This list will be updated occasionally as development progresses.

# Future Releases:

## 0.8.0 Release

- [x] KName system (string hashing)
- [x] Virtual file system (VFS)
  - [x] Sits on top of and manages packages, doles out requests to loaded packages, etc.
- [ ] New Resource System

  - [ ] New replacement Resource System will not only replace old system but also all resource types within the engine to standardize resource handling.
        New system will make requests to new Asset System asynchronously, and be responsible for all reference counting and auto-releasing.
  - [x] Provide kresource structure which contains basic high-level items such as type, name, generation, etc.
  - [ ] Resource type reworks

    - [ ] Textures
      - [x] Handler - Does this also include GPU uploads? Thinking so, versus a different "gpu loader"
      - [ ] Possible elimination of texture system, as it's primary function was ref counting? What about default textures?
    - [ ] Material
      - [ ] Handler
      - [x] Conversion of material .kmt files to version 3.
    - [ ] Shader
      - [ ] Handler
    - [ ] Static Mesh (formerly just Mesh) (Actor-ActorComponent-ActorComponentSystem)
      - [ ] Handler
      - [ ] Refactor to have a static mesh contain just a single geometry with a single material.
            This will remove the material instance from the geometry and instead place it on the mesh, which
            makes more sense as not all geometries have materials, but all meshes would.
            This will mean that imports of complex objects may need to be converted to a group or prefab, which isn't
            a concept yet. This would require a way to group the outer object (i.e. the Sponza).
            Perhaps a GroupActor?
    - [ ] StaticMeshActor - represents the in-world "instance" of the mesh resource. Pairs this together with a material instance.

    - [ ] Bitmap Font
      - [ ] Handler
    - [ ] System Font
      - [ ] Handler
    - [ ] Scene
      - [ ] Handler
    - [ ] Heightmap Terrain (formerly just Terrain)
      - [ ] Handler
      - [ ] HeightmapTerrainActor - represents the in-world "instance" of the heightmap terrain resource. Pairs this together with a layered material instance.
    - [ ] Sound Effect
      - [ ] Handler
      - [ ] SoundEffectActor - represents the location in-world where a sound is emitted from. Pairs together with a SoundEffect resource.
    - [ ] Music
      - [ ] Handler
    - [ ] Extensibility for user-defined resource types.
      - [ ] Handler?

- [x] Split asset loading logic from Resource System to new Asset System
  - [x] New asset system will work with asynchronous nature of VFS and automatically handle asset parsing/processing, hot-reloading and interaction with systems where necessary (i.e. texture system).
  - [x] Provide kasset structure which contains basic high-level metadata such as name, type, size and pointer to data, as well as a general "asset state" that can be looked at by any system (i.e. Uninitialized->Initialized->Loading->Loaded->Unloading)
  - [ ] When an asset is requested, a name and callback are provided. The name and a asset-type-specific handler callback are provided to the VFS, which responds in kind when the asset is loaded. In the event more than one VFS call is required, the asset handler will be responsible for managing this and will ultimately invoke the original callback provided when the top-level asset was requested.
- [ ] Asset packaging (kpackage)
  - [x] Reorganize assets from testbed.assets folder to go along with the respective "module".
  - [x] Rename all assets and asset references to use the format "<package>.<asset_type>.<name>". I.e. "Testbed.Texture.arch"
    - [x] REDO this! asset_handler_request(kasset_type, kname package_name, kname asset_name). Assets that rely on other assets can optionally specify a
          package name. VFS will first look in the same package for a asset of the right type with a match name. If not found, then iterate
          all packages looking for one and use the first one found. If more than one package contains such an asset, the user should specify
          the package in the asset reference. NOTE: This should use the new kname system.
  - [x] Setup one manifest file including a list of all these files in each "module.". Exclude "source" files (i.e. .obj and .mtl).
  - [ ] Asset type reworks:
    - [ ] Static meshes
      - [ ] Rework OBJ import process to take in package name as well as make material generation optional (so we don't overwrite the hand-rolled materials);
      - [ ] Regenerate all .ksm files.
      - [ ] Create a "default static mesh" (named "StaticMesh_Default") which is a cube with perhaps a warning material.
      - [x] Asset handler
      - [x] Importer from Wavefront OBJ
        - [x] OBJ Serializer
      - [x] Serializer
    - [x] Images
      - [x] Asset handler
      - [x] Importer
        - [x] Common formats (.bmp, .tga, .jpg, .png)
      - [x] Binary .kbi format
      - [x] Serializer
    - [ ] Shaders
      - [ ] Fix hot-reloading/change watches to be called from package/vfs
      - [x] Convert .shadercfg file to KSON-based .ksc (Kohi Shader Config)
      - [x] Asset handler
      - [x] Serializer
    - [x] Bitmap fonts
      - [x] Rename .fnt files to .kbf (Kohi Bitmap Font)
      - [x] Asset handler
      - [x] Serializer
    - [x] System fonts
      - [x] Convert .fontcfg to KSON-based .ksf file (Kohi System Font)
      - [x] Asset handler
      - [x] Primary format Serializer
    - [ ] Materials
      - [ ] Add a default "warning" material that stands out, to use in place of a non-existent material.
      - [x] Convert .kmt to KSON
      - [ ] Importer from MTL directly (as opposed to with an OBJ file).
        - [x] MTL Serializer
      - [x] Asset handler
      - [x] Serializer
    - [x] Terrains -> HeightmapTerrains
      - [x] Convert .kterrain to KSON-based .kht file (Kohi Heightmap Terrain)
      - [x] Asset handler
      - [x] Serializer
    - [x] Scenes
      - [x] Asset handler
      - [x] Serializer
    - [ ] Audio effects (future?)
      - [ ] Binary container format (.kfx)
      - [ ] Asset handler
      - [ ] Importer
      - [ ] Serializer
    - [ ] Music (future?)
      - [ ] Binary container format (.kmu)
      - [ ] Asset handler
      - [ ] Importer
      - [ ] Serializer
  - [x] Create kpackage interface in kohi.core.
  - [x] Point kpackage to files on disk for "debug" builds.
  - [ ] Asset hot reloading
  - [ ] Jobify VFS asset loading
  - [ ] Manifest file generator (utility that looks at directory structure and auto-creates manifest.kson file from that)
  - [ ] Create binary blob format (.kpackage file) and read/write.
  - [ ] Point kpackage to .kpackage file for "release" builds.
  - [ ] Rename all references to "mesh" in the engine to "static_mesh" to separate it from later mesh types.
- [x] BUG: Fix release build hang on startup (creating logical device).
- [x] BUG: Fix macOS window resizing.
- [x] Fix release build hang on startup (creating logical device).
- [ ] Combine duplicated platform code (such as device_pixel_ratio and callback assignments) to a general platform.c file.
- [ ] Split out MAX_SHADOW_CASCADE_COUNT to a global of some sort (kvar?);
  - [ ] Make this configurable
- [ ] Change rendergraph to gather required resources at the beginning of a frame (i.e. global.colourbuffer from current window's render target).
- [ ] Separate colourbuffer to its own texture separate from the swapchain/blit to swapchain image just before present.
  - [ ] (Should probably be switchable for potential performance reasons i.e. mobile?)
- [ ] Material system refactor
  - [ ] Have material/texture system create the "combined" image at runtime instead of it being stored as a static asset (will make it easier to change in an editor without having to run a utility to do it).
  - [ ] Convert material configs to KSON
  - [ ] Material hot-reloading (should do the above first)
- [ ] BUG: Material map declaration order should not matter in material config files, but it seemingly does.
- [ ] Flag for toggling debug items on/off in scene.
- [ ] Flag for toggling grid on/off in scene.
- [ ] Water plane: Add various properties to configuration.
- [ ] Water plane: Fix frustum culling for reflections.
- [ ] Remove calls to deprecated geometry functions in renderer.
- [ ] Profiling and performance pass - a few quick things to consider:
  - [ ] Optional ability to set texture filtering to aniso, and level, or disable.
  - [ ] Reduce draw calls
  - [ ] Occlusion queries/blocking volumes
- [ ] Material redux:
  - [ ] Shading models:
    - [ ] PBR (default now)
    - [ ] Phong
    - [ ] Unlit
    - [ ] PBR Water (convert current water shader to act as material)
    - [ ] Custom (requires shader asset name OR eventually one generated by shader graph)
  - [ ] Two-sided support
  - [ ] Split "combined" material map back out to Metallic(to r)/Roughness(to g)/AO(to b), instead combining them into a single texture within the renderer instead (so it only uses one sampler).
  - [ ] Transparency flag (turning off ignores alpha on albedo). Also used for material sorting during deferred/forward passes.
  - [ ] Additional texture channels:
    - [ ] Emissive (PBR, Phong)
    - [ ] Specular (scale specularity on non-metallic surfaces (0-1, default 0.5)) (PBR, Phong)
    - [ ] Clear coat (PBR)
    - [ ] Clear coat roughness. (PBR)
- [x] Static-sized, dynamically-allocated, type-safe array with iterator.
  - [x] Tests
- [x] Static-sized, stack-allocated, type-safe array with iterator.
  - [x] Tests
- [x] Dynamic-sized, type-safe darray with iterator.
  - [x] Tests
  - [ ] Mark old darray functions as deprecated.

## 0.9.0 Release

- [ ] Remove deprecated geometry functions in renderer.
- [ ] Remove calls to deprecated darray functions.
- [ ] Replace regular c arrays throughout the codebase with the new static-sized, type-safe array where it makes sense.
- [ ] Replace all instances of old darray usage with new one (NOTE: darray_create and darray_reserve)
- [ ] Replace all instances of typical C-style arrays and replace with new typed array from array.h
- [ ] Refactor handling of texture_map/texture resources in Vulkan renderer.
- [ ] Asset System
  - [ ] Asset type reworks:
    - [ ] Folders (Future)
      - [ ] Create/Destroy/Rename, etc., All happens on-disk.
      - [ ] Manifest Updates as items are moved in/out.
    - [x] Images
      - [ ] Importers
        - [ ] Importer for KTX (future)
        - [ ] Importer for PSD (future)
    - [ ] Materials
      - [ ] Importer from MTL directly (as opposed to with an OBJ file).
    - [ ] Audio effects
      - [ ] Binary container format (.kfx)
      - [ ] Asset handler
      - [ ] Importer
      - [ ] Serializer
    - [ ] Music
      - [ ] Binary container format (.kmu)
      - [ ] Asset handler
      - [ ] Importer
      - [ ] Serializer
- [ ] Handle refactoring
  - [ ] Create mesh system that uses handles (NOTE: maybe called "static_mesh_system"?)
  - [ ] Convert material system to use handles
  - [ ] Convert texture system to use handles (everything that _isn't_ the renderer should use handles).
  - [ ] Convert shader system to use handles (everything that _isn't_ the renderer should use handles).
  - [ ] Convert lighting system to use handles.
  - [ ] Create skybox system that uses handles.
  - [ ] Create scene system that uses handles.

## 0.10.0 Release

- [ ] Remove deprecated darray functions.

## Engine general:

- [x] platform layer (Windows, Linux, macOS)
  - [x] UTF-8/Wide character handling for Win32 windowing.
  - [x] UTF-8/Wide character handling for Linux windowing.
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
  - [x] static-sized array (with iterator)
  - [x] ring buffer
  - [x] queue
  - [ ] pool
  - [ ] bst
  - [x] registry
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
- [ ] Strings
  - [ ] kname string hashing
    - [ ] compile-time hashing (Requires either custom preprocessor or using C++ compiler)
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
- [x] timeline system
- [ ] skeletal animation system
- [x] skybox
- [ ] skysphere (i.e dynamic day/night cycles)
- [x] water plane
- [x] Raycasting
- [x] Object picking
  - [x] Pixel-perfect picking
  - [x] Raycast picking
- [x] Gizmo (in-world object manipulation)
- [x] Viewports
- [x] terrain
  - [ ] binary format (.kbt extension, Kohi Binary Terrain)
  - [x] heightmap-based (.kht extension, Kohi Heightmap Terrain)
  - [ ] voxel-based (.kvt extension, Kohi Voxel Terrain)
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
- [x] Multi-window applications
- [ ] Asset packaging system, including package build process.
      https://excalidraw.com/#json=5krkRPmGHvqoYkufVE_ED,ujzx6tqRDUn63DzjraQ_jw
  - [ ] Assets specific to rutime or plugins should be provided at that level to the package build process.
  - [ ] Would provide an interface to the engine, and the implementation could either load from disk or binary blob.
- [ ] For release builds, compile shaders to bytecode/SPIR-V and place into package binary.
- [ ] Custom types capability for asset system.

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
  - [x] Decouple renderpass from shader/pipeline and framebuffer/rendertargets
  - [ ] multithreading
    - [ ] texture data upload
    - [ ] mesh data upload
  - [ ] pipeline statistic querying
  - [ ] compute support
- [ ] Direct3D Renderer Plugin
  - [ ] multithreading
- [ ] Metal Renderer Plugin
- [ ] OpenGL Renderer Plugin
- [ ] Headless Renderer Plugin

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
  - [x] Base control - all controls come from this
  - [x] panel
  - [ ] image box
  - [ ] viewport control (world/scenes will be switched to use these as render targets)
  - [ ] rich text control (system text w/ multicolour, bold/italic, etc. and bitmap text with multicolour only)
  - [x] button
  - [ ] checkbox
  - [ ] radio buttons
  - [ ] tabs
  - [ ] windows/modals (complete with resize, min/max/restore, open/close, etc.)
  - [ ] resizable multi-panels
  - [ ] scrollbar
  - [ ] scroll container
  - [x] textbox/textarea
  - [x] In-game debug console

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

- [x] Split off "core" items (defines, memory, strings, containers, etc.) into a "core" or "foundation" library so they may be used without having to pull in all of the engine.
- [ ] Split off platform layers into separate libraries outside the engine.
- [x] Auto-Generated API documentation
- [ ] Documentation
- [ ] Continuous Integration
- [x] Add git tags to mark version releases (https://github.com/travisvroman/kohi/issues/174)
- [ ] Nix build compatability (https://github.com/travisvroman/kohi/issues/175)

# Previous Release TODO lists:

## 0.7.0 Release

- [x] 0.7 Reorg
  - [x] Split engine into "core" and "runtime"
  - [x] Rename plugin libs to consistent format, update builds, etc.
  - [x] Remove systems manager, and move all system init back to engine.
  - [x] External systems registry
  - [x] Plugin System implementation
    - [x] kruntime_plugin structure
    - [x] Convert Vulkan Plugin to kruntime_plugin
    - [x] Convert OpenAL Plugin to kruntime_plugin
    - [x] Convert Standard UI Plugin to kruntime_plugin
  - [x] Implement windowing logic in platform layer.
  - [x] Implement windowing logic in renderer front/backend
  - [x] Ability to deprecate code (mark as deprecated/warn).
  - [x] Deprecate "geometry" interface points in renderer frontend/
  - [x] Move loading of application lib from app to engine (config-powered!)
  - [x] frame/swapchain image count refactor:
    - [x] Move anything that's aware of double/triple/5x buffering exclusively to the renderer backend. Nothing outside that should know or care about it.
    - [x] Refactor renderer texture API to pass a new TEXTURE_FLAG_RENDERER_BUFFERING flag used to indicate to the backend that
          resources should be created "per-frame" if this is set (i.e. 3 internal textures for triple-buffering)
    - [x] Adjust render target logic to defer to the backend when reaching for a texture handle to use for the above case.
    - [x] Renderpasses should no longer own render targets.
    - [x] Framebuffers (i.e. render targets) should no longer require a renderpass to create. (i.e. use a dummy pass)
    - [x] Shaders (graphics pipelines) should no longer require a renderpass to create. (i.e. use a dummy pass)
- [x] 0.7 Scene refactor (see notes below):
- [x] Rename simple scene to just "scene" and move to engine core.
- [x] Create a unique-per-system handle for each system to identify a resource. These handles would be linked to
      a resource array of some sort and an index element within that array via a structure that holds both.
- [x] Create new "xform" structure and system that uses handles and can manage dependencies in updates internally.
      NOTE: This system should be laid out in a data-oriented way.
- [x] Create hierarchy graph that handles transform hierarchy and can provide a view of it. Also generating world matrices.
- [x] Remove transform from mesh.
- [x] Replace any and all transforms with xform handles.
- [x] Update systems (and create some) that use handles:
  - [x] Create xform system that uses handles
- [x] (See KSON) Refactor scene loader to a version 2 that is more expressive and allows "{}" syntax to nest objects.
- [x] Write "(de)serialization" routines for savable resources and use those in the above loader.
      Scene Refactor notes: Refactor into node-based system using handles for various types.
      A node should contain 3 (maybe 4) things: a unique identifier, a handle id (which is a
      link into a scene-wide handle table, which itself points to an index into an array of resources),
      a potential parent handle id (which can be INVALID*ID if unused), and potentially a name.
      There would then be lists of resource types (think mesh, terrain, lights, skybox, etc) which would
      each have lookup tables of handle ids to indices into these arrays. Additionally there would be a set
      of a lookup table and transforms that would be used. Separating these would allow updates on these objects
      in cache-coherent loops as well as any sorting/dependency lookup that would need to be done.
      The above will require that meshes have transforms removed from them. The transform would then be
      also referenced by the *node\* instead of the mesh. This would also facilitate batching of like meshes for
      rendering in the future. Transforms would also have the parent pointer removed, and instead also use
      handles. This would eliminate issues with invalid pointers when an array of a resource (i.e. transform) is
      expanded and realloced. This should be done in a phased approach, and thus perhaps a new "xform" should be
      created, and the "transform" structure could be deprecated. Note that the resource lookup for this would
      likely be global, not just within a scene.

        We could then have a few different "graphs" in the scene: one for transforms, one for visibility (i.e a flat
        array of currently-visible objects would likely suffice here), and others.

        We might also think about, at this point, reworking the scene parser to better handle object heirarchy in a more
        expressive language fasion (perhaps using some sort of scoping syntax like "{}" to surround objects).

- [x] Remove specialized rendergraphs. Replaced by app config.
- [x] Separate debug shapes out to new debug_rendergraph_node.
- [x] Separate editor gizmo out to new editor_gizmo_rendergraph_node.

Back to [readme](readme.md)
