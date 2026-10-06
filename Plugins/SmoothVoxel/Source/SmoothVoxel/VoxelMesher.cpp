
#include "VoxelMesher.h"
#include "VoxelTypes.h"


namespace
{
    FORCEINLINE int32 BlockIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        return X + Y * Size + Z * Size * Size;
    }


    FORCEINLINE int32 SideIndex(
        int32 A,
        int32 B,
        int32 Size)
    {
        return A + B * Size;
    }
}


/*
 * Получение блока.
 *
 * Если координаты находятся внутри текущего чанка —
 * читаем Blocks.
 *
 * Если координата выходит за границу —
 * читаем соответствующий снимок соседнего чанка.
 */
uint8 FVoxelMesher::GetBlock(
    const FVoxelMeshBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z)
{
    const int32 Size = Input.Size;

    if (X >= 0 && X < Size &&
        Y >= 0 && Y < Size &&
        Z >= 0 && Z < Size)
    {
        return Input.Blocks[
            BlockIndex(X, Y, Z, Size)
        ];
    }


    // X-

    if (X < 0)
    {
        if (Y >= 0 && Y < Size &&
            Z >= 0 && Z < Size)
        {
            return Input.Neighbors.XMinus[
                SideIndex(Y, Z, Size)
            ];
        }

        return uint8(EVoxelBlock::Air);
    }


    // X+

    if (X >= Size)
    {
        if (Y >= 0 && Y < Size &&
            Z >= 0 && Z < Size)
        {
            return Input.Neighbors.XPlus[
                SideIndex(Y, Z, Size)
            ];
        }

        return uint8(EVoxelBlock::Air);
    }


    // Y-

    if (Y < 0)
    {
        if (X >= 0 && X < Size &&
            Z >= 0 && Z < Size)
        {
            return Input.Neighbors.YMinus[
                SideIndex(X, Z, Size)
            ];
        }

        return uint8(EVoxelBlock::Air);
    }


    // Y+

    if (Y >= Size)
    {
        if (X >= 0 && X < Size &&
            Z >= 0 && Z < Size)
        {
            return Input.Neighbors.YPlus[
                SideIndex(X, Z, Size)
            ];
        }

        return uint8(EVoxelBlock::Air);
    }


    // Z-

    if (Z < 0)
    {
        if (X >= 0 && X < Size &&
            Y >= 0 && Y < Size)
        {
            return Input.Neighbors.ZMinus[
                SideIndex(X, Y, Size)
            ];
        }

        return uint8(EVoxelBlock::Air);
    }


    // Z+

    if (Z >= Size)
    {
        if (X >= 0 && X < Size &&
            Y >= 0 && Y < Size)
        {
            return Input.Neighbors.ZPlus[
                SideIndex(X, Y, Size)
            ];
        }

        return uint8(EVoxelBlock::Air);
    }


    return uint8(EVoxelBlock::Air);
}


/*
 * Добавляет одну квадратную грань.
 *
 * Вершины идут так, чтобы нормаль смотрела наружу.
 */
void FVoxelMesher::AddFace(
    FVoxelMeshBuildOutput& Output,
    const FVector& Origin,
    const FVector& A,
    const FVector& B,
    const FVector& C,
    const FVector& D,
    const FVector& Normal,
    const FLinearColor& Color,
    bool bWater)
{
    TArray<FVector>& Vertices = bWater ? Output.WaterVertices : Output.Vertices;
    TArray<int32>& Triangles = bWater ? Output.WaterTriangles : Output.Triangles;
    TArray<FVector>& Normals = bWater ? Output.WaterNormals : Output.Normals;
    TArray<FVector2D>& UV0 = bWater ? Output.WaterUV0 : Output.UV0;
    TArray<FLinearColor>& VertexColors = bWater ? Output.WaterVertexColors : Output.VertexColors;

    const int32 StartIndex = Vertices.Num();

    Vertices.Add(Origin + A);
    Vertices.Add(Origin + B);
    Vertices.Add(Origin + C);
    Vertices.Add(Origin + D);

    Normals.Add(Normal);
    Normals.Add(Normal);
    Normals.Add(Normal);
    Normals.Add(Normal);

    UV0.Add(FVector2D(0.0f, 0.0f));
    UV0.Add(FVector2D(1.0f, 0.0f));
    UV0.Add(FVector2D(1.0f, 1.0f));
    UV0.Add(FVector2D(0.0f, 1.0f));

    VertexColors.Add(Color);
    VertexColors.Add(Color);
    VertexColors.Add(Color);
    VertexColors.Add(Color);


    /*
     * Наружная сторона должна быть front face.
     */
    Triangles.Add(StartIndex + 0);
    Triangles.Add(StartIndex + 2);
    Triangles.Add(StartIndex + 1);

    Triangles.Add(StartIndex + 0);
    Triangles.Add(StartIndex + 3);
    Triangles.Add(StartIndex + 2);
}


void FVoxelMesher::Build(
    const FVoxelMeshBuildInput& Input,
    FVoxelMeshBuildOutput& Output)
{
    const int32 Size = Input.Size;
    const float VoxelSize = Input.VoxelSize;

    if (Size <= 0)
    {
        return;
    }

    if (Input.Blocks.Num() != Size * Size * Size)
    {
        return;
    }


    const int32 MaxBlocks = Size * Size * Size;

    /*
     * В худшем случае один блок может иметь 6 граней.
     */
    Output.Vertices.Reserve(MaxBlocks * 6 * 4);
    Output.Normals.Reserve(MaxBlocks * 6 * 4);
    Output.UV0.Reserve(MaxBlocks * 6 * 4);
    Output.VertexColors.Reserve(MaxBlocks * 6 * 4);
    Output.Triangles.Reserve(MaxBlocks * 6 * 6);


    for (int32 Z = 0; Z < Size; ++Z)
    {
        for (int32 Y = 0; Y < Size; ++Y)
        {
            for (int32 X = 0; X < Size; ++X)
            {
                const uint8 Block = Input.Blocks[
                    BlockIndex(X, Y, Z, Size)
                ];

                if (Block != uint8(EVoxelBlock::Water) && !IsVoxelSolid(Block))
                {
                    continue;
                }


                const FVector Origin(
                    X * VoxelSize,
                    Y * VoxelSize,
                    Z * VoxelSize);


                const float S = VoxelSize;

                FLinearColor BiomeColor = FLinearColor::White;

                if (Input.Biomes.Num() == MaxBlocks)
                {
                    switch (Input.Biomes[BlockIndex(X, Y, Z, Size)])
                    {
                    case 0: // Plains
                        BiomeColor = FLinearColor(0.20f, 0.80f, 0.20f, 1.0f);
                        break;
                    case 1: // Forest
                        BiomeColor = FLinearColor(0.05f, 0.35f, 0.08f, 1.0f);
                        break;
                    case 2: // Desert
                        BiomeColor = FLinearColor(0.95f, 0.75f, 0.25f, 1.0f);
                        break;
                    case 3: // Mountain
                        BiomeColor = FLinearColor(0.55f, 0.55f, 0.60f, 1.0f);
                        break;
                    case 4: // Water
                        BiomeColor = FLinearColor(0.05f, 0.35f, 0.85f, 1.0f);
                        break;
                    default:
                        break;
                    }
                }


                /*
                 * X-
                 */
                if (!IsVoxelSolid(
                    GetBlock(Input, X - 1, Y, Z)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, 0),
                        FVector(0, 0, S),
                        FVector(0, S, S),
                        FVector(0, S, 0),

                        FVector(-1, 0, 0),
                        BiomeColor,
                        false);
                }


                /*
                 * X+
                 */
                if (!IsVoxelSolid(
                    GetBlock(Input, X + 1, Y, Z)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(S, 0, 0),
                        FVector(S, S, 0),
                        FVector(S, S, S),
                        FVector(S, 0, S),

                        FVector(1, 0, 0),
                        BiomeColor,
                        false);
                }


                /*
                 * Y-
                 */
                if (!IsVoxelSolid(
                    GetBlock(Input, X, Y - 1, Z)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, 0),
                        FVector(S, 0, 0),
                        FVector(S, 0, S),
                        FVector(0, 0, S),

                        FVector(0, -1, 0),
                        BiomeColor,
                        false);
                }


                /*
                 * Y+
                 */
                if (!IsVoxelSolid(
                    GetBlock(Input, X, Y + 1, Z)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, S, 0),
                        FVector(0, S, S),
                        FVector(S, S, S),
                        FVector(S, S, 0),

                        FVector(0, 1, 0),
                        BiomeColor,
                        false);
                }


                /*
                 * Z-
                 */
                if (!IsVoxelSolid(
                    GetBlock(Input, X, Y, Z - 1)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, 0),
                        FVector(0, S, 0),
                        FVector(S, S, 0),
                        FVector(S, 0, 0),

                        FVector(0, 0, -1),
                        BiomeColor,
                        false);
                }


                /*
                 * Z+
                 */
                if (!IsVoxelSolid(
                    GetBlock(Input, X, Y, Z + 1)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, S),
                        FVector(S, 0, S),
                        FVector(S, S, S),
                        FVector(0, S, S),

                        FVector(0, 0, 1),
                        BiomeColor,
                        false);
                }
            }
        }
    }
}

