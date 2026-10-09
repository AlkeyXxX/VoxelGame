
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


void AVoxelChunk::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    if (MeshBuildCancellationToken.IsValid())
    {
        MeshBuildCancellationToken->AtomicSet(true);
    }

    Super::EndPlay(EndPlayReason);
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


void AVoxelChunk::SetGeneratedData(
    TArray<uint8>&& InBlocks,
    TArray<uint8>&& InBaseBlocks,
    TArray<uint8>&& InBiomes,
    TArray<uint8>&& InModificationFlags)
{
    const int32 ExpectedCount =
        ChunkSize * ChunkSize * ChunkSize;

    if (InBlocks.Num() != ExpectedCount ||
        InBaseBlocks.Num() != ExpectedCount ||
        InBiomes.Num() != ExpectedCount ||
        InModificationFlags.Num() != ExpectedCount)
    {
        ensureMsgf(false, TEXT("Invalid generated voxel data size."));
        return;
    }

    Blocks = MoveTemp(InBlocks);
    BaseBlocks = MoveTemp(InBaseBlocks);
    Biomes = MoveTemp(InBiomes);
    ModificationFlags = MoveTemp(InModificationFlags);
    ++DataGenerationVersion;
    bGeneratedDataReady = true;
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

    /*
     * Superseded mesh jobs are allowed to finish only until their next
     * cancellation check. They must never consume CPU building geometry
     * that is already known to be stale.
     */
    if (MeshBuildCancellationToken.IsValid())
    {
        MeshBuildCancellationToken->AtomicSet(true);
    }

    MeshBuildCancellationToken =
        MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);

    const TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> LocalCancellationToken =
        MeshBuildCancellationToken;

    const uint32 LocalVersion =
        MeshGenerationVersion;

    FVoxelMeshBuildInput CubicInput;

    CubicInput.Size = ChunkSize;
    CubicInput.VoxelSize = VoxelSize;
    CubicInput.WorldOrigin = GetActorLocation();
    CubicInput.CancellationToken = LocalCancellationToken;

    CopyBlockData(CubicInput.Blocks);
    CopyBiomeData(CubicInput.Biomes);
    CopyModificationFlags(CubicInput.StructureFlags);

    CubicInput.Neighbors.Init(ChunkSize);

    FVoxelMarchingCubesDataSnapshot SmoothSnapshot;

    if (World)
    {
        /*
         * Copy only immutable generator/settings and local edit deltas here.
         * Building the 34^3 Marching Cubes halo is intentionally deferred to
         * the worker thread instead of doing procedural noise work on the
         * Game Thread while the player is moving.
         */
        World->BuildNeighborData(
            ChunkCoord,
            CubicInput.Neighbors);

        World->CaptureMarchingCubesDataSnapshot(
            ChunkCoord,
            SmoothSnapshot);
    }

    SmoothSnapshot.CancellationToken =
        LocalCancellationToken;

    TWeakObjectPtr<AVoxelChunk> WeakThis(this);
    TWeakObjectPtr<AVoxelWorld> WeakWorld(World);

    Async(
        EAsyncExecution::ThreadPool,

        [
            WeakThis,
            WeakWorld,
            CubicInput = MoveTemp(CubicInput),
            SmoothSnapshot = MoveTemp(SmoothSnapshot),
            LocalCancellationToken,
            LocalVersion
        ]() mutable
        {
            FVoxelMeshBuildOutput Output;
            FVoxelMeshBuildOutput CubicOutput;
            FVoxelMarchingCubesBuildInput SmoothInput;

            SmoothSnapshot.Build(SmoothInput);

            if (static_cast<bool>(*LocalCancellationToken))
            {
                return;
            }

            FVoxelMarchingCubesMesher::Build(
                SmoothInput,
                Output);

            if (static_cast<bool>(*LocalCancellationToken))
            {
                return;
            }

            FVoxelMesher::Build(
                CubicInput,
                CubicOutput);

            if (static_cast<bool>(*LocalCancellationToken))
            {
                return;
            }

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
                    WeakWorld,
                    Output = MoveTemp(Output),
                    LocalCancellationToken,
                    LocalVersion
                ]() mutable
                {
                    AVoxelChunk* Chunk = WeakThis.Get();

                    if (!Chunk ||
                        static_cast<bool>(*LocalCancellationToken) ||
                        Chunk->GetMeshGenerationVersion() != LocalVersion)
                    {
                        return;
                    }

                    if (AVoxelWorld* WorldActor = WeakWorld.Get())
                    {
                        /*
                         * Mesh section creation can itself be expensive on
                         * the Game Thread. Queue completed geometry so only
                         * a bounded number of chunks upload per frame.
                         */
                        WorldActor->QueueChunkMeshResult(
                            Chunk,
                            MoveTemp(Output),
                            LocalVersion);
                    }
                });
        });
}


/*
 * LOD строится из тех же данных чанка, но с уменьшенной дискретизацией.
 * Это сохраняет форму мира и не требует отдельного генератора.
 */
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


