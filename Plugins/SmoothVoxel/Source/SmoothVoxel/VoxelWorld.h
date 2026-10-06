
#pragma once

#include "CoreMinimal.h" 
#include "GameFramework/Actor.h" 
#include "VoxelTypes.h" 
#include "VoxelMesher.h"
#include "VoxelWorldGenerator.h"
#include "VoxelWorld.generated.h"


class AVoxelChunk;
class UMaterialInterface;


UCLASS()
class SMOOTHVOXEL_API AVoxelWorld : public AActor
{
    GENERATED_BODY()


public:

    AVoxelWorld();

    virtual void BeginPlay() override;


    /*
     * Создание мира.
     */
    UFUNCTION(
        BlueprintCallable,
        Category="Voxel")
    void GenerateWorld();


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

    /*
     * Количество чанков по каждой оси.
     *
     * Сейчас оставляем текущую модель. В дальнейшем
     * этот набор будет описывать активную область мира.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="256"))
    int32 WorldSizeX = 4;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="256"))
    int32 WorldSizeY = 4;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="32"))
    int32 WorldSizeZ = 1;


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
     * Материал voxel mesh.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Rendering")
    UMaterialInterface* Material = nullptr;


    /*
     * Дальность взаимодействия.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Player")
    float InteractionDistance = 1000.0f;


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
     * Настроить генератор из текущих UPROPERTY.
     */
    void ConfigureWorldGenerator();


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
};

