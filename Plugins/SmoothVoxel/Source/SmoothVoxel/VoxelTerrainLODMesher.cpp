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

    // Keep road influence identical at shared coordinates in every ring.
    constexpr int32 LODRoadHeightSearchRadius = 4;
    constexpr float MCSurfaceOffsetBlocks = 1.02f;

    bool FindRoadSurfaceHeightNear(
        const TSharedPtr<TMap<FIntPoint, float>, ESPMode::ThreadSafe>& RoadSurfaceHeights,
        int32 WorldX,
        int32 WorldY,
        int32 SearchRadius,
        float& OutHeight)
    {
        if (!RoadSurfaceHeights.IsValid())
        {
            return false;
        }

        SearchRadius = FMath::Clamp(SearchRadius, 0, 8);
        int32 BestDistanceSquared = MAX_int32;
        float BestHeight = -1.0f;

        for (int32 DY = -SearchRadius; DY <= SearchRadius; ++DY)
        {
            for (int32 DX = -SearchRadius; DX <= SearchRadius; ++DX)
            {
                const int32 DistanceSquared = DX * DX + DY * DY;
                if (DistanceSquared > SearchRadius * SearchRadius ||
                    DistanceSquared >= BestDistanceSquared)
                {
                    continue;
                }

                if (const float* Height =
                    RoadSurfaceHeights->Find(FIntPoint(WorldX + DX, WorldY + DY)))
                {
                    BestDistanceSquared = DistanceSquared;
                    BestHeight = *Height;
                }
            }
        }

        if (BestHeight < 0.0f)
        {
            return false;
        }

        OutHeight = BestHeight;
        return true;
    }

    float GetEffectiveSurfaceHeight(
        const FVoxelWorldGenerator& Generator,
        int32 WorldX,
        int32 WorldY,
        const TSharedPtr<TMap<FIntPoint, float>, ESPMode::ThreadSafe>& RoadSurfaceHeights,
        int32 RoadSearchRadius)
    {
        const float TerrainHeight =
            Generator.GetSurfaceHeightFloat(WorldX, WorldY);

        const FVoxelWaterColumn WaterColumn =
            Generator.GetWaterColumn(
                WorldX,
                WorldY,
                TerrainHeight);

        // Water remains governed by the water generator; only dry land snaps
        // to the active road/shoulder height map.
        if (WaterColumn.WaterSurfaceBlockZ == INDEX_NONE)
        {
            float RoadHeight = 0.0f;
            if (FindRoadSurfaceHeightNear(
                RoadSurfaceHeights, WorldX, WorldY, RoadSearchRadius, RoadHeight))
            {
                return RoadHeight;
            }
        }

        return WaterColumn.EffectiveSurfaceHeight;
    }

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
        const float ColumnHeights[4])
    {
        // Match the full-resolution Marching Cubes iso level exactly.
        constexpr float LODIsoLevel = 0.5f;

        float MinHeight = ColumnHeights[0];
        float MaxHeight = ColumnHeights[0];

        for (int32 I = 1; I < 4; ++I)
        {
            MinHeight = FMath::Min(MinHeight, ColumnHeights[I]);
            MaxHeight = FMath::Max(MaxHeight, ColumnHeights[I]);
        }

        auto DensityAtNodeZ =
            [ColumnHeights](int32 NodeZ)
            {
                float SolidSamples = 0.0f;

                for (int32 I = 0; I < 4; ++I)
                {
                    /*
                     * Match the continuous top-cell occupancy used by the
                     * full-resolution MC mesh. Integer terrain reduces to
                     * binary samples; fractional heights move the surface
                     * smoothly within each voxel layer.
                     */
                    SolidSamples += FMath::Clamp(
                        ColumnHeights[I] -
                            static_cast<float>(NodeZ - 1) + 1.0f,
                        0.0f,
                        1.0f);

                    SolidSamples += FMath::Clamp(
                        ColumnHeights[I] -
                            static_cast<float>(NodeZ) + 1.0f,
                        0.0f,
                        1.0f);
                }

                return SolidSamples / 8.0f;
            };

        const int32 MinNodeZ = FMath::FloorToInt(MinHeight);
        const int32 MaxNodeZ = FMath::CeilToInt(MaxHeight) + 2;

        int32 PreviousNodeZ = MinNodeZ - 1;
        float PreviousDensity = DensityAtNodeZ(PreviousNodeZ);

        for (int32 NodeZ = MinNodeZ; NodeZ <= MaxNodeZ; ++NodeZ)
        {
            const float CurrentDensity = DensityAtNodeZ(NodeZ);

            if (PreviousDensity >= LODIsoLevel &&
                CurrentDensity < LODIsoLevel)
            {
                const float Denominator =
                    CurrentDensity - PreviousDensity;

                const float Alpha =
                    FMath::IsNearlyZero(Denominator)
                        ? 0.0f
                        : (LODIsoLevel - PreviousDensity) / Denominator;

                return
                    static_cast<float>(PreviousNodeZ) +
                    FMath::Clamp(Alpha, 0.0f, 1.0f);
            }

            PreviousNodeZ = NodeZ;
            PreviousDensity = CurrentDensity;
        }

        return MaxHeight + 1.02f;
    }
}

void FVoxelTerrainLODMesher::Build(
    const FVoxelTerrainLODBuildInput& Input,
    FVoxelTerrainLODMeshOutput& Output)
{
    if (Input.CancellationToken.IsValid() &&
        static_cast<bool>(*Input.CancellationToken))
    {
        return;
    }

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

    TArray<float> Heights;
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
        if (Input.CancellationToken.IsValid() &&
            static_cast<bool>(*Input.CancellationToken))
        {
            return;
        }
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

            const float SmoothSurfaceHeight =
                Input.Generator.GetSurfaceHeightFloat(
                    ClampedWorldX,
                    ClampedWorldY);

            const int32 Height =
                FMath::RoundToInt(SmoothSurfaceHeight);

            const FVoxelWaterColumn WaterColumn =
                Input.Generator.GetWaterColumn(
                    ClampedWorldX,
                    ClampedWorldY,
                    SmoothSurfaceHeight);

            float EffectiveHeight =
                WaterColumn.EffectiveSurfaceHeight;
            if (WaterColumn.WaterSurfaceBlockZ == INDEX_NONE)
            {
                float RoadHeight = 0.0f;
                if (FindRoadSurfaceHeightNear(
                    Input.RoadSurfaceHeights,
                    ClampedWorldX,
                    ClampedWorldY,
                    LODRoadHeightSearchRadius,
                    RoadHeight))
                {
                    EffectiveHeight = RoadHeight;
                }
            }

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
                EffectiveHeight;

            const bool bSubmergedBed =
                WaterColumn.WaterSurfaceBlockZ != INDEX_NONE &&
                EffectiveHeight <
                    static_cast<float>(WaterColumn.WaterSurfaceBlockZ);

            if (bSubmergedBed)
            {
                Colors[Index] =
                    FLinearColor(
                        0.85f, 0.72f, 0.42f, 1.0f);
            }
            else
            {
                Colors[Index] =
                    GetBiomeColor(
                        Biome,
                        Landform,
                        Height,
                        Input.SeaLevel,
                        Input.BeachWidth);
            }
        }
    }

    /*
     * Reconstruct the vertical MC crossing from the same four neighboring
     * columns for every tier. Shared XY nodes therefore get identical Z,
     * including at LOD1/2/3/4 boundaries; only horizontal tessellation differs.
     */
    for (int32 Y = 0; Y < CountY; ++Y)
    {
        if (Input.CancellationToken.IsValid() &&
            static_cast<bool>(*Input.CancellationToken))
        {
            return;
        }

        for (int32 X = 0; X < CountX; ++X)
        {
            const int32 Index = GridIndex(X, Y, CountX);
            const int32 WorldX = FMath::Clamp(
                StartBlockX + X * Input.SampleStep, 0, WorldBlocksX - 1);
            const int32 WorldY = FMath::Clamp(
                StartBlockY + Y * Input.SampleStep, 0, WorldBlocksY - 1);
            const int32 PreviousWorldX = FMath::Max(0, WorldX - 1);
            const int32 PreviousWorldY = FMath::Max(0, WorldY - 1);

            const float ColumnHeights[4] =
            {
                GetEffectiveSurfaceHeight(
                    Input.Generator, PreviousWorldX, PreviousWorldY,
                    Input.RoadSurfaceHeights, LODRoadHeightSearchRadius),
                GetEffectiveSurfaceHeight(
                    Input.Generator, WorldX, PreviousWorldY,
                    Input.RoadSurfaceHeights, LODRoadHeightSearchRadius),
                GetEffectiveSurfaceHeight(
                    Input.Generator, PreviousWorldX, WorldY,
                    Input.RoadSurfaceHeights, LODRoadHeightSearchRadius),
                GetEffectiveSurfaceHeight(
                    Input.Generator, WorldX, WorldY,
                    Input.RoadSurfaceHeights, LODRoadHeightSearchRadius)
            };

            SurfaceHeights[Index] =
                GetMCSurfaceHeightFromColumns(ColumnHeights);
        }
    }

    /*
     * Morph each LOD's outer border onto the next coarser triangulation.
     * The generator heights at shared grid nodes are already identical,
     * but a fine edge contains extra vertices that would otherwise sit off
     * the coarse edge and leave cracks. Within a narrow transition band,
     * blend toward the coarse grid's exact piecewise-linear surface.
     *
     * The coarse interpolation uses the same I00-I11 diagonal as the mesh
     * below. The source heights are immutable here so processing order
     * cannot influence neighboring vertices.
     */
    if (Input.SampleStep < 8 &&
        CountX > 2 &&
        CountY > 2)
    {
        const TArray<float> OriginalSurfaceHeights = SurfaceHeights;
        const int32 CoarseStride = 2;
        const float CoarseStepBlocks =
            static_cast<float>(Input.SampleStep * CoarseStride);
        const float MorphBandBlocks =
            CoarseStepBlocks * 2.0f;

        auto GetCoarseInterpolatedHeight =
            [&OriginalSurfaceHeights, CountX, CountY](
                int32 GridX,
                int32 GridY)
            {
                const int32 X0 =
                    (GridX / 2) * 2;
                const int32 Y0 =
                    (GridY / 2) * 2;

                const int32 X1 =
                    FMath::Min(X0 + 2, CountX - 1);
                const int32 Y1 =
                    FMath::Min(Y0 + 2, CountY - 1);

                const float TX =
                    X1 > X0
                        ? static_cast<float>(GridX - X0) /
                            static_cast<float>(X1 - X0)
                        : 0.0f;

                const float TY =
                    Y1 > Y0
                        ? static_cast<float>(GridY - Y0) /
                            static_cast<float>(Y1 - Y0)
                        : 0.0f;

                const float H00 =
                    OriginalSurfaceHeights[
                        GridIndex(X0, Y0, CountX)];
                const float H10 =
                    OriginalSurfaceHeights[
                        GridIndex(X1, Y0, CountX)];
                const float H01 =
                    OriginalSurfaceHeights[
                        GridIndex(X0, Y1, CountX)];
                const float H11 =
                    OriginalSurfaceHeights[
                        GridIndex(X1, Y1, CountX)];

                // Match the mesh's fixed diagonal from lower-left to upper-right.
                if (TX >= TY)
                {
                    return
                        H00 * (1.0f - TX) +
                        H10 * (TX - TY) +
                        H11 * TY;
                }

                return
                    H00 * (1.0f - TY) +
                    H01 * (TY - TX) +
                    H11 * TX;
            };

        for (int32 Y = 0; Y < CountY; ++Y)
        {
            if (Input.CancellationToken.IsValid() &&
                static_cast<bool>(*Input.CancellationToken))
            {
                return;
            }

            for (int32 X = 0; X < CountX; ++X)
            {
                const int32 DistanceX =
                    FMath::Min(X, CountX - 1 - X) *
                    Input.SampleStep;

                const int32 DistanceY =
                    FMath::Min(Y, CountY - 1 - Y) *
                    Input.SampleStep;

                const int32 DistanceToOuterEdge =
                    FMath::Min(DistanceX, DistanceY);

                if (DistanceToOuterEdge >= MorphBandBlocks)
                {
                    continue;
                }

                float MorphWeight = 1.0f;

                if (DistanceToOuterEdge > CoarseStepBlocks)
                {
                    const float T = FMath::Clamp(
                        (static_cast<float>(DistanceToOuterEdge) -
                            CoarseStepBlocks) /
                            (MorphBandBlocks - CoarseStepBlocks),
                        0.0f,
                        1.0f);

                    MorphWeight =
                        1.0f - T * T * (3.0f - 2.0f * T);
                }

                if (MorphWeight <= KINDA_SMALL_NUMBER)
                {
                    continue;
                }

                const int32 Index =
                    GridIndex(X, Y, CountX);

                const float CoarseHeight =
                    GetCoarseInterpolatedHeight(X, Y);

                SurfaceHeights[Index] =
                    FMath::Lerp(
                        OriginalSurfaceHeights[Index],
                        CoarseHeight,
                        MorphWeight);
            }
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
        if (Input.CancellationToken.IsValid() &&
            static_cast<bool>(*Input.CancellationToken))
        {
            return;
        }
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
                    static_cast<float>(WorldX) * Input.UVScalePerBlock,
                    static_cast<float>(WorldY) * Input.UVScalePerBlock);
        }
    }

    for (int32 Y = 0; Y < CountY - 1; ++Y)
    {
        if (Input.CancellationToken.IsValid() &&
            static_cast<bool>(*Input.CancellationToken))
        {
            return;
        }
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

            const bool bRenderCell =
                IsCellInRing(
                    CellChunkX,
                    CellChunkY,
                    Input.CenterChunk,
                    Input.InnerRadiusChunks,
                    Input.OuterRadiusChunks);

            /*
             * Use a single consistent diagonal to keep the coarse
             * terrain stable while the player moves. Normal accumulation
             * continues for hidden cells as well, so the visible ring edge
             * does not get a one-sided normal and a dark lighting seam.
             */
            if (bRenderCell)
            {
                // Match the upward-facing vertex normals (CCW winding in XY).
                Output.Triangles.Add(I00);
                Output.Triangles.Add(I10);
                Output.Triangles.Add(I11);

                Output.Triangles.Add(I00);
                Output.Triangles.Add(I11);
                Output.Triangles.Add(I01);
            }

            /* Accumulate upward-facing normals for the matching triangle winding. */
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


        }
    }

    /*
     * Paved roads must survive decimation. Sampling the road height map only
     * at terrain-grid vertices made 10-block roads disappear on LOD3/LOD4.
     * Build a sparse block-grid overlay from stamped paved columns; attribute
     * each road cell to exactly one LOD ring, like terrain and water.
     */
    if (Input.RoadMaterialMask.IsValid() &&
        Input.RoadSurfaceHeights.IsValid() &&
        Input.RoadMaterialMask->Num() > 0)
    {
        /*
         * The road overlay must lie on the actual simplified triangles, not on
         * raw generator/stamp heights. Otherwise the overlay can float or get
         * buried between widely spaced LOD vertices on steep terrain.
         */
        auto GetPavedRoadVertexHeight =
            [&SurfaceHeights, &Input, StartBlockX, StartBlockY, CountX, CountY](
                int32 WorldX, int32 WorldY)
            {
                const float GridX =
                    static_cast<float>(WorldX - StartBlockX) /
                    static_cast<float>(Input.SampleStep);
                const float GridY =
                    static_cast<float>(WorldY - StartBlockY) /
                    static_cast<float>(Input.SampleStep);

                const int32 X0 = FMath::Clamp(
                    FMath::FloorToInt(GridX), 0, CountX - 2);
                const int32 Y0 = FMath::Clamp(
                    FMath::FloorToInt(GridY), 0, CountY - 2);
                const int32 X1 = X0 + 1;
                const int32 Y1 = Y0 + 1;
                const float TX = FMath::Clamp(GridX - X0, 0.0f, 1.0f);
                const float TY = FMath::Clamp(GridY - Y0, 0.0f, 1.0f);

                const float H00 = SurfaceHeights[GridIndex(X0, Y0, CountX)];
                const float H10 = SurfaceHeights[GridIndex(X1, Y0, CountX)];
                const float H01 = SurfaceHeights[GridIndex(X0, Y1, CountX)];
                const float H11 = SurfaceHeights[GridIndex(X1, Y1, CountX)];

                // Use the exact I00-I11 diagonal used by the terrain LOD mesh.
                const float SurfaceHeight = TX >= TY
                    ? H00 * (1.0f - TX) + H10 * (TX - TY) + H11 * TY
                    : H00 * (1.0f - TY) + H01 * (TY - TX) + H11 * TX;

                // 1.5 cm at the default 100 cm voxel size prevents z-fighting.
                return (SurfaceHeight + 0.015f) * Input.VoxelSize;
            };

        for (const TPair<FIntPoint, uint8>& RoadCell : *Input.RoadMaterialMask)
        {
            if (Input.CancellationToken.IsValid() &&
                static_cast<bool>(*Input.CancellationToken))
            {
                return;
            }

            if (RoadCell.Value == 0)
            {
                continue;
            }

            const int32 WorldX = RoadCell.Key.X;
            const int32 WorldY = RoadCell.Key.Y;
            if (WorldX < StartBlockX || WorldY < StartBlockY ||
                WorldX >= EndBlockX || WorldY >= EndBlockY ||
                WorldX + 1 >= WorldBlocksX || WorldY + 1 >= WorldBlocksY)
            {
                continue;
            }

            const int32 CellChunkX = FMath::Clamp(
                WorldX / Input.ChunkSize, 0, Input.WorldSizeX - 1);
            const int32 CellChunkY = FMath::Clamp(
                WorldY / Input.ChunkSize, 0, Input.WorldSizeY - 1);
            if (!IsCellInRing(
                    CellChunkX, CellChunkY, Input.CenterChunk,
                    Input.InnerRadiusChunks, Input.OuterRadiusChunks))
            {
                continue;
            }

            const int32 BaseIndex = Output.RoadVertices.Num();
            const FVector P00(
                static_cast<float>(WorldX) * Input.VoxelSize,
                static_cast<float>(WorldY) * Input.VoxelSize,
                GetPavedRoadVertexHeight(WorldX, WorldY));
            const FVector P10(
                static_cast<float>(WorldX + 1) * Input.VoxelSize,
                static_cast<float>(WorldY) * Input.VoxelSize,
                GetPavedRoadVertexHeight(WorldX + 1, WorldY));
            const FVector P01(
                static_cast<float>(WorldX) * Input.VoxelSize,
                static_cast<float>(WorldY + 1) * Input.VoxelSize,
                GetPavedRoadVertexHeight(WorldX, WorldY + 1));
            const FVector P11(
                static_cast<float>(WorldX + 1) * Input.VoxelSize,
                static_cast<float>(WorldY + 1) * Input.VoxelSize,
                GetPavedRoadVertexHeight(WorldX + 1, WorldY + 1));

            const FVector RawNormal =
                FVector::CrossProduct(P10 - P00, P11 - P00) +
                FVector::CrossProduct(P11 - P00, P01 - P00);
            const FVector Normal = RawNormal.GetSafeNormal();
            const FVector SafeNormal = Normal.IsNearlyZero() ? FVector::UpVector : Normal;

            Output.RoadVertices.Add(P00);
            Output.RoadVertices.Add(P10);
            Output.RoadVertices.Add(P01);
            Output.RoadVertices.Add(P11);
            Output.RoadNormals.Add(SafeNormal);
            Output.RoadNormals.Add(SafeNormal);
            Output.RoadNormals.Add(SafeNormal);
            Output.RoadNormals.Add(SafeNormal);

            Output.RoadUV0.Add(FVector2D(
                static_cast<float>(WorldX) * Input.RoadUVScalePerBlock,
                static_cast<float>(WorldY) * Input.RoadUVScalePerBlock));
            Output.RoadUV0.Add(FVector2D(
                static_cast<float>(WorldX + 1) * Input.RoadUVScalePerBlock,
                static_cast<float>(WorldY) * Input.RoadUVScalePerBlock));
            Output.RoadUV0.Add(FVector2D(
                static_cast<float>(WorldX) * Input.RoadUVScalePerBlock,
                static_cast<float>(WorldY + 1) * Input.RoadUVScalePerBlock));
            Output.RoadUV0.Add(FVector2D(
                static_cast<float>(WorldX + 1) * Input.RoadUVScalePerBlock,
                static_cast<float>(WorldY + 1) * Input.RoadUVScalePerBlock));
            Output.RoadVertexColors.Add(FLinearColor::White);
            Output.RoadVertexColors.Add(FLinearColor::White);
            Output.RoadVertexColors.Add(FLinearColor::White);
            Output.RoadVertexColors.Add(FLinearColor::White);

            Output.RoadTriangles.Add(BaseIndex);
            Output.RoadTriangles.Add(BaseIndex + 1);
            Output.RoadTriangles.Add(BaseIndex + 3);
            Output.RoadTriangles.Add(BaseIndex);
            Output.RoadTriangles.Add(BaseIndex + 3);
            Output.RoadTriangles.Add(BaseIndex + 2);
        }
    }

    /*
     * Water is meshed on its own finer grid instead of borrowing terrain
     * LOD vertices. A narrow river can pass between two widely-spaced
     * terrain samples; using the land grid for water silently removed it
     * from the horizon. Keep water detail capped at four blocks even in
     * the farthest terrain ring.
     */
    // Preserve shoreline and river detail next to the player, cap only far rings.
    const int32 WaterSampleStep =
        FMath::Clamp(Input.SampleStep, 1, 4);

    const int32 WaterCountX =
        FMath::DivideAndRoundUp(
            WidthBlocks,
            WaterSampleStep) + 1;

    const int32 WaterCountY =
        FMath::DivideAndRoundUp(
            HeightBlocks,
            WaterSampleStep) + 1;

    TArray<uint8> WaterMask;
    WaterMask.SetNumZeroed(WaterCountX * WaterCountY);

    // Cache shared water-grid vertices to keep wide lakes inexpensive.
    TArray<int32> WaterVertexIndices;
    WaterVertexIndices.Init(-1, WaterCountX * WaterCountY);

    for (int32 Y = 0; Y < WaterCountY; ++Y)
    {
        if (Input.CancellationToken.IsValid() &&
            static_cast<bool>(*Input.CancellationToken))
        {
            return;
        }

        const int32 WorldY =
            FMath::Clamp(
                FMath::Min(
                    StartBlockY + Y * WaterSampleStep,
                    EndBlockY),
                0,
                WorldBlocksY - 1);

        for (int32 X = 0; X < WaterCountX; ++X)
        {
            const int32 WorldX =
                FMath::Clamp(
                    FMath::Min(
                        StartBlockX + X * WaterSampleStep,
                        EndBlockX),
                0,
                WorldBlocksX - 1);

            const float TerrainHeight =
                Input.Generator.GetSurfaceHeightFloat(
                    WorldX,
                    WorldY);

            const FVoxelWaterColumn WaterColumn =
                Input.Generator.GetWaterColumn(
                    WorldX,
                    WorldY,
                    TerrainHeight);

            /*
             * bCarved also describes dry sloping banks. Only flag columns
             * that genuinely have a water cap above their bed, otherwise
             * far LOD painted broad false water sheets across dry banks.
             */
            const bool bIsWet =
                WaterColumn.WaterSurfaceBlockZ != INDEX_NONE &&
                WaterColumn.EffectiveSurfaceHeight <
                    static_cast<float>(WaterColumn.WaterSurfaceBlockZ) -
                    KINDA_SMALL_NUMBER;

            WaterMask[
                GridIndex(X, Y, WaterCountX)] =
                    bIsWet ? 1 : 0;
        }
    }

    // Match the full-resolution surface crossing (block surface + about one block).
    const float WaterZ =
        (static_cast<float>(Input.SeaLevel) + MCSurfaceOffsetBlocks) *
        Input.VoxelSize;

    auto GetWaterVertexIndex =
        [&](
            int32 GridX,
            int32 GridY,
            int32 WorldX,
            int32 WorldY)
        {
            int32& CachedIndex =
                WaterVertexIndices[
                    GridIndex(
                        GridX,
                        GridY,
                        WaterCountX)];

            if (CachedIndex != INDEX_NONE)
            {
                return CachedIndex;
            }

            CachedIndex =
                Output.WaterVertices.Num();

            Output.WaterVertices.Add(
                FVector(
                    static_cast<float>(WorldX) * Input.VoxelSize,
                    static_cast<float>(WorldY) * Input.VoxelSize,
                    WaterZ));

            Output.WaterNormals.Add(FVector::UpVector);
            Output.WaterVertexColors.Add(WaterColor);
            Output.WaterUV0.Add(
                FVector2D(
                    static_cast<float>(WorldX) * Input.UVScalePerBlock,
                    static_cast<float>(WorldY) * Input.UVScalePerBlock));

            return CachedIndex;
        };

    for (int32 Y = 0; Y < WaterCountY - 1; ++Y)
    {
        if (Input.CancellationToken.IsValid() &&
            static_cast<bool>(*Input.CancellationToken))
        {
            return;
        }

        for (int32 X = 0; X < WaterCountX - 1; ++X)
        {
            const int32 WorldX0 =
                FMath::Min(
                    StartBlockX + X * WaterSampleStep,
                    EndBlockX);

            const int32 WorldX1 =
                FMath::Min(
                    StartBlockX + (X + 1) * WaterSampleStep,
                    EndBlockX);

            const int32 WorldY0 =
                FMath::Min(
                    StartBlockY + Y * WaterSampleStep,
                    EndBlockY);

            const int32 WorldY1 =
                FMath::Min(
                    StartBlockY + (Y + 1) * WaterSampleStep,
                    EndBlockY);

            const int32 CellChunkX =
                FMath::Clamp(
                    WorldX0 / Input.ChunkSize,
                    0,
                    Input.WorldSizeX - 1);

            const int32 CellChunkY =
                FMath::Clamp(
                    WorldY0 / Input.ChunkSize,
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

            const int32 I00 =
                GridIndex(X, Y, WaterCountX);

            const int32 I10 =
                GridIndex(X + 1, Y, WaterCountX);

            const int32 I01 =
                GridIndex(X, Y + 1, WaterCountX);

            const int32 I11 =
                GridIndex(X + 1, Y + 1, WaterCountX);

            /*
             * A wet corner keeps narrow winding channels represented.
             * Grid spacing is at most four blocks, so shoreline expansion
             * is bounded and much smaller than the old coarse quads.
             */
            if (WaterMask[I00] == 0 &&
                WaterMask[I10] == 0 &&
                WaterMask[I01] == 0 &&
                WaterMask[I11] == 0)
            {
                continue;
            }

            const int32 V00 =
                GetWaterVertexIndex(
                    X,
                    Y,
                    WorldX0,
                    WorldY0);

            const int32 V10 =
                GetWaterVertexIndex(
                    X + 1,
                    Y,
                    WorldX1,
                    WorldY0);

            const int32 V11 =
                GetWaterVertexIndex(
                    X + 1,
                    Y + 1,
                    WorldX1,
                    WorldY1);

            const int32 V01 =
                GetWaterVertexIndex(
                    X,
                    Y + 1,
                    WorldX0,
                    WorldY1);

            // Water top uses the same upward-facing winding as terrain.
            Output.WaterTriangles.Add(V00);
            Output.WaterTriangles.Add(V10);
            Output.WaterTriangles.Add(V11);

            Output.WaterTriangles.Add(V00);
            Output.WaterTriangles.Add(V11);
            Output.WaterTriangles.Add(V01);
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
