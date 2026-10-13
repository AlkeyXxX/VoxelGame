# West Biyove Valley — source-world reference

Purpose: preserve the analysis of the user-supplied West Biyove Valley.7z for reproducing this exact 7 Days to Die world in VoxelGame.

## Main findings

- Game version: **V.3.1.4**.
- Height raster: **8192 × 8192**, Scale=1; nominal map extent is **8192 m (8.192 km) per side**.
- Map-level Seed: **-1058264646**. Nested Generation/Seed: **123**. They are distinct and must both be preserved.
- RandomGeneratedWorld=true; FixedWaterLevel=false.
- dtm.raw and dtm_processed.raw are each 134,217,728 bytes = 8192 × 8192 uint16 samples. Empirically supported conversion: height in metres = sample / 256.
- dtm.raw range: **1.0039–198.375 m**, mean **48.9082 m**.
- Prefab coordinate check supports column ≈ round(world_x + 4096), row ≈ round(world_z + 4096). Across 4175 prefab entries, median absolute height difference to prefab Y = **0.855 m**, mean 2.339 m, p90 6.145 m. Good evidence for the transform, not proof of row direction/half-cell alignment; verify these in game.
- prefabs.xml contains **4175 placements**, **1107 distinct prefab names**. It includes POIs/buildings, roads, driveways, highways, and RWG tile pieces—not only POI markers.
- spawnpoints.xml contains **87 spawnpoints**.
- biomes.png is 1024 × 1024 RGBA with five opaque colors. Exact counts are recorded in biome_palette.csv; human-readable biome labels remain visual inferences.
- dtm_processed.raw matches dtm.raw on 96.7202% of pixels. Most differences are small but a subset differ by several metres. Keep both alternatives until the target-version behavior is confirmed.

## Files committed in this GitHub folder

- world-analysis.json — structured dimensions, seed fields, DTM stats, coordinate validation, prefab/biome analysis.
- terrain-import-spec.md — source-to-Unreal transform proposal, intended architecture, and open questions.
- prefab-layout.md — placement counts and road-network implications.
- biome_palette.csv — exact RGBA counts with tentative labels.
- map_info.xml — preserved world generation metadata.
- source-manifest.csv — byte size, SHA-256 and CRC32 for all 18 original archive members, including source assets not committed here.
- source-inventory.md — human-readable description of every original archive member, sizes, roles, data assumptions, and retention caveats.
- binary-asset-upload.md — one-time instructions for adding the companion package's binary/source data.

## Companion reference package

The companion downloadable analysis ZIP prepared for this task contains the full-resolution **lossless 16-bit PNG conversions of both DTM heightfields**, original splat3/splat4 layers, normalized CSV of all 4175 prefab placements, all unique prefab counts, the three original screenshots, and a compressed source bundle that restores the original small XML/PNG assets. Both height conversions were checked sample-for-sample against their source RAW arrays.

That ZIP is delivered separately; it is **not currently committed to this branch**. This is a real storage limitation rather than an intentional omission: the connected GitHub editor can commit text but cannot stream a local 60–67 MB PNG into Git LFS. See binary-asset-upload.md. To make the repo alone sufficient for future work, one-time copying/commit of the package contents is still required.

## Reproduction strategy

Do not attempt to reproduce this specific world with seed/noise alone. Use the full 8192² original heightfield as the terrain input, then use the source biome raster and fixed prefab placements as separate inputs. Rebuild road planning only after the DTM has been mapped and verified against landmarks. For exact appearance, also preserve/test the source splat layers, water/rivers, materials, and game-version-compatible prefab definitions.

## Source integrity

Uploaded archive SHA-256: `10e4b2dd90a3b5a0d2b1368b48e9d47f5e4546835811ceca6d9b704ee95dbfaa`. Full file fingerprints are in source-manifest.csv.
