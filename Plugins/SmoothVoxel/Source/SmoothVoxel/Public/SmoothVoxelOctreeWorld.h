#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelOctree.h"
#include "SmoothVoxelTypes.h"
#include "VoxelTypes.h"
#include "SmoothVoxelOctreeWorld.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UDataTable;

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
 * A placed non-soil block. It uses six independent flat-shaded faces instead
 * of the smooth Transvoxel terrain field, so grass/stone/wood remain genuinely
 * cubic and are ray-traceable for mining.
 */
UCLASS()
class SMOOTHVOXEL_API ASmoothVoxelPlacedCubeBlock : public AActor
{
	GENERATED_BODY()

public:
	ASmoothVoxelPlacedCubeBlock();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Blocks")
	UProceduralMeshComponent* Mesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Blocks")
	EVoxelBlock BlockType = EVoxelBlock::Air;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Blocks")
	FIntVector BlockCell = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Blocks")
	int32 DamageStage = 0;

	void ConfigureBlock(
		EVoxelBlock InBlockType,
		const FIntVector& InCell,
		float InBlockSize,
		UMaterialInterface* InMaterial,
		int32 InDamageStage = 0);

	int32 RegisterDamageHit();
	int32 GetDamageStage() const { return DamageStage; }
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
	float UpdateDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="2"))
	int32 MaxConcurrentBuilds = 2;

	/** Limit actor creation/reconfiguration work per frame to avoid streaming hitches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxChunkRequestsPerFrame = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="32", ClampMax="8192"))
	int32 MaxLeafCount = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="4", ClampMax="128"))
	int32 CellsPerAxis = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="1.0"))
	float BaseVoxelSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="0", ClampMax="4"))
	int32 MaxLod = 4;

	/** Fixed world window scale. The default fills a 4 km square at default settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="4", ClampMax="7"))
	int32 RootWindowLod = 7;

	/** LOD refinement distance multiplier. Higher values create more chunks and transition boundaries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="0.05", ClampMax="4.0"))
	float SplitDistanceMultiplier = 0.75f;

	/** Fixed centre of the finite world bounds. The world does not follow the
	 * player; its horizontal size is capped at 4 km per axis.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree")
	FVector RootWorldCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	float BaseHeight = 900.0f;

	/** Terrain relief amplitude in centimetres. 200 means only 2 m either side of BaseHeight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain", meta=(Units="cm"))
	float HeightAmplitude = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	// Frequencies are in inverse centimetres. These defaults keep the third
	// FBM octave representable up to the supported 16 m cell spacing.
	float NoiseScale = 0.00005f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
	float DetailScale = 0.0000375f;

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
	float CollisionRadius = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Rendering")
	UMaterialInterface* TerrainMaterial = nullptr;

	UFUNCTION(BlueprintCallable, Category="Voxel|Streaming")
	void RegenerateWorld();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Blocks")
	bool bEnableBlockEditing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Blocks")
	UDataTable* BlockDataTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Blocks", meta=(ClampMin="100.0"))
	float InteractionDistance = 600.0f;

	/** Reserved for future tool-vs-block durability; mining is currently one-hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Blocks", meta=(ClampMin="0.1"))
	float TerrainMiningToolDamage = 1.0f;

	// Compatible with the existing player/Blueprint interaction calls.
	UFUNCTION(BlueprintCallable, Category="Voxel|Blocks")
	bool BreakBlockByRay();

	UFUNCTION(BlueprintCallable, Category="Voxel|Blocks")
	bool PlaceBlockByRay();

	UFUNCTION(BlueprintCallable, Category="Voxel|Blocks")
	bool PlaceBlockByRayWithType(EVoxelBlock BlockToPlace);

	UFUNCTION(BlueprintCallable, Category="Voxel|Blocks")
	void RemoveBlockAtWorld(const FVector& WorldPosition);

	UFUNCTION(BlueprintCallable, Category="Voxel|Blocks")
	void SetBlockAtWorld(const FVector& WorldPosition, EVoxelBlock Block);

	void SetBlockDataTable(UDataTable* InDataTable) { BlockDataTable = InDataTable; }

	UFUNCTION(BlueprintPure, Category="Voxel|Streaming")
	int32 GetActiveChunkCount() const { return ActiveChunks.Num(); }

	/** Called by chunk workers so idle ticks do not scan every active actor. */
	void NotifyWorkerBuildFinished();
	void EnqueueChunkBuild(ASmoothVoxelTransvoxelChunk* Chunk);
	void EnqueueCompletedChunk(ASmoothVoxelTransvoxelChunk* Chunk);

private:
	void RebuildForCamera(const FVector& CameraLocation);
	void PrepareDesiredChunks(const FVector& CameraLocation);
	void LaunchQueuedBuilds(const FVector& CameraLocation);
	void ApplyOneCompletedBuild(const FVector& CameraLocation);
	bool AreDesiredChunksReady() const;
	void RetireUndesiredChunks(int32 MaxChunksToRetire);

	bool TraceInteractionRay(FHitResult& OutHit) const;
	float GetEffectiveBlockSize() const;
	FIntVector WorldPositionToBlockCell(const FVector& WorldPosition) const;
	FVector BlockCellCenter(const FIntVector& Cell) const;
	FSVoxelGenerationSettings MakeBlockQuerySettings() const;
	float SampleCurrentDensityAt(const FVector& WorldPosition) const;
	EVoxelBlock GetBlockTypeForCell(const FIntVector& Cell) const;
	bool ApplyBlockCellEdit(
		const FIntVector& Cell,
		EVoxelBlock Block,
		int8 EditStateOverride = 0,
		const FVector* EditCenterOverride = nullptr);
	void RebuildChunksNearEditedCell(const FIntVector& Cell, bool bGridNode = false);
	void SchedulePostMiningMeshRepair(const FIntVector& Cell);

	FSVoxelGenerationSettings MakeGenerationSettings(
		const FVector& ChunkOrigin,
		const FIntVector& ChunkOriginCells,
		float EffectiveBaseVoxelSize,
		float VoxelSize,
		bool bFilterBlockEdits = true) const;

	TMap<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*> ActiveChunks;
	TArray<TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>> QueuedChunkBuilds;
	TArray<TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>> CompletedChunkBuilds;
	int32 InFlightMeshBuildCount = 0;
	TSet<FSVoxelOctreeNode> DesiredNodes;
	TMap<FSVoxelOctreeNode, uint8> DesiredFaceMasks;
	TMap<FSVoxelOctreeNode, FVector> DesiredChunkOrigins;
	TMap<FSVoxelOctreeNode, float> DesiredVoxelSizes;
	TMap<FSVoxelOctreeNode, bool> DesiredCollisionFlags;
	TSet<FSVoxelOctreeNode> PreparedDesiredNodes;

	// Block-cell edits: +1 adds smooth Dirt/Sand, negative values preserve
	// explicit legacy air cuts, and +2 marks a separately rendered hard cube.
	// Hard cubes never modify the procedural terrain density field.
	FSVoxelBlockEditMap EditedBlockCells;
	FSVoxelBlockEditCenterMap EditedBlockCenters;
	TMap<FIntVector, EVoxelBlock> EditedBlockTypes;
	TSharedPtr<const FSVoxelBlockEditMap, ESPMode::ThreadSafe> BlockEditSnapshot;
	TSharedPtr<const FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe> BlockEditCenterSnapshot;

	// A placed cube temporarily replaces the edit state of its target cell.
	// Restore the prior state when that cube is broken so removing a block
	// does not itself excavate terrain or refill a previously mined cell.
	TMap<FIntVector, int8> PrePlacedBlockEditStates;
	TMap<FIntVector, FVector> PrePlacedBlockEditCenters;
	TMap<FIntVector, EVoxelBlock> PrePlacedBlockEditTypes;

	// Completed mining cuts are keyed by absolute integer voxel cells. Each hit
	// removes one axis-aligned cell from the density field, producing stable
	// floors/walls while looking straight down.
	FSVoxelMinedNodeSet MinedTerrainNodes;
	TSharedPtr<const FSVoxelMinedNodeSet, ESPMode::ThreadSafe> MinedNodeSnapshot;

	UPROPERTY()
	TMap<FIntVector, ASmoothVoxelPlacedCubeBlock*> PlacedCubeBlocks;

	int32 DesiredCellsPerAxis = 32;
	float DesiredBaseVoxelSize = 100.0f;
	bool bForceBuildDesiredSelection = false;

	FVector SelectionCameraLocation = FVector::ZeroVector;
	FVector LastCompletedCameraLocation = FVector::ZeroVector;
	float TimeSinceSelection = 0.0f;
	bool bHasCompletedSelection = false;
	bool bForceUpdate = true;
	bool bApplyingSelection = false;
	bool bSelectionFailed = false;
};
