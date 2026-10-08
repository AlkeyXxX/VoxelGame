#pragma once

#include "CoreMinimal.h"

enum class EVoxelBiome : uint8
{
    Plains = 0,
    Forest,
    Desert,
    Mountain
};

struct FVoxelWorldGenerationSettings
{
    int32 Seed = 1337;
    int32 BaseHeight = 12;
    int32 HeightVariation = 8;
    float NoiseScale = 0.025f;
    float DetailNoiseScale = 0.08f;
    int32 DetailHeightVariation = 2;

    /*
     * Масштаб широких зон плато.
     */
    float PlateauScale = 0.007f;

    /*
     * Размер шага по высоте для плато.
     * 2 блока дают широкие ровные поверхности,
     * сохраняя естественные переходы между уровнями.
     */
    int32 PlateauHeightStep = 2;

    /*
     * Сила формирования плоских участков.
     */
    float PlateauStrength = 0.68f;

    int32 SeaLevel = 10;

    bool bEnableCaves = true;
    float CaveNoiseScale = 0.045f;
    float CaveSecondaryScale = 0.09f;
    float CaveThreshold = 0.62f;
    float CaveSecondaryThreshold = 0.10f;
    int32 CaveMinDepth = 5;

    float TemperatureScale = 0.006f;
    float MoistureScale = 0.008f;
};

class FVoxelWorldGenerator
{
public:
    FVoxelWorldGenerator();

    void Configure(const FVoxelWorldGenerationSettings& InSettings);

    int32 GetSurfaceHeight(int32 WorldX, int32 WorldY) const;

    bool IsCave(
        int32 WorldX,
        int32 WorldY,
        int32 WorldZ,
        int32 SurfaceHeight) const;

    float GetTerrainNoise(int32 WorldX, int32 WorldY) const;
    float GetTemperature(int32 WorldX, int32 WorldY) const;
    float GetMoisture(int32 WorldX, int32 WorldY) const;

    EVoxelBiome GetBiome(
        int32 WorldX,
        int32 WorldY,
        int32 SurfaceHeight) const;

private:
    FVoxelWorldGenerationSettings Settings;
};
