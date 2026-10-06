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
	/*
	 * Seed используется как детерминированный offset.
	 *
	 * FMath::PerlinNoise2D возвращает одинаковое значение
	 * для одинаковых входных координат.
	 */
	const FVector2D SamplePosition(
		(WorldX + Settings.Seed * 13) *
			Settings.NoiseScale,

		(WorldY + Settings.Seed * 17) *
			Settings.NoiseScale
	);

	return FMath::PerlinNoise2D(
		SamplePosition);
}


int32 FVoxelWorldGenerator::GetSurfaceHeight(
	int32 WorldX,
	int32 WorldY) const
{
	const float Noise =
		GetTerrainNoise(
			WorldX,
			WorldY);

	int32 Height =
		Settings.BaseHeight +
		FMath::RoundToInt(
			Noise *
			static_cast<float>(
				Settings.HeightVariation));

	/*
	 * Никогда не позволяем terrain уходить
	 * ниже первого блока.
	 */
	Height = FMath::Max(
		Height,
		1);

	return Height;
}
