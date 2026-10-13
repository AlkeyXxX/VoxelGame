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
6. The world uses a fixed finite root centred on `RootWorldCenter`; it does
   not follow the player indefinitely. With default settings it is exactly
   4 km × 4 km horizontally (400,000 cm per axis). A slightly smaller effective
   base step keeps the power-of-two octree lattice within that hard size limit;
   absolute chunk keys keep all neighbouring samples aligned.
7. Regular mesh vertices use projected secondary positions in transition
   boundary cells instead of moving density sample corners before interpolation.
   Adjacent transition faces remain on their shared edge lattice. Normals use a
   fixed world-space gradient step across all LODs; world-XY UVs stay stable and
   tangent frames derive from that same projection at every vertex.
8. Terrain noise bandwidth is clamped globally for the coarsest supported sample
   step (maximum leaf LOD 4 / about 16 m per cell). The fixed world root defaults
   to LOD 7 and exactly 4 km × 4 km horizontally; the effective base step is
   reduced to fit the strict world-size limit without increasing leaf LOD.
9. Completed CPU meshes are queued to the manager and only one procedural mesh
   section is committed on the game thread per frame. Worker concurrency is
   capped at two, and LOD reselection has a minimum 10 m movement threshold.

## Important current limitations

The new mesher and octree selector are attached to a separate, opt-in terrain
manager that uses the followed pawn or configured actor only to choose detail
levels inside fixed world bounds. It does not move the world bounds or create
new terrain outside that finite area. It rejects stale build results. On initial spawn, chunks become visible
as they finish; during later LOD replacements, new chunks stay hidden until the
full desired set is ready, then old leaves are retired and new leaves revealed
together to avoid overlap and z-fighting between coarse parents and fine children.
Completed CPU meshes are committed one per frame, and worker concurrency is
capped at two to reduce game-thread upload bursts and CPU contention.
The world root is a fixed finite volume centred on `RootWorldCenter`, not a
rolling window that follows the player. With default `CellsPerAxis=32` and
`RootWindowLod=7`, its XY footprint is exactly 4 km × 4 km (400,000 cm per
axis). The effective base step is reduced slightly from 1 m as needed to make
the octree's power-of-two lattice fit inside that strict cap; changing
`CellsPerAxis` or lowering `RootWindowLod` can make the footprint smaller.
Rendered leaves remain capped at `MaxLod=4`, keeping the maximum cell spacing
around 16 m and avoiding the previous highly aliased coarse LODs. The vertical
root interval is also fixed around `RootWorldCenter.Z`.
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

## Mining and block placement in the smooth-world path

The opt-in `ASmoothVoxelOctreeWorld` has its own sparse runtime edit overlay; it
does not reuse the legacy world's block save format. Terrain mining progress is
keyed by the nearest solid density-grid node, not a fractional ray-hit cell.
Each hit adds `TerrainMiningToolDamage` divided by the selected block's DataTable
durability. When progress reaches 100%, that node is added to an immutable mined
node snapshot, and a voxel-sized octahedral cut is centred exactly on the lattice
point. Placement edits remain in a separate map, so a mined node cannot overwrite
a hard-block reservation at the same integer key.

Before accepting procedural-mesh collision, the interaction checks that the hit
normal straddles the current signed-density surface. A stale asynchronous collision
face is ignored and the interaction ray marches the current density snapshot instead.
Mined nodes also use a smaller gradient radius for surface normals. Triangle winding
is corrected per emitted triangle from its geometric cross and outward shading
normal, with a local-gradient fallback at sharp edited corners. Vertex welding uses
0.01 cm quantization to preserve close intersections around hard-cube cavities.

Terrain UVs use one world-space repeat scale and choose the planar projection for
the whole triangle, with separate vertex caches for XY floors, XZ walls and YZ
walls. This prevents a shared vertex from inheriting UVs from whichever surface
orientation happened to be emitted first. Material-level triplanar blending remains
a better long-term option for smoothly transitioning across steep slopes.

Placed `Dirt` and `Sand` are rounded, blended SDF additions to the smooth
landscape. Other placeable block types (for example `Grass`, `Stone`, and
`Wood`) reserve an air cell in the terrain mesh and render as separate six-face
cubes with flat per-face normals and collision. Each cube takes multiple hits
based on its block durability; its silhouette remains cubic while a small inset
shows accumulating damage, and the procedural terrain under the cube is restored
when it is finally removed. The current edit overlay lasts for the lifetime of the
octree world actor; SaveGame persistence and per-block material assignment are
not part of this change.

The new mining/cube path and edited-mesh regression tests have **not** been built
or executed in UE4.27 in this environment. Check LMB mining from both above and
below, repeated hits at one point, texture scale on vertical walls, and place /
damage / remove Grass and Stone. Also check that Dirt/Sand still blend naturally
into the landscape before treating the change as runtime validated.

## Triangle winding and collision

`FSVoxelTransvoxelMesher::AppendCellToMesh` retains outward shading normals but
orders triangle indices using Unreal's front-face convention. A mismatch here
can make a one-sided procedural surface visible only from below and interfere
with triangle-mesh collision. Edited-mesh tests check each triangle against the
averaged outward vertex normal and check indices/areas after repeated mined-node
edits and hard-cube cavity generation.

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

- `HeightAmplitude` is measured in Unreal centimetres and is a signed
  amplitude around `BaseHeight`. A value of 200 means only ±2 m; for obvious hills,
  start around 1500–3000 (±15–30 m) and place the PlayerStart above
  `BaseHeight + HeightAmplitude` with clearance. Existing Blueprint instances may
  have serialized overrides, so changing the C++ default does not overwrite
  their Details-panel values.
- Chunk streaming work is spread across frames: at most `MaxChunkRequestsPerFrame`
  chunks are spawned/reconfigured per frame, obsolete chunks are retired in
  batches, and identical mesh vertices are indexed once per chunk to reduce GPU
  vertex data and upload size. Height noise is cached once per X/Y sample column
  instead of being recomputed at every Z layer; height-field normals also avoid
  six full density evaluations. Only the nearby fine LODs cast dynamic shadows,
  and chunks without collision have their collision component disabled. This
  reduces CPU and shadow-pass work but still needs profiling in UE4.27.
- `SplitDistanceMultiplier` controls how early the octree refines a chunk.
  The default is 0.75; raising it increases the number of fine chunks and LOD
  boundaries and can reduce frame rate. Existing placed Blueprint/level instances
  may retain serialized Details-panel values, so check this setting on the actor.
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
- UVs use per-triangle world-space planar projection at a single repeat scale
  (one tile per 800 cm): XY for horizontal faces, XZ for Y-facing walls and YZ
  for X-facing walls. Chunk origins are rebased in double precision to keep GPU UVs
  bounded. Separate caches avoid sharing one vertex between incompatible UV axes.
  True material-level triplanar blending remains the best option for smooth
  texture transitions over strongly sloped terrain.
- Tangents are derived from the same planar projection as each vertex's UVs and
  the smoothed surface normal. The UE4.27 material and normal-map result still
  needs local verification.
- `BaseHeight=900` places the average surface at about Z=900 cm. With
  `HeightAmplitude=850` and `DetailAmplitude=60`, a test PlayerStart should be
  placed above roughly Z=2000 cm, with additional clearance for the capsule.
- Near collision is selected by distance to the chunk bounds rather than only
  to the chunk centre, and the procedural mesh explicitly blocks query/physics.

## Transvoxel transition sample correctness

The official Transvoxel table requires paired scalar values to match across the two parallel faces of a transition cell: samples 0/9, 2/10, 6/11, and 8/12 have identical density values even though the latter four positions are offset into the coarse chunk by half a coarse cell. The mesher samples the nine full-resolution face values and copies those four paired corner values, as described in Eric Lengyel's Figure 4.16 and implementation notes. Resampling the inward points at their spatially offset coordinates violates the lookup table's assumptions and can create LOD-scaled patches, corner holes, and dark seams.

Regression tests cover sloped density fields and a coarse/fine shared-face contour. These automation tests still need to be run in UE4.27.

## Legacy player Blueprint warning

The legacy block nodes on `AVoxelWorld` still expect an `AVoxelWorld` reference. When the player Blueprint is switched to the opt-in `ASmoothVoxelOctreeWorld`, store a reference to the actual smooth-world actor at `BeginPlay` and validate it before calling `Break Block by Ray` or `Place Block by Ray`. A missing reference is a Blueprint wiring issue, separate from mesh topology.

## Coordinate contract

- Unreal positions are in centimetres.
- The density function returns positive values for solid and negative for air.
- `CellsPerAxis` is the number of regular cells across a chunk.
- `VoxelSize` is the world-space step at that chunk's LOD.
- `OriginCells` in octree nodes is an absolute LOD-0 cell-lattice coordinate.
- `RootOriginCells` marks the fixed finite world's lower corner; balancing and
  face-neighbour lookup are relative to that root, while chunk keys stay absolute.
- The world bounds remain fixed around `RootWorldCenter`; flying beyond the
  configured 4 km × 4 km footprint does not create additional terrain.
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
7. Verify the fixed 4 km world boundary, long-distance LOD normal quality,
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
