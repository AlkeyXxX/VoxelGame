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
	uint8 TransitionFaceMask,
	float VoxelSize,
	bool bCreateCollision) const
{
	return bHasNode &&
		RequestedNode == Node &&
		RequestedFaceMask == TransitionFaceMask &&
		FMath::IsNearlyEqual(RequestedVoxelSize, VoxelSize) &&
		bRequestedCollision == bCreateCollision;
}

bool ASmoothVoxelTransvoxelChunk::IsReadyFor(
	const FSVoxelOctreeNode& Node,
	uint8 TransitionFaceMask,
	float VoxelSize,
	bool bCreateCollision) const
{
	return IsConfiguredFor(Node, TransitionFaceMask, VoxelSize, bCreateCollision) &&
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
						WeakThis->CompleteBuild(
							WorkRevision,
							MoveTemp(*Result),
							bWorkCollision);
					}
				});
		});
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
			TArray<FProcMeshTangent>(),
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
	float VoxelSize) const
{
	FSVoxelGenerationSettings Settings;
	Settings.CellsPerAxis = CellsPerAxis;
	Settings.VoxelSize = VoxelSize;
	Settings.IsoLevel = 0.0f;
	Settings.Seed = Seed;
	Settings.BaseHeight = BaseHeight;
	Settings.HeightAmplitude = HeightAmplitude;
	Settings.NoiseScale = NoiseScale;
	Settings.DetailScale = DetailScale;
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

	// Do not keep changing the desired tree while a replacement is still being
	// generated. This prevents moving-camera churn and lets old meshes stay up.
	if (bApplyingSelection)
	{
		LaunchQueuedBuilds(CameraLocation);
		if (AreDesiredChunksReady())
		{
			RetireUndesiredChunks();
			bApplyingSelection = false;
			bHasCompletedSelection = true;
			LastCompletedCameraLocation = SelectionCameraLocation;
		}
		return;
	}

	TimeSinceSelection += DeltaSeconds;
	const bool bCameraMoved =
		!bHasCompletedSelection ||
		FVector::DistSquared(CameraLocation, LastCompletedCameraLocation) >=
			FMath::Square(FMath::Max(1.0f, UpdateDistance));

	if (bForceUpdate ||
		(!bHasCompletedSelection) ||
		(TimeSinceSelection >= FMath::Max(0.05f, UpdateIntervalSeconds) && bCameraMoved))
	{
		RebuildForCamera(CameraLocation);
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
	SelectionSettings.BaseVoxelSize = FMath::Max(1.0f, BaseVoxelSize);
	SelectionSettings.MaxLod = FMath::Clamp(MaxLod, 0, 8);
	SelectionSettings.SplitDistanceMultiplier = FMath::Clamp(SplitDistanceMultiplier, 0.05f, 4.0f);
	SelectionSettings.MaxLeafCount = FMath::Max(32, MaxLeafCount);

	const float RootSize = static_cast<float>(SelectionSettings.CellsPerAxis) *
		static_cast<float>(1 << SelectionSettings.MaxLod) *
		SelectionSettings.BaseVoxelSize;
	SelectionSettings.WorldOrigin = RootWorldCenter - FVector(RootSize * 0.5f);

	TArray<FSVoxelOctreeNode> Leaves;
	if (!FSVoxelOctreeSelector::BuildBalancedLeaves(CameraLocation, SelectionSettings, Leaves))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SmoothVoxelOctreeWorld: octree selection failed (leaf cap may be too small); keeping current terrain."));
		return;
	}

	TArray<uint8> FaceMasks;
	if (!FSVoxelOctreeSelector::BuildTransitionFaceMasks(
		Leaves, SelectionSettings.CellsPerAxis, SelectionSettings.MaxLod, FaceMasks))
	{
		UE_LOG(LogTemp, Warning, TEXT("SmoothVoxelOctreeWorld: transition mask generation failed; keeping current terrain."));
		return;
	}

	TSet<FSVoxelOctreeNode> NewDesiredNodes;
	TMap<FSVoxelOctreeNode, uint8> NewDesiredMasks;
	NewDesiredNodes.Reserve(Leaves.Num());
	NewDesiredMasks.Reserve(Leaves.Num());

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
		const FVector ChunkCenter = ChunkOrigin + FVector(
			Node.GetWorldSize(SelectionSettings.CellsPerAxis, SelectionSettings.BaseVoxelSize) * 0.5f);
		const bool bCollision =
			bGenerateNearCollision &&
			Node.Lod <= FMath::Clamp(CollisionMaxLod, 0, 8) &&
			FVector::DistSquared(ChunkCenter, CameraLocation) <= CollisionRadiusSq;

		ASmoothVoxelTransvoxelChunk* Chunk = ActiveChunks.FindRef(Node);
		if (!IsValid(Chunk))
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Chunk = GetWorld()->SpawnActor<ASmoothVoxelTransvoxelChunk>(
				ChunkOrigin,
				FRotator::ZeroRotator,
				SpawnParams);
			if (!IsValid(Chunk))
			{
				UE_LOG(LogTemp, Warning, TEXT("SmoothVoxelOctreeWorld: failed to spawn a chunk actor."));
				return;
			}
			Chunk->SetTerrainMaterial(TerrainMaterial);
			ActiveChunks.Add(Node, Chunk);
		}
		else
		{
			Chunk->SetTerrainMaterial(TerrainMaterial);
		}

		if (bForceBuild || !Chunk->IsConfiguredFor(Node, FaceMask, VoxelSize, bCollision))
		{
			const FSVoxelGenerationSettings GeneratorSettings =
				MakeGenerationSettings(ChunkOrigin, VoxelSize);
			Chunk->QueueBuild(
				Node,
				SelectionSettings.CellsPerAxis,
				VoxelSize,
				FaceMask,
				GeneratorSettings,
				bCollision);
		}

		NewDesiredNodes.Add(Node);
		NewDesiredMasks.Add(Node, FaceMask);
	}

	DesiredNodes = MoveTemp(NewDesiredNodes);
	DesiredFaceMasks = MoveTemp(NewDesiredMasks);
	SelectionCameraLocation = CameraLocation;
	bApplyingSelection = true;
	bForceUpdate = false;
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

	const int32 Limit = FMath::Clamp(MaxConcurrentBuilds, 1, 16);
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
	if (DesiredNodes.Num() == 0)
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

		const float VoxelSize = FMath::Max(1.0f, BaseVoxelSize) * static_cast<float>(1 << Node.Lod);
		const bool bCollision = Chunk->GetRequestedCollision();
		if (!Chunk->IsReadyFor(Node, *FaceMask, VoxelSize, bCollision))
		{
			return false;
		}
	}

	return true;
}

void ASmoothVoxelOctreeWorld::RetireUndesiredChunks()
{
	for (auto It = ActiveChunks.CreateIterator(); It; ++It)
	{
		if (!DesiredNodes.Contains(It.Key()))
		{
			if (IsValid(It.Value()))
			{
				It.Value()->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}
