#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "VoxelMesher.h"

struct FVoxelMarchingCubesBuildInput
{
    int32 Size = 32;
    float VoxelSize = 100.0f;

    // Number of UV repeats per voxel block (0.5 = one repeat every two blocks).
    float UVScalePerBlock = 0.5f;
    float RoadUVScalePerBlock = 0.5f;

    /*
     * Density support is derived from blocks in the range [-1, Size].
     * Array side = Size + 2.
     */
    TArray<uint8> Blocks;

    /*
     * Fractional terrain surface height per local XY column, including
     * the one-column halo. Values are relative to the chunk's Z origin.
     * MC uses this field for smooth terrain while block deltas preserve edits.
     */
    TArray<float> TerrainSurfaceHeights;

    /*
     * Optional per-column material hint for stamped road surfaces. Air means
     * "no override"; the mesher uses this only to break surface-material ties
     * in favour of the actual road block rather than neighboring grass.
     */
    TArray<uint8> TerrainSurfaceBlocks;

    // Separate from material hints: only paved/main roads use the custom road material.
    // Dirt road surface blocks still need TerrainSurfaceBlocks for correct vertex colors.
    TArray<uint8> TerrainRoadMaterialMask;

    TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> CancellationToken;

    void Init(int32 InSize)
    {
        Size = InSize;

        const int32 Side = Size + 2;

        Blocks.SetNumZeroed(
            Side * Side * Side);

        // Only generated world snapshots provide these optional surface hints.
        TerrainSurfaceHeights.Reset();
        TerrainSurfaceBlocks.Reset();
        TerrainRoadMaterialMask.Reset();
    }
};


class FVoxelMarchingCubesMesher
{
public:

    static void Build(
        const FVoxelMarchingCubesBuildInput& Input,
        FVoxelMeshBuildOutput& Output);

private:

    static uint8 GetBlock(
        const FVoxelMarchingCubesBuildInput& Input,
        int32 X,
        int32 Y,
        int32 Z);

    static float GetDensity(
        const FVoxelMarchingCubesBuildInput& Input,
        int32 X,
        int32 Y,
        int32 Z);

    static FLinearColor GetBlockColor(
        uint8 Block);

    static uint8 GetRepresentativeBlock(
        const FVoxelMarchingCubesBuildInput& Input,
        int32 X,
        int32 Y,
        int32 Z);
};
