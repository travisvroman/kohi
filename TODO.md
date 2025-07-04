# Kohi Engine Roadmap

The items in this list are not in any particular order. This list will be updated occasionally as development progresses.

# Future Releases:

## 0.10.0 Release

- [x] Refactor texture system to use ids only, typedefed as a ktexture.
- [x] Refactor asset system to use direct types, dispense with handlers/import logic from engine
  - [x] Jobify VFS loading.
  - [x] Rework import pipeline to exist in kohi.tools instead.
- [x] Remove resource system, directly use reworked asset system in dedicated systems.
- [x] Rework hot-reloading. (systems can register for a KASSET_HOT_RELOADED event and handle it thusly)
  - [x] Separate hot reload request call in VFS. This should return a watch id.
  - [x] Remove hot reload bool option in VFS request and in vfs_asset_data, as well as the file watch id.
  - [x] Fire VFS-specific event when a filesystem watch updates.
  - [x] Intercept above message in asset system, interpret how to handle it.
  - [x] Fire off asset-system specific hot reload event, accompanied by watch id.

## 0.11.0 Release

- [ ] Move scene to game code, game-specific. Remove all references to scenes in engine core/runtime.

## 0.12.0 Release

- [ ] Remove rendergraph - have application drive render process directly.

## 0.13.0 Release

- [ ] Remove legacy systems (i.e. lighting, camera, etc.)

## 0.14.0 Release

- [ ] Animated meshes.

## Future Releases

- [ ] Rework freelist to take alignment into account.
  - [ ] Rework renderbuffer to take alignment in during creation, and use said alignment for allocations.
  - [ ] Change Vulkan backend to use actual uniform size instead of stride when allocating from renderbuffer.
- [ ] Rework Vulkan shaders to only use compiled SPIR-V resources in the plugin runtime. This means that "shaderc" would not be
      required in the release build of the plugin. This also means that it _would_ need to be linked _somewhere_. Adding it to the
      utils plugin is one option, but having Vulkan-specific stuff there is strange. Could also make _another_ plugin that just contains
      the importer. This makes the most sense but has the drawback of being yet another plugin that has to be distributed (although, perhaps)
      it could be next to/a part of the vulkan renderer plugin somehow?
- [ ] Remove deprecated geometry functions in renderer.
- [ ] Remove calls to deprecated darray functions.
- [ ] Replace regular c arrays throughout the codebase with the new static-sized, type-safe array where it makes sense.
- [ ] Replace all instances of old darray usage with new one (NOTE: darray_create and darray_reserve)
- [ ] Replace all instances of typical C-style arrays and replace with new typed array from array.h
- [ ] Refactor handling of texture_map/texture resources in Vulkan renderer.
- [ ] Asset System
  - [ ] Jobify VFS asset loading
  - [ ] Asset type reworks:
    - [ ] Folders (Future)
      - [ ] Create/Destroy/Rename, etc., All happens on-disk.
      - [ ] Manifest Updates as items are moved in/out.
    - [x] Images
      - [ ] Importers
        - [ ] Importer for KTX (future)
        - [ ] Importer for PSD (future)
    - [ ] Static meshes
      - [ ] OBJ Imports - make material import/generation optional (so we don't overwrite the hand-rolled materials);
      - [ ] Create a "default static mesh" (named "StaticMesh_Default") which is a cube with perhaps a warning material.
    - [ ] Materials
      - [ ] Importer from MTL directly (as opposed to with an OBJ file).
    - [ ] Audio
      - [ ] Importers
        - [ ] .wav
        - [ ] .qoa (Quite OK Audio format)
- [ ] Asset Packaging
  - [ ] Manifest file generator (utility that looks at directory structure and auto-creates manifest.kson file from that)
  - [ ] Create binary blob format (.kpackage file) and read/write.
  - [ ] Point kpackage to .kpackage file for "release" builds.
- [ ] Resources
  - [ ] Materials
    - [ ] Importer from MTL directly (as opposed to with an OBJ file).
  - [ ] Heightmap Terrain (formerly just Terrain)
    - [ ] Refactor to encode a "material index" into an unused int/float within the vertex data, and eliminate the need to have "terrain vertex data" entirely.
    - [ ] Resource Handler - finish
- [ ] Handle refactoring
  - [ ] Create mesh system that uses handles (NOTE: maybe called "static_mesh_system"?)
  - [ ] Convert texture system to use handles (everything that _isn't_ the renderer should use handles).
  - [ ] Convert lighting system to use handles.
  - [ ] Create skybox system that uses handles.
  - [ ] Create scene system that uses handles.
- [ ] Audio
  - [ ] Audio velocity
  - [ ] Effects
    - [ ] Reverb
    - [ ] Low- and Hi-pass filtering
    - [ ] Ability to apply effects to categories or to channels directly.
    - [ ] Sound Volumes (namely, echo for caves/tunnels, with properties to adjust in scene config)
- [ ] Material system
  - [ ] Add a default "warning" material that stands out, to use in place of a non-existent material.
  - [ ] Blended materials
  - [ ] Material hot-reloading
  - [ ] Shading models:
    - [ ] Phong
    - [ ] Unlit
    - [ ] Custom (requires shader asset name OR eventually one generated by shader graph)
  - [ ] Two-sided support
  - [ ] Additional texture channels:
    - [ ] Specular (scale specularity on non-metallic surfaces (0-1, default 0.5)) (PBR, Phong)
- [ ] Break out kresource_static_mesh to be a single geometry with a single material.
  - Import process should ask about combining meshes. If so, all objects in an OBJ/GLTF/etc. would be imported as a
    single kresource_static_mesh. Materials would be combined into a single layered material. NOTE: This would probably
    require a limitation somewhere on the number of materials a "imported scene" can have.
- [ ] Rework hashtable to better handle collisions.
- [ ] Custom cursor
  - [ ] Hardware cursor (i.e. from the windowing system)
  - [ ] Software cursor (i.e. a texture drawn at cursor pos)
  - [ ] Option to set cursor image (and change on the fly)
- [ ] BUG: Hierarchy graph destroy does not release xforms. Should optionally do so.
- [ ] BUG: water plane - Fix frustum culling for reflections.
- [ ] Flag for toggling debug items on/off in scene.
- [ ] Flag for toggling grid on/off in scene.
- [ ] Profiling and performance pass - a few quick things to consider:
  - [ ] Optional ability to set texture filtering to aniso, and level, or disable.
  - [ ] Reduce draw calls
  - [ ] Occlusion queries/blocking volumes
- [ ] Combine duplicated platform code (such as device_pixel_ratio and callback assignments) to a general platform.c file.
- [ ] billboards
- [ ] particles
- [?] Move descriptor pools to be global to the backend instead of one per shader.
- [ ] Remove deprecated darray functions.
- [ ] Material redux:
  - [ ] Additional texture channels:
    - [ ] Clear coat (PBR)
    - [ ] Clear coat roughness. (PBR)
- [ ] Resource type reworks
  - [ ] Scene
    - [ ] Convert nodes->Actors.
    - [ ] Convert attachments->ActorComponents.
  - [ ] Extensibility for user-defined resource types.
    - [ ] Handler?
- [ ] Actor-ActorComponent-ActorComponentSystem

  - [ ] Actor: Represents the base of something that can exist or be spawned into the world. It is similar in
        concept to the current scene "node".

    - Contains properties:
      - u64 actor_id: A unique generated identifier of an actor upon creation.
      - kname: The name of the actor.
      - khandle hierarchy_handle: Holds handle to hierarchy id if used with a hierarchy graph (which scenes do)
      - khandle xform_handle: NOTE: Not sure if this is really needed since it can be retrieved using the hierarchy_handle,
        but it might be useful for anyone using actors without hierarchy.
    - NOTE: For actors _not_ using hierarchy, the hierarchy would need to be managed some other way.
    - Can have any number of kactor_comps attached to it (see ActorComponent)
      - Each ActorComponentSystem will be responsible for maintaining this relationship (i.e. the system maintains a
        list of components to actor_ids). Better for memory, but might make it tricky to obtain a list of all actor types
        associated with a given actor.

  - [ ] ActorComponent: Used to control an actor (i.e. how it's rendered, moved, etc.). An actor can hold many of these.
  - [ ] ActorComponentSystem: Used to manage actors of a given type.
  - [ ] Types:
    - [ ] kactor_comp_static_mesh
      - Holds a copy of a static_mesh_instance
        - Contains a pointer to the backing mesh resource, which has an array of submeshes with geometries.
        - Contains array of obtained kresource_material_instances (matches resource submesh array, including duplicates for flexibiliy)
      - system used to generate static_mesh_render_data, which contains:
        - IBL probe index
        - mesh count
        - tint override (applies to all sub-meshes)
        - An array of static_mesh_submesh_render_data, each including:
          - vertex and index count
          - vertex and index buffer offsets
          - NOTE: size is known based on the type of render data
          - copy of kresource_material_instance
          - render data flags (i.e. invert winding for negative-scale meshes)
          - model matrix (per-sub-mesh for flexibility, in case an imported "scene" i.e. OBJ/GLTF has them)
    - [ ] kactor_comp_skybox
    - [ ] kactor_comp_water_plane
    - [ ] kactor_comp_heightmap_terrain
      - Holds pointer to kresource_heightmap_terrain
        - Contains array of geometries (3D_HEIGHTMAP_TERRAIN type, see above).
      - Holds kresource_material_instance of layered terrain material.
      - system used to generate kactor_comp_heightmap_terrain_render_data, which contains:
        - An array of heightmap_terrain_chunk_render_data (per terrain chunk), each containing:
          - vertex and index count
          - vertex and index buffer offsets
          - NOTE: size is known based on the type of render data
          - NOTE: material info is passed once at the per_group level as a layered material.
    - [ ] kactor_comp_directional_light
      - Holds a pointer to generated shadow map layered texture (NOTE: remove this from the forward render node)
      - Has a xform of its own, with the following caveats:
        - Scale does nothing.
        - Position is only used by the editor/debug displays to show a gizmo.
        - Rotation is used to derive light direction when obtaining render data.
      - system used to generate kactor_comp_directional_light_render_data, which contains:
        - vec4 light colour
        - vec4 light direction (derived from xform)
        - pointer to shadow map resource
        - NOTE: For debug views, this can be used to determine the rendering of a "line" or something indicating world direction and colour
    - [ ] kactor_comp_point_light
      - Holds a pointer to generated shadow map layered texture (NOTE: Not yet implemented, but this is where it will go)
      - Has a xform of its own, with the following caveats:
        - Scale does nothing.
        - Position is used for render data generation.
        - Rotation does nothing.
      - system used to generate kactor_comp_point_light_render_data, which contains:
        - vec4 light colour
        - vec3 light position
        - constant, linear and quadratic (TODO: Can we simplify these properties to be more user-friendly?)
        - pointer to shadow map resource (NOTE: when implemented)
        - NOTE: For debug views, this can be used to determine the rendering of a "box" or something indicating world position, size and colour
    - [ ] kactor_comp_sound_effect
      - Holds pointer to sound effect resource
      - Holds position
    - [ ] kactor_music

- [ ] Blitting -
  - [ ] Ability to use/not use blitting (i.e. performance reasons on mobile?) - may punt down the road further

## Engine general:

- [x] platform layer (Windows, Linux, macOS)
  - [x] UTF-8/Wide character handling for Win32 windowing.
  - [x] UTF-8/Wide character handling for Linux windowing.
  - [ ] Actual Wayland support (not via XWayland)
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
  - [x] u64_bst
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
  - [x] binary file format (.kbi)
- [x] Renderable (writeable) textures
- [x] Static geometry
- [x] Materials
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
- [x] Strings
  - [x] kname string hashing
    - [ ] compile-time hashing (Requires either custom preprocessor or using C++ compiler)
  - [ ] high-level string structure library (not c-strings)
  - [ ] I18n strings
- [x] resource hot reloading
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
  - [ ] binary format (.kbt extension, Kohi Binary Terrain) - .kht imports to this.
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
  - [x] Would provide an interface to the engine, and the implementation could either load from disk or binary blob.
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
- [ ] Scripting language - Kohi Language (KLang) - Thanks for the name idea Dr. Elliot. :)

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

## 0.8.0 Release

- [x] KName system (case-insensitve string hashing)
- [x] KString Id system (case-sensitive string hashing)
- [x] Virtual file system (VFS)
  - [x] Sits on top of and manages packages, doles out requests to loaded packages, etc.
- [x] Remove geometry system

  - [x] Rename geometry->kgeometry
  - [x] Move creation of geometries to geometry.h/c
  - [x] Add geometry_type:
    - [x] 2D_STATIC - Used for 2d geometry that doesn't change
    - [x] 2D_DYNAMIC - Used for 2d geometry that changes often (NOTE: initially not supported)
    - [x] 3D_STATIC - Used for 3d geometry that doesn't change
    - [x] 3D_HEIGHTMAP_TERRAIN - Used for heightmap terrain-specific geometry that rarely (if ever) changes - includes material index/weight data
    - [x] 3D_SKINNED - Used for skinned 3d geometry that changes potentially every frame, and includes bone/weight data
    - [x] 3D_DYNAMIC - Used for 3d geometry that changes often (NOTE: initially not supported)
    - [x] CUSTOM - User-defined geometry type. Vertex/index size will only be looked at for this type.

- [x] New Resource System

  - [x] Remove old resource system after it is decomissioned.
  - [x] New replacement Resource System will not only replace old system but also all resource types within the engine to standardize resource handling.
        New system will make requests to new Asset System asynchronously, and be responsible for all reference counting and auto-releasing.
  - [x] Provide kresource structure which contains basic high-level items such as type, name, generation, etc.
  - [ ] Resource type reworks
    - [x] Text (Simple, generic loading of a text file)
      - [x] Handler
    - [x] Binary (Simple loading of all bytes in a file)
      - [x] Handler
    - [x] Textures
      - [x] Handler - Does this also include GPU uploads? Thinking so, versus a different "gpu loader"
    - [x] Material
      - [x] Handler
      - [x] Conversion of material .kmt files to version 3.
      - [x] Material instances
      - [x] Standard materials
      - [x] Water materials
        - [x] Water plane: Add various properties to configuration.
    - [x] Shader
      - [x] Handler
      - [x] Conversion of scopes to update_frequency (global/instance/local -> per_frame/per_group/per_draw)
    - [x] Scene
      - [x] Handler
    - [x] Static Mesh (formerly just Mesh)
      - [x] Handler
      - [x] kresource_static_mesh structure
        - [x] holds (and owns) static_geometry structure
    - [x] Bitmap Font
      - [x] Resource Handler
    - [x] System Font
      - [x] Resource Handler
  - [x] Audio Resource
    - [x] Resource Handler

- [x] Split asset loading logic from Resource System to new Asset System
  - [x] New asset system will work with asynchronous nature of VFS and automatically handle asset parsing/processing, hot-reloading and interaction with systems where necessary (i.e. texture system).
  - [x] Provide kasset structure which contains basic high-level metadata such as name, type, size and pointer to data, as well as a general "asset state" that can be looked at by any system (i.e. Uninitialized->Initialized->Loading->Loaded->Unloading)
  - [x] When an asset is requested, a name and callback are provided. The name and a asset-type-specific handler callback are provided to the VFS, which responds in kind when the asset is loaded. In the event more than one VFS call is required, the asset handler will be responsible for managing this and will ultimately invoke the original callback provided when the top-level asset was requested.
- [x] Asset packaging (kpackage)
  - [x] Reorganize assets from testbed.assets folder to go along with the respective "module".
  - [x] Rename all assets and asset references to use the format "<package>.<asset_type>.<name>". I.e. "Testbed.Texture.arch"
  - [x] Setup one manifest file including a list of all these files in each "module.". Exclude "source" files (i.e. .obj and .mtl).
  - [x] Asset type reworks:
    - [x] Static meshes
      - [x] Regenerate all .ksm files.
      - [x] Asset handler
      - [x] Importer from Wavefront OBJ
        - [x] OBJ Serializer
      - [x] Serializer
      - [x] Rename all references to "mesh" in the engine to "static_mesh" to separate it from later mesh types.
    - [x] Images
      - [x] Asset handler
      - [x] Importer
        - [x] Common formats (.bmp, .tga, .jpg, .png)
      - [x] Binary .kbi format
      - [x] Serializer
    - [x] Shaders
      - [x] Fix hot-reloading/change watches to be called from package/vfs
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
    - [x] Materials
      - [x] Convert .kmt to KSON
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
    - [x] Music/Audio effects
      - [x] Binary container format (.kaf - Kohi Audio File)
      - [x] Asset handler
      - [x] Importers
        - [x] mp3
        - [x] ogg vorbis
      - [x] Serializer
  - [x] Create kpackage interface in kohi.core.
  - [x] Point kpackage to files on disk for "debug" builds.
  - [x] Asset hot reloading
- [x] BUG: Fix release build hang on startup (creating logical device).
- [x] BUG: Fix macOS window resizing.
- [x] Fix release build hang on startup (creating logical device).
- [x] Blitting - Separate colourbuffer to its own texture separate from the swapchain/blit to swapchain image just before present.
- [x] Material system refactor
  - [x] Have material/texture system create the "combined" image at runtime instead of it being stored as a static asset (will make it easier to change in an editor without having to run a utility to do it).
  - [x] Convert material configs to KSON
- [x] Material redux:
  - [x] Shading models:
    - [x] PBR (default now)
  - [x] Transparency flag (turning off ignores alpha on albedo). Also used for material sorting during deferred/forward passes.
  - [x] Additional texture channels:
    - [x] Emissive (PBR, Phong)
- [x] Static-sized, dynamically-allocated, type-safe array with iterator.
  - [x] Tests
- [x] Static-sized, stack-allocated, type-safe array with iterator.
  - [x] Tests
- [x] Dynamic-sized, type-safe darray with iterator.
  - [x] Tests
- [x] Vulkan backend:
  - [x] Support for separate image descriptors and sampler descriptors. Remove support for combined image sampler descriptors.
  - [x] Add generic samplers usable everywhere (linear repeat, nearest repeat, linear border, linear clamp etc.)
    - [x] Add tracking to shader system as to whether these are used.
    - [x] Default sampler uniforms to use these.
    - [x] Add 16 sampler/texture limitation to shader system. (see required limits doc)
  - [x] Change samplers to use khandles
  - [x] Bubble up sampler functions to renderer frontend.
  - [x] Remove concept of texture maps in favour of separate samplers and images
  - [x] Change shader system to set "texture" instead of "sampler".
  - [x] Change shader system to no longer hold string names for uniforms, but use knames instead.
- [x] Handle refactoring
  - [x] Convert shader system to use handles
  - [x] Convert material system to use handles
- [ ] Audio system
  - [x] kaudio system refactor.
  - [x] Audio "instances"
  - [x] Audio radius checks (think emitter vs lister pos./falloff)
  - [x] Audio instance play position + radius vs. listener + radius indicates if currently bound/playing on a channel.
  - [x] Auto-channel selection based on availability (gracefully handle out-of-channels i.e. discard oldest or simply don't play?)
    - [x] Channel reservation/sound types or "categories"
- [x] Debug shape
  - [x] debug_sphere3d (similar to the one generated for the editor gizmo)

Back to [readme](readme.md)
