#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "VoxelWorldSaveGame.generated.h"

USTRUCT()
struct SMOOTHVOXEL_API FVoxelSavedBlock
{
    GENERATED_BODY()

    UPROPERTY()
    FIntVector ChunkCoord = FIntVector::ZeroValue;

    UPROPERTY()
    int32 LocalIndex = 0;

    UPROPERTY()
    uint8 Block = 0;
};

USTRUCT()
struct SMOOTHVOXEL_API FVoxelSavedObjectState
{
    GENERATED_BODY()

    UPROPERTY()
    int64 ObjectId = 0;

    UPROPERTY()
    uint8 State = 0;
};

UCLASS()
class SMOOTHVOXEL_API UVoxelWorldSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 SaveVersion = 1;

    UPROPERTY()
    int32 Seed = 1337;

    UPROPERTY()
    int32 ChunkSize = 32;

    UPROPERTY()
    float VoxelSize = 100.0f;

    UPROPERTY()
    TArray<FVoxelSavedBlock> ModifiedBlocks;

    UPROPERTY()
    TArray<FVoxelSavedObjectState> ObjectStates;
};
