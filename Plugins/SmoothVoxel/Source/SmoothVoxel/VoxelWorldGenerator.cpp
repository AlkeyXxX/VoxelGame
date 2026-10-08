#include "VoxelWorldGenerator.h"

FVoxelWorldGenerator::FVoxelWorldGenerator()
{
}

void FVoxelWorldGenerator::Configure(
    const FVoxelWorldGenerationSettings& InSettings)
{
    Settings = InSettings;
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

int32 FVoxelWorldGenerator::GetSurfaceHeight(
    int32 WorldX,
    int32 WorldY) const
{
    const float Noise = GetTerrainNoise(WorldX, WorldY);

    const float DetailNoise =
        FMath::PerlinNoise2D(
            FVector2D(
                (WorldX + Settings.Seed * 29) * Settings.DetailNoiseScale,
                (WorldY - Settings.Seed * 31) * Settings.DetailNoiseScale));

    float DetailAmplitude =
        Settings.DetailHeightVariation;

    /*
     * Deserts stay relatively flat, while mountain terrain keeps the
     * full detail amplitude for a more broken silhouette.
     */
    const float Temperature =
        GetTemperature(WorldX, WorldY);
    const float Moisture =
        GetMoisture(WorldX, WorldY);

    if (Temperature > 0.62f && Moisture < 0.42f)
    {
        DetailAmplitude *= 0.45f;
    }

    int32 Height =
        Settings.BaseHeight +
        FMath::RoundToInt(
            Noise * static_cast<float>(Settings.HeightVariation)) +
        FMath::RoundToInt(
            DetailNoise * DetailAmplitude);

    return FMath::Max(Height, 1);
}

float FVoxelWorldGenerator::GetTemperature(
    int32 WorldX,
    int32 WorldY) const
{
    const FVector2D SamplePosition(
        (WorldX + Settings.Seed * 101) * Settings.TemperatureScale,
        (WorldY - Settings.Seed * 67) * Settings.TemperatureScale);

    const float Noise = FMath::PerlinNoise2D(SamplePosition);

    return FMath::Clamp(Noise * 0.5f + 0.5f, 0.0f, 1.0f);
}

float FVoxelWorldGenerator::GetMoisture(
    int32 WorldX,
    int32 WorldY) const
{
    const FVector2D SamplePosition(
        (WorldX - Settings.Seed * 47) * Settings.MoistureScale,
        (WorldY + Settings.Seed * 83) * Settings.MoistureScale);

    const float Noise = FMath::PerlinNoise2D(SamplePosition);

    return FMath::Clamp(Noise * 0.5f + 0.5f, 0.0f, 1.0f);
}


bool FVoxelWorldGenerator::IsCave(
    int32 WorldX,
    int32 WorldY,
    int32 WorldZ,
    int32 SurfaceHeight) const
{
    if (WorldZ <= 1 ||
        WorldZ >= SurfaceHeight - Settings.CaveMinDepth)
    {
        return false;
    }

    const float X =
        (WorldX + Settings.Seed * 113) *
        Settings.CaveNoiseScale;

    const float Y =
        (WorldY - Settings.Seed * 71) *
        Settings.CaveNoiseScale;

    const float Z =
        (WorldZ + Settings.Seed * 43) *
        Settings.CaveNoiseScale;

    const float NoiseA =
        FMath::PerlinNoise3D(
            FVector(X, Y, Z));

    const float NoiseB =
        FMath::PerlinNoise3D(
            FVector(
                X * 1.73f + 17.0f,
                Y * 1.73f - 11.0f,
                Z * 1.73f + 5.0f));

    /*
     * The sum of absolute noises forms elongated tunnel-like regions
     * instead of large spherical holes.
     */
    const float CaveValue =
        FMath::Abs(NoiseA) +
        FMath::Abs(NoiseB);

    return CaveValue < Settings.CaveThreshold;
}


EVoxelBiome FVoxelWorldGenerator::GetBiome(
    int32 WorldX,
    int32 WorldY,
    int32 SurfaceHeight) const
{
    const float Temperature = GetTemperature(WorldX, WorldY);
    const float Moisture = GetMoisture(WorldX, WorldY);

    const int32 MountainThreshold =
        Settings.BaseHeight +
        FMath::Max(2, Settings.HeightVariation * 2 / 3);

    if (SurfaceHeight >= MountainThreshold)
    {
        return EVoxelBiome::Mountain;
    }

    if (Temperature > 0.62f &&
        Moisture < 0.42f)
    {
        return EVoxelBiome::Desert;
    }

    if (Temperature > 0.35f &&
        Moisture > 0.58f)
    {
        return EVoxelBiome::Forest;
    }

    return EVoxelBiome::Plains;
}
