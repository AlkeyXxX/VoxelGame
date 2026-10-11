#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelOctree.h"
#include "SmoothVoxelTypes.h"
#include "SmoothVoxelOctreeWorld.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * One independently streamed chunk rendered with the new experimental
 * Transvoxel backend. This actor is only spawned by ASmoothVoxelOctreeWorld.
 */
UCLASS(BlueprintType, Blueprintable)
class SMOOTHVOXEL_API ASmoothVoxelTransvoxelChunk : public AActor
{
	GENERATED_BODY()

public:
	ASmoothVoxelTransvoxelChunk();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel")
	UProceduralMeshComponent* Mesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Rendering")
	UMaterialInterface* Material = nullptr;

	void SetTerrainMaterial(UMaterialInterface* InMaterial);

	void QueueBuild(
		const FSVoxelOctreeNode& Node,
		int32 CellsPerAxis,
		float VoxelSize,
		uint8 TransitionFaceMask,
		const FSVoxelGenerationSettings& Settings,
		bool bCreateCollision);

	void StartQueuedBuild();
	bool HasPendingBuildApply() const { return bHasPendingBuildApply; }
	void QueueCompletedBuild(
		int32 BuildRevision,
		const TSharedPtr<FSVoxelMeshData, ESPMode::ThreadSafe>& Data,
		bool bCreateCollision);
	void ApplyPendingBuild();

	bool IsConfiguredFor(
		const FSVoxelOctreeNode& Node,
		int32 InCellsPerAxis,
		const FVector& ChunkOrigin,
		uint8 TransitionFaceMask,
		float VoxelSize,
		bool bCreateCollision) const;

	bool IsReadyFor(
		const FSVoxelOctreeNode& Node,
		int32 InCellsPerAxis,
		const FVector& ChunkOrigin,
		uint8 TransitionFaceMask,
		float VoxelSize,
		bool bCreateCollision) const;

	bool IsBuildQueued() const { return bBuildQueued; }
	bool IsBuildInProgress() const { return bBuildInProgress; }
	const FSVoxelOctreeNode& GetNode() const { return RequestedNode; }
	uint8 GetRequestedFaceMask() const { return RequestedFaceMask; }
	float GetRequestedVoxelSize() const { return RequestedVoxelSize; }
	bool GetRequestedCollision() const { return bRequestedCollision; }

private:
	void CompleteBuild(
		int32 BuildRevision,
		FSVoxelMeshData&& Data,
		bool bCreateCollision);

	TSharedPtr<FSVoxelMeshData, ESPMode::ThreadSafe> PendingCompletedData;
	int32 PendingCompletedRevision = -1;
	bool bPendingCompletedCollision = false;
	bool bHasPendingBuildApply = false;

	FSVoxelOctreeNode RequestedNode;
	FSVoxelGenerationSettings PendingSettings;
	int32 RequestedCellsPerAxis = 32;
	float RequestedVoxelSize = 100.0f;
	uint8 RequestedFaceMask = 0;
	bool bRequestedCollision = false;

	int32 RequestedRevision = 0;
	int32 CompletedRevision = -1;
	bool bHasNode = false;
	bool bBuildQueued = false;
	bool bBuildInProgress = false;
	bool bBuildFailed = false;
};

/**
 * Separate opt-in octree terrain manager. Existing AVoxelWorld generation is
 * not changed: place this actor in a test level to exercise the new path.
 */
UCLASS(BlueprintType, Blueprintable)
class SMOOTHVOXEL_API ASmoothVoxelOctreeWorld : public AActor
{
	GENERATED_BODY()

public:
	ASmoothVoxelOctreeWorld();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
	bool bAutoGenerate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
	AActor* FollowActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="0.05"))
	float UpdateIntervalSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1.0"))
	float UpdateDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxConcurrentBuilds = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="32", ClampMax="8192"))
	int32 MaxLeafCount = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="4", ClampMax="128"))
	int32 CellsPerAxis = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="1.0"))
	float BaseVoxelSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="0", ClampMax="8"))
	int32 MaxLod = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="0.05", ClampMax="4.0"))
	float SplitDistanceMultiplier = 0.75f;

	/** Anchor for the moving root window. The active XY centre is snapped in
	 * half-root-size increments around this anchor as the followed actor moves;
	 * the vertical range stays fixed so upward flight does not discard terrain.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree")
	FVector RootWorldCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	float BaseHeight = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	float HeightAmplitude = 850.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	// Frequencies are in inverse centimetres. Keep the highest FBM octave
	// comfortably below the Nyquist limit of 100 cm terrain samples.
	float NoiseScale = 0.0008f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	float DetailScale = 0.0006f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	float DetailAmplitude = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	float BottomDepth = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
	bool bEnableCaves = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
	float CaveScale = 0.0030f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
	float CaveThreshold = 0.58f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
	float CaveStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Collision")
	bool bGenerateNearCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Collision", meta=(ClampMin="0", ClampMax="8"))
	int32 CollisionMaxLod = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Collision", meta=(ClampMin="0.0"))
	float CollisionRadius = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Rendering")
	UMaterialInterface* TerrainMaterial = nullptr;

	UFUNCTION(BlueprintCallable, Category="Voxel|Streaming")
	void RegenerateWorld();

	UFUNCTION(BlueprintPure, Category="Voxel|Streaming")
	int32 GetActiveChunkCount() const { return ActiveChunks.Num(); }

private:
	void RebuildForCamera(const FVector& CameraLocation);
	void LaunchQueuedBuilds(const FVector& CameraLocation);
	void ApplyOneCompletedBuild(const FVector& CameraLocation);
	bool AreDesiredChunksReady() const;
	void RetireUndesiredChunks();

	FSVoxelGenerationSettings MakeGenerationSettings(const FVector& ChunkOrigin, float VoxelSize) const;

	TMap<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*> ActiveChunks;
	TSet<FSVoxelOctreeNode> DesiredNodes;
	TMap<FSVoxelOctreeNode, uint8> DesiredFaceMasks;
	TMap<FSVoxelOctreeNode, FVector> DesiredChunkOrigins;
	TMap<FSVoxelOctreeNode, float> DesiredVoxelSizes;
	TMap<FSVoxelOctreeNode, bool> DesiredCollisionFlags;
	int32 DesiredCellsPerAxis = 32;

	FVector SelectionCameraLocation = FVector::ZeroVector;
	FVector LastCompletedCameraLocation = FVector::ZeroVector;
	float TimeSinceSelection = 0.0f;
	bool bHasCompletedSelection = false;
	bool bForceUpdate = true;
	bool bApplyingSelection = false;
	bool bSelectionFailed = false;
};
