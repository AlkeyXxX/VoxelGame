#include "VoxelWorldGenerator.h"

namespace
{
    FORCEINLINE uint32 MixSeed(uint32 Value)
    {
        Value ^= Value >> 16;
        Value *= 0x7feb352dU;
        Value ^= Value >> 15;
        Value *= 0x846ca68bU;
        Value ^= Value >> 16;
        return Value;
    }

    FORCEINLINE float SeedOffset(uint32 Seed, uint32 Salt)
    {
        const uint32 Hash = MixSeed(Seed ^ Salt);
        return (static_cast<float>(Hash) / 4294967295.0f) * 200000.0f - 100000.0f;
    }
}

FVoxelWorldGenerator::FVoxelWorldGenerator()
{
}

void FVoxelWorldGenerator::Configure(
    const FVoxelWorldGenerationSettings& InSettings)
{
    Settings = InSettings;
}

float FVoxelWorldGenerator::SampleSeededNoise(
    int32 WorldX,
    int32 WorldY,
    float Scale,
    uint32 Salt) const
{
    const uint32 Seed = static_cast<uint32>(Settings.Seed);
    const float OffsetX = SeedOffset(Seed, Salt ^ 0xA511E9B3U);
    const float OffsetY = SeedOffset(Seed, Salt ^ 0x63D83595U);

    const FVector2D Position(
        (static_cast<float>(WorldX) + OffsetX) * Scale,
        (static_cast<float>(WorldY) + OffsetY) * Scale);

    return FMath::PerlinNoise2D(Position);
}

float FVoxelWorldGenerator::GetTerrainNoise(
    int32 WorldX,
    int32 WorldY) const
{
    // Broad landforms dominate; smaller scales add hills and surface detail.
    const float Continental = SampleSeededNoise(WorldX, WorldY, 0.0035f, 0x1021U);
    const float Hills = SampleSeededNoise(WorldX, WorldY, 0.010f, 0x2043U);
    const float Detail = SampleSeededNoise(
        WorldX, WorldY, FMath::Max(0.001f, Settings.NoiseScale), 0x4087U);
    const float Ridges = 1.0f - FMath::Abs(
        SampleSeededNoise(WorldX, WorldY, 0.006f, 0x810FU));

    return FMath::Clamp(
        Continental * 0.52f +
        Hills * 0.27f +
        Detail * 0.11f +
        (Ridges * 2.0f - 1.0f) * 0.10f,
        -1.0f,
        1.0f);
}

int32 FVoxelWorldGenerator::GetSurfaceHeight(
    int32 WorldX,
    int32 WorldY) const
{
    const float Continental = SampleSeededNoise(
        WorldX, WorldY, 0.0035f, 0x1021U);
    const float Hills = SampleSeededNoise(
        WorldX, WorldY, 0.010f, 0x2043U);
    const float Detail = SampleSeededNoise(
        WorldX, WorldY, FMath::Max(0.001f, Settings.NoiseScale), 0x4087U);

    const float Variation = static_cast<float>(
        FMath::Max(1, Settings.HeightVariation));

    const float Height =
        static_cast<float>(Settings.BaseHeight) +
        Continental * Variation * 1.35f +
        Hills * Variation * 0.55f +
        Detail * Variation * 0.12f;

    return FMath::Max(1, FMath::RoundToInt(Height));
}

float FVoxelWorldGenerator::GetTemperature(
    int32 WorldX,
    int32 WorldY) const
{
    const float BroadClimate = SampleSeededNoise(
        WorldX, WorldY, FMath::Max(0.0005f, Settings.TemperatureScale * 0.45f), 0xC1A1U);
    const float LocalClimate = SampleSeededNoise(
        WorldX, WorldY, FMath::Max(0.0005f, Settings.TemperatureScale), 0xC1A2U);

    return FMath::Clamp(
        0.5f + BroadClimate * 0.34f + LocalClimate * 0.16f,
        0.0f,
        1.0f);
}

float FVoxelWorldGenerator::GetMoisture(
    int32 WorldX,
    int32 WorldY) const
{
    const float BroadMoisture = SampleSeededNoise(
        WorldX, WorldY, FMath::Max(0.0005f, Settings.MoistureScale * 0.5f), 0xD2B1U);
    const float LocalMoisture = SampleSeededNoise(
        WorldX, WorldY, FMath::Max(0.0005f, Settings.MoistureScale), 0xD2B2U);

    return FMath::Clamp(
        0.5f + BroadMoisture * 0.36f + LocalMoisture * 0.14f,
        0.0f,
        1.0f);
}

EVoxelBiome FVoxelWorldGenerator::GetBiome(
    int32 WorldX,
    int32 WorldY,
    int32 SurfaceHeight) const
{
    const float Temperature = GetTemperature(WorldX, WorldY);
    const float Moisture = GetMoisture(WorldX, WorldY);

    // Biome masks use broad climate fields to avoid noisy single-block patches.
    if (Temperature < 0.30f)
    {
        return EVoxelBiome::SnowyForest;
    }

    if (Temperature > 0.62f && Moisture < 0.42f)
    {
        return EVoxelBiome::Desert;
    }

    if (Moisture < 0.27f && Temperature < 0.64f)
    {
        return EVoxelBiome::Wasteland;
    }

    return EVoxelBiome::Forest;
}
