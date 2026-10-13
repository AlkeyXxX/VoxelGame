# West Biyove Valley — source-world reference

Purpose: preserve the analysis and import contract for the user-supplied West Biyove Valley 7 Days to Die world, and reproduce the same macro terrain in VoxelGame rather than generating a new landscape from unrelated noise.

## Confirmed source facts

- Game version in map_info.xml: **V.3.1.4**.
- Raster: **8192 × 8192**, Scale=1; nominal footprint **8.192 km × 8.192 km**.
- Map-level Seed: **-1058264646**. Nested Generation/Seed: **123**. These are separate metadata fields.
- RandomGeneratedWorld=true; FixedWaterLevel=false.
- dtm.raw: 134,217,728 bytes = 8192² little-endian uint16 samples; conversion is **height metres = sample / 256**. Height range: **1.0039–198.375 m**, mean **48.9082 m**.
- dtm_processed.raw differs from dtm.raw in about **3.2798%** of pixels, so retain both until source-version A/B testing is completed.
- Prefab coordinate check supports column ≈ round(world_x+4096), row ≈ round(world_z+4096). Across 4175 placements, median absolute difference between sampled DTM height and prefab Y is 0.855 m (mean 2.339 m, p90 6.145 m). This supports the global mapping; verify half-cell/row orientation in-game.
- prefabs.xml: **4175 placed entries**, **1107 distinct names**, including road/driveway/highway tiles and building POIs.
- spawnpoints.xml: **87 entries**.
- biomes.png: 1024 × 1024 with five opaque RGBA classes.
- splat3.png: 8192 × 8192 RGBA with **959,472 red road-mask pixels** and **480,670 green road-mask pixels**. Red is interpreted as asphalt/major road; green as dirt/minor road based on the supplied screenshot. Other pixels are transparent.

## Current VoxelGame implementation

Branch: `feature/transvoxel-octree-terrain`.

- In reference mode, `FSVoxelGenerator::SampleSurfaceHeight` uses bilinear samples of the original DTM and bypasses procedural hills, POI plateaus, micro-noise, and synthetic river/lake carving.
- The 8192² source map is centred on `RootWorldCenter`; one source pixel is 1 m. In Unreal units, the DTM returns height in centimetres.
- The 1024² biome raster is mapped into six mesh material sections (Plains fallback, Forest, Desert, Snow, Burnt Forest, Wasteland). Assign the corresponding material properties on `ASmoothVoxelOctreeWorld` to reproduce the colors/textures; if unassigned, the section falls back to the chunk's terrain material.
- The 2-bit packed splat3 road mask classifies asphalt/dirt faces in the same terrain chunk mesh and collision component. Source road cells are available to the mining/placement query without per-road actors.
- Reference mode defaults to 8.192 km. It suppresses unrelated seed-generated roads and synthetic flat water plane rather than layering them over the source.
- The loader reports missing/invalid files and falls back to procedural terrain. The DTM/biome/road files still need to be copied into the runtime path below; **large binary files have not yet been pushed into Git LFS by this editor connection**.

## Runtime raster files

Required files:
- `dtm.raw` — exact source height raster, 128 MiB.
- `biomes.raw` — 1 byte per biome pixel, 1 MiB; IDs: 1 Forest, 2 Desert, 3 Snow, 4 BurntForest, 5 Wasteland.
- `roadmask_2bit.raw` — 4 road pixels per byte, 16 MiB; two-bit codes: 0 none, 1 red/asphalt, 2 green/dirt, 3 unused.

Download the ready-to-extract [runtime rasters ZIP](https://github.com/AlkeyXxX/VoxelGame) is not stored in this repository; in this ChatGPT conversation it is supplied as `West_Biyove_Valley_Runtime_Assets.zip`. Extract it at the project root to populate `Docs/WorldReference/WestBiyoveValley/*.raw`. The loader also supports `Content/WorldReference/WestBiyoveValley/`. The project packaging config stages the Content directory for packaged builds.

## Files in this folder

- `world-analysis.json` — machine-readable source facts, DTM statistics, map seeds, coordinate check, biome palette, and prefab counts.
- `terrain-import-spec.md` — coordinate and height conversion contract and remaining validation tasks.
- `prefab-layout.md` — placement data interpretation and road/prefab implications.
- `source-inventory.md` — all 18 source archive members, roles, lengths and caveats.
- `source-manifest.csv` — source archive SHA-256, CRC32 and byte lengths.
- `analysis-artifact-manifest.csv` — hashes for derived analysis-package artifacts.
- `biome_palette.csv` and `map_info.xml` — exact palette counts and copied map metadata.
- `binary-asset-upload.md` — one-time LFS instructions to make the repository self-contained.

The companion analysis ZIP also contains lossless 16-bit PNG versions of both DTM files, original splat layers/screenshots, prefab-placement CSVs, full prefab name counts, and a decoder for compact source bundles. That package is delivered separately, not yet committed here.

## Accuracy boundaries

**Exact macro-heightfield and source road-mask layout** are the first implemented import milestone once runtime rasters are present. Exact rendered buildings/POIs still require compatible meshes/prefab definitions: the source world archive contains prefab *placements* and names, not all of 7DTD's building mesh assets. Actual water-body shapes are also not explicitly represented in the current runtime raster bundle; the synthetic water plane is hidden in reference mode to avoid inventing a contradictory layout until the source water representation is reconstructed. Do not call the whole rendered world one-to-one until these remaining items are addressed and the import is checked in UE4.27.

Archive SHA-256: `10e4b2dd90a3b5a0d2b1368b48e9d47f5e4546835811ceca6d9b704ee95dbfaa`.
