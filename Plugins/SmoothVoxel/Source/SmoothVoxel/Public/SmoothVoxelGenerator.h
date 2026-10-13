
#pragma once

#include "CoreMinimal.h"
#include "SmoothVoxelTypes.h"
#include "VoxelTypes.h"

// Sparse block-cell edits: +1 adds smooth Dirt/Sand, negative values preserve legacy air cuts, and +2 reserves a separately rendered cube without changing terrain density. Ray mining uses FSVoxelMinedNodeSet.
using FSVoxelBlockEditMap = TMap<FIntVector, int8>;
using FSVoxelBlockEditCenterMap = TMap<FIntVector, FVector>;
// Fully mined voxel cells. Keys are integer cell coordinates (not density-grid
// corners), so each cut is centred at (Cell + 0.5) * BaseVoxelSize and stays
// aligned as the ray marches down through a shaft.
using FSVoxelMinedNodeSet = TSet<FIntVector>;
// Seed-generated road blocks are sampled directly by the terrain density field.
// The shared immutable map avoids copying thousands of road cells into every chunk.
using FSVoxelRoadBlockMap = TMap<FIntVector, EVoxelBlock>;
// Continuous top elevation for each generated road cell, keyed by the same cell.
using FSVoxelRoadSurfaceHeightMap = TMap<FIntVector, double>;
using FSVoxelRoadSampleBinSet = TSet<FIntVector>;

// Runtime copy of the immutable 7 Days to Die source rasters. The DTM stores
// 8192x8192 little-endian uint16 samples; biome IDs are one byte per 1024x1024
// pixel; road types are four 2-bit samples packed into each byte at 8192x8192.
struct FSVoxelReferenceWorldData
{
    int32 HeightWidth = 8192;
    int32 HeightHeight = 8192;
    TArray<uint8> HeightBytesLE;
    // Exact source DTM envelope in Unreal centimetres; used to cull chunks safely.
    double MinHeightCentimeters = 0.0;
    double MaxHeightCentimeters = 0.0;

    int32 BiomeWidth = 1024;
    int32 BiomeHeight = 1024;
    TArray<uint8> BiomeIds;

    int32 RoadWidth = 8192;
    int32 RoadHeight = 8192;
    TArray<uint8> PackedRoadMask;

    bool HasHeightField() const;
    bool HasBiomeMask() const;
    bool HasRoadMask() const;
    double SampleHeightCentimeters(
        double WorldX, double WorldY, double WorldCenterX, double WorldCenterY) const;
    ESVoxelBiome SampleBiome(
        double WorldX, double WorldY, double WorldCenterX, double WorldCenterY) const;
    EVoxelBlock SampleRoadType(
        double WorldX, double WorldY, double WorldCenterX, double WorldCenterY) const;
};
using FSVoxelReferenceWorldDataPtr =
    TSharedPtr<const FSVoxelReferenceWorldData, ESPMode::ThreadSafe>;

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
	float CoarsestSurfaceSampleStep = 100.0f;
	// Largest supported terrain surface sample interval across active LODs.
	// Feature masks use this shared scale so terrain chunks and fixed water
	// surfaces agree on the minimum size of basins and other narrow features.
	FIntVector ChunkOriginCells = FIntVector::ZeroValue;
	bool bUseChunkLatticeCoordinates = false;

	float IsoLevel = 0.0f;

	// Road carriageways use the terrain mesh, but may disable their road-face
	// collision independently from ordinary terrain chunk collision.
	bool bEnableRoadCollision = true;

	// When loaded, the source DTM replaces procedural height noise entirely.
	bool bUseReferenceWorldData = false;
	FSVoxelReferenceWorldDataPtr ReferenceWorldData;

	int32 Seed = 555;

	// Biomes follow one stable layout tied to the finite map rather than independent
	// climate patches. Distances are normalized to the selected world footprint.
	bool bEnableBiomes = true;
	float BiomeScale = 0.0000075f;
	FVector BiomeWorldCenter = FVector::ZeroVector;
	float BiomeWorldSize = 400000.0f;

	// Deterministic clearings reserved for future POIs.
	bool bEnablePOIPlateaus = true;
	float POISiteSpacing = 45000.0f;
	float POIPlateauRadius = 4500.0f;
	float POIPlateauBlendDistance = 3000.0f;

	// Large-scale landform patches; flatness is shared by terrain and water masks.
	float FlatlandRegionScale = 0.00002f;
	// Half of non-mountain terrain is flat, giving roughly 40% plains / 40% hills
	// after the independent 20% mountain mask is applied.
	float FlatlandCoverage = 0.5f;
	float FlatlandCoverageMultiplier = 1.0f;
	// Broad, low-slope height variation added only where the main hill mask is low.
	float GentleReliefHeight = 800.0f;
	float FlatlandNoiseAmplitude = 50.0f;
	float HillScale = 0.00002f;
	float HillHeight = 4000.0f;
	float DesertHillCoverage = 0.5f;
	float MountainBiomeThreshold = 0.80f;
	float MountainCoverage = 0.20f;
	float MountainHeight = 12000.0f;

	float BaseHeight = 900.0f;
	float HeightAmplitude = 3500.0f;
	float NoiseScale = 0.00005f;
	float DetailScale = 0.000025f;
	float DetailAmplitude = 60.0f;

	bool bEnableRivers = true;
	float RiverScale = 0.000004f;
	float RiverHalfWidth = 500.0f;
	float RiverDepth = 200.0f;
	float RiverWaterLevel = 850.0f;

	bool bEnableLakes = true;
	float LakeSiteSpacing = 50000.0f;
	float LakeRadius = 4000.0f;
	float LakeShoreBlendDistance = 1800.0f;
	float LakeDepth = 500.0f;
	float LakeChance = 0.3f;
	float HillLakeChance = 0.15f;

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
	// integer voxel cells instead, stored in MinedNodes below.
	TSharedPtr<const FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe> BlockEditCenters;
	// Immutable mined-cell cuts are separate from placed-cell occupancy so mining
	// progress can never collide with a block placement at the same integer key.
	TSharedPtr<const FSVoxelMinedNodeSet, ESPMode::ThreadSafe> MinedNodes;
	// Road block removals only suppress the generated road SDF; they must not
	// carve natural terrain underneath the carriageway.
	TSharedPtr<const FSVoxelMinedNodeSet, ESPMode::ThreadSafe> MinedRoadBlocks;
	// Road carriageways are part of the scalar terrain field, not separate cube actors.
	TSharedPtr<const FSVoxelRoadBlockMap, ESPMode::ThreadSafe> RoadBlocks;
	// Continuous deck elevations prevent the road's top from snapping to voxel planes.
	TSharedPtr<const FSVoxelRoadSurfaceHeightMap, ESPMode::ThreadSafe> RoadSurfaceHeights;
	// Coarse 3D occupancy bins let density queries reject samples far from any
	// road with one set lookup instead of 27 TMap searches.
	TSharedPtr<const FSVoxelRoadSampleBinSet, ESPMode::ThreadSafe> RoadSampleBins;
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

	/** Load and validate the source DTM, categorical biome raster and packed road mask. */
	static FSVoxelReferenceWorldDataPtr LoadReferenceWorldData(
		const FString& HeightmapPath,
		const FString& BiomeMaskPath,
		const FString& RoadMaskPath,
		FString& OutStatus);

	/** Evaluate the deterministic world-space terrain surface height for a column. */
	static double SampleSurfaceHeight(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** Deterministic 0..1 river-channel mask used by both terrain carving and water rendering. */
	static float SampleRiverInfluence(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** Flatland suitability mask shared with rivers and POI planning. */
	static float SampleFlatlandInfluence(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** Deterministic lake mask and its absolute water elevation. */
	static void SampleLakeMaskAndLevel(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings,
		float& OutInfluence,
		float& OutWaterLevel);

	/** Returns the channel mask and the signed field used to recover the local river direction. */
	static void SampleRiverMaskAndField(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings,
		float& OutInfluence,
		float& OutSignedField,
		float* OutBankInfluence = nullptr);

	/** Sample the macro terrain noise at a global XY coordinate. */
	static float SampleTerrainNoise(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** Return the signed temperature, moisture, and mountain climate signals. */
	static void SampleBiomeClimate(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings,
		float& OutTemperature,
		float& OutMoisture,
		float& OutMountainSignal);

	/** Return world-relative desert, winter, and central temperate biome weights (each 0..1). */
	static void SampleBiomeRegionWeights(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings,
		float& OutDesertWeight,
		float& OutWinterWeight,
		float& OutTemperateWeight);

	/** Classify biome at a deterministic global XY coordinate. */
	static ESVoxelBiome SampleBiome(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** Deterministic location of the POI candidate whose grid cell contains this world position. */
	static FVector2D SamplePOISitePosition(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** 0..1: high values indicate a generated flat clearing suitable for a POI. */
	static float SamplePOISuitability(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	/** Per-vertex tint for materials that consume the ProceduralMesh vertex-color channel. */
	static FLinearColor GetBiomeTint(
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

	static double SampleUnflattenedSurfaceHeight(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings);

	static float GetPOIPlateauInfluence(
		double WorldX,
		double WorldY,
		const FSVoxelGenerationSettings& Settings,
		double& OutSiteX,
		double& OutSiteY);

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

