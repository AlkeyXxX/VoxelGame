
#pragma once

#include "CoreMinimal.h" 
#include "GameFramework/Actor.h" 
#include "VoxelTypes.h" 
#include "VoxelMesher.h"
#include "VoxelWorldGenerator.h"
#include "VoxelWorld.generated.h"


class AVoxelChunk;
class UMaterialInterface;
class UVoxelWorldSaveGame;
class UVoxelMainMenuWidget;


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


    /*
     * Получение чанка.
     *
     * ВАЖНО:
     * Здесь больше нет const.
     */
    AVoxelChunk* GetChunk(
        const FIntVector& ChunkCoord);


    /*
     * Формирование snapshot границ соседей.
     */
    void BuildNeighborData(
        const FIntVector& ChunkCoord,
        FVoxelNeighborData& OutData) const;


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
    int32 WorldSizeX = 8;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="256"))
    int32 WorldSizeY = 8;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="32"))
    int32 WorldSizeZ = 1;


    /*
     * Радиус чанков вокруг игрока, которые держим загруженными.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="16"))
    int32 StreamingRadius = 3;


    /*
     * Радиус, после которого чанк можно выгрузить.
     *
     * Должен быть больше StreamingRadius,
     * чтобы не было постоянного load/unload на границе.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="20"))
    int32 UnloadRadius = 4;


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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|UI")
    TSubclassOf<UVoxelMainMenuWidget> MainMenuClass;

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

    TMap<int64, uint8> PersistentObjectStates;
    float TimeSinceLastAutoSave = 0.0f;


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

    void SetPersistentObjectStateInternal(int64 ObjectId, uint8 State);
    void ShowMainMenu();

    UPROPERTY()
    UVoxelMainMenuWidget* MainMenuWidget = nullptr;


    bool bStreamingInitialized = false;

    FIntVector LastStreamingCenter =
        FIntVector::ZeroValue;
};

