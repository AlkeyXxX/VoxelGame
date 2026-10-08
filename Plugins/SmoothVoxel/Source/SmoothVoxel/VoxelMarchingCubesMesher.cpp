#include "VoxelMarchingCubesMesher.h"

#include "VoxelTypes.h"
#include "SmoothVoxelMCTables.h"

#include "Math/UnrealMathUtility.h"


namespace
{
    constexpr float IsoLevel = 0.5f;

    FORCEINLINE int32 MarchingCubesBlockIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        const int32 Side = Size + 2;

        return
            (X + 1) +
            (Y + 1) * Side +
            (Z + 1) * Side * Side;
    }

    FORCEINLINE int32 DensityIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        const int32 Side = Size + 2;

        return
            X +
            Y * Side +
            Z * Side * Side;
    }

    FORCEINLINE int32 XEdgeIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        return
            X +
            Y * Size +
            Z * Size * (Size + 1);
    }

    FORCEINLINE int32 YEdgeIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        return
            X +
            Y * (Size + 1) +
            Z * (Size + 1) * Size;
    }

    FORCEINLINE int32 ZEdgeIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        return
            X +
            Y * (Size + 1) +
            Z * (Size + 1) * (Size + 1);
    }

    FVector ComputeGradient(
        const TArray<float>& Densities,
        int32 Size,
        const FVector& Position)
    {
        int32 X = FMath::RoundToInt(Position.X);
        int32 Y = FMath::RoundToInt(Position.Y);
        int32 Z = FMath::RoundToInt(Position.Z);

        const int32 MaxNode = Size + 1;

        X = FMath::Clamp(X, 0, MaxNode);
        Y = FMath::Clamp(Y, 0, MaxNode);
        Z = FMath::Clamp(Z, 0, MaxNode);

        float DX = 0.0f;
        float DY = 0.0f;
        float DZ = 0.0f;

        if (X == 0)
        {
            DX =
                Densities[
                    DensityIndex(X + 1, Y, Z, Size)] -
                Densities[
                    DensityIndex(X, Y, Z, Size)];
        }
        else if (X == MaxNode)
        {
            DX =
                Densities[
                    DensityIndex(X, Y, Z, Size)] -
                Densities[
                    DensityIndex(X - 1, Y, Z, Size)];
        }
        else
        {
            DX =
                0.5f *
                (Densities[
                    DensityIndex(X + 1, Y, Z, Size)] -
                 Densities[
                    DensityIndex(X - 1, Y, Z, Size)]);
        }

        if (Y == 0)
        {
            DY =
                Densities[
                    DensityIndex(X, Y + 1, Z, Size)] -
                Densities[
                    DensityIndex(X, Y, Z, Size)];
        }
        else if (Y == MaxNode)
        {
            DY =
                Densities[
                    DensityIndex(X, Y, Z, Size)] -
                Densities[
                    DensityIndex(X, Y - 1, Z, Size)];
        }
        else
        {
            DY =
                0.5f *
                (Densities[
                    DensityIndex(X, Y + 1, Z, Size)] -
                 Densities[
                    DensityIndex(X, Y - 1, Z, Size)]);
        }

        if (Z == 0)
        {
            DZ =
                Densities[
                    DensityIndex(X, Y, Z + 1, Size)] -
                Densities[
                    DensityIndex(X, Y, Z, Size)];
        }
        else if (Z == MaxNode)
        {
            DZ =
                Densities[
                    DensityIndex(X, Y, Z, Size)] -
                Densities[
                    DensityIndex(X, Y, Z - 1, Size)];
        }
        else
        {
            DZ =
                0.5f *
                (Densities[
                    DensityIndex(X, Y, Z + 1, Size)] -
                 Densities[
                    DensityIndex(X, Y, Z - 1, Size)]);
        }

        FVector Gradient(DX, DY, DZ);

        if (!Gradient.Normalize())
        {
            Gradient = FVector::UpVector;
        }

        return Gradient;
    }

    FVector EdgePosition(
        int32 Edge,
        int32 X,
        int32 Y,
        int32 Z,
        const float CornerDensity[8])
    {
        const int32 CornerA =
            SmoothVoxelMCTables::EdgeCorners[Edge][0];

        const int32 CornerB =
            SmoothVoxelMCTables::EdgeCorners[Edge][1];

        const int32 AX =
            SmoothVoxelMCTables::CornerOffset[CornerA][0];

        const int32 AY =
            SmoothVoxelMCTables::CornerOffset[CornerA][1];

        const int32 AZ =
            SmoothVoxelMCTables::CornerOffset[CornerA][2];

        const int32 BX =
            SmoothVoxelMCTables::CornerOffset[CornerB][0];

        const int32 BY =
            SmoothVoxelMCTables::CornerOffset[CornerB][1];

        const int32 BZ =
            SmoothVoxelMCTables::CornerOffset[CornerB][2];

        const float D0 = CornerDensity[CornerA];
        const float D1 = CornerDensity[CornerB];

        float T = 0.5f;

        if (!FMath::IsNearlyEqual(D0, D1))
        {
            T =
                (IsoLevel - D0) /
                (D1 - D0);
        }

        T = FMath::Clamp(T, 0.0f, 1.0f);

        return FVector(
            static_cast<float>(X + AX) +
                static_cast<float>(BX - AX) * T,
            static_cast<float>(Y + AY) +
                static_cast<float>(BY - AY) * T,
            static_cast<float>(Z + AZ) +
                static_cast<float>(BZ - AZ) * T);
    }
}


uint8 FVoxelMarchingCubesMesher::GetBlock(
    const FVoxelMarchingCubesBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z)
{
    if (X < -1 || X > Input.Size ||
        Y < -1 || Y > Input.Size ||
        Z < -1 || Z > Input.Size)
    {
        return uint8(EVoxelBlock::Air);
    }

    return Input.Blocks[
        MarchingCubesBlockIndex(
            X,
            Y,
            Z,
            Input.Size)];
}


float FVoxelMarchingCubesMesher::GetDensity(
    const FVoxelMarchingCubesBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z)
{
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
                            X - 1 + DX,
                            Y - 1 + DY,
                            Z - 1 + DZ));

                if (IsVoxelSolid(Block))
                {
                    Sum += 1.0f;
                }
            }
        }
    }

    return Sum / 8.0f;
}


FLinearColor FVoxelMarchingCubesMesher::GetBlockColor(
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

    default:
        return FLinearColor::White;
    }
}


uint8 FVoxelMarchingCubesMesher::GetRepresentativeBlock(
    const FVoxelMarchingCubesBuildInput& Input,
    int32 X,
    int32 Y,
    int32 Z)
{
    bool bFound = false;
    int32 BestZ = TNumericLimits<int32>::Lowest();
    uint8 BestBlock = uint8(EVoxelBlock::Stone);

    for (int32 DZ = 0; DZ <= 1; ++DZ)
    {
        for (int32 DY = 0; DY <= 1; ++DY)
        {
            for (int32 DX = 0; DX <= 1; ++DX)
            {
                const int32 SampleZ = Z + DZ;

                const uint8 Block =
                    GetBlock(
                        Input,
                        X + DX,
                        Y + DY,
                        SampleZ);

                if (!IsVoxelSolid(
                    static_cast<EVoxelBlock>(Block)))
                {
                    continue;
                }

                if (!bFound || SampleZ > BestZ)
                {
                    bFound = true;
                    BestZ = SampleZ;
                    BestBlock = Block;
                }
            }
        }
    }

    return BestBlock;
}


void FVoxelMarchingCubesMesher::Build(
    const FVoxelMarchingCubesBuildInput& Input,
    FVoxelMeshBuildOutput& Output)
{
    const int32 Size = Input.Size;

    if (Size <= 0)
    {
        return;
    }

    const int32 Side = Size + 2;

    if (Input.Blocks.Num() !=
        Side * Side * Side)
    {
        return;
    }

    const int32 NodeSide = Size + 2;

    TArray<float> Densities;
    Densities.SetNumUninitialized(
        NodeSide * NodeSide * NodeSide);

    for (int32 Z = 0; Z < NodeSide; ++Z)
    {
        for (int32 Y = 0; Y < NodeSide; ++Y)
        {
            for (int32 X = 0; X < NodeSide; ++X)
            {
                Densities[
                    DensityIndex(X, Y, Z, Size)] =
                    GetDensity(
                        Input,
                        X,
                        Y,
                        Z);
            }
        }
    }

    const int32 XEdgeCount =
        Size * (Size + 1) * (Size + 1);

    const int32 YEdgeCount =
        (Size + 1) * Size * (Size + 1);

    const int32 ZEdgeCount =
        (Size + 1) * (Size + 1) * Size;

    TArray<int32> XEdges;
    TArray<int32> YEdges;
    TArray<int32> ZEdges;

    XEdges.Init(-1, XEdgeCount);
    YEdges.Init(-1, YEdgeCount);
    ZEdges.Init(-1, ZEdgeCount);

    Output.Vertices.Reserve(
        Output.Vertices.Num() +
        Size * Size * 3);

    Output.Normals.Reserve(
        Output.Normals.Num() +
        Size * Size * 3);

    Output.UV0.Reserve(
        Output.UV0.Num() +
        Size * Size * 3);

    Output.VertexColors.Reserve(
        Output.VertexColors.Num() +
        Size * Size * 3);

    Output.Triangles.Reserve(
        Output.Triangles.Num() +
        Size * Size * 6);

    for (int32 Z = 0; Z < Size; ++Z)
    {
        for (int32 Y = 0; Y < Size; ++Y)
        {
            for (int32 X = 0; X < Size; ++X)
            {
                float CornerDensity[8];

                int32 CubeIndex = 0;

                for (int32 Corner = 0; Corner < 8; ++Corner)
                {
                    const int32 CX =
                        X +
                        SmoothVoxelMCTables::CornerOffset[Corner][0];

                    const int32 CY =
                        Y +
                        SmoothVoxelMCTables::CornerOffset[Corner][1];

                    const int32 CZ =
                        Z +
                        SmoothVoxelMCTables::CornerOffset[Corner][2];

                    CornerDensity[Corner] =
                        Densities[
                            DensityIndex(
                                CX,
                                CY,
                                CZ,
                                Size)];

                    /*
                     * The low-density side is air.
                     * Standard MC tables use this binary classification.
                     */
                    if (CornerDensity[Corner] < IsoLevel)
                    {
                        CubeIndex |=
                            1 << Corner;
                    }
                }

                const int EdgeMask =
                    SmoothVoxelMCTables::EdgeTable[CubeIndex];

                if (EdgeMask == 0)
                {
                    continue;
                }

                int32 EdgeVertex[12];

                for (int32 Edge = 0; Edge < 12; ++Edge)
                {
                    EdgeVertex[Edge] = -1;

                    if ((EdgeMask & (1 << Edge)) == 0)
                    {
                        continue;
                    }

                    int32* Cache = nullptr;
                    int32 CacheIndex = -1;

                    switch (Edge)
                    {
                    case 0:
                        Cache = &XEdges[
                            XEdgeIndex(
                                X,
                                Y,
                                Z,
                                Size)];
                        break;

                    case 1:
                        Cache = &YEdges[
                            YEdgeIndex(
                                X + 1,
                                Y,
                                Z,
                                Size)];
                        break;

                    case 2:
                        Cache = &XEdges[
                            XEdgeIndex(
                                X,
                                Y + 1,
                                Z,
                                Size)];
                        break;

                    case 3:
                        Cache = &YEdges[
                            YEdgeIndex(
                                X,
                                Y,
                                Z,
                                Size)];
                        break;

                    case 4:
                        Cache = &XEdges[
                            XEdgeIndex(
                                X,
                                Y,
                                Z + 1,
                                Size)];
                        break;

                    case 5:
                        Cache = &YEdges[
                            YEdgeIndex(
                                X + 1,
                                Y,
                                Z + 1,
                                Size)];
                        break;

                    case 6:
                        Cache = &XEdges[
                            XEdgeIndex(
                                X,
                                Y + 1,
                                Z + 1,
                                Size)];
                        break;

                    case 7:
                        Cache = &YEdges[
                            YEdgeIndex(
                                X,
                                Y,
                                Z + 1,
                                Size)];
                        break;

                    case 8:
                        Cache = &ZEdges[
                            ZEdgeIndex(
                                X,
                                Y,
                                Z,
                                Size)];
                        break;

                    case 9:
                        Cache = &ZEdges[
                            ZEdgeIndex(
                                X + 1,
                                Y,
                                Z,
                                Size)];
                        break;

                    case 10:
                        Cache = &ZEdges[
                            ZEdgeIndex(
                                X + 1,
                                Y + 1,
                                Z,
                                Size)];
                        break;

                    case 11:
                        Cache = &ZEdges[
                            ZEdgeIndex(
                                X,
                                Y + 1,
                                Z,
                                Size)];
                        break;

                    default:
                        break;
                    }

                    if (!Cache)
                    {
                        continue;
                    }

                    CacheIndex = *Cache;

                    if (CacheIndex < 0)
                    {
                        const FVector Position =
                            EdgePosition(
                                Edge,
                                X,
                                Y,
                                Z,
                                CornerDensity);

                        const int32 VertexIndex =
                            Output.Vertices.Num();

                        const FVector Gradient =
                            ComputeGradient(
                                Densities,
                                Size,
                                Position);

                        Output.Vertices.Add(
                            Position *
                            Input.VoxelSize);

                        Output.Normals.Add(
                            -Gradient);

                        const int32 MaterialBlockZ =
                            FMath::FloorToInt(
                                Position.Z);

                        const uint8 RepresentativeBlock =
                            GetRepresentativeBlock(
                                Input,
                                X,
                                Y,
                                MaterialBlockZ);

                        Output.UV0.Add(
                            FVector2D(
                                Position.X * 0.05f,
                                Position.Y * 0.05f));

                        Output.VertexColors.Add(
                            GetBlockColor(
                                RepresentativeBlock));

                        *Cache = VertexIndex;
                        CacheIndex = VertexIndex;
                    }

                    EdgeVertex[Edge] = CacheIndex;
                }

                for (int32 T = 0; T < 16; T += 3)
                {
                    const int8 EdgeA =
                        SmoothVoxelMCTables::TriTable[
                            CubeIndex][T];

                    if (EdgeA < 0)
                    {
                        break;
                    }

                    const int8 EdgeB =
                        SmoothVoxelMCTables::TriTable[
                            CubeIndex][T + 1];

                    const int8 EdgeC =
                        SmoothVoxelMCTables::TriTable[
                            CubeIndex][T + 2];

                    if (EdgeB < 0 || EdgeC < 0)
                    {
                        break;
                    }

                    const int32 I0 =
                        EdgeVertex[EdgeA];

                    const int32 I1 =
                        EdgeVertex[EdgeB];

                    const int32 I2 =
                        EdgeVertex[EdgeC];

                    if (I0 < 0 || I1 < 0 || I2 < 0)
                    {
                        continue;
                    }

                    FVector FaceNormal =
                        FVector::CrossProduct(
                            Output.Vertices[I1] -
                                Output.Vertices[I0],
                            Output.Vertices[I2] -
                                Output.Vertices[I0]);

                    if (!FaceNormal.Normalize())
                    {
                        continue;
                    }

                    const FVector P =
                        (
                            Output.Vertices[I0] +
                            Output.Vertices[I1] +
                            Output.Vertices[I2]
                            ) / 3.0f;

                    const FVector Gradient =
                        ComputeGradient(
                            Densities,
                            Size,
                            P / Input.VoxelSize);

                    /*
                     * Unreal's front-face winding is opposite to the
                     * conventional mathematical cross-product winding.
                     *
                     * The density gradient points toward solid terrain,
                     * while the rendered surface must face toward air.
                     * Therefore the mathematical face normal should point
                     * along the density gradient so the reversed UE winding
                     * faces outward.
                     */
                    if (FVector::DotProduct(
                            FaceNormal,
                            Gradient) < 0.0f)
                    {
                        Output.Triangles.Add(I0);
                        Output.Triangles.Add(I2);
                        Output.Triangles.Add(I1);
                    }
                    else
                    {
                        Output.Triangles.Add(I0);
                        Output.Triangles.Add(I1);
                        Output.Triangles.Add(I2);
                    }
                }
            }
        }
    }
}
