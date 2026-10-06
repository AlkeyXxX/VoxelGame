
#include "VoxelWorld.h"
#include "VoxelChunk.h"
#include "VoxelWorldGenerator.h"

#include "Engine/World.h"
#include "Engine/Engine.h"

#include "GameFramework/PlayerController.h"

#include "Kismet/GameplayStatics.h"


namespace
{
    /*
     * Floor division для отрицательных координат.
     *
     * Обычный C++ int division обрезает к нулю,
     * а нам нужно математическое floor division.
     */
    FORCEINLINE int32 FastFloorDiv(
        int32 A,
        int32 B)
    {
        check(B > 0);

        if (A >= 0)
        {
            return A / B;
        }

        return -(
            ((-A) + B - 1) / B
        );
    }


    /*
     * Положительный modulo.
     */
    FORCEINLINE int32 PositiveModulo(
        int32 A,
        int32 B)
    {
        check(B > 0);

        const int32 Result = A % B;

        return Result < 0
            ? Result + B
            : Result;
    }


    /*
     * Получить ray из центра экрана.
     *
     * Это правильный вариант для UE 4.27.
     */
    bool GetCenterScreenRay(
        APlayerController* PC,
        FVector& OutStart,
        FVector& OutDirection)
    {
        if (!PC)
        {
            return false;
        }


        int32 ViewportSizeX = 0;
        int32 ViewportSizeY = 0;


        PC->GetViewportSize(
            ViewportSizeX,
            ViewportSizeY);


        if (ViewportSizeX <= 0 ||
            ViewportSizeY <= 0)
        {
            return false;
        }


        const float ScreenX =
            static_cast<float>(ViewportSizeX) * 0.5f;

        const float ScreenY =
            static_cast<float>(ViewportSizeY) * 0.5f;


        return PC->DeprojectScreenPositionToWorld(
            ScreenX,
            ScreenY,
            OutStart,
            OutDirection);
    }
}


AVoxelWorld::AVoxelWorld()
{
    PrimaryActorTick.bCanEverTick = false;
}


void AVoxelWorld::BeginPlay()
{
    Super::BeginPlay();

    GenerateWorld();
}


void AVoxelWorld::ConfigureWorldGenerator()
{
    FVoxelWorldGenerationSettings Settings;

    Settings.Seed = Seed;
    Settings.BaseHeight = BaseHeight;
    Settings.HeightVariation = HeightVariation;
    Settings.NoiseScale = NoiseScale;
    Settings.TemperatureScale = TemperatureScale;
    Settings.MoistureScale = MoistureScale;

    WorldGenerator.Configure(Settings);
}


void AVoxelWorld::GenerateWorld()
{
    UWorld* World = GetWorld();

    if (!World)
    {
        return;
    }


    /*
     * Перед генерацией синхронизируем параметры
     * AVoxelWorld с отдельным world generator.
     */
    ConfigureWorldGenerator();


    /*
     * На случай повторного вызова GenerateWorld().
     */
    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            Pair.Value->Destroy();
        }
    }

    Chunks.Empty();


    /*
     * Сначала создаём ВСЕ chunks.
     *
     * Это важно, потому что при генерации mesh
     * сосед уже должен существовать.
     */
    for (int32 Z = 0; Z < WorldSizeZ; ++Z)
    {
        for (int32 Y = 0; Y < WorldSizeY; ++Y)
        {
            for (int32 X = 0; X < WorldSizeX; ++X)
            {
                const FIntVector Coord(
                    X,
                    Y,
                    Z);


                const FVector Location =
                    GetActorLocation() +
                    FVector(
                        X * ChunkSize * VoxelSize,
                        Y * ChunkSize * VoxelSize,
                        Z * ChunkSize * VoxelSize);


                FActorSpawnParameters SpawnParams;
                SpawnParams.Owner = this;
                SpawnParams.SpawnCollisionHandlingOverride =
                    ESpawnActorCollisionHandlingMethod::AlwaysSpawn;


                AVoxelChunk* Chunk =
                    World->SpawnActor<AVoxelChunk>(
                        AVoxelChunk::StaticClass(),
                        Location,
                        FRotator::ZeroRotator,
                        SpawnParams);


                if (!Chunk)
                {
                    continue;
                }


                Chunk->SetActorLocation(Location);


                /*
                 * Передаём world + coordinate.
                 */
                Chunk->InitializeChunk(
                    this,
                    Coord);


                /*
                 * Материал мира передаём chunk.
                 */
                Chunk->SetVoxelMaterial(
                    Material);


                Chunks.Add(
                    Coord,
                    Chunk);
            }
        }
    }


    /*
     * Теперь, когда все chunks существуют,
     * генерируем блоки.
     */
    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            GenerateChunkBlocks(
                Pair.Value);
        }
    }


    /*
     * И только после этого строим mesh.
     */
    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            Pair.Value->RebuildMesh();
        }
    }
}


/*
 * Генерация terrain.
 */
void AVoxelWorld::GenerateChunkBlocks(
    AVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        return;
    }


    const FIntVector ChunkCoord =
        Chunk->GetChunkCoord();


    for (int32 Z = 0; Z < ChunkSize; ++Z)
    {
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            for (int32 X = 0; X < ChunkSize; ++X)
            {
                /*
                 * Глобальная координата блока.
                 */
                const int32 WorldX =
                    ChunkCoord.X * ChunkSize + X;

                const int32 WorldY =
                    ChunkCoord.Y * ChunkSize + Y;

                const int32 WorldZ =
                    ChunkCoord.Z * ChunkSize + Z;


                const int32 Height =
                    WorldGenerator.GetSurfaceHeight(
                        WorldX,
                        WorldY);

                const EVoxelBiome Biome =
                    WorldGenerator.GetBiome(
                        WorldX,
                        WorldY,
                        Height);

                uint8 Block =
                    uint8(EVoxelBlock::Air);

                if (WorldZ > Height)
                {
                    if (WorldZ <= SeaLevel)
                    {
                        Block =
                            uint8(EVoxelBlock::Water);
                    }
                    else
                    {
                        Block =
                            uint8(EVoxelBlock::Air);
                    }
                }
                else if (WorldZ == Height)
                {
                    const bool bBeach =
                        Height < SeaLevel &&
                        Height >= SeaLevel - BeachWidth;

                    if (bBeach)
                    {
                        Block =
                            uint8(EVoxelBlock::Sand);
                    }
                    else
                    {
                        switch (Biome)
                        {
                        case EVoxelBiome::Desert:
                            Block =
                                uint8(EVoxelBlock::Sand);
                            break;

                        case EVoxelBiome::Mountain:
                            Block =
                                uint8(EVoxelBlock::Stone);
                            break;

                        case EVoxelBiome::Forest:
                        case EVoxelBiome::Plains:
                        default:
                            Block =
                                uint8(EVoxelBlock::Grass);
                            break;
                        }
                    }
                }
                else if (Biome == EVoxelBiome::Desert &&
                         WorldZ >= Height - 3)
                {
                    Block =
                        uint8(EVoxelBlock::Sand);
                }
                else if (WorldZ >= Height - 3)
                {
                    Block =
                        uint8(EVoxelBlock::Dirt);
                }
                else
                {
                    Block =
                        uint8(EVoxelBlock::Stone);
                }


                Chunk->SetBiome(
                    X,
                    Y,
                    Z,
                    uint8(Biome));

                Chunk->SetBlock(
                    X,
                    Y,
                    Z,
                    Block);
            }
        }
    }
}


/*
 * Получение чанка.
 *
 * ВАЖНО:
 * метод НЕ const.
 */
AVoxelChunk* AVoxelWorld::GetChunk(
    const FIntVector& ChunkCoord)
{
    AVoxelChunk* const* Found =
        Chunks.Find(ChunkCoord);


    return Found
        ? *Found
        : nullptr;
}


/*
 * Создаёт snapshots шести соседних сторон.
 */
void AVoxelWorld::BuildNeighborData(
    const FIntVector& ChunkCoord,
    FVoxelNeighborData& OutData) const
{
    OutData.Init(ChunkSize);


    /*
     * X-
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(-1, 0, 0)))
    {
        Neighbor->CopyXPlus(
            OutData.XMinus);
    }


    /*
     * X+
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(1, 0, 0)))
    {
        Neighbor->CopyXMinus(
            OutData.XPlus);
    }


    /*
     * Y-
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, -1, 0)))
    {
        Neighbor->CopyYPlus(
            OutData.YMinus);
    }


    /*
     * Y+
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, 1, 0)))
    {
        Neighbor->CopyYMinus(
            OutData.YPlus);
    }


    /*
     * Z-
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, 0, -1)))
    {
        Neighbor->CopyZPlus(
            OutData.ZMinus);
    }


    /*
     * Z+
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, 0, 1)))
    {
        Neighbor->CopyZMinus(
            OutData.ZPlus);
    }
}


/*
 * World position -> world block.
 */
bool AVoxelWorld::WorldToBlock(
    const FVector& WorldPosition,
    FIntVector& OutBlock) const
{
    const FVector LocalPosition =
        WorldPosition - GetActorLocation();


    OutBlock.X =
        FMath::FloorToInt(
            LocalPosition.X / VoxelSize);


    OutBlock.Y =
        FMath::FloorToInt(
            LocalPosition.Y / VoxelSize);


    OutBlock.Z =
        FMath::FloorToInt(
            LocalPosition.Z / VoxelSize);


    return true;
}


/*
 * World block -> chunk coordinate.
 */
FIntVector AVoxelWorld::WorldBlockToChunk(
    const FIntVector& WorldBlock) const
{
    return FIntVector(
        FastFloorDiv(
            WorldBlock.X,
            ChunkSize),

        FastFloorDiv(
            WorldBlock.Y,
            ChunkSize),

        FastFloorDiv(
            WorldBlock.Z,
            ChunkSize));
}


/*
 * World block -> local block.
 */
FIntVector AVoxelWorld::WorldBlockToLocal(
    const FIntVector& WorldBlock) const
{
    return FIntVector(
        PositiveModulo(
            WorldBlock.X,
            ChunkSize),

        PositiveModulo(
            WorldBlock.Y,
            ChunkSize),

        PositiveModulo(
            WorldBlock.Z,
            ChunkSize));
}


/*
 * Изменение блока.
 */
void AVoxelWorld::SetBlockInternal(
    const FIntVector& WorldBlock,
    uint8 Block)
{
    const FIntVector ChunkCoord =
        WorldBlockToChunk(
            WorldBlock);


    const FIntVector LocalBlock =
        WorldBlockToLocal(
            WorldBlock);


    AVoxelChunk* Chunk =
        GetChunk(ChunkCoord);


    if (!Chunk)
    {
        return;
    }


    Chunk->SetBlock(
        LocalBlock.X,
        LocalBlock.Y,
        LocalBlock.Z,
        Block);


    /*
     * Перестраиваем изменённый chunk
     * и его соседей.
     */
    RebuildChunkAndNeighbors(
        ChunkCoord);
}


/*
 * Удаление блока по World Position.
 */
void AVoxelWorld::RemoveBlockAtWorld(
    const FVector& WorldPosition)
{
    FIntVector WorldBlock;


    if (!WorldToBlock(
        WorldPosition,
        WorldBlock))
    {
        return;
    }


    SetBlockInternal(
        WorldBlock,
        uint8(EVoxelBlock::Air));
}


/*
 * Установка блока по World Position.
 */
void AVoxelWorld::SetBlockAtWorld(
    const FVector& WorldPosition,
    EVoxelBlock Block)
{
    FIntVector WorldBlock;


    if (!WorldToBlock(
        WorldPosition,
        WorldBlock))
    {
        return;
    }


    SetBlockInternal(
        WorldBlock,
        uint8(Block));
}


/*
 * Перестраиваем изменённый chunk
 * плюс шесть соседей.
 */
void AVoxelWorld::RebuildChunkAndNeighbors(
    const FIntVector& ChunkCoord)
{
    static const FIntVector Directions[] =
    {
        FIntVector(0, 0, 0),

        FIntVector(-1, 0, 0),
        FIntVector(1, 0, 0),

        FIntVector(0, -1, 0),
        FIntVector(0, 1, 0),

        FIntVector(0, 0, -1),
        FIntVector(0, 0, 1)
    };


    for (const FIntVector& Direction : Directions)
    {
        const FIntVector TargetCoord =
            ChunkCoord + Direction;


        AVoxelChunk* Chunk =
            GetChunk(TargetCoord);


        if (Chunk)
        {
            Chunk->RebuildMesh();
        }
    }
}


/*
 * Разрушение блока через ray из центра экрана.
 */
bool AVoxelWorld::BreakBlockByRay()
{
    APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);


    if (!PC)
    {
        return false;
    }


    FVector Start;
    FVector Direction;


    if (!GetCenterScreenRay(
        PC,
        Start,
        Direction))
    {
        return false;
    }


    const FVector End =
        Start +
        Direction * InteractionDistance;


    FHitResult Hit;


    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(VoxelBreak),
        true);


    Params.AddIgnoredActor(this);


    const bool bHit =
        GetWorld()->LineTraceSingleByChannel(
            Hit,
            Start,
            End,
            ECC_Visibility,
            Params);


    if (!bHit)
    {
        return false;
    }


    AVoxelChunk* HitChunk =
        Cast<AVoxelChunk>(
            Hit.GetActor());


    if (!HitChunk)
    {
        return false;
    }


    /*
     * Чуть двигаемся внутрь блока,
     * чтобы получить именно тот блок,
     * по которому кликнули.
     */
    const FVector BlockPoint =
        Hit.ImpactPoint -
        Hit.ImpactNormal * 0.01f;


    FIntVector WorldBlock;


    if (!WorldToBlock(
        BlockPoint,
        WorldBlock))
    {
        return false;
    }


    SetBlockInternal(
        WorldBlock,
        uint8(EVoxelBlock::Air));


    return true;
}


/*
 * Установка блока рядом с поверхностью.
 */
bool AVoxelWorld::PlaceBlockByRay()
{
    APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);


    if (!PC)
    {
        return false;
    }


    FVector Start;
    FVector Direction;


    if (!GetCenterScreenRay(
        PC,
        Start,
        Direction))
    {
        return false;
    }


    const FVector End =
        Start +
        Direction * InteractionDistance;


    FHitResult Hit;


    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(VoxelPlace),
        true);


    Params.AddIgnoredActor(this);


    const bool bHit =
        GetWorld()->LineTraceSingleByChannel(
            Hit,
            Start,
            End,
            ECC_Visibility,
            Params);


    if (!bHit)
    {
        return false;
    }


    AVoxelChunk* HitChunk =
        Cast<AVoxelChunk>(
            Hit.GetActor());


    if (!HitChunk)
    {
        return false;
    }


    /*
     * Двигаемся наружу от грани.
     *
     * 0.51 * VoxelSize гарантированно
     * переводит точку в соседнюю клетку.
     */
    const FVector PlacePoint =
        Hit.ImpactPoint +
        Hit.ImpactNormal *
        (VoxelSize * 0.51f);


    FIntVector WorldBlock;


    if (!WorldToBlock(
        PlacePoint,
        WorldBlock))
    {
        return false;
    }


    /*
     * Пока ставим Dirt.
     *
     * Позже сюда подключим выбранный
     * игроком тип блока.
     */
    SetBlockInternal(
        WorldBlock,
        uint8(EVoxelBlock::Dirt));


    return true;
}

