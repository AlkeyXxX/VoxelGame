# Prefab and road-layout findings

Source file: prefabs.xml, SHA-256 e89500ca051d77195365443deead297be6f61032ce2ca05251150993c137ea44.

The XML contains **4175 entries**, all decoration records with attributes type, name, position, and rotation; there are **1107 distinct names**. Position tuples are stored as x,y,z. World X and Z span approximately [-3923, 3807] and [-3920, 3847]; prefab Y spans [2,143].

This file must not be treated as a simple list of POIs:
- **1916** entries have names beginning part_, including road/driveway/highway transitions.
- **372** entries have names beginning rwg_tile_, which are road/network tile pieces.
- **25** entries have names beginning road.
- Several hundred additional entries are houses, town lots, urban buildings, traders, caves, camps, and other placed models.

Most frequent examples:

| Prefab name | Count |
|---|---:|
| part_highway_transition | 116 |
| part_driveway_countryresidential_07 | 66 |
| part_driveway_industrial_01 | 58 |
| part_driveway_countryresidential_06 | 48 |
| part_driveway_rural_08 | 43 |
| part_driveway_rural_06 | 42 |
| part_driveway_industrial_08 | 40 |
| part_driveway_countryresidential_01 | 37 |
| part_driveway_rural_02 | 35 |
| part_driveway_countryresidential_09 | 33 |
| part_driveway_countrytown_10 | 32 |
| rwg_tile_rural_cap | 31 |

The exact original XML is preserved losslessly as prefabs.xml.zlib.b64; it is not necessary to repeat the upload to recover the entries. Decode it with decode_reference_assets.py. The output file SHA-256 must equal the hash above.

spawnpoints.xml contains **87** spawn points, spanning X [-1541,1429], Y [31.40749,95.48764], Z [-1240,1489].

## Interpreting the screenshots

- Screenshot “terrain + POIs as squares”: emphasizes clustered prefab/building regions over exact terrain shading.
- Screenshot “terrain + roads”: road alignments are easier to read; a red road network connects towns through forest, desert/burnt zones, wasteland, and mountainous/snow regions.
- Black/white/ochre/green/purple relief regions align with the biome mask. The map forms a large central forest region; a wasteland zone is on the west/southwest; a desert-like region occupies much of the north/northwest; an ochre/burnt zone covers much of the south; pale mountain/snow terrain dominates the east/northeast.
- These regional names are visual inference from the supplied image/palette. Preserve raw RGBA values as source truth until confirmed against the game palette.

The 8192² splat3.png / splat3_processed.png layers and their half-resolution versions are separate inputs and fingerprinted in source-manifest.csv. They may carry terrain-material / road visualization details not represented completely by prefab positions; those large layers are not yet stored in this branch.
