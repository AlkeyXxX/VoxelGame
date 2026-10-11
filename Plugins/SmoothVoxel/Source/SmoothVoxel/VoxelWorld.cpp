#include "VoxelWorld.h"

AVoxelWorld::AVoxelWorld()
{
    // This class exists only to keep serialized legacy Blueprint classes resolvable.
    PrimaryActorTick.bCanEverTick = false;
}

void AVoxelWorld::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Warning,
        TEXT("AVoxelWorld is retired and does not generate terrain. Place BP_OctreeWorld instead."));
}

void AVoxelWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
}

void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
}

void AVoxelWorld::GenerateWorld()
{
    // Intentionally empty: terrain generation belongs to ASmoothVoxelOctreeWorld.
}

void AVoxelWorld::SaveWorld()
{
    // Intentionally empty: legacy SaveGame data is not compatible with the Octree world.
}

bool AVoxelWorld::LoadWorld()
{
    return false;
}

void AVoxelWorld::RemoveBlockAtWorld(const FVector& WorldPosition)
{
    (void)WorldPosition;
}

void AVoxelWorld::SetBlockAtWorld(const FVector& WorldPosition, EVoxelBlock Block)
{
    (void)WorldPosition;
    (void)Block;
}

bool AVoxelWorld::BreakBlockByRay()
{
    return false;
}

bool AVoxelWorld::PlaceBlockByRay()
{
    return false;
}

bool AVoxelWorld::PlaceBlockByRayWithType(EVoxelBlock BlockToPlace)
{
    (void)BlockToPlace;
    return false;
}

void AVoxelWorld::SetPersistentObjectState(int64 ObjectId, uint8 State)
{
    (void)ObjectId;
    (void)State;
}

bool AVoxelWorld::GetPersistentObjectState(int64 ObjectId, uint8& OutState) const
{
    (void)ObjectId;
    OutState = 0;
    return false;
}
