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
    Water = 6
};

/*
 * Данные одного типа блока.
 *
 * Эта структура является общей точкой описания блока для
 * будущих систем: инвентаря, hotbar, дропа, инструментов,
 * звуков и визуальных материалов.
 */
USTRUCT(BlueprintType)
struct SMOOTHVOXEL_API FVoxelBlockDefinition : public FTableRowBase
{
    GENERATED_BODY()

    /*
     * FVoxelBlockDefinition является строкой DataTable.
     * Row Name должен совпадать с именем EVoxelBlock:
     * Air, Grass, Dirt, Stone, Sand, Wood, Water.
     */

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    EVoxelBlock Block = EVoxelBlock::Air;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    FText DisplayName;

    /*
     * Прочность блока в условных единицах.
     * Позже она будет использоваться системой добычи/инструментов.
     */
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

    /*
     * Какой предмет/блок выпадает при разрушении.
     * Air означает отсутствие дропа.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    EVoxelBlock DropBlock = EVoxelBlock::Air;

    /*
     * Логическое имя материала.
     *
     * Сейчас renderer всё ещё использует общий material чанка,
     * поэтому это значение готовит систему к будущей привязке
     * конкретных материалов без переделки enum.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block")
    FName MaterialSlot = NAME_None;

    /*
     * Ассеты пока необязательны.
     * Позже сюда можно назначить индивидуальные звуки/материалы
     * прямо из DataTable или другого реестра.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block|Audio")
    TSoftObjectPtr<USoundBase> BreakSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block|Audio")
    TSoftObjectPtr<USoundBase> PlaceSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Block|Rendering")
    TSoftObjectPtr<UMaterialInterface> Material;
};


/*
 * Проверяем только физическую твёрдость блока для mesher.
 */
FORCEINLINE bool IsVoxelSolid(EVoxelBlock Block)
{
    return Block != EVoxelBlock::Air &&
           Block != EVoxelBlock::Water;
}

FORCEINLINE bool IsVoxelSolid(uint8 Block)
{
    return IsVoxelSolid(
        static_cast<EVoxelBlock>(Block));
}
