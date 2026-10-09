#pragma once

#include "CoreMinimal.h"

/*
 * Alpha 21.2-inspired biome set.
 * These are world-generation biomes, not voxel block types.
 */
enum class EVoxelBiome : uint8
{
    Forest = 0,
    Desert,
    SnowyForest,
    Wasteland
};

struct FVoxelWorldGenerationSettings
{
    int32 Seed = 1337;
    int32 BaseHeight = 12;
    int32 HeightVariation = 8;
    float NoiseScale = 0.025f;
    float TemperatureScale = 0.006f;
    float MoistureScale = 0.008f;
};

/*
 * Deterministic, coordinate-based generator.
 *
 * A21 target: broad terrain forms, independent climate fields, and
 * four biome regions. This is the first foundation layer; roads,
 * settlements, POI rules, and map-preview calibration are separate stages.
 */
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

private:
    float SampleSeededNoise(
        int32 WorldX,
        int32 WorldY,
        float Scale,
        uint32 Salt) const;

    FVoxelWorldGenerationSettings Settings;
};
