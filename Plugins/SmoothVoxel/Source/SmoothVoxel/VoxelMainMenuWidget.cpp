#include "VoxelMainMenuWidget.h"
#include "VoxelWorld.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

void UVoxelMainMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (WidgetTree && WidgetTree->RootWidget)
    {
        // Widget Blueprint owns the visual layout.
    }
    else
    {
        BuildMenu();
    }

    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->bShowMouseCursor = true;

        FInputModeUIOnly InputMode;
        InputMode.SetWidgetToFocus(TakeWidget());
        InputMode.SetLockMouseToViewportBehavior(
            EMouseLockMode::DoNotLock);

        PC->SetInputMode(InputMode);
    }
}

void UVoxelMainMenuWidget::BuildMenu()
{
    if (!WidgetTree)
    {
        return;
    }

    UVerticalBox* Menu =
        WidgetTree->ConstructWidget<UVerticalBox>(
            UVerticalBox::StaticClass());

    WidgetTree->RootWidget = Menu;

    NewGameButton =
        WidgetTree->ConstructWidget<UButton>(
            UButton::StaticClass());

    LoadGameButton =
        WidgetTree->ConstructWidget<UButton>(
            UButton::StaticClass());

    SaveGameButton =
        WidgetTree->ConstructWidget<UButton>(
            UButton::StaticClass());

    auto AddButton =
        [Menu](UButton* Button, const FString& Label)
    {
        UTextBlock* Text =
            NewObject<UTextBlock>(Button);

        Text->SetText(
            FText::FromString(Label));

        Text->SetJustification(
            ETextJustify::Center);

        Button->AddChild(Text);

        UVerticalBoxSlot* Slot =
            Menu->AddChildToVerticalBox(Button);

        Slot->SetPadding(
            FMargin(8.0f));
    };

    AddButton(
        NewGameButton,
        TEXT("Новая игра"));

    AddButton(
        LoadGameButton,
        TEXT("Загрузить игру"));

    AddButton(
        SaveGameButton,
        TEXT("Сохранить игру"));

    NewGameButton->OnClicked.AddDynamic(
        this,
        &UVoxelMainMenuWidget::OnNewGameClicked);

    LoadGameButton->OnClicked.AddDynamic(
        this,
        &UVoxelMainMenuWidget::OnLoadGameClicked);

    SaveGameButton->OnClicked.AddDynamic(
        this,
        &UVoxelMainMenuWidget::OnSaveGameClicked);
}

void UVoxelMainMenuWidget::OnNewGameClicked()
{
    if (AVoxelWorld* VoxelWorld =
        Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass())))
    {
        VoxelWorld->GenerateWorld();
    }

    CloseMenu();
}

void UVoxelMainMenuWidget::OnLoadGameClicked()
{
    if (AVoxelWorld* VoxelWorld =
        Cast<AVoxelWorld>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AVoxelWorld::StaticClass())))
    {
        VoxelWorld->LoadWorld();
    }

    CloseMenu();
}

void UVoxelMainMenuWidget::OnSaveGameClicked()
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

void UVoxelMainMenuWidget::CloseMenu()
{
    RemoveFromParent();

    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->bShowMouseCursor = false;
        PC->SetPause(false);

        FInputModeGameOnly InputMode;
        PC->SetInputMode(InputMode);
    }
}
