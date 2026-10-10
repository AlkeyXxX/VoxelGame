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
6. The octree window moves in half-root-size increments around the followed
   actor. Its 50% overlap preserves world-aligned chunk keys and provides new
   terrain before old chunks are retired; the previous implementation stopped
   generating terrain at the fixed root cube boundary.
7. Vertex-normal sampling scales with LOD, blending to the finer neighbour's
   sampling distance at transition faces. UVs use dominant-axis projection to
   reduce texture stretching on steep surfaces.

## Important current limitations

The new mesher and octree selector are attached to a separate, opt-in terrain
manager that follows a pawn or configured actor and streams chunks as the view
moves. It uses an async build limit, rejects stale build results, and keeps
old chunks until the requested set is ready. The root is a rolling finite
window, not an unbounded world at once: it shifts by half its width and keeps
50% overlap to avoid stopping terrain generation at the original root boundary.
It has not been compiled or run in UE4.27 in this environment, so automation
tests are added but **not verified in Unreal Editor yet**. No claim is made that
all seams, collision contacts, edge/corner junctions, material boundaries or
real-world performance are already correct. The legacy gameplay path remains
unchanged as a fallback.

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
  the default `NoiseScale=0.0008` and `DetailScale=0.0006` reduce aliasing at
  LOD 0; they cannot preserve full geometric detail at very coarse LODs.
- Normal finite-difference distance now grows with voxel spacing, reducing
  high-frequency shading noise on triangles that no longer represent those
  small details. At coarse/fine boundaries it blends to the fine neighbour's
  normal-sampling distance to reduce visible lighting discontinuity.
- UVs keep world-scale tiling but choose XY/XZ/YZ projection based on the
  outward normal. This reduces stretching on steep slopes; it is not full
  triplanar blending, and distant mip-filtered textures can still look softer.
- `BaseHeight=900` places the average surface at about Z=900 cm. With
  `HeightAmplitude=850` and `DetailAmplitude=60`, a test PlayerStart should be
  placed above roughly Z=2000 cm, with additional clearance for the capsule.
- Near collision is selected by distance to the chunk bounds rather than only
  to the chunk centre, and the procedural mesh explicitly blocks query/physics.

## Transvoxel transition sample correctness

The official Transvoxel table requires paired scalar values to match across the two parallel faces of a transition cell: samples 0/9, 2/10, 6/11, and 8/12 have identical density values even though the latter four positions are offset into the coarse chunk by half a coarse cell. The mesher samples the nine full-resolution face values and copies those four paired corner values, as described in Eric Lengyel's Figure 4.16 and implementation notes. Resampling the inward points at their spatially offset coordinates violates the lookup table's assumptions and can create LOD-scaled patches, corner holes, and dark seams.

Regression tests cover sloped density fields and a coarse/fine shared-face contour. These automation tests still need to be run in UE4.27.

## Legacy player Blueprint warning

The player Blueprint still has legacy block-interaction logic that expects an `AVoxelWorld` reference. The opt-in `ASmoothVoxelOctreeWorld` is a different class and does not populate that variable. In a test map using only the smooth manager, guard the existing block-interaction call with an `IsValid` check or disable that legacy interaction in the debug player Blueprint. This warning is separate from mesh topology.

## Coordinate contract

- Unreal positions are in centimetres.
- The density function returns positive values for solid and negative for air.
- `CellsPerAxis` is the number of regular cells across a chunk.
- `VoxelSize` is the world-space step at that chunk's LOD.
- `OriginCells` in octree nodes is an absolute LOD-0 cell-lattice coordinate.
- `RootOriginCells` marks the active rolling window's lower corner; balancing
  and face-neighbour lookup are relative to that root but chunk keys stay absolute.
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
7. Benchmark the rolling-window overlap, long-distance LOD normal quality,
   edge/corner seams, texture projections, memory use and frame-time in UE4.27.


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
