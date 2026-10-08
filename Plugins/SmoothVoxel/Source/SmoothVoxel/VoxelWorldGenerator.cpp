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
    /*
     * Основной рельеф теперь специально очень крупный.
     *
     * Settings.NoiseScale остаётся управляемым из Blueprint,
     * но внутри используется только его часть, чтобы на
     * существующей карте получить большие плавные поля.
     */
    const float MacroScale =
        Settings.NoiseScale * 0.22f;

    const FVector2D MacroSamplePosition(
        (WorldX + Settings.Seed * 13) * MacroScale,
        (WorldY + Settings.Seed * 17) * MacroScale);

    const float MacroNoise =
        FMath::PerlinNoise2D(MacroSamplePosition);

    /*
     * Сжимаем середину диапазона шума.
     * Благодаря этому большая часть мира остаётся
     * около одного уровня, а подъёмы начинаются очень
     * плавно и занимают большую площадь.
     */
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
        MacroNoise < 0.0f
            ? -1.0f
            : 1.0f;

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

    const int32 BaseTerrainHeight =
        Settings.BaseHeight +
        FMath::RoundToInt(
            TerrainNoise *
            static_cast<float>(Settings.HeightVariation));

    /*
     * Второй шум используется только как очень мягкое
     * формирование холмов поверх крупных форм.
     * В равнинных областях его влияние почти нулевое.
     */
    const float HillScale =
        FMath::Max(
            Settings.PlateauScale,
            0.0001f);

    const FVector2D HillSamplePosition(
        (WorldX - Settings.Seed * 53) * HillScale,
        (WorldY + Settings.Seed * 71) * HillScale);

    const float HillNoise =
        FMath::PerlinNoise2D(HillSamplePosition);

    const float HillMask =
        FMath::SmoothStep(
            0.10f,
            0.65f,
            FMath::Abs(MacroNoise));

    const float HillContribution =
        HillNoise *
        static_cast<float>(Settings.DetailHeightVariation) *
        1.5f *
        HillMask *
        (1.0f - Flatness * 0.55f);

    int32 Height =
        BaseTerrainHeight +
        FMath::RoundToInt(
            HillContribution);

    /*
     * Мелкая детализация теперь значительно слабее
     * и тоже привязана к крупному рельефу.
     */
    const float DetailScale =
        Settings.DetailNoiseScale * 0.18f;

    const FVector2D DetailSamplePosition(
        (WorldX + Settings.Seed * 29) * DetailScale,
        (WorldY - Settings.Seed * 31) * DetailScale);

    const float DetailNoise =
        FMath::PerlinNoise2D(DetailSamplePosition);

    const float DetailWeight =
        FMath::Clamp(
            static_cast<float>(
                BaseTerrainHeight -
                (Settings.SeaLevel - 3)) / 6.0f,
            0.0f,
            1.0f) *
        (1.0f - Flatness * 0.80f);

    Height +=
        FMath::RoundToInt(
            DetailNoise *
            static_cast<float>(Settings.DetailHeightVariation) *
            DetailWeight);

    /*
     * Низкий рельеф остаётся ниже уровня воды,
     * поэтому шум не создаёт островки внутри озёр.
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

bool FVoxelWorldGenerator::IsCave(
    int32 WorldX,
    int32 WorldY,
    int32 WorldZ,
    int32 SurfaceHeight) const
{
    if (!Settings.bEnableCaves)
    {
        return false;
    }

    /*
     * Не вскрываем поверхность.
     * Минимальная глубина нужна, чтобы пещеры оставались
     * под землёй и не превращались в случайные ямы.
     */
    const int32 Depth =
        SurfaceHeight - WorldZ;

    if (Depth < FMath::Max(Settings.CaveMinDepth, 2))
    {
        return false;
    }

    /*
     * Не вмешиваемся в слой непосредственно под водой.
     * Это оставляет водоёмы стабильными на первом этапе.
     */
    if (WorldZ >= Settings.SeaLevel - 1)
    {
        return false;
    }

    const float PrimaryScale =
        FMath::Max(Settings.CaveNoiseScale, 0.0001f);

    const float SecondaryScale =
        FMath::Max(Settings.CaveSecondaryScale, 0.0001f);

    const FVector PrimaryPosition(
        (WorldX + Settings.Seed * 17) * PrimaryScale,
        (WorldY - Settings.Seed * 23) * PrimaryScale,
        (WorldZ + Settings.Seed * 31) * PrimaryScale);

    const FVector SecondaryPosition(
        (WorldX - Settings.Seed * 43) * SecondaryScale,
        (WorldY + Settings.Seed * 47) * SecondaryScale,
        (WorldZ - Settings.Seed * 53) * SecondaryScale);

    const float PrimaryNoise =
        FMath::PerlinNoise3D(PrimaryPosition);

    const float SecondaryNoise =
        FMath::PerlinNoise3D(SecondaryPosition);

    /*
     * Два независимых шума дают редкие объёмные полости,
     * вместо того чтобы вырезать слишком большую часть грунта.
     */
    return
        PrimaryNoise >= Settings.CaveThreshold &&
        SecondaryNoise >= Settings.CaveSecondaryThreshold;
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
