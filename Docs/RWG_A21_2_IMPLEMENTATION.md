# RWG A21.2 + Marching Cubes recovery branch

This branch is based on `experiment/marching-cubes` and keeps its latest gameplay, Blueprint assets, input mappings, chunk pipeline, Marching Cubes mesher, and terrain LOD implementation. It is the recovery/integration branch to test instead of the older `feature/a21-rwg-foundation` snapshot.

## Restored from the Marching Cubes branch

- `Content/MyFiles/Blueprints/UI/WBP_DebugMenu.uasset` debug widget.
- Updated `BP_VoxelPlayer` and `BP_VoxelPlayerController` assets.
- `Config/DefaultInput.ini` mappings:
  - `F` — `ToggleDebugFly` (debug flight toggle).
  - `F1` — `ToggleDebugMenu` (debug menu).
  - `Escape` — `ToggleMainMenu`.
  - `1`–`9` and mouse wheel — hotbar selection.
- `VoxelMarchingCubesMesher` and `VoxelTerrainLODMesher`.
- Chunk streaming/async mesh generation, cancellation and per-frame mesh upload budgets.
- Block interaction, inventory/hotbar and save/load code from the branch.

## Terrain generation currently present

- Seeded world terrain with separate landform and climate-biome classification.
- Plains/forest/desert/snow biome values and distinct surface blocks, including snow and sandstone.
- Multi-scale terrain height, detail and plateau controls.
- Water-column generation with river/lake masks and shore adjustment.
- Marching Cubes surface extraction, plus far-terrain LOD meshes.

These are procedural approximations inspired by 7 Days to Die Alpha 21.2, not a bit-for-bit reproduction of the proprietary generator.

## Still not implemented as a complete A21 RWG pipeline

- A full road graph and road-tile merge pass.
- Settlement/hub/district generation equivalent to `rwgmixer.xml`.
- Data-driven POI/prefab registry, placement scoring, rotation and overlap validation.
- Map-wide export/visual comparison against a reference A21.2 map.
- Automated Unreal Engine 4.27 compile and runtime validation.

## Validation checklist

1. Open `MyVoxelGame.uproject` with Unreal Engine 4.27 and allow C++ modules to rebuild if prompted.
2. Verify the active GameMode and PlayerController use the expected Blueprint classes.
3. Run PIE: test `F` for debug flight and `F1` for the debug menu.
4. Test chunk boundaries, terrain edits, hotbar, water/coast transitions and save/load.
5. Check the Output Log for compile/runtime errors before using this branch as the new baseline.

The branch has been assembled on GitHub, but it has not been compiled or launched in the user's local Unreal installation from this environment.
