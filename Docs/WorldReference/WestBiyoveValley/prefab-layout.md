# Prefab and road-layout findings

Source file: `prefabs.xml`, SHA-256 `e89500ca051d77195365443deead297be6f61032ce2ca05251150993c137ea44`.

The XML contains **4175 entries**, all decoration records with attributes `type`, `name`, `position`, and `rotation`; there are **1107 distinct names**. Position tuples are stored as `x,y,z`. World X and Z span approximately `[-3923, 3807]` and `[-3920, 3847]`; prefab Y spans `[2,143]`.

This data is not a simple list of POIs:
- **1916** entries have names beginning `part_`, including road/driveway/highway parts.
- **372** entries have names beginning `rwg_tile_`, used by the road network.
- **25** entries have names beginning `road`.
- Other records include houses, town lots, urban buildings, traders, caves, camps, and other placed models.

Most frequent examples:

| Prefab name | Count |
|---|---:|
| `part_highway_transition` | 116 |
| `part_driveway_countryresidential_07` | 66 |
| `part_driveway_industrial_01` | 58 |
| `part_driveway_countryresidential_06` | 48 |
| `part_driveway_rural_08` | 43 |
| `part_driveway_rural_06` | 42 |
| `part_driveway_industrial_08` | 40 |
| `part_driveway_countryresidential_01` | 37 |
| `part_driveway_rural_02` | 35 |
| `part_driveway_countryresidential_09` | 33 |
| `part_driveway_countrytown_10` | 32 |
| `rwg_tile_rural_cap` | 31 |

The exact original XML and normalized `prefab_placements.csv` are inside the companion downloadable reference package, not yet committed in this folder. The CSV contains all 4175 placement records and rotations, so it is the more convenient import format for VoxelGame. The package also includes `prefab_name_counts.csv` with all unique names/counts and a compressed bundle that restores the original XML exactly.

`spawnpoints.xml` contains **87** spawnpoints, spanning X `[-1541,1429]`, Y `[31.40749,95.48764]`, Z `[-1240,1489]`.

## Visual observations from the supplied screenshots

- Screenshot with square POI markers emphasizes prefab/building clusters.
- Screenshot with road overlay makes the route topology easier to see: roads connect settlement clusters and cross multiple biome zones.
- Visually, a dark-green central region, purple west/southwest region, orange north/northwest region, ochre southern region, and pale mountain/snow-looking east/northeast area can be distinguished.
- These labels are visual interpretations, not a substitute for the exact game palette. The raw RGBA palette and counts are recorded in `biome_palette.csv`.

Original `splat3*` and `splat4*` layers may be useful for exact surface coloring/road-overlay reconstruction. The companion package includes the exact source-layer PNGs and fingerprints are recorded in `source-manifest.csv`.
