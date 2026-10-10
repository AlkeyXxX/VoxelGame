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

- Full A21-style street-tile topology, gateway/highway merge, authored intersections, and prefab-driven road integration. The current planner uses smoothed A* curves and rasterizes dry road surfaces into the procedural terrain, but it is still not the original tile-based RWG system.
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

F2 builds the road plan, smooths A* corners into short Catmull-Rom segments, filters height samples along the path to reduce one-block "washboard" ridges, stamps dry road beds into terrain, and creates bridge-deck meshes only over water:

- Default widths are 10 blocks for main highways, 10 for ordinary connectors, 6 for rural dirt roads, and 3 for local POI driveways. These are exposed in the `VoxelWorld` Details panel.
- Dry roads are generated as stamped Marching Cubes terrain, not as floating road ribbons. Their surface height remains fractional for smooth geometry; road lanes follow a smoothed terrain profile, while a three-block shoulder blends back toward native terrain. Roads use the shared generator/stamp map in newly streamed chunks and far LOD. The settlement backbone also adds a sparse outer loop where enough outer hubs exist; this improves coverage, but it is not a dense A21-style road grid.
- `RWGRoadMaterial` is used for the main paved highway/connector surfaces and bridge-deck meshes. The road material gets its own mesh section and un-tinted vertex colors so its Base Color/Normal/Roughness textures are not multiplied by biome tint. Rural roads (6 blocks) and local POI driveways (3 blocks) retain their dirt block colors and do not inherit the custom paved-road material.
- Road surface hints now respect player block edits. Removing the stamped top layer disables the road material for that column and lowers the smoothed surface to the exposed lower layer, so the paved texture should not persist in a mined-out hole. The material-face classifier checks nearby road-mask samples and surface height rather than a single triangle centroid, reducing stray gray/sand triangles along the road top.
- Water crossings are sampled every two blocks and check the complete road width, so a wide paved road gets a bridge deck when water intrudes into the lane edges, even if the centerline remains dry. Water voxel columns themselves stay intact. The procedural mesh remains for those bridge/wet-edge spans, with collision enabled by default using async cooking. Toggle `bEnableRWGRoadCollision` to disable bridge collision or `bBuildRWGRoadSurface` to suppress bridge meshes.
- For the rest of the land, assign a terrain material to `VoxelWorld.Material`; without one, the surface is only the block/biome vertex-color look, not a textured PBR landscape. Textures on ordinary terrain and road sections use UV coordinates. `TerrainUVScalePerBlock` controls the terrain repeat density, while `RWGRoadUVScalePerBlock` controls road/bridge materials independently. Both default to 0.5 repeats per block (at `VoxelSize=100 cm`, about one repeat every 2 metres). Far LOD uses the same terrain UV scale.
- For speedier inspection, `DebugFlySpeed`, `DebugFlyBoostSpeed`, and `DebugFlyAcceleration` are editable in the `VoxelWorld` Details panel. Defaults are 6000 cm/s, 18000 cm/s while boosting, and 24000 cm/s² acceleration. Full A21-style road tile topology, bridge supports, detailed intersections, and driveway transitions remain future work.

## Validation checklist

1. Open `MyVoxelGame.uproject` with Unreal Engine 4.27 and allow C++ modules to rebuild if prompted.
2. Verify the active GameMode and PlayerController use the expected Blueprint classes.
3. Run PIE: test `F` for debug flight and `F1` for the debug menu.
4. Test chunk boundaries, terrain edits, hotbar, water/coast transitions and save/load.
5. Press F2 in PIE and inspect `Saved/RWG/WorldLayout.csv`; confirm roads, stamps and markers align with the generated terrain. Walk/drive over a road and bridge to verify collision and check cut/fill at hills and shoulders.
6. Check the procedural road mesh; assign `RWGRoadMaterial` if the fallback material does not look like pavement.
7. Check the Output Log for compile/runtime errors before using this branch as the new baseline.


The branch has been assembled on GitHub, but it has not been compiled or launched in the user's local Unreal installation from this environment.
