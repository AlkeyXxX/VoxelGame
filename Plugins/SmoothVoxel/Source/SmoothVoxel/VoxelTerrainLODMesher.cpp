#include "VoxelTerrainLODMesher.h"

#include "Math/UnrealMathUtility.h"

namespace
{
    FLinearColor GetBiomeColor(
        EVoxelBiome Biome,
        int32 Height,
        int32 SeaLevel,
        int32 BeachWidth)
    {
        if (Height < SeaLevel &&
            Height >= SeaLevel - BeachWidth)
        {
            return FLinearColor(
                0.85f, 0.72f, 0.42f, 1.0f);
        }

        switch (Biome)
        {
        case EVoxelBiome::Desert:
            return FLinearColor(
                0.85f, 0.72f, 0.42f, 1.0f);

        case EVoxelBiome::Mountain:
            return FLinearColor(
                0.50f, 0.52f, 0.56f, 1.0f);

        case EVoxelBiome::Forest:
        case EVoxelBiome::Plains:
        default:
            return FLinearColor(
                0.20f, 0.65f, 0.12f, 1.0f);
        }
    }

    FLinearColor GetWaterColor()
    {
        return FLinearColor(
            0.05f, 0.35f, 0.85f, 1.0f);
    }

    FORCEINLINE int32 SampleIndex(
        int32 X,
        int32 Y,
        int32 SampleCount)
    {
        return X + Y * (SampleCount + 1);
    }

    bool IsCellInsideBand(
        int32 WorldX0,
        int32 WorldX1,
        int32 WorldY0,
        int32 WorldY1,
        const FVoxelTerrainLODBuildInput& Input)
    {
        const float CellCenterX =
            (static_cast<float>(WorldX0) +
             static_cast<float>(WorldX1)) * 0.5f /
            static_cast<float>(Input.ChunkSize);

        const float CellCenterY =
            (static_cast<float>(WorldY0) +
             static_cast<float>(WorldY1)) * 0.5f /
            static_cast<float>(Input.ChunkSize);

        const float Distance =
            FMath::Max(
                FMath::Abs(
                    CellCenterX -
                    static_cast<float>(Input.CenterChunk.X)),
                FMath::Abs(
                    CellCenterY -
                    static_cast<float>(Input.CenterChunk.Y)));

        return
            Distance >
                static_cast<float>(Input.InnerRadiusChunks) &&
            Distance <=
                static_cast<float>(Input.OuterRadiusChunks);
    }
}

void FVoxelTerrainLODMesher::Build(
    const FVoxelTerrainLODBuildInput& Input,
    FVoxelTerrainLODMeshOutput& Output)
{
    if (Input.WorldSizeX <= 0 ||
        Input.WorldSizeY <= 0 ||
        Input.ChunkSize <= 0 ||
        Input.VoxelSize <= 0.0f ||
        Input.SampleStep <= 0 ||
        Input.TileChunkSize <= 0)
    {
        return;
    }

    const int32 WorldBlocksX =
        Input.WorldSizeX * Input.ChunkSize;

    const int32 WorldBlocksY =
        Input.WorldSizeY * Input.ChunkSize;

    const int32 TileBlocks =
        Input.TileChunkSize * Input.ChunkSize;

    const int32 StartBlockX =
        Input.TileCoord.X * TileBlocks;

    const int32 StartBlockY =
        Input.TileCoord.Y * TileBlocks;

    if (StartBlockX >= WorldBlocksX ||
        StartBlockY >= WorldBlocksY)
    {
        return;
    }

    const int32 SampleCount =
        FMath::Max(
            1,
            FMath::DivideAndRoundUp(
                TileBlocks,
                Input.SampleStep));

    TArray<int32> Heights;
    TArray<FLinearColor> Colors;

    Heights.SetNumZeroed(
        (SampleCount + 1) * (SampleCount + 1));

    Colors.SetNum(
        (SampleCount + 1) * (SampleCount + 1));

    /*
     * Sample the real terrain generator directly.
     * No voxel block snapshot and no Marching Cubes are needed
     * for distant terrain.
     */
    for (int32 Y = 0; Y <= SampleCount; ++Y)
    {
        for (int32 X = 0; X <= SampleCount; ++X)
        {
            const int32 RawWorldX =
                StartBlockX +
                X * Input.SampleStep;

            const int32 RawWorldY =
                StartBlockY +
                Y * Input.SampleStep;

            const int32 WorldX =
                FMath::Clamp(
                    RawWorldX,
                    0,
                    WorldBlocksX - 1);

            const int32 WorldY =
                FMath::Clamp(
                    RawWorldY,
                    0,
                    WorldBlocksY - 1);

            const int32 Height =
                Input.Generator.GetSurfaceHeight(
                    WorldX,
                    WorldY);

            const EVoxelBiome Biome =
                Input.Generator.GetBiome(
                    WorldX,
                    WorldY,
                    Height);

            const int32 Index =
                SampleIndex(
                    X,
                    Y,
                    SampleCount);

            Heights[Index] = Height;

            Colors[Index] =
                GetBiomeColor(
                    Biome,
                    Height,
                    Input.SeaLevel,
                    Input.BeachWidth);
        }
    }

    const int32 VertexCount =
        (SampleCount + 1) * (SampleCount + 1);

    Output.Vertices.SetNumZeroed(VertexCount);
    Output.Normals.SetNumZeroed(VertexCount);
    Output.UV0.SetNumZeroed(VertexCount);
    Output.VertexColors = Colors;

    for (int32 Y = 0; Y <= SampleCount; ++Y)
    {
        for (int32 X = 0; X <= SampleCount; ++X)
        {
            const int32 Index =
                SampleIndex(
                    X,
                    Y,
                    SampleCount);

            const int32 WorldX =
                FMath::Clamp(
                    StartBlockX + X * Input.SampleStep,
                    0,
                    WorldBlocksX - 1);

            const int32 WorldY =
                FMath::Clamp(
                    StartBlockY + Y * Input.SampleStep,
                    0,
                    WorldBlocksY - 1);

            const int32 Height =
                Heights[Index];

            Output.Vertices[Index] =
                FVector(
                    static_cast<float>(WorldX - StartBlockX) *
                        Input.VoxelSize,
                    static_cast<float>(WorldY - StartBlockY) *
                        Input.VoxelSize,
                    static_cast<float>(Height + 1) *
                        Input.VoxelSize);

            Output.UV0[Index] =
                FVector2D(
                    static_cast<float>(WorldX) * 0.05f,
                    static_cast<float>(WorldY) * 0.05f);
        }
    }

    const float SafeStepWorld =
        FMath::Max(
            Input.VoxelSize *
                static_cast<float>(Input.SampleStep),
            1.0f);

    Output.Triangles.Reserve(
        SampleCount * SampleCount * 6);

    for (int32 Y = 0; Y < SampleCount; ++Y)
    {
        for (int32 X = 0; X < SampleCount; ++X)
        {
            const int32 WorldX0 =
                StartBlockX +
                X * Input.SampleStep;

            const int32 WorldY0 =
                StartBlockY +
                Y * Input.SampleStep;

            if (WorldX0 >= WorldBlocksX - 1 ||
                WorldY0 >= WorldBlocksY - 1)
            {
                continue;
            }

            const int32 WorldX1 =
                FMath::Min(
                    WorldX0 + Input.SampleStep,
                    WorldBlocksX - 1);

            const int32 WorldY1 =
                FMath::Min(
                    WorldY0 + Input.SampleStep,
                    WorldBlocksY - 1);

            if (!IsCellInsideBand(
                    WorldX0,
                    WorldX1,
                    WorldY0,
                    WorldY1,
                    Input))
            {
                continue;
            }

            const int32 I00 =
                SampleIndex(X, Y, SampleCount);

            const int32 I10 =
                SampleIndex(X + 1, Y, SampleCount);

            const int32 I01 =
                SampleIndex(X, Y + 1, SampleCount);

            const int32 I11 =
                SampleIndex(X + 1, Y + 1, SampleCount);

            const FVector& P00 = Output.Vertices[I00];
            const FVector& P10 = Output.Vertices[I10];
            const FVector& P01 = Output.Vertices[I01];
            const FVector& P11 = Output.Vertices[I11];

            const FVector N0 =
                FVector::CrossProduct(
                    P10 - P00,
                    P11 - P00);

            const FVector N1 =
                FVector::CrossProduct(
                    P11 - P00,
                    P01 - P00);

            Output.Triangles.Add(I00);
            Output.Triangles.Add(I11);
            Output.Triangles.Add(I10);

            Output.Triangles.Add(I00);
            Output.Triangles.Add(I01);
            Output.Triangles.Add(I11);

            Output.Normals[I00] += N0 + N1;
            Output.Normals[I10] += N0;
            Output.Normals[I11] += N0 + N1;
            Output.Normals[I01] += N1;

            const float AverageHeight =
                (
                    static_cast<float>(Heights[I00]) +
                    static_cast<float>(Heights[I10]) +
                    static_cast<float>(Heights[I01]) +
                    static_cast<float>(Heights[I11])
                ) * 0.25f;

            /*
             * Simple distant water plane. The surface is continuous
             * and remains stable even when terrain sampling is coarse.
             */
            if (AverageHeight <
                static_cast<float>(Input.SeaLevel))
            {
                const float WaterZ =
                    (
                        static_cast<float>(Input.SeaLevel) +
                        0.95f
                    ) * Input.VoxelSize;

                const float LX0 =
                    static_cast<float>(WorldX0 - StartBlockX) *
                    Input.VoxelSize;

                const float LX1 =
                    static_cast<float>(WorldX1 - StartBlockX) *
                    Input.VoxelSize;

                const float LY0 =
                    static_cast<float>(WorldY0 - StartBlockY) *
                    Input.VoxelSize;

                const float LY1 =
                    static_cast<float>(WorldY1 - StartBlockY) *
                    Input.VoxelSize;

                const int32 WaterStart =
                    Output.WaterVertices.Num();

                Output.WaterVertices.Add(
                    FVector(LX0, LY0, WaterZ));
                Output.WaterVertices.Add(
                    FVector(LX1, LY0, WaterZ));
                Output.WaterVertices.Add(
                    FVector(LX1, LY1, WaterZ));
                Output.WaterVertices.Add(
                    FVector(LX0, LY1, WaterZ));

                Output.WaterTriangles.Add(
                    WaterStart + 0);
                Output.WaterTriangles.Add(
                    WaterStart + 2);
                Output.WaterTriangles.Add(
                    WaterStart + 1);

                Output.WaterTriangles.Add(
                    WaterStart + 0);
                Output.WaterTriangles.Add(
                    WaterStart + 3);
                Output.WaterTriangles.Add(
                    WaterStart + 2);

                const FVector WaterNormal =
                    FVector::UpVector;

                Output.WaterNormals.Add(WaterNormal);
                Output.WaterNormals.Add(WaterNormal);
                Output.WaterNormals.Add(WaterNormal);
                Output.WaterNormals.Add(WaterNormal);

                const float U0 =
                    static_cast<float>(WorldX0) /
                    FMath::Max(Input.VoxelSize, 1.0f);

                const float U1 =
                    static_cast<float>(WorldX1) /
                    FMath::Max(Input.VoxelSize, 1.0f);

                const float V0 =
                    static_cast<float>(WorldY0) /
                    FMath::Max(Input.VoxelSize, 1.0f);

                const float V1 =
                    static_cast<float>(WorldY1) /
                    FMath::Max(Input.VoxelSize, 1.0f);

                Output.WaterUV0.Add(FVector2D(U0, V0));
                Output.WaterUV0.Add(FVector2D(U1, V0));
                Output.WaterUV0.Add(FVector2D(U1, V1));
                Output.WaterUV0.Add(FVector2D(U0, V1));

                const FLinearColor WaterColor =
                    GetWaterColor();

                Output.WaterVertexColors.Add(WaterColor);
                Output.WaterVertexColors.Add(WaterColor);
                Output.WaterVertexColors.Add(WaterColor);
                Output.WaterVertexColors.Add(WaterColor);
            }
        }
    }

    /*
     * Convert accumulated face normals to normalized vertex normals.
     */
    for (FVector& Normal : Output.Normals)
    {
        if (!Normal.Normalize())
        {
            Normal = FVector::UpVector;
        }
    }

    /*
     * The current heightfield renderer does not need collision.
     * Tangents are intentionally omitted.
     */
    (void)SafeStepWorld;
}
