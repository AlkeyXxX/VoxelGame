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

## RWG planner implemented in the recovery branch

The first map-wide planning pass is now available from `AVoxelWorld`:

- Press **F2** in PIE to build a deterministic layout report for the current seed.
- The planner samples the existing terrain/water generator on a configurable coarse grid, prefers buildable dry settlement sites, and increases route cost on steep ground.
- It places settlement hubs (city/town/village/rural/industrial categories), biome-tagged POI markers, a settlement road backbone, a limited number of connector loops, and local roads from each POI to its hub.
- Road paths use A* over the sampled cost grid. Deep water is avoided; narrow higher-elevation carved channels are expensive rather than strictly forbidden.
- The export is written to `Saved/RWG/WorldLayout.csv`; the planner summary appears in the Output Log and the debug overlay is drawn temporarily in the current world.
- Default test scale: 12 settlement hubs, 72 POIs, 32-block route-grid spacing. These are tuning defaults, not asserted A21.2 source values.

The planner is deliberately data-only at this stage. It does **not** yet change voxel blocks, create road meshes, spawn prefabs, reproduce A21 district zoning, or parse the original game's `rwgmixer.xml`. Treat the output as a deterministic layout prototype to inspect and calibrate, not a finished A21 clone. The layout build is triggered explicitly with F2 and is not run automatically when a world starts.

## Validation checklist

1. Open `MyVoxelGame.uproject` with Unreal Engine 4.27 and allow C++ modules to rebuild if prompted.
2. Verify the active GameMode and PlayerController use the expected Blueprint classes.
3. Run PIE: test `F` for debug flight and `F1` for the debug menu.
4. Test chunk boundaries, terrain edits, hotbar, water/coast transitions and save/load.
5. Press F2 in PIE and inspect `Saved/RWG/WorldLayout.csv`; confirm the debug roads and markers align with the generated terrain.
6. Check the Output Log for compile/runtime errors before using this branch as the new baseline.

The branch has been assembled on GitHub, but it has not been compiled or launched in the user's local Unreal installation from this environment.
