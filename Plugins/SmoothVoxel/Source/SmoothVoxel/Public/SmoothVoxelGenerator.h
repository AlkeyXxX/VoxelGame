
#pragma once

#include "CoreMinimal.h"
#include "SmoothVoxelTypes.h"

// Sparse block-cell edits: +1 adds smooth Dirt/Sand, negative values preserve legacy cell-removal calls, +2 reserves a separately rendered cube. Ray mining uses FSVoxelMinedNodeSet.
using FSVoxelBlockEditMap = TMap<FIntVector, int8>;
using FSVoxelBlockEditCenterMap = TMap<FIntVector, FVector>;
// Fully mined density-lattice nodes. Keys identify exact grid sample coordinates,
// so terrain mining does not depend on a moving fractional ray-hit location.
using FSVoxelMinedNodeSet = TSet<FIntVector>;

struct FSVoxelGenerationSettings
{
	int32 CellsPerAxis = 32;
	float VoxelSize = 100.0f;

	// World-space gradient distance shared by every LOD to keep surface normals
	// consistent when adjacent chunks use different sample spacings.
	float NormalSampleStep = 50.0f;

	// Used to reconstruct precise noise coordinates from the absolute voxel
	// lattice instead of adding small local offsets to a large float origin.
	float BaseVoxelSize = 100.0f;
	FIntVector ChunkOriginCells = FIntVector::ZeroValue;
	bool bUseChunkLatticeCoordinates = false;

	float IsoLevel = 0.0f;

	int32 Seed = 1337;

	// Terrain
	float BaseHeight = 900.0f;
	float HeightAmplitude = 850.0f;

	// Frequencies are in inverse centimetres. The values stay representable
	// up to the 16 m maximum cell size supported by the streaming world.
	float NoiseScale = 0.00005f;

	float DetailScale = 0.0000375f;
	float DetailAmplitude = 60.0f;

	// Solid bottom
	float BottomDepth = 2000.0f;

	// Caves
	bool bEnableCaves = false;

	float CaveScale = 0.0030f;
	float CaveThreshold = 0.58f;
	float CaveStrength = 1.0f;

	// World position of this chunk.
	// Allows neighboring chunks to use the same continuous noise field.
	FVector ChunkWorldOrigin = FVector::ZeroVector;

	// Immutable snapshots shared safely with asynchronous mesh workers.
	TSharedPtr<const FSVoxelBlockEditMap, ESPMode::ThreadSafe> BlockEdits;
	// Optional centres for explicit block-cell edits. The ray-mining path uses
	// exact density-grid nodes instead, stored in MinedNodes below.
	TSharedPtr<const FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe> BlockEditCenters;
	// Immutable grid-node cuts are separate from placed-cell edits so mining
	// progress can never collide with a block placement at the same integer key.
	TSharedPtr<const FSVoxelMinedNodeSet, ESPMode::ThreadSafe> MinedNodes;
};

class FSVoxelGenerator
{
public:

	/** Sample the same deterministic signed density field used by mesh generation.
	 * Positive values represent solid terrain; negative values represent air.
	 * Coordinates are world-space Unreal units (centimetres by default).
	 */
	static float SampleDensity(
		double WorldX,
		double WorldY,
		double WorldZ,
		const FSVoxelGenerationSettings& Settings);

	/** Sample using chunk-local centimetres and an integer absolute chunk origin. */
	static float SampleDensityAtChunkLocal(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings);

	/** Evaluate the deterministic world-space terrain surface height for a column. */
	static double SampleSurfaceHeight(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** Height-only surface sample, shared by every Z sample in one vertical column. */
	static double SampleSurfaceHeightAtChunkLocal(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings);

	/** Sample density using a precomputed surface height to avoid re-evaluating 2D terrain noise for each Z layer. */
	static float SampleDensityAtChunkLocalWithSurfaceHeight(
		const FVector& LocalPosition,
		double SurfaceHeight,
		const FSVoxelGenerationSettings& Settings);

	/** Clamp the FBM frequencies so its highest octave is representable at the
	 * coarsest voxel step, without changing the density field from LOD to LOD.
	 */
	static void ClampNoiseScalesForMaxSampleStep(
		FSVoxelGenerationSettings& Settings,
		float CoarsestVoxelStep);

	static void Generate(
		const FSVoxelGenerationSettings& Settings,
		FSVoxelMeshData& OutMesh);

private:

	static float Density(
		double X,
		double Y,
		double Z,
		const FSVoxelGenerationSettings& S);

	static float DensityFromSurfaceHeight(
		double X,
		double Y,
		double Z,
		double SurfaceHeight,
		const FSVoxelGenerationSettings& S);

	static FVector Interpolate(
		const FVector& A,
		const FVector& B,
		float VA,
		float VB,
		float Iso);

	static FVector DensityGradient(
		double X,
		double Y,
		double Z,
		float Step,
		const FSVoxelGenerationSettings& S);

	// 2D terrain noise
	static float HashNoise2D(
		double X,
		double Y,
		int32 Seed);

	static float FBM2D(
		double X,
		double Y,
		int32 Seed);

	// Continuous 3D noise for caves
	static float HashNoise3D(
		int64 X,
		int64 Y,
		int64 Z,
		int32 Seed);

	static float SmoothNoise3D(
		double X,
		double Y,
		double Z,
		int32 Seed);

	static float FBM3D(
		double X,
		double Y,
		double Z,
		int32 Seed);
};

