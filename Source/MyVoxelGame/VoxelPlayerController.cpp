#include "VoxelPlayerController.h"

#include "VoxelWorld.h"

#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

bool AVoxelPlayerController::InputKey(
    const FInputKeyEventArgs& Params)
{
    /*
     * Escape обрабатываем на самом PlayerController.
     *
     * Это важно: VoxelWorld больше не создаёт свой InputComponent
     * и не блокирует обычный input персонажа.
     *
     * Когда игра уже на паузе, Escape должен обрабатываться
     * самим UMG-меню, поэтому здесь ничего не делаем.
     */
    if (Params.Key == EKeys::Escape &&
        Params.Event == IE_Pressed)
    {
        if (!UGameplayStatics::IsGamePaused(GetWorld()))
        {
            HandleEscape();
            return true;
        }
    }

    return Super::InputKey(Params);
}

void AVoxelPlayerController::HandleEscape()
{
    if (AVoxelWorld* VoxelWorld =
        Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass())))
    {
        VoxelWorld->ToggleMainMenu();
    }
}
