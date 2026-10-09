#include "VoxelTerrainLODMesher.h"

#include "Math/UnrealMathUtility.h"

namespace
{
    FLinearColor GetBiomeColor(
        EVoxelBiome Biome,
        EVoxelLandform Landform,
        int32 Height,
        int32 SeaLevel,
        int32 BeachWidth)
    {
        if (Biome != EVoxelBiome::Snow &&
            Height < SeaLevel &&
            Height >= SeaLevel - BeachWidth)
        {
            return FLinearColor(
                0.85f, 0.72f, 0.42f, 1.0f);
        }

        switch (Biome)
        {
        case EVoxelBiome::Snow:
            return FLinearColor(
                0.94f, 0.97f, 1.0f, 1.0f);

        case EVoxelBiome::Desert:
            if (Landform == EVoxelLandform::Mountains)
            {
                return FLinearColor(
                    0.72f, 0.38f, 0.22f, 1.0f);
            }

            return FLinearColor(
                0.85f, 0.72f, 0.42f, 1.0f);

        case EVoxelBiome::Mountain: // Legacy biome value.
            return FLinearColor(
                0.50f, 0.52f, 0.56f, 1.0f);

        case EVoxelBiome::Forest:
        case EVoxelBiome::Plains:
        default:
            // Green-biome mountains should share the grassy surface color
            // used by full-resolution chunks, not look like bare boulders.
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
        int32 CellChunkX,
        int32 CellChunkY,
        const FIntVector& CenterChunk,
        int32 InnerRadius,
        int32 OuterRadius)
    {
        /*
         * Assign every sampled cell to the chunk containing its
         * lower-left sample point.
         *
         * The previous implementation classified cells using their
         * floating-point extents. At LOD boundaries this could make
         * the boundary cell belong to both rings or neither ring,
         * producing visible empty bands.
         *
         * Chunk-based classification is deterministic:
         * every cell belongs to exactly one LOD ring.
         */
        const int32 DistanceX =
            FMath::Abs(
                CellChunkX -
                CenterChunk.X);

        const int32 DistanceY =
            FMath::Abs(
                CellChunkY -
                CenterChunk.Y);

        const int32 Distance =
            FMath::Max(
                DistanceX,
                DistanceY);

        return
            Distance >= InnerRadius &&
            Distance <= OuterRadius;
    }

    /*
     * Estimate the exact vertical crossing of the same averaged solid
     * density used by FVoxelMarchingCubesMesher. Each of the four
     * surrounding columns contributes the two voxel layers at NodeZ-1
     * and NodeZ, matching GetDensity() in the full-resolution mesh.
     *
     * For SampleStep == 1 this reproduces the full mesh's vertical
     * iso-surface crossing at every sampled XY node, instead of placing
     * the LOD directly at the integer generator height.
     */
    float GetMCSurfaceHeightFromColumns(
        const int32 ColumnHeights[4])
    {
        constexpr float IsoLevel = 0.49f;

        int32 MinHeight = ColumnHeights[0];
        int32 MaxHeight = ColumnHeights[0];

        for (int32 I = 1; I < 4; ++I)
        {
            MinHeight = FMath::Min(MinHeight, ColumnHeights[I]);
            MaxHeight = FMath::Max(MaxHeight, ColumnHeights[I]);
        }

        auto DensityAtNodeZ =
            [ColumnHeights](int32 NodeZ)
            {
                int32 SolidSamples = 0;

                for (int32 I = 0; I < 4; ++I)
                {
                    SolidSamples +=
                        ColumnHeights[I] >= NodeZ - 1 ? 1 : 0;
                    SolidSamples +=
                        ColumnHeights[I] >= NodeZ ? 1 : 0;
                }

                return static_cast<float>(SolidSamples) / 8.0f;
            };

        int32 PreviousNodeZ = MinHeight - 1;
        float PreviousDensity = DensityAtNodeZ(PreviousNodeZ);

        for (int32 NodeZ = MinHeight; NodeZ <= MaxHeight + 2; ++NodeZ)
        {
            const float CurrentDensity = DensityAtNodeZ(NodeZ);

            if (PreviousDensity >= IsoLevel &&
                CurrentDensity < IsoLevel)
            {
                const float Denominator =
                    CurrentDensity - PreviousDensity;

                const float Alpha =
                    FMath::IsNearlyZero(Denominator)
                        ? 0.0f
                        : (IsoLevel - PreviousDensity) / Denominator;

                return
                    static_cast<float>(PreviousNodeZ) +
                    FMath::Clamp(Alpha, 0.0f, 1.0f);
            }

            PreviousNodeZ = NodeZ;
            PreviousDensity = CurrentDensity;
        }

        // Flat, fully solid columns have their MC surface just above
        // Height + 1; this is a defensive fallback for malformed input.
        return static_cast<float>(MaxHeight) + 1.02f;
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
    TArray<float> SurfaceHeights;
    TArray<FLinearColor> Colors;

    Heights.SetNumZeroed(
        CountX * CountY);

    SurfaceHeights.SetNumZeroed(
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

            const EVoxelLandform Landform =
                Input.Generator.GetLandform(
                    ClampedWorldX,
                    ClampedWorldY);

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
                    Landform,
                    Height,
                    Input.SeaLevel,
                    Input.BeachWidth);
        }
    }

    /*
     * Apply the same vertical density interpolation as the full MC mesh.
     * LOD1 already contains every block column, so reuse its neighboring
     * height samples. Coarser tiers query the three adjacent voxel columns
     * too; this keeps their shared border heights consistent with LOD1 and
     * with full-resolution chunks. Only the horizontal triangulation is
     * decimated at the farther tiers.
     */
    for (int32 Y = 0; Y < CountY; ++Y)
    {
        for (int32 X = 0; X < CountX; ++X)
        {
            const int32 Index =
                GridIndex(X, Y, CountX);

            int32 ColumnHeights[4];

            if (Input.SampleStep == 1)
            {
                const int32 PreviousX = FMath::Max(0, X - 1);
                const int32 PreviousY = FMath::Max(0, Y - 1);

                ColumnHeights[0] =
                    Heights[GridIndex(PreviousX, PreviousY, CountX)];
                ColumnHeights[1] =
                    Heights[GridIndex(X, PreviousY, CountX)];
                ColumnHeights[2] =
                    Heights[GridIndex(PreviousX, Y, CountX)];
                ColumnHeights[3] =
                    Heights[Index];
            }
            else
            {
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

                const int32 PreviousWorldX =
                    FMath::Max(0, WorldX - 1);

                const int32 PreviousWorldY =
                    FMath::Max(0, WorldY - 1);

                ColumnHeights[0] =
                    Input.Generator.GetSurfaceHeight(
                        PreviousWorldX,
                        PreviousWorldY);

                ColumnHeights[1] =
                    Input.Generator.GetSurfaceHeight(
                        WorldX,
                        PreviousWorldY);

                ColumnHeights[2] =
                    Input.Generator.GetSurfaceHeight(
                        PreviousWorldX,
                        WorldY);

                ColumnHeights[3] =
                    Heights[Index];
            }

            SurfaceHeights[Index] =
                GetMCSurfaceHeightFromColumns(ColumnHeights);
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
                    SurfaceHeights[Index] *
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

            /*
             * The cell is assigned to the chunk containing its first
             * sample point. SampleStep values used by the current LOD
             * levels divide ChunkSize, so cells stay aligned to the
             * chunk grid and cannot leave a ring-sized gap.
             */
            const int32 CellChunkX =
                FMath::Clamp(
                    SampleWorldX0 /
                        Input.ChunkSize,
                    0,
                    Input.WorldSizeX - 1);

            const int32 CellChunkY =
                FMath::Clamp(
                    SampleWorldY0 /
                        Input.ChunkSize,
                    0,
                    Input.WorldSizeY - 1);

            if (!IsCellInRing(
                    CellChunkX,
                    CellChunkY,
                    Input.CenterChunk,
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
