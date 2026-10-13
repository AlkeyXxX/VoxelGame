# Source archive inventory — West Biyove Valley

Source archive: `West Biyove Valley.7z`, 138,447,232 bytes, SHA-256 `10e4b2dd90a3b5a0d2b1368b48e9d47f5e4546835811ceca6d9b704ee95dbfaa`. It contains 18 members. Exact file hashes, byte sizes, and CRC32 values are in `source-manifest.csv`.

| Original member | Size | Interpretation / use |
|---|---:|---|
| `dtm.raw` | 134,217,728 B | Full 8192² uint16 terrain heightfield; empirical metres = value / 256. |
| `dtm_processed.raw` | 134,217,728 B | Same dimensions; 96.7202% of pixel values match `dtm.raw`, but some differ materially. Keep as an alternate source until compared in-game. |
| `biomes.png` | 35,267 B | 1024² RGBA biome/class mask with five opaque colors. Exact counts are in `biome_palette.csv`; label mapping remains tentative. |
| `splat3.png` | 1,873,078 B | 8192² RGBA source overlay/material layer; preserve exactly, channel semantics still need confirmation. |
| `splat3_half.png` | 1,469,234 B | 4096² half-size counterpart. |
| `splat3_processed.png` | 2,780,497 B | 8192² processed counterpart. |
| `splat4.png` | 1,277,801 B | 8192² RGBA second overlay/material layer; preserve exactly. |
| `splat4_half.png` | 523,742 B | 4096² half-size counterpart. |
| `splat4_processed.png` | 1,474,614 B | 8192² processed counterpart. |
| `radiation.png` | 1,225 B | 256² RGBA source image; retain for compatible map rendering. |
| `prefabs.xml` | 408,361 B | 4175 placed decoration/prefab records; names and coordinates include road/driveway pieces, not only POIs. |
| `spawnpoints.xml` | 5,862 B | 87 spawn points with positions and rotations. |
| `map_info.xml` | 1,221 B | World size, map version, top-level seed and nested generation parameters. |
| `checksums.txt` | 363 B | Original per-file CRC32 values; checked against extracted source members. |
| `main.ttw` | 263 B | Small binary world metadata marker. Its header includes the text `V 3.0.1 (b4)`, whereas `map_info.xml` reports `V.3.1.4`; preserve both facts rather than assuming these version markers mean the same thing. |
| `map info xml.png` | 55,436 B | User screenshot of the source XML/world info as shown on the website. |
| `скрин с сайта, рельеф и POI.png` | 2,040,860 B | User screenshot of the full world with POI square markers. |
| `скрин с сайта, рельеф и дороги.png` | 2,067,643 B | User screenshot of the full world with a clearer road overlay. |

## Derived facts and cautions

- Map raster is 8192×8192 with `Scale=1`; nominal footprint is 8.192×8.192 km.
- Map-level Seed is `-1058264646`; nested `Generation/Seed` is `123`. Do not collapse them into a single seed.
- The coordinate hypothesis `column ≈ round(x+4096)`, `row ≈ round(z+4096)`, and `height_m ≈ dtm.raw[row,column]/256` matches all 4175 prefab Y coordinates with median absolute difference 0.855 m. Confirm coordinate row direction, half-cell alignment and origin using known landmarks in Unreal before treating it as final.
- Both RAW files are 128 MiB, above GitHub's ordinary 100 MiB file limit. The companion reference package contains lossless 16-bit PNG conversions for both files; they were compared against the original sample arrays and matched exactly.
- The companion package also contains all original `splat3*`/`splat4*` layers and screenshots, plus a compressed bundle restoring small source assets. The package has not yet been pushed into GitHub due to the binary-upload limitation documented in `binary-asset-upload.md`.
- For true world reproduction, use the source height raster and the original placement list. Seed-only procedural generation is not expected to reconstruct the exact same terrain and roads.
