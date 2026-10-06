#include "VoxelMainMenuWidget.h"

#include "VoxelWorld.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UVoxelMainMenuWidget::StartNewGame()
{
    if (AVoxelWorld* VoxelWorld =
        Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass())))
    {
        VoxelWorld->GenerateWorld();
    }
}

void UVoxelMainMenuWidget::LoadGame()
{
    if (AVoxelWorld* VoxelWorld =
        Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass())))
    {
        VoxelWorld->LoadWorld();
    }
}

void UVoxelMainMenuWidget::SaveGame()
{
    if (AVoxelWorld* VoxelWorld =
        Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass())))
    {
        VoxelWorld->SaveWorld();
    }
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
