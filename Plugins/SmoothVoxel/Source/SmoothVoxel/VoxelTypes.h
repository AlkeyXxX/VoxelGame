
#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.generated.h"

UENUM(BlueprintType)
enum class EVoxelBlock : uint8
{
	Air   = 0,
	Grass = 1,
	Dirt  = 2,
	Stone = 3,
	Sand  = 4,
	Wood  = 5
};

// TEST GITHUB SYNC

FORCEINLINE bool IsVoxelSolid(EVoxelBlock Block)
{
	return Block != EVoxelBlock::Air;
}

FORCEINLINE bool IsVoxelSolid(uint8 Block)
{
	return Block != uint8(EVoxelBlock::Air);
}

