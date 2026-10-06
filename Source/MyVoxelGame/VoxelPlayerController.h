#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "VoxelPlayerController.generated.h"

UCLASS()
class MYVOXELGAME_API AVoxelPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    virtual bool InputKey(
        const FInputKeyEventArgs& Params) override;

private:
    void HandleEscape();
};
