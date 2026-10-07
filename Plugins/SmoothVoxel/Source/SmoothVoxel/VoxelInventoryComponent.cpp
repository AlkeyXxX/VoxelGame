#include "VoxelInventoryComponent.h"

#include "Math/UnrealMathUtility.h"

UVoxelInventoryComponent::UVoxelInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UVoxelInventoryComponent::BeginPlay()
{
    Super::BeginPlay();

    EnsureSlotArray();

    SelectedSlot =
        FMath::Clamp(
            SelectedSlot,
            0,
            FMath::Max(0, Slots.Num() - 1));
}

void UVoxelInventoryComponent::EnsureSlotArray()
{
    const int32 SafeSlotCount =
        FMath::Clamp(
            SlotCount,
            1,
            36);

    if (Slots.Num() != SafeSlotCount)
    {
        Slots.SetNum(SafeSlotCount);
    }

    for (FVoxelInventorySlot& Slot : Slots)
    {
        if (Slot.Quantity <= 0 ||
            Slot.Block == EVoxelBlock::Air)
        {
            Slot.Block = EVoxelBlock::Air;
            Slot.Quantity = 0;
        }
        else
        {
            Slot.Quantity =
                FMath::Clamp(
                    Slot.Quantity,
                    1,
                    FMath::Max(1, MaxStackSize));
        }
    }
}

bool UVoxelInventoryComponent::CanAddBlock(
    EVoxelBlock Block,
    int32 Quantity) const
{
    if (Block == EVoxelBlock::Air ||
        Quantity <= 0)
    {
        return false;
    }

    const int32 StackSize =
        FMath::Max(1, MaxStackSize);

    int32 Capacity = 0;

    /*
     * Считаем свободное место сначала в существующих стаках,
     * затем в пустых слотах.
     */
    for (const FVoxelInventorySlot& Slot : Slots)
    {
        if (Slot.Block == Block &&
            Slot.Quantity > 0)
        {
            Capacity +=
                FMath::Max(
                    0,
                    StackSize - Slot.Quantity);
        }
        else if (Slot.IsEmpty())
        {
            Capacity += StackSize;
        }

        if (Capacity >= Quantity)
        {
            return true;
        }
    }

    return Capacity >= Quantity;
}

int32 UVoxelInventoryComponent::AddBlock(
    EVoxelBlock Block,
    int32 Quantity)
{
    EnsureSlotArray();

    if (Block == EVoxelBlock::Air ||
        Quantity <= 0)
    {
        return 0;
    }

    const int32 StackSize =
        FMath::Max(1, MaxStackSize);

    int32 Remaining = Quantity;
    int32 Added = 0;

    /*
     * Сначала заполняем уже существующие стаки
     * этого же блока.
     */
    for (FVoxelInventorySlot& Slot : Slots)
    {
        if (Remaining <= 0)
        {
            break;
        }

        if (Slot.Block != Block ||
            Slot.Quantity >= StackSize)
        {
            continue;
        }

        const int32 Space =
            StackSize - Slot.Quantity;

        const int32 ToAdd =
            FMath::Min(
                Space,
                Remaining);

        Slot.Quantity += ToAdd;
        Remaining -= ToAdd;
        Added += ToAdd;
    }

    /*
     * Потом занимаем пустые слоты.
     */
    for (FVoxelInventorySlot& Slot : Slots)
    {
        if (Remaining <= 0)
        {
            break;
        }

        if (!Slot.IsEmpty())
        {
            continue;
        }

        const int32 ToAdd =
            FMath::Min(
                StackSize,
                Remaining);

        Slot.Block = Block;
        Slot.Quantity = ToAdd;

        Remaining -= ToAdd;
        Added += ToAdd;
    }

    if (Added > 0)
    {
        BroadcastInventoryChanged();
    }

    return Added;
}

int32 UVoxelInventoryComponent::RemoveBlock(
    EVoxelBlock Block,
    int32 Quantity)
{
    EnsureSlotArray();

    if (Block == EVoxelBlock::Air ||
        Quantity <= 0)
    {
        return 0;
    }

    int32 Remaining = Quantity;
    int32 Removed = 0;

    for (FVoxelInventorySlot& Slot : Slots)
    {
        if (Remaining <= 0)
        {
            break;
        }

        if (Slot.Block != Block ||
            Slot.Quantity <= 0)
        {
            continue;
        }

        const int32 ToRemove =
            FMath::Min(
                Slot.Quantity,
                Remaining);

        Slot.Quantity -= ToRemove;
        Remaining -= ToRemove;
        Removed += ToRemove;

        if (Slot.Quantity <= 0)
        {
            Slot.Block = EVoxelBlock::Air;
            Slot.Quantity = 0;
        }
    }

    if (Removed > 0)
    {
        BroadcastInventoryChanged();
    }

    return Removed;
}

int32 UVoxelInventoryComponent::RemoveFromSelectedSlot(
    int32 Quantity)
{
    EnsureSlotArray();

    if (Quantity <= 0 ||
        !Slots.IsValidIndex(SelectedSlot) ||
        Slots[SelectedSlot].IsEmpty())
    {
        return 0;
    }

    FVoxelInventorySlot& Slot =
        Slots[SelectedSlot];

    const int32 Removed =
        FMath::Min(
            Slot.Quantity,
            Quantity);

    Slot.Quantity -= Removed;

    if (Slot.Quantity <= 0)
    {
        Slot.Block = EVoxelBlock::Air;
        Slot.Quantity = 0;
    }

    if (Removed > 0)
    {
        BroadcastInventoryChanged();
    }

    return Removed;
}

void UVoxelInventoryComponent::SetSelectedSlot(
    int32 NewSelectedSlot)
{
    EnsureSlotArray();

    const int32 NewIndex =
        FMath::Clamp(
            NewSelectedSlot,
            0,
            FMath::Max(0, Slots.Num() - 1));

    if (SelectedSlot == NewIndex)
    {
        return;
    }

    SelectedSlot = NewIndex;
    BroadcastInventoryChanged();
}

int32 UVoxelInventoryComponent::GetSelectedSlot() const
{
    return SelectedSlot;
}

EVoxelBlock UVoxelInventoryComponent::GetSelectedBlock() const
{
    if (!Slots.IsValidIndex(SelectedSlot) ||
        Slots[SelectedSlot].IsEmpty())
    {
        return EVoxelBlock::Air;
    }

    return Slots[SelectedSlot].Block;
}

int32 UVoxelInventoryComponent::GetSelectedQuantity() const
{
    if (!Slots.IsValidIndex(SelectedSlot) ||
        Slots[SelectedSlot].IsEmpty())
    {
        return 0;
    }

    return Slots[SelectedSlot].Quantity;
}

FVoxelInventorySlot UVoxelInventoryComponent::GetSlot(
    int32 Index) const
{
    if (Slots.IsValidIndex(Index))
    {
        return Slots[Index];
    }

    return FVoxelInventorySlot();
}

int32 UVoxelInventoryComponent::GetSlotCount() const
{
    return Slots.Num() > 0
        ? Slots.Num()
        : FMath::Clamp(SlotCount, 1, 36);
}

bool UVoxelInventoryComponent::IsSlotEmpty(
    int32 Index) const
{
    return !Slots.IsValidIndex(Index) ||
           Slots[Index].IsEmpty();
}

void UVoxelInventoryComponent::ClearInventory()
{
    EnsureSlotArray();

    for (FVoxelInventorySlot& Slot : Slots)
    {
        Slot.Block = EVoxelBlock::Air;
        Slot.Quantity = 0;
    }

    SelectedSlot = 0;

    BroadcastInventoryChanged();
}

void UVoxelInventoryComponent::BroadcastInventoryChanged()
{
    OnInventoryChanged.Broadcast();
}
