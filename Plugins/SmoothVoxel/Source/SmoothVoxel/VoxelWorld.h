#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelTypes.h"
#include "VoxelWorld.generated.h"

class UMaterialInterface;
class UDataTable;

/**
 * Compatibility shim for serialized legacy Blueprints only.
 *
 * The voxel-block backend has been retired. This class intentionally does not
 * generate terrain, stream chunks, edit blocks, or save/load world state.
 * Use ASmoothVoxelOctreeWorld for all active world functionality.
 */
UCLASS()
class SMOOTHVOXEL_API AVoxelWorld : public AActor
{
    GENERATED_BODY()

public:
    AVoxelWorld();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Retained for old Blueprint serialization; the legacy backend is permanently disabled. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Legacy",
        meta=(DeprecatedProperty, DeprecationMessage="Use ASmoothVoxelOctreeWorld."))
    bool bEnableLegacyWorld = false;

    UFUNCTION(BlueprintCallable, Category="Voxel")
    void GenerateWorld();

    UFUNCTION(BlueprintCallable, Category="Voxel|Save")
    void SaveWorld();

    UFUNCTION(BlueprintCallable, Category="Voxel|Save")
    bool LoadWorld();

    UFUNCTION(BlueprintCallable, Category="Voxel")
    void RemoveBlockAtWorld(const FVector& WorldPosition);

    UFUNCTION(BlueprintCallable, Category="Voxel")
    void SetBlockAtWorld(const FVector& WorldPosition, EVoxelBlock Block);

    UFUNCTION(BlueprintCallable, Category="Voxel")
    bool BreakBlockByRay();

    UFUNCTION(BlueprintCallable, Category="Voxel")
    bool PlaceBlockByRay();

    UFUNCTION(BlueprintCallable, Category="Voxel")
    bool PlaceBlockByRayWithType(EVoxelBlock BlockToPlace);

    UFUNCTION(BlueprintCallable, Category="Voxel|Save")
    void SetPersistentObjectState(int64 ObjectId, uint8 State);

    UFUNCTION(BlueprintPure, Category="Voxel|Save")
    bool GetPersistentObjectState(int64 ObjectId, uint8& OutState) const;

    // Deprecated serialized properties: preserved so existing Blueprint packages
    // can be loaded while their actors/nodes are migrated to ASmoothVoxelOctreeWorld.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="256"))
    int32 WorldSizeX = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="256"))
    int32 WorldSizeY = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World",
        meta=(ClampMin="1", ClampMax="32"))
    int32 WorldSizeZ = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="16"))
    int32 StreamingRadius = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="20"))
    int32 UnloadRadius = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ClampMin="1", ClampMax="8"))
    int32 MaxChunksPerTick = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World",
        meta=(ClampMin="4", ClampMax="64"))
    int32 ChunkSize = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World",
        meta=(ClampMin="1.0"))
    float VoxelSize = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    int32 BaseHeight = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    int32 HeightVariation = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    float NoiseScale = 0.025f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
    float TemperatureScale = 0.006f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
    float MoistureScale = 0.008f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    int32 SeaLevel = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water",
        meta=(ClampMin="0", ClampMax="8"))
    int32 BeachWidth = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Rendering")
    UMaterialInterface* Material = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Rendering")
    UMaterialInterface* WaterMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Player")
    float InteractionDistance = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Blocks")
    UDataTable* BlockDataTable = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Save")
    FString SaveSlotName = TEXT("VoxelWorld_Save");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Save")
    int32 SaveUserIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Save",
        meta=(ClampMin="0.0"))
    float AutoSaveInterval = 60.0f;
};
