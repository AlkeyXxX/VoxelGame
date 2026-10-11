#include "SmoothVoxelOctreeWorld.h"

#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "SmoothVoxelTransvoxelMesher.h"

ASmoothVoxelTransvoxelChunk::ASmoothVoxelTransvoxelChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransvoxelMesh"));
	RootComponent = Mesh;
	Mesh->bUseAsyncCooking = true;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetMobility(EComponentMobility::Movable);
}

void ASmoothVoxelTransvoxelChunk::SetTerrainMaterial(UMaterialInterface* InMaterial)
{
	Material = InMaterial;
	if (Mesh)
	{
		Mesh->SetMaterial(0, Material);
	}
}

void ASmoothVoxelTransvoxelChunk::QueueBuild(
	const FSVoxelOctreeNode& Node,
	int32 InCellsPerAxis,
	float VoxelSize,
	uint8 TransitionFaceMask,
	const FSVoxelGenerationSettings& Settings,
	bool bCreateCollision)
{
	RequestedNode = Node;
	bHasNode = true;
	RequestedCellsPerAxis = InCellsPerAxis;
	RequestedVoxelSize = VoxelSize;
	RequestedFaceMask = TransitionFaceMask;
	bRequestedCollision = bCreateCollision;
	PendingSettings = Settings;
	PendingSettings.CellsPerAxis = InCellsPerAxis;
	PendingSettings.VoxelSize = VoxelSize;
	PendingSettings.ChunkWorldOrigin = Settings.ChunkWorldOrigin;

	++RequestedRevision;
	bBuildQueued = true;
	bBuildFailed = false;

	SetActorLocation(Settings.ChunkWorldOrigin);
	SetTerrainMaterial(Material);
}

bool ASmoothVoxelTransvoxelChunk::IsConfiguredFor(
	const FSVoxelOctreeNode& Node,
	int32 InCellsPerAxis,
	const FVector& ChunkOrigin,
	uint8 TransitionFaceMask,
	float VoxelSize,
	bool bCreateCollision) const
{
	return bHasNode &&
		RequestedNode == Node &&
		RequestedCellsPerAxis == InCellsPerAxis &&
		GetActorLocation().Equals(ChunkOrigin, KINDA_SMALL_NUMBER) &&
		RequestedFaceMask == TransitionFaceMask &&
		FMath::IsNearlyEqual(RequestedVoxelSize, VoxelSize) &&
		bRequestedCollision == bCreateCollision;
}

bool ASmoothVoxelTransvoxelChunk::IsReadyFor(
	const FSVoxelOctreeNode& Node,
	int32 InCellsPerAxis,
	const FVector& ChunkOrigin,
	uint8 TransitionFaceMask,
	float VoxelSize,
	bool bCreateCollision) const
{
	return IsConfiguredFor(
		Node, InCellsPerAxis, ChunkOrigin, TransitionFaceMask, VoxelSize, bCreateCollision) &&
		!bBuildQueued &&
		!bBuildInProgress &&
		!bBuildFailed &&
		CompletedRevision == RequestedRevision;
}

void ASmoothVoxelTransvoxelChunk::StartQueuedBuild()
{
	if (!bBuildQueued || bBuildInProgress)
	{
		return;
	}

	bBuildQueued = false;
	bBuildInProgress = true;

	const int32 WorkRevision = RequestedRevision;
	const int32 WorkCellsPerAxis = RequestedCellsPerAxis;
	const float WorkVoxelSize = RequestedVoxelSize;
	const uint8 WorkFaceMask = RequestedFaceMask;
	const bool bWorkCollision = bRequestedCollision;
	const FSVoxelGenerationSettings WorkSettings = PendingSettings;
	const TWeakObjectPtr<ASmoothVoxelTransvoxelChunk> WeakThis(this);
	const TSharedPtr<FSVoxelMeshData, ESPMode::ThreadSafe> Result =
		MakeShared<FSVoxelMeshData, ESPMode::ThreadSafe>();

	Async(EAsyncExecution::ThreadPool,
		[WeakThis, WorkRevision, WorkCellsPerAxis, WorkVoxelSize, WorkFaceMask,
			WorkSettings, bWorkCollision, Result]() mutable
		{
			FSVoxelTransvoxelMesher::Generate(
				WorkSettings,
				WorkCellsPerAxis,
				WorkVoxelSize,
				WorkFaceMask,
				*Result);

			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, WorkRevision, Result, bWorkCollision]() mutable
				{
					if (WeakThis.IsValid())
					{
						// Do not upload/render/cook many meshes in the same frame.
						// Hand the CPU result to the world manager, which commits at
						// most one completed chunk per Tick.
						WeakThis->QueueCompletedBuild(
							WorkRevision,
							Result,
							bWorkCollision);
					}
				});
		});
}

void ASmoothVoxelTransvoxelChunk::QueueCompletedBuild(
	int32 BuildRevision,
	const TSharedPtr<FSVoxelMeshData, ESPMode::ThreadSafe>& Data,
	bool bCreateCollision)
{
	check(IsInGameThread());

	if (BuildRevision != RequestedRevision)
	{
		bBuildInProgress = false;
		bBuildQueued = true;
		return;
	}

	PendingCompletedRevision = BuildRevision;
	PendingCompletedData = Data;
	bPendingCompletedCollision = bCreateCollision;
	bHasPendingBuildApply = true;
}

void ASmoothVoxelTransvoxelChunk::ApplyPendingBuild()
{
	check(IsInGameThread());
	if (!bHasPendingBuildApply || !PendingCompletedData.IsValid())
	{
		return;
	}

	const int32 BuildRevision = PendingCompletedRevision;
	const bool bCreateCollision = bPendingCompletedCollision;
	FSVoxelMeshData Data = MoveTemp(*PendingCompletedData);
	PendingCompletedData.Reset();
	bHasPendingBuildApply = false;
	PendingCompletedRevision = -1;

	CompleteBuild(BuildRevision, MoveTemp(Data), bCreateCollision);
}

void ASmoothVoxelTransvoxelChunk::CompleteBuild(
	int32 BuildRevision,
	FSVoxelMeshData&& Data,
	bool bCreateCollision)
{
	check(IsInGameThread());

	bBuildInProgress = false;

	// A newer request can replace this one while the worker is running. Ignore
	// stale geometry and leave the newest request queued for the manager.
	if (BuildRevision != RequestedRevision)
	{
		bBuildQueued = true;
		return;
	}

	if (!Mesh)
	{
		bBuildFailed = true;
		return;
	}

	if (Data.IsEmpty())
	{
		Mesh->ClearAllMeshSections();
	}
	else
	{
		Mesh->CreateMeshSection_LinearColor(
			0,
			Data.Vertices,
			Data.Triangles,
			Data.Normals,
			Data.UV0,
			TArray<FVector2D>(),
			TArray<FVector2D>(),
			TArray<FVector2D>(),
			TArray<FLinearColor>(),
			Data.Tangents,
			bCreateCollision);
	}

	CompletedRevision = BuildRevision;
	bBuildFailed = false;
}

ASmoothVoxelOctreeWorld::ASmoothVoxelOctreeWorld()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASmoothVoxelOctreeWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (TPair<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*>& Pair : ActiveChunks)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	ActiveChunks.Reset();

	Super::EndPlay(EndPlayReason);
}

void ASmoothVoxelOctreeWorld::RegenerateWorld()
{
	bForceUpdate = true;
}

FSVoxelGenerationSettings ASmoothVoxelOctreeWorld::MakeGenerationSettings(
	const FVector& ChunkOrigin,
	const FIntVector& ChunkOriginCells,
	float EffectiveBaseVoxelSize,
	float VoxelSize) const
{
	FSVoxelGenerationSettings Settings;
	Settings.CellsPerAxis = CellsPerAxis;
	Settings.VoxelSize = VoxelSize;
	Settings.BaseVoxelSize = EffectiveBaseVoxelSize;
	Settings.ChunkOriginCells = ChunkOriginCells;
	Settings.bUseChunkLatticeCoordinates = true;
	Settings.NormalSampleStep = FMath::Max(5.0f, BaseVoxelSize * 0.5f);
	Settings.IsoLevel = 0.0f;
	Settings.Seed = Seed;
	Settings.BaseHeight = BaseHeight;
	Settings.HeightAmplitude = HeightAmplitude;

	Settings.NoiseScale = NoiseScale;
	Settings.DetailScale = DetailScale;

	const int32 SafeMaxLod = FMath::Clamp(MaxLod, 0, 4);
	const float CoarsestVoxelStep =
		FMath::Max(1.0f, EffectiveBaseVoxelSize) * static_cast<float>(1 << SafeMaxLod);
	FSVoxelGenerator::ClampNoiseScalesForMaxSampleStep(Settings, CoarsestVoxelStep);
	Settings.DetailAmplitude = DetailAmplitude;
	Settings.BottomDepth = BottomDepth;
	Settings.bEnableCaves = bEnableCaves;
	Settings.CaveScale = CaveScale;
	Settings.CaveThreshold = CaveThreshold;
	Settings.CaveStrength = CaveStrength;
	Settings.ChunkWorldOrigin = ChunkOrigin;
	return Settings;
}

void ASmoothVoxelOctreeWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bAutoGenerate || !GetWorld())
	{
		return;
	}

	AActor* Target = FollowActor;
	if (!IsValid(Target))
	{
		Target = UGameplayStatics::GetPlayerPawn(this, 0);
	}
	const FVector CameraLocation = IsValid(Target) ? Target->GetActorLocation() : GetActorLocation();

	// CreateMeshSection_LinearColor and mesh/collision resource creation still
	// need game-thread work. Apply only one finished CPU mesh per frame to avoid
	// bursts of completions freezing the editor/game during fast flight.
	ApplyOneCompletedBuild(CameraLocation);

	// Do not keep changing the desired tree while a replacement is still being
	// generated. This prevents moving-camera churn and lets old meshes stay up.
	if (bApplyingSelection)
	{
		// Spawn/reconfigure only a small number of chunk actors per frame.
		// SpawnActor and component registration are game-thread operations and
		// doing this for hundreds of leaves in one selection caused hard hitches.
		PrepareDesiredChunks(CameraLocation);
		LaunchQueuedBuilds(CameraLocation);
		if (AreDesiredChunksReady())
		{
			// Switch visibility atomically, but do not destroy a potentially large
			// set of old actors in this frame. Hide obsolete chunks, reveal the ready
			// selection, and retire hidden actors gradually on following frames.
			for (TPair<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*>& Pair : ActiveChunks)
			{
				if (!DesiredNodes.Contains(Pair.Key) && IsValid(Pair.Value))
				{
					Pair.Value->SetActorHiddenInGame(true);
				}
			}
			for (const FSVoxelOctreeNode& Node : DesiredNodes)
			{
				if (ASmoothVoxelTransvoxelChunk* Chunk = ActiveChunks.FindRef(Node))
				{
					if (IsValid(Chunk))
					{
						Chunk->SetActorHiddenInGame(false);
					}
				}
			}
			bApplyingSelection = false;
			bHasCompletedSelection = true;
			LastCompletedCameraLocation = SelectionCameraLocation;
		}
		return;
	}

	// Actor destruction and component teardown also touch the game thread, so
	// clean obsolete hidden chunks within the same per-frame budget.
	RetireUndesiredChunks(FMath::Clamp(MaxChunkRequestsPerFrame, 1, 16));

	TimeSinceSelection += DeltaSeconds;
	const bool bCameraMoved =
		!bHasCompletedSelection ||
		FVector::DistSquared(CameraLocation, LastCompletedCameraLocation) >=
			FMath::Square(FMath::Max(1000.0f, UpdateDistance));
	const bool bIntervalElapsed =
		TimeSinceSelection >= FMath::Max(0.05f, UpdateIntervalSeconds);

	if (bForceUpdate || (bIntervalElapsed && (bSelectionFailed || !bHasCompletedSelection || bCameraMoved)))
	{
		RebuildForCamera(CameraLocation);
		bForceUpdate = false;
		TimeSinceSelection = 0.0f;
	}

	LaunchQueuedBuilds(CameraLocation);
}

void ASmoothVoxelOctreeWorld::RebuildForCamera(const FVector& CameraLocation)
{
	if (!GetWorld())
	{
		return;
	}

	FSVoxelOctreeSelectionSettings SelectionSettings;
	SelectionSettings.CellsPerAxis = FMath::Clamp(CellsPerAxis, 4, 128);
	// The world is a finite, fixed window: never larger than 400,000 cm (4 km)
	// on an axis. Use the octree's power-of-two lattice but reduce the effective
	// base step slightly when needed so the default 32-cell, RootLod-7 window is
	// exactly 4 km rather than 4.096 km.
	constexpr float MaxWorldSizeCm = 400000.0f;
	SelectionSettings.MaxLod = FMath::Clamp(MaxLod, 0, 4);
	SelectionSettings.RootLod = FMath::Clamp(
		RootWindowLod, SelectionSettings.MaxLod, 7);
	const float RootCellCount = static_cast<float>(
		SelectionSettings.CellsPerAxis * (1 << SelectionSettings.RootLod));
	const float MaxCellSizeForWorldBound = MaxWorldSizeCm / RootCellCount;
	SelectionSettings.BaseVoxelSize = FMath::Min(
		FMath::Max(1.0f, BaseVoxelSize), MaxCellSizeForWorldBound);
	SelectionSettings.SplitDistanceMultiplier = FMath::Clamp(SplitDistanceMultiplier, 0.05f, 4.0f);
	SelectionSettings.MaxLeafCount = FMath::Max(32, MaxLeafCount);

	const float RootSize = static_cast<float>(SelectionSettings.CellsPerAxis) *
		static_cast<float>(1 << SelectionSettings.RootLod) *
		SelectionSettings.BaseVoxelSize;

	// Fixed finite world: moving the player never shifts the root or creates
	// more terrain beyond its configured bounds. RootWorldCenter is the centre
	// of the world window in all three axes.
	const FVector RootCenter = RootWorldCenter;
	const FVector RootOrigin = RootCenter - FVector(RootSize * 0.5f);
	SelectionSettings.WorldOrigin = FVector::ZeroVector;
	SelectionSettings.RootOriginCells = FIntVector(
		FMath::RoundToInt(RootOrigin.X / SelectionSettings.BaseVoxelSize),
		FMath::RoundToInt(RootOrigin.Y / SelectionSettings.BaseVoxelSize),
		FMath::RoundToInt(RootOrigin.Z / SelectionSettings.BaseVoxelSize));

	TArray<FSVoxelOctreeNode> Leaves;
	if (!FSVoxelOctreeSelector::BuildBalancedLeaves(CameraLocation, SelectionSettings, Leaves))
	{
		bSelectionFailed = true;
		UE_LOG(LogTemp, Warning,
			TEXT("SmoothVoxelOctreeWorld: octree selection failed (leaf cap may be too small); keeping current terrain."));
		return;
	}

	// Remove leaves that cannot contain an isosurface. The noise amplitudes
	// define a conservative terrain-height envelope. When caves are enabled,
	// only the air-above-terrain test is safe because cave carving can extend
	// into otherwise solid columns.
	const float HeightSafetyMargin = SelectionSettings.BaseVoxelSize * 2.0f;
	const float MinSurfaceHeight = BaseHeight -
		FMath::Abs(HeightAmplitude) - FMath::Abs(DetailAmplitude);
	const float MaxSurfaceHeight = BaseHeight +
		FMath::Abs(HeightAmplitude) + FMath::Abs(DetailAmplitude);
	Leaves.RemoveAll([&](const FSVoxelOctreeNode& Node)
	{
		const FVector NodeOrigin = SelectionSettings.WorldOrigin +
			FVector(Node.OriginCells.X, Node.OriginCells.Y, Node.OriginCells.Z) *
				SelectionSettings.BaseVoxelSize;
		const float NodeMaxZ = NodeOrigin.Z +
			Node.GetWorldSize(SelectionSettings.CellsPerAxis, SelectionSettings.BaseVoxelSize);

		if (NodeOrigin.Z > MaxSurfaceHeight + HeightSafetyMargin)
		{
			return true;
		}
		if (!bEnableCaves && NodeMaxZ < MinSurfaceHeight - HeightSafetyMargin)
		{
			return true;
		}
		return false;
	});

	if (Leaves.Num() == 0)
	{
		for (TPair<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*>& Pair : ActiveChunks)
		{
			if (IsValid(Pair.Value))
			{
				Pair.Value->Destroy();
			}
		}
		ActiveChunks.Reset();
		DesiredNodes.Reset();
		DesiredFaceMasks.Reset();
		DesiredChunkOrigins.Reset();
		DesiredVoxelSizes.Reset();
		DesiredCollisionFlags.Reset();
		PreparedDesiredNodes.Reset();
		bForceBuildDesiredSelection = false;
		bApplyingSelection = false;
		bSelectionFailed = false;
		bForceUpdate = false;
		TimeSinceSelection = 0.0f;
		bHasCompletedSelection = true;
		SelectionCameraLocation = CameraLocation;
		LastCompletedCameraLocation = CameraLocation;
		return;
	}

	TArray<uint8> FaceMasks;
	if (!FSVoxelOctreeSelector::BuildTransitionFaceMasks(
		Leaves, SelectionSettings.CellsPerAxis, SelectionSettings.MaxLod,
		FaceMasks, SelectionSettings.RootOriginCells, SelectionSettings.RootLod))
	{
		bSelectionFailed = true;
		UE_LOG(LogTemp, Warning, TEXT("SmoothVoxelOctreeWorld: transition mask generation failed; keeping current terrain."));
		return;
	}

	TSet<FSVoxelOctreeNode> NewDesiredNodes;
	TMap<FSVoxelOctreeNode, uint8> NewDesiredMasks;
	TMap<FSVoxelOctreeNode, FVector> NewDesiredOrigins;
	TMap<FSVoxelOctreeNode, float> NewDesiredVoxelSizes;
	TMap<FSVoxelOctreeNode, bool> NewDesiredCollision;
	NewDesiredNodes.Reserve(Leaves.Num());
	NewDesiredMasks.Reserve(Leaves.Num());
	NewDesiredOrigins.Reserve(Leaves.Num());
	NewDesiredVoxelSizes.Reserve(Leaves.Num());
	NewDesiredCollision.Reserve(Leaves.Num());

	const float CollisionRadiusSq = FMath::Square(FMath::Max(0.0f, CollisionRadius));
	const bool bForceBuild = bForceUpdate;

	for (int32 Index = 0; Index < Leaves.Num(); ++Index)
	{
		const FSVoxelOctreeNode& Node = Leaves[Index];
		const uint8 FaceMask = FaceMasks[Index];
		const float VoxelSize = SelectionSettings.BaseVoxelSize * static_cast<float>(1 << Node.Lod);
		const FVector ChunkOrigin = SelectionSettings.WorldOrigin +
			FVector(Node.OriginCells.X, Node.OriginCells.Y, Node.OriginCells.Z) *
				SelectionSettings.BaseVoxelSize;
		const float NodeWorldSize = Node.GetWorldSize(
			SelectionSettings.CellsPerAxis, SelectionSettings.BaseVoxelSize);
		const FVector ChunkBoundsSize(NodeWorldSize);
		const FBox ChunkBounds(ChunkOrigin, ChunkOrigin + ChunkBoundsSize);

		// Test the distance to the chunk AABB, not its centre. A player can be
		// standing on a chunk edge while the centre of a coarse chunk is far
		// outside CollisionRadius; the centre-only test left such terrain
		// visible but non-collidable.
		const bool bCollision =
			bGenerateNearCollision &&
			Node.Lod <= FMath::Clamp(CollisionMaxLod, 0, 8) &&
			ChunkBounds.ComputeSquaredDistanceToPoint(CameraLocation) <= CollisionRadiusSq;

		NewDesiredNodes.Add(Node);
		NewDesiredMasks.Add(Node, FaceMask);
		NewDesiredOrigins.Add(Node, ChunkOrigin);
		NewDesiredVoxelSizes.Add(Node, VoxelSize);
		NewDesiredCollision.Add(Node, bCollision);
	}

	DesiredNodes = MoveTemp(NewDesiredNodes);
	DesiredFaceMasks = MoveTemp(NewDesiredMasks);
	DesiredChunkOrigins = MoveTemp(NewDesiredOrigins);
	DesiredVoxelSizes = MoveTemp(NewDesiredVoxelSizes);
	DesiredCollisionFlags = MoveTemp(NewDesiredCollision);
	DesiredCellsPerAxis = SelectionSettings.CellsPerAxis;
	DesiredBaseVoxelSize = SelectionSettings.BaseVoxelSize;
	PreparedDesiredNodes.Reset();
	bForceBuildDesiredSelection = bForceBuild;
	SelectionCameraLocation = CameraLocation;
	bApplyingSelection = true;
	bSelectionFailed = false;
	bForceUpdate = false;
}

void ASmoothVoxelOctreeWorld::PrepareDesiredChunks(const FVector& CameraLocation)
{
	check(IsInGameThread());

	TArray<FSVoxelOctreeNode> PendingNodes;
	PendingNodes.Reserve(DesiredNodes.Num());
	for (const FSVoxelOctreeNode& Node : DesiredNodes)
	{
		if (!PreparedDesiredNodes.Contains(Node))
		{
			PendingNodes.Add(Node);
		}
	}

	PendingNodes.Sort([this, &CameraLocation](const FSVoxelOctreeNode& A, const FSVoxelOctreeNode& B)
	{
		const FVector* OriginA = DesiredChunkOrigins.Find(A);
		const FVector* OriginB = DesiredChunkOrigins.Find(B);
		const float DistanceA = OriginA
			? FVector::DistSquared(*OriginA, CameraLocation)
			: TNumericLimits<float>::Max();
		const float DistanceB = OriginB
			? FVector::DistSquared(*OriginB, CameraLocation)
			: TNumericLimits<float>::Max();
		return DistanceA < DistanceB;
	});

	const int32 Budget = FMath::Clamp(MaxChunkRequestsPerFrame, 1, 16);
	int32 Attempts = 0;
	for (const FSVoxelOctreeNode& Node : PendingNodes)
	{
		if (Attempts >= Budget)
		{
			break;
		}
		++Attempts;

		const FVector* ChunkOrigin = DesiredChunkOrigins.Find(Node);
		const float* VoxelSize = DesiredVoxelSizes.Find(Node);
		const uint8* FaceMask = DesiredFaceMasks.Find(Node);
		const bool* bCollision = DesiredCollisionFlags.Find(Node);
		if (!ChunkOrigin || !VoxelSize || !FaceMask || !bCollision)
		{
			continue;
		}

		ASmoothVoxelTransvoxelChunk* Chunk = ActiveChunks.FindRef(Node);
		if (!IsValid(Chunk))
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Chunk = GetWorld()->SpawnActor<ASmoothVoxelTransvoxelChunk>(
				*ChunkOrigin,
				FRotator::ZeroRotator,
				SpawnParams);
			if (!IsValid(Chunk))
			{
				UE_LOG(LogTemp, Warning, TEXT("SmoothVoxelOctreeWorld: failed to spawn a chunk actor."));
				continue;
			}

			// New leaves stay hidden during LOD replacement until the whole new
			// selection is ready. Initial world creation has no old mesh to overlap.
			Chunk->SetActorHiddenInGame(bHasCompletedSelection);
			Chunk->SetTerrainMaterial(TerrainMaterial);
			ActiveChunks.Add(Node, Chunk);
		}
		else
		{
			Chunk->SetTerrainMaterial(TerrainMaterial);
		}

		if (bForceBuildDesiredSelection || !Chunk->IsConfiguredFor(
			Node, DesiredCellsPerAxis, *ChunkOrigin, *FaceMask, *VoxelSize, *bCollision))
		{
			const FSVoxelGenerationSettings GeneratorSettings =
				MakeGenerationSettings(
					*ChunkOrigin,
					Node.OriginCells,
					DesiredBaseVoxelSize,
					*VoxelSize);
			Chunk->QueueBuild(
				Node,
				DesiredCellsPerAxis,
				*VoxelSize,
				*FaceMask,
				GeneratorSettings,
				*bCollision);
		}

		PreparedDesiredNodes.Add(Node);
	}

	if (PreparedDesiredNodes.Num() >= DesiredNodes.Num())
	{
		bForceBuildDesiredSelection = false;
	}
}

void ASmoothVoxelOctreeWorld::ApplyOneCompletedBuild(const FVector& CameraLocation)
{
	ASmoothVoxelTransvoxelChunk* BestChunk = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();

	for (TPair<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*>& Pair : ActiveChunks)
	{
		ASmoothVoxelTransvoxelChunk* Chunk = Pair.Value;
		if (!IsValid(Chunk) || !Chunk->HasPendingBuildApply())
		{
			continue;
		}

		const float DistanceSquared =
			FVector::DistSquared(Chunk->GetActorLocation(), CameraLocation);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestChunk = Chunk;
		}
	}

	if (IsValid(BestChunk))
	{
		BestChunk->ApplyPendingBuild();
	}
}

void ASmoothVoxelOctreeWorld::LaunchQueuedBuilds(const FVector& CameraLocation)
{
	int32 InFlightCount = 0;
	TArray<ASmoothVoxelTransvoxelChunk*> QueuedChunks;

	for (TPair<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*>& Pair : ActiveChunks)
	{
		ASmoothVoxelTransvoxelChunk* Chunk = Pair.Value;
		if (!IsValid(Chunk))
		{
			continue;
		}

		if (Chunk->IsBuildInProgress())
		{
			++InFlightCount;
		}

		if (DesiredNodes.Contains(Pair.Key) && Chunk->IsBuildQueued() && !Chunk->IsBuildInProgress())
		{
			QueuedChunks.Add(Chunk);
		}
	}

	QueuedChunks.Sort([CameraLocation](const ASmoothVoxelTransvoxelChunk& A, const ASmoothVoxelTransvoxelChunk& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), CameraLocation) <
			FVector::DistSquared(B.GetActorLocation(), CameraLocation);
	});

	// More worker tasks increase CPU contention and bunch up render-resource
	// uploads. Keep a small fixed ceiling; completed mesh commits are separately
	// budgeted to one per frame.
	const int32 Limit = FMath::Clamp(MaxConcurrentBuilds, 1, 2);
	for (ASmoothVoxelTransvoxelChunk* Chunk : QueuedChunks)
	{
		if (InFlightCount >= Limit)
		{
			break;
		}
		if (IsValid(Chunk) && Chunk->IsBuildQueued() && !Chunk->IsBuildInProgress())
		{
			Chunk->StartQueuedBuild();
			++InFlightCount;
		}
	}
}

bool ASmoothVoxelOctreeWorld::AreDesiredChunksReady() const
{
	if (DesiredNodes.Num() == 0 || PreparedDesiredNodes.Num() < DesiredNodes.Num())
	{
		return false;
	}

	for (const FSVoxelOctreeNode& Node : DesiredNodes)
	{
		const ASmoothVoxelTransvoxelChunk* Chunk = ActiveChunks.FindRef(Node);
		if (!IsValid(Chunk))
		{
			return false;
		}

		const uint8* FaceMask = DesiredFaceMasks.Find(Node);
		if (!FaceMask)
		{
			return false;
		}

		const FVector* ChunkOrigin = DesiredChunkOrigins.Find(Node);
		const float* VoxelSize = DesiredVoxelSizes.Find(Node);
		const bool* bCollision = DesiredCollisionFlags.Find(Node);
		if (!ChunkOrigin || !VoxelSize || !bCollision ||
			!Chunk->IsReadyFor(
				Node, DesiredCellsPerAxis, *ChunkOrigin, *FaceMask, *VoxelSize, *bCollision))
		{
			return false;
		}
	}

	return true;
}

void ASmoothVoxelOctreeWorld::RetireUndesiredChunks(int32 MaxChunksToRetire)
{
	int32 RetiredCount = 0;
	const int32 Budget = FMath::Clamp(MaxChunksToRetire, 1, 16);

	for (auto It = ActiveChunks.CreateIterator(); It && RetiredCount < Budget; ++It)
	{
		if (!DesiredNodes.Contains(It.Key()))
		{
			if (IsValid(It.Value()))
			{
				// Obsolete chunks have already been hidden at the selection swap.
				// Spread actor/component teardown over multiple frames.
				It.Value()->Destroy();
			}
			It.RemoveCurrent();
			++RetiredCount;
		}
	}
}
