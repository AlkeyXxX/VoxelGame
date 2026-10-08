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

    void Init(int32 InSize)
    {
        Size = InSize;

        const int32 Side = Size + 2;

        Blocks.SetNumZeroed(
            Side * Side * Side);
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
        const TArray<uint8>& SolidSamples,
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size);

    static FLinearColor GetBlockColor(
        uint8 Block);

    static uint8 GetRepresentativeBlock(
        const FVoxelMarchingCubesBuildInput& Input,
        int32 X,
        int32 Y,
        int32 Z);
};
