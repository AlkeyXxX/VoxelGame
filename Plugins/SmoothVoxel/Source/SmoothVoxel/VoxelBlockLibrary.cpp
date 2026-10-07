#include "VoxelBlockLibrary.h"

#include "Engine/DataTable.h"

namespace
{
    FVoxelBlockDefinition MakeBlock(
        EVoxelBlock Block,
        const TCHAR* Name,
        float Durability,
        bool bIsSolid,
        bool bCanBreak,
        bool bCanPlace,
        bool bIsLiquid,
        EVoxelBlock DropBlock,
        FName MaterialSlot)
    {
        FVoxelBlockDefinition Definition;

        Definition.Block = Block;
        Definition.DisplayName = FText::FromString(Name);
        Definition.Durability = Durability;
        Definition.bIsSolid = bIsSolid;
        Definition.bCanBreak = bCanBreak;
        Definition.bCanPlace = bCanPlace;
        Definition.bIsLiquid = bIsLiquid;
        Definition.DropBlock = DropBlock;
        Definition.MaterialSlot = MaterialSlot;

        return Definition;
    }
}

namespace
{
    FName GetBlockRowName(
        EVoxelBlock Block)
    {
        switch (Block)
        {
        case EVoxelBlock::Grass:
            return TEXT("Grass");

        case EVoxelBlock::Dirt:
            return TEXT("Dirt");

        case EVoxelBlock::Stone:
            return TEXT("Stone");

        case EVoxelBlock::Sand:
            return TEXT("Sand");

        case EVoxelBlock::Wood:
            return TEXT("Wood");

        case EVoxelBlock::Water:
            return TEXT("Water");

        case EVoxelBlock::Air:
        default:
            return TEXT("Air");
        }
    }
}

FVoxelBlockDefinition UVoxelBlockLibrary::GetBlockDefinition(
    EVoxelBlock Block)
{
    switch (Block)
    {
    case EVoxelBlock::Grass:
        return MakeBlock(
            EVoxelBlock::Grass,
            TEXT("Трава"),
            1.0f,
            true,
            true,
            true,
            false,
            EVoxelBlock::Grass,
            TEXT("Default"));

    case EVoxelBlock::Dirt:
        return MakeBlock(
            EVoxelBlock::Dirt,
            TEXT("Земля"),
            1.0f,
            true,
            true,
            true,
            false,
            EVoxelBlock::Dirt,
            TEXT("Default"));

    case EVoxelBlock::Stone:
        return MakeBlock(
            EVoxelBlock::Stone,
            TEXT("Камень"),
            3.0f,
            true,
            true,
            true,
            false,
            EVoxelBlock::Stone,
            TEXT("Default"));

    case EVoxelBlock::Sand:
        return MakeBlock(
            EVoxelBlock::Sand,
            TEXT("Песок"),
            0.75f,
            true,
            true,
            true,
            false,
            EVoxelBlock::Sand,
            TEXT("Default"));

    case EVoxelBlock::Wood:
        return MakeBlock(
            EVoxelBlock::Wood,
            TEXT("Дерево"),
            2.5f,
            true,
            true,
            true,
            false,
            EVoxelBlock::Wood,
            TEXT("Default"));

    case EVoxelBlock::Water:
        return MakeBlock(
            EVoxelBlock::Water,
            TEXT("Вода"),
            0.0f,
            false,
            false,
            false,
            true,
            EVoxelBlock::Air,
            TEXT("Water"));

    case EVoxelBlock::Air:
    default:
        return MakeBlock(
            EVoxelBlock::Air,
            TEXT("Воздух"),
            0.0f,
            false,
            false,
            false,
            false,
            EVoxelBlock::Air,
            TEXT("Default"));
    }
}

FVoxelBlockDefinition UVoxelBlockLibrary::GetBlockDefinitionFromTable(
    const UDataTable* DataTable,
    EVoxelBlock Block)
{
    if (DataTable)
    {
        const FName RowName =
            GetBlockRowName(Block);

        static const FString ContextString =
            TEXT("VoxelBlockLibrary");

        if (const FVoxelBlockDefinition* Row =
            DataTable->FindRow<FVoxelBlockDefinition>(
                RowName,
                ContextString,
                false))
        {
            if (Row->Block == Block)
            {
                return *Row;
            }
        }
    }

    /*
     * Если таблица не назначена или строка отсутствует,
     * используем встроенное описание.
     */
    return GetBlockDefinition(Block);
}

FText UVoxelBlockLibrary::GetBlockDisplayName(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).DisplayName;
}

float UVoxelBlockLibrary::GetBlockDurability(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).Durability;
}

float UVoxelBlockLibrary::GetBlockDurabilityFromTable(
    const UDataTable* DataTable,
    EVoxelBlock Block)
{
    return GetBlockDefinitionFromTable(
        DataTable,
        Block).Durability;
}

bool UVoxelBlockLibrary::CanBreakBlock(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).bCanBreak;
}

bool UVoxelBlockLibrary::CanBreakBlockFromTable(
    const UDataTable* DataTable,
    EVoxelBlock Block)
{
    return GetBlockDefinitionFromTable(
        DataTable,
        Block).bCanBreak;
}

bool UVoxelBlockLibrary::CanPlaceBlock(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).bCanPlace;
}

bool UVoxelBlockLibrary::CanPlaceBlockFromTable(
    const UDataTable* DataTable,
    EVoxelBlock Block)
{
    return GetBlockDefinitionFromTable(
        DataTable,
        Block).bCanPlace;
}

EVoxelBlock UVoxelBlockLibrary::GetBlockDrop(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).DropBlock;
}

EVoxelBlock UVoxelBlockLibrary::GetBlockDropFromTable(
    const UDataTable* DataTable,
    EVoxelBlock Block)
{
    return GetBlockDefinitionFromTable(
        DataTable,
        Block).DropBlock;
}

bool UVoxelBlockLibrary::IsLiquidBlock(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).bIsLiquid;
}

bool UVoxelBlockLibrary::IsSolidBlock(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).bIsSolid;
}
