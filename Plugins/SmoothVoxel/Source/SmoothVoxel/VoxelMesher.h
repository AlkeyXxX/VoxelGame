
#pragma once

#include "CoreMinimal.h"

/*
 * Снимки граничных блоков соседних чанков.
 *
 * Если соседнего чанка нет, массив остаётся заполненным нулями,
 * то есть сосед считается Air.
 */
struct FVoxelNeighborData
{
	TArray<uint8> XMinus;
	TArray<uint8> XPlus;

	TArray<uint8> YMinus;
	TArray<uint8> YPlus;

	TArray<uint8> ZMinus;
	TArray<uint8> ZPlus;

	void Init(int32 Size)
	{
		const int32 Count = Size * Size;

		XMinus.SetNumZeroed(Count);
		XPlus.SetNumZeroed(Count);

		YMinus.SetNumZeroed(Count);
		YPlus.SetNumZeroed(Count);

		ZMinus.SetNumZeroed(Count);
		ZPlus.SetNumZeroed(Count);
	}
};


/*
 * Всё необходимое для построения mesh.
 *
 * ВАЖНО:
 * Эта структура полностью копируется перед отправкой
 * работы в другой поток.
 */
struct FVoxelMeshBuildInput
{
	int32 Size = 32;
	float VoxelSize = 100.0f;

	TArray<uint8> Blocks;
	TArray<uint8> Biomes;

	FVoxelNeighborData Neighbors;
};


/*
 * Результат построения mesh.
 *
 * Эти данные можно создавать в worker thread,
 * а CreateMeshSection выполнять уже на Game Thread.
 */
struct FVoxelMeshBuildOutput
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> VertexColors;

	bool IsEmpty() const
	{
		return Vertices.Num() == 0 || Triangles.Num() == 0;
	}
};


class FVoxelMesher
{
public:

	static void Build(
		const FVoxelMeshBuildInput& Input,
		FVoxelMeshBuildOutput& Output);

private:

	static uint8 GetBlock(
		const FVoxelMeshBuildInput& Input,
		int32 X,
		int32 Y,
		int32 Z);

	static void AddFace(
		FVoxelMeshBuildOutput& Output,
		const FVector& Origin,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& Normal,
		const FLinearColor& Color);
};

