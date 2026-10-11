#pragma once

#include "CoreMinimal.h"

/** Bit flags identifying a coarse chunk face that needs Transvoxel transition cells. */
enum ESVoxelTransitionFaceMask : uint8
{
	SVoxelTransitionFace_NegativeX = 1 << 0,
	SVoxelTransitionFace_PositiveX = 1 << 1,
	SVoxelTransitionFace_NegativeY = 1 << 2,
	SVoxelTransitionFace_PositiveY = 1 << 3,
	SVoxelTransitionFace_NegativeZ = 1 << 4,
	SVoxelTransitionFace_PositiveZ = 1 << 5
};

/**
 * A leaf in a spatial octree used by the smooth-terrain LOD selector.
 *
 * OriginCells is expressed on the LOD-0 sample lattice, not in Unreal units.
 * A node at Lod L spans CellsPerAxis * (1 << L) cells on each axis.
 *
 * The selector deliberately does not create meshes. In particular, selecting
 * a balanced hierarchy is not enough to make mixed-resolution surfaces crack
 * free; the renderer must also build the corresponding transition cells.
 */
struct FSVoxelOctreeNode
{
	FIntVector OriginCells = FIntVector::ZeroValue;
	int32 Lod = 0;

	FSVoxelOctreeNode() = default;

	FSVoxelOctreeNode(const FIntVector& InOriginCells, int32 InLod)
		: OriginCells(InOriginCells)
		, Lod(InLod)
	{
	}

	int32 GetSpanInBaseCells(int32 CellsPerAxis) const
	{
		return CellsPerAxis * (1 << Lod);
	}

	FVector GetWorldOrigin(const FVector& WorldOrigin, float BaseVoxelSize) const
	{
		return WorldOrigin + FVector(OriginCells.X, OriginCells.Y, OriginCells.Z) * BaseVoxelSize;
	}

	float GetWorldSize(int32 CellsPerAxis, float BaseVoxelSize) const
	{
		return static_cast<float>(GetSpanInBaseCells(CellsPerAxis)) * BaseVoxelSize;
	}

	friend bool operator==(const FSVoxelOctreeNode& A, const FSVoxelOctreeNode& B)
	{
		return A.OriginCells == B.OriginCells && A.Lod == B.Lod;
	}

	friend bool operator!=(const FSVoxelOctreeNode& A, const FSVoxelOctreeNode& B)
	{
		return !(A == B);
	}
};

FORCEINLINE uint32 GetTypeHash(const FSVoxelOctreeNode& Node)
{
	return HashCombine(::GetTypeHash(Node.OriginCells), ::GetTypeHash(Node.Lod));
}

struct FSVoxelOctreeSelectionSettings
{
	/** Number of sampled cells across a rendered chunk at LOD 0. */
	int32 CellsPerAxis = 32;

	/** Size of a single LOD-0 cell in Unreal units (UE centimetres by default). */
	float BaseVoxelSize = 100.0f;

	/** Maximum LOD allowed for rendered leaves. */
	int32 MaxLod = 4;

	/** LOD of the streaming root. -1 preserves the original MaxLod-sized root. */
	int32 RootLod = -1;

	/** Camera-to-node distance threshold, measured in node side lengths. */
	float SplitDistanceMultiplier = 0.75f;

	/** Conservative hard safety cap for selected leaves while streaming is being optimized. */
	int32 MaxLeafCount = 2048;

	/** World-space location represented by base-lattice cell coordinate (0,0,0). */
	FVector WorldOrigin = FVector::ZeroVector;

	/** Absolute base-lattice coordinate of this streaming root's lower corner.
	 * Adjacent root windows differ by aligned half-root shifts so overlapping
	 * chunks retain identical world-space node keys and can be reused.
	 */
	FIntVector RootOriginCells = FIntVector::ZeroValue;
};

/**
 * Builds a view-dependent octree leaf set and enforces a 2:1 neighbour rule.
 *
 * This is a pure CPU spatial selector. It intentionally does not spawn actors
 * or modify the existing block-based AVoxelWorld. A mixed-LOD renderer must
 * consume the returned leaves only after transition-cell meshing is connected.
 */
class SMOOTHVOXEL_API FSVoxelOctreeSelector
{
public:
	/**
	 * Returns false when input is invalid or the safety cap prevents balancing.
	 * On failure OutLeaves is cleared, so callers cannot accidentally render a
	 * partially balanced hierarchy.
	 */
	static bool BuildBalancedLeaves(
		const FVector& CameraWorldLocation,
		const FSVoxelOctreeSelectionSettings& Settings,
		TArray<FSVoxelOctreeNode>& OutLeaves);

	/** Build one six-bit transition-face mask per leaf. A bit is set only on a
	 * coarser leaf face adjacent to a finer neighbour. Leaves must come from a
	 * valid balanced selection produced by BuildBalancedLeaves.
	 */
	static bool BuildTransitionFaceMasks(
		const TArray<FSVoxelOctreeNode>& Leaves,
		int32 CellsPerAxis,
		int32 MaxLod,
		TArray<uint8>& OutFaceMasks,
		const FIntVector& RootOriginCells = FIntVector::ZeroValue,
		int32 RootLod = -1);
};
