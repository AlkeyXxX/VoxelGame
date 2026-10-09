#include "VoxelMarchingCubesMesher.h"

#include "VoxelTypes.h"
#include "SmoothVoxelMCTables.h"

#include "Math/UnrealMathUtility.h"


namespace
{
    /*
     * Density samples are averages of eight binary voxel values, so every
     * sample is a multiple of 1/8. An exact 0.5 iso-level can put MC vertices
     * directly on density nodes; several edge caches then create coincident
     * vertices and degenerate triangles. The triangle cleanup may discard
     * those triangles and leave visible holes. A nearby non-sample threshold
     * keeps the same solid/air classification for all node values while
     * moving intersections a small, non-zero distance off the nodes.
     */
    constexpr float IsoLevel = 0.49f;

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
        /*
         * Evaluate the gradient of the trilinearly interpolated density
         * field at the actual surface position. Rounding Position to the
         * nearest sample node can give adjacent triangles unrelated normal
         * directions around dug blocks, which may flip their winding and
         * make otherwise valid faces disappear with back-face culling.
         */
        const float PX = FMath::Clamp(
            Position.X,
            0.0f,
            static_cast<float>(Size));

        const float PY = FMath::Clamp(
            Position.Y,
            0.0f,
            static_cast<float>(Size));

        const float PZ = FMath::Clamp(
            Position.Z,
            0.0f,
            static_cast<float>(Size));

        const int32 X0 = FMath::FloorToInt(PX);
        const int32 Y0 = FMath::FloorToInt(PY);
        const int32 Z0 = FMath::FloorToInt(PZ);

        const int32 X1 = FMath::Min(X0 + 1, Size + 1);
        const int32 Y1 = FMath::Min(Y0 + 1, Size + 1);
        const int32 Z1 = FMath::Min(Z0 + 1, Size + 1);

        const float TX = PX - static_cast<float>(X0);
        const float TY = PY - static_cast<float>(Y0);
        const float TZ = PZ - static_cast<float>(Z0);

        const float D000 = Densities[DensityIndex(X0, Y0, Z0, Size)];
        const float D100 = Densities[DensityIndex(X1, Y0, Z0, Size)];
        const float D010 = Densities[DensityIndex(X0, Y1, Z0, Size)];
        const float D110 = Densities[DensityIndex(X1, Y1, Z0, Size)];
        const float D001 = Densities[DensityIndex(X0, Y0, Z1, Size)];
        const float D101 = Densities[DensityIndex(X1, Y0, Z1, Size)];
        const float D011 = Densities[DensityIndex(X0, Y1, Z1, Size)];
        const float D111 = Densities[DensityIndex(X1, Y1, Z1, Size)];

        const float DX = FMath::Lerp(
            FMath::Lerp(D100 - D000, D110 - D010, TY),
            FMath::Lerp(D101 - D001, D111 - D011, TY),
            TZ);

        const float DY = FMath::Lerp(
            FMath::Lerp(D010 - D000, D110 - D100, TX),
            FMath::Lerp(D011 - D001, D111 - D101, TX),
            TZ);

        const float DZ = FMath::Lerp(
            FMath::Lerp(D001 - D000, D101 - D100, TX),
            FMath::Lerp(D011 - D010, D111 - D110, TX),
            TY);

        FVector Gradient(DX, DY, DZ);

        // A zero gradient must not force an arbitrary up-facing triangle
        // winding. The triangle table's order is preserved in that case.
        Gradient.Normalize();
        return Gradient;
    }
    int32 GetSurfaceMaterialPriority(uint8 Block)
    {
        switch (static_cast<EVoxelBlock>(Block))
        {
        case EVoxelBlock::Snow:
            return 7;
        case EVoxelBlock::Grass:
            return 6;
        case EVoxelBlock::Sandstone:
            return 5;
        case EVoxelBlock::Sand:
            return 4;
        case EVoxelBlock::Dirt:
            return 3;
        case EVoxelBlock::Stone:
            return 2;
        case EVoxelBlock::Wood:
            return 1;
        default:
            return 0;
        }
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


uint8 FVoxelMarchingCubesMesher::GetDensityBlock(
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

    const int32 Index =
        MarchingCubesBlockIndex(
            X,
            Y,
            Z,
            Input.Size);

    return Input.DensityBlocks.IsValidIndex(Index)
        ? Input.DensityBlocks[Index]
        : GetBlock(Input, X, Y, Z);
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
                        GetDensityBlock(
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

    case EVoxelBlock::Sandstone:
        /* Southwest-style warm red/tan rock. */
        return FLinearColor(0.72f, 0.38f, 0.22f, 1.0f);

    case EVoxelBlock::Snow:
        return FLinearColor(0.94f, 0.97f, 1.0f, 1.0f);

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

    /*
     * The MC vertex lies on the boundary between air and solid.
     * Search downward from that boundary so the surface material comes
     * from the actual solid layer instead of the air cell above it.
     * A small X/Y neighborhood also handles sloped surfaces cleanly.
     */
    for (int32 DZ = 0; DZ >= -2; --DZ)
    {
        for (int32 DY = -1; DY <= 1; ++DY)
        {
            for (int32 DX = -1; DX <= 1; ++DX)
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

                if (!bFound ||
                    SampleZ > BestZ ||
                    (SampleZ == BestZ &&
                     GetSurfaceMaterialPriority(Block) >
                         GetSurfaceMaterialPriority(BestBlock)))
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

    /*
     * Make a very shallow recess around exposed tops of placed blocks by
     * adjusting scalar density samples, not mesh vertices. The target value
     * places the usual IsoLevel crossing at 12/13 of the vertical edge,
     * i.e. 1/13 voxel below the block's top. Marching Cubes still builds all
     * triangles normally, so cave walls and ceilings are not deformed.
     */
    for (int32 Z = 1; Z <= Size; ++Z)
    {
        for (int32 Y = 0; Y <= Size; ++Y)
        {
            for (int32 X = 0; X <= Size; ++X)
            {
                const float DensityBelow =
                    Densities[
                        DensityIndex(
                            X,
                            Y,
                            Z - 1,
                            Size)];

                if (DensityBelow <= IsoLevel)
                {
                    continue;
                }

                const int32 CandidateX[2] = { X - 1, X };
                const int32 CandidateY[2] = { Y - 1, Y };
                bool bExposedPlacedTop = false;

                for (int32 CY = 0;
                     CY < 2 && !bExposedPlacedTop;
                     ++CY)
                {
                    for (int32 CX = 0; CX < 2; ++CX)
                    {
                        const int32 BlockX = CandidateX[CX];
                        const int32 BlockY = CandidateY[CY];
                        const int32 BlockZ = Z - 1;

                        const bool bTerrainBlock =
                            IsVoxelSolid(
                                GetBlock(
                                    Input,
                                    BlockX,
                                    BlockY,
                                    BlockZ));

                        const bool bDensityBlock =
                            IsVoxelSolid(
                                GetDensityBlock(
                                    Input,
                                    BlockX,
                                    BlockY,
                                    BlockZ));

                        const bool bTerrainAbove =
                            IsVoxelSolid(
                                GetBlock(
                                    Input,
                                    BlockX,
                                    BlockY,
                                    Z));

                        const bool bDensityAbove =
                            IsVoxelSolid(
                                GetDensityBlock(
                                    Input,
                                    BlockX,
                                    BlockY,
                                    Z));

                        /*
                         * A placed solid exists only in DensityBlocks, not
                         * the terrain-only render array. Require open space
                         * immediately above it so buried blocks stay buried.
                         */
                        if (!bTerrainBlock &&
                            bDensityBlock &&
                            !bTerrainAbove &&
                            !bDensityAbove)
                        {
                            bExposedPlacedTop = true;
                            break;
                        }
                    }
                }

                if (!bExposedPlacedTop)
                {
                    continue;
                }

                const float TargetDensity =
                    DensityBelow +
                    (IsoLevel - DensityBelow) * (13.0f / 12.0f);

                Densities[
                    DensityIndex(
                        X,
                        Y,
                        Z,
                        Size)] =
                    TargetDensity;
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
                /*
                 * A player-placed solid block is kept in DensityBlocks so
                 * surrounding terrain can meet its cubic side, but the cell
                 * itself is rendered only by the cubic mesher. This avoids
                 * duplicate surfaces/collision inside the placed block.
                 */
                if (!IsVoxelSolid(GetBlock(Input, X, Y, Z)) &&
                    IsVoxelSolid(GetDensityBlock(Input, X, Y, Z)))
                {
                    continue;
                }

                /*
                 * Do not skip whole cells above placed blocks here. A wall
                 * or cave-floor surface may pass through the same cell;
                 * suppressing the entire cell opens large holes.
                 */

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

                        FVector VertexNormal = -Gradient;
                        if (!VertexNormal.Normalize())
                        {
                            VertexNormal = FVector::UpVector;
                        }

                        Output.Normals.Add(VertexNormal);

                        const int32 MaterialBlockZ =
                            FMath::FloorToInt(
                                Position.Z);

                        const uint8 RepresentativeBlock =
                            GetRepresentativeBlock(
                                Input,
                                FMath::FloorToInt(Position.X),
                                FMath::FloorToInt(Position.Y),
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

                    const FVector Edge01 =
                        Output.Vertices[I1] - Output.Vertices[I0];
                    const FVector Edge12 =
                        Output.Vertices[I2] - Output.Vertices[I1];
                    const FVector Edge20 =
                        Output.Vertices[I0] - Output.Vertices[I2];

                    /*
                     * Density averaging around a dug voxel can leave an
                     * almost-zero-area MC triangle on an otherwise solid
                     * wall. Such slivers can look like razor-thin faces and
                     * are poor collision targets, so discard only triangles
                     * far below the scale of a voxel.
                     */
                    const float MinEdgeLength =
                        FMath::Max(Input.VoxelSize * 0.005f, 0.01f);
                    const float MinEdgeLengthSquared =
                        FMath::Square(MinEdgeLength);

                    if (Edge01.SizeSquared() < MinEdgeLengthSquared ||
                        Edge12.SizeSquared() < MinEdgeLengthSquared ||
                        Edge20.SizeSquared() < MinEdgeLengthSquared)
                    {
                        continue;
                    }

                    FVector FaceNormal =
                        FVector::CrossProduct(
                            Edge01,
                            Output.Vertices[I2] - Output.Vertices[I0]);

                    const float MinDoubleArea =
                        FMath::Square(Input.VoxelSize) * 0.001f;

                    if (FaceNormal.SizeSquared() <=
                            FMath::Square(MinDoubleArea) ||
                        !FaceNormal.Normalize())
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
