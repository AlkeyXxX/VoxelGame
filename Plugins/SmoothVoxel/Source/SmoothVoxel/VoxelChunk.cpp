
#include "VoxelChunk.h"
#include "VoxelWorld.h"
#include "VoxelMarchingCubesMesher.h"

#include "ProceduralMeshComponent.h"
#include "Async/Async.h"


AVoxelChunk::AVoxelChunk()
{
    PrimaryActorTick.bCanEverTick = false;


    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(
        TEXT("VoxelMesh"));

    RootComponent = Mesh;


    /*
     * Чанк редактируется во время игры,
     * поэтому Movable безопаснее для runtime rebuild.
     */
    Mesh->SetMobility(EComponentMobility::Movable);


    /*
     * Асинхронное приготовление collision.
     */
    Mesh->bUseAsyncCooking = true;
}


void AVoxelChunk::BeginPlay()
{
    Super::BeginPlay();
}


void AVoxelChunk::InitializeChunk(
    AVoxelWorld* InWorld,
    const FIntVector& InChunkCoord)
{
    World = InWorld;
    ChunkCoord = InChunkCoord;


    const int32 BlockCount =
        ChunkSize * ChunkSize * ChunkSize;

    Blocks.SetNumZeroed(BlockCount);
    BaseBlocks.SetNumZeroed(BlockCount);
    Biomes.SetNumZeroed(BlockCount);
    ModificationFlags.SetNumZeroed(BlockCount);


    if (Material)
    {
        Mesh->SetMaterial(0, Material);
    }

    if (WaterMaterial)
    {
        Mesh->SetMaterial(1, WaterMaterial);
    }
}


void AVoxelChunk::SetVoxelMaterial(
    UMaterialInterface* InMaterial)
{
    Material = InMaterial;

    if (Mesh)
    {
        Mesh->SetMaterial(0, Material);
    }
}


void AVoxelChunk::SetWaterMaterial(
    UMaterialInterface* InMaterial)
{
    WaterMaterial = InMaterial;

    if (Mesh)
    {
        Mesh->SetMaterial(1, WaterMaterial);
    }
}


int32 AVoxelChunk::BlockIndex(
    int32 X,
    int32 Y,
    int32 Z) const
{
    return X +
        Y * ChunkSize +
        Z * ChunkSize * ChunkSize;
}


uint8 AVoxelChunk::GetBlock(
    int32 X,
    int32 Y,
    int32 Z) const
{
    if (X < 0 || X >= ChunkSize ||
        Y < 0 || Y >= ChunkSize ||
        Z < 0 || Z >= ChunkSize)
    {
        return uint8(EVoxelBlock::Air);
    }

    return Blocks[
        BlockIndex(X, Y, Z)
    ];
}


uint8 AVoxelChunk::GetTerrainBlock(
    int32 X,
    int32 Y,
    int32 Z) const
{
    if (X < 0 || X >= ChunkSize ||
        Y < 0 || Y >= ChunkSize ||
        Z < 0 || Z >= ChunkSize)
    {
        return uint8(EVoxelBlock::Air);
    }

    const int32 Index = BlockIndex(X, Y, Z);

    if (ModificationFlags.IsValidIndex(Index) &&
        ModificationFlags[Index] != 0)
    {
        return uint8(EVoxelBlock::Air);
    }

    return BaseBlocks[Index];
}


void AVoxelChunk::SetBiome(
    int32 X,
    int32 Y,
    int32 Z,
    uint8 Biome)
{
    if (X < 0 || X >= ChunkSize ||
        Y < 0 || Y >= ChunkSize ||
        Z < 0 || Z >= ChunkSize)
    {
        return;
    }

    Biomes[BlockIndex(X, Y, Z)] = Biome;
}


void AVoxelChunk::SetBlock(
    int32 X,
    int32 Y,
    int32 Z,
    uint8 Block)
{
    if (X < 0 || X >= ChunkSize ||
        Y < 0 || Y >= ChunkSize ||
        Z < 0 || Z >= ChunkSize)
    {
        return;
    }


    Blocks[
        BlockIndex(X, Y, Z)
    ] = Block;
}


void AVoxelChunk::RemoveBlock(
    int32 X,
    int32 Y,
    int32 Z)
{
    SetBlock(
        X,
        Y,
        Z,
        uint8(EVoxelBlock::Air));
}


void AVoxelChunk::CopyBlockData(
    TArray<uint8>& OutData) const
{
    OutData = Blocks;
}


void AVoxelChunk::CopyBiomeData(
    TArray<uint8>& OutData) const
{
    OutData = Biomes;
}


void AVoxelChunk::CopyModificationFlags(
    TArray<uint8>& OutData) const
{
    OutData = ModificationFlags;
}


void CopyStructureFace(
    const TArray<uint8>& Flags,
    TArray<uint8>& OutData,
    int32 Size,
    int32 Axis,
    bool bPlus)
{
    const int32 Count = Size * Size;
    OutData.SetNumUninitialized(Count);

    for (int32 B = 0; B < Size; ++B)
    {
        for (int32 A = 0; A < Size; ++A)
        {
            int32 X = A;
            int32 Y = B;
            int32 Z = 0;

            if (Axis == 0)
            {
                X = bPlus ? Size - 1 : 0;
                Y = A;
                Z = B;
            }
            else if (Axis == 1)
            {
                X = A;
                Y = bPlus ? Size - 1 : 0;
                Z = B;
            }
            else
            {
                X = A;
                Y = B;
                Z = bPlus ? Size - 1 : 0;
            }

            OutData[A + B * Size] =
                Flags[X + Y * Size + Z * Size * Size];
        }
    }
}


void AVoxelChunk::SetBaseBlock(
    int32 X,
    int32 Y,
    int32 Z,
    uint8 Block)
{
    if (X < 0 || X >= ChunkSize ||
        Y < 0 || Y >= ChunkSize ||
        Z < 0 || Z >= ChunkSize)
    {
        return;
    }

    BaseBlocks[BlockIndex(X, Y, Z)] = Block;
}


void AVoxelChunk::SetModificationFlag(
    int32 X,
    int32 Y,
    int32 Z,
    bool bModified)
{
    if (X < 0 || X >= ChunkSize ||
        Y < 0 || Y >= ChunkSize ||
        Z < 0 || Z >= ChunkSize)
    {
        return;
    }

    ModificationFlags[BlockIndex(X, Y, Z)] =
        bModified ? 1 : 0;
}


void AVoxelChunk::ClearModificationFlags()
{
    ModificationFlags.SetNumZeroed(
        ChunkSize * ChunkSize * ChunkSize);
}


void AVoxelChunk::CopyXMinus(
    TArray<uint8>& OutData) const
{
    const int32 Count =
        ChunkSize * ChunkSize;

    OutData.SetNumUninitialized(Count);


    for (int32 Z = 0; Z < ChunkSize; ++Z)
    {
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            OutData[
                Y + Z * ChunkSize
            ] =
                GetBlock(
                    0,
                    Y,
                    Z);
        }
    }
}


void AVoxelChunk::CopyXPlus(
    TArray<uint8>& OutData) const
{
    const int32 Count =
        ChunkSize * ChunkSize;

    OutData.SetNumUninitialized(Count);


    const int32 X = ChunkSize - 1;


    for (int32 Z = 0; Z < ChunkSize; ++Z)
    {
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            OutData[
                Y + Z * ChunkSize
            ] =
                GetBlock(
                    X,
                    Y,
                    Z);
        }
    }
}


void AVoxelChunk::CopyYMinus(
    TArray<uint8>& OutData) const
{
    const int32 Count =
        ChunkSize * ChunkSize;

    OutData.SetNumUninitialized(Count);


    for (int32 Z = 0; Z < ChunkSize; ++Z)
    {
        for (int32 X = 0; X < ChunkSize; ++X)
        {
            OutData[
                X + Z * ChunkSize
            ] =
                GetBlock(
                    X,
                    0,
                    Z);
        }
    }
}


void AVoxelChunk::CopyYPlus(
    TArray<uint8>& OutData) const
{
    const int32 Count =
        ChunkSize * ChunkSize;

    OutData.SetNumUninitialized(Count);


    const int32 Y = ChunkSize - 1;


    for (int32 Z = 0; Z < ChunkSize; ++Z)
    {
        for (int32 X = 0; X < ChunkSize; ++X)
        {
            OutData[
                X + Z * ChunkSize
            ] =
                GetBlock(
                    X,
                    Y,
                    Z);
        }
    }
}


void AVoxelChunk::CopyZMinus(
    TArray<uint8>& OutData) const
{
    const int32 Count =
        ChunkSize * ChunkSize;

    OutData.SetNumUninitialized(Count);


    for (int32 Y = 0; Y < ChunkSize; ++Y)
    {
        for (int32 X = 0; X < ChunkSize; ++X)
        {
            OutData[
                X + Y * ChunkSize
            ] =
                GetBlock(
                    X,
                    Y,
                    0);
        }
    }
}


void AVoxelChunk::CopyZPlus(
    TArray<uint8>& OutData) const
{
    const int32 Count =
        ChunkSize * ChunkSize;

    OutData.SetNumUninitialized(Count);


    const int32 Z = ChunkSize - 1;


    for (int32 Y = 0; Y < ChunkSize; ++Y)
    {
        for (int32 X = 0; X < ChunkSize; ++X)
        {
            OutData[
                X + Y * ChunkSize
            ] =
                GetBlock(
                    X,
                    Y,
                    Z);
        }
    }
}


/*
 * Запускает построение mesh в worker thread.
 *
 * НИКАКИХ UObject внутри worker thread мы не используем.
 *
 * Worker работает только с копиями TArray.
 */
void AVoxelChunk::RebuildMesh()
{
    ++MeshGenerationVersion;

    const uint32 LocalVersion =
        MeshGenerationVersion;

    bLOD = false;
    CurrentLODLevel = 0;

    FVoxelMeshBuildInput CubicInput;

    CubicInput.Size = ChunkSize;
    CubicInput.VoxelSize = VoxelSize;
    CubicInput.WorldOrigin = GetActorLocation();

    CopyBlockData(CubicInput.Blocks);
    CopyBiomeData(CubicInput.Biomes);
    CopyModificationFlags(CubicInput.StructureFlags);

    CubicInput.Neighbors.Init(ChunkSize);

    FVoxelMarchingCubesBuildInput SmoothInput;

    SmoothInput.Init(ChunkSize);
    SmoothInput.VoxelSize = VoxelSize;

    if (World)
    {
        World->BuildNeighborData(
            ChunkCoord,
            CubicInput.Neighbors);

        World->BuildMarchingCubesData(
            ChunkCoord,
            SmoothInput);
    }

    TWeakObjectPtr<AVoxelChunk> WeakThis(this);

    Async(
        EAsyncExecution::ThreadPool,

        [
            WeakThis,
            CubicInput = MoveTemp(CubicInput),
            SmoothInput = MoveTemp(SmoothInput),
            LocalVersion
        ]() mutable
        {
            FVoxelMeshBuildOutput Output;
            FVoxelMeshBuildOutput CubicOutput;

            FVoxelMarchingCubesMesher::Build(
                SmoothInput,
                Output);

            FVoxelMesher::Build(
                CubicInput,
                CubicOutput);

            const int32 VertexOffset =
                Output.Vertices.Num();

            Output.Vertices.Append(
                CubicOutput.Vertices);

            for (const int32 Triangle :
                CubicOutput.Triangles)
            {
                Output.Triangles.Add(
                    Triangle + VertexOffset);
            }

            Output.Normals.Append(
                CubicOutput.Normals);

            Output.UV0.Append(
                CubicOutput.UV0);

            Output.VertexColors.Append(
                CubicOutput.VertexColors);

            Output.WaterVertices =
                MoveTemp(
                    CubicOutput.WaterVertices);

            Output.WaterTriangles =
                MoveTemp(
                    CubicOutput.WaterTriangles);

            Output.WaterNormals =
                MoveTemp(
                    CubicOutput.WaterNormals);

            Output.WaterUV0 =
                MoveTemp(
                    CubicOutput.WaterUV0);

            Output.WaterVertexColors =
                MoveTemp(
                    CubicOutput.WaterVertexColors);

            AsyncTask(
                ENamedThreads::GameThread,

                [
                    WeakThis,
                    Output = MoveTemp(Output),
                    LocalVersion
                ]() mutable
                {
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    WeakThis->ApplyMesh(
                        MoveTemp(Output),
                        LocalVersion,
                        true);
                });
        });
}


/*
 * LOD строится из тех же данных чанка, но с уменьшенной дискретизацией.
 * Это сохраняет форму мира и не требует отдельного генератора.
 */
void AVoxelChunk::RebuildLODMesh(int32 DownsampleFactor)
{
    DownsampleFactor = FMath::Max(DownsampleFactor, 2);

    ++MeshGenerationVersion;

    const uint32 LocalVersion =
        MeshGenerationVersion;

    bLOD = true;
    CurrentLODLevel =
        DownsampleFactor == 2 ? 1 : 2;

    const int32 SourceSize = ChunkSize;
    const int32 LODSize =
        FMath::Max(1, SourceSize / DownsampleFactor);

    const int32 SourceSide = SourceSize + 2;
    const float EffectiveVoxelSize =
        VoxelSize * DownsampleFactor;

    /*
     * Получаем тот же expanded snapshot, который используется
     * обычным smooth Marching Cubes. Это важно для границ чанка.
     */
    FVoxelMarchingCubesBuildInput SourceSmoothInput;
    SourceSmoothInput.Init(SourceSize);
    SourceSmoothInput.VoxelSize = VoxelSize;

    if (World)
    {
        World->BuildMarchingCubesData(
            ChunkCoord,
            SourceSmoothInput);
    }

    TWeakObjectPtr<AVoxelChunk> WeakThis(this);

    Async(
        EAsyncExecution::ThreadPool,

        [
            WeakThis,
            SourceSmoothInput = MoveTemp(SourceSmoothInput),
            SourceSize,
            SourceSide,
            LODSize,
            DownsampleFactor,
            EffectiveVoxelSize,
            LocalVersion
        ]() mutable
        {
            FVoxelMarchingCubesBuildInput SmoothInput;
            SmoothInput.Init(LODSize);
            SmoothInput.VoxelSize = EffectiveVoxelSize;

            /*
             * Coarse samples берутся из исходного smooth field.
             * В отличие от старого варианта, результат снова проходит
             * через Marching Cubes, поэтому дальняя поверхность остаётся
             * сглаженной.
             */
            auto SourceIndex =
                [SourceSide](int32 X, int32 Y, int32 Z)
                {
                    return
                        (X + 1) +
                        (Y + 1) * SourceSide +
                        (Z + 1) * SourceSide * SourceSide;
                };

            auto IsSolid =
                [](uint8 Block)
                {
                    return IsVoxelSolid(
                        static_cast<EVoxelBlock>(Block));
                };

            auto Priority =
                [](uint8 Block)
                {
                    switch (static_cast<EVoxelBlock>(Block))
                    {
                    case EVoxelBlock::Grass: return 6;
                    case EVoxelBlock::Sand:  return 5;
                    case EVoxelBlock::Dirt:  return 4;
                    case EVoxelBlock::Stone: return 3;
                    case EVoxelBlock::Wood:  return 2;
                    case EVoxelBlock::Water: return 1;
                    default: return 0;
                    }
                };

            /*
             * Из каждого coarse voxel берём representative block
             * из исходного объёма factor^3. Solid-majority сохраняет
             * форму поверхности лучше, чем один центральный sample.
             *
             * Координаты LOD Input также включают support [-1, Size],
             * поэтому граница сохраняется.
             */
            for (int32 Z = -1; Z <= LODSize; ++Z)
            {
                for (int32 Y = -1; Y <= LODSize; ++Y)
                {
                    for (int32 X = -1; X <= LODSize; ++X)
                    {
                        int32 SolidCount = 0;
                        int32 TotalCount = 0;

                        uint8 BestBlock =
                            uint8(EVoxelBlock::Air);

                        int32 BestPriority = 0;

                        const int32 BaseX =
                            X * DownsampleFactor;

                        const int32 BaseY =
                            Y * DownsampleFactor;

                        const int32 BaseZ =
                            Z * DownsampleFactor;

                        for (int32 OZ = 0; OZ < DownsampleFactor; ++OZ)
                        {
                            for (int32 OY = 0; OY < DownsampleFactor; ++OY)
                            {
                                for (int32 OX = 0; OX < DownsampleFactor; ++OX)
                                {
                                    const int32 SX =
                                        FMath::Clamp(
                                            BaseX + OX,
                                            -1,
                                            SourceSize);

                                    const int32 SY =
                                        FMath::Clamp(
                                            BaseY + OY,
                                            -1,
                                            SourceSize);

                                    const int32 SZ =
                                        FMath::Clamp(
                                            BaseZ + OZ,
                                            -1,
                                            SourceSize);

                                    const uint8 Block =
                                        SourceSmoothInput.Blocks[
                                            SourceIndex(
                                                SX,
                                                SY,
                                                SZ)];

                                    ++TotalCount;

                                    if (IsSolid(Block))
                                    {
                                        ++SolidCount;
                                    }

                                    const int32 P =
                                        Priority(Block);

                                    if (P > BestPriority)
                                    {
                                        BestPriority = P;
                                        BestBlock = Block;
                                    }
                                }
                            }
                        }

                        /*
                         * Majority threshold prevents isolated tiny
                         * distant features from surviving at full strength.
                         */
                        const uint8 CoarseBlock =
                            SolidCount * 2 >= TotalCount
                                ? (BestBlock == uint8(EVoxelBlock::Water)
                                    ? uint8(EVoxelBlock::Stone)
                                    : BestBlock)
                                : uint8(EVoxelBlock::Air);

                        SmoothInput.Blocks[
                            (X + 1) +
                            (Y + 1) * (LODSize + 2) +
                            (Z + 1) * (LODSize + 2) * (LODSize + 2)] =
                            CoarseBlock;
                    }
                }
            }

            FVoxelMeshBuildOutput Output;

            FVoxelMarchingCubesMesher::Build(
                SmoothInput,
                Output);

            /*
             * Вода остаётся отдельной дешёвой cubic surface.
             * Для неё используем тот же coarse sampling, но только
             * water cells, чтобы не дублировать solid terrain.
             */
            FVoxelMeshBuildInput WaterInput;
            WaterInput.Size = LODSize;
            WaterInput.VoxelSize = EffectiveVoxelSize;
            WaterInput.WorldOrigin =
                FVector::ZeroVector;
            WaterInput.Neighbors.Init(LODSize);
            WaterInput.Blocks.SetNumZeroed(
                LODSize * LODSize * LODSize);

            const int32 LODSide = LODSize + 2;

            auto IsWaterAtSource =
                [&SourceSmoothInput, SourceSide, &SourceIndex](int32 SX, int32 SY, int32 SZ)
                {
                    const int32 CX =
                        FMath::Clamp(SX, -1, SourceSize);
                    const int32 CY =
                        FMath::Clamp(SY, -1, SourceSize);
                    const int32 CZ =
                        FMath::Clamp(SZ, -1, SourceSize);

                    return SourceSmoothInput.Blocks[
                        SourceIndex(CX, CY, CZ)] ==
                        uint8(EVoxelBlock::Water);
                };

            for (int32 Z = 0; Z < LODSize; ++Z)
            {
                for (int32 Y = 0; Y < LODSize; ++Y)
                {
                    for (int32 X = 0; X < LODSize; ++X)
                    {
                        bool bFoundWater = false;

                        const int32 BaseX =
                            X * DownsampleFactor;
                        const int32 BaseY =
                            Y * DownsampleFactor;
                        const int32 BaseZ =
                            Z * DownsampleFactor;

                        for (int32 OZ = 0; OZ < DownsampleFactor && !bFoundWater; ++OZ)
                        {
                            for (int32 OY = 0; OY < DownsampleFactor && !bFoundWater; ++OY)
                            {
                                for (int32 OX = 0; OX < DownsampleFactor; ++OX)
                                {
                                    if (IsWaterAtSource(
                                            BaseX + OX,
                                            BaseY + OY,
                                            BaseZ + OZ))
                                    {
                                        bFoundWater = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (bFoundWater)
                        {
                            WaterInput.Blocks[
                                X +
                                Y * LODSize +
                                Z * LODSize * LODSize] =
                                uint8(EVoxelBlock::Water);
                        }
                    }
                }
            }

            FVoxelMeshBuildOutput WaterOutput;

            FVoxelMesher::Build(
                WaterInput,
                WaterOutput);

            Output.WaterVertices =
                MoveTemp(WaterOutput.WaterVertices);

            Output.WaterTriangles =
                MoveTemp(WaterOutput.WaterTriangles);

            Output.WaterNormals =
                MoveTemp(WaterOutput.WaterNormals);

            Output.WaterUV0 =
                MoveTemp(WaterOutput.WaterUV0);

            Output.WaterVertexColors =
                MoveTemp(WaterOutput.WaterVertexColors);

            AsyncTask(
                ENamedThreads::GameThread,

                [
                    WeakThis,
                    Output = MoveTemp(Output),
                    LocalVersion
                ]() mutable
                {
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    WeakThis->ApplyMesh(
                        MoveTemp(Output),
                        LocalVersion,
                        false);
                });
        });
}




void AVoxelChunk::ApplyMesh(
    FVoxelMeshBuildOutput&& Output,
    uint32 Version,
    bool bEnableCollision)
{
    /*
     * Если пока строился этот mesh,
     * блок был изменён ещё раз,
     * результат устарел.
     */
    if (Version != MeshGenerationVersion)
    {
        return;
    }


    if (!Mesh)
    {
        return;
    }


    /*
     * Полностью удаляем старую секцию.
     */
    Mesh->ClearMeshSection(0);
    Mesh->ClearMeshSection(1);


    /*
     * Если чанк полностью пустой,
     * оставляем его без geometry.
     */
    if (Output.IsEmpty())
    {
        return;
    }


    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;


    /*
     * Tangents здесь не нужны —
     * Unreal сможет работать с normals.
     */
    Mesh->CreateMeshSection_LinearColor(
        0,

        Output.Vertices,
        Output.Triangles,
        Output.Normals,
        Output.UV0,

        TArray<FVector2D>(),
        TArray<FVector2D>(),
        TArray<FVector2D>(),

        Output.VertexColors,

        Tangents,

        bEnableCollision);


    if (Material)
    {
        Mesh->SetMaterial(
            0,
            Material);
    }

    if (Output.WaterVertices.Num() > 0 &&
        Output.WaterTriangles.Num() > 0)
    {
        Mesh->CreateMeshSection_LinearColor(
            1,
            Output.WaterVertices,
            Output.WaterTriangles,
            Output.WaterNormals,
            Output.WaterUV0,
            TArray<FVector2D>(),
            TArray<FVector2D>(),
            TArray<FVector2D>(),
            Output.WaterVertexColors,
            Tangents,
            false);

        if (WaterMaterial)
        {
            Mesh->SetMaterial(
                1,
                WaterMaterial);
        }
    }
}



void AVoxelChunk::CopyXMinusStructure(TArray<uint8>& OutData) const
{
    CopyStructureFace(ModificationFlags, OutData, ChunkSize, 0, false);
}
void AVoxelChunk::CopyXPlusStructure(TArray<uint8>& OutData) const
{
    CopyStructureFace(ModificationFlags, OutData, ChunkSize, 0, true);
}
void AVoxelChunk::CopyYMinusStructure(TArray<uint8>& OutData) const
{
    CopyStructureFace(ModificationFlags, OutData, ChunkSize, 1, false);
}
void AVoxelChunk::CopyYPlusStructure(TArray<uint8>& OutData) const
{
    CopyStructureFace(ModificationFlags, OutData, ChunkSize, 1, true);
}
void AVoxelChunk::CopyZMinusStructure(TArray<uint8>& OutData) const
{
    CopyStructureFace(ModificationFlags, OutData, ChunkSize, 2, false);
}
void AVoxelChunk::CopyZPlusStructure(TArray<uint8>& OutData) const
{
    CopyStructureFace(ModificationFlags, OutData, ChunkSize, 2, true);
}


void AVoxelChunk::SetLODLevel(int32 LODLevel)
{
    LODLevel = FMath::Clamp(LODLevel, 0, 2);

    if (CurrentLODLevel == LODLevel)
    {
        return;
    }

    if (LODLevel == 0)
    {
        RebuildMesh();
    }
    else if (LODLevel == 1)
    {
        RebuildLODMesh(2);
    }
    else
    {
        RebuildLODMesh(4);
    }
}
