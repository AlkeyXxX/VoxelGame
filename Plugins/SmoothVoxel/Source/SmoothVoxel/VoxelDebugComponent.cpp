#include "VoxelDebugComponent.h"

#include "VoxelBlockLibrary.h"
#include "VoxelInventoryComponent.h"
#include "VoxelWorld.h"
#include "VoxelChunk.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    bool GetCenterScreenRay(
        APlayerController* PC,
        FVector& OutStart,
        FVector& OutDirection)
    {
        if (!PC)
        {
            return false;
        }

        int32 ViewportSizeX = 0;
        int32 ViewportSizeY = 0;

        PC->GetViewportSize(
            ViewportSizeX,
            ViewportSizeY);

        if (ViewportSizeX <= 0 ||
            ViewportSizeY <= 0)
        {
            return false;
        }

        return PC->DeprojectScreenPositionToWorld(
            static_cast<float>(ViewportSizeX) * 0.5f,
            static_cast<float>(ViewportSizeY) * 0.5f,
            OutStart,
            OutDirection);
    }

    template<typename T>
    FString SoftObjectPathToString(
        const TSoftObjectPtr<T>& Asset)
    {
        return Asset.ToSoftObjectPath().ToString();
    }
}

UVoxelDebugComponent::UVoxelDebugComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickInterval = 0.0f;
}

void UVoxelDebugComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!VoxelWorld)
    {
        VoxelWorld = Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass()));
    }

    SetComponentTickEnabled(false);
}

void UVoxelDebugComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(
        DeltaTime,
        TickType,
        ThisTickFunction);

    RefreshPerformanceData(DeltaTime);

    RefreshAccumulator += DeltaTime;

    if (RefreshAccumulator >= RefreshInterval)
    {
        RefreshAccumulator = 0.0f;
        RefreshDebugData();
    }
}

void UVoxelDebugComponent::ToggleDebug()
{
    SetDebugEnabled(!bDebugEnabled);
}

void UVoxelDebugComponent::SetDebugEnabled(
    bool bEnabled)
{
    bDebugEnabled = bEnabled;
    RefreshAccumulator = 0.0f;

    SetComponentTickEnabled(
        bDebugEnabled);

    if (bDebugEnabled)
    {
        RefreshDebugData();
    }
}

bool UVoxelDebugComponent::IsDebugEnabled() const
{
    return bDebugEnabled;
}

void UVoxelDebugComponent::RefreshPerformanceData(
    float DeltaTime)
{
    const float SafeDelta =
        FMath::Max(
            DeltaTime,
            KINDA_SMALL_NUMBER);

    /*
     * Очень дешёвое сглаживание frame time.
     * Считаем его каждый кадр только пока debug открыт.
     */
    SmoothedFrameTime =
        FMath::Lerp(
            SmoothedFrameTime,
            SafeDelta,
            0.08f);

    DebugData.FrameTimeMs =
        SmoothedFrameTime * 1000.0f;

    DebugData.FPS =
        1.0f / FMath::Max(
            SmoothedFrameTime,
            KINDA_SMALL_NUMBER);
}

APawn* UVoxelDebugComponent::GetPlayerPawn() const
{
    if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
    {
        return OwnerPawn;
    }

    return UGameplayStatics::GetPlayerPawn(
        GetWorld(),
        0);
}

APlayerController* UVoxelDebugComponent::GetPlayerController() const
{
    if (APawn* PlayerPawn = GetPlayerPawn())
    {
        if (APlayerController* OwnerPC =
            Cast<APlayerController>(PlayerPawn->GetController()))
        {
            return OwnerPC;
        }
    }

    return UGameplayStatics::GetPlayerController(
        GetWorld(),
        0);
}

void UVoxelDebugComponent::ResetTargetData()
{
    DebugData.bHasTargetBlock = false;
    DebugData.TargetBlock = EVoxelBlock::Air;
    DebugData.TargetBlockName = FText::FromString(TEXT("-"));
    DebugData.TargetDistance = 0.0f;
    DebugData.TargetWorldBlock = FIntVector::ZeroValue;
    DebugData.TargetLocalBlock = FIntVector::ZeroValue;
    DebugData.TargetChunk = FIntVector::ZeroValue;
    DebugData.TargetDropBlock = EVoxelBlock::Air;
    DebugData.TargetDropBlockName = FText::FromString(TEXT("-"));
    DebugData.TargetDurability = 0.0f;
    DebugData.bTargetIsSolid = false;
    DebugData.bTargetCanBreak = false;
    DebugData.bTargetCanPlace = false;
    DebugData.bTargetIsLiquid = false;
    DebugData.TargetMaterialSlot = NAME_None;
    DebugData.TargetMaterialAsset.Empty();
    DebugData.TargetBreakSoundAsset.Empty();
    DebugData.TargetPlaceSoundAsset.Empty();
    DebugData.TargetSurfaceHeight = 0;
    DebugData.TargetTerrainNoise = 0.0f;
    DebugData.TargetTemperature = 0.0f;
    DebugData.TargetMoisture = 0.0f;
    DebugData.TargetBiome = TEXT("-");
}

void UVoxelDebugComponent::RefreshDebugData()
{
    if (!VoxelWorld)
    {
        VoxelWorld = Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass()));
    }

    APawn* PlayerPawn = GetPlayerPawn();

    DebugData.PlayerLocation =
        PlayerPawn
        ? PlayerPawn->GetActorLocation()
        : FVector::ZeroVector;

    DebugData.PlayerRotation =
        PlayerPawn
        ? PlayerPawn->GetActorRotation()
        : FRotator::ZeroRotator;

    DebugData.LoadedChunkCount =
        VoxelWorld
        ? VoxelWorld->GetLoadedChunkCount()
        : 0;

    DebugData.StreamingCenterChunk =
        VoxelWorld
        ? VoxelWorld->GetStreamingCenterChunkDebug()
        : FIntVector::ZeroValue;

    if (VoxelWorld)
    {
        DebugData.WorldSeed = VoxelWorld->Seed;
        DebugData.ChunkSize = VoxelWorld->ChunkSize;
        DebugData.VoxelSize = VoxelWorld->VoxelSize;
        DebugData.StreamingRadius =
            VoxelWorld->StreamingRadius;
        DebugData.UnloadRadius =
            VoxelWorld->UnloadRadius;
        DebugData.WorldSizeX = VoxelWorld->WorldSizeX;
        DebugData.WorldSizeY = VoxelWorld->WorldSizeY;
        DebugData.WorldSizeZ = VoxelWorld->WorldSizeZ;
        DebugData.InteractionDistance =
            VoxelWorld->InteractionDistance;
    }

    if (VoxelWorld)
    {
        EVoxelBlock PlayerBlock =
            EVoxelBlock::Air;

        FIntVector PlayerWorldBlock;
        FIntVector PlayerLocalBlock;
        FIntVector PlayerChunk;

        if (VoxelWorld->GetBlockDebugInfoAtWorld(
                DebugData.PlayerLocation,
                PlayerBlock,
                PlayerWorldBlock,
                PlayerLocalBlock,
                PlayerChunk))
        {
            DebugData.PlayerWorldBlock =
                PlayerWorldBlock;

            DebugData.PlayerChunk =
                PlayerChunk;
        }
        else
        {
            DebugData.PlayerWorldBlock =
                FIntVector::ZeroValue;

            DebugData.PlayerChunk =
                FIntVector::ZeroValue;
        }
    }

    if (PlayerPawn)
    {
        if (UVoxelInventoryComponent* Inventory =
            PlayerPawn->FindComponentByClass<
                UVoxelInventoryComponent>())
        {
            DebugData.SelectedSlot =
                Inventory->GetSelectedSlot();

            DebugData.InventorySlotCount =
                Inventory->GetSlotCount();

            DebugData.InventoryMaxStackSize =
                Inventory->MaxStackSize;

            DebugData.SelectedBlock =
                Inventory->GetSelectedBlock();

            DebugData.SelectedBlockName =
                UVoxelBlockLibrary::GetBlockDefinitionFromTable(
                    VoxelWorld
                    ? VoxelWorld->BlockDataTable
                    : nullptr,
                    DebugData.SelectedBlock).DisplayName;

            DebugData.SelectedQuantity =
                Inventory->GetSelectedQuantity();
        }
        else
        {
            DebugData.SelectedSlot = 0;
            DebugData.InventorySlotCount = 0;
            DebugData.InventoryMaxStackSize = 0;
            DebugData.SelectedBlock = EVoxelBlock::Air;
            DebugData.SelectedBlockName =
                FText::FromString(TEXT("-"));
            DebugData.SelectedQuantity = 0;
        }
    }

    ResetTargetData();

    APlayerController* PC =
        GetPlayerController();

    if (!PC || !VoxelWorld)
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    FVector Start;
    FVector Direction;

    if (!GetCenterScreenRay(
        PC,
        Start,
        Direction))
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    const FVector End =
        Start +
        Direction * VoxelWorld->InteractionDistance;

    FHitResult Hit;

    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(VoxelDebug),
        true);

    Params.AddIgnoredActor(
        GetOwner());

    const bool bHit =
        GetWorld()->LineTraceSingleByChannel(
            Hit,
            Start,
            End,
            ECC_Visibility,
            Params);

    if (!bHit)
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    AVoxelChunk* HitChunk =
        Cast<AVoxelChunk>(
            Hit.GetActor());

    if (!HitChunk)
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    DebugData.TargetDistance =
        FVector::Distance(
            Start,
            Hit.ImpactPoint);

    const FVector BlockSamplePoint =
        Hit.ImpactPoint -
        Hit.ImpactNormal *
        (VoxelWorld->VoxelSize * 0.51f);

    EVoxelBlock TargetBlock =
        EVoxelBlock::Air;

    FIntVector WorldBlock;
    FIntVector LocalBlock;
    FIntVector ChunkCoord;

    if (!VoxelWorld->GetBlockDebugInfoAtWorld(
            BlockSamplePoint,
            TargetBlock,
            WorldBlock,
            LocalBlock,
            ChunkCoord))
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    DebugData.bHasTargetBlock = true;
    DebugData.TargetBlock = TargetBlock;
    DebugData.TargetWorldBlock = WorldBlock;
    DebugData.TargetLocalBlock = LocalBlock;
    DebugData.TargetChunk = ChunkCoord;

    const FVoxelBlockDefinition Definition =
        UVoxelBlockLibrary::GetBlockDefinitionFromTable(
            VoxelWorld->BlockDataTable,
            TargetBlock);

    DebugData.TargetBlockName =
        Definition.DisplayName;

    DebugData.TargetDropBlock =
        Definition.DropBlock;

    DebugData.TargetDropBlockName =
        UVoxelBlockLibrary::GetBlockDefinitionFromTable(
            VoxelWorld->BlockDataTable,
            Definition.DropBlock).DisplayName;

    DebugData.TargetDurability =
        Definition.Durability;

    DebugData.bTargetIsSolid =
        Definition.bIsSolid;

    DebugData.bTargetCanBreak =
        Definition.bCanBreak;

    DebugData.bTargetCanPlace =
        Definition.bCanPlace;

    DebugData.bTargetIsLiquid =
        Definition.bIsLiquid;

    DebugData.TargetMaterialSlot =
        Definition.MaterialSlot;

    DebugData.TargetMaterialAsset =
        SoftObjectPathToString(
            Definition.Material);

    DebugData.TargetBreakSoundAsset =
        SoftObjectPathToString(
            Definition.BreakSound);

    DebugData.TargetPlaceSoundAsset =
        SoftObjectPathToString(
            Definition.PlaceSound);

    VoxelWorld->GetTerrainDebugInfo(
        WorldBlock.X,
        WorldBlock.Y,
        DebugData.TargetSurfaceHeight,
        DebugData.TargetTerrainNoise,
        DebugData.TargetTemperature,
        DebugData.TargetMoisture,
        DebugData.TargetBiome);

    OnDebugDataUpdated.Broadcast(DebugData);
}

FVoxelDebugData UVoxelDebugComponent::GetDebugData() const
{
    return DebugData;
}
