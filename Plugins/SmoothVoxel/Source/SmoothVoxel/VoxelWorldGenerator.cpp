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
    const float BaseNoise =
        GetTerrainNoise(WorldX, WorldY);

    const FVector2D DetailSamplePosition(
        (WorldX + Settings.Seed * 29) * Settings.DetailNoiseScale,
        (WorldY - Settings.Seed * 31) * Settings.DetailNoiseScale);

    const float DetailNoise =
        FMath::PerlinNoise2D(DetailSamplePosition);

    const int32 BaseTerrainHeight =
        Settings.BaseHeight +
        FMath::RoundToInt(
            BaseNoise * static_cast<float>(Settings.HeightVariation));

    /*
     * Широкая маска плато.
     *
     * Второй низкочастотный шум определяет большие области,
     * где рельеф должен быть спокойнее и пригоднее для POI.
     */
    const FVector2D PlateauSamplePosition(
        (WorldX - Settings.Seed * 53) * Settings.PlateauScale,
        (WorldY + Settings.Seed * 71) * Settings.PlateauScale);

    const float PlateauNoise =
        FMath::PerlinNoise2D(PlateauSamplePosition);

    const float PlateauValue =
        FMath::Clamp(
            PlateauNoise * 0.5f + 0.5f,
            0.0f,
            1.0f);

    /*
     * Формируем большие зоны плато только в достаточно
     * спокойной части рельефа. На подходе к горам эффект
     * автоматически ослабевает.
     */
    const float PlateauCoverage =
        FMath::Clamp(
            (PlateauValue - 0.30f) / 0.45f,
            0.0f,
            1.0f);

    const float MountainStart =
        Settings.BaseHeight +
        static_cast<float>(Settings.HeightVariation) * 0.70f;

    const float MountainProtection =
        FMath::Clamp(
            (MountainStart + 2.0f -
             static_cast<float>(BaseTerrainHeight)) / 5.0f,
            0.0f,
            1.0f);

    const float PlateauMask =
        PlateauCoverage *
        MountainProtection *
        FMath::Clamp(
            Settings.PlateauStrength,
            0.0f,
            1.0f);

    const int32 PlateauStep =
        FMath::Max(
            Settings.PlateauHeightStep,
            1);

    const int32 PlateauHeight =
        Settings.BaseHeight +
        FMath::RoundToInt(
            static_cast<float>(
                BaseTerrainHeight -
                Settings.BaseHeight) /
            static_cast<float>(PlateauStep)) *
        PlateauStep;

    const float PlateauBlendedHeight =
        FMath::Lerp(
            static_cast<float>(BaseTerrainHeight),
            static_cast<float>(PlateauHeight),
            PlateauMask);

    const int32 FlattenedBaseHeight =
        FMath::RoundToInt(
            PlateauBlendedHeight);

    /*
     * Мелкий шум не должен создавать отдельные островки
     * в явно низкой местности. На плато он дополнительно
     * приглушается, чтобы поверхность оставалась пригодной
     * для размещения POI.
     */
    const float DetailWeight =
        FMath::Clamp(
            static_cast<float>(
                BaseTerrainHeight -
                (Settings.SeaLevel - 3)) / 6.0f,
            0.0f,
            1.0f) *
        (1.0f - PlateauMask * 0.85f);

    int32 Height =
        FlattenedBaseHeight +
        FMath::RoundToInt(
            DetailNoise *
            static_cast<float>(Settings.DetailHeightVariation) *
            DetailWeight);

    /*
     * Низкий рельеф остаётся ниже уровня воды,
     * поэтому мелкий шум не создаёт отдельные островки
     * внутри озера.
     */
    if (BaseTerrainHeight <= Settings.SeaLevel)
    {
        Height =
            FMath::Min(
                Height,
                Settings.SeaLevel - 1);
    }

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
