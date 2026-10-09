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

The planner builds a deterministic macro-cell grid (default 512 voxel blocks per cell), classifies cells as wilderness/rural/town/city/industrial using seeded values and broad noise, samples biome/landform/buildability, and prefers zoned cells when placing hubs. The CSV includes a `CELL` record for every grid cell; when the debug preview is enabled, colored cell outlines show the zoning. This is a tunable prototype, not a claim that 512 blocks matches an A21.2 internal constant. F2 is still an explicit action: it builds the plan and activates its terrain stamps for the current session; a fresh session reconstructs the plan when F2 is pressed again. POI prefab spawning, A21 township/district rules, and parsing the original `rwgmixer.xml` remain future work.

## Procedural road surface preview

F2 also builds a `UProceduralMeshComponent` road surface from the planned routes:

- Main, connector and local roads are separate mesh sections with independent width controls in voxel blocks. Current defaults are 10 blocks for main roads, 6 for connectors, and 3.5 for local/gravel access roads. Mesh collision is enabled by default using async cooking; navigation-mesh registration remains disabled.
- F2 rasterizes road-bed and shoulder stamps into dry terrain columns. Chunk generation cuts high ground, fills low road beds, and sets a surface block by road class (stone for main roads, dirt for connectors, sand for local access); a three-block shoulder blends back toward native terrain. Stamps are applied to generated block data and Marching Cubes snapshots, and currently loaded chunks are regenerated when F2 is pressed. Future streamed chunks use the same stamp map, and far-LOD rings refresh to follow stamped heights instead of hiding cut roads behind the original terrain silhouette. Water columns are left intact so main/connector routes can cross on the raised bridge deck.
- The road ribbon and bridge-deck preview generates triangle collision by default, so the player can stand/walk on the surface and cross water spans. Toggle `bEnableRWGRoadCollision` to disable it. Full A21-style road tile topology, bridge supports, detailed intersections, and driveway transitions remain future work.
- In the `VoxelWorld` Details panel, optionally assign `RWGRoadMaterial`; otherwise the mesh uses Unreal's default surface material. Tune `RWGRoadWidthMainBlocks`, `RWGRoadWidthConnectorBlocks`, and `RWGRoadWidthLocalBlocks` as needed. Widths are in voxel blocks: with `VoxelSize=100 cm`, for example, main width 10 is about 10 metres. Turn off `bBuildRWGRoadSurface` to suppress the mesh while keeping the CSV/debug plan.

## Validation checklist

1. Open `MyVoxelGame.uproject` with Unreal Engine 4.27 and allow C++ modules to rebuild if prompted.
2. Verify the active GameMode and PlayerController use the expected Blueprint classes.
3. Run PIE: test `F` for debug flight and `F1` for the debug menu.
4. Test chunk boundaries, terrain edits, hotbar, water/coast transitions and save/load.
5. Press F2 in PIE and inspect `Saved/RWG/WorldLayout.csv`; confirm roads, stamps and markers align with the generated terrain. Walk/drive over a road and bridge to verify collision and check cut/fill at hills and shoulders.
6. Check the procedural road mesh; assign `RWGRoadMaterial` if the fallback material does not look like pavement.
7. Check the Output Log for compile/runtime errors before using this branch as the new baseline.


The branch has been assembled on GitHub, but it has not been compiled or launched in the user's local Unreal installation from this environment.
