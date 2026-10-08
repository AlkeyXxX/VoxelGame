
#include "VoxelWorld.h"
#include "VoxelChunk.h"
#include "VoxelWorldGenerator.h"
#include "VoxelWorldSaveGame.h"
#include "VoxelBlockLibrary.h"
#include "VoxelInventoryComponent.h"

#include "Engine/World.h"
#include "Engine/Engine.h"

#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

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
    /*
     * Streaming проверяет позицию игрока каждый кадр,
     * но сама тяжёлая работа выполняется только
     * при смене чанка или пока есть недогруженные chunks.
     */
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
}


void AVoxelWorld::BeginPlay()
{
    Super::BeginPlay();

    GenerateWorld();
}


void AVoxelWorld::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    /*
     * Перед выходом из мира сохраняем накопленные изменения.
     * SaveGame содержит только delta-данные, поэтому для текущего
     * этапа это маленькая операция.
     */
    SaveWorld();

    Super::EndPlay(EndPlayReason);
}


void AVoxelWorld::Tick(
    float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    UpdateChunkStreaming();

    if (AutoSaveInterval > 0.0f)
    {
        TimeSinceLastAutoSave += DeltaSeconds;

        if (TimeSinceLastAutoSave >= AutoSaveInterval)
        {
            TimeSinceLastAutoSave = 0.0f;
            SaveWorld();
        }
    }
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
    /*
     * Синхронизируем параметры генератора.
     */
    ConfigureWorldGenerator();


    /*
     * Удаляем только chunks, которые сейчас загружены.
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
     * GenerateWorld() означает полную генерацию мира заново,
     * поэтому старые runtime-изменения очищаем.
     * При обычном streaming изменения сюда не попадают.
     */
    ModifiedBlocks.Empty();

    bStreamingInitialized = false;
    LastStreamingCenter = FIntVector::ZeroValue;


    /*
     * Дальше мир создаётся streaming-системой
     * вокруг игрока небольшими порциями.
     */
    UpdateChunkStreaming();
}


bool AVoxelWorld::GetStreamingCenterChunk(
    FIntVector& OutChunkCoord) const
{
    UWorld* World = GetWorld();

    if (!World)
    {
        return false;
    }


    APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            World,
            0);


    if (PlayerPawn)
    {
        FIntVector PlayerBlock;

        if (WorldToBlock(
            PlayerPawn->GetActorLocation(),
            PlayerBlock))
        {
            OutChunkCoord =
                WorldBlockToChunk(
                    PlayerBlock);

            OutChunkCoord.X =
                FMath::Clamp(
                    OutChunkCoord.X,
                    0,
                    WorldSizeX - 1);

            OutChunkCoord.Y =
                FMath::Clamp(
                    OutChunkCoord.Y,
                    0,
                    WorldSizeY - 1);

            OutChunkCoord.Z =
                FMath::Clamp(
                    OutChunkCoord.Z,
                    0,
                    WorldSizeZ - 1);

            return true;
        }
    }


    /*
     * Если игрок ещё не появился,
     * начинаем с центра доступного мира.
     */
    OutChunkCoord =
        FIntVector(
            WorldSizeX / 2,
            WorldSizeY / 2,
            0);

    return true;
}


bool AVoxelWorld::IsChunkInsideWorld(
    const FIntVector& ChunkCoord) const
{
    return
        ChunkCoord.X >= 0 &&
        ChunkCoord.X < WorldSizeX &&
        ChunkCoord.Y >= 0 &&
        ChunkCoord.Y < WorldSizeY &&
        ChunkCoord.Z >= 0 &&
        ChunkCoord.Z < WorldSizeZ;
}


AVoxelChunk* AVoxelWorld::CreateChunk(
    const FIntVector& ChunkCoord)
{
    if (!IsChunkInsideWorld(ChunkCoord))
    {
        return nullptr;
    }


    if (AVoxelChunk* Existing =
        GetChunk(ChunkCoord))
    {
        return Existing;
    }


    UWorld* World = GetWorld();

    if (!World)
    {
        return nullptr;
    }


    const FVector Location =
        GetActorLocation() +
        FVector(
            ChunkCoord.X * ChunkSize * VoxelSize,
            ChunkCoord.Y * ChunkSize * VoxelSize,
            ChunkCoord.Z * ChunkSize * VoxelSize);


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
        return nullptr;
    }


    Chunk->InitializeChunk(
        this,
        ChunkCoord);

    Chunk->SetVoxelMaterial(
        Material);

    Chunk->SetWaterMaterial(
        WaterMaterial);


    Chunks.Add(
        ChunkCoord,
        Chunk);


    GenerateChunkBlocks(
        Chunk);


    /*
     * Новый chunk и его уже загруженные соседи
     * получают корректные границы mesh.
     */
    RebuildChunkAndNeighbors(
        ChunkCoord);


    return Chunk;
}


void AVoxelWorld::UpdateChunkStreaming()
{
    FIntVector CenterChunk;

    if (!GetStreamingCenterChunk(
        CenterChunk))
    {
        return;
    }


    const bool bCenterChanged =
        !bStreamingInitialized ||
        CenterChunk != LastStreamingCenter;


    LastStreamingCenter =
        CenterChunk;

    bStreamingInitialized = true;


    /*
     * Сначала выгружаем слишком далёкие chunks.
     *
     * UnloadRadius обычно немного больше
     * StreamingRadius, чтобы не было дёрганья
     * при переходе через границу.
     */
    TArray<FIntVector> ChunksToUnload;


    const int32 EffectiveUnloadRadius =
        FMath::Max(
            UnloadRadius,
            StreamingRadius + 1);


    for (const TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        const FIntVector& Coord =
            Pair.Key;


        const int32 DistanceX =
            FMath::Abs(
                Coord.X - CenterChunk.X);

        const int32 DistanceY =
            FMath::Abs(
                Coord.Y - CenterChunk.Y);

        const int32 DistanceZ =
            FMath::Abs(
                Coord.Z - CenterChunk.Z);


        if (DistanceX > EffectiveUnloadRadius ||
            DistanceY > EffectiveUnloadRadius ||
            DistanceZ > EffectiveUnloadRadius)
        {
            ChunksToUnload.Add(
                Coord);
        }
    }


    for (const FIntVector& Coord :
        ChunksToUnload)
    {
        AVoxelChunk* Chunk =
            GetChunk(Coord);

        if (Chunk)
        {
            Chunk->Destroy();
        }

        Chunks.Remove(Coord);
    }


    /*
     * Если центр не изменился и вокруг уже всё загружено,
     * здесь практически ничего не делаем.
     */
    TArray<FIntVector> Candidates;


    for (int32 Z = CenterChunk.Z - StreamingRadius;
         Z <= CenterChunk.Z + StreamingRadius;
         ++Z)
    {
        for (int32 Y = CenterChunk.Y - StreamingRadius;
             Y <= CenterChunk.Y + StreamingRadius;
             ++Y)
        {
            for (int32 X = CenterChunk.X - StreamingRadius;
                 X <= CenterChunk.X + StreamingRadius;
                 ++X)
            {
                const FIntVector Coord(
                    X,
                    Y,
                    Z);


                if (!IsChunkInsideWorld(
                    Coord))
                {
                    continue;
                }


                Candidates.Add(
                    Coord);
            }
        }
    }


    /*
     * Ближайшие chunks грузим первыми.
     */
    Candidates.Sort(
        [&CenterChunk](
            const FIntVector& A,
            const FIntVector& B)
        {
            const int32 AX =
                A.X - CenterChunk.X;

            const int32 AY =
                A.Y - CenterChunk.Y;

            const int32 AZ =
                A.Z - CenterChunk.Z;

            const int32 BX =
                B.X - CenterChunk.X;

            const int32 BY =
                B.Y - CenterChunk.Y;

            const int32 BZ =
                B.Z - CenterChunk.Z;


            const int32 DistanceA =
                AX * AX +
                AY * AY +
                AZ * AZ;

            const int32 DistanceB =
                BX * BX +
                BY * BY +
                BZ * BZ;


            return DistanceA < DistanceB;
        });


    int32 LoadedThisTick = 0;


    for (const FIntVector& Coord :
        Candidates)
    {
        if (LoadedThisTick >=
            MaxChunksPerTick)
        {
            break;
        }


        if (Chunks.Contains(Coord))
        {
            continue;
        }


        if (CreateChunk(Coord))
        {
            ++LoadedThisTick;
        }
    }


    /*
     * После выгрузки соседний chunk мог потерять
     * свой соседа. Восстанавливаем видимые границы.
     */
    if (ChunksToUnload.Num() > 0)
    {
        for (const FIntVector& UnloadedCoord :
            ChunksToUnload)
        {
            for (int32 Z = -1; Z <= 1; ++Z)
            {
                for (int32 Y = -1; Y <= 1; ++Y)
                {
                    for (int32 X = -1; X <= 1; ++X)
                    {
                        const FIntVector NeighborCoord =
                            UnloadedCoord +
                            FIntVector(X, Y, Z);

                        if (AVoxelChunk* Neighbor =
                            GetChunk(NeighborCoord))
                        {
                            Neighbor->RebuildMesh();
                        }
                    }
                }
            }
        }
    }


    /*
     * Если центр сменился, следующими Tick'ами
     * продолжаем дозагружать недостающие chunks.
     */
    if (!bCenterChanged &&
        LoadedThisTick == 0)
    {
        return;
    }
}


/*
 * Генерация terrain.
 */
bool AVoxelWorld::GetBlockDebugInfoAtWorld(
    const FVector& WorldPosition,
    EVoxelBlock& OutBlock,
    FIntVector& OutWorldBlock,
    FIntVector& OutLocalBlock,
    FIntVector& OutChunkCoord) const
{
    if (!WorldToBlock(
            WorldPosition,
            OutWorldBlock))
    {
        return false;
    }

    OutChunkCoord =
        WorldBlockToChunk(
            OutWorldBlock);

    OutLocalBlock =
        WorldBlockToLocal(
            OutWorldBlock);

    const AVoxelChunk* const* ChunkPtr =
        Chunks.Find(
            OutChunkCoord);

    if (!ChunkPtr || !*ChunkPtr)
    {
        OutBlock = EVoxelBlock::Air;
        return false;
    }

    OutBlock =
        static_cast<EVoxelBlock>(
            (*ChunkPtr)->GetBlock(
                OutLocalBlock.X,
                OutLocalBlock.Y,
                OutLocalBlock.Z));

    return true;
}

void AVoxelWorld::GetTerrainDebugInfo(
    int32 WorldX,
    int32 WorldY,
    int32& OutSurfaceHeight,
    float& OutTerrainNoise,
    float& OutTemperature,
    float& OutMoisture,
    FString& OutBiomeName) const
{
    OutSurfaceHeight =
        WorldGenerator.GetSurfaceHeight(
            WorldX,
            WorldY);

    OutTerrainNoise =
        WorldGenerator.GetTerrainNoise(
            WorldX,
            WorldY);

    OutTemperature =
        WorldGenerator.GetTemperature(
            WorldX,
            WorldY);

    OutMoisture =
        WorldGenerator.GetMoisture(
            WorldX,
            WorldY);

    const EVoxelBiome Biome =
        WorldGenerator.GetBiome(
            WorldX,
            WorldY,
            OutSurfaceHeight);

    switch (Biome)
    {
    case EVoxelBiome::Forest:
        OutBiomeName = TEXT("Forest");
        break;

    case EVoxelBiome::Desert:
        OutBiomeName = TEXT("Desert");
        break;

    case EVoxelBiome::Mountain:
        OutBiomeName = TEXT("Mountain");
        break;

    case EVoxelBiome::Plains:
    default:
        OutBiomeName = TEXT("Plains");
        break;
    }
}


void AVoxelWorld::GenerateChunkBlocks(
    AVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        return;
    }

    Chunk->ClearModificationFlags();

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
                    Block == uint8(EVoxelBlock::Water)
                        ? 4
                        : uint8(Biome));

                /*
                 * Сначала строим обычный процедурный блок.
                 * Затем накладываем изменение игрока, если
                 * этот локальный блок уже был изменён ранее.
                 */
                const int32 LocalIndex =
                    X +
                    Y * ChunkSize +
                    Z * ChunkSize * ChunkSize;

                Chunk->SetBaseBlock(
                    X,
                    Y,
                    Z,
                    Block);

                bool bModified = false;

                if (const TMap<int32, uint8>* ChunkModifications =
                    ModifiedBlocks.Find(ChunkCoord))
                {
                    if (const uint8* ModifiedBlock =
                        ChunkModifications->Find(LocalIndex))
                    {
                        Block = *ModifiedBlock;
                        bModified = true;
                    }
                }

                Chunk->SetModificationFlag(
                    X,
                    Y,
                    Z,
                    bModified);

                Chunk->SetBlock(
                    X,
                    Y,
                    Z,
                    Block);
            }
        }
    }
}


void AVoxelWorld::SaveWorld()
{
    if (SaveSlotName.IsEmpty())
    {
        return;
    }

    UVoxelWorldSaveGame* SaveGame =
        Cast<UVoxelWorldSaveGame>(
            UGameplayStatics::CreateSaveGameObject(
                UVoxelWorldSaveGame::StaticClass()));

    if (!SaveGame)
    {
        return;
    }

    SaveGame->SaveVersion = 1;
    SaveGame->Seed = Seed;
    SaveGame->ChunkSize = ChunkSize;
    SaveGame->VoxelSize = VoxelSize;

    SaveGame->ModifiedBlocks.Reset();

    for (const TPair<FIntVector, TMap<int32, uint8>>& ChunkPair : ModifiedBlocks)
    {
        for (const TPair<int32, uint8>& BlockPair : ChunkPair.Value)
        {
            FVoxelSavedBlock& Record =
                SaveGame->ModifiedBlocks.AddDefaulted_GetRef();

            Record.ChunkCoord = ChunkPair.Key;
            Record.LocalIndex = BlockPair.Key;
            Record.Block = BlockPair.Value;
        }
    }

    SaveGame->ObjectStates.Reset();

    for (const TPair<int64, uint8>& ObjectPair : PersistentObjectStates)
    {
        FVoxelSavedObjectState& Record =
            SaveGame->ObjectStates.AddDefaulted_GetRef();

        Record.ObjectId = ObjectPair.Key;
        Record.State = ObjectPair.Value;
    }

    if (UGameplayStatics::SaveGameToSlot(
        SaveGame,
        SaveSlotName,
        SaveUserIndex))
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("Voxel world saved: %d block changes, %d object states."),
            SaveGame->ModifiedBlocks.Num(),
            SaveGame->ObjectStates.Num());
    }
}


bool AVoxelWorld::LoadWorld()
{
    if (SaveSlotName.IsEmpty() ||
        !UGameplayStatics::DoesSaveGameExist(
            SaveSlotName,
            SaveUserIndex))
    {
        return false;
    }

    UVoxelWorldSaveGame* SaveGame =
        Cast<UVoxelWorldSaveGame>(
            UGameplayStatics::LoadGameFromSlot(
                SaveSlotName,
                SaveUserIndex));

    if (!SaveGame ||
        SaveGame->SaveVersion != 1 ||
        SaveGame->Seed != Seed ||
        SaveGame->ChunkSize != ChunkSize ||
        !FMath::IsNearlyEqual(
            SaveGame->VoxelSize,
            VoxelSize))
    {
        return false;
    }

    ModifiedBlocks.Empty();
    PersistentObjectStates.Empty();

    for (const FVoxelSavedBlock& Record :
        SaveGame->ModifiedBlocks)
    {
        const int32 BlockCount =
            ChunkSize * ChunkSize * ChunkSize;

        if (Record.LocalIndex < 0 ||
            Record.LocalIndex >= BlockCount ||
            !IsChunkInsideWorld(Record.ChunkCoord))
        {
            continue;
        }

        ModifiedBlocks
            .FindOrAdd(Record.ChunkCoord)
            .Add(
                Record.LocalIndex,
                Record.Block);
    }

    for (const FVoxelSavedObjectState& Record :
        SaveGame->ObjectStates)
    {
        PersistentObjectStates.Add(
            Record.ObjectId,
            Record.State);
    }

    /*
     * Важно: загруженный SaveGame хранит только delta-изменения
     * относительно процедурного мира.
     *
     * Поэтому уже загруженные chunks нельзя просто дополнить
     * сохранёнными блоками поверх их текущего runtime-состояния:
     * иначе изменения, сделанные ПОСЛЕ сейва, останутся.
     *
     * Сначала полностью восстанавливаем процедурную базу,
     * затем GenerateChunkBlocks() накладывает сохранённые
     * ModifiedBlocks.
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
     * После восстановления данных перестраиваем mesh всех
     * загруженных chunks. Так изменения после сейва действительно
     * исчезают, а состояние возвращается ровно к моменту Save.
     */
    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            Pair.Value->RebuildMesh();
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("Voxel world loaded: %d block changes, %d object states."),
        SaveGame->ModifiedBlocks.Num(),
        SaveGame->ObjectStates.Num());

    return true;
}


void AVoxelWorld::ApplyLoadedPersistenceToLoadedChunks()
{
    for (const TPair<FIntVector, TMap<int32, uint8>>& ChunkPair :
        ModifiedBlocks)
    {
        AVoxelChunk* Chunk =
            GetChunk(ChunkPair.Key);

        if (!Chunk)
        {
            continue;
        }

        for (const TPair<int32, uint8>& BlockPair :
            ChunkPair.Value)
        {
            const int32 LocalIndex = BlockPair.Key;

            const int32 X =
                LocalIndex % ChunkSize;

            const int32 Y =
                (LocalIndex / ChunkSize) % ChunkSize;

            const int32 Z =
                LocalIndex /
                (ChunkSize * ChunkSize);

            Chunk->SetBlock(
                X,
                Y,
                Z,
                BlockPair.Value);
        }

        RebuildChunkAndNeighbors(
            ChunkPair.Key);
    }
}


void AVoxelWorld::SetPersistentObjectState(
    int64 ObjectId,
    uint8 State)
{
    SetPersistentObjectStateInternal(
        ObjectId,
        State);
}


void AVoxelWorld::SetPersistentObjectStateInternal(
    int64 ObjectId,
    uint8 State)
{
    PersistentObjectStates.Add(
        ObjectId,
        State);
}


bool AVoxelWorld::GetPersistentObjectState(
    int64 ObjectId,
    uint8& OutState) const
{
    if (const uint8* State =
        PersistentObjectStates.Find(ObjectId))
    {
        OutState = *State;
        return true;
    }

    OutState = 0;
    return false;
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
        Neighbor->CopyXPlusStructure(
            OutData.XMinusStructure);
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
        Neighbor->CopyXMinusStructure(
            OutData.XPlusStructure);
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
        Neighbor->CopyYPlusStructure(
            OutData.YMinusStructure);
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
        Neighbor->CopyYMinusStructure(
            OutData.YPlusStructure);
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
        Neighbor->CopyZPlusStructure(
            OutData.ZMinusStructure);
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
        Neighbor->CopyZMinusStructure(
            OutData.ZPlusStructure);
    }
}


/*
 * Expanded block snapshot for Marching Cubes.
 * The smooth field deliberately excludes player-modified cells so
 * construction blocks stay on the separate cubic layer.
 * Local block coordinates range from [-1, Size].
 */
void AVoxelWorld::BuildMarchingCubesData(
    const FIntVector& ChunkCoord,
    FVoxelMarchingCubesBuildInput& OutData) const
{
    OutData.Init(ChunkSize);
    OutData.VoxelSize = VoxelSize;

    const int32 Side = ChunkSize + 2;

    for (int32 Z = -1; Z <= ChunkSize; ++Z)
    {
        for (int32 Y = -1; Y <= ChunkSize; ++Y)
        {
            for (int32 X = -1; X <= ChunkSize; ++X)
            {
                const FIntVector WorldBlock(
                    ChunkCoord.X * ChunkSize + X,
                    ChunkCoord.Y * ChunkSize + Y,
                    ChunkCoord.Z * ChunkSize + Z);

                const FIntVector NeighborChunkCoord =
                    WorldBlockToChunk(WorldBlock);

                const FIntVector LocalBlock =
                    WorldBlockToLocal(WorldBlock);

                uint8 Block =
                    uint8(EVoxelBlock::Air);

                if (const AVoxelChunk* Chunk =
                    Chunks.FindRef(NeighborChunkCoord))
                {
                    Block = Chunk->GetTerrainBlock(
                        LocalBlock.X,
                        LocalBlock.Y,
                        LocalBlock.Z);
                }

                const int32 Index =
                    (X + 1) +
                    (Y + 1) * Side +
                    (Z + 1) * Side * Side;

                OutData.Blocks[Index] = Block;
            }
        }
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

    Chunk->SetModificationFlag(
        LocalBlock.X,
        LocalBlock.Y,
        LocalBlock.Z,
        true);


    /*
     * Сохраняем изменение отдельно от runtime-данных чанка.
     *
     * Поэтому после Destroy() чанка streaming не теряет
     * изменения: при следующем CreateChunk() они будут
     * наложены поверх процедурной генерации.
     */
    const int32 LocalIndex =
        LocalBlock.X +
        LocalBlock.Y * ChunkSize +
        LocalBlock.Z * ChunkSize * ChunkSize;

    TMap<int32, uint8>& ChunkModifications =
        ModifiedBlocks.FindOrAdd(ChunkCoord);

    ChunkModifications.Add(
        LocalIndex,
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
    /*
     * MC density nodes near borders can depend on blocks in diagonal
     * neighbours because each node samples the surrounding voxel cells.
     */
    for (int32 Z = -1; Z <= 1; ++Z)
    {
        for (int32 Y = -1; Y <= 1; ++Y)
        {
            for (int32 X = -1; X <= 1; ++X)
            {
                if (AVoxelChunk* Chunk =
                    GetChunk(
                        ChunkCoord + FIntVector(X, Y, Z)))
                {
                    Chunk->RebuildMesh();
                }
            }
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


    AVoxelChunk* Chunk =
        GetChunk(WorldBlockToChunk(WorldBlock));

    if (!Chunk)
    {
        return false;
    }

    const FIntVector LocalBlock =
        WorldBlockToLocal(WorldBlock);

    const EVoxelBlock HitBlock =
        static_cast<EVoxelBlock>(
            Chunk->GetBlock(
                LocalBlock.X,
                LocalBlock.Y,
                LocalBlock.Z));

    /*
     * Вся игровая логика блока теперь смотрит в единый реестр.
     */
    if (!UVoxelBlockLibrary::CanBreakBlockFromTable(
        BlockDataTable,
        HitBlock))
    {
        return false;
    }

    const EVoxelBlock DropBlock =
        UVoxelBlockLibrary::GetBlockDropFromTable(
            BlockDataTable,
            HitBlock);

    APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            GetWorld(),
            0);

    UVoxelInventoryComponent* Inventory = nullptr;

    if (PlayerPawn)
    {
        Inventory =
            PlayerPawn->FindComponentByClass<
                UVoxelInventoryComponent>();
    }

    /*
     * Если у блока есть дроп и inventory установлен,
     * сначала убеждаемся, что предмет можно подобрать.
     * Тогда блок не исчезнет впустую при заполненном hotbar.
     */
    if (Inventory &&
        DropBlock != EVoxelBlock::Air &&
        !Inventory->CanAddBlock(
            DropBlock,
            1))
    {
        return false;
    }

    SetBlockInternal(
        WorldBlock,
        uint8(EVoxelBlock::Air));

    if (Inventory &&
        DropBlock != EVoxelBlock::Air)
    {
        Inventory->AddBlock(
            DropBlock,
            1);
    }

    return true;
}


/*
 * Установка блока рядом с поверхностью.
 */
bool AVoxelWorld::PlaceBlockByRay()
{
    /*
     * Старый Blueprint-вызов остаётся совместимым.
     *
     * Если на игроке есть inventory, берём блок из выбранного
     * слота. Если компонента ещё нет, для обратной совместимости
     * используем прежний Dirt.
     */
    if (APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            GetWorld(),
            0))
    {
        if (UVoxelInventoryComponent* Inventory =
            PlayerPawn->FindComponentByClass<
                UVoxelInventoryComponent>())
        {
            return PlaceBlockByRayWithType(
                Inventory->GetSelectedBlock());
        }
    }

    return PlaceBlockByRayWithType(
        EVoxelBlock::Dirt);
}


bool AVoxelWorld::PlaceBlockByRayWithType(
    EVoxelBlock BlockToPlace)
{
    APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);

    if (!PC)
    {
        return false;
    }

    if (!UVoxelBlockLibrary::CanPlaceBlockFromTable(
        BlockDataTable,
        BlockToPlace))
    {
        return false;
    }

    APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            GetWorld(),
            0);

    UVoxelInventoryComponent* Inventory = nullptr;

    if (PlayerPawn)
    {
        Inventory =
            PlayerPawn->FindComponentByClass<
                UVoxelInventoryComponent>();
    }

    /*
     * Если inventory подключён, выбранный слот обязан
     * содержать тот же блок, который мы ставим.
     */
    if (Inventory &&
        (Inventory->GetSelectedBlock() != BlockToPlace ||
         Inventory->GetSelectedQuantity() <= 0))
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

    const FIntVector LocalBlock =
        WorldBlockToLocal(WorldBlock);

    AVoxelChunk* TargetChunk =
        GetChunk(
            WorldBlockToChunk(
                WorldBlock));

    if (!TargetChunk)
    {
        return false;
    }

    /*
     * Не перезаписываем существующий твёрдый блок.
     * Воду также не считаем свободным местом для строительства.
     */
    const EVoxelBlock ExistingBlock =
        static_cast<EVoxelBlock>(
            TargetChunk->GetBlock(
                LocalBlock.X,
                LocalBlock.Y,
                LocalBlock.Z));

    if (ExistingBlock != EVoxelBlock::Air)
    {
        return false;
    }

    SetBlockInternal(
        WorldBlock,
        uint8(BlockToPlace));

    if (Inventory)
    {
        Inventory->RemoveFromSelectedSlot(1);
    }

    return true;
}
