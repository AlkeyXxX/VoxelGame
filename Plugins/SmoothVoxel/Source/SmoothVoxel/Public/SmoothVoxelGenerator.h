
#pragma once

#include "CoreMinimal.h"
#include "SmoothVoxelTypes.h"

struct FSVoxelGenerationSettings
{
	int32 CellsPerAxis = 32;
	float VoxelSize = 100.0f;
	float IsoLevel = 0.0f;

	int32 Seed = 1337;

	// Terrain
	float BaseHeight = 900.0f;
	float HeightAmplitude = 850.0f;

	// IMPORTANT:
	// At VoxelSize=100, do not use very high-frequency detail.
	float NoiseScale = 0.0035f;

	float DetailScale = 0.0050f;
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
};

class FSVoxelGenerator
{
public:

	static void Generate(
		const FSVoxelGenerationSettings& Settings,
		FSVoxelMeshData& OutMesh);

private:

	static float Density(
		float X,
		float Y,
		float Z,
		const FSVoxelGenerationSettings& S);

	static FVector Interpolate(
		const FVector& A,
		const FVector& B,
		float VA,
		float VB,
		float Iso);

	static FVector DensityGradient(
		float X,
		float Y,
		float Z,
		float Step,
		const FSVoxelGenerationSettings& S);

	// 2D terrain noise
	static float HashNoise2D(
		float X,
		float Y,
		int32 Seed);

	static float FBM2D(
		float X,
		float Y,
		int32 Seed);

	// Continuous 3D noise for caves
	static float HashNoise3D(
		int32 X,
		int32 Y,
		int32 Z,
		int32 Seed);

	static float SmoothNoise3D(
		float X,
		float Y,
		float Z,
		int32 Seed);

	static float FBM3D(
		float X,
		float Y,
		float Z,
		int32 Seed);
};

