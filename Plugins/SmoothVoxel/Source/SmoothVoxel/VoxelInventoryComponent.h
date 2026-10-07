#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "VoxelTypes.h"
#include "VoxelInventoryComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVoxelInventoryChanged);

USTRUCT(BlueprintType)
struct SMOOTHVOXEL_API FVoxelInventorySlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Inventory")
    EVoxelBlock Block = EVoxelBlock::Air;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Inventory")
    int32 Quantity = 0;

    bool IsEmpty() const
    {
        return Block == EVoxelBlock::Air || Quantity <= 0;
    }
};

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class SMOOTHVOXEL_API UVoxelInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoxelInventoryComponent();

    virtual void BeginPlay() override;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Inventory",
        meta=(ClampMin="1", ClampMax="36"))
    int32 SlotCount = 9;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Inventory",
        meta=(ClampMin="1", ClampMax="999"))
    int32 MaxStackSize = 64;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Voxel|Inventory")
    int32 SelectedSlot = 0;

    UPROPERTY(
        BlueprintAssignable,
        Category="Voxel|Inventory")
    FVoxelInventoryChanged OnInventoryChanged;

    UFUNCTION(BlueprintCallable, Category="Voxel|Inventory")
    int32 AddBlock(
        EVoxelBlock Block,
        int32 Quantity);

    UFUNCTION(BlueprintPure, Category="Voxel|Inventory")
    bool CanAddBlock(
        EVoxelBlock Block,
        int32 Quantity) const;

    UFUNCTION(BlueprintCallable, Category="Voxel|Inventory")
    int32 RemoveBlock(
        EVoxelBlock Block,
        int32 Quantity);

    UFUNCTION(BlueprintCallable, Category="Voxel|Inventory")
    int32 RemoveFromSelectedSlot(
        int32 Quantity);

    UFUNCTION(BlueprintCallable, Category="Voxel|Inventory")
    void SetSelectedSlot(
        int32 NewSelectedSlot);

    UFUNCTION(BlueprintPure, Category="Voxel|Inventory")
    int32 GetSelectedSlot() const;

    UFUNCTION(BlueprintPure, Category="Voxel|Inventory")
    EVoxelBlock GetSelectedBlock() const;

    UFUNCTION(BlueprintPure, Category="Voxel|Inventory")
    int32 GetSelectedQuantity() const;

    UFUNCTION(BlueprintPure, Category="Voxel|Inventory")
    FVoxelInventorySlot GetSlot(
        int32 Index) const;

    UFUNCTION(BlueprintPure, Category="Voxel|Inventory")
    int32 GetSlotCount() const;

    UFUNCTION(BlueprintPure, Category="Voxel|Inventory")
    bool IsSlotEmpty(
        int32 Index) const;

    UFUNCTION(BlueprintCallable, Category="Voxel|Inventory")
    void ClearInventory();

private:
    UPROPERTY()
    TArray<FVoxelInventorySlot> Slots;

    void EnsureSlotArray();
    void BroadcastInventoryChanged();
};
