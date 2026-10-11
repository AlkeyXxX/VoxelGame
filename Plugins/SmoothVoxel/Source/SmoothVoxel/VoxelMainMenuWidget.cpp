#include "VoxelMainMenuWidget.h"

#include "SmoothVoxelOctreeWorld.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UVoxelMainMenuWidget::StartNewGame()
{
    if (ASmoothVoxelOctreeWorld* World =
        Cast<ASmoothVoxelOctreeWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                ASmoothVoxelOctreeWorld::StaticClass())))
    {
        World->RegenerateWorld();
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("StartNewGame: BP_OctreeWorld was not found in the active level."));
    }
}

void UVoxelMainMenuWidget::LoadGame()
{
    UE_LOG(LogTemp, Warning,
        TEXT("LoadGame is not available for the Octree world yet; persistence is planned for roadmap stage 8."));
}

void UVoxelMainMenuWidget::SaveGame()
{
    UE_LOG(LogTemp, Warning,
        TEXT("SaveGame is not available for the Octree world yet; persistence is planned for roadmap stage 8."));
}

void UVoxelMainMenuWidget::ExitGame()
{
    if (APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0))
    {
        UKismetSystemLibrary::QuitGame(
            this,
            PC,
            EQuitPreference::Quit,
            false);
    }
}
