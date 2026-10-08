
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "VoxelTypes.h"
#include "VoxelMesher.h"

#include "VoxelChunk.generated.h"


class UProceduralMeshComponent;
class UMaterialInterface;
class AVoxelWorld;


UCLASS()
class SMOOTHVOXEL_API AVoxelChunk : public AActor
{
    GENERATED_BODY()


public:

    AVoxelChunk();

    virtual void BeginPlay() override;


    /*
     * Инициализация чанка.
     */
    void InitializeChunk(
        AVoxelWorld* InWorld,
        const FIntVector& InChunkCoord);


    /*
     * Работа с блоками.
     */
    uint8 GetBlock(
        int32 X,
        int32 Y,
        int32 Z) const;

    /*
     * Блок, который принадлежит smooth terrain.
     * Любое player modification убирает эту ячейку из Surface Nets.
     */
    uint8 GetTerrainBlock(
        int32 X,
        int32 Y,
        int32 Z) const;

    void SetBlock(
        int32 X,
        int32 Y,
        int32 Z,
        uint8 Block);

    void SetBaseBlock(
        int32 X,
        int32 Y,
        int32 Z,
        uint8 Block);

    void SetModificationFlag(
        int32 X,
        int32 Y,
        int32 Z,
        bool bModified);

    void ClearModificationFlags();

    void RemoveBlock(
        int32 X,
        int32 Y,
        int32 Z);


    /*
     * Перестроение mesh.
     */
    void RebuildMesh();


    /*
     * Информация о чанке.
     */
    const FIntVector& GetChunkCoord() const
    {
        return ChunkCoord;
    }

    int32 GetChunkSize() const
    {
        return ChunkSize;
    }

    float GetVoxelSize() const
    {
        return VoxelSize;
    }


    /*
     * Копирование данных для worker thread.
     */
    void CopyBlockData(
        TArray<uint8>& OutData) const;

    void CopyBiomeData(
        TArray<uint8>& OutData) const;

    void CopyModificationFlags(
        TArray<uint8>& OutData) const;

    void SetBiome(
        int32 X,
        int32 Y,
        int32 Z,
        uint8 Biome);

    void CopyXMinus(
        TArray<uint8>& OutData) const;

    void CopyXPlus(
        TArray<uint8>& OutData) const;

    void CopyYMinus(
        TArray<uint8>& OutData) const;

    void CopyYPlus(
        TArray<uint8>& OutData) const;

    void CopyZMinus(
        TArray<uint8>& OutData) const;

    void CopyZPlus(
        TArray<uint8>& OutData) const;

    void CopyXMinusStructure(
        TArray<uint8>& OutData) const;

    void CopyXPlusStructure(
        TArray<uint8>& OutData) const;

    void CopyYMinusStructure(
        TArray<uint8>& OutData) const;

    void CopyYPlusStructure(
        TArray<uint8>& OutData) const;

    void CopyZMinusStructure(
        TArray<uint8>& OutData) const;

    void CopyZPlusStructure(
        TArray<uint8>& OutData) const;


    /*
     * Материал.
     */
    void SetVoxelMaterial(
        UMaterialInterface* InMaterial);

    void SetWaterMaterial(
        UMaterialInterface* InMaterial);


    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category="Voxel")
    UProceduralMeshComponent* Mesh;


protected:

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel",
        meta=(ClampMin="4", ClampMax="64"))
    int32 ChunkSize = 32;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel",
        meta=(ClampMin="1.0"))
    float VoxelSize = 100.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel")
    UMaterialInterface* Material = nullptr;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel")
    UMaterialInterface* WaterMaterial = nullptr;


private:

    AVoxelWorld* World = nullptr;

    FIntVector ChunkCoord =
        FIntVector::ZeroValue;


    TArray<uint8> Blocks;
    TArray<uint8> BaseBlocks;
    TArray<uint8> Biomes;
    TArray<uint8> ModificationFlags;


    /*
     * Каждое новое перестроение увеличивает Version.
     *
     * Если старый worker закончит позже нового,
     * его результат будет проигнорирован.
     */
    uint32 MeshGenerationVersion = 0;


    int32 BlockIndex(
        int32 X,
        int32 Y,
        int32 Z) const;


    void ApplyMesh(
        FVoxelMeshBuildOutput&& Output,
        uint32 Version);
};

