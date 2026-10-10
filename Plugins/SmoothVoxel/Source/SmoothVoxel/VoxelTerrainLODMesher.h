#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "VoxelWorldGenerator.h"

struct FVoxelTerrainLODBuildInput
{
    FVoxelWorldGenerator Generator;

    int32 WorldSizeX = 0;
    int32 WorldSizeY = 0;
    int32 ChunkSize = 32;
    float VoxelSize = 100.0f;
    float UVScalePerBlock = 0.5f;
    float RoadUVScalePerBlock = 0.5f;

    int32 BeachWidth = 2;
    int32 SeaLevel = 10;

    FIntVector CenterChunk = FIntVector::ZeroValue;

    int32 SampleStep = 4;

    // Shared immutable stamp data lets every LOD tier follow the same surface profile.
    TSharedPtr<TMap<FIntPoint, float>, ESPMode::ThreadSafe> RoadSurfaceHeights;
    // 1 marks paved main/connector road columns for the dedicated LOD material section.
    TSharedPtr<TMap<FIntPoint, uint8>, ESPMode::ThreadSafe> RoadMaterialMask;

    int32 InnerRadiusChunks = 8;
    int32 OuterRadiusChunks = 16;

    /* Cancellation flag shared with obsolete background LOD builds. */
    TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> CancellationToken;
};

struct FVoxelTerrainLODMeshOutput
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FLinearColor> VertexColors;

    TArray<FVector> RoadVertices;
    TArray<int32> RoadTriangles;
    TArray<FVector> RoadNormals;
    TArray<FVector2D> RoadUV0;
    TArray<FLinearColor> RoadVertexColors;

    TArray<FVector> WaterVertices;
    TArray<int32> WaterTriangles;
    TArray<FVector> WaterNormals;
    TArray<FVector2D> WaterUV0;
    TArray<FLinearColor> WaterVertexColors;

    bool IsEmpty() const
    {
        return
            (Vertices.Num() == 0 || Triangles.Num() == 0) &&
            (RoadVertices.Num() == 0 || RoadTriangles.Num() == 0) &&
            (WaterVertices.Num() == 0 || WaterTriangles.Num() == 0);
    }
};

class FVoxelTerrainLODMesher
{
public:
    static void Build(
        const FVoxelTerrainLODBuildInput& Input,
        FVoxelTerrainLODMeshOutput& Output);
};
