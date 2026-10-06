// Copyright Epic Games, Inc. All Rights Reserved.


#include "MyVoxelGameGameModeBase.h"
#include "VoxelPlayerController.h"

AMyVoxelGameGameModeBase::AMyVoxelGameGameModeBase()
{
    PlayerControllerClass = AVoxelPlayerController::StaticClass();
}

