
#pragma once

#include "CoreMinimal.h" 
#include "GameFramework/Actor.h" 
#include "VoxelTypes.h" 
#include "VoxelMesher.h"
#include "VoxelMarchingCubesMesher.h"
#include "VoxelWorldGenerator.h"
#include "VoxelTerrainLODMesher.h"
#include "HAL/ThreadSafeBool.h"
#include "VoxelWorld.generated.h"


class AVoxelChunk;
class FVoxelRWGPlanner;
class UMaterialInterface;
class UVoxelWorldSaveGame;
class UDataTable;
class UProceduralMeshComponent;


/*
 * Deterministic terrain cut/fill stamp for one XY road/shoulder column.
 * SurfaceZ is a world-block height; dry road columns are flattened to this
 * height while adjacent shoulder columns blend back to the native terrain.
 */
struct FVoxelRWGRoadStamp
{
    float SurfaceZ = -1.0f;
    uint8 SurfaceBlock = uint8(EVoxelBlock::Dirt);
    uint8 FillBlock = uint8(EVoxelBlock::Dirt);
    uint8 Priority = 0;
    bool bRoadSurface = true;
};

/*
 * Immutable inputs needed to build the Marching Cubes halo.
 * Captured on the Game Thread, evaluated on a worker thread without
 * reading Actors, Components, or AVoxelWorld containers.
 */
struct FVoxelMarchingCubesDataSnapshot
{
    FVoxelWorldGenerator Generator;
    FIntVector ChunkCoord = FIntVector::ZeroValue;
    int32 ChunkSize = 0;
    int32 WorldSizeX = 0;
    int32 WorldSizeY = 0;
    int32 WorldSizeZ = 0;
    int32 SeaLevel = 0;
    int32 BeachWidth = 0;
    float VoxelSize = 100.0f;
    float UVScalePerBlock = 0.5f;

    TMap<FIntVector, TMap<int32, uint8>> ChunkModifications;
    TSharedPtr<TMap<FIntPoint, FVoxelRWGRoadStamp>, ESPMode::ThreadSafe> RoadStamps;
    TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> CancellationToken;

    void Build(FVoxelMarchingCubesBuildInput& OutData) const;
};


/* Mesh results are applied on the Game Thread with a per-frame budget. */
struct FPendingVoxelChunkMeshResult
{
    TWeakObjectPtr<AVoxelChunk> Chunk;
    FVoxelMeshBuildOutput Output;
    uint32 Version = 0;
};

struct FPendingVoxelLODMeshResult
{
    TWeakObjectPtr<UProceduralMeshComponent> Mesh;
    FVoxelTerrainLODMeshOutput Output;
    uint32 GenerationVersion = 0;
};


UCLASS()
class SMOOTHVOXEL_API AVoxelWorld : public AActor
{
    GENERATED_BODY()


public:

    AVoxelWorld();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


    /*
     * Создание мира.
     */
    UFUNCTION(
        BlueprintCallable,
        Category="Voxel")
    void GenerateWorld();

    /*
     * Build a deterministic settlement/POI/road plan and export it to CSV.
     * Dry roads are stamped into voxel terrain and far LOD; the procedural
     * road mesh is reserved for bridge decks. This is an explicit F2 action.
     */
    UFUNCTION(BlueprintCallable, Category="Voxel|RWG")
    bool GenerateRWGLayoutAndExport();

    UFUNCTION(BlueprintCallable, Category="Voxel|Save")
    void SaveWorld();

    UFUNCTION(BlueprintCallable, Category="Voxel|Save")
    bool LoadWorld();


    /*
     * Работа с блоками через World Position.
     */
    UFUNCTION(
        BlueprintCallable,
        Category="Voxel")
    void RemoveBlockAtWorld(
        const FVector& WorldPosition);


    UFUNCTION(
        BlueprintCallable,
        Category="Voxel")
    void SetBlockAtWorld(
        const FVector& WorldPosition,
        EVoxelBlock Block);


    /*
     * Игровое взаимодействие.
     */
    UFUNCTION(
        BlueprintCallable,
        Category="Voxel")
    bool BreakBlockByRay();


    UFUNCTION(
        BlueprintCallable,
        Category="Voxel")
    bool PlaceBlockByRay();


    UFUNCTION(
        BlueprintCallable,
        Category="Voxel")
    bool PlaceBlockByRayWithType(
        EVoxelBlock BlockToPlace);


    /*
     * Получение чанка.
     *
     * ВАЖНО:
     * Здесь больше нет const.
     */
    AVoxelChunk* GetChunk(
        const FIntVector& ChunkCoord);

    /*
     * Recommended player start: exact center of the map, above generated
     * terrain. The center remains inside the green macro-biome band.
     */
    FVector GetCenterSpawnLocation();


    /*
     * Формирование snapshot границ соседей.
     */
    void BuildNeighborData(
        const FIntVector& ChunkCoord,
        FVoxelNeighborData& OutData) const;

    void BuildMarchingCubesData(
        const FIntVector& ChunkCoord,
        FVoxelMarchingCubesBuildInput& OutData) const;

    void CaptureMarchingCubesDataSnapshot(
        const FIntVector& ChunkCoord,
        FVoxelMarchingCubesDataSnapshot& OutSnapshot) const;

    void QueueChunkMeshResult(
        AVoxelChunk* Chunk,
        FVoxelMeshBuildOutput&& Output,
        uint32 Version);

    void QueueFarLODMeshResult(
        UProceduralMeshComponent* Mesh,
        FVoxelTerrainLODMeshOutput&& Output,
        uint32 GenerationVersion);

    /*
     * Debug helpers.
     *
     * Эти функции не меняют состояние мира. Они дают debug-компоненту
     * доступ к уже существующим данным без дублирования логики
     * преобразования координат и генерации terrain/biome.
     */
    bool GetBlockDebugInfoAtWorld(
        const FVector& WorldPosition,
        EVoxelBlock& OutBlock,
        FIntVector& OutWorldBlock,
        FIntVector& OutLocalBlock,
        FIntVector& OutChunkCoord) const;

    int32 GetLoadedChunkCount() const
    {
        return Chunks.Num();
    }

    const FIntVector& GetStreamingCenterChunkDebug() const
    {
        return LastStreamingCenter;
    }

    void GetTerrainDebugInfo(
        int32 WorldX,
        int32 WorldY,
        int32& OutSurfaceHeight,
        float& OutTerrainNoise,
        float& OutTemperature,
        float& OutMoisture,
        FString& OutBiomeName) const;


public:

    UFUNCTION(BlueprintCallable, Category="Voxel|Save")
    void SetPersistentObjectState(int64 ObjectId, uint8 State);

    UFUNCTION(BlueprintPure, Category="Voxel|Save")
    bool GetPersistentObjectState(int64 ObjectId, uint8& OutState) const;


    /*
     * Количество чанков по каждой оси.
     *
     * Сейчас оставляем текущую модель. В дальнейшем
     * этот набор описывает размер генерируемой области мира.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="256"))
    int32 WorldSizeX = 128;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="256"))
    int32 WorldSizeY = 128;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="32"))
    int32 WorldSizeZ = 2;


    /*
     * Радиус чанков вокруг игрока, которые держим загруженными.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="16"))
    int32 StreamingRadius = 3;

    /* Дальность LOD1 в чанках. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="32"))
    int32 LOD1Radius = 6;

    /* Дальность LOD2 в чанках. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="64"))
    int32 LOD2Radius = 16;

    /* Дальность LOD3 в чанках. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="128"))
    int32 LOD3Radius = 32;

    /* Дальность LOD4 в чанках. Самое дальнее и самое дешёвое кольцо. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="256"))
    int32 LOD4Radius = 64;

    /*
     * Как часто перестраивать дальние LOD при движении по чанкам.
     * 1 держит границы LOD синхронными с Full чанками и исключает
     * временные пустые полосы при переходе между соседними chunks.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="8"))
    int32 LODUpdateChunkInterval = 1;

    /*
     * Радиус, после которого Full chunk можно выгрузить.
     * Дальний terrain при этом продолжает жить как отдельный LOD mesh.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="20"))
    int32 UnloadRadius = 3;


    /*
     * Максимальное количество новых chunks за один Tick.
     *
     * Маленькое значение уменьшает скачки FPS.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="8"))
    int32 MaxChunksPerTick = 1;

    /* Limit synchronous mesh-input preparation to smooth out streaming hitches. */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="2"))
    int32 MaxChunkMeshRebuildsPerTick = 2;

    /* Limit expensive chunk/LOD ProceduralMesh uploads on the Game Thread. */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="2"))
    int32 MaxMeshSectionUploadsPerFrame = 1;


    /*
     * Размер чанка.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="4", ClampMax="64"))
    int32 ChunkSize = 32;


    /*
     * Размер одного блока в Unreal units.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1.0"))
    float VoxelSize = 100.0f;


    /*
     * Seed.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG", meta=(ClampMin="1", ClampMax="64"))
    int32 RWGTargetSettlementCount = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG", meta=(ClampMin="0", ClampMax="512"))
    int32 RWGTargetPOICount = 72;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG", meta=(ClampMin="8", ClampMax="128"))
    int32 RWGGridSpacing = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG", meta=(ClampMin="128", ClampMax="2048"))
    int32 RWGCellSizeBlocks = 512;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG")
    bool bDrawRWGCellOverlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG")
    bool bDrawRWGDebugPreview = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG", meta=(ClampMin="1.0"))
    float RWGDebugDrawDuration = 60.0f;

    // When enabled, generate the procedural mesh for water-crossing bridge decks.
    // Dry roads are part of the Marching Cubes terrain, not floating ribbons.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG|Roads")
    bool bBuildRWGRoadSurface = true;

    // Collision lets the player walk/drive over road ribbons and bridge decks.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG|Roads")
    bool bEnableRWGRoadCollision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG|Roads")
    UMaterialInterface* RWGRoadMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG|Roads", meta=(ClampMin="1.0", ClampMax="20.0"))
    float RWGRoadWidthMainBlocks = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG|Roads", meta=(ClampMin="1.0", ClampMax="20.0"))
    float RWGRoadWidthConnectorBlocks = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG|Roads", meta=(ClampMin="1.0", ClampMax="16.0"))
    float RWGRoadWidthRuralBlocks = 6.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|RWG|Roads", meta=(ClampMin="1.0", ClampMax="12.0"))
    float RWGRoadWidthLocalBlocks = 3.0f;


    /*
     * Базовая высота поверхности.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain")
    int32 BaseHeight = 12;


    /*
     * Амплитуда изменения высоты.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain")
    int32 HeightVariation = 8;


    /*
     * Масштаб Perlin Noise.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain")
    float NoiseScale = 0.025f;

    /*
     * Более мелкий шум поверхности.
     * Маленькая амплитуда добавляет естественную неровность,
     * не ломая основную форму рельефа.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain",
        meta=(ClampMin="0.001"))
    float DetailNoiseScale = 0.08f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain",
        meta=(ClampMin="0", ClampMax="8"))
    int32 DetailHeightVariation = 2;

    /*
     * Масштаб широких зон плато.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain",
        meta=(ClampMin="0.001"))
    float PlateauScale = 0.007f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain",
        meta=(ClampMin="1", ClampMax="8"))
    int32 PlateauHeightStep = 2;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Terrain",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float PlateauStrength = 0.68f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
    float TemperatureScale = 0.006f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
    float MoistureScale = 0.008f;

    /*
     * Уровень моря в мировых блоках.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Water")
    int32 SeaLevel = 10;

    /*
     * Ширина пляжной зоны в блоках.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Water",
        meta=(ClampMin="0", ClampMax="8"))
    int32 BeachWidth = 2;


    /*
     * Материал voxel mesh.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Rendering")
    UMaterialInterface* Material = nullptr;

    /*
     * UV tiling density for the full-resolution terrain. With the default
     * voxel size of 100 cm, 0.5 means one texture repeat per 2 metres.
     * Choose materials whose UVs use TextureCoordinate rather than world-aligned mapping.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Rendering",
        meta=(ClampMin="0.01", ClampMax="4.0"))
    float TerrainUVScalePerBlock = 0.5f;

    /*
     * Отдельный материал воды.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Rendering")
    UMaterialInterface* WaterMaterial = nullptr;


    /*
     * Дальность взаимодействия.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Player")
    float InteractionDistance = 1000.0f;


    /*
     * Таблица свойств блоков.
     *
     * Если не назначена, используются встроенные значения
     * из UVoxelBlockLibrary.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Blocks")
    UDataTable* BlockDataTable = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Save")
    FString SaveSlotName = TEXT("VoxelWorld_Save");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Save")
    int32 SaveUserIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Save", meta=(ClampMin="0.0"))
    float AutoSaveInterval = 60.0f;


private:

    /*
     * Все chunks мира.
     */
    UPROPERTY()
    TMap<FIntVector, AVoxelChunk*> Chunks;


    /*
     * Процедурный генератор данных мира.
     */
    FVoxelWorldGenerator WorldGenerator;


    /*
     * Изменения игрока.
     *
     * Храним только изменённые блоки, а не весь chunk.
     * Поэтому выгрузка чанка не уничтожает изменения.
     *
     * Key первого уровня  = координата чанка.
     * Key второго уровня   = локальный индекс блока.
     * Value                 = новый тип блока.
     */
    TMap<FIntVector, TMap<int32, uint8>> ModifiedBlocks;

    /* Unique chunk coordinates waiting for their mesh input to be snapshotted. */
    TArray<FIntVector> PendingChunkMeshRebuilds;
    TArray<FPendingVoxelChunkMeshResult> PendingChunkMeshResults;
    TArray<FPendingVoxelLODMeshResult> PendingVoxelLODMeshResults;

    TMap<int64, uint8> PersistentObjectStates;
    float TimeSinceLastAutoSave = 0.0f;
    float TimeSinceLastStreamingUpdate = 0.0f;


    /*
     * Настроить генератор из текущих UPROPERTY.
     */
    void ConfigureWorldGenerator();
    void ApplyLoadedPersistenceToLoadedChunks();


    /*
     * Генерация блоков одного чанка.
     */
    void GenerateChunkBlocks(
        AVoxelChunk* Chunk);

    void GenerateChunkBlocksAsync(
        AVoxelChunk* Chunk);


    /*
     * Изменение блока.
     */
    void SetBlockInternal(
        const FIntVector& WorldBlock,
        uint8 Block);


    /*
     * World Position -> world block.
     */
    bool WorldToBlock(
        const FVector& WorldPosition,
        FIntVector& OutBlock) const;


    /*
     * World block -> chunk coordinate.
     */
    FIntVector WorldBlockToChunk(
        const FIntVector& WorldBlock) const;


    /*
     * World block -> local block.
     */
    FIntVector WorldBlockToLocal(
        const FIntVector& WorldBlock) const;


    /*
     * Перестроить изменённый chunk
     * и шесть соседей.
     */
    void RebuildChunkAndNeighbors(
        const FIntVector& ChunkCoord);

    void QueueChunkNeighborhoodRebuilds(
        const FIntVector& ChunkCoord);
    void QueueChunkFaceNeighborRebuilds(
        const FIntVector& ChunkCoord);

    void ProcessPendingChunkMeshRebuilds();
    void ProcessPendingMeshUploads();


    /*
     * Streaming.
     */
    bool GetStreamingCenterChunk(
        FIntVector& OutChunkCoord) const;

    bool IsChunkInsideWorld(
        const FIntVector& ChunkCoord) const;

    AVoxelChunk* CreateChunk(
        const FIntVector& ChunkCoord);

    void UpdateChunkStreaming();
    void UpdateFarLOD(const FIntVector& CenterChunk);
    void ClearFarLOD();
    bool IsPositionInsideWater(const FVector& WorldPosition) const;
    void UpdateUnderwaterEffect();

    void SetPersistentObjectStateInternal(int64 ObjectId, uint8 State);

    bool bStreamingInitialized = false;

    FIntVector LastStreamingCenter =
        FIntVector::ZeroValue;

    bool bUnderwaterEffectActive = false;

    UPROPERTY(Transient)
    UProceduralMeshComponent* RWGRoadMesh = nullptr;

    TSharedPtr<TMap<FIntPoint, FVoxelRWGRoadStamp>, ESPMode::ThreadSafe> RWGRoadSurfaceStamps;
    TSharedPtr<TMap<FIntPoint, float>, ESPMode::ThreadSafe> RWGRoadLODHeights;

    void BuildRWGRoadTerrainStamps(const FVoxelRWGPlanner& Planner);
    void BuildRWGRoadSurface(const FVoxelRWGPlanner& Planner);
    void ClearRWGRoadSurface();

    UPROPERTY()
    UProceduralMeshComponent* FarLOD1Mesh = nullptr;

    UPROPERTY()
    UProceduralMeshComponent* FarLOD2Mesh = nullptr;

    UPROPERTY()
    UProceduralMeshComponent* FarLOD3Mesh = nullptr;

    UPROPERTY()
    UProceduralMeshComponent* FarLOD4Mesh = nullptr;

    uint32 FarLODGenerationVersion = 0;
    TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> FarLODCancellationToken;
    FIntVector LastFarLODCenter = FIntVector::ZeroValue;
    bool bFarLODInitialized = false;

    bool bDebugFlyMode = false;
    bool bDebugFlyBoost = false;
    float DebugFlyVerticalInput = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Debug Fly",
        meta=(ClampMin="100.0", ClampMax="100000.0"))
    float DebugFlySpeed = 6000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Debug Fly",
        meta=(ClampMin="100.0", ClampMax="200000.0"))
    float DebugFlyBoostSpeed = 18000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Debug Fly",
        meta=(ClampMin="100.0", ClampMax="200000.0"))
    float DebugFlyAcceleration = 24000.0f;

    UFUNCTION()
    void ToggleDebugFly();
    void GenerateRWGLayoutFromInput();

    void DebugFlyUpPressed();
    void DebugFlyUpReleased();
    void DebugFlyDownPressed();
    void DebugFlyDownReleased();
    void DebugFlyBoostPressed();
    void DebugFlyBoostReleased();
    void ApplyDebugFlySettings(bool bEnable);
};

