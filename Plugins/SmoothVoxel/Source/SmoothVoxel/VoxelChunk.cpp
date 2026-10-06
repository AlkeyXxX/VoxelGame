
#include "VoxelChunk.h"
#include "VoxelWorld.h"

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
    Biomes.SetNumZeroed(BlockCount);


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
    /*
     * Новая версия mesh.
     */
    ++MeshGenerationVersion;

    const uint32 LocalVersion =
        MeshGenerationVersion;


    /*
     * Полностью собираем snapshot на Game Thread.
     */
    FVoxelMeshBuildInput Input;

    Input.Size = ChunkSize;
    Input.VoxelSize = VoxelSize;


    CopyBlockData(Input.Blocks);
    CopyBiomeData(Input.Biomes);

    Input.Neighbors.Init(ChunkSize);


    if (World)
    {
        World->BuildNeighborData(
            ChunkCoord,
            Input.Neighbors);
    }


    /*
     * Сохраняем weak pointer.
     *
     * Если chunk будет уничтожен,
     * worker ничего не применит.
     */
    TWeakObjectPtr<AVoxelChunk> WeakThis(this);


    /*
     * Само построение выполняется в ThreadPool.
     */
    Async(
        EAsyncExecution::ThreadPool,

        [
            WeakThis,
            Input = MoveTemp(Input),
            LocalVersion
        ]() mutable
        {
            FVoxelMeshBuildOutput Output;


            FVoxelMesher::Build(
                Input,
                Output);


            /*
             * CreateMeshSection нельзя выполнять
             * из worker thread.
             *
             * Возвращаемся на Game Thread.
             */
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
                        LocalVersion);
                });
        });
}


void AVoxelChunk::ApplyMesh(
    FVoxelMeshBuildOutput&& Output,
    uint32 Version)
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

        true);


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

