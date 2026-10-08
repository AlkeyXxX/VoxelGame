// Copyright Epic Games, Inc. All Rights Reserved.

#include "MyVoxelGameGameModeBase.h"

#include "VoxelWorld.h"

#include "Engine/PlayerStart.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

AActor* AMyVoxelGameGameModeBase::ChoosePlayerStart_Implementation(
    AController* Player)
{
    UWorld* World = GetWorld();

    if (World)
    {
        AVoxelWorld* VoxelWorld =
            Cast<AVoxelWorld>(
                UGameplayStatics::GetActorOfClass(
                    World,
                    AVoxelWorld::StaticClass()));

        if (VoxelWorld)
        {
            const FVector CenterSpawnLocation =
                VoxelWorld->GetCenterSpawnLocation();

            /*
             * Reuse our generated start on respawn, rather than creating
             * another PlayerStart each time the player dies/restarts.
             */
            for (TActorIterator<APlayerStart> It(World); It; ++It)
            {
                APlayerStart* Start = *It;

                if (Start &&
                    Start->ActorHasTag(
                        FName(TEXT("VoxelWorldCenterSpawn"))))
                {
                    Start->SetActorLocation(
                        CenterSpawnLocation);

                    return Start;
                }
            }

            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride =
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            APlayerStart* CenterStart =
                World->SpawnActor<APlayerStart>(
                    APlayerStart::StaticClass(),
                    CenterSpawnLocation,
                    FRotator::ZeroRotator,
                    SpawnParams);

            if (CenterStart)
            {
                CenterStart->Tags.AddUnique(
                    FName(TEXT("VoxelWorldCenterSpawn")));

                return CenterStart;
            }
        }
    }

    return Super::ChoosePlayerStart_Implementation(Player);
}
