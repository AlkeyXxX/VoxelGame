# West Biyove Valley — source-world reference

Purpose: preserve findings from the user-provided West Biyove Valley.7z so VoxelGame can target this exact 7 Days to Die world instead of approximating its shape from screenshots.

## Confirmed source facts

- Game version recorded in map_info.xml: **V.3.1.4**.
- Map raster: **8192 × 8192**; Scale=1, nominally **8192 m × 8192 m (8.192 km each side)**.
- Map-level seed: **-1058264646**.
- Nested Generation/Seed: **123**. These are two distinct metadata fields and must not be merged.
- RandomGeneratedWorld=true; FixedWaterLevel=false.
- dtm.raw: 134,217,728 bytes, exactly 8192 × 8192 × 2 bytes. It is consistent with a row-major little-endian uint16 height field.
- Empirically tested height conversion: **height in metres = uint16 sample / 256**. Values range from 257 to 50784, or about **1.004–198.375 m**.
- Empirically tested coordinate transform: column ≈ round(world_x + 4096), row ≈ round(world_z + 4096). For all 4175 prefab placements, sampling dtm.raw[row,column]/256 gives median absolute difference **0.855 m** from prefab Y, mean **2.339 m**, 90th percentile **6.145 m**. This strongly supports scale/orientation, but prefab origin and actual ground-contact points can differ.
- prefabs.xml: **4175 placed decorations**, **1107 distinct prefab names**. It contains buildings/POIs as well as road, driveway, and RWG road-tile pieces.
- spawnpoints.xml: **87 spawn points**.
- biomes.png: 1024 × 1024 RGBA, five opaque colors; exact pixel counts and tentative visual labels are in biome_palette.csv.

## Files in this folder

- world-analysis.json — machine-readable source facts, terrain statistics, coordinate check, biome palette, prefab counts, and import transform.
- source-manifest.csv — file size, SHA-256, and CRC32 for every extracted archive member, including original screenshots and large binary files.
- map_info.xml — readable copy of the world generation metadata.
- terrain-import-spec.md — proposed data-to-Unreal coordinate conversion and fidelity plan.
- prefab-layout.md — interpretation of the 4175 placed entries and road layout implications.
- biome_palette.csv — exact RGBA pixel counts/percentages; biome-name mapping is marked as visual inference.
- prefabs.xml.zlib.b64 — losslessly compressed, base64-encoded exact original prefabs.xml. Use decode_reference_assets.py to restore it.
- biomes.png.zlib.b64 — losslessly compressed, base64-encoded exact original biomes.png. The same decoder restores it.
- decode_reference_assets.py — integrity-checking restore utility for the two embedded source assets.
- binary-asset-upload.md — instructions for completing retention of the height map and splat layers.

## Reproduction strategy

**Do not try to recreate this exact landform by seed/noise alone.** The target should use the original height samples as data. Import the DTM into the shared terrain density/surface query, use the biome mask as a second raster, and place world structures from the original prefab positions. Once those sources are used by terrain, road generation should be reworked to conform to this specific elevation field.

## Important retention limitation

The compact prefab and biome source assets are in GitHub. The two raw DTM files are each 128 MiB, and several 8192² splat PNGs are also large. The available GitHub connection can commit text files, but it has no local-file/Git-LFS binary-upload operation; consequently, the full-resolution height field and original splat layers are **not yet stored in this branch**. The lossless PNG conversion heightmap_u16_lossless.png is provided as a local artifact, and its 16-bit pixel values were verified to match dtm.raw sample-for-sample. Follow binary-asset-upload.md to add it through Git LFS once from a local checkout.

Until the lossless height field itself is stored in the repository/LFS, this folder preserves exact world metadata, all prefab placement source data, the biome mask, screenshots' hashes, and verified import rules — but **not every individual terrain height sample**. Do not claim one-to-one terrain reproduction until the full-resolution height asset is present and used at runtime.

## Integrity

SHA-256 of the original uploaded archive:
10e4b2dd90a3b5a0d2b1368b48e9d47f5e4546835811ceca6d9b704ee95dbfaa

All per-file fingerprints are in source-manifest.csv.