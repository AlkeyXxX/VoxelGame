# Terrain import specification — West Biyove Valley

## Source data and transformation

Primary source: `dtm.raw`, 8192 × 8192 little-endian uint16; bytes = 8192 × 8192 × 2. Height conversion empirically verified:

```text
height_metres = uint16_sample / 256.0
height_unreal_cm = uint16_sample * (100.0 / 256.0)
raster_column ~= round(world_x_metres + 4096)
raster_row    ~= round(world_z_metres + 4096)
```

For Unreal coordinates (X/Y horizontal, Z vertical), using the configured `RootWorldCenter`:

```text
worldX_cm = (column - 4096) * 100 + RootWorldCenter.X
worldY_cm = (row    - 4096) * 100 + RootWorldCenter.Y
worldZ_cm = sample * (100 / 256)
```

The column/row hypothesis was cross-checked with 4175 prefab Y positions (median absolute height error 0.855 m; mean 2.339 m). Still verify orientation and sample/cell centering against visible landmarks in Unreal before calling placement one-to-one.

`dtm_processed.raw` has the same dimensions but differs at ~3.28% of pixels. Do not replace the primary DTM with the processed version without a visual comparison.

## Runtime rasters used by the new reference mode

- `dtm.raw`: 8192² uint16 little-endian, exact primary DTM.
- `biomes.raw`: 1024² bytes. IDs from exact source RGBA values are 1 Forest `(0,64,0,255)`, 2 Desert `(255,168,0,255)`, 3 Snow `(255,255,255,255)`, 4 BurntForest `(255,228,119,255)`, 5 Wasteland `(186,0,255,255)`.
- `roadmask_2bit.raw`: each source pixel in `splat3.png` stored in two bits, four pixels per byte, row-major, least-significant two-bit code first. 0 transparent/no road; 1 opaque red (959472 px) interpreted asphalt; 2 opaque green (480670 px) interpreted dirt road; 3 unused.

The C++ loader validates exact dimensions/lengths. It reads the DTM, maps masks into a single immutable shared structure, and passes it to terrain chunks and interactions; no per-chunk 128 MiB map copy. The road faces are triangle material sections on the terrain mesh, so they share chunk collision and do not spawn per-block actors.

## Runtime path

Default paths on `ASmoothVoxelOctreeWorld`:
- `Content/WorldReference/WestBiyoveValley/dtm.raw`
- `Content/WorldReference/WestBiyoveValley/biomes.raw`
- `Content/WorldReference/WestBiyoveValley/roadmask_2bit.raw`

Fallback read paths:
- `Docs/WorldReference/WestBiyoveValley/`
- `Saved/WorldReference/WestBiyoveValley/`

The ready runtime ZIP has the exact three RAW files under `Docs/WorldReference/WestBiyoveValley/`; extracting it at the project root is sufficient for editor loading. To retain the binary data in GitHub, the folder's RAW files must be pushed through Git LFS using the pattern in `.gitattributes`. The available GitHub editor cannot upload local binary files to LFS by itself.

## Expected behavior and limitations

- Source DTM mode replaces macro procedural height, micro-detail and POI plateau flattening with the source heightfield.
- Source biome material classification is performed per terrain triangle (not just selected from the chunk centre), so large LOD chunks can cross biome boundaries.
- The source road mask classifies the asphalt/dirt surfaces within the terrain chunk. Query and mining logic recognizes that road layer separately, allowing the top material to be removed without excavating the underlying DTM.
- The old synthetic water actor/river/lake masks and unrelated seeded road actor are suppressed in reference mode.
- Current map metadata says `FixedWaterLevel=false`; exact water-body geometry/materials have not yet been extracted from the source files, so source rivers/water are not yet an exact reproduction.
- The archive has 4175 prefab placement entries, but it does not include the actual meshes/models for every 7DTD prefab. Their original positions can be imported separately, but exact buildings require compatible prefab geometry.
- Same seeds are not a substitute for using the already-generated source DTM and overlays.

## Future validation

1. Place the three RAW runtime files in the configured paths, run UE4.27 and verify the loader's status log.
2. Verify four landmarks near the corners plus the map centre to confirm X/Y orientation and half-cell alignment.
3. Compare a height-profile transect and 2D DTM difference against the source.
4. Check biome material boundaries and red/green road alignment at low and high LOD.
5. Implement prefab geometry/markers and water reconstruction from the remaining source information.
