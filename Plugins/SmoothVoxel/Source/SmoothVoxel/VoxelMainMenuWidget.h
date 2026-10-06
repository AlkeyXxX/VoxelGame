#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VoxelMainMenuWidget.generated.h"

class UButton;

UCLASS()
class SMOOTHVOXEL_API UVoxelMainMenuWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

private:
    UButton* NewGameButton = nullptr;
    UButton* LoadGameButton = nullptr;
    UButton* SaveGameButton = nullptr;

    void BuildMenu();

    UFUNCTION()
    void OnNewGameClicked();

    UFUNCTION()
    void OnLoadGameClicked();

    UFUNCTION()
    void OnSaveGameClicked();

    void CloseMenu();
};
