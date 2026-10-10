# Official Transvoxel lookup tables

This folder vendors the table data from Eric Lengyel's upstream repository:
https://github.com/EricLengyel/Transvoxel

- `TransvoxelTables.inl` is the upstream `Transvoxel.cpp` table source, kept
  with its original introductory copyright notice.
- `LICENSE` contains the upstream MIT license.
- The tables are included only by `SmoothVoxelTransvoxelMesher.cpp` to avoid
  multiple-definition problems.

The wrapper is a new UE4.27 integration layer; it does not copy the complete
Unity/Godot implementation. Keep the MIT notice and license with distributions.
The code is experimental until the in-engine six-face/edge/corner seam tests,
collision checks, streaming tests, and performance measurements pass.
