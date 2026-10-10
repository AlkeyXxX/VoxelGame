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
   octree balance and determinism, face masks, and a flat-terrain fixture.
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

6. Validate all six face orientations and combined edge/corner transitions,
   normals, winding, degenerates, deterministic rebuilds, collision and LOD
   changes in Unreal Editor.
7. Integrate roads/water/biome/material/edit persistence into the shared field.
8. Benchmark 4 x 4 km first; only expand toward 8 x 8 km after chunk streaming,
   memory use and frame-time have been measured.
