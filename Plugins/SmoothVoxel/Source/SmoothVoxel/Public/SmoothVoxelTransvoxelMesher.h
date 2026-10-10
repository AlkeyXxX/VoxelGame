#pragma once

#include "CoreMinimal.h"
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelTypes.h"

/**
 * Experimental regular/transition cell mesher for the separate smooth terrain path.
 *
 * This backend uses Eric Lengyel's official Transvoxel lookup tables under the MIT
 * license. The tables are kept in a separate translation-unit include with the
 * upstream copyright notice and license. Runtime integration still needs seam,
 * collision, and performance qualification.
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
	/** Exhaustively sanity-checks all 256 regular and 512 transition table cases. */
	static bool ValidateLookupTables();

	static void Generate(
		const FSVoxelGenerationSettings& Settings,
		int32 CellsPerAxis,
		float VoxelSize,
		uint8 TransitionFaceMask,
		FSVoxelMeshData& OutMesh);
};
