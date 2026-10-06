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

    /*
     * Input mode и keyboard focus настраиваются в AVoxelWorld
     * после AddToViewport(). До этого Slate ещё не гарантирует,
     * что виджет готов принимать клавиатуру.
     */
}

bool UVoxelMainMenuWidget::NativeSupportsKeyboardFocus() const
{
    return true;
}

FReply UVoxelMainMenuWidget::NativeOnPreviewKeyDown(
    const FGeometry& InGeometry,
    const FKeyEvent& InKeyEvent)
{
    /*
     * Preview получает клавишу раньше дочерних Button-ов,
     * поэтому Escape работает независимо от того, какая
     * кнопка сейчас находится в фокусе.
     */
    if (InKeyEvent.GetKey() == EKeys::Escape)
    {
        CloseMenu();
        return FReply::Handled();
    }

    return Super::NativeOnPreviewKeyDown(
        InGeometry,
        InKeyEvent);
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
    StartNewGame();
}

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

    CloseMenu();
}

void UVoxelMainMenuWidget::OnLoadGameClicked()
{
    LoadGame();
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

    CloseMenu();
}

void UVoxelMainMenuWidget::OnSaveGameClicked()
{
    SaveGame();
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

void UVoxelMainMenuWidget::CloseMenu()
{
    RemoveFromParent();

    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->bShowMouseCursor = false;

        /*
         * Сначала снимаем паузу, затем возвращаем GameOnly.
         * Это полностью отдаёт управление обратно персонажу.
         */
        PC->SetPause(false);

        PC->SetIgnoreMoveInput(false);
        PC->SetIgnoreLookInput(false);

        FInputModeGameOnly InputMode;
        PC->SetInputMode(InputMode);
    }
}
