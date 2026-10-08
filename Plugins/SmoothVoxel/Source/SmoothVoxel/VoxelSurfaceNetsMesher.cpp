#include "VoxelSurfaceNetsMesher.h"

#include "Math/UnrealMathUtility.h"


namespace
{
    constexpr float IsoLevel = 0.5f;

    constexpr int32 CellHalo = 1;
    constexpr int32 NodeHalo = 1;

    FORCEINLINE int32 BlockIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        const int32 Side = Size + 3;
        const int32 Offset = 1;

        return
            (X + Offset) +
            (Y + Offset) * Side +
            (Z + Offset) * Side * Side;
    }


    FORCEINLINE int32 NodeIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        const int32 Side = Size + 2;
        const int32 Offset = NodeHalo;

        return
            (X + Offset) +
            (Y + Offset) * Side +
            (Z + Offset) * Side * Side;
    }


    FORCEINLINE int32 CellVertexIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        const int32 Side = Size + 1;
        const int32 Offset = CellHalo;

        return
            (X + Offset) +
            (Y + Offset) * Side +
            (Z + Offset) * Side * Side;
    }


    FORCEINLINE bool IsInsideNodeRange(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        return
            X >= -NodeHalo &&
            X <= Size &&
            Y >= -NodeHalo &&
            Y <= Size &&
            Z >= -NodeHalo &&
            Z <= Size;
    }


    FVector ComputeTrilinearGradient(
        const TArray<float>& Densities,
        int32 Size,
        const FVector& Position)
    {
        int32 CellX =
            FMath::FloorToInt(Position.X);

        int32 CellY =
            FMath::FloorToInt(Position.Y);

        int32 CellZ =
            FMath::FloorToInt(Position.Z);

        CellX = FMath::Clamp(CellX, -1, Size - 1);
        CellY = FMath::Clamp(CellY, -1, Size - 1);
        CellZ = FMath::Clamp(CellZ, -1, Size - 1);

        const float FX =
            FMath::Clamp(
                Position.X - static_cast<float>(CellX),
                0.0f,
                1.0f);

        const float FY =
            FMath::Clamp(
                Position.Y - static_cast<float>(CellY),
                0.0f,
                1.0f);

        const float FZ =
            FMath::Clamp(
                Position.Z - static_cast<float>(CellZ),
                0.0f,
                1.0f);

        auto D = [&](int32 DX, int32 DY, int32 DZ)
        {
            const int32 X = CellX + DX;
            const int32 Y = CellY + DY;
            const int32 Z = CellZ + DZ;

            if (!IsInsideNodeRange(X, Y, Z, Size))
            {
                return 0.0f;
            }

            return Densities[
                NodeIndex(X, Y, Z, Size)];
        };

        const float D000 = D(0, 0, 0);
        const float D100 = D(1, 0, 0);
        const float D010 = D(0, 1, 0);
        const float D110 = D(1, 1, 0);
        const float D001 = D(0, 0, 1);
        const float D101 = D(1, 0, 1);
        const float D011 = D(0, 1, 1);
        const float D111 = D(1, 1, 1);

        const float DX =
            (1.0f - FY) * (1.0f - FZ) * (D100 - D000) +
            FY * (1.0f - FZ) * (D110 - D010) +
            (1.0f - FY) * FZ * (D101 - D001) +
            FY * FZ * (D111 - D011);

        const float DY =
            (1.0f - FX) * (1.0f - FZ) * (D010 - D000) +
            FX * (1.0f - FZ) * (D110 - D100) +
            (1.0f - FX) * FZ * (D011 - D001) +
            FX * FZ * (D111 - D101);

        const float DZ =
            (1.0f - FX) * (1.0f - FY) * (D001 - D000) +
            FX * (1.0f - FY) * (D101 - D100) +
            (1.0f - FX) * FY * (D011 - D010) +
            FX * FY * (D111 - D110);

        return FVector(DX, DY, DZ);
    }


    void AddSurfaceQuad(
        FVoxelMeshBuildOutput& Output,
        int32 A,
        int32 B,
        int32 C,
        int32 D,
        const FVector& FaceCenter,
        const TArray<float>& Densities,
        int32 Size)
    {
        int32 Indices[4];
        int32 IndexCount = 0;

        const int32 Candidates[4] = { A, B, C, D };

        for (const int32 Candidate : Candidates)
        {
            if (Candidate < 0 ||
                Candidate >= Output.Vertices.Num())
            {
                continue;
            }

            bool bAlreadyAdded = false;

            for (int32 Index = 0;
                 Index < IndexCount;
                 ++Index)
            {
                if (Indices[Index] == Candidate)
                {
                    bAlreadyAdded = true;
                    break;
                }
            }

            if (!bAlreadyAdded &&
                IndexCount < 4)
            {
                Indices[IndexCount++] = Candidate;
            }
        }

        if (IndexCount < 3)
        {
            return;
        }

        FVector Outward =
            -ComputeTrilinearGradient(
                Densities,
                Size,
                FaceCenter);

        if (!Outward.Normalize())
        {
            return;
        }

        const FVector P0 =
            Output.Vertices[Indices[0]];

        const FVector P1 =
            Output.Vertices[Indices[1]];

        const FVector P2 =
            Output.Vertices[Indices[2]];

        FVector FaceNormal =
            FVector::CrossProduct(
                P1 - P0,
                P2 - P0);

        if (!FaceNormal.Normalize())
        {
            return;
        }

        if (FVector::DotProduct(
                FaceNormal,
                Outward) < 0.0f)
        {
            Swap(
                Indices[1],
                Indices[IndexCount - 1]);
        }

        Output.Triangles.Add(Indices[0]);
        Output.Triangles.Add(Indices[1]);
        Output.Triangles.Add(Indices[2]);

        if (IndexCount == 4)
        {
            Output.Triangles.Add(Indices[0]);
            Output.Triangles.Add(Indices[2]);
            Output.Triangles.Add(Indices[3]);
        }
    }
}


uint8 FVoxelSurfaceNetsMesher::GetBlock(
    const FVoxelSurfaceNetsBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z)
{
    if (X < -1 || X > Input.Size + 1 ||
        Y < -1 || Y > Input.Size + 1 ||
        Z < -1 || Z > Input.Size + 1)
    {
        return uint8(EVoxelBlock::Air);
    }

    return Input.Blocks[
        BlockIndex(
            X,
            Y,
            Z,
            Input.Size)];
}


float FVoxelSurfaceNetsMesher::GetNodeDensity(
    const FVoxelSurfaceNetsBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z)
{
    if (!IsInsideNodeRange(
            X,
            Y,
            Z,
            Input.Size))
    {
        return 0.0f;
    }

    float Sum = 0.0f;

    for (int32 DZ = 0; DZ <= 1; ++DZ)
    {
        for (int32 DY = 0; DY <= 1; ++DY)
        {
            for (int32 DX = 0; DX <= 1; ++DX)
            {
                const EVoxelBlock Block =
                    static_cast<EVoxelBlock>(
                        GetBlock(
                            Input,
                            X + DX,
                            Y + DY,
                            Z + DZ));

                if (IsVoxelSolid(Block))
                {
                    Sum += 1.0f;
                }
            }
        }
    }

    return Sum / 8.0f;
}


FLinearColor FVoxelSurfaceNetsMesher::GetBlockColor(
    uint8 Block)
{
    switch (static_cast<EVoxelBlock>(Block))
    {
    case EVoxelBlock::Grass:
        return FLinearColor(0.20f, 0.65f, 0.12f, 1.0f);

    case EVoxelBlock::Dirt:
        return FLinearColor(0.45f, 0.25f, 0.10f, 1.0f);

    case EVoxelBlock::Stone:
        return FLinearColor(0.50f, 0.52f, 0.56f, 1.0f);

    case EVoxelBlock::Sand:
        return FLinearColor(0.85f, 0.72f, 0.42f, 1.0f);

    case EVoxelBlock::Wood:
        return FLinearColor(0.58f, 0.32f, 0.12f, 1.0f);

    case EVoxelBlock::Water:
        return FLinearColor(0.05f, 0.35f, 0.85f, 1.0f);

    case EVoxelBlock::Air:
    default:
        return FLinearColor::White;
    }
}


uint8 FVoxelSurfaceNetsMesher::GetRepresentativeBlock(
    const FVoxelSurfaceNetsBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z)
{
    int32 BestBlock = uint8(EVoxelBlock::Stone);
    int32 BestCount = 0;

    for (int32 DZ = 0; DZ <= 1; ++DZ)
    {
        for (int32 DY = 0; DY <= 1; ++DY)
        {
            for (int32 DX = 0; DX <= 1; ++DX)
            {
                const uint8 Block =
                    GetBlock(
                        Input,
                        X + DX,
                        Y + DY,
                        Z + DZ);

                if (!IsVoxelSolid(
                    static_cast<EVoxelBlock>(Block)))
                {
                    continue;
                }

                int32 Count = 0;

                for (int32 AZ = 0; AZ <= 1; ++AZ)
                {
                    for (int32 AY = 0; AY <= 1; ++AY)
                    {
                        for (int32 AX = 0; AX <= 1; ++AX)
                        {
                            if (GetBlock(
                                    Input,
                                    X + AX,
                                    Y + AY,
                                    Z + AZ) == Block)
                            {
                                ++Count;
                            }
                        }
                    }
                }

                if (Count > BestCount)
                {
                    BestCount = Count;
                    BestBlock = Block;
                }
            }
        }
    }

    return static_cast<uint8>(BestBlock);
}


void FVoxelSurfaceNetsMesher::Build(
    const FVoxelSurfaceNetsBuildInput& Input,
    FVoxelMeshBuildOutput& Output)
{
    const int32 Size = Input.Size;

    if (Size <= 0)
    {
        return;
    }

    const int32 BlockSide = Size + 3;

    Output.Vertices.Reserve(
        Output.Vertices.Num() + Size * Size * Size);
    Output.Normals.Reserve(
        Output.Normals.Num() + Size * Size * Size);
    Output.UV0.Reserve(
        Output.UV0.Num() + Size * Size * Size);
    Output.VertexColors.Reserve(
        Output.VertexColors.Num() + Size * Size * Size);
    Output.Triangles.Reserve(
        Output.Triangles.Num() + Size * Size * 6);

    if (Input.Blocks.Num() !=
        BlockSide * BlockSide * BlockSide)
    {
        return;
    }

    const int32 NodeSide = Size + 2;

    TArray<float> Densities;
    Densities.SetNumZeroed(
        NodeSide * NodeSide * NodeSide);

    for (int32 Z = -1; Z <= Size; ++Z)
    {
        for (int32 Y = -1; Y <= Size; ++Y)
        {
            for (int32 X = -1; X <= Size; ++X)
            {
                Densities[
                    NodeIndex(
                        X,
                        Y,
                        Z,
                        Size)] =
                    GetNodeDensity(
                        Input,
                        X,
                        Y,
                        Z);
            }
        }
    }

    const int32 CellSide = Size + 1;

    TArray<int32> CellVertices;
    CellVertices.SetNum(
        CellSide * CellSide * CellSide);

    for (int32& Index : CellVertices)
    {
        Index = -1;
    }

    for (int32 Z = -1; Z < Size; ++Z)
    {
        for (int32 Y = -1; Y < Size; ++Y)
        {
            for (int32 X = -1; X < Size; ++X)
            {
                float Corner[2][2][2];

                float MinDensity = 1.0f;
                float MaxDensity = 0.0f;

                for (int32 DZ = 0; DZ <= 1; ++DZ)
                {
                    for (int32 DY = 0; DY <= 1; ++DY)
                    {
                        for (int32 DX = 0; DX <= 1; ++DX)
                        {
                            const float Density =
                                Densities[
                                    NodeIndex(
                                        X + DX,
                                        Y + DY,
                                        Z + DZ,
                                        Size)];

                            Corner[DX][DY][DZ] =
                                Density;

                            MinDensity =
                                FMath::Min(
                                    MinDensity,
                                    Density);

                            MaxDensity =
                                FMath::Max(
                                    MaxDensity,
                                    Density);
                        }
                    }
                }

                if (MinDensity >= IsoLevel ||
                    MaxDensity < IsoLevel)
                {
                    continue;
                }

                FVector Average =
                    FVector::ZeroVector;

                int32 CrossingCount = 0;

                static const int32 EdgeCorners[12][2][3] =
                {
                    {{0,0,0}, {1,0,0}},
                    {{1,0,0}, {1,1,0}},
                    {{1,1,0}, {0,1,0}},
                    {{0,1,0}, {0,0,0}},

                    {{0,0,1}, {1,0,1}},
                    {{1,0,1}, {1,1,1}},
                    {{1,1,1}, {0,1,1}},
                    {{0,1,1}, {0,0,1}},

                    {{0,0,0}, {0,0,1}},
                    {{1,0,0}, {1,0,1}},
                    {{1,1,0}, {1,1,1}},
                    {{0,1,0}, {0,1,1}}
                };

                for (const auto& Edge : EdgeCorners)
                {
                    const int32 AX = Edge[0][0];
                    const int32 AY = Edge[0][1];
                    const int32 AZ = Edge[0][2];

                    const int32 BX = Edge[1][0];
                    const int32 BY = Edge[1][1];
                    const int32 BZ = Edge[1][2];

                    const float D0 =
                        Corner[AX][AY][AZ];

                    const float D1 =
                        Corner[BX][BY][BZ];

                    if ((D0 < IsoLevel) ==
                        (D1 < IsoLevel))
                    {
                        continue;
                    }

                    const float Denominator =
                        D1 - D0;

                    if (FMath::IsNearlyZero(
                        Denominator))
                    {
                        continue;
                    }

                    const float T =
                        (IsoLevel - D0) /
                        Denominator;

                    const FVector P0(
                        static_cast<float>(AX),
                        static_cast<float>(AY),
                        static_cast<float>(AZ));

                    const FVector P1(
                        static_cast<float>(BX),
                        static_cast<float>(BY),
                        static_cast<float>(BZ));

                    Average +=
                        FMath::Lerp(
                            P0,
                            P1,
                            T);

                    ++CrossingCount;
                }

                if (CrossingCount == 0)
                {
                    continue;
                }

                Average /=
                    static_cast<float>(CrossingCount);

                const FVector CellPosition =
                    FVector(
                        static_cast<float>(X),
                        static_cast<float>(Y),
                        static_cast<float>(Z)) +
                    Average;

                FVector Normal =
                    -ComputeTrilinearGradient(
                        Densities,
                        Size,
                        CellPosition);

                if (!Normal.Normalize())
                {
                    Normal =
                        FVector::UpVector;
                }

                const uint8 RepresentativeBlock =
                    GetRepresentativeBlock(
                        Input,
                        X,
                        Y,
                        Z);

                const int32 VertexIndex =
                    Output.Vertices.Num();

                Output.Vertices.Add(
                    CellPosition *
                    Input.VoxelSize);

                Output.Normals.Add(
                    Normal);

                Output.UV0.Add(
                    FVector2D(
                        CellPosition.X * 0.05f,
                        CellPosition.Y * 0.05f));

                Output.VertexColors.Add(
                    GetBlockColor(
                        RepresentativeBlock));

                CellVertices[
                    CellVertexIndex(
                        X,
                        Y,
                        Z,
                        Size)] =
                    VertexIndex;
            }
        }
    }

    /*
     * Every grid edge owns one surface quad.
     * The edge coordinate is in [0, Size-1], which means
     * each edge belongs to exactly one chunk.
     */
    for (int32 Z = 0; Z < Size; ++Z)
    {
        for (int32 Y = 0; Y < Size; ++Y)
        {
            for (int32 X = 0; X < Size; ++X)
            {
                const float D0X =
                    Densities[
                        NodeIndex(X, Y, Z, Size)];

                const float D1X =
                    Densities[
                        NodeIndex(X + 1, Y, Z, Size)];

                if ((D0X < IsoLevel) !=
                    (D1X < IsoLevel))
                {
                    const int32 C0 =
                        CellVertexIndex(
                            X,
                            Y - 1,
                            Z - 1,
                            Size);

                    const int32 C1 =
                        CellVertexIndex(
                            X,
                            Y,
                            Z - 1,
                            Size);

                    const int32 C2 =
                        CellVertexIndex(
                            X,
                            Y,
                            Z,
                            Size);

                    const int32 C3 =
                        CellVertexIndex(
                            X,
                            Y - 1,
                            Z,
                            Size);

                    const int32 I0 =
                        CellVertices[C0];

                    const int32 I1 =
                        CellVertices[C1];

                    const int32 I2 =
                        CellVertices[C2];

                    const int32 I3 =
                        CellVertices[C3];

                    TArray<int32, TInlineAllocator<4>> Indices;
                    if (I0 >= 0) Indices.Add(I0);
                    if (I1 >= 0) Indices.Add(I1);
                    if (I2 >= 0) Indices.Add(I2);
                    if (I3 >= 0) Indices.Add(I3);

                    if (Indices.Num() >= 3)
                    {
                        const FVector Center =
                            (
                                Output.Vertices[Indices[0]] +
                                Output.Vertices[Indices[1]] +
                                Output.Vertices[Indices[2]]
                                ) / 3.0f /
                            Input.VoxelSize;

                        const int32 Before =
                            Output.Triangles.Num();

                        AddSurfaceQuad(
                            Output,
                            I0,
                            I1,
                            I2,
                            I3,
                            Center,
                            Densities,
                            Size);

                        (void)Before;
                    }
                }

                const float D0Y =
                    Densities[
                        NodeIndex(X, Y, Z, Size)];

                const float D1Y =
                    Densities[
                        NodeIndex(X, Y + 1, Z, Size)];

                if ((D0Y < IsoLevel) !=
                    (D1Y < IsoLevel))
                {
                    const int32 C0 =
                        CellVertexIndex(
                            X - 1,
                            Y,
                            Z - 1,
                            Size);

                    const int32 C1 =
                        CellVertexIndex(
                            X,
                            Y,
                            Z - 1,
                            Size);

                    const int32 C2 =
                        CellVertexIndex(
                            X,
                            Y,
                            Z,
                            Size);

                    const int32 C3 =
                        CellVertexIndex(
                            X - 1,
                            Y,
                            Z,
                            Size);

                    const int32 I0 =
                        CellVertices[C0];

                    const int32 I1 =
                        CellVertices[C1];

                    const int32 I2 =
                        CellVertices[C2];

                    const int32 I3 =
                        CellVertices[C3];

                    TArray<int32, TInlineAllocator<4>> Indices;
                    if (I0 >= 0) Indices.Add(I0);
                    if (I1 >= 0) Indices.Add(I1);
                    if (I2 >= 0) Indices.Add(I2);
                    if (I3 >= 0) Indices.Add(I3);

                    if (Indices.Num() >= 3)
                    {
                        const FVector Center =
                            (
                                Output.Vertices[Indices[0]] +
                                Output.Vertices[Indices[1]] +
                                Output.Vertices[Indices[2]]
                                ) / 3.0f /
                            Input.VoxelSize;

                        AddSurfaceQuad(
                            Output,
                            I0,
                            I1,
                            I2,
                            I3,
                            Center,
                            Densities,
                            Size);
                    }
                }

                const float D0Z =
                    Densities[
                        NodeIndex(X, Y, Z, Size)];

                const float D1Z =
                    Densities[
                        NodeIndex(X, Y, Z + 1, Size)];

                if ((D0Z < IsoLevel) !=
                    (D1Z < IsoLevel))
                {
                    const int32 C0 =
                        CellVertexIndex(
                            X - 1,
                            Y - 1,
                            Z,
                            Size);

                    const int32 C1 =
                        CellVertexIndex(
                            X,
                            Y - 1,
                            Z,
                            Size);

                    const int32 C2 =
                        CellVertexIndex(
                            X,
                            Y,
                            Z,
                            Size);

                    const int32 C3 =
                        CellVertexIndex(
                            X - 1,
                            Y,
                            Z,
                            Size);

                    const int32 I0 =
                        CellVertices[C0];

                    const int32 I1 =
                        CellVertices[C1];

                    const int32 I2 =
                        CellVertices[C2];

                    const int32 I3 =
                        CellVertices[C3];

                    TArray<int32, TInlineAllocator<4>> Indices;
                    if (I0 >= 0) Indices.Add(I0);
                    if (I1 >= 0) Indices.Add(I1);
                    if (I2 >= 0) Indices.Add(I2);
                    if (I3 >= 0) Indices.Add(I3);

                    if (Indices.Num() >= 3)
                    {
                        const FVector Center =
                            (
                                Output.Vertices[Indices[0]] +
                                Output.Vertices[Indices[1]] +
                                Output.Vertices[Indices[2]]
                                ) / 3.0f /
                            Input.VoxelSize;

                        AddSurfaceQuad(
                            Output,
                            I0,
                            I1,
                            I2,
                            I3,
                            Center,
                            Densities,
                            Size);
                    }
                }
            }
        }
    }
}
