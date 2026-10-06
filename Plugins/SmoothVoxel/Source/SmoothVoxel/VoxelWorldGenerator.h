#pragma once

#include "CoreMinimal.h"

/*
 * Настройки процедурной генерации мира.
 *
 * Важно:
 * этот тип не зависит от AVoxelWorld или AVoxelChunk.
 * Благодаря этому генератор можно использовать независимо
 * от размера мира и позже переносить часть работы на worker threads.
 */
struct FVoxelWorldGenerationSettings
{
	int32 Seed = 1337;
	int32 BaseHeight = 12;
	int32 HeightVariation = 8;
	float NoiseScale = 0.025f;
};


/*
 * Базовый генератор данных мира.
 *
 * Сейчас он отвечает только за высоту поверхности.
 *
 * Позже сюда постепенно добавим:
 * - биомы;
 * - воду;
 * - дороги;
 * - POI;
 * - и другие слои генерации.
 *
 * AVoxelWorld при этом останется ответственным за:
 * - chunks;
 * - блоки;
 * - mesh;
 * - взаимодействие с игроком.
 */
class FVoxelWorldGenerator
{
public:

	FVoxelWorldGenerator();

	/*
	 * Настроить генератор.
	 */
	void Configure(
		const FVoxelWorldGenerationSettings& InSettings);

	/*
	 * Получить высоту поверхности в глобальной
	 * координате блока X/Y.
	 *
	 * Одна и та же комбинация:
	 * Seed + WorldX + WorldY
	 *
	 * всегда даёт один и тот же результат.
	 */
	int32 GetSurfaceHeight(
		int32 WorldX,
		int32 WorldY) const;

	/*
	 * Получить значение terrain noise.
	 *
	 * Оставляем этот метод отдельным, потому что позже
	 * он может пригодиться другим слоям генерации.
	 */
	float GetTerrainNoise(
		int32 WorldX,
		int32 WorldY) const;

private:

	FVoxelWorldGenerationSettings Settings;
};
