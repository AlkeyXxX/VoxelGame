# Smooth voxel terrain LOD rewrite (UE 4.27)

## Scope of this first milestone

The new code lives beside the existing block-based world and does not change
its generation, block editing, biome selection, roads, or SaveGame behaviour.

The repository already contains a CPU Marching Cubes prototype in
Plugins/SmoothVoxel/Source/SmoothVoxel. The legacy AVoxelWorld path still uses
block-face meshing, and the repository did not contain a connected octree
streamer or a Transvoxel transition-mesh implementation.

This milestone adds a public scalar-density sampling entry point and an
independent view-dependent octree leaf selector. The selector balances adjacent
leaf sizes to a maximum one-LOD difference and has a hard leaf-count limit.

## Important limit

The selector does not build or draw transition meshes. A 2:1-balanced octree
by itself does not close cracks between surfaces sampled at different
resolutions. Do not switch the current world to mixed-LOD rendering until the
transition-cell mesher is implemented and the six face orientations, edges,
corners, winding, normals and deterministic rebuilds have passed tests.

## Coordinate contract

- Unreal positions are in centimetres.
- LOD-0 cell size is BaseVoxelSize.
- A node at LOD L has a side length of CellsPerAxis * (1 << L) base cells.
- Node origins are integer coordinates on the LOD-0 sample lattice.
- All LODs must sample the same signed density function at the same world
  coordinates. Positive density means solid; negative density means air.
- The current prototype density function represents a procedural heightfield
  plus optional noise-carved caves. Roads, water, biome attributes and player
  edits still need to be moved into the shared world-data contract before this
  can replace the existing gameplay path.

## First-pass acceptance tests

1. BuildBalancedLeaves returns an empty result on invalid settings or when its
   node budget prevents required 2:1 balancing.
2. Every selected leaf lies on the root-aligned cell lattice.
3. For every pair of leaves that share a face, their LOD difference is at most 1.
4. Identical camera position and settings produce an identical leaf set.
5. A follow-up renderer tests all six transition-face orientations and edge /
   corner junctions against a shared density function.
6. Unreal Editor validation verifies collision, biome/material output, edits,
   save/load, streaming replacement, roads and world-scale performance.

## Planned stages

1. Shared density query and balanced octree selection (this commit).
2. Transition-cell mesher and seam test fixtures.
3. Octree-driven chunk streaming with asynchronous generation, stale-result
   rejection and safe mesh replacement.
4. Integrate road/water/biome/edit data; keep the old world path as a fallback
   until end-to-end validation passes.
5. Benchmark 4 x 4 km first, then increase the configured bounds to 8 x 8 km.
