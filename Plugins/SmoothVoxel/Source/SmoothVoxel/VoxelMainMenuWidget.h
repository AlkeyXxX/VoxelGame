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
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

public:
    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void StartNewGame();

    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void LoadGame();

    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void SaveGame();

    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void CloseMenu();

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
};
