#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "VoxelTypes.h"
#include "VoxelDebugComponent.generated.h"

class AVoxelWorld;

USTRUCT(BlueprintType)
struct SMOOTHVOXEL_API FVoxelDebugData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    bool bHasTargetBlock = false;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    EVoxelBlock TargetBlock = EVoxelBlock::Air;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FText TargetBlockName;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float TargetDistance = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FIntVector TargetWorldBlock = FIntVector::ZeroValue;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FIntVector TargetLocalBlock = FIntVector::ZeroValue;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FIntVector TargetChunk = FIntVector::ZeroValue;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    EVoxelBlock TargetDropBlock = EVoxelBlock::Air;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FText TargetDropBlockName;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float TargetDurability = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    bool bTargetIsSolid = false;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    bool bTargetCanBreak = false;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    bool bTargetCanPlace = false;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    bool bTargetIsLiquid = false;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FName TargetMaterialSlot = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FString TargetMaterialAsset;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FString TargetBreakSoundAsset;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FString TargetPlaceSoundAsset;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FVector PlayerLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FRotator PlayerRotation = FRotator::ZeroRotator;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FIntVector PlayerWorldBlock = FIntVector::ZeroValue;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FIntVector PlayerChunk = FIntVector::ZeroValue;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float FPS = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float FrameTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 LoadedChunkCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FIntVector StreamingCenterChunk = FIntVector::ZeroValue;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 SelectedSlot = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 InventorySlotCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 InventoryMaxStackSize = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    EVoxelBlock SelectedBlock = EVoxelBlock::Air;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FText SelectedBlockName;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 SelectedQuantity = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 WorldSeed = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 ChunkSize = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float VoxelSize = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 StreamingRadius = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 UnloadRadius = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 WorldSizeX = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 WorldSizeY = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 WorldSizeZ = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    int32 TargetSurfaceHeight = 0;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float TargetTerrainNoise = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float TargetTemperature = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float TargetMoisture = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    FString TargetBiome;

    UPROPERTY(BlueprintReadOnly, Category="Voxel|Debug")
    float InteractionDistance = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FVoxelDebugDataUpdated,
    const FVoxelDebugData&,
    Data);

UCLASS(
    ClassGroup=(Voxel),
    meta=(BlueprintSpawnableComponent))
class SMOOTHVOXEL_API UVoxelDebugComponent
    : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoxelDebugComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Debug")
    AVoxelWorld* VoxelWorld = nullptr;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Debug",
        meta=(ClampMin="0.05", ClampMax="2.0"))
    float RefreshInterval = 0.10f;

    UPROPERTY(
        BlueprintAssignable,
        Category="Voxel|Debug")
    FVoxelDebugDataUpdated OnDebugDataUpdated;

    UFUNCTION(BlueprintCallable, Category="Voxel|Debug")
    void ToggleDebug();

    UFUNCTION(BlueprintCallable, Category="Voxel|Debug")
    void SetDebugEnabled(bool bEnabled);

    UFUNCTION(BlueprintPure, Category="Voxel|Debug")
    bool IsDebugEnabled() const;

    UFUNCTION(BlueprintCallable, Category="Voxel|Debug")
    void RefreshDebugData();

    UFUNCTION(BlueprintPure, Category="Voxel|Debug")
    const FVoxelDebugData& GetDebugData() const;

private:
    UPROPERTY()
    FVoxelDebugData DebugData;

    bool bDebugEnabled = false;
    float RefreshAccumulator = 0.0f;
    float SmoothedFrameTime = 0.016f;

    class APawn* GetPlayerPawn() const;
    class APlayerController* GetPlayerController() const;

    void ResetTargetData();
    void RefreshPerformanceData(float DeltaTime);
};
