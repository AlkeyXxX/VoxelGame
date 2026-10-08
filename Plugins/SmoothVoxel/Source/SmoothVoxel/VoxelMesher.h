
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

	TArray<uint8> XMinusStructure;
	TArray<uint8> XPlusStructure;
	TArray<uint8> YMinusStructure;
	TArray<uint8> YPlusStructure;
	TArray<uint8> ZMinusStructure;
	TArray<uint8> ZPlusStructure;

	void Init(int32 Size)
	{
		const int32 Count = Size * Size;

		XMinus.SetNumZeroed(Count);
		XPlus.SetNumZeroed(Count);

		YMinus.SetNumZeroed(Count);
		YPlus.SetNumZeroed(Count);

		ZMinus.SetNumZeroed(Count);
		ZPlus.SetNumZeroed(Count);

		XMinusStructure.SetNumZeroed(Count);
		XPlusStructure.SetNumZeroed(Count);
		YMinusStructure.SetNumZeroed(Count);
		YPlusStructure.SetNumZeroed(Count);
		ZMinusStructure.SetNumZeroed(Count);
		ZPlusStructure.SetNumZeroed(Count);
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
	FVector WorldOrigin = FVector::ZeroVector;

	TArray<uint8> Blocks;
	TArray<uint8> Biomes;

	/*
	 * 1 means the block was modified by the player.
	 * Modified solid blocks are rendered by the cubic construction layer.
	 */
	TArray<uint8> StructureFlags;

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

	TArray<FVector> WaterVertices;
	TArray<int32> WaterTriangles;
	TArray<FVector> WaterNormals;
	TArray<FVector2D> WaterUV0;
	TArray<FLinearColor> WaterVertexColors;

	bool IsEmpty() const
	{
		return (Vertices.Num() == 0 || Triangles.Num() == 0) &&
			(WaterVertices.Num() == 0 || WaterTriangles.Num() == 0);
	}
};


class FVoxelMesher
{
public:

	static void Build(
		const FVoxelMeshBuildInput& Input,
		FVoxelMeshBuildOutput& Output);

private:

	static bool IsFaceVisible(
		uint8 Block,
		uint8 NeighborBlock);

	static uint8 GetBlock(
		const FVoxelMeshBuildInput& Input,
		int32 X,
		int32 Y,
		int32 Z);

	static uint8 GetCubicNeighborBlock(
		const FVoxelMeshBuildInput& Input,
		int32 X,
		int32 Y,
		int32 Z,
		bool bCurrentWater);

	static void AddFace(
		FVoxelMeshBuildOutput& Output,
		const FVector& Origin,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& Normal,
		const FLinearColor& Color,
		bool bWater,
		const FVector& WorldOrigin,
		float VoxelSize);
};

