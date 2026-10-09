
#include "VoxelWorld.h"
#include "VoxelChunk.h"
#include "VoxelWorldGenerator.h"
#include "VoxelWorldSaveGame.h"
#include "VoxelBlockLibrary.h"
#include "VoxelInventoryComponent.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
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

    /*
     * Pure voxel-data generation. This function deliberately uses no
     * UObject or AVoxelWorld members, so it is safe on a worker thread.
     */
    void BuildGeneratedChunkData(
        const FVoxelWorldGenerator& Generator,
        const FIntVector& ChunkCoord,
        int32 ChunkSize,
        int32 SeaLevel,
        int32 BeachWidth,
        const TMap<int32, uint8>& ChunkModifications,
        TArray<uint8>& OutBlocks,
        TArray<uint8>& OutBaseBlocks,
        TArray<uint8>& OutBiomes,
        TArray<uint8>& OutModificationFlags)
    {
        const int32 BlockCount =
            ChunkSize * ChunkSize * ChunkSize;

        OutBlocks.SetNumZeroed(BlockCount);
        OutBaseBlocks.SetNumZeroed(BlockCount);
        OutBiomes.SetNumZeroed(BlockCount);
        OutModificationFlags.SetNumZeroed(BlockCount);

        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            for (int32 X = 0; X < ChunkSize; ++X)
            {
                const int32 WorldX =
                    ChunkCoord.X * ChunkSize + X;

                const int32 WorldY =
                    ChunkCoord.Y * ChunkSize + Y;

                const int32 Height =
                    Generator.GetSurfaceHeight(WorldX, WorldY);

                const EVoxelBiome Biome =
                    Generator.GetBiome(WorldX, WorldY, Height);

                const EVoxelLandform Landform =
                    Generator.GetLandform(WorldX, WorldY);

                for (int32 Z = 0; Z < ChunkSize; ++Z)
                {
                    const int32 WorldZ =
                        ChunkCoord.Z * ChunkSize + Z;

                    uint8 Block = uint8(EVoxelBlock::Air);

                    if (WorldZ > Height)
                    {
                        Block = WorldZ <= SeaLevel
                            ? uint8(EVoxelBlock::Water)
                            : uint8(EVoxelBlock::Air);
                    }
                    else if (WorldZ == Height)
                    {
                        const bool bBeach =
                            Biome != EVoxelBiome::Snow &&
                            Height < SeaLevel &&
                            Height >= SeaLevel - BeachWidth;

                        if (bBeach)
                        {
                            Block = uint8(EVoxelBlock::Sand);
                        }
                        else if (Biome == EVoxelBiome::Snow)
                        {
                            Block = uint8(EVoxelBlock::Snow);
                        }
                        else if (Landform == EVoxelLandform::Mountains)
                        {
                            // Green-biome mountain tops use a grassy surface;
                            // Snow and desert already have dedicated branches.
                            Block = Biome == EVoxelBiome::Desert
                                ? uint8(EVoxelBlock::Sandstone)
                                : uint8(EVoxelBlock::Grass);
                        }
                        else if (Biome == EVoxelBiome::Desert)
                        {
                            Block = uint8(EVoxelBlock::Sand);
                        }
                        else
                        {
                            Block = uint8(EVoxelBlock::Grass);
                        }
                    }
                    else if (Biome == EVoxelBiome::Desert &&
                             Landform == EVoxelLandform::Mountains &&
                             WorldZ >= Height - 5)
                    {
                        Block = uint8(EVoxelBlock::Sandstone);
                    }
                    else if (Biome == EVoxelBiome::Desert &&
                             WorldZ >= Height - 3)
                    {
                        Block = uint8(EVoxelBlock::Sand);
                    }
                    else if (Landform == EVoxelLandform::Mountains &&
                             WorldZ >= Height - 3)
                    {
                        // Keep the outer few blocks grassy on green
                        // mountain faces. Snow mountains remain rocky.
                        Block = Biome == EVoxelBiome::Snow
                            ? uint8(EVoxelBlock::Stone)
                            : uint8(EVoxelBlock::Grass);
                    }
                    else if (WorldZ >= Height - 3)
                    {
                        Block = uint8(EVoxelBlock::Dirt);
                    }
                    else
                    {
                        Block = uint8(EVoxelBlock::Stone);
                    }

                    const int32 LocalIndex =
                        X + Y * ChunkSize + Z * ChunkSize * ChunkSize;

                    OutBaseBlocks[LocalIndex] = Block;
                    OutBiomes[LocalIndex] =
                        Block == uint8(EVoxelBlock::Water)
                            ? 4
                            : uint8(Biome);

                    if (const uint8* ModifiedBlock =
                        ChunkModifications.Find(LocalIndex))
                    {
                        OutBlocks[LocalIndex] = *ModifiedBlock;
                        OutModificationFlags[LocalIndex] = 1;
                    }
                    else
                    {
                        OutBlocks[LocalIndex] = Block;
                        OutModificationFlags[LocalIndex] = 0;
                    }
                }
            }
        }
    }


    /*
     * Sample the same procedural terrain used by chunk generation.
     * Marching Cubes needs a one-voxel halo around a chunk; a neighbour
     * actor may not exist yet while streaming asynchronously, but treating
     * that halo as Air creates temporary/fake faces at chunk boundaries.
     */
    uint8 GetGeneratedBlockAtWorld(
        const FVoxelWorldGenerator& Generator,
        int32 WorldX,
        int32 WorldY,
        int32 WorldZ,
        int32 SeaLevel,
        int32 BeachWidth)
    {
        const int32 Height =
            Generator.GetSurfaceHeight(WorldX, WorldY);

        const EVoxelBiome Biome =
            Generator.GetBiome(WorldX, WorldY, Height);

        const EVoxelLandform Landform =
            Generator.GetLandform(WorldX, WorldY);

        if (WorldZ > Height)
        {
            return WorldZ <= SeaLevel
                ? uint8(EVoxelBlock::Water)
                : uint8(EVoxelBlock::Air);
        }

        if (WorldZ == Height)
        {
            const bool bBeach =
                Biome != EVoxelBiome::Snow &&
                Height < SeaLevel &&
                Height >= SeaLevel - BeachWidth;

            if (bBeach)
            {
                return uint8(EVoxelBlock::Sand);
            }

            if (Biome == EVoxelBiome::Snow)
            {
                return uint8(EVoxelBlock::Snow);
            }

            if (Landform == EVoxelLandform::Mountains)
            {
                // Snow was handled above. Use grass for green-biome
                // mountain surfaces to avoid the giant-boulder look.
                return Biome == EVoxelBiome::Desert
                    ? uint8(EVoxelBlock::Sandstone)
                    : uint8(EVoxelBlock::Grass);
            }

            if (Biome == EVoxelBiome::Desert)
            {
                return uint8(EVoxelBlock::Sand);
            }

            return uint8(EVoxelBlock::Grass);
        }

        if (Biome == EVoxelBiome::Desert &&
            Landform == EVoxelLandform::Mountains &&
            WorldZ >= Height - 5)
        {
            return uint8(EVoxelBlock::Sandstone);
        }

        if (Biome == EVoxelBiome::Desert &&
            WorldZ >= Height - 3)
        {
            return uint8(EVoxelBlock::Sand);
        }

        if (Landform == EVoxelLandform::Mountains &&
            WorldZ >= Height - 3)
        {
            return Biome == EVoxelBiome::Snow
                ? uint8(EVoxelBlock::Stone)
                : uint8(EVoxelBlock::Grass);
        }

        if (WorldZ >= Height - 3)
        {
            return uint8(EVoxelBlock::Dirt);
        }

        return uint8(EVoxelBlock::Stone);
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
    PrimaryActorTick.TickInterval = 0.0f;
}


void AVoxelWorld::BeginPlay()
{
    Super::BeginPlay();

    /*
     * Debug fly управляется через сам VoxelWorld, поэтому не требуется
     * менять parent у BP_VoxelPlayer / BP_VoxelPlayerController.
     */
    if (APlayerController* PC =
        GetWorld()
            ? GetWorld()->GetFirstPlayerController()
            : nullptr)
    {
        EnableInput(PC);

        /*
         * AVoxelWorld получает только debug input, но не должен переводить
         * PlayerController в UI input mode. Явно возвращаем игровой режим
         * и захват мыши после регистрации debug bindings.
         */
        PC->bShowMouseCursor = false;
        FInputModeGameOnly GameOnlyInput;
        PC->SetInputMode(GameOnlyInput);

        if (InputComponent)
        {
            InputComponent->BindAction(
                TEXT("ToggleDebugFly"),
                IE_Pressed,
                this,
                &AVoxelWorld::ToggleDebugFly);

            InputComponent->BindAction(
                TEXT("DebugFlyUp"),
                IE_Pressed,
                this,
                &AVoxelWorld::DebugFlyUpPressed);

            InputComponent->BindAction(
                TEXT("DebugFlyUp"),
                IE_Released,
                this,
                &AVoxelWorld::DebugFlyUpReleased);

            InputComponent->BindAction(
                TEXT("DebugFlyDown"),
                IE_Pressed,
                this,
                &AVoxelWorld::DebugFlyDownPressed);

            InputComponent->BindAction(
                TEXT("DebugFlyDown"),
                IE_Released,
                this,
                &AVoxelWorld::DebugFlyDownReleased);

            InputComponent->BindAction(
                TEXT("DebugFlyBoost"),
                IE_Pressed,
                this,
                &AVoxelWorld::DebugFlyBoostPressed);

            InputComponent->BindAction(
                TEXT("DebugFlyBoost"),
                IE_Released,
                this,
                &AVoxelWorld::DebugFlyBoostReleased);
        }
    }

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

    if (bDebugFlyMode)
    {
        if (APawn* Pawn =
            UGameplayStatics::GetPlayerPawn(
                GetWorld(),
                0))
        {
            Pawn->AddMovementInput(
                FVector::UpVector,
                DebugFlyVerticalInput);
        }
    }

    /*
     * Keep streaming decisions throttled to 10 Hz, but drain the mesh
     * rebuild queue every frame so each frame handles only a small amount
     * of synchronous snapshot work.
     */
    TimeSinceLastStreamingUpdate += DeltaSeconds;
    if (TimeSinceLastStreamingUpdate >= 0.1f)
    {
        TimeSinceLastStreamingUpdate =
            FMath::Fmod(TimeSinceLastStreamingUpdate, 0.1f);
        UpdateChunkStreaming();
    }

    ProcessPendingChunkMeshRebuilds();
    UpdateUnderwaterEffect();

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
    Settings.WorldBlocksX =
        FMath::Max(1, WorldSizeX * ChunkSize);
    Settings.WorldBlocksY =
        FMath::Max(1, WorldSizeY * ChunkSize);
    Settings.MaxTerrainHeight =
        FMath::Max(1, WorldSizeZ * ChunkSize - 3);
    Settings.BaseHeight = BaseHeight;
    Settings.HeightVariation = HeightVariation;
    Settings.NoiseScale = NoiseScale;
    Settings.DetailNoiseScale = DetailNoiseScale;
    Settings.DetailHeightVariation = DetailHeightVariation;
    Settings.PlateauScale = PlateauScale;
    Settings.PlateauHeightStep = PlateauHeightStep;
    Settings.PlateauStrength = PlateauStrength;
    Settings.SeaLevel = SeaLevel;
    Settings.TemperatureScale = TemperatureScale;
    Settings.MoistureScale = MoistureScale;

    WorldGenerator.Configure(Settings);
}


/*
 * Calculate the center start point from the same generator used by chunks.
 * Configure here as well because GameMode may request a start location
 * before AVoxelWorld::BeginPlay has finished initializing the world.
 */
FVector AVoxelWorld::GetCenterSpawnLocation()
{
    ConfigureWorldGenerator();

    const int32 WorldBlocksX =
        FMath::Max(1, WorldSizeX * ChunkSize);

    const int32 WorldBlocksY =
        FMath::Max(1, WorldSizeY * ChunkSize);

    const int32 CenterBlockX =
        FMath::Clamp(WorldBlocksX / 2, 0, WorldBlocksX - 1);

    const int32 CenterBlockY =
        FMath::Clamp(WorldBlocksY / 2, 0, WorldBlocksY - 1);

    const int32 SurfaceHeight =
        WorldGenerator.GetSurfaceHeight(
            CenterBlockX,
            CenterBlockY);

    return GetActorLocation() +
        FVector(
            (static_cast<float>(CenterBlockX) + 0.5f) * VoxelSize,
            (static_cast<float>(CenterBlockY) + 0.5f) * VoxelSize,
            (static_cast<float>(SurfaceHeight) + 2.5f) * VoxelSize);
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
    ClearFarLOD();

    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            Pair.Value->Destroy();
        }
    }

    Chunks.Empty();
    PendingChunkMeshRebuilds.Empty();

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

    /*
     * Expensive terrain generation runs on a worker. Mesh creation and
     * collision remain on the Game Thread through the normal rebuild path.
     */
    GenerateChunkBlocksAsync(Chunk);

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
     * Full chunks are the only AVoxelChunk actors.
     * Far terrain is rendered by lightweight world-owned tiles.
     */
    const int32 EffectiveUnloadRadius =
        FMath::Max(
            UnloadRadius,
            StreamingRadius);

    TArray<FIntVector> ChunksToUnload;

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
        PendingChunkMeshRebuilds.Remove(Coord);
    }

    TArray<FIntVector> Candidates;

    for (int32 Z = CenterChunk.Z - StreamingRadius;
         Z <= CenterChunk.Z + StreamingRadius; ++Z)
    {
        for (int32 Y = CenterChunk.Y - StreamingRadius;
             Y <= CenterChunk.Y + StreamingRadius; ++Y)
        {
            for (int32 X = CenterChunk.X - StreamingRadius;
                 X <= CenterChunk.X + StreamingRadius; ++X)
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
     * Ближайшие Full chunks грузим первыми.
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
     * Far LOD follows the streaming center so its inner boundary stays
     * synchronized with the Full chunk ring. The build itself is performed
     * asynchronously, so this does not block the game thread while the
     * distant meshes are regenerated.
     */
    const int32 FarMoveX =
        FMath::Abs(
            CenterChunk.X -
            LastFarLODCenter.X);

    const int32 FarMoveY =
        FMath::Abs(
            CenterChunk.Y -
            LastFarLODCenter.Y);

    const int32 FarMove =
        FMath::Max(
            FarMoveX,
            FarMoveY);

    if (!bFarLODInitialized ||
        FarMove >= FMath::Max(1, LODUpdateChunkInterval))
    {
        LastFarLODCenter =
            CenterChunk;

        bFarLODInitialized = true;

        UpdateFarLOD(
            CenterChunk);
    }

    /*
     * Chunks which lost a neighbour are queued rather than all being
     * snapshotted in one frame. This is especially important at vehicle speed.
     */
    if (ChunksToUnload.Num() > 0)
    {
        for (const FIntVector& UnloadedCoord : ChunksToUnload)
        {
            QueueChunkNeighborhoodRebuilds(UnloadedCoord);
        }
    }

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

    case EVoxelBiome::Snow:
        OutBiomeName = TEXT("Snow");
        break;

    case EVoxelBiome::Mountain:
        OutBiomeName = TEXT("Mountain (legacy)");
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

    const FIntVector ChunkCoord = Chunk->GetChunkCoord();

    TMap<int32, uint8> ChunkModifications;
    if (const TMap<int32, uint8>* FoundMods =
        ModifiedBlocks.Find(ChunkCoord))
    {
        ChunkModifications = *FoundMods;
    }

    TArray<uint8> Blocks;
    TArray<uint8> BaseBlocks;
    TArray<uint8> Biomes;
    TArray<uint8> ModificationFlags;

    BuildGeneratedChunkData(
        WorldGenerator,
        ChunkCoord,
        ChunkSize,
        SeaLevel,
        BeachWidth,
        ChunkModifications,
        Blocks,
        BaseBlocks,
        Biomes,
        ModificationFlags);

    Chunk->SetGeneratedData(
        MoveTemp(Blocks),
        MoveTemp(BaseBlocks),
        MoveTemp(Biomes),
        MoveTemp(ModificationFlags));
}


void AVoxelWorld::GenerateChunkBlocksAsync(
    AVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        return;
    }

    const FIntVector ChunkCoord = Chunk->GetChunkCoord();
    const uint32 ExpectedDataGenerationVersion =
        Chunk->GetDataGenerationVersion();
    const FVoxelWorldGenerator GeneratorCopy = WorldGenerator;
    const int32 LocalChunkSize = ChunkSize;
    const int32 LocalSeaLevel = SeaLevel;
    const int32 LocalBeachWidth = BeachWidth;

    TMap<int32, uint8> ChunkModifications;
    if (const TMap<int32, uint8>* FoundMods =
        ModifiedBlocks.Find(ChunkCoord))
    {
        ChunkModifications = *FoundMods;
    }

    TWeakObjectPtr<AVoxelWorld> WeakWorld(this);
    TWeakObjectPtr<AVoxelChunk> WeakChunk(Chunk);

    Async(
        EAsyncExecution::ThreadPool,
        [
            WeakWorld,
            WeakChunk,
            GeneratorCopy,
            ExpectedDataGenerationVersion,
            ChunkCoord,
            LocalChunkSize,
            LocalSeaLevel,
            LocalBeachWidth,
            ChunkModifications = MoveTemp(ChunkModifications)
        ]() mutable
        {
            TArray<uint8> Blocks;
            TArray<uint8> BaseBlocks;
            TArray<uint8> Biomes;
            TArray<uint8> ModificationFlags;

            BuildGeneratedChunkData(
                GeneratorCopy,
                ChunkCoord,
                LocalChunkSize,
                LocalSeaLevel,
                LocalBeachWidth,
                ChunkModifications,
                Blocks,
                BaseBlocks,
                Biomes,
                ModificationFlags);

            AsyncTask(
                ENamedThreads::GameThread,
                [
                    WeakWorld,
                    WeakChunk,
                    ExpectedDataGenerationVersion,
                    ChunkCoord,
                    Blocks = MoveTemp(Blocks),
                    BaseBlocks = MoveTemp(BaseBlocks),
                    Biomes = MoveTemp(Biomes),
                    ModificationFlags = MoveTemp(ModificationFlags)
                ]() mutable
                {
                    AVoxelWorld* World = WeakWorld.Get();
                    AVoxelChunk* ReadyChunk = WeakChunk.Get();

                    if (!World || !ReadyChunk ||
                        World->GetChunk(ChunkCoord) != ReadyChunk ||
                        ReadyChunk->GetDataGenerationVersion() !=
                            ExpectedDataGenerationVersion)
                    {
                        return;
                    }

                    ReadyChunk->SetGeneratedData(
                        MoveTemp(Blocks),
                        MoveTemp(BaseBlocks),
                        MoveTemp(Biomes),
                        MoveTemp(ModificationFlags));

                    /* Render the new chunk immediately; spread neighbour rebuilds across ticks. */
                    ReadyChunk->RebuildMesh();
                    World->QueueChunkNeighborhoodRebuilds(ChunkCoord);
                });
        });
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

    /*
     * Cache this chunk and its 26 neighbours once. The previous version
     * searched the Chunks TMap and recomputed world/local coordinates for
     * every one of the (ChunkSize + 2)^3 samples, repeated for each rebuild.
     */
    const AVoxelChunk* NeighborChunks[3][3][3] = {};

    for (int32 NZ = -1; NZ <= 1; ++NZ)
    {
        for (int32 NY = -1; NY <= 1; ++NY)
        {
            for (int32 NX = -1; NX <= 1; ++NX)
            {
                NeighborChunks[NX + 1][NY + 1][NZ + 1] =
                    Chunks.FindRef(
                        ChunkCoord + FIntVector(NX, NY, NZ));
            }
        }
    }

    for (int32 Z = -1; Z <= ChunkSize; ++Z)
    {
        const int32 SourceChunkZ =
            Z < 0 ? -1 : (Z >= ChunkSize ? 1 : 0);
        const int32 LocalZ =
            Z < 0 ? ChunkSize - 1 :
            (Z >= ChunkSize ? 0 : Z);

        for (int32 Y = -1; Y <= ChunkSize; ++Y)
        {
            const int32 SourceChunkY =
                Y < 0 ? -1 : (Y >= ChunkSize ? 1 : 0);
            const int32 LocalY =
                Y < 0 ? ChunkSize - 1 :
                (Y >= ChunkSize ? 0 : Y);

            for (int32 X = -1; X <= ChunkSize; ++X)
            {
                const int32 SourceChunkX =
                    X < 0 ? -1 : (X >= ChunkSize ? 1 : 0);
                const int32 LocalX =
                    X < 0 ? ChunkSize - 1 :
                    (X >= ChunkSize ? 0 : X);

                uint8 Block = uint8(EVoxelBlock::Air);

                const FIntVector SourceChunkCoord =
                    ChunkCoord + FIntVector(
                        SourceChunkX,
                        SourceChunkY,
                        SourceChunkZ);

                const AVoxelChunk* SourceChunk =
                    NeighborChunks
                        [SourceChunkX + 1]
                        [SourceChunkY + 1]
                        [SourceChunkZ + 1];

                if (SourceChunk && SourceChunk->HasGeneratedData())
                {
                    Block = SourceChunk->GetTerrainBlock(
                        LocalX,
                        LocalY,
                        LocalZ);
                }
                else if (IsChunkInsideWorld(SourceChunkCoord))
                {
                    /*
                     * Neighbour terrain may still be generating (or be
                     * outside the full-chunk streaming radius). Sample its
                     * procedural block directly so the MC halo remains
                     * continuous instead of inventing an Air wall.
                     * Apply saved/player edits with the same rule as
                     * GetTerrainBlock: modified cells belong to the cubic
                     * layer and are excluded from the smooth surface.
                     */
                    const FIntVector WorldBlock(
                        ChunkCoord.X * ChunkSize + X,
                        ChunkCoord.Y * ChunkSize + Y,
                        ChunkCoord.Z * ChunkSize + Z);

                    const int32 SourceLocalIndex =
                        LocalX +
                        LocalY * ChunkSize +
                        LocalZ * ChunkSize * ChunkSize;

                    bool bIsModified = false;
                    if (const TMap<int32, uint8>* SourceModifications =
                        ModifiedBlocks.Find(SourceChunkCoord))
                    {
                        bIsModified =
                            SourceModifications->Contains(SourceLocalIndex);
                    }

                    if (bIsModified)
                    {
                        Block = uint8(EVoxelBlock::Air);
                    }
                    else if (WorldBlock.X >= 0 &&
                             WorldBlock.X < WorldSizeX * ChunkSize &&
                             WorldBlock.Y >= 0 &&
                             WorldBlock.Y < WorldSizeY * ChunkSize &&
                             WorldBlock.Z >= 0 &&
                             WorldBlock.Z < WorldSizeZ * ChunkSize)
                    {
                        Block = GetGeneratedBlockAtWorld(
                            WorldGenerator,
                            WorldBlock.X,
                            WorldBlock.Y,
                            WorldBlock.Z,
                            SeaLevel,
                            BeachWidth);
                    }
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

bool AVoxelWorld::IsPositionInsideWater(
    const FVector& WorldPosition) const
{
    FIntVector WorldBlock;

    if (!WorldToBlock(
        WorldPosition,
        WorldBlock))
    {
        return false;
    }

    const FIntVector ChunkCoord =
        WorldBlockToChunk(WorldBlock);

    const AVoxelChunk* Chunk =
        Chunks.FindRef(ChunkCoord);

    if (!Chunk)
    {
        return false;
    }

    const FIntVector LocalBlock =
        WorldBlockToLocal(WorldBlock);

    return Chunk->GetBlock(
        LocalBlock.X,
        LocalBlock.Y,
        LocalBlock.Z) ==
        uint8(EVoxelBlock::Water);
}


void AVoxelWorld::UpdateUnderwaterEffect()
{
    APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);

    if (!PC || !PC->PlayerCameraManager)
    {
        return;
    }

    const bool bUnderwater =
        IsPositionInsideWater(
            PC->PlayerCameraManager->GetCameraLocation());

    if (bUnderwater == bUnderwaterEffectActive)
    {
        return;
    }

    bUnderwaterEffectActive = bUnderwater;

    if (bUnderwater)
    {
        /*
         * Keep the blue tint active for the whole time the camera
         * is inside a water voxel. StartCameraFade only animates a
         * transition unless it is explicitly held.
         */
        PC->PlayerCameraManager->SetManualCameraFade(
            0.38f,
            FLinearColor(
                0.02f,
                0.20f,
                0.55f,
                1.0f),
            false);
    }
    else
    {
        /*
         * Remove the underwater overlay immediately after leaving water.
         */
        PC->PlayerCameraManager->StopCameraFade();
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


    /* Rebuild the edited chunk immediately; neighbour seams are coalesced and queued. */
    Chunk->RebuildMesh();
    QueueChunkNeighborhoodRebuilds(ChunkCoord);
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
                    if (Chunk->HasGeneratedData())
                    {
                        Chunk->RebuildMesh();
                    }
                }
            }
        }
    }
}


/*
 * Coalesce streaming/edit neighbour updates. Face neighbours are inserted
 * before edge/corner neighbours because they are the most visible seams.
 */
void AVoxelWorld::QueueChunkNeighborhoodRebuilds(
    const FIntVector& ChunkCoord)
{
    for (int32 Distance = 1; Distance <= 3; ++Distance)
    {
        for (int32 Z = -1; Z <= 1; ++Z)
        {
            for (int32 Y = -1; Y <= 1; ++Y)
            {
                for (int32 X = -1; X <= 1; ++X)
                {
                    if (FMath::Abs(X) +
                        FMath::Abs(Y) +
                        FMath::Abs(Z) != Distance)
                    {
                        continue;
                    }

                    const FIntVector NeighborCoord =
                        ChunkCoord + FIntVector(X, Y, Z);

                    AVoxelChunk* Neighbor = GetChunk(NeighborCoord);

                    if (!Neighbor || !Neighbor->HasGeneratedData())
                    {
                        continue;
                    }

                    if (!PendingChunkMeshRebuilds.Contains(NeighborCoord))
                    {
                        PendingChunkMeshRebuilds.Add(NeighborCoord);
                    }
                }
            }
        }
    }
}


void AVoxelWorld::ProcessPendingChunkMeshRebuilds()
{
    const int32 RebuildBudget =
        FMath::Clamp(MaxChunkMeshRebuildsPerTick, 1, 32);

    int32 ProcessedEntries = 0;

    while (PendingChunkMeshRebuilds.Num() > 0 &&
           ProcessedEntries < RebuildBudget)
    {
        ++ProcessedEntries;

        const FIntVector Coord =
            PendingChunkMeshRebuilds[0];

        PendingChunkMeshRebuilds.RemoveAt(0, 1, false);

        AVoxelChunk* Chunk = GetChunk(Coord);

        if (!Chunk || !Chunk->HasGeneratedData())
        {
            continue;
        }

        Chunk->RebuildMesh();
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
     * На MC-поверхности ImpactPoint находится на сглаженной границе,
     * поэтому одного FloorToInt недостаточно. Идём по нормали внутрь
     * поверхности и берём первый реально ломаемый voxel.
     */
    FIntVector WorldBlock;
    AVoxelChunk* Chunk = nullptr;
    FIntVector LocalBlock;
    EVoxelBlock HitBlock = EVoxelBlock::Air;
    bool bFoundBreakableBlock = false;

    const float SampleStep =
        FMath::Max(
            VoxelSize * 0.05f,
            1.0f);

    const int32 MaxSamples = 16;

    for (int32 Sample = 1;
         Sample <= MaxSamples;
         ++Sample)
    {
        const FVector SamplePoint =
            Hit.ImpactPoint -
            Hit.ImpactNormal *
            (SampleStep * Sample);

        FIntVector CandidateWorldBlock;

        if (!WorldToBlock(
            SamplePoint,
            CandidateWorldBlock))
        {
            continue;
        }

        AVoxelChunk* CandidateChunk =
            GetChunk(
                WorldBlockToChunk(
                    CandidateWorldBlock));

        if (!CandidateChunk)
        {
            continue;
        }

        const FIntVector CandidateLocalBlock =
            WorldBlockToLocal(
                CandidateWorldBlock);

        const EVoxelBlock CandidateBlock =
            static_cast<EVoxelBlock>(
                CandidateChunk->GetBlock(
                    CandidateLocalBlock.X,
                    CandidateLocalBlock.Y,
                    CandidateLocalBlock.Z));

        if (!UVoxelBlockLibrary::CanBreakBlockFromTable(
            BlockDataTable,
            CandidateBlock))
        {
            continue;
        }

        WorldBlock = CandidateWorldBlock;
        Chunk = CandidateChunk;
        HitBlock = CandidateBlock;
        bFoundBreakableBlock = true;
        break;
    }

    if (!bFoundBreakableBlock)
    {
        return false;
    }

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

    /*
     * Marching Cubes surfaces are smooth and their hit normal is not
     * necessarily axis-aligned. A single 0.51-voxel offset can still land
     * in a solid voxel even when the point just outside the visible surface
     * is empty. Probe outwards in small steps and pick the nearest cell that
     * is actually available. Water is also replaceable by a placed block.
     */
    const FVector PlacementNormal =
        Hit.ImpactNormal.GetSafeNormal();

    if (PlacementNormal.IsNearlyZero())
    {
        return false;
    }

    const float ProbeStep =
        FMath::Max(VoxelSize * 0.05f, 1.0f);

    const float MaxProbeDistance =
        VoxelSize * 1.1f;

    FIntVector WorldBlock = FIntVector::ZeroValue;
    AVoxelChunk* TargetChunk = nullptr;
    bool bFoundPlacementCell = false;

    for (float Distance = ProbeStep;
         Distance <= MaxProbeDistance + KINDA_SMALL_NUMBER;
         Distance += ProbeStep)
    {
        const FVector ProbePoint =
            Hit.ImpactPoint + PlacementNormal * Distance;

        FIntVector CandidateWorldBlock;
        if (!WorldToBlock(ProbePoint, CandidateWorldBlock))
        {
            continue;
        }

        const FIntVector CandidateChunkCoord =
            WorldBlockToChunk(CandidateWorldBlock);

        AVoxelChunk* CandidateChunk =
            GetChunk(CandidateChunkCoord);

        if (!CandidateChunk || !CandidateChunk->HasGeneratedData())
        {
            continue;
        }

        const FIntVector CandidateLocalBlock =
            WorldBlockToLocal(CandidateWorldBlock);

        const EVoxelBlock CandidateBlock =
            static_cast<EVoxelBlock>(
                CandidateChunk->GetBlock(
                    CandidateLocalBlock.X,
                    CandidateLocalBlock.Y,
                    CandidateLocalBlock.Z));

        if (CandidateBlock != EVoxelBlock::Air &&
            CandidateBlock != EVoxelBlock::Water)
        {
            continue;
        }

        WorldBlock = CandidateWorldBlock;
        TargetChunk = CandidateChunk;
        bFoundPlacementCell = true;
        break;
    }

    if (!bFoundPlacementCell || !TargetChunk)
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




void AVoxelWorld::ToggleDebugFly()
{
    bDebugFlyMode = !bDebugFlyMode;
    DebugFlyVerticalInput = 0.0f;
    bDebugFlyBoost = false;
    ApplyDebugFlySettings(bDebugFlyMode);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("Debug Fly Mode: %s"),
        bDebugFlyMode ? TEXT("ON") : TEXT("OFF"));
}


void AVoxelWorld::DebugFlyUpPressed()
{
    if (bDebugFlyMode)
    {
        DebugFlyVerticalInput = 1.0f;
    }
}


void AVoxelWorld::DebugFlyUpReleased()
{
    if (bDebugFlyMode && DebugFlyVerticalInput > 0.0f)
    {
        DebugFlyVerticalInput = 0.0f;
    }
}


void AVoxelWorld::DebugFlyDownPressed()
{
    if (bDebugFlyMode)
    {
        DebugFlyVerticalInput = -1.0f;
    }
}


void AVoxelWorld::DebugFlyDownReleased()
{
    if (bDebugFlyMode && DebugFlyVerticalInput < 0.0f)
    {
        DebugFlyVerticalInput = 0.0f;
    }
}


void AVoxelWorld::DebugFlyBoostPressed()
{
    if (bDebugFlyMode)
    {
        bDebugFlyBoost = true;
        ApplyDebugFlySettings(true);
    }
}


void AVoxelWorld::DebugFlyBoostReleased()
{
    bDebugFlyBoost = false;

    if (bDebugFlyMode)
    {
        ApplyDebugFlySettings(true);
    }
}


void AVoxelWorld::ApplyDebugFlySettings(bool bEnable)
{
    UWorld* World = GetWorld();

    if (!World)
    {
        return;
    }

    APawn* Pawn =
        UGameplayStatics::GetPlayerPawn(
            World,
            0);

    ACharacter* Character =
        Cast<ACharacter>(Pawn);

    if (!Character)
    {
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    if (!Movement)
    {
        return;
    }

    if (bEnable)
    {
        APlayerController* PC =
            World->GetFirstPlayerController();

        if (PC)
        {
            PC->bShowMouseCursor = false;
            FInputModeGameOnly GameOnlyInput;
            PC->SetInputMode(GameOnlyInput);
        }

        Movement->SetMovementMode(MOVE_Flying);
        Movement->MaxFlySpeed = bDebugFlyBoost ? 9000.0f : 3000.0f;
        Movement->MaxAcceleration = 12000.0f;
        Movement->BrakingDecelerationFlying = 12000.0f;
        Movement->GravityScale = 0.0f;

    }
    else
    {
        Movement->SetMovementMode(MOVE_Walking);
        Movement->GravityScale = 1.0f;
        DebugFlyVerticalInput = 0.0f;
        bDebugFlyBoost = false;
    }
}


void AVoxelWorld::ClearFarLOD()
{
    ++FarLODGenerationVersion;

    UProceduralMeshComponent* FarMeshes[] =
    {
        FarLOD1Mesh,
        FarLOD2Mesh,
        FarLOD3Mesh,
        FarLOD4Mesh
    };

    for (UProceduralMeshComponent* Mesh : FarMeshes)
    {
        if (Mesh)
        {
            Mesh->ClearMeshSection(0);
            Mesh->ClearMeshSection(1);
            Mesh->DestroyComponent();
        }
    }

    FarLOD1Mesh = nullptr;
    FarLOD2Mesh = nullptr;
    FarLOD3Mesh = nullptr;
    FarLOD4Mesh = nullptr;

    bFarLODInitialized = false;
    LastFarLODCenter = FIntVector::ZeroValue;
}


namespace
{
    UProceduralMeshComponent* CreateFarLODMeshComponent(
        AVoxelWorld* Owner,
        const TCHAR* Name,
        UMaterialInterface* Material,
        UMaterialInterface* WaterMaterial)
    {
        if (!Owner)
        {
            return nullptr;
        }

        UProceduralMeshComponent* Mesh =
            NewObject<UProceduralMeshComponent>(
                Owner,
                Name);

        if (!Mesh)
        {
            return nullptr;
        }

        /*
         * Far LOD is generated and replaced at runtime.
         * Keep the same movable lighting path as normal voxel chunks;
         * Static mobility can make runtime procedural meshes receive
         * noticeably darker/different lighting.
         */
        Mesh->SetMobility(
            EComponentMobility::Movable);

        Mesh->SetCollisionEnabled(
            ECollisionEnabled::NoCollision);

        Mesh->SetGenerateOverlapEvents(
            false);

        Mesh->SetMaterial(0, Material);
        Mesh->SetMaterial(1, WaterMaterial);

        Mesh->RegisterComponent();

        return Mesh;
    }
}


void AVoxelWorld::UpdateFarLOD(
    const FIntVector& CenterChunk)
{
    ++FarLODGenerationVersion;

    const uint32 LocalGeneration =
        FarLODGenerationVersion;

    if (!FarLOD1Mesh)
    {
        FarLOD1Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD1"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD2Mesh)
    {
        FarLOD2Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD2"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD3Mesh)
    {
        FarLOD3Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD3"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD4Mesh)
    {
        FarLOD4Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD4"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD1Mesh ||
        !FarLOD2Mesh ||
        !FarLOD3Mesh ||
        !FarLOD4Mesh)
    {
        return;
    }

    const FVoxelWorldGenerator GeneratorCopy =
        WorldGenerator;

    const int32 LODWorldSizeX =
        WorldSizeX;

    const int32 LODWorldSizeY =
        WorldSizeY;

    const int32 LODChunkSize =
        ChunkSize;

    const float LODVoxelSize =
        VoxelSize;

    const int32 LODBeachWidth =
        BeachWidth;

    const int32 LODSeaLevel =
        SeaLevel;

    TWeakObjectPtr<AVoxelWorld> WeakWorld(this);

    /*
     * Builds one lightweight mesh for a complete ring.
     * No AVoxelChunk actors are created in this path.
     *
     * IMPORTANT:
     * Capture local copies of all AVoxelWorld properties.
     * Capturing class fields directly would require capturing this,
     * which is not allowed by this C++ lambda capture form.
     */
    auto ScheduleLOD =
        [
            WeakWorld,
            GeneratorCopy,
            CenterChunk,
            LODWorldSizeX,
            LODWorldSizeY,
            LODChunkSize,
            LODVoxelSize,
            LODBeachWidth,
            LODSeaLevel,
            LocalGeneration
        ](
            UProceduralMeshComponent* Mesh,
            int32 SampleStep,
            int32 InnerRadius,
            int32 OuterRadius)
        {
            if (!Mesh || InnerRadius >= OuterRadius)
            {
                return;
            }

            FVoxelTerrainLODBuildInput BuildInput;

            BuildInput.Generator =
                GeneratorCopy;

            BuildInput.WorldSizeX =
                LODWorldSizeX;

            BuildInput.WorldSizeY =
                LODWorldSizeY;

            BuildInput.ChunkSize =
                LODChunkSize;

            BuildInput.VoxelSize =
                LODVoxelSize;

            BuildInput.BeachWidth =
                LODBeachWidth;

            BuildInput.SeaLevel =
                LODSeaLevel;

            BuildInput.CenterChunk =
                CenterChunk;

            BuildInput.SampleStep =
                SampleStep;

            BuildInput.InnerRadiusChunks =
                InnerRadius;

            BuildInput.OuterRadiusChunks =
                OuterRadius;

            TWeakObjectPtr<UProceduralMeshComponent> WeakMesh(
                Mesh);

            Async(
                EAsyncExecution::ThreadPool,
                [
                    BuildInput = MoveTemp(BuildInput),
                    WeakMesh,
                    WeakWorld,
                    LocalGeneration
                ]() mutable
                {
                    FVoxelTerrainLODMeshOutput Output;

                    FVoxelTerrainLODMesher::Build(
                        BuildInput,
                        Output);

                    AsyncTask(
                        ENamedThreads::GameThread,
                        [
                            Output = MoveTemp(Output),
                            WeakMesh,
                            WeakWorld,
                            LocalGeneration
                        ]() mutable
                        {
                            if (!WeakWorld.IsValid() ||
                                WeakWorld->FarLODGenerationVersion !=
                                    LocalGeneration ||
                                !WeakMesh.IsValid())
                            {
                                return;
                            }

                            UProceduralMeshComponent* Mesh =
                                WeakMesh.Get();

                            if (!Mesh)
                            {
                                return;
                            }

                            Mesh->ClearMeshSection(0);
                            Mesh->ClearMeshSection(1);

                            if (Output.Vertices.Num() > 0 &&
                                Output.Triangles.Num() > 0)
                            {
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
                                    TArray<FProcMeshTangent>(),
                                    false);
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
                                    TArray<FProcMeshTangent>(),
                                    false);
                            }
                        });
                });
        };

    /*
     * Replacing the complete mesh section on update keeps the ring
     * deterministic and prevents stale geometry from previous centers.
     */
    /*
     * Sample the generated height field more densely at every LOD tier.
     * The nearest far ring uses one-block spacing to match the full terrain
     * silhouette closely; distant rings progressively decimate the surface.
     * Every step divides ChunkSize, keeping ring boundaries grid-aligned.
     */
    ScheduleLOD(
        FarLOD1Mesh,
        1,
        StreamingRadius + 1,
        LOD1Radius);

    ScheduleLOD(
        FarLOD2Mesh,
        2,
        LOD1Radius + 1,
        LOD2Radius);

    ScheduleLOD(
        FarLOD3Mesh,
        4,
        LOD2Radius + 1,
        LOD3Radius);

    ScheduleLOD(
        FarLOD4Mesh,
        8,
        LOD3Radius + 1,
        LOD4Radius);
}

