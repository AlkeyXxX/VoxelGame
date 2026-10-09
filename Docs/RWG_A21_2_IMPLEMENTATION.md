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

- A21-style street-tile topology, gateway/highway merge, road smoothing and road rasterization into terrain; the current planner has an abstract A* road network only.
- District/township generation and density rules equivalent to the original `rwgmixer.xml`.
- Data-driven POI/prefab registry, placement scoring, rotation and overlap validation.
- Map-wide export/visual comparison against a reference A21.2 map.
- Automated Unreal Engine 4.27 compile and runtime validation.

## RWG planner implemented in the recovery branch

The first map-wide planning pass is now available from `AVoxelWorld`:

- Press **F2** in PIE to build a deterministic layout report for the current seed.
- The planner samples the existing terrain/water generator on a configurable coarse grid, prefers buildable dry settlement sites, and increases route cost on steep ground.
- It places settlement hubs (city/town/village/rural/industrial categories), biome-tagged POI markers, a settlement road backbone, a limited number of connector loops, and local roads from each POI to its hub.
- Road paths use A* over the sampled cost grid. Deep water is blocked; shallow water is assigned a high cost so backbone routes prefer dry detours. Local POI access roads are stricter: the A* path disallows water cells and the completed polyline is checked against the exact water generator; if access from the nearest road would cross water, the planner tries the parent settlement and otherwise omits that driveway rather than drawing it across water.
- The export is written to `Saved/RWG/WorldLayout.csv`; the planner summary appears in the Output Log and the debug overlay is drawn temporarily in the current world.
- Default test scale: 12 settlement hubs, 72 POIs, 32-block route-grid spacing, and 512-block macro cells on a 4096×4096-block map. These are tuning defaults, not asserted A21.2 source values.

The planner is deliberately data-only at this stage. It now builds a deterministic macro-cell grid (default 512 voxel blocks per cell), classifies cells as wilderness/rural/town/city/industrial using seeded cell values plus broad noise, samples biome/landform/buildability, and prefers zoned cells when placing hubs. The CSV includes a `CELL` record for every grid cell; when the debug preview is enabled, colored cell outlines show the zoning. This is a tunable prototype rather than a claim that 512 blocks matches an A21.2 internal constant. It does **not** yet change voxel blocks, spawn POI prefabs, reproduce A21 district/township rules, or parse the original game's `rwgmixer.xml`. The layout build is triggered explicitly with F2 and is not run automatically when a world starts.

## Procedural road surface preview

F2 now also builds a non-colliding `UProceduralMeshComponent` road surface from the planned routes:

- Main, connector and local roads are separate mesh sections with independent width controls in voxel blocks. Current defaults are 10 blocks for main roads, 6 for connectors, and 3.5 for local/gravel access roads. Mesh collision is enabled by default using async cooking; navigation-mesh registration remains disabled.
- Centerlines are resampled at short intervals; both road shoulders are sampled against the voxel terrain to reduce buried edges. Where a backbone path crosses a detected water span, the preview interpolates a bridge deck between dry banks instead of dipping to the waterline. POIs near an existing backbone road can connect to that road, and local access ribbons stop near the POI footprint rather than at its center. Local access roads are not allowed to cross a detected water span.
- The road ribbon and bridge-deck preview now generates triangle collision by default, so the player can stand/walk on the surface and cross water spans. Toggle `bEnableRWGRoadCollision` to disable it. The mesh still does not modify voxel blocks; terrain cut/fill, bridge supports, intersections, and driveway transitions remain future work.
- In the `VoxelWorld` Details panel, optionally assign `RWGRoadMaterial`; otherwise the mesh uses Unreal's default surface material. Tune `RWGRoadWidthMainBlocks`, `RWGRoadWidthConnectorBlocks`, and `RWGRoadWidthLocalBlocks` as needed. Widths are in voxel blocks: with `VoxelSize=100 cm`, for example, main width 10 is about 10 metres. Turn off `bBuildRWGRoadSurface` to suppress the mesh while keeping the CSV/debug plan.

## Validation checklist

1. Open `MyVoxelGame.uproject` with Unreal Engine 4.27 and allow C++ modules to rebuild if prompted.
2. Verify the active GameMode and PlayerController use the expected Blueprint classes.
3. Run PIE: test `F` for debug flight and `F1` for the debug menu.
4. Test chunk boundaries, terrain edits, hotbar, water/coast transitions and save/load.
5. Press F2 in PIE and inspect `Saved/RWG/WorldLayout.csv`; confirm the debug roads and markers align with the generated terrain.
6. Check the procedural road mesh; assign `RWGRoadMaterial` if the fallback material does not look like pavement.
7. Check the Output Log for compile/runtime errors before using this branch as the new baseline.


The branch has been assembled on GitHub, but it has not been compiled or launched in the user's local Unreal installation from this environment.
