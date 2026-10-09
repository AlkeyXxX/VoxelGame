#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "VoxelTypes.generated.h"

class UMaterialInterface;
class USoundBase;

UENUM(BlueprintType)
enum class EVoxelBlock : uint8
{
    Air   = 0,
    Grass = 1,
    Dirt  = 2,
    Stone = 3,
    Sand  = 4,
    Wood  = 5,
    Water = 6,
    Snow  = 7
};

USTRUCT(BlueprintType)
struct SMOOTHVOXEL_API FVoxelBlockDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    EVoxelBlock Block = EVoxelBlock::Air;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    float Durability = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    bool bIsSolid = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    bool bCanBreak = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    bool bCanPlace = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    bool bIsLiquid = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    EVoxelBlock DropBlock = EVoxelBlock::Air;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    FName MaterialSlot = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block|Audio")
    TSoftObjectPtr<USoundBase> BreakSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block|Audio")
    TSoftObjectPtr<USoundBase> PlaceSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block|Rendering")
    TSoftObjectPtr<UMaterialInterface> Material;
};

FORCEINLINE bool IsVoxelSolid(EVoxelBlock Block)
{
    return Block != EVoxelBlock::Air &&
           Block != EVoxelBlock::Water;
}

FORCEINLINE bool IsVoxelSolid(uint8 Block)
{
    return IsVoxelSolid(static_cast<EVoxelBlock>(Block));
}
