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
   face masks, flat/sloped coarse-fine seams, normal/tangent continuity and a
   large-coordinate cross-chunk density regression.
5. An opt-in actor pair now drives the new path: an octree world manager plus
   async chunk actors, bounded parallel builds, height-envelope culling, and
   deferred retirement of old chunks until the requested set is ready.
6. The octree window slides horizontally in half-root-size increments around
   the followed actor, with 50% overlap. Its vertical range remains anchored so
   upward flight does not move terrain out of the active window; absolute chunk
   keys allow overlapping meshes to be reused.
7. Regular mesh vertices use projected secondary positions in transition
   boundary cells instead of moving density sample corners before interpolation.
   Adjacent transition faces remain on their shared edge lattice. Normals use a
   fixed world-space gradient step across all LODs; world-XY UVs stay stable and
   tangent frames derive from that same projection at every vertex.
8. Terrain noise bandwidth is clamped globally for the coarsest supported sample
   step (maximum leaf LOD 4 / 16 m per cell). The streaming root has a separate
   LOD (default 6), retaining a 2.048 km-wide window without allowing rendered
   chunks to exceed the 16 m sample spacing.
9. Completed CPU meshes are queued to the manager and only one procedural mesh
   section is committed on the game thread per frame. Worker concurrency is
   capped at two, and LOD reselection has a minimum 10 m movement threshold.

## Important current limitations

The new mesher and octree selector are attached to a separate, opt-in terrain
manager that follows a pawn or configured actor and streams chunks as the view
moves. It rejects stale build results. On initial spawn, chunks become visible
as they finish; during later LOD replacements, new chunks stay hidden until the
full desired set is ready, then old leaves are retired and new leaves revealed
together to avoid overlap and z-fighting between coarse parents and fine children.
Completed CPU meshes are committed one per frame, and worker concurrency is
capped at two to reduce game-thread upload bursts and CPU contention.
The root is a rolling finite window, not an unbounded world at once. By default,
its separate `RootWindowLod=6` makes it 2.048 km wide at 1 m base cells, and it
shifts horizontally by half that width (1.024 km) with 50% overlap. Its vertical
interval remains anchored to `RootWorldCenter.Z`. Rendered leaves are still
capped at `MaxLod=4` (up to 16 m between samples), restoring draw distance
without reintroducing coarser mesh sampling. The cap also applies to serialized
actor instances so old Details-panel values cannot restore highly aliased coarse LODs.
It has not been compiled or run in UE4.27 in this environment, so automation
tests are added but **not verified in Unreal Editor yet**. No claim is made that
all seams, collision contacts, edge/corner junctions, material boundaries or
real-world performance are already correct. The legacy gameplay path remains
unchanged as a fallback.

The transition table encodes topology for a 2:1 resolution change. A balanced
octree does not by itself make a crack-free surface: regular cells bordering a
transition need secondary positions, and multiple transition faces must preserve
a shared edge lattice. The current branch projects a quarter-cell border offset
into the regular surface tangent plane and leaves face intersections on exact
shared coordinates. These changes are new and still require runtime verification
at all six face orientations and edge/corner combinations.

## Triangle winding and collision

`FSVoxelTransvoxelMesher::AppendCellToMesh` retains outward shading normals but
orders triangle indices using Unreal's front-face convention. A mismatch here
can make a one-sided procedural surface visible only from below and interfere
with triangle-mesh collision. The flat-field automation test checks the index
winding separately from the stored outward normals.

## Runtime tuning notes

- Terrain frequency parameters are in inverse centimetres. The defaults are
  `NoiseScale=0.00005` and `DetailScale=0.0000375`. The world manager clamps them
  to the 16 m maximum leaf spacing and uses the same values for every LOD.
- The Transvoxel path reconstructs double-precision sample coordinates from the
  chunk's integer base-lattice origin plus its local position. Its 2D terrain and
  optional 3D cave noise use 64-bit integer lattice hashes instead of a float
  sine-hash. This avoids losing the noise-grid fractional coordinate or creating
  repeated hash patterns as absolute world coordinates grow. Mesh vertices remain
  chunk-local. UE4.27 still uses float actor/component transforms, so truly huge
  worlds may additionally need world-origin rebasing.
- This intentionally trades some fine geometric noise for stable distant terrain.
  Add high-frequency albedo/normal detail in the material rather than raising the
  geometry noise scales beyond the supported sampling bandwidth.
- Normal finite-difference distance is fixed in world units from `BaseVoxelSize`
  and shared by every LOD. The same world-space point therefore gets a consistent
  normal across chunks and LOD changes, reducing square-shaped lighting bands.
  Geometry noise frequency remains globally capped to limit detail that coarse
  leaves cannot represent.
- Transition faces use secondary-position deformation of regular-surface
  vertices, with displacement projected along the surface tangent rather than
  moving sampled cube corners before isosurface interpolation. Vertices shared
  with a non-transition face remain fixed to preserve neighbouring seams.
- Normal sampling no longer changes at transition faces or per-LOD chunk borders:
  all chunks use the same world-space sample radius, avoiding normal-step
  discontinuities that could appear as dark square patches.
- Stream selection has a minimum 10 m movement threshold, at most two CPU
  workers, and a one-mesh-section-per-frame commit budget. These safeguards
  reduce repeated rebuilds and completion bursts but cannot guarantee zero
  frame-time cost for every terrain/material configuration.
- UVs use a stable world-XY projection at fixed world scale, reconstructed from
  the chunk's integer lattice origin in double precision before conversion to
  the float UV channel. This is seam-consistent for the height-field terrain and
  avoids per-vertex projection switching. Near-vertical cliffs can stretch; use
  triplanar mapping there. Very large absolute UVs may still require material-side
  wrapping/offset management because UE4 shader coordinates are floats.
- Tangents are derived from the UV0 world-XY projection and the smoothed surface
  normal, rather than accumulated independently from each cell's triangle list.
  Duplicate vertices at adjacent cells therefore get the same tangent frame,
  which should prevent normal-map lighting from producing cell-sized dark squares.
  A near-vertical wall is singular for XY projection; use a triplanar material
  for cliffs/caves. The UE4.27 material result still needs local verification.
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
- The rolling window recentres in XY only; its Z interval stays fixed around
  `RootWorldCenter.Z` so vertical flight does not discard the ground surface.
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
