#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "VoxelTypes.h"
#include "VoxelBlockLibrary.generated.h"

class UDataTable;

/*
 * Единый реестр параметров стандартных voxel-блоков.
 *
 * Все функции доступны из Blueprint, поэтому hotbar и UI позже
 * смогут работать с теми же данными, что и C++.
 */
UCLASS()
class SMOOTHVOXEL_API UVoxelBlockLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static FVoxelBlockDefinition GetBlockDefinition(
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static FVoxelBlockDefinition GetBlockDefinitionFromTable(
        const UDataTable* DataTable,
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static FText GetBlockDisplayName(
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static float GetBlockDurability(
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static float GetBlockDurabilityFromTable(
        const UDataTable* DataTable,
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static bool CanBreakBlock(
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static bool CanBreakBlockFromTable(
        const UDataTable* DataTable,
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static bool CanPlaceBlock(
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static bool CanPlaceBlockFromTable(
        const UDataTable* DataTable,
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static EVoxelBlock GetBlockDrop(
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static bool IsLiquidBlock(
        EVoxelBlock Block);

    UFUNCTION(BlueprintPure, Category="Voxel|Blocks")
    static bool IsSolidBlock(
        EVoxelBlock Block);
};
