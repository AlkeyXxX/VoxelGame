#include "VoxelBlockLibrary.h"

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

bool UVoxelBlockLibrary::CanBreakBlock(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).bCanBreak;
}

bool UVoxelBlockLibrary::CanPlaceBlock(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).bCanPlace;
}

EVoxelBlock UVoxelBlockLibrary::GetBlockDrop(
    EVoxelBlock Block)
{
    return GetBlockDefinition(Block).DropBlock;
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
