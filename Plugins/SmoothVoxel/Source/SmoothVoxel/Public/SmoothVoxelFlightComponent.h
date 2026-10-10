#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SmoothVoxelFlightComponent.generated.h"

class ACharacter;
class APlayerController;
class UCharacterMovementComponent;
class UInputComponent;

/**
 * Optional debug flight controls for a player Character.
 *
 * Add this component to BP_VoxelPlayer (or another player Character).
 * F toggles flying, the existing WASD movement drives horizontal flight,
 * Space ascends, Left/Right Ctrl descends, and Left Shift selects the faster
 * speed. Collision remains enabled.
 */
UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class SMOOTHVOXEL_API USmoothVoxelFlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USmoothVoxelFlightComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Normal flight speed in Unreal centimetres per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug Flight", meta=(ClampMin="0.0"))
	float FlightSpeed = 3000.0f;

	/** Flight speed while Left Shift is held, in Unreal centimetres per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug Flight", meta=(ClampMin="0.0"))
	float SprintFlightSpeed = 9000.0f;

	/** Toggle debug flight with F. Exposed for testing and Blueprint use. */
	UFUNCTION(BlueprintCallable, Category="Debug Flight")
	void ToggleFlight();

	UFUNCTION(BlueprintPure, Category="Debug Flight")
	bool IsFlightEnabled() const { return bFlightEnabled; }

private:
	void TryBindInput();
	void BeginAscend();
	void EndAscend();
	void BeginDescend();
	void EndDescend();
	void SetFlightEnabled(bool bEnabled);

	UPROPERTY(Transient)
	ACharacter* Character = nullptr;

	UPROPERTY(Transient)
	APlayerController* BoundController = nullptr;

	UPROPERTY(Transient)
	UCharacterMovementComponent* CharacterMovement = nullptr;

	UPROPERTY(Transient)
	UInputComponent* FlightInputComponent = nullptr;

	bool bFlightEnabled = false;
	bool bAscending = false;
	bool bDescending = false;
	bool bSavedMovementSettings = false;
	TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
	uint8 PreviousCustomMovementMode = 0;
	float PreviousMaxFlySpeed = 600.0f;
};
