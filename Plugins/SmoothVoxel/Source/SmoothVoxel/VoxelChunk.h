
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


    /*
     * Инициализация чанка.
     */
    void InitializeChunk(
        AVoxelWorld* InWorld,
        const FIntVector& InChunkCoord);

    /* Bulk-apply generated data on the Game Thread after worker generation. */
    void SetGeneratedData(
        TArray<uint8>&& InBlocks,
        TArray<uint8>&& InBaseBlocks,
        TArray<uint8>&& InBiomes,
        TArray<uint8>&& InModificationFlags);

    bool HasGeneratedData() const
    {
        return bGeneratedDataReady;
    }

    uint32 GetDataGenerationVersion() const
    {
        return DataGenerationVersion;
    }

    uint32 GetMeshGenerationVersion() const
    {
        return MeshGenerationVersion;
    }


    /*
     * Работа с блоками.
     */
    uint8 GetBlock(
        int32 X,
        int32 Y,
        int32 Z) const;

    uint8 GetTerrainBlock(
        int32 X,
        int32 Y,
        int32 Z) const;

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

    void SetBlock(
        int32 X,
        int32 Y,
        int32 Z,
        uint8 Block);

    void RemoveBlock(
        int32 X,
        int32 Y,
        int32 Z);


    /*
     * Перестроение полноценного mesh.
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

    void CopyXMinusStructure(TArray<uint8>& OutData) const;
    void CopyXPlusStructure(TArray<uint8>& OutData) const;
    void CopyYMinusStructure(TArray<uint8>& OutData) const;
    void CopyYPlusStructure(TArray<uint8>& OutData) const;
    void CopyZMinusStructure(TArray<uint8>& OutData) const;
    void CopyZPlusStructure(TArray<uint8>& OutData) const;


    /*
     * Материал.
     */
    void SetVoxelMaterial(
        UMaterialInterface* InMaterial);

    void SetWaterMaterial(
        UMaterialInterface* InMaterial);

    void SetRoadMaterial(
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

    UPROPERTY()
    UMaterialInterface* RoadMaterial = nullptr;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel")
    UMaterialInterface* WaterMaterial = nullptr;


private:

    friend class AVoxelWorld;

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
    TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> MeshBuildCancellationToken;
    TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> DataGenerationCancellationToken;
    uint32 DataGenerationVersion = 0;
    bool bGeneratedDataReady = false;



    int32 BlockIndex(
        int32 X,
        int32 Y,
        int32 Z) const;


    void ApplyMesh(
        FVoxelMeshBuildOutput&& Output,
        uint32 Version,
        bool bEnableCollision);
};

