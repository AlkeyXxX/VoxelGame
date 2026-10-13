# One-time binary-asset retention

The source files dtm.raw and dtm_processed.raw are each 128 MiB; GitHub blocks ordinary Git files above 100 MiB and recommends Git LFS or release assets for large binaries. Official limit details: https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github

The GitHub connection used for these edits can create UTF-8 text files and commits, but it does not offer a way to stream the local binary file into Git LFS. Therefore, no LFS pointer has been inserted without its payload — that would produce a broken reference.

A lossless conversion is ready outside the repository as heightmap_u16_lossless.png (60,669,777 bytes). Its 8192×8192 uint16 pixel array was checked against dtm.raw and matches every sample exactly. Use that file for a one-time local upload, or upload the original raw file if preferred.

From a local checkout on the correct branch:

    git lfs install
    git lfs track "Docs/WorldReference/WestBiyoveValley/*.png"
    git add .gitattributes
    # Copy heightmap_u16_lossless.png into Docs/WorldReference/WestBiyoveValley/
    git add Docs/WorldReference/WestBiyoveValley/heightmap_u16_lossless.png
    git commit -m "Store West Biyove Valley lossless heightmap source"
    git push origin feature/transvoxel-octree-terrain

Do not add only an LFS pointer: the real LFS object must also be uploaded. Once the height asset is in LFS, update source-manifest.csv with its SHA-256 and have the importer read it losslessly. The original large splat3* and splat4* PNGs may be added by the same process if exact texture/road overlays are required.
