# One-time retention of the source-world data in GitHub

The full-resolution DTM must be present in the repo if future work is to reproduce this exact world without needing the original archive or companion download again. The connected GitHub editor used for this task can commit text but cannot stream local binary artifacts into Git LFS. The companion download therefore needs one local checkout commit.

## Files to copy from the companion reference ZIP

Extract the downloadable archive and copy these items into `Docs/WorldReference/WestBiyoveValley/`:

- `heightmap_u16_lossless.png` and `heightmap_processed_u16_lossless.png` — each is an exact 8192×8192 uint16-preserving conversion of its matching RAW file. Both were checked sample-for-sample.
- `source_layers/` — exact original `splat3*`, `splat4*`, and `radiation.png`.
- `prefab_placements.csv` and `prefab_name_counts.csv` — all 4175 placement records/rotations and all 1107 unique names/counts.
- `core-source-bundle.zlib.b64` and `decode_core_bundle.py` — restores exact `map_info.xml`, `prefabs.xml`, `spawnpoints.xml`, `checksums.txt`, `main.ttw`, `biomes.png`, and `radiation.png`.
- `original_screenshots/`, `biome_palette.csv`, and the visual previews if you want the complete research artefact retained together.
- `analysis_artifact_manifest.csv` — sizes and SHA-256 checksums for the exported/derived artifacts.

The reference package preserves all source archive members: the two raw height files are represented by verified lossless uint16 PNGs (both sample arrays are identical), the original splat layers and screenshots are retained as PNGs, and small original files can be restored from the source bundle.

## Git LFS upload for the two 16-bit height maps

From the extracted package, copy the two large height PNGs into the folder above, then from a local checkout on branch `feature/transvoxel-octree-terrain`:

```powershell
git lfs install
git lfs track "Docs/WorldReference/WestBiyoveValley/heightmap*_u16_lossless.png"
git add .gitattributes
git add Docs/WorldReference/WestBiyoveValley
git commit -m "Store West Biyove Valley source data and heightfields"
git push origin feature/transvoxel-octree-terrain
```

The height maps are about 60.7 MB and 66.8 MB. The original `dtm.raw` and `dtm_processed.raw` files are each 128 MiB, above GitHub's normal 100 MiB file limit. Git LFS should contain the **real uploaded object**, not just a pointer. If Git LFS is unavailable, use another external release/object-storage method and record its immutable URL and checksum.

## Why this one-time step is necessary

The written analysis and checksums are already in this GitHub folder, but the large binary map assets and complete placement table are delivered in the companion ZIP rather than this branch. Until those files are copied and pushed, this folder alone describes the world but does not contain enough data to sample every original terrain height in a future session.
