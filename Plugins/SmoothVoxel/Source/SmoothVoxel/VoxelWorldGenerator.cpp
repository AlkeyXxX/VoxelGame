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
    const int32 SafeSizeX = FMath::Max(1, Settings.WorldBlocksX);
    const int32 SafeSizeY = FMath::Max(1, Settings.WorldBlocksY);

    const float NormalizedX =
        (static_cast<float>(FMath::Clamp(WorldX, 0, SafeSizeX - 1)) + 0.5f) /
        static_cast<float>(SafeSizeX);

    const float NormalizedY =
        (static_cast<float>(FMath::Clamp(WorldY, 0, SafeSizeY - 1)) + 0.5f) /
        static_cast<float>(SafeSizeY);

    /*
     * Macro terrain noise remains deterministic in global block space.
     * Landform regions modulate its amplitude rather than replacing it,
     * so Full chunks and all LOD levels use exactly the same surface.
     */
    const float MacroScale =
        FMath::Max(Settings.NoiseScale * 0.22f, 0.0001f);

    const FVector2D MacroSamplePosition(
        (WorldX + Settings.Seed * 13) * MacroScale,
        (WorldY + Settings.Seed * 17) * MacroScale);

    const float MacroNoise =
        FMath::PerlinNoise2D(MacroSamplePosition);

    const float Flatness =
        FMath::Clamp(
            Settings.PlateauStrength,
            0.0f,
            1.0f);

    const float MacroExponent =
        FMath::Lerp(
            1.0f,
            2.8f,
            Flatness);

    const float MacroSign =
        MacroNoise < 0.0f ? -1.0f : 1.0f;

    const float ShapedMacroNoise =
        MacroSign *
        FMath::Pow(
            FMath::Abs(MacroNoise),
            MacroExponent);

    const float TerrainNoise =
        FMath::Lerp(
            MacroNoise,
            ShapedMacroNoise,
            Flatness);

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

    const EVoxelLandform Landform =
        GetLandform(WorldX, WorldY);

    const float HeightVariation =
        static_cast<float>(Settings.HeightVariation);

    const int32 RawMacroHeight =
        Settings.BaseHeight +
        FMath::RoundToInt(
            TerrainNoise * HeightVariation);

    int32 Height = Settings.BaseHeight;

    switch (Landform)
    {
    case EVoxelLandform::Flatlands:
        /*
         * Wide, mostly level ground for cities and large POIs.
         * There is still a little macro variation so fields do not look
         * like an artificial plane.
         */
        Height =
            Settings.BaseHeight +
            FMath::RoundToInt(
                TerrainNoise * HeightVariation * 0.18f) +
            FMath::RoundToInt(
                HillNoise * 0.6f) +
            FMath::RoundToInt(
                DetailNoise *
                static_cast<float>(Settings.DetailHeightVariation) *
                0.15f);
        break;

    case EVoxelLandform::Hills:
        /*
         * Rolling hills use the broad hill noise as a true height signal,
         * not only the tiny detail-noise budget. This makes ridges visible
         * while keeping their tops below mountain peaks.
         */
        Height =
            Settings.BaseHeight +
            FMath::RoundToInt(
                TerrainNoise * HeightVariation * 0.35f) +
            FMath::RoundToInt(
                HillNoise * HeightVariation * 0.95f) +
            FMath::RoundToInt(
                DetailNoise *
                static_cast<float>(Settings.DetailHeightVariation) *
                0.45f);
        break;

    case EVoxelLandform::Mountains:
    default:
        /*
         * Elevated, broken ridges. The broad landform mask gives the area
         * mountain-scale elevation while a second normalized noise adds
         * ridges; Desert mountains will later use sandstone surfaces.
         */
        {
            const float MountainMask =
                FMath::Pow(
                    FMath::Clamp(
                        (GetLandformNoise(WorldX, WorldY) - 0.10f) /
                            0.42f,
                        0.0f,
                        1.0f),
                    0.65f);

            const FVector2D RidgeSamplePosition(
                NormalizedX * 23.0f + Settings.Seed * 0.031f,
                NormalizedY * 23.0f - Settings.Seed * 0.017f);

            const float RidgeSource =
                FMath::PerlinNoise2D(RidgeSamplePosition);

            const float RidgeNoise =
                1.0f - FMath::Abs(RidgeSource);

            Height =
                Settings.BaseHeight +
                FMath::RoundToInt(
                    TerrainNoise * HeightVariation * 0.25f +
                    MountainMask * HeightVariation * 4.0f +
                    RidgeNoise * HeightVariation * 0.45f) +
                FMath::RoundToInt(
                    DetailNoise *
                    static_cast<float>(Settings.DetailHeightVariation) *
                    0.45f);

            Height =
                FMath::Max(
                    Height,
                    Settings.SeaLevel + 2);
        }
        break;
    }

    /*
     * Preserve low basins for seas/lakes. Mountain regions remain above
     * sea level, while low terrain keeps the previous water behavior.
     */
    if (Landform != EVoxelLandform::Mountains &&
        RawMacroHeight <= Settings.SeaLevel)
    {
        Height =
            FMath::Min(
                Height,
                Settings.SeaLevel - 1);
    }

    return FMath::Clamp(
        Height,
        1,
        Settings.MaxTerrainHeight);
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

EVoxelBiome FVoxelWorldGenerator::GetBiome(
    int32 WorldX,
    int32 WorldY,
    int32 /*SurfaceHeight*/) const
{
    /*
     * Macro regions are diagonal bands across the normalized map:
     * northwest = winter/snow, middle = green, southeast = desert.
     *
     * The offset is small enough that the exact map center always stays
     * in the green band, but it makes the borders slightly irregular.
     * The two outer bands occupy approximately one third of the map each.
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
     * Ensure the start is always in a green Plains patch. The surrounding
     * green band still uses the seeded 38/62 Plains/Forest distribution.
     */
    if (FMath::Abs(NormalizedX - 0.5f) <= 0.01f &&
        FMath::Abs(NormalizedY - 0.5f) <= 0.01f)
    {
        return EVoxelBiome::Plains;
    }

    const FVector2D BorderSamplePosition(
        NormalizedX * 5.0f + Settings.Seed * 0.037f,
        NormalizedY * 5.0f - Settings.Seed * 0.021f);

    const float BorderNoise =
        FMath::PerlinNoise2D(BorderSamplePosition) * 0.035f;

    const float DiagonalPosition =
        NormalizedX - NormalizedY + BorderNoise;

    const float BandBoundary =
        1.0f - FMath::Sqrt(2.0f / 3.0f);

    if (DiagonalPosition < -BandBoundary)
    {
        return EVoxelBiome::Snow;
    }

    if (DiagonalPosition > BandBoundary)
    {
        return EVoxelBiome::Desert;
    }

    /*
     * Split only the central green band into Plains and Forest.
     * A normalized, seeded noise gives large patches that scale with
     * world size. The threshold targets roughly 38% Plains / 62% Forest.
     */
    const FVector2D GreenBiomeSamplePosition(
        NormalizedX * 8.0f + Settings.Seed * 0.011f,
        NormalizedY * 8.0f - Settings.Seed * 0.019f);

    const float GreenBiomeNoise =
        FMath::PerlinNoise2D(GreenBiomeSamplePosition);

    if (GreenBiomeNoise > -0.08f)
    {
        return EVoxelBiome::Forest;
    }

    return EVoxelBiome::Plains;
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
