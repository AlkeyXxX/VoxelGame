#pragma once

#include "CoreMinimal.h"
#include "VoxelMesher.h"

struct FVoxelMarchingCubesBuildInput
{
    int32 Size = 32;
    float VoxelSize = 100.0f;

    /*
     * Density support is derived from blocks in the range [-1, Size].
     * Array side = Size + 2.
     */
    TArray<uint8> Blocks;

    /*
     * Density-only occupancy. Player-placed solid blocks remain hidden
     * from the smooth mesh itself, but still contribute to nearby density
     * samples so terrain can meet their cubic faces instead of receding.
     */
    TArray<uint8> DensityBlocks;

    void Init(int32 InSize)
    {
        Size = InSize;

        const int32 Side = Size + 2;

        const int32 SampleCount = Side * Side * Side;
        Blocks.SetNumZeroed(SampleCount);
        DensityBlocks.SetNumZeroed(SampleCount);
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

    static uint8 GetDensityBlock(
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
