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

    const FLinearColor WaterColor(
        0.05f, 0.35f, 0.85f, 1.0f);

    FORCEINLINE int32 GridIndex(
        int32 X,
        int32 Y,
        int32 CountX)
    {
        return X + Y * CountX;
    }

    FORCEINLINE bool IsCellInRing(
        float MinChunkX,
        float MaxChunkX,
        float MinChunkY,
        float MaxChunkY,
        int32 InnerRadius,
        int32 OuterRadius)
    {
        /*
         * Use cell extents instead of only the cell centre.
         * This prevents visible holes at the boundary between
         * full chunks and coarse terrain.
         */
        const float ClosestX =
            FMath::Clamp(
                0.0f,
                MinChunkX,
                MaxChunkX);

        const float ClosestY =
            FMath::Clamp(
                0.0f,
                MinChunkY,
                MaxChunkY);

        const float FarthestX =
            FMath::Max(
                FMath::Abs(MinChunkX),
                FMath::Abs(MaxChunkX));

        const float FarthestY =
            FMath::Max(
                FMath::Abs(MinChunkY),
                FMath::Abs(MaxChunkY));

        const float MinDistance =
            FMath::Max(
                FMath::Abs(ClosestX),
                FMath::Abs(ClosestY));

        const float MaxDistance =
            FMath::Max(
                FarthestX,
                FarthestY);

        return
            MaxDistance >=
                static_cast<float>(InnerRadius) &&
            MinDistance <=
                static_cast<float>(OuterRadius);
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
        Input.OuterRadiusChunks <=
            Input.InnerRadiusChunks)
    {
        return;
    }

    const int32 WorldBlocksX =
        Input.WorldSizeX * Input.ChunkSize;

    const int32 WorldBlocksY =
        Input.WorldSizeY * Input.ChunkSize;

    const int32 MinChunkX =
        FMath::Max(
            0,
            Input.CenterChunk.X -
            Input.OuterRadiusChunks);

    const int32 MaxChunkX =
        FMath::Min(
            Input.WorldSizeX - 1,
            Input.CenterChunk.X +
            Input.OuterRadiusChunks);

    const int32 MinChunkY =
        FMath::Max(
            0,
            Input.CenterChunk.Y -
            Input.OuterRadiusChunks);

    const int32 MaxChunkY =
        FMath::Min(
            Input.WorldSizeY - 1,
            Input.CenterChunk.Y +
            Input.OuterRadiusChunks);

    const int32 StartBlockX =
        MinChunkX * Input.ChunkSize;

    const int32 StartBlockY =
        MinChunkY * Input.ChunkSize;

    const int32 EndBlockX =
        FMath::Min(
            WorldBlocksX,
            (MaxChunkX + 1) * Input.ChunkSize);

    const int32 EndBlockY =
        FMath::Min(
            WorldBlocksY,
            (MaxChunkY + 1) * Input.ChunkSize);

    const int32 WidthBlocks =
        FMath::Max(
            1,
            EndBlockX - StartBlockX);

    const int32 HeightBlocks =
        FMath::Max(
            1,
            EndBlockY - StartBlockY);

    const int32 CountX =
        FMath::DivideAndRoundUp(
            WidthBlocks,
            Input.SampleStep) + 1;

    const int32 CountY =
        FMath::DivideAndRoundUp(
            HeightBlocks,
            Input.SampleStep) + 1;

    TArray<int32> Heights;
    TArray<FLinearColor> Colors;

    Heights.SetNumZeroed(
        CountX * CountY);

    Colors.SetNum(
        CountX * CountY);

    for (int32 Y = 0; Y < CountY; ++Y)
    {
        for (int32 X = 0; X < CountX; ++X)
        {
            const int32 WorldX =
                FMath::Min(
                    StartBlockX +
                    X * Input.SampleStep,
                    EndBlockX);

            const int32 WorldY =
                FMath::Min(
                    StartBlockY +
                    Y * Input.SampleStep,
                    EndBlockY);

            const int32 ClampedWorldX =
                FMath::Clamp(
                    WorldX,
                    0,
                    WorldBlocksX - 1);

            const int32 ClampedWorldY =
                FMath::Clamp(
                    WorldY,
                    0,
                    WorldBlocksY - 1);

            const int32 Height =
                Input.Generator.GetSurfaceHeight(
                    ClampedWorldX,
                    ClampedWorldY);

            const EVoxelBiome Biome =
                Input.Generator.GetBiome(
                    ClampedWorldX,
                    ClampedWorldY,
                    Height);

            const int32 Index =
                GridIndex(
                    X,
                    Y,
                    CountX);

            Heights[Index] =
                Height;

            Colors[Index] =
                GetBiomeColor(
                    Biome,
                    Height,
                    Input.SeaLevel,
                    Input.BeachWidth);
        }
    }

    Output.Vertices.SetNumZeroed(
        CountX * CountY);

    Output.Normals.SetNumZeroed(
        CountX * CountY);

    Output.UV0.SetNumZeroed(
        CountX * CountY);

    Output.VertexColors =
        Colors;

    for (int32 Y = 0; Y < CountY; ++Y)
    {
        for (int32 X = 0; X < CountX; ++X)
        {
            const int32 Index =
                GridIndex(
                    X,
                    Y,
                    CountX);

            const int32 WorldX =
                FMath::Min(
                    StartBlockX +
                    X * Input.SampleStep,
                    EndBlockX);

            const int32 WorldY =
                FMath::Min(
                    StartBlockY +
                    Y * Input.SampleStep,
                    EndBlockY);

            Output.Vertices[Index] =
                FVector(
                    static_cast<float>(
                        WorldX) *
                        Input.VoxelSize,
                    static_cast<float>(
                        WorldY) *
                        Input.VoxelSize,
                    static_cast<float>(
                        Heights[Index] + 1) *
                        Input.VoxelSize);

            Output.UV0[Index] =
                FVector2D(
                    static_cast<float>(WorldX) * 0.05f,
                    static_cast<float>(WorldY) * 0.05f);
        }
    }

    for (int32 Y = 0; Y < CountY - 1; ++Y)
    {
        for (int32 X = 0; X < CountX - 1; ++X)
        {
            const int32 I00 =
                GridIndex(X, Y, CountX);

            const int32 I10 =
                GridIndex(X + 1, Y, CountX);

            const int32 I01 =
                GridIndex(X, Y + 1, CountX);

            const int32 I11 =
                GridIndex(X + 1, Y + 1, CountX);

            const int32 SampleWorldX0 =
                FMath::Min(
                    StartBlockX +
                    X * Input.SampleStep,
                    EndBlockX);

            const int32 SampleWorldX1 =
                FMath::Min(
                    StartBlockX +
                    (X + 1) * Input.SampleStep,
                    EndBlockX);

            const int32 SampleWorldY0 =
                FMath::Min(
                    StartBlockY +
                    Y * Input.SampleStep,
                    EndBlockY);

            const int32 SampleWorldY1 =
                FMath::Min(
                    StartBlockY +
                    (Y + 1) * Input.SampleStep,
                    EndBlockY);

            const float CellMinChunkX =
                (
                    static_cast<float>(
                        SampleWorldX0) /
                    static_cast<float>(
                        Input.ChunkSize))
                -
                static_cast<float>(
                    Input.CenterChunk.X);

            const float CellMaxChunkX =
                (
                    static_cast<float>(
                        SampleWorldX1) /
                    static_cast<float>(
                        Input.ChunkSize))
                -
                static_cast<float>(
                    Input.CenterChunk.X);

            const float CellMinChunkY =
                (
                    static_cast<float>(
                        SampleWorldY0) /
                    static_cast<float>(
                        Input.ChunkSize))
                -
                static_cast<float>(
                    Input.CenterChunk.Y);

            const float CellMaxChunkY =
                (
                    static_cast<float>(
                        SampleWorldY1) /
                    static_cast<float>(
                        Input.ChunkSize))
                -
                static_cast<float>(
                    Input.CenterChunk.Y);

            if (!IsCellInRing(
                    CellMinChunkX,
                    CellMaxChunkX,
                    CellMinChunkY,
                    CellMaxChunkY,
                    Input.InnerRadiusChunks,
                    Input.OuterRadiusChunks))
            {
                continue;
            }

            /*
             * Use a single consistent diagonal to keep the coarse
             * terrain stable while the player moves.
             */
            Output.Triangles.Add(I00);
            Output.Triangles.Add(I11);
            Output.Triangles.Add(I10);

            Output.Triangles.Add(I00);
            Output.Triangles.Add(I01);
            Output.Triangles.Add(I11);

            /*
             * Winding above is intentionally kept stable for the mesh,
             * but the cross-product order here must produce an upward
             * lighting normal. The previous order generated -Z on flat
             * terrain, which made the LOD surface appear almost black.
             */
            const FVector N0 =
                FVector::CrossProduct(
                    Output.Vertices[I10] -
                        Output.Vertices[I00],
                    Output.Vertices[I11] -
                        Output.Vertices[I00]);

            const FVector N1 =
                FVector::CrossProduct(
                    Output.Vertices[I11] -
                        Output.Vertices[I00],
                    Output.Vertices[I01] -
                        Output.Vertices[I00]);

            Output.Normals[I00] += N0 + N1;
            Output.Normals[I10] += N0;
            Output.Normals[I11] += N0 + N1;
            Output.Normals[I01] += N1;

            /*
             * Water is continuous at world SeaLevel and is rendered
             * only for cells outside the nearer ring.
             */
            const float AverageHeight =
                (
                    static_cast<float>(Heights[I00]) +
                    static_cast<float>(Heights[I10]) +
                    static_cast<float>(Heights[I01]) +
                    static_cast<float>(Heights[I11])
                ) * 0.25f;

            if (AverageHeight <
                static_cast<float>(Input.SeaLevel))
            {
                const float WaterZ =
                    (
                        static_cast<float>(
                            Input.SeaLevel) +
                        0.95f) *
                    Input.VoxelSize;

                const int32 WaterStart =
                    Output.WaterVertices.Num();

                Output.WaterVertices.Add(
                    Output.Vertices[I00] *
                    FVector(1.0f, 1.0f, 0.0f) +
                    FVector(
                        0.0f,
                        0.0f,
                        WaterZ));

                Output.WaterVertices.Add(
                    Output.Vertices[I10] *
                    FVector(1.0f, 1.0f, 0.0f) +
                    FVector(
                        0.0f,
                        0.0f,
                        WaterZ));

                Output.WaterVertices.Add(
                    Output.Vertices[I11] *
                    FVector(1.0f, 1.0f, 0.0f) +
                    FVector(
                        0.0f,
                        0.0f,
                        WaterZ));

                Output.WaterVertices.Add(
                    Output.Vertices[I01] *
                    FVector(1.0f, 1.0f, 0.0f) +
                    FVector(
                        0.0f,
                        0.0f,
                        WaterZ));

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

                for (int32 I = 0; I < 4; ++I)
                {
                    Output.WaterNormals.Add(
                        FVector::UpVector);
                }

                const FVector2D UV0 =
                    Output.UV0[I00];

                const FVector2D UV1 =
                    Output.UV0[I10];

                const FVector2D UV2 =
                    Output.UV0[I11];

                const FVector2D UV3 =
                    Output.UV0[I01];

                Output.WaterUV0.Add(UV0);
                Output.WaterUV0.Add(UV1);
                Output.WaterUV0.Add(UV2);
                Output.WaterUV0.Add(UV3);

                for (int32 I = 0; I < 4; ++I)
                {
                    Output.WaterVertexColors.Add(
                        WaterColor);
                }
            }
        }
    }

    for (FVector& Normal :
        Output.Normals)
    {
        if (!Normal.Normalize())
        {
            Normal = FVector::UpVector;
        }
    }
}
