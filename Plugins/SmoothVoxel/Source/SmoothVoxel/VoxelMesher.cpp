
#include "VoxelMesher.h"
#include "VoxelTypes.h"


namespace
{
    constexpr float WaterSurfaceInsetFraction = 0.08f;

    FORCEINLINE bool IsFaceVisibleInternal(uint8 Block, uint8 NeighborBlock)
    {
        const bool bWater = Block == uint8(EVoxelBlock::Water);

        if (bWater)
        {
            /*
             * Water only renders faces toward open air.
             * Faces against solid terrain/structures are internal surfaces.
             */
            return NeighborBlock == uint8(EVoxelBlock::Air);
        }

        return !IsVoxelSolid(NeighborBlock);
    }

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
bool FVoxelMesher::IsFaceVisible(
    uint8 Block,
    uint8 NeighborBlock)
{
    return IsFaceVisibleInternal(Block, NeighborBlock);
}


uint8 FVoxelMesher::GetCubicNeighborBlock(
    const FVoxelMeshBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z,
    bool bCurrentWater)
{
    const int32 Size = Input.Size;

    if (X >= 0 && X < Size &&
        Y >= 0 && Y < Size &&
        Z >= 0 && Z < Size)
    {
        const int32 Index =
            BlockIndex(X, Y, Z, Size);

        if (bCurrentWater)
        {
            return Input.Blocks[Index];
        }

        if (Input.StructureFlags.Num() ==
                Size * Size * Size &&
            Input.StructureFlags[Index] != 0)
        {
            return Input.Blocks[Index];
        }

        return uint8(EVoxelBlock::Air);
    }

    if (bCurrentWater)
    {
        if (X < 0 && Y >= 0 && Y < Size && Z >= 0 && Z < Size)
        {
            return Input.Neighbors.XMinus[
                SideIndex(Y, Z, Size)];
        }

        if (X >= Size && Y >= 0 && Y < Size && Z >= 0 && Z < Size)
        {
            return Input.Neighbors.XPlus[
                SideIndex(Y, Z, Size)];
        }

        if (Y < 0 && X >= 0 && X < Size && Z >= 0 && Z < Size)
        {
            return Input.Neighbors.YMinus[
                SideIndex(X, Z, Size)];
        }

        if (Y >= Size && X >= 0 && X < Size && Z >= 0 && Z < Size)
        {
            return Input.Neighbors.YPlus[
                SideIndex(X, Z, Size)];
        }

        if (Z < 0 && X >= 0 && X < Size && Y >= 0 && Y < Size)
        {
            return Input.Neighbors.ZMinus[
                SideIndex(X, Y, Size)];
        }

        if (Z >= Size && X >= 0 && X < Size && Y >= 0 && Y < Size)
        {
            return Input.Neighbors.ZPlus[
                SideIndex(X, Y, Size)];
        }

        return uint8(EVoxelBlock::Air);
    }

    if (X < 0 && Y >= 0 && Y < Size && Z >= 0 && Z < Size)
    {
        return Input.Neighbors.XMinusStructure[
                   SideIndex(Y, Z, Size)] != 0
            ? Input.Neighbors.XMinus[
                SideIndex(Y, Z, Size)]
            : uint8(EVoxelBlock::Air);
    }

    if (X >= Size && Y >= 0 && Y < Size && Z >= 0 && Z < Size)
    {
        return Input.Neighbors.XPlusStructure[
                   SideIndex(Y, Z, Size)] != 0
            ? Input.Neighbors.XPlus[
                SideIndex(Y, Z, Size)]
            : uint8(EVoxelBlock::Air);
    }

    if (Y < 0 && X >= 0 && X < Size && Z >= 0 && Z < Size)
    {
        return Input.Neighbors.YMinusStructure[
                   SideIndex(X, Z, Size)] != 0
            ? Input.Neighbors.YMinus[
                SideIndex(X, Z, Size)]
            : uint8(EVoxelBlock::Air);
    }

    if (Y >= Size && X >= 0 && X < Size && Z >= 0 && Z < Size)
    {
        return Input.Neighbors.YPlusStructure[
                   SideIndex(X, Z, Size)] != 0
            ? Input.Neighbors.YPlus[
                SideIndex(X, Z, Size)]
            : uint8(EVoxelBlock::Air);
    }

    if (Z < 0 && X >= 0 && X < Size && Y >= 0 && Y < Size)
    {
        return Input.Neighbors.ZMinusStructure[
                   SideIndex(X, Y, Size)] != 0
            ? Input.Neighbors.ZMinus[
                SideIndex(X, Y, Size)]
            : uint8(EVoxelBlock::Air);
    }

    if (Z >= Size && X >= 0 && X < Size && Y >= 0 && Y < Size)
    {
        return Input.Neighbors.ZPlusStructure[
                   SideIndex(X, Y, Size)] != 0
            ? Input.Neighbors.ZPlus[
                SideIndex(X, Y, Size)]
            : uint8(EVoxelBlock::Air);
    }

    return uint8(EVoxelBlock::Air);
}


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

                const bool bWater = Block == uint8(EVoxelBlock::Water);

                if (!bWater && !IsVoxelSolid(Block))
                {
                    continue;
                }

                if (!bWater &&
                    Input.StructureFlags.Num() == MaxBlocks &&
                    Input.StructureFlags[
                        BlockIndex(X, Y, Z, Size)] == 0)
                {
                    continue;
                }


                const FVector Origin(
                    X * VoxelSize,
                    Y * VoxelSize,
                    Z * VoxelSize);


                const float S = VoxelSize;
                float WaterTop = S;

                if (bWater)
                {
                    int32 SolidNeighborCount = 0;

                    const int32 NeighborDX[4] = { -1, 1, 0, 0 };
                    const int32 NeighborDY[4] = { 0, 0, -1, 1 };

                    for (int32 Neighbor = 0;
                         Neighbor < 4;
                         ++Neighbor)
                    {
                        const uint8 NeighborBlock =
                            GetCubicNeighborBlock(
                                Input,
                                X + NeighborDX[Neighbor],
                                Y + NeighborDY[Neighbor],
                                Z,
                                true);

                        if (IsVoxelSolid(
                            static_cast<EVoxelBlock>(NeighborBlock)))
                        {
                            ++SolidNeighborCount;
                        }
                    }

                    /*
                     * Deep water stays slightly inset. Near solid shore cells
                     * the surface rises toward the terrain level, producing a
                     * small shoreline transition instead of a hard vertical cut.
                     */
                    const float ShoreBlend =
                        static_cast<float>(
                            FMath::Clamp(
                                SolidNeighborCount,
                                0,
                                2)) *
                        0.04f;

                    const float WaterTopFraction =
                        FMath::Clamp(
                            (1.0f - WaterSurfaceInsetFraction) +
                                ShoreBlend,
                            0.0f,
                            1.0f);

                    WaterTop =
                        S * WaterTopFraction;
                }

                /*
                 * Сейчас используем понятные preview/debug-цвета
                 * именно по типу блока, а не по биому.
                 *
                 * Благодаря этому Grass всегда выглядит как Grass,
                 * Dirt как Dirt и т.д. независимо от того,
                 * в каком биоме находится блок.
                 *
                 * Позже эти Vertex Colors можно заменить
                 * полноценными block-specific материалами без
                 * изменения логики генерации.
                 */
                FLinearColor BlockColor =
                    FLinearColor::White;

                switch (static_cast<EVoxelBlock>(Block))
                {
                case EVoxelBlock::Grass:
                    BlockColor =
                        FLinearColor(
                            0.20f,
                            0.65f,
                            0.12f,
                            1.0f);
                    break;

                case EVoxelBlock::Dirt:
                    BlockColor =
                        FLinearColor(
                            0.45f,
                            0.25f,
                            0.10f,
                            1.0f);
                    break;

                case EVoxelBlock::Stone:
                    BlockColor =
                        FLinearColor(
                            0.50f,
                            0.52f,
                            0.56f,
                            1.0f);
                    break;

                case EVoxelBlock::Sand:
                    BlockColor =
                        FLinearColor(
                            0.85f,
                            0.72f,
                            0.42f,
                            1.0f);
                    break;

                case EVoxelBlock::Wood:
                    BlockColor =
                        FLinearColor(
                            0.58f,
                            0.32f,
                            0.12f,
                            1.0f);
                    break;

                case EVoxelBlock::Water:
                    BlockColor =
                        FLinearColor(
                            0.05f,
                            0.35f,
                            0.85f,
                            1.0f);
                    break;

                case EVoxelBlock::Air:
                default:
                    break;
                }


                /*
                 * X-
                 */
                if (IsFaceVisible(
                    Block,
                    GetCubicNeighborBlock(
                        Input,
                        X - 1, Y, Z,
                        bWater)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, 0),
                        FVector(0, 0, WaterTop),
                        FVector(0, S, WaterTop),
                        FVector(0, S, 0),

                        FVector(-1, 0, 0),
                        BlockColor,
                        bWater);
                }


                /*
                 * X+
                 */
                if (IsFaceVisible(
                    Block,
                    GetCubicNeighborBlock(
                        Input,
                        X + 1, Y, Z,
                        bWater)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(S, 0, 0),
                        FVector(S, S, 0),
                        FVector(S, S, WaterTop),
                        FVector(S, 0, WaterTop),

                        FVector(1, 0, 0),
                        BlockColor,
                        bWater);
                }


                /*
                 * Y-
                 */
                if (IsFaceVisible(
                    Block,
                    GetCubicNeighborBlock(
                        Input,
                        X, Y - 1, Z,
                        bWater)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, 0),
                        FVector(S, 0, 0),
                        FVector(S, 0, WaterTop),
                        FVector(0, 0, WaterTop),

                        FVector(0, -1, 0),
                        BlockColor,
                        bWater);
                }


                /*
                 * Y+
                 */
                if (IsFaceVisible(
                    Block,
                    GetCubicNeighborBlock(
                        Input,
                        X, Y + 1, Z,
                        bWater)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, S, 0),
                        FVector(0, S, WaterTop),
                        FVector(S, S, WaterTop),
                        FVector(S, S, 0),

                        FVector(0, 1, 0),
                        BlockColor,
                        bWater);
                }


                /*
                 * Z-
                 */
                if (IsFaceVisible(
                    Block,
                    GetCubicNeighborBlock(
                        Input,
                        X, Y, Z - 1,
                        bWater)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, 0),
                        FVector(0, S, 0),
                        FVector(S, S, 0),
                        FVector(S, 0, 0),

                        FVector(0, 0, -1),
                        BlockColor,
                        bWater);
                }


                /*
                 * Z+
                 */
                if (IsFaceVisible(
                    Block,
                    GetCubicNeighborBlock(
                        Input,
                        X, Y, Z + 1,
                        bWater)))
                {
                    AddFace(
                        Output,
                        Origin,

                        FVector(0, 0, WaterTop),
                        FVector(S, 0, WaterTop),
                        FVector(S, S, WaterTop),
                        FVector(0, S, WaterTop),

                        FVector(0, 0, 1),
                        BlockColor,
                        bWater);
                }
            }
        }
    }
}

