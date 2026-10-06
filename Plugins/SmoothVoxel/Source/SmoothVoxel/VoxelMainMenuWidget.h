#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VoxelMainMenuWidget.generated.h"

UCLASS()
class SMOOTHVOXEL_API UVoxelMainMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void StartNewGame();

    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void LoadGame();

    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void SaveGame();

    UFUNCTION(BlueprintCallable, Category="Voxel|Menu")
    void ExitGame();
};
