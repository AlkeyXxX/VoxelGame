# One-time runtime-data and Git LFS setup

The reference generator now reads three external RAW rasters. Their actual bytes must be present on disk; text documentation alone is insufficient for identical terrain.

## Fastest local setup

Download `West_Biyove_Valley_Runtime_Assets.zip` from the ChatGPT response and extract it at the local repository/project root. It contains:

- `Docs/WorldReference/WestBiyoveValley/dtm.raw` — the original 128 MiB DTM.
- `Docs/WorldReference/WestBiyoveValley/biomes.raw` — 1 MiB exact categorical biome IDs derived from the source `biomes.png`.
- `Docs/WorldReference/WestBiyoveValley/roadmask_2bit.raw` — 16 MiB packed red/green road classes derived from exact `splat3.png` pixels.

The code first looks for the configured `Content/WorldReference/WestBiyoveValley/` paths, then falls back to these `Docs/` paths and `Saved/WorldReference/WestBiyoveValley/`. After extraction, use the output log to check that the DTM and both masks report as loaded. If you see the warning “source DTM unavailable”, the fallback procedural terrain is active instead.

## Make the repository self-contained (recommended)

A root `.gitattributes` is configured to track `*.raw` in these two paths using Git LFS:

- `Docs/WorldReference/WestBiyoveValley/`
- `Content/WorldReference/WestBiyoveValley/`

From a local checkout on branch `feature/transvoxel-octree-terrain`:

```powershell
git lfs install
# Extract the runtime ZIP at the repository root.
git add Docs/WorldReference/WestBiyoveValley/*.raw
git commit -m "Store West Biyove Valley source terrain rasters"
git push origin feature/transvoxel-octree-terrain
```

The DTM exceeds GitHub's normal 100 MiB per-file limit; the .gitattributes rules route it through LFS. The actual large-file object must be pushed, not just a pointer. If Git LFS reports quota/permission errors, keep the runtime ZIP archived externally and record its SHA-256.

For a packaged build, the project packaging settings stage `Content/WorldReference/WestBiyoveValley` as UFS. For best portability, copy the three files into that Content directory and commit them via LFS as well. Test the packaged build separately; the source data is not a UE-imported texture asset and should not be treated as a normal uasset.

## Re-generating the masks

`prepare_runtime_assets.py` converts the source `biomes.png` and `splat3.png` into the exact two raw mask files. It requires Python 3, Pillow, and NumPy. The DTM itself is copied byte-for-byte from the original archive, not re-saved from the PNG preview.

The code connection in this session can create commits and text files, but cannot stream local binary assets into Git LFS. The runtime ZIP is provided separately to bridge this one-time binary step.
