#include "VoxelWorldGenerator.h"

FVoxelWorldGenerator::FVoxelWorldGenerator()
{
}

void FVoxelWorldGenerator::Configure(
    const FVoxelWorldGenerationSettings& InSettings)
{
    Settings = InSettings;
    Settings.WorldBlocksX = FMath::Max(1, Settings.WorldBlocksX);
    Settings.WorldBlocksY = FMath::Max(1, Settings.WorldBlocksY);
    Settings.MaxTerrainHeight = FMath::Max(1, Settings.MaxTerrainHeight);
}

float FVoxelWorldGenerator::GetTerrainNoise(
    int32 WorldX,
    int32 WorldY) const
{
    const FVector2D SamplePosition(
        (WorldX + Settings.Seed * 13) * Settings.NoiseScale,
        (WorldY + Settings.Seed * 17) * Settings.NoiseScale);

    return FMath::PerlinNoise2D(SamplePosition);
}

float FVoxelWorldGenerator::GetLandformNoise(
    int32 WorldX,
    int32 WorldY) const
{
    const int32 SafeSizeX = FMath::Max(1, Settings.WorldBlocksX);
    const int32 SafeSizeY = FMath::Max(1, Settings.WorldBlocksY);

    const float NormalizedX =
        (static_cast<float>(FMath::Clamp(WorldX, 0, SafeSizeX - 1)) + 0.5f) /
        static_cast<float>(SafeSizeX);

    const float NormalizedY =
        (static_cast<float>(FMath::Clamp(WorldY, 0, SafeSizeY - 1)) + 0.5f) /
        static_cast<float>(SafeSizeY);

    /*
     * Broad normalized noise creates landform regions on the order of
     * hundreds of meters to about a kilometer. Their size scales with
     * the chosen world dimensions instead of becoming tiny on large maps.
     */
    const FVector2D SamplePosition(
        NormalizedX * 6.5f + Settings.Seed * 0.0137f,
        NormalizedY * 6.5f - Settings.Seed * 0.0211f);

    return FMath::PerlinNoise2D(SamplePosition);
}

EVoxelLandform FVoxelWorldGenerator::GetLandform(
    int32 WorldX,
    int32 WorldY) const
{
    const int32 SafeSizeX = FMath::Max(1, Settings.WorldBlocksX);
    const int32 SafeSizeY = FMath::Max(1, Settings.WorldBlocksY);

    const float NormalizedX =
        (static_cast<float>(FMath::Clamp(WorldX, 0, SafeSizeX - 1)) + 0.5f) /
        static_cast<float>(SafeSizeX);

    const float NormalizedY =
        (static_cast<float>(FMath::Clamp(WorldY, 0, SafeSizeY - 1)) + 0.5f) /
        static_cast<float>(SafeSizeY);

    /*
     * Reserve a small, stable flat starting zone at the center. This is
     * deliberately tiny compared with the whole map and leaves all three
     * landforms available throughout the rest of every climate region.
     */
    if (FMath::Abs(NormalizedX - 0.5f) <= 0.01f &&
        FMath::Abs(NormalizedY - 0.5f) <= 0.01f)
    {
        return EVoxelLandform::Flatlands;
    }

    const float Noise = GetLandformNoise(WorldX, WorldY);

    /*
     * Keep flatlands common for large city POIs, while widening the
     * mountain mask and giving hills their own substantial share.
     * These thresholds affect landforms only, never climate regions.
     */
    if (Noise > 0.10f)
    {
        return EVoxelLandform::Mountains;
    }

    if (Noise < -0.08f)
    {
        return EVoxelLandform::Flatlands;
    }

    return EVoxelLandform::Hills;
}

int32 FVoxelWorldGenerator::GetSurfaceHeight(
    int32 WorldX,
    int32 WorldY) const
{
    return FMath::RoundToInt(
        GetSurfaceHeightFloat(WorldX, WorldY));
}


float FVoxelWorldGenerator::GetSurfaceHeightFloat(
    int32 WorldX,
    int32 WorldY) const
{
    const int32 SafeSizeX = FMath::Max(1, Settings.WorldBlocksX);
    const int32 SafeSizeY = FMath::Max(1, Settings.WorldBlocksY);

    const float NormalizedX =
        (static_cast<float>(FMath::Clamp(WorldX, 0, SafeSizeX - 1)) + 0.5f) /
        static_cast<float>(SafeSizeX);

    const float NormalizedY =
        (static_cast<float>(FMath::Clamp(WorldY, 0, SafeSizeY - 1)) + 0.5f) /
        static_cast<float>(SafeSizeY);

    const FVector2D MacroSamplePosition(
        (WorldX + Settings.Seed * 13) *
            FMath::Max(Settings.NoiseScale * 0.22f, 0.0001f),
        (WorldY + Settings.Seed * 17) *
            FMath::Max(Settings.NoiseScale * 0.22f, 0.0001f));

    const float MacroNoise =
        FMath::PerlinNoise2D(MacroSamplePosition);

    const float Flatness =
        FMath::Clamp(Settings.PlateauStrength, 0.0f, 1.0f);

    const float MacroExponent =
        FMath::Lerp(1.0f, 2.8f, Flatness);

    const float MacroSign =
        MacroNoise < 0.0f ? -1.0f : 1.0f;

    const float ShapedMacroNoise =
        MacroSign *
        FMath::Pow(FMath::Abs(MacroNoise), MacroExponent);

    const float TerrainNoise =
        FMath::Lerp(MacroNoise, ShapedMacroNoise, Flatness);

    const FVector2D HillSamplePosition(
        (WorldX - Settings.Seed * 53) *
            FMath::Max(Settings.PlateauScale, 0.0001f),
        (WorldY + Settings.Seed * 71) *
            FMath::Max(Settings.PlateauScale, 0.0001f));

    const float HillNoise =
        FMath::PerlinNoise2D(HillSamplePosition);

    const FVector2D DetailSamplePosition(
        (WorldX + Settings.Seed * 29) *
            FMath::Max(Settings.DetailNoiseScale * 0.18f, 0.0001f),
        (WorldY - Settings.Seed * 31) *
            FMath::Max(Settings.DetailNoiseScale * 0.18f, 0.0001f));

    const float DetailNoise =
        FMath::PerlinNoise2D(DetailSamplePosition);

    const float HeightVariation =
        static_cast<float>(Settings.HeightVariation);

    /*
     * These are continuous height signals. Do not round individual noise
     * terms: quantizing each octave independently is what produced the
     * one-block terraces across otherwise smooth hills.
     */
    const float FlatHeight =
        static_cast<float>(Settings.BaseHeight) +
        TerrainNoise * HeightVariation * 0.18f +
        HillNoise * 0.6f +
        DetailNoise *
            static_cast<float>(Settings.DetailHeightVariation) * 0.15f;

    const float HillHeight =
        static_cast<float>(Settings.BaseHeight) +
        TerrainNoise * HeightVariation * 0.35f +
        HillNoise * HeightVariation * 0.95f +
        DetailNoise *
            static_cast<float>(Settings.DetailHeightVariation) * 0.45f;

    /*
     * A low-frequency domain warp bends the mountain ridges into long,
     * connected ranges instead of a field of unrelated sharp peaks. A
     * ridged noise signal then adds narrow crests inside those ranges.
     * Sampling in normalized world coordinates keeps the shape scale
     * consistent when the world size changes.
     */
    const FVector2D RidgeWarpXPosition(
        NormalizedX * 3.8f + Settings.Seed * 0.019f,
        NormalizedY * 3.8f - Settings.Seed * 0.027f);

    const FVector2D RidgeWarpYPosition(
        NormalizedX * 3.8f - Settings.Seed * 0.023f,
        NormalizedY * 3.8f + Settings.Seed * 0.031f);

    const float RidgeWarpX =
        FMath::PerlinNoise2D(RidgeWarpXPosition) * 0.085f;

    const float RidgeWarpY =
        FMath::PerlinNoise2D(RidgeWarpYPosition) * 0.085f;

    const FVector2D RidgeSamplePosition(
        (NormalizedX + RidgeWarpX) * 10.5f + Settings.Seed * 0.031f,
        (NormalizedY + RidgeWarpY) * 10.5f - Settings.Seed * 0.017f);

    const float RidgeSource =
        FMath::PerlinNoise2D(RidgeSamplePosition);

    const float RidgeNoise =
        FMath::Pow(
            FMath::Clamp(1.0f - FMath::Abs(RidgeSource), 0.0f, 1.0f),
            1.65f);

    const FVector2D MountainShoulderPosition(
        NormalizedX * 6.8f - Settings.Seed * 0.014f,
        NormalizedY * 6.8f + Settings.Seed * 0.022f);

    const float MountainShoulderNoise =
        FMath::PerlinNoise2D(MountainShoulderPosition);

    const float LandformNoise =
        GetLandformNoise(WorldX, WorldY);

    const float MountainMask =
        FMath::Pow(
            FMath::Clamp(
                (LandformNoise - 0.08f) / 0.48f,
                0.0f,
                1.0f),
            0.72f);

    const float MountainCandidate =
        static_cast<float>(Settings.BaseHeight) +
        TerrainNoise * HeightVariation * 0.38f +
        MountainShoulderNoise * HeightVariation * 0.75f +
        MountainMask * HeightVariation * 2.65f +
        RidgeNoise * HeightVariation * 1.25f +
        DetailNoise *
            static_cast<float>(Settings.DetailHeightVariation) * 0.35f;

    /*
     * Blend landform amplitudes instead of switching formulas at a hard
     * mask threshold. The former discrete Flatlands/Hills/Mountains switch
     * could introduce sharp ridges even after rounding was removed.
     */
    const auto SmoothStep = [](float Edge0, float Edge1, float Value)
    {
        const float T =
            FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    };

    const bool bStartArea =
        FMath::Abs(NormalizedX - 0.5f) <= 0.01f &&
        FMath::Abs(NormalizedY - 0.5f) <= 0.01f;

    float Height = FlatHeight;

    if (!bStartArea)
    {
        const float FlatWeight =
            1.0f - SmoothStep(-0.14f, -0.02f, LandformNoise);

        const float MountainWeight =
            SmoothStep(0.08f, 0.34f, LandformNoise);

        const float LowlandAndHills =
            FMath::Lerp(HillHeight, FlatHeight, FlatWeight);

        const float MountainHeight =
            FMath::Max(
                MountainCandidate,
                static_cast<float>(Settings.SeaLevel + 2));

        Height =
            FMath::Lerp(
                LowlandAndHills,
                MountainHeight,
                MountainWeight);
    }

    /*
     * Preserve the existing broad low-basin mask for water generation.
     * This is a macro-region constraint; all heights within the resulting
     * shape remain fractional and can be meshed continuously.
     */
    const float RawMacroHeight =
        static_cast<float>(Settings.BaseHeight) +
        TerrainNoise * HeightVariation;

    if ((bStartArea || LandformNoise <= 0.10f) &&
        RawMacroHeight <= static_cast<float>(Settings.SeaLevel))
    {
        Height =
            FMath::Min(
                Height,
                static_cast<float>(Settings.SeaLevel - 1));
    }

    return FMath::Clamp(
        Height,
        1.0f,
        static_cast<float>(Settings.MaxTerrainHeight));
}


float FVoxelWorldGenerator::GetTemperature(
    int32 WorldX,
    int32 WorldY) const
{
    const FVector2D SamplePosition(
        (WorldX + Settings.Seed * 101) * Settings.TemperatureScale,
        (WorldY - Settings.Seed * 67) * Settings.TemperatureScale);

    const float Noise =
        FMath::PerlinNoise2D(SamplePosition);

    return FMath::Clamp(Noise * 0.5f + 0.5f, 0.0f, 1.0f);
}

float FVoxelWorldGenerator::GetMoisture(
    int32 WorldX,
    int32 WorldY) const
{
    const FVector2D SamplePosition(
        (WorldX - Settings.Seed * 47) * Settings.MoistureScale,
        (WorldY + Settings.Seed * 83) * Settings.MoistureScale);

    const float Noise =
        FMath::PerlinNoise2D(SamplePosition);

    return FMath::Clamp(Noise * 0.5f + 0.5f, 0.0f, 1.0f);
}

float FVoxelWorldGenerator::GetRiverField(
    int32 WorldX,
    int32 WorldY) const
{
    const int32 SafeSizeX = FMath::Max(1, Settings.WorldBlocksX);
    const int32 SafeSizeY = FMath::Max(1, Settings.WorldBlocksY);

    const float NormalizedX =
        (static_cast<float>(FMath::Clamp(WorldX, 0, SafeSizeX - 1)) + 0.5f) /
        static_cast<float>(SafeSizeX);

    const float NormalizedY =
        (static_cast<float>(FMath::Clamp(WorldY, 0, SafeSizeY - 1)) + 0.5f) /
        static_cast<float>(SafeSizeY);

    /*
     * Low-frequency domain warping bends the river centerlines. The final
     * near-zero Perlin contour creates long, narrow paths from global
     * coordinates, so every chunk computes exactly the same river mask.
     */
    const FVector2D WarpXPosition(
        NormalizedX * 2.7f + Settings.Seed * 0.021f,
        NormalizedY * 2.7f - Settings.Seed * 0.017f);

    const FVector2D WarpYPosition(
        NormalizedX * 2.7f - Settings.Seed * 0.031f,
        NormalizedY * 2.7f + Settings.Seed * 0.013f);

    const float WarpX =
        FMath::PerlinNoise2D(WarpXPosition) * 0.04f;

    const float WarpY =
        FMath::PerlinNoise2D(WarpYPosition) * 0.04f;

    const FVector2D RiverSamplePosition(
        (NormalizedX + WarpX) * 5.5f + Settings.Seed * 0.071f,
        (NormalizedY + WarpY) * 5.5f - Settings.Seed * 0.043f);

    const float RiverNoise =
        FMath::PerlinNoise2D(RiverSamplePosition);

    return RiverNoise;
}


bool FVoxelWorldGenerator::IsRiverMask(
    int32 WorldX,
    int32 WorldY) const
{
    return FMath::Abs(GetRiverField(WorldX, WorldY)) <= 0.0045f;
}


float FVoxelWorldGenerator::GetLakeScore(
    int32 WorldX,
    int32 WorldY) const
{
    const int32 SafeSizeX = FMath::Max(1, Settings.WorldBlocksX);
    const int32 SafeSizeY = FMath::Max(1, Settings.WorldBlocksY);

    const float NormalizedX =
        (static_cast<float>(FMath::Clamp(WorldX, 0, SafeSizeX - 1)) + 0.5f) /
        static_cast<float>(SafeSizeX);

    const float NormalizedY =
        (static_cast<float>(FMath::Clamp(WorldY, 0, SafeSizeY - 1)) + 0.5f) /
        static_cast<float>(SafeSizeY);

    /*
     * Broad noise selects a small number of lake regions. Fine noise only
     * roughens their shorelines; the water-column query also checks height
     * and landform so this mask does not flood mountains.
     */
    const FVector2D LakeShapePosition(
        NormalizedX * 5.0f + Settings.Seed * 0.031f,
        NormalizedY * 5.0f - Settings.Seed * 0.023f);

    const FVector2D LakeDetailPosition(
        NormalizedX * 13.0f + Settings.Seed * 0.019f,
        NormalizedY * 13.0f - Settings.Seed * 0.037f);

    const float LakeShape =
        FMath::PerlinNoise2D(LakeShapePosition);

    const float LakeDetail =
        FMath::PerlinNoise2D(LakeDetailPosition);

    const float LakeScore =
        LakeShape * 0.82f + LakeDetail * 0.18f;

    return LakeScore;
}


bool FVoxelWorldGenerator::IsLakeMask(
    int32 WorldX,
    int32 WorldY) const
{
    return GetLakeScore(WorldX, WorldY) >= 0.42f;
}


FVoxelWaterColumn FVoxelWorldGenerator::GetWaterColumn(
    int32 WorldX,
    int32 WorldY,
    float SurfaceHeight) const
{
    FVoxelWaterColumn Column;
    Column.WaterSurfaceBlockZ = INDEX_NONE;
    Column.EffectiveTerrainHeight = FMath::RoundToInt(SurfaceHeight);
    Column.EffectiveSurfaceHeight = SurfaceHeight;
    Column.bCarved = false;
    Column.bShoreAdjusted = false;

    const float SeaLevel = static_cast<float>(Settings.SeaLevel);

    const auto SmoothStep = [](float Edge0, float Edge1, float Value)
    {
        const float T =
            FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    };

    /*
     * Existing low basins are still filled to the common water level.
     * Their beds deepen gradually as terrain descends below the shoreline,
     * instead of creating a sudden drop at the water edge.
     */
    if (SurfaceHeight < SeaLevel)
    {
        Column.WaterSurfaceBlockZ = Settings.SeaLevel;

        const float BasinBedHeight =
            FMath::Min(SurfaceHeight, SeaLevel - 4.0f);

        const float BasinDepthWeight =
            1.0f - SmoothStep(
                SeaLevel - 4.5f,
                SeaLevel - 0.25f,
                SurfaceHeight);

        Column.EffectiveSurfaceHeight =
            FMath::Lerp(
                SurfaceHeight,
                BasinBedHeight,
                BasinDepthWeight);
    }

    /*
     * Fade river and lake carving as the terrain rises into mountain
     * country. The fade begins before the discrete biome boundary so
     * channel banks do not suddenly end at a mountain-mask edge.
     */
    const float LandformNoise =
        GetLandformNoise(WorldX, WorldY);

    const float NonMountainWeight =
        1.0f - SmoothStep(0.08f, 0.22f, LandformNoise);

    const float RiverField = GetRiverField(WorldX, WorldY);
    const float LakeScore = GetLakeScore(WorldX, WorldY);
    const float RiverDistance = FMath::Abs(RiverField);

    /*
     * Widths are field-space values, not block counts. The channel floor
     * stays fairly narrow, while the broad bank interval feathers it into
     * surrounding ground with a zero-slope edge.
     */
    constexpr float RiverCoreWidth = 0.0085f;
    constexpr float RiverBankWidth = 0.055f;
    constexpr float LakeBankScore = 0.30f;
    constexpr float LakeCoreScore = 0.42f;

    const float RiverShapeInfluence =
        1.0f - SmoothStep(
            RiverCoreWidth,
            RiverBankWidth,
            RiverDistance);

    const float LakeShapeInfluence =
        SmoothStep(
            LakeBankScore,
            LakeCoreScore,
            LakeScore);

    /*
     * Water uses one shared level, so features on high ground must fade
     * rather than abruptly disappear. Rivers are allowed to cut rolling
     * hills; lake masks fade out sooner to keep lakes in lower basins.
     */
    const float RiverElevationWeight =
        1.0f - SmoothStep(
            SeaLevel + 10.0f,
            SeaLevel + 22.0f,
            SurfaceHeight);

    const float LakeElevationWeight =
        1.0f - SmoothStep(
            SeaLevel + 3.0f,
            SeaLevel + 12.0f,
            SurfaceHeight);

    const float RiverInfluence =
        RiverShapeInfluence *
        RiverElevationWeight *
        NonMountainWeight;

    const float LakeInfluence =
        LakeShapeInfluence *
        LakeElevationWeight *
        NonMountainWeight;

    const float FeatureInfluence =
        FMath::Max(RiverInfluence, LakeInfluence);

    if (FeatureInfluence > KINDA_SMALL_NUMBER)
    {
        float BedDepth = 4.0f;

        if (RiverInfluence >= LakeInfluence)
        {
            // Deepest along the centerline, shallower toward the banks.
            const float CenterDepth =
                1.0f - FMath::Clamp(
                    RiverDistance / RiverBankWidth,
                    0.0f,
                    1.0f);

            BedDepth = 4.0f + CenterDepth * 2.0f;
        }
        else
        {
            // Modest deterministic depth variation across lake basins.
            const float BasinDepth =
                SmoothStep(
                    LakeBankScore,
                    LakeCoreScore,
                    LakeScore);

            BedDepth = 4.0f + BasinDepth * 2.0f;
        }

        const float FeatureBedHeight =
            FMath::Min(
                SurfaceHeight,
                SeaLevel - BedDepth);

        const float PreviousEffectiveHeight =
            Column.EffectiveSurfaceHeight;

        Column.EffectiveSurfaceHeight =
            FMath::Lerp(
                PreviousEffectiveHeight,
                FeatureBedHeight,
                FeatureInfluence);

        Column.bShoreAdjusted =
            FeatureInfluence < 0.999f &&
            Column.EffectiveSurfaceHeight < SurfaceHeight - KINDA_SMALL_NUMBER;
    }

    /*
     * Fill any column where the smoothed ground profile lies below the
     * water plane. This is deterministic for generated chunks and MC halos.
     */
    if (Column.EffectiveSurfaceHeight < SeaLevel)
    {
        Column.WaterSurfaceBlockZ = Settings.SeaLevel;

        /*
         * A wet column must start filling water above the actual bed cell.
         * Floor keeps a fractional shoreline below the water plane from
         * rounding up to the water level and leaving a dry one-cell gap.
         */
        Column.EffectiveTerrainHeight =
            FMath::FloorToInt(Column.EffectiveSurfaceHeight);
    }
    else
    {
        Column.EffectiveTerrainHeight =
            FMath::RoundToInt(Column.EffectiveSurfaceHeight);
    }

    Column.bCarved =
        Column.EffectiveSurfaceHeight < SurfaceHeight - KINDA_SMALL_NUMBER;

    return Column;
}

EVoxelBiome FVoxelWorldGenerator::GetBiome(
    int32 WorldX,
    int32 WorldY,
    int32 /*SurfaceHeight*/) const
{
    /*
     * Keep the existing large-scale map layout: a diagonal winter region,
     * a central green belt and a diagonal desert region. Broad normalized
     * noise bends the climate borders over large distances; a much smaller
     * detail signal prevents them from looking perfectly geometric.
     *
     * Temperature only nudges these borders, so local climate noise cannot
     * turn the three macro regions into a patchwork. All sampling uses global
     * coordinates and the world seed, keeping results deterministic across
     * chunk boundaries and generation order.
     */
    const int32 SafeSizeX = FMath::Max(1, Settings.WorldBlocksX);
    const int32 SafeSizeY = FMath::Max(1, Settings.WorldBlocksY);

    const float NormalizedX =
        (static_cast<float>(FMath::Clamp(WorldX, 0, SafeSizeX - 1)) + 0.5f) /
        static_cast<float>(SafeSizeX);

    const float NormalizedY =
        (static_cast<float>(FMath::Clamp(WorldY, 0, SafeSizeY - 1)) + 0.5f) /
        static_cast<float>(SafeSizeY);

    /*
     * Preserve a guaranteed Plains starting area around the map center.
     * This early return intentionally takes priority over climate noise.
     */
    if (FMath::Abs(NormalizedX - 0.5f) <= 0.01f &&
        FMath::Abs(NormalizedY - 0.5f) <= 0.01f)
    {
        return EVoxelBiome::Plains;
    }

    const float Temperature = GetTemperature(WorldX, WorldY);

    /*
     * Generate broad, irregular climate regions rather than fixed diagonal
     * stripes. Low-frequency domain warping bends biome borders over long
     * distances, while a small secondary signal keeps borders from looking
     * perfectly smooth or geometric. The field is normalized to world size,
     * so the region scale stays consistent for different map dimensions.
     */
    const FVector2D ClimateWarpXPosition(
        NormalizedX * 2.8f + Settings.Seed * 0.023f,
        NormalizedY * 2.8f - Settings.Seed * 0.017f);

    const FVector2D ClimateWarpYPosition(
        NormalizedX * 2.8f - Settings.Seed * 0.031f,
        NormalizedY * 2.8f + Settings.Seed * 0.019f);

    const float ClimateWarpX =
        FMath::PerlinNoise2D(ClimateWarpXPosition) * 0.12f;

    const float ClimateWarpY =
        FMath::PerlinNoise2D(ClimateWarpYPosition) * 0.12f;

    const FVector2D MacroClimatePosition(
        (NormalizedX + ClimateWarpX) * 4.2f + Settings.Seed * 0.037f,
        (NormalizedY + ClimateWarpY) * 4.2f - Settings.Seed * 0.021f);

    const FVector2D FineClimatePosition(
        NormalizedX * 9.0f - Settings.Seed * 0.017f,
        NormalizedY * 9.0f + Settings.Seed * 0.029f);

    const float MacroClimateNoise =
        FMath::PerlinNoise2D(MacroClimatePosition);

    const float FineClimateNoise =
        FMath::PerlinNoise2D(FineClimatePosition) * 0.12f;

    const float TemperatureBorderOffset =
        (Temperature - 0.5f) * 0.10f;

    const float ClimateScore =
        MacroClimateNoise * 0.88f +
        FineClimateNoise +
        TemperatureBorderOffset;

    if (ClimateScore < -0.24f)
    {
        return EVoxelBiome::Snow;
    }

    if (ClimateScore > 0.24f)
    {
        return EVoxelBiome::Desert;
    }

    /*
     * In the central green belt, blend local moisture with a broad normalized
     * patch field. Moisture favors forest in humid locations, while the broad
     * field keeps forests and plains in coherent, world-sized patches instead
     * of allowing every moisture fluctuation to create a tiny biome island.
     */
    const FVector2D GreenBiomeSamplePosition(
        NormalizedX * 8.0f + Settings.Seed * 0.011f,
        NormalizedY * 8.0f - Settings.Seed * 0.019f);

    const float GreenBiomeNoise =
        FMath::PerlinNoise2D(GreenBiomeSamplePosition);

    const float GreenPatchScore =
        GreenBiomeNoise * 0.5f + 0.5f;

    const float Moisture =
        GetMoisture(WorldX, WorldY);

    const float ForestScore =
        Moisture * 0.35f +
        GreenPatchScore * 0.65f;

    /*
     * Warmer green locations can support forest at a slightly lower score;
     * colder locations favor open Plains. Moisture remains the dominant
     * local signal, and thresholds stay near the middle to retain a healthy
     * mix of both biomes.
     */
    const float ForestThreshold =
        0.47f + (0.5f - Temperature) * 0.08f;

    return ForestScore >= ForestThreshold
        ? EVoxelBiome::Forest
        : EVoxelBiome::Plains;
}

EVoxelMajorBiome FVoxelWorldGenerator::GetMajorBiome(
    int32 WorldX,
    int32 WorldY) const
{
    const EVoxelBiome Biome =
        GetBiome(
            WorldX,
            WorldY,
            0);

    switch (Biome)
    {
    case EVoxelBiome::Desert:
        return EVoxelMajorBiome::Desert;

    case EVoxelBiome::Snow:
        return EVoxelMajorBiome::Winter;

    case EVoxelBiome::Forest:
    case EVoxelBiome::Plains:
    case EVoxelBiome::Mountain:
    default:
        return EVoxelMajorBiome::Green;
    }
}
