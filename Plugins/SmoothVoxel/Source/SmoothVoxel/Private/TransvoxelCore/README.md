# Transvoxel candidate backend

The code and lookup tables in this folder are from the independent clean-room
0BSD core at https://github.com/BoQsc/Transvoxel-0BSD, mirrored without source
license changes. The core is not byte-identical to Eric Lengyel's MIT-licensed
Transvoxel.cpp and does not claim identical interior triangulation in every
case. See LICENSE and the upstream project's README_CORE.txt.

The wrapper in SmoothVoxelTransvoxelMesher.cpp samples the same signed density
field for regular cells at every LOD and emits transition cells on faces
identified by the balanced octree. The implementation remains experimental
until the UE4.27 automation tests and visual tests pass on six face orientations,
edge/corner junctions, collision, streaming and edits. It does not replace the
legacy AVoxelWorld path in this change.
