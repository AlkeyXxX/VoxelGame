# Smooth voxel terrain LOD rewrite (UE 4.27)

## Scope

The rewrite lives beside the current world and does not replace the legacy
`AVoxelWorld` generator, block editing, roads, biome rules, or SaveGame path.

## Implemented milestones

1. `FSVoxelGenerator::SampleDensity` exposes the existing signed scalar field
   in world coordinates, giving all LODs one source of terrain shape.
2. `FSVoxelOctreeSelector` selects view-dependent leaves, enforces 2:1
   neighbour balance, obeys a leaf budget, and builds per-leaf transition-face
   masks.
3. `FSVoxelTransvoxelMesher` uses Eric Lengyel's official regular and
   transition lookup tables, under the included MIT license, and emits a
   separate CPU mesh for one chunk.
4. Unreal automation tests cover table validity, repeated density sampling,
   octree balance/determinism, face-mask coverage, all six face flags, combined
   face masks, and a flat coarse/fine seam fixture.
5. An opt-in actor pair now drives the new path: an octree world manager plus
   async chunk actors, bounded parallel builds, height-envelope culling, and
   deferred retirement of old chunks until the requested set is ready.

## Important current limitations

The new mesher and octree selector are now attached to a separate, opt-in
terrain manager that follows a pawn or a configured actor and streams chunks
as the view moves. It uses an async build limit, rejects stale build results,
and keeps old chunks until the requested set is ready. It has not been compiled
or run in UE4.27 in this environment, so the automation tests are added but
**not verified in Unreal Editor yet**. No claim is made that all seams, collision
contacts, edge/corner junctions, material boundaries or real-world performance
are already correct. The legacy gameplay path remains unchanged as a fallback.

The transition table encodes topology for a 2:1 resolution change. A balanced
octree does not by itself make a crack-free surface; the runtime manager must
derive correct face masks, keep a consistent world-space density field, retain
old meshes until replacements finish, and test edges/corners where multiple
transition faces meet.

## Triangle winding and collision

`FSVoxelTransvoxelMesher::AppendCellToMesh` retains outward shading normals but
orders triangle indices using Unreal's front-face convention. A mismatch here
can make a one-sided procedural surface visible only from below and interfere
with triangle-mesh collision. The flat-field automation test checks the index
winding separately from the stored outward normals.

## Runtime tuning notes

- Terrain frequency parameters are in inverse centimetres because the density
  function samples Unreal world coordinates directly. With 100 cm base voxels,
  the default `NoiseScale=0.0008` and `DetailScale=0.0006` keep the highest FBM
  octaves sampled more safely; the previous `0.0035` / `0.0050` defaults could
  alias into sharp spikes. Keep `HeightAmplitude` as a height range control,
  not as a workaround for undersampled noise.
- `BaseHeight=900` places the average surface at about Z=900 cm. With
  `HeightAmplitude=850` and `DetailAmplitude=60`, a test PlayerStart should be
  placed above roughly Z=2000 cm, with additional clearance for the capsule.
- Near collision is selected by distance to the chunk bounds rather than only
  to the chunk centre, and the procedural mesh explicitly blocks query/physics.

## Coordinate contract

- Unreal positions are in centimetres.
- The density function returns positive values for solid and negative for air.
- `CellsPerAxis` is the number of regular cells across a chunk.
- `VoxelSize` is the world-space step at that chunk's LOD.
- `OriginCells` in octree nodes is on the LOD-0 cell lattice.
- Each coarse transition face uses 3x3 high-resolution samples per transition
  cell; the four far-face samples share the corner values but are offset into
  the coarse chunk by half a coarse cell.
- Roads, water, biomes and player edits still need a shared density/attribute
  contract before the new path can replace the old one.

## Next milestones

5. Compile in UE4.27 and run the automation suite, then inspect all six face
   orientations and combined edge/corner transitions, normals, winding,
   degenerate triangles, collision and LOD changes in Unreal Editor.
6. Integrate roads/water/biome/material/edit persistence into the shared field.
7. Benchmark 4 x 4 km first; only expand toward 8 x 8 km after chunk streaming,
   memory use and frame-time have been measured.


## Optional debug flight controls

The reusable `USmoothVoxelFlightComponent` is available for test builds. After
compiling the project, add this component to `BP_VoxelPlayer` (or the relevant
player Character Blueprint) through **Add Component**. It does not change the
default player until attached.

- Press **F** to toggle flying on/off.
- Use the Blueprint's existing **WASD** bindings for forward/back/strafe.
- Hold **Space** to ascend and **Left/Right Ctrl** to descend.
- Hold **Left Shift** to use the faster flight speed.
- Configure `FlightSpeed` and `SprintFlightSpeed` in the component's Details.
  Units are Unreal centimetres per second; defaults are 3000 and 9000.
- Collision stays enabled. Flight removes gravity but is not a noclip mode.
- The component displays an on-screen message when flight is toggled.

Manual UE4.27 verification: compile the editor target, add the component to the
player Blueprint, launch a small terrain test map, confirm the toggle, horizontal
movement, vertical movement, Shift speed, and restoration of the previous
movement mode after disabling flight. This has not been compiled or run in
Unreal Editor in the current environment.
