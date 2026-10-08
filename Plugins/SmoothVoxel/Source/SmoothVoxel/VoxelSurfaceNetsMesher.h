#pragma once

#include "CoreMinimal.h"
#include "VoxelMesher.h"

struct FVoxelSurfaceNetsBuildInput
{
    int32 Size = 32;
    float VoxelSize = 100.0f;

    /*
     * Block data with a one-cell negative and two-cell positive halo.
     * Valid block coordinates are [-1, Size + 1] on every axis.
     */
    TArray<uint8> Blocks;

    void Init(int32 InSize)
    {
        Size = InSize;

        const int32 Side = Size + 3;
        Blocks.SetNumZeroed(Side * Side * Side);
    }
};


class FVoxelSurfaceNetsMesher
{
public:

    static void Build(
        const FVoxelSurfaceNetsBuildInput& Input,
        FVoxelMeshBuildOutput& Output);

private:

    static uint8 GetBlock(
        const FVoxelSurfaceNetsBuildInput& Input,
        int32 X,
        int32 Y,
        int32 Z);

    static float GetNodeDensity(
        const FVoxelSurfaceNetsBuildInput& Input,
        int32 X,
        int32 Y,
        int32 Z);

    static FLinearColor GetBlockColor(
        uint8 Block);

    static uint8 GetRepresentativeBlock(
        const FVoxelSurfaceNetsBuildInput& Input,
        int32 X,
        int32 Y,
        int32 Z);
};
