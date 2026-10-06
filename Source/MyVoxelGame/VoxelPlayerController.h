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
        FKey Key,
        EInputEvent Event,
        float AmountDepressed = 1.0f,
        bool bGamepad = false) override;

private:
    void HandleEscape();
};
