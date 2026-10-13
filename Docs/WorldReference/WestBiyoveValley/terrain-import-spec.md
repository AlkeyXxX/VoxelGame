# Terrain import specification — West Biyove Valley

## Source raster

Primary source: dtm.raw from the supplied world. File length is 134,217,728 bytes, exactly 8192 × 8192 × 2. Treat it initially as row-major unsigned 16-bit little-endian samples. The second file dtm_processed.raw has the same size and resolution, but is **not interchangeable by assumption**: 96.7202% of its pixels match the primary file, while a small portion contains large changes. Keep both source hashes and test each in game before choosing one.

## Empirically supported mapping

Cross-checking the 4175 prefab positions against the terrain samples supports:

    column = round(world_x + 4096)
    row = round(world_z + 4096)
    height_metres = sample_uint16 / 256.0

The resulting height range is approximately 1.004–198.375 m. At those locations, the median absolute difference between sampled height and prefab Y is 0.855 m; mean is 2.339 m and p90 is 6.145 m. This validates broad axis/scale, not a guarantee that every prefab origin sits on its contact surface.

For the current Unreal Engine world using centimetres:

    WorldX_cm = (column - 4096) * 100
    WorldY_cm = (row - 4096) * 100
    WorldZ_cm = (sample / 256.0) * 100

Do not assume image row order, horizontal axes, half-cell offsets, or boundary handling are correct until sampled landmarks are verified in a small in-engine test. Use the original 8192² samples, not only the 1024² image preview.

## Recommended architecture in VoxelGame

1. Add an immutable shared reference-heightfield asset loaded once by the world; avoid loading/parsing it separately for every chunk.
2. Define one deterministic world-space sampling function that bilinearly samples the DTM. Use that same function for base surface height, terrain density, normal calculations, block queries, river validation, and road planning.
3. For voxel mesh generation, sample the heightfield into the existing chunk density field so it remains compatible with octree LOD and shared terrain collision. Do not generate 8192² independent mesh objects.
4. Preserve the exact height raster at runtime, but allow configured surface detail/noise only as a separate displacement layer with a default amplitude of zero until the baseline matches.
5. Rebuild road logic **after** fixed heightfield sampling works. Use the DTM-based surface as road input; maintain road block destruction/collision inside the terrain chunk system.
6. Load biomes.png as a separate raster. Its resolution is 1024², i.e. about 8 map metres per pixel under the scale assumption. Verify orientation, filtering, and edge alignment against the website screenshot before choosing nearest or bilinear sampling.
7. Use the original prefab entries for POI/building/road-tile positions; prefab placement must not be re-randomized if matching this source world is the goal.

## Definition of identical

- **Exact macro-heightfield**: possible only when the full-resolution DTM source is present and mapped correctly.
- **Exact road and POI layout**: requires original prefab placements and correct rotation/coordinate conventions; prefab source data is preserved in compressed form in this folder.
- **Exact appearance**: additionally depends on biome/texture/splat masks, water/rivers, game version, materials and prefab definitions. DTM alone does not reproduce those.
- **Same seed** is not sufficient. This source records map seed -1058264646 and separate nested generation seed 123, plus already-generated output files.

## Open questions to answer during implementation

- Should dtm.raw or dtm_processed.raw be the final source in game version V.3.1.4?
- Is sample index zero the exact world edge or a cell centre? The current transform is empirically supported by prefab elevations but still needs landmark verification.
- What bilinear/normal sampling and chunk-edge seam policy preserves a continuous terrain surface across Transvoxel LOD transitions?
- Which splat3* channel encodes road/terrain visualization, and how should it map to runtime road/biome materials?
- Does water need to reproduce source river shapes from terrain/splat assets or can it be re-derived from terrain lows? FixedWaterLevel=false argues against forcing one global plane.
