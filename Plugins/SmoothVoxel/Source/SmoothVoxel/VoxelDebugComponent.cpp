#include "VoxelDebugComponent.h"

#include "VoxelBlockLibrary.h"
#include "VoxelInventoryComponent.h"
#include "SmoothVoxelOctreeWorld.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    bool GetDebugCenterScreenRay(
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

    if (!IsValid(OctreeWorld))
    {
        OctreeWorld = Cast<ASmoothVoxelOctreeWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                ASmoothVoxelOctreeWorld::StaticClass()));
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
    DebugData.TargetLod = INDEX_NONE;
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
    if (!IsValid(OctreeWorld))
    {
        OctreeWorld = Cast<ASmoothVoxelOctreeWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                ASmoothVoxelOctreeWorld::StaticClass()));
    }
    if (!IsValid(OctreeWorld))
    {
        OctreeWorld = nullptr;
    }

    APawn* PlayerPawn = GetPlayerPawn();
    DebugData.PlayerLocation =
        PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
    DebugData.PlayerRotation =
        PlayerPawn ? PlayerPawn->GetActorRotation() : FRotator::ZeroRotator;

    DebugData.LoadedChunkCount = OctreeWorld ? OctreeWorld->GetActiveChunkCount() : 0;
    DebugData.WorldSeed = OctreeWorld ? OctreeWorld->Seed : 0;
    DebugData.ChunkSize = OctreeWorld ? OctreeWorld->CellsPerAxis : 0;
    DebugData.VoxelSize = OctreeWorld ? OctreeWorld->BaseVoxelSize : 0.0f;
    DebugData.InteractionDistance = OctreeWorld ? OctreeWorld->InteractionDistance : 0.0f;
    // The old streaming/unload radii do not exist in the Octree selector.
    DebugData.StreamingRadius = 0;
    DebugData.UnloadRadius = 0;
    DebugData.WorldSizeX = 0;
    DebugData.WorldSizeY = 0;
    DebugData.WorldSizeZ = 0;
    DebugData.WorldSizeKilometers = OctreeWorld
        ? OctreeWorld->GetWorldSizeCentimeters() / 100000.0f
        : 0.0f;
    DebugData.MaxLod = OctreeWorld ? OctreeWorld->MaxLod : 0;
    DebugData.MaxLeafCount = OctreeWorld ? OctreeWorld->MaxLeafCount : 0;
    DebugData.MaxConcurrentBuilds = OctreeWorld ? OctreeWorld->MaxConcurrentBuilds : 0;
    DebugData.CollisionMaxLod = OctreeWorld ? OctreeWorld->CollisionMaxLod : 0;
    DebugData.CollisionRadius = OctreeWorld ? OctreeWorld->CollisionRadius : 0.0f;
    DebugData.RootWorldCenter = OctreeWorld ? OctreeWorld->RootWorldCenter : FVector::ZeroVector;

    if (OctreeWorld)
    {
        const float SafeVoxelSize = FMath::Max(1.0f, OctreeWorld->BaseVoxelSize);
        const int32 WorldCells = FMath::RoundToInt(
            OctreeWorld->GetWorldSizeCentimeters() / SafeVoxelSize);
        // The Octree root is a finite cube; these are base-cell counts, not legacy chunk-grid dimensions.
        DebugData.WorldSizeX = WorldCells;
        DebugData.WorldSizeY = WorldCells;
        DebugData.WorldSizeZ = WorldCells;
        DebugData.PlayerWorldBlock = FIntVector(
            FMath::FloorToInt(DebugData.PlayerLocation.X / SafeVoxelSize),
            FMath::FloorToInt(DebugData.PlayerLocation.Y / SafeVoxelSize),
            FMath::FloorToInt(DebugData.PlayerLocation.Z / SafeVoxelSize));

        FSVoxelOctreeNode PlayerNode;
        if (OctreeWorld->GetOctreeNodeAtWorldPosition(DebugData.PlayerLocation, PlayerNode))
        {
            DebugData.PlayerChunk = PlayerNode.OriginCells;
            DebugData.StreamingCenterChunk = PlayerNode.OriginCells;
            DebugData.PlayerLod = PlayerNode.Lod;
        }
        else
        {
            DebugData.PlayerChunk = FIntVector::ZeroValue;
            DebugData.StreamingCenterChunk = FIntVector::ZeroValue;
            DebugData.PlayerLod = INDEX_NONE;
        }
    }
    else
    {
        DebugData.PlayerWorldBlock = FIntVector::ZeroValue;
        DebugData.PlayerChunk = FIntVector::ZeroValue;
        DebugData.StreamingCenterChunk = FIntVector::ZeroValue;
        DebugData.PlayerLod = INDEX_NONE;
    }

    if (PlayerPawn)
    {
        if (UVoxelInventoryComponent* Inventory =
            PlayerPawn->FindComponentByClass<UVoxelInventoryComponent>())
        {
            DebugData.SelectedSlot = Inventory->GetSelectedSlot();
            DebugData.InventorySlotCount = Inventory->GetSlotCount();
            DebugData.InventoryMaxStackSize = Inventory->MaxStackSize;
            DebugData.SelectedBlock = Inventory->GetSelectedBlock();
            DebugData.SelectedBlockName =
                UVoxelBlockLibrary::GetBlockDefinitionFromTable(
                    OctreeWorld ? OctreeWorld->BlockDataTable : nullptr,
                    DebugData.SelectedBlock).DisplayName;
            DebugData.SelectedQuantity = Inventory->GetSelectedQuantity();
        }
        else
        {
            DebugData.SelectedSlot = 0;
            DebugData.InventorySlotCount = 0;
            DebugData.InventoryMaxStackSize = 0;
            DebugData.SelectedBlock = EVoxelBlock::Air;
            DebugData.SelectedBlockName = FText::FromString(TEXT("-"));
            DebugData.SelectedQuantity = 0;
        }
    }

    ResetTargetData();
    APlayerController* PC = GetPlayerController();
    if (!PC || !OctreeWorld)
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    FVector Start;
    FVector Direction;
    if (!GetDebugCenterScreenRay(PC, Start, Direction))
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    const FVector End = Start + Direction * OctreeWorld->InteractionDistance;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelDebug), true);
    Params.AddIgnoredActor(GetOwner());
    Params.AddIgnoredActor(OctreeWorld);
    const bool bHit = GetWorld()->LineTraceSingleByChannel(
        Hit, Start, End, ECC_Visibility, Params);
    if (!bHit)
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    ASmoothVoxelPlacedCubeBlock* HitCube =
        Cast<ASmoothVoxelPlacedCubeBlock>(Hit.GetActor());
    ASmoothVoxelTransvoxelChunk* HitChunk =
        Cast<ASmoothVoxelTransvoxelChunk>(Hit.GetActor());
    if (!HitCube && !HitChunk)
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    DebugData.TargetDistance = FVector::Distance(Start, Hit.ImpactPoint);
    const float Step = FMath::Max(1.0f, OctreeWorld->BaseVoxelSize);
    FVector BlockSamplePoint = Hit.ImpactPoint -
        Hit.ImpactNormal * (Step * 0.51f);

    EVoxelBlock TargetBlock = EVoxelBlock::Air;
    FIntVector WorldBlock = FIntVector::ZeroValue;
    FIntVector NodeOriginCells = FIntVector::ZeroValue;
    int32 TargetLod = INDEX_NONE;
    bool bFoundTarget = false;

    if (HitCube)
    {
        WorldBlock = HitCube->BlockCell;
        TargetBlock = HitCube->BlockType;
        BlockSamplePoint = FVector(
            (static_cast<double>(WorldBlock.X) + 0.5) * Step,
            (static_cast<double>(WorldBlock.Y) + 0.5) * Step,
            (static_cast<double>(WorldBlock.Z) + 0.5) * Step);

        FSVoxelOctreeNode TargetNode;
        if (OctreeWorld->GetOctreeNodeAtWorldPosition(BlockSamplePoint, TargetNode))
        {
            NodeOriginCells = TargetNode.OriginCells;
            TargetLod = TargetNode.Lod;
            bFoundTarget = TargetBlock != EVoxelBlock::Air;
        }
    }
    else
    {
        bFoundTarget = OctreeWorld->GetBlockDebugInfoAtWorldPosition(
            BlockSamplePoint, TargetBlock, WorldBlock, NodeOriginCells, TargetLod);
    }

    if (!bFoundTarget)
    {
        OnDebugDataUpdated.Broadcast(DebugData);
        return;
    }

    DebugData.bHasTargetBlock = true;
    DebugData.TargetBlock = TargetBlock;
    DebugData.TargetWorldBlock = WorldBlock;
    DebugData.TargetLocalBlock = WorldBlock - NodeOriginCells;
    DebugData.TargetChunk = NodeOriginCells;
    DebugData.TargetLod = TargetLod;

    const FVoxelBlockDefinition Definition =
        UVoxelBlockLibrary::GetBlockDefinitionFromTable(
            OctreeWorld->BlockDataTable, TargetBlock);
    DebugData.TargetBlockName = Definition.DisplayName;
    DebugData.TargetDropBlock = Definition.DropBlock;
    DebugData.TargetDropBlockName =
        UVoxelBlockLibrary::GetBlockDefinitionFromTable(
            OctreeWorld->BlockDataTable, Definition.DropBlock).DisplayName;
    DebugData.TargetDurability = Definition.Durability;
    DebugData.bTargetIsSolid = Definition.bIsSolid;
    DebugData.bTargetCanBreak = Definition.bCanBreak;
    DebugData.bTargetCanPlace = Definition.bCanPlace;
    DebugData.bTargetIsLiquid = Definition.bIsLiquid;
    DebugData.TargetMaterialSlot = Definition.MaterialSlot;
    DebugData.TargetMaterialAsset = SoftObjectPathToString(Definition.Material);
    DebugData.TargetBreakSoundAsset = SoftObjectPathToString(Definition.BreakSound);
    DebugData.TargetPlaceSoundAsset = SoftObjectPathToString(Definition.PlaceSound);

    OctreeWorld->GetTerrainDebugInfoAtWorldPosition(
        Hit.ImpactPoint,
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
