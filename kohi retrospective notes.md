## Kohi engine retrospective

## Some statistics!

- First commit on Feb 28, 2021

- 472 commits

- 77.5k LoC

- 265 full episodes in the game engine series (plus some minor additional ones in between)

# The Good:

- Fairly organized systems (renderer frontend/backend, audio frontend/backend)

- Cross-platform support is pretty stable

- Vulkan renderer coming along nicely

- Decent amount of features implemented

- Can build a game with it as it stands at version 0.9

# The Not-so-good:

- Rendergraph - cool concept, technically works, adds a lot of overhead (in terms of code complexity)

- Scene - Okay, but we can do better. It's a bit too complex in terms of managing objects, hierarchy and leaning a bit too much in the "ECS" direction.

- Asset/Resource systems - better, but still need more work. "Requests" are verbose, asset system doesn't immediately return ptr to asset like resource system does, etc.

- Some systems don't serve much purpose and could be removed (looking at you, lighting system)

## Where to go from here

- Close off version 0.9

- Going to enter a few small refactoring stages, complete with design discussions:

- 0.10 will include rework of asset/resources to improve the API usability

- 0.11 will include either the scene refactor or the rendergraph removal

- 0.12 will include the other, not previously done (either the scene refactor or the rendergraph removal)

- 0.13 will remove some of the legacy systems (i.e. lighting system, camera system, etc.) and maybe incorporate a few small features.

- 0.14 will add animated meshes

# TODO list

### 0.10

- removal of the resource system in general - offload resource handler logic to bespoke systems per resource type.
- Assets will be the only system left, with the API looking like the below reference.
- Auto-imports? (externalize to a tool that reads the asset manifest and performs all imports of assets within it)
- Hot-reloading? (systems can register for a KASSET_HOT_RELOADED event and handle it thusly)

Reference:

    // COULD do this per asset type:
    kasset_binary* itemdb_asset = asset_system_request_binary(engine_systems_get()->asset_state, "ItemDB", asset_loaded_callback);
    kasset_binary* itemdb_asset = asset_system_request_binary_sync(engine_systems_get()->asset_state, "ItemDB");
    kasset_binary* itemdb_asset = asset_system_request_binary_from_package(engine_systems_get()->asset_state, "package_name", "ItemDB", asset_loaded_callback);
    kasset_binary* itemdb_asset = asset_system_request_binary_from_package_sync(engine_systems_get()->asset_state, "package_name", "ItemDB");
    asset_system_release_binary(engine_systems_get()->asset_state, itemdb_asset);
    if (!itemdb_asset) {
        KERROR("Failed to request ItemDB asset. See logs for details.");
        return false;
    }
