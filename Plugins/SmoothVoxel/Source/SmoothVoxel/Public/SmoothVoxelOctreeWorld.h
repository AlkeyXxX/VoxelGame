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
class APlayerController;

// World dimensions are selected per new world in the editor. Keep these values
// in centimetres in code (Unreal's default world units) for exact grid bounds.
UENUM(BlueprintType)
enum class ESmoothVoxelWorldSize : uint8
{
	Size2Km UMETA(DisplayName = "2 x 2 km"),
	Size4Km UMETA(DisplayName = "4 x 4 km"),
	Size8Km UMETA(DisplayName = "8 x 8 km")
};

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
 * A single world-aligned water surface owned by ASmoothVoxelOctreeWorld.
 * The terrain remains the source of shoreline shape: opaque terrain above the
 * sea level naturally occludes this surface, while low terrain is flooded.
 */
UCLASS()
class SMOOTHVOXEL_API ASmoothVoxelWaterSurface : public AActor
{
	GENERATED_BODY()

public:
	ASmoothVoxelWaterSurface();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Water")
	UProceduralMeshComponent* Mesh = nullptr;

	/** Builds level lake/ocean water plus continuous river ribbons near the terrain surface. */
	void ConfigureSurface(
		const FVector& WorldCenter,
		float WorldSize,
		float InSeaLevel,
		float InTileSize,
		int32 InResolution,
		UMaterialInterface* InMaterial,
		const FSVoxelGenerationSettings& InGenerationSettings,
		bool bEnableCollision);

private:
	void BuildSurfaceMesh(
		float WorldSize,
		float InTileSize,
		int32 InResolution,
		const FSVoxelGenerationSettings& GenerationSettings,
		bool bEnableCollision);

	UPROPERTY()
	UMaterialInterface* FallbackWaterMaterial = nullptr;

	UPROPERTY()
	UMaterialInterface* ActiveMaterial = nullptr;

	FVector ConfiguredWorldCenter = FVector::ZeroVector;
	float ConfiguredWorldSize = -1.0f;
	float ConfiguredSeaLevel = 0.0f;
	float ConfiguredTileSize = 5000.0f;
	int32 ConfiguredResolution = 0;
	FSVoxelGenerationSettings ConfiguredGenerationSettings;
	bool bConfiguredCollision = false;
	bool bHasConfiguration = false;
};

/**
 * Primary procedural world. Owns Octree selection, Transvoxel mesh streaming,
 * biome sampling, and block editing without depending on the legacy AVoxelWorld.
 */
UCLASS(BlueprintType, Blueprintable)
class SMOOTHVOXEL_API ASmoothVoxelOctreeWorld : public AActor
{
	GENERATED_BODY()

public:
	ASmoothVoxelOctreeWorld();

	virtual void BeginPlay() override;
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

	/** Chunk budget kept conservative to avoid long frame stalls while streaming is still being optimized. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="32", ClampMax="2048"))
	int32 MaxLeafCount = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="4", ClampMax="128"))
	int32 CellsPerAxis = 32;

	/** Runtime-effective sample spacing is derived from WorldSize so the map bounds stay exact. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Octree", meta=(Units="cm"))
	float BaseVoxelSize = 97.65625f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="0", ClampMax="4"))
	int32 MaxLod = 4;

	/** Select the finite map dimensions for this world instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World", meta=(DisplayName="World Size"))
	ESmoothVoxelWorldSize WorldSize = ESmoothVoxelWorldSize::Size4Km;

	/** LOD refinement distance multiplier. Higher values create more chunks and transition boundaries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Octree", meta=(ClampMin="0.05", ClampMax="4.0"))
	float SplitDistanceMultiplier = 0.75f;

	/** Fixed centre of the finite world bounds. The world does not follow the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
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

	/** Enables the single sea-level water sheet for this finite Octree world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
	bool bEnableWater = true;

	/** Absolute world-space Z for oceans and any low basin intersecting this plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(Units="cm"))
	float SeaLevel = 0.0f;

	/** World-space size represented by one full water-material UV tile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(ClampMin="100.0", Units="cm"))
	float WaterTileSize = 5000.0f;

	/** Retained for Blueprint compatibility; flat water uses a single quad regardless of resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(ClampMin="16", ClampMax="512"))
	int32 WaterGridResolution = 512;

	/** Off by default so the water does not behave like a solid floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
	bool bWaterCollision = false;

	/** Assign a project water material here. Starter Content water is used if available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
	UMaterialInterface* WaterMaterial = nullptr;

	/** Enable seed-stable carved river valleys; the level sea plane fills them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
	bool bEnableRivers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(ClampMin="0.000001", ClampMax="0.00005"))
	float RiverScale = 0.00001f;

	/** Approximate river half-width in centimetres. Keep channels narrow relative to the map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(ClampMin="500.0", Units="cm"))
	float RiverHalfWidth = 4000.0f;

	/** Channel bed target is this far below SeaLevel, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(ClampMin="50.0", Units="cm"))
	float RiverDepth = 600.0f;

	/** Enables deterministic forest/plains/desert/mountain/snow terrain masks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes")
	bool bEnableBiomes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes", meta=(ClampMin="0.000001", ClampMax="0.00005"))
	float BiomeScale = 0.0000075f;

	/** Reserve many seed-stable clearings for future POI placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|POI", meta=(DisplayName="Generate POI Plateaus"))
	bool bEnablePOIPlateaus = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|POI", meta=(ClampMin="20000.0", Units="cm"))
	float POISiteSpacing = 65000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|POI", meta=(ClampMin="0.0", Units="cm"))
	float POIPlateauRadius = 6500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|POI", meta=(ClampMin="0.0", Units="cm"))
	float POIPlateauBlendDistance = 5000.0f;

	/** Optional overrides. Unassigned entries fall back to TerrainMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes")
	UMaterialInterface* PlainsBiomeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes")
	UMaterialInterface* ForestBiomeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes")
	UMaterialInterface* DesertBiomeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes")
	UMaterialInterface* MountainBiomeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes")
	UMaterialInterface* SnowBiomeMaterial = nullptr;

	UFUNCTION(BlueprintPure, Category="Voxel|Biomes")
	ESVoxelBiome GetBiomeAtWorldPosition(const FVector& WorldPosition) const;

	/** Returns 0..1; high values are generated flat clearings suitable for POIs. */
	UFUNCTION(BlueprintPure, Category="Voxel|POI")
	float GetPOISuitabilityAtWorldPosition(const FVector& WorldPosition) const;

	/** Query the currently selected Octree leaf containing a world-space position. */
	bool GetOctreeNodeAtWorldPosition(
		const FVector& WorldPosition,
		FSVoxelOctreeNode& OutNode) const;

	/** Query the current block interpretation and Octree leaf for an absolute base-cell coordinate. */
	bool GetBlockDebugInfoForCell(
		const FIntVector& Cell,
		EVoxelBlock& OutBlock,
		FIntVector& OutNodeOriginCells,
		int32& OutLod) const;

	bool GetBlockDebugInfoAtWorldPosition(
		const FVector& WorldPosition,
		EVoxelBlock& OutBlock,
		FIntVector& OutCell,
		FIntVector& OutNodeOriginCells,
		int32& OutLod) const;

	void GetTerrainDebugInfoAtWorldPosition(
		const FVector& WorldPosition,
		int32& OutSurfaceHeight,
		float& OutTerrainNoise,
		float& OutTemperature,
		float& OutMoisture,
		FString& OutBiomeName) const;

	UFUNCTION(BlueprintCallable, Category="Voxel|Streaming")
	void RegenerateWorld();

	/** Clear runtime terrain edits and placed cubes, then rebuild the seed-defined base world. */
	UFUNCTION(BlueprintCallable, Category="Voxel|World")
	void StartNewGame();

	UFUNCTION(BlueprintPure, Category="Voxel|World")
	float GetWorldSizeCentimeters() const;

	UFUNCTION(BlueprintPure, Category="Voxel|World")
	static float GetWorldSizeCentimetersForPreset(ESmoothVoxelWorldSize InWorldSize);

	/** Root levels are part of the size preset so the global voxel spacing is identical across 2K/4K/8K. */
	static int32 GetRootLodForWorldSizePreset(ESmoothVoxelWorldSize InWorldSize);

	/** Raise the far-world LOD when the requested tree depth cannot fit the leaf budget. */
	static int32 GetEffectiveMaxLodForBudget(
		int32 RootLod, int32 RequestedMaxLod, int32 MaxLeafCount);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Blocks")
	bool bEnableBlockEditing = true;

	/** Bind BreakBlock/PlaceBlock input actions directly to this Octree world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Blocks")
	bool bBindPlayerInteractionInput = true;

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
	void UpdateWaterSurface();
	void TryBindPlayerInteractionInput();
	void HandleBreakBlockInput();
	void HandlePlaceBlockInput();
	void RebuildForCamera(const FVector& CameraLocation);
	void PrepareDesiredChunks(const FVector& CameraLocation);
	void LaunchQueuedBuilds(const FVector& CameraLocation);
	void ApplyOneCompletedBuild(const FVector& CameraLocation);
	bool AreDesiredChunksReady() const;
	void RetireUndesiredChunks(int32 MaxChunksToRetire);
	int32 GetRootLodForWorldSize() const { return GetRootLodForWorldSizePreset(WorldSize); }

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

	// Guard same-frame duplicate calls from existing Blueprint input handlers and this actor.
	uint64 LastBreakInteractionFrame = 0;
	uint64 LastPlaceInteractionFrame = 0;

	// Bind only once, and retry when the PlayerController appears after BeginPlay.
	TWeakObjectPtr<APlayerController> InteractionInputController;
	bool bInteractionActionsBound = false;

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

	UPROPERTY(Transient)
	ASmoothVoxelWaterSurface* WaterSurfaceActor = nullptr;

	int32 DesiredCellsPerAxis = 32;
	float DesiredBaseVoxelSize = 97.65625f;
	bool bForceBuildDesiredSelection = false;

	FVector SelectionCameraLocation = FVector::ZeroVector;
	FVector LastCompletedCameraLocation = FVector::ZeroVector;
	float TimeSinceSelection = 0.0f;
	bool bHasCompletedSelection = false;
	bool bForceUpdate = true;
	bool bApplyingSelection = false;
	bool bSelectionFailed = false;
};
