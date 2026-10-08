#pragma once

#include "CoreMinimal.h"

/*
 * Climate/loot biomes. Mountain is kept at its old numeric value for
 * compatibility with existing per-block biome data, but mountains are
 * now a landform and GetBiome() no longer returns it.
 */
enum class EVoxelBiome : uint8
{
    Plains = 0,
    Forest = 1,
    Desert = 2,
    Mountain = 3, // Legacy value; use EVoxelLandform::Mountains.
    Snow = 5
};

/*
 * Landform is independent of climate biome:
 * each climate region can contain flats, hills and mountains.
 */
enum class EVoxelLandform : uint8
{
    Flatlands = 0,
    Hills,
    Mountains
};

/*
 * Three major regions used by future POI/loot rules.
 * Plains and Forest share Green; Desert and Snow remain separate.
 */
enum class EVoxelMajorBiome : uint8
{
    Green = 0,
    Desert,
    Winter
};

struct FVoxelWorldGenerationSettings
{
    int32 Seed = 1337;

    /*
     * Biome layout is normalized to these dimensions, so the diagonal
     * regions scale with the selected world size.
     */
    int32 WorldBlocksX = 4096;
    int32 WorldBlocksY = 4096;

    int32 BaseHeight = 12;
    int32 HeightVariation = 8;
    float NoiseScale = 0.025f;
    float DetailNoiseScale = 0.08f;
    int32 DetailHeightVariation = 2;

    /*
     * Controls broad plateaus and large terrain shapes.
     */
    float PlateauScale = 0.007f;
    int32 PlateauHeightStep = 2;
    float PlateauStrength = 0.68f;

    int32 SeaLevel = 10;

    float TemperatureScale = 0.006f;
    float MoistureScale = 0.008f;
};

class FVoxelWorldGenerator
{
public:
    FVoxelWorldGenerator();

    void Configure(const FVoxelWorldGenerationSettings& InSettings);

    int32 GetSurfaceHeight(int32 WorldX, int32 WorldY) const;

    float GetTerrainNoise(int32 WorldX, int32 WorldY) const;
    float GetTemperature(int32 WorldX, int32 WorldY) const;
    float GetMoisture(int32 WorldX, int32 WorldY) const;

    EVoxelBiome GetBiome(
        int32 WorldX,
        int32 WorldY,
        int32 SurfaceHeight) const;

    EVoxelMajorBiome GetMajorBiome(
        int32 WorldX,
        int32 WorldY) const;

    EVoxelLandform GetLandform(
        int32 WorldX,
        int32 WorldY) const;

private:
    FVoxelWorldGenerationSettings Settings;

    float GetLandformNoise(
        int32 WorldX,
        int32 WorldY) const;
};
