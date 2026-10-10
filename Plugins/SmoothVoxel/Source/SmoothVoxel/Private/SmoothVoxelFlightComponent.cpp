#include "SmoothVoxelFlightComponent.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

USmoothVoxelFlightComponent::USmoothVoxelFlightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USmoothVoxelFlightComponent::BeginPlay()
{
	Super::BeginPlay();

	Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SmoothVoxelFlightComponent must be attached to an ACharacter."));
		SetComponentTickEnabled(false);
		return;
	}

	CharacterMovement = Character->GetCharacterMovement();
	TryBindInput();
}

void USmoothVoxelFlightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetFlightEnabled(false);

	if (BoundController && FlightInputComponent)
	{
		BoundController->PopInputComponent(FlightInputComponent);
	}

	if (FlightInputComponent)
	{
		FlightInputComponent->DestroyComponent();
		FlightInputComponent = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void USmoothVoxelFlightComponent::TryBindInput()
{
	if (FlightInputComponent || !Character)
	{
		return;
	}

	APlayerController* PlayerController =
		Cast<APlayerController>(Character->GetController());

	if (!PlayerController)
	{
		PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	}

	// Components can BeginPlay just before a Blueprint pawn is possessed.
	// Keep trying each tick until this is the locally controlled player pawn.
	if (!PlayerController || PlayerController->GetPawn() != Character)
	{
		return;
	}

	BoundController = PlayerController;
	FlightInputComponent = NewObject<UInputComponent>(
		Character, TEXT("SmoothVoxelFlightInput"));

	if (!FlightInputComponent)
	{
		return;
	}

	FlightInputComponent->Priority = 100;
	FlightInputComponent->bBlockInput = false;
	FlightInputComponent->BindKey(
		EKeys::F, IE_Pressed, this, &USmoothVoxelFlightComponent::ToggleFlight);
	FlightInputComponent->BindKey(
		EKeys::SpaceBar, IE_Pressed, this, &USmoothVoxelFlightComponent::BeginAscend).bConsumeInput = false;
	FlightInputComponent->BindKey(
		EKeys::SpaceBar, IE_Released, this, &USmoothVoxelFlightComponent::EndAscend).bConsumeInput = false;
	FlightInputComponent->BindKey(
		EKeys::LeftControl, IE_Pressed, this, &USmoothVoxelFlightComponent::BeginDescend).bConsumeInput = false;
	FlightInputComponent->BindKey(
		EKeys::LeftControl, IE_Released, this, &USmoothVoxelFlightComponent::EndDescend).bConsumeInput = false;
	FlightInputComponent->BindKey(
		EKeys::RightControl, IE_Pressed, this, &USmoothVoxelFlightComponent::BeginDescend).bConsumeInput = false;
	FlightInputComponent->BindKey(
		EKeys::RightControl, IE_Released, this, &USmoothVoxelFlightComponent::EndDescend).bConsumeInput = false;

	FlightInputComponent->RegisterComponent();
	BoundController->PushInputComponent(FlightInputComponent);

	UE_LOG(LogTemp, Log,
		TEXT("SmoothVoxel flight controls ready: F toggle, WASD move, Space up, Ctrl down, Shift boost."));
}

void USmoothVoxelFlightComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!FlightInputComponent)
	{
		TryBindInput();
	}

	if (!bFlightEnabled || !Character || !CharacterMovement)
	{
		return;
	}

	const bool bFast = BoundController &&
		BoundController->IsInputKeyDown(EKeys::LeftShift);

	CharacterMovement->MaxFlySpeed = FMath::Max(
		0.0f, bFast ? SprintFlightSpeed : FlightSpeed);

	// The Blueprint's existing WASD bindings continue to provide forward and
	// strafe input. Vertical movement is added here, so diagonal movement works
	// naturally and uses the same normal/Shift speed setting.
	if (bAscending != bDescending)
	{
		Character->AddMovementInput(
			FVector::UpVector,
			bAscending ? 1.0f : -1.0f,
			false);
	}
}

void USmoothVoxelFlightComponent::ToggleFlight()
{
	if (!Character || !CharacterMovement)
	{
		return;
	}

	SetFlightEnabled(!bFlightEnabled);
}

void USmoothVoxelFlightComponent::SetFlightEnabled(bool bEnabled)
{
	if (bFlightEnabled == bEnabled || !CharacterMovement)
	{
		return;
	}

	if (bEnabled)
	{
		PreviousMovementMode = CharacterMovement->MovementMode;
		PreviousCustomMovementMode = CharacterMovement->CustomMovementMode;
		PreviousMaxFlySpeed = CharacterMovement->MaxFlySpeed;
		bSavedMovementSettings = true;

		bAscending = false;
		bDescending = false;
		CharacterMovement->StopMovementImmediately();
		CharacterMovement->SetMovementMode(MOVE_Flying);
		bFlightEnabled = true;
	}
	else
	{
		bAscending = false;
		bDescending = false;
		CharacterMovement->StopMovementImmediately();

		if (bSavedMovementSettings)
		{
			CharacterMovement->MaxFlySpeed = PreviousMaxFlySpeed;
			CharacterMovement->SetMovementMode(
				PreviousMovementMode, PreviousCustomMovementMode);
		}
		else
		{
			CharacterMovement->SetMovementMode(MOVE_Walking);
		}

		bSavedMovementSettings = false;
		bFlightEnabled = false;
	}

	if (GEngine)
	{
		const FString Message = bFlightEnabled
			? FString::Printf(
				TEXT("FLIGHT ON  |  Speed: %.0f cm/s  |  Shift: %.0f cm/s"),
				FlightSpeed, SprintFlightSpeed)
			: TEXT("FLIGHT OFF");
		GEngine->AddOnScreenDebugMessage(
			-1, 2.5f, bFlightEnabled ? FColor::Cyan : FColor::White, Message);
	}

	UE_LOG(LogTemp, Log, TEXT("SmoothVoxel flight %s."),
		bFlightEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void USmoothVoxelFlightComponent::BeginAscend()
{
	bAscending = true;
}

void USmoothVoxelFlightComponent::EndAscend()
{
	bAscending = false;
}

void USmoothVoxelFlightComponent::BeginDescend()
{
	bDescending = true;
}

void USmoothVoxelFlightComponent::EndDescend()
{
	bDescending = false;
}
