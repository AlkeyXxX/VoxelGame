#pragma once

#include "CoreMinimal.h"
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelTypes.h"

/**
 * Candidate regular/transition cell mesher for the separate smooth terrain path.
 *
 * This backend uses an independently derived 0BSD Transvoxel-style cell table
 * set. It is intentionally separate from the existing classic-MC prototype
 * until visual, seam, collision and performance qualification is complete.
 */
class SMOOTHVOXEL_API FSVoxelTransvoxelMesher
{
public:
	/**
	 * Builds one chunk from the same world-space density function at any LOD.
	 *
	 * VoxelSize is the sample spacing for this chunk. A set face bit indicates
	 * that this chunk is the coarser neighbour on that face and a transition
	 * band must be emitted there. TransitionFaceMask must come from a balanced
	 * octree whose neighbouring LODs differ by no more than one.
	 *
	 * Output vertices are local to Settings.ChunkWorldOrigin.
	 */
	static void Generate(
		const FSVoxelGenerationSettings& Settings,
		int32 CellsPerAxis,
		float VoxelSize,
		uint8 TransitionFaceMask,
		FSVoxelMeshData& OutMesh);
};
