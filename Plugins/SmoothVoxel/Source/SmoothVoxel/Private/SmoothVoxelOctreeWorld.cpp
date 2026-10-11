#include "SmoothVoxelOctreeWorld.h"

#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "SmoothVoxelTransvoxelMesher.h"
#include "../VoxelBlockLibrary.h"
#include "../VoxelInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"

namespace
{
	// Only these materials are fused into the smooth terrain SDF. All other
	// placeable solid blocks use an independent hard-edged cube mesh.
	bool IsSmoothTerrainBlock(EVoxelBlock Block)
	{
		return Block == EVoxelBlock::Dirt || Block == EVoxelBlock::Sand;
	}
}

ASmoothVoxelTransvoxelChunk::ASmoothVoxelTransvoxelChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransvoxelMesh"));
	RootComponent = Mesh;
	Mesh->bUseAsyncCooking = true;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	const TWeakObjectPtr<ASmoothVoxelOctreeWorld> WeakWorld = Cast<ASmoothVoxelOctreeWorld>(GetOwner());
	const TSharedPtr<FSVoxelMeshData, ESPMode::ThreadSafe> Result =
		MakeShared<FSVoxelMeshData, ESPMode::ThreadSafe>();

	Async(EAsyncExecution::ThreadPool,
		[WeakThis, WeakWorld, WorkRevision, WorkCellsPerAxis, WorkVoxelSize, WorkFaceMask,
			WorkSettings, bWorkCollision, Result]() mutable
		{
			FSVoxelTransvoxelMesher::Generate(
				WorkSettings,
				WorkCellsPerAxis,
				WorkVoxelSize,
				WorkFaceMask,
				*Result);

			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, WeakWorld, WorkRevision, Result, bWorkCollision]() mutable
				{
					// Always release a worker slot even if the chunk became obsolete
					// and was destroyed while this task was running.
					if (WeakWorld.IsValid())
					{
						WeakWorld->NotifyWorkerBuildFinished();
					}
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
		if (ASmoothVoxelOctreeWorld* World = Cast<ASmoothVoxelOctreeWorld>(GetOwner()))
		{
			World->EnqueueChunkBuild(this);
		}
		return;
	}

	PendingCompletedRevision = BuildRevision;
	PendingCompletedData = Data;
	bPendingCompletedCollision = bCreateCollision;
	bHasPendingBuildApply = true;
	if (ASmoothVoxelOctreeWorld* World = Cast<ASmoothVoxelOctreeWorld>(GetOwner()))
	{
		World->EnqueueCompletedChunk(this);
	}
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
		if (ASmoothVoxelOctreeWorld* World = Cast<ASmoothVoxelOctreeWorld>(GetOwner()))
		{
			World->EnqueueChunkBuild(this);
		}
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

	// Only fine terrain casts shadows; distant coarse chunks otherwise multiply
	// draw calls in every dynamic shadow pass without meaningful visual gain.
	Mesh->SetCastShadow(RequestedNode.Lod <= 1);
	Mesh->SetCollisionEnabled(
		bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	CompletedRevision = BuildRevision;
	bBuildFailed = false;
}

ASmoothVoxelPlacedCubeBlock::ASmoothVoxelPlacedCubeBlock()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PlacedCubeMesh"));
	RootComponent = Mesh;
	Mesh->bUseAsyncCooking = true;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

int32 ASmoothVoxelPlacedCubeBlock::RegisterDamageHit()
{
	++DamageStage;
	return DamageStage;
}

void ASmoothVoxelPlacedCubeBlock::ConfigureBlock(
	EVoxelBlock InBlockType,
	const FIntVector& InCell,
	float InBlockSize,
	UMaterialInterface* InMaterial,
	int32 InDamageStage)
{
	BlockType = InBlockType;
	BlockCell = InCell;
	DamageStage = FMath::Max(0, InDamageStage);

	const float Size = FMath::Max(1.0f, InBlockSize);
	// Keep the block cubic but show accumulating damage through a small inset.
	// The hit-point stages are intentionally subtle, not a sudden full-cell cut.
	const float Inset = FMath::Min(Size * 0.045f, Size * 0.015f * DamageStage);
	const float Lo = Inset;
	const float Hi = Size - Inset;
	SetActorLocation(FVector(
		static_cast<float>(InCell.X) * Size,
		static_cast<float>(InCell.Y) * Size,
		static_cast<float>(InCell.Z) * Size));

	if (!Mesh)
	{
		return;
	}

	if (InMaterial)
	{
		Mesh->SetMaterial(0, InMaterial);
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Vertices.Reserve(24);
	Triangles.Reserve(36);
	Normals.Reserve(24);
	UV0.Reserve(24);
	Colors.Reserve(24);
	Tangents.Reserve(24);

	const auto AddFace = [&](
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& FaceNormal)
	{
		const int32 First = Vertices.Num();
		const FVector FaceTangent = (D - A).GetSafeNormal();
		Vertices.Add(A);
		Vertices.Add(B);
		Vertices.Add(C);
		Vertices.Add(D);
		Normals.Add(FaceNormal);
		Normals.Add(FaceNormal);
		Normals.Add(FaceNormal);
		Normals.Add(FaceNormal);
		UV0.Add(FVector2D(0.0f, 0.0f));
		UV0.Add(FVector2D(0.0f, 1.0f));
		UV0.Add(FVector2D(1.0f, 1.0f));
		UV0.Add(FVector2D(1.0f, 0.0f));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Colors.Add(FLinearColor::White);
			Tangents.Add(FProcMeshTangent(FaceTangent, false));
		}
		// ProceduralMeshComponent uses the opposite triangle winding from the
		// outward normal convention used by our terrain mesher.
		Triangles.Add(First);
		Triangles.Add(First + 2);
		Triangles.Add(First + 1);
		Triangles.Add(First);
		Triangles.Add(First + 3);
		Triangles.Add(First + 2);
	};

	// Every face owns its four vertices: this is deliberate. Reusing shared
	// corners would average normals and round the block under smooth shading.
	AddFace(
		FVector(Lo, Lo, Lo), FVector(Lo, Lo, Hi), FVector(Lo, Hi, Hi), FVector(Lo, Hi, Lo),
		FVector(-1, 0, 0));
	AddFace(
		FVector(Hi, Lo, Lo), FVector(Hi, Hi, Lo), FVector(Hi, Hi, Hi), FVector(Hi, Lo, Hi),
		FVector(1, 0, 0));
	AddFace(
		FVector(Lo, Lo, Lo), FVector(Hi, Lo, Lo), FVector(Hi, Lo, Hi), FVector(Lo, Lo, Hi),
		FVector(0, -1, 0));
	AddFace(
		FVector(Lo, Hi, Lo), FVector(Lo, Hi, Hi), FVector(Hi, Hi, Hi), FVector(Hi, Hi, Lo),
		FVector(0, 1, 0));
	AddFace(
		FVector(Lo, Lo, Lo), FVector(Lo, Hi, Lo), FVector(Hi, Hi, Lo), FVector(Hi, Lo, Lo),
		FVector(0, 0, -1));
	AddFace(
		FVector(Lo, Lo, Hi), FVector(Hi, Lo, Hi), FVector(Hi, Hi, Hi), FVector(Lo, Hi, Hi),
		FVector(0, 0, 1));

	Mesh->ClearAllMeshSections();
	Mesh->CreateMeshSection_LinearColor(
		0,
		Vertices,
		Triangles,
		Normals,
		UV0,
		TArray<FVector2D>(),
		TArray<FVector2D>(),
		TArray<FVector2D>(),
		Colors,
		Tangents,
		true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
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

	for (TPair<FIntVector, ASmoothVoxelPlacedCubeBlock*>& Pair : PlacedCubeBlocks)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	PlacedCubeBlocks.Reset();

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

	// Only carry edits near this chunk into the worker snapshot. Without this
	// spatial filtering, every unrelated chunk generated after a single edit
	// would pay for edit-map lookups at every density sample.
	if (BlockEditSnapshot.IsValid() && BlockEditSnapshot->Num() > 0)
	{
		const double EditGridStep = FMath::Max(1.0, static_cast<double>(EffectiveBaseVoxelSize));
		const double Padding = static_cast<double>(VoxelSize) + EditGridStep;
		const double MinX = static_cast<double>(ChunkOrigin.X) - Padding;
		const double MinY = static_cast<double>(ChunkOrigin.Y) - Padding;
		const double MinZ = static_cast<double>(ChunkOrigin.Z) - Padding;
		const double MaxX = static_cast<double>(ChunkOrigin.X) +
			static_cast<double>(CellsPerAxis) * VoxelSize + Padding;
		const double MaxY = static_cast<double>(ChunkOrigin.Y) +
			static_cast<double>(CellsPerAxis) * VoxelSize + Padding;
		const double MaxZ = static_cast<double>(ChunkOrigin.Z) +
			static_cast<double>(CellsPerAxis) * VoxelSize + Padding;

		FSVoxelBlockEditMap LocalEdits;
		FSVoxelBlockEditCenterMap LocalEditCenters;
		for (const TPair<FIntVector, int8>& Edit : *BlockEditSnapshot)
		{
			double EditX = (static_cast<double>(Edit.Key.X) + 0.5) * EditGridStep;
			double EditY = (static_cast<double>(Edit.Key.Y) + 0.5) * EditGridStep;
			double EditZ = (static_cast<double>(Edit.Key.Z) + 0.5) * EditGridStep;
			if (BlockEditCenterSnapshot.IsValid())
			{
				if (const FVector* EditCenter = BlockEditCenterSnapshot->Find(Edit.Key))
				{
					EditX = EditCenter->X;
					EditY = EditCenter->Y;
					EditZ = EditCenter->Z;
				}
			}
			if (EditX >= MinX && EditX <= MaxX &&
				EditY >= MinY && EditY <= MaxY &&
				EditZ >= MinZ && EditZ <= MaxZ)
			{
				LocalEdits.Add(Edit.Key, Edit.Value);
				if (BlockEditCenterSnapshot.IsValid())
				{
					if (const FVector* EditCenter = BlockEditCenterSnapshot->Find(Edit.Key))
					{
						LocalEditCenters.Add(Edit.Key, *EditCenter);
					}
				}
			}
		}
		if (LocalEdits.Num() > 0)
		{
			Settings.BlockEdits = MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>(LocalEdits);
			if (LocalEditCenters.Num() > 0)
			{
				Settings.BlockEditCenters =
					MakeShared<FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe>(LocalEditCenters);
			}
		}
	}
	return Settings;
}

float ASmoothVoxelOctreeWorld::GetEffectiveBlockSize() const
{
	return FMath::Max(1.0f, DesiredBaseVoxelSize > 0.0f ? DesiredBaseVoxelSize : BaseVoxelSize);
}

FIntVector ASmoothVoxelOctreeWorld::WorldPositionToBlockCell(const FVector& WorldPosition) const
{
	const float Step = GetEffectiveBlockSize();
	return FIntVector(
		FMath::FloorToInt(WorldPosition.X / Step),
		FMath::FloorToInt(WorldPosition.Y / Step),
		FMath::FloorToInt(WorldPosition.Z / Step));
}

FVector ASmoothVoxelOctreeWorld::BlockCellCenter(const FIntVector& Cell) const
{
	const float Step = GetEffectiveBlockSize();
	return FVector(
		(static_cast<float>(Cell.X) + 0.5f) * Step,
		(static_cast<float>(Cell.Y) + 0.5f) * Step,
		(static_cast<float>(Cell.Z) + 0.5f) * Step);
}

FSVoxelGenerationSettings ASmoothVoxelOctreeWorld::MakeBlockQuerySettings() const
{
	const float Step = GetEffectiveBlockSize();
	FSVoxelGenerationSettings Settings =
		MakeGenerationSettings(FVector::ZeroVector, FIntVector::ZeroValue, Step, Step);
	// Point queries for interaction can occur anywhere in the finite world, so
	// use the complete edit snapshot rather than a per-chunk filtered subset.
	Settings.BlockEdits = BlockEditSnapshot;
	Settings.BlockEditCenters = BlockEditCenterSnapshot;
	return Settings;
}

float ASmoothVoxelOctreeWorld::SampleCurrentDensityAt(const FVector& WorldPosition) const
{
	const FSVoxelGenerationSettings Settings = MakeBlockQuerySettings();
	return FSVoxelGenerator::SampleDensity(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		static_cast<double>(WorldPosition.Z),
		Settings);
}

EVoxelBlock ASmoothVoxelOctreeWorld::GetBlockTypeForCell(const FIntVector& Cell) const
{
	if (const int8* EditState = EditedBlockCells.Find(Cell))
	{
		if (*EditState < 0)
		{
			return EVoxelBlock::Air;
		}
		if (const EVoxelBlock* EditedType = EditedBlockTypes.Find(Cell))
		{
			return *EditedType;
		}
	}

	const FVector Center = BlockCellCenter(Cell);
	if (SampleCurrentDensityAt(Center) <= 0.0f)
	{
		return EVoxelBlock::Air;
	}

	const FSVoxelGenerationSettings Settings = MakeBlockQuerySettings();
	const double SurfaceHeight = FSVoxelGenerator::SampleSurfaceHeight(
		static_cast<double>(Center.X),
		static_cast<double>(Center.Y),
		Settings);
	const double Depth = SurfaceHeight - static_cast<double>(Center.Z);
	const float Step = GetEffectiveBlockSize();
	if (Depth <= Step * 1.25)
	{
		return EVoxelBlock::Grass;
	}
	if (Depth <= Step * 4.0)
	{
		return EVoxelBlock::Dirt;
	}
	return EVoxelBlock::Stone;
}

bool ASmoothVoxelOctreeWorld::TraceInteractionRay(FHitResult& OutHit) const
{
	if (!bEnableBlockEditing || !GetWorld())
	{
		return false;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return false;
	}

	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);
	if (ViewportSizeX <= 0 || ViewportSizeY <= 0)
	{
		return false;
	}

	FVector RayStart;
	FVector RayDirection;
	if (!PC->DeprojectScreenPositionToWorld(
		static_cast<float>(ViewportSizeX) * 0.5f,
		static_cast<float>(ViewportSizeY) * 0.5f,
		RayStart,
		RayDirection))
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SmoothVoxelBlockInteraction), true);
	Params.AddIgnoredActor(this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}

	return GetWorld()->LineTraceSingleByChannel(
		OutHit,
		RayStart,
		RayStart + RayDirection * FMath::Max(100.0f, InteractionDistance),
		ECC_Visibility,
		Params);
}

void ASmoothVoxelOctreeWorld::ApplyBlockCellEdit(
	const FIntVector& Cell,
	EVoxelBlock Block,
	int8 EditStateOverride,
	const FVector* EditCenterOverride)
{
	ASmoothVoxelPlacedCubeBlock* ExistingCube = PlacedCubeBlocks.FindRef(Cell);
	const bool bRemoveBlock = Block == EVoxelBlock::Air;
	const bool bSmoothBlock = !bRemoveBlock && IsSmoothTerrainBlock(Block);
	int8 EditState = EditStateOverride;
	if (EditState == 0)
	{
		EditState = bRemoveBlock ? int8(-5) : (bSmoothBlock ? int8(1) : int8(2));
	}

	// Removing a separately rendered hard cube restores the procedural terrain
	// field under it; only actual terrain mining keeps an explicit air override.
	if (bRemoveBlock && IsValid(ExistingCube) && EditStateOverride == 0)
	{
		ExistingCube->Destroy();
		PlacedCubeBlocks.Remove(Cell);
		EditedBlockTypes.Remove(Cell);
		EditedBlockCells.Remove(Cell);
		EditedBlockCenters.Remove(Cell);
		BlockEditSnapshot = EditedBlockCells.Num() > 0
			? MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>(EditedBlockCells)
			: nullptr;
		BlockEditCenterSnapshot = EditedBlockCenters.Num() > 0
			? MakeShared<FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe>(EditedBlockCenters)
			: nullptr;
		RebuildChunksNearEditedCell(Cell);
		return;
	}

	if (bRemoveBlock)
	{
		// Leave an explicit air override so procedural terrain cannot refill a
		// mined cell when adjacent chunks rebuild.
		if (IsValid(ExistingCube))
		{
			ExistingCube->Destroy();
		}
		PlacedCubeBlocks.Remove(Cell);
		EditedBlockTypes.Remove(Cell);
		if (EditCenterOverride && !EditedBlockCenters.Contains(Cell))
		{
			EditedBlockCenters.Add(Cell, *EditCenterOverride);
		}
		EditState = EditStateOverride != 0 ? EditStateOverride : int8(-5);
	}
	else if (bSmoothBlock)
	{
		// Soil/sand are blended into the scalar terrain field and never get a
		// cube actor. Rounded SDF union keeps their silhouette terrain-like.
		if (IsValid(ExistingCube))
		{
			ExistingCube->Destroy();
		}
		PlacedCubeBlocks.Remove(Cell);
		EditedBlockCenters.Remove(Cell);
		EditedBlockTypes.Add(Cell, Block);
		EditState = EditStateOverride != 0 ? EditStateOverride : int8(1);
	}
	else
	{
		// Hard blocks reserve an air-shaped cell in the terrain SDF; their actual
		// visible/collidable shape is a separate flat-shaded cube mesh.
		ASmoothVoxelPlacedCubeBlock* Cube = ExistingCube;
		if (!IsValid(Cube))
		{
			if (!GetWorld())
			{
				return;
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Cube = GetWorld()->SpawnActor<ASmoothVoxelPlacedCubeBlock>(
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				SpawnParams);
			if (!IsValid(Cube))
			{
				return;
			}
			PlacedCubeBlocks.Add(Cell, Cube);
		}

		Cube->ConfigureBlock(
			Block, Cell, GetEffectiveBlockSize(), TerrainMaterial, Cube->GetDamageStage());
		EditedBlockCenters.Remove(Cell);
		EditedBlockTypes.Add(Cell, Block);
		EditState = EditStateOverride != 0 ? EditStateOverride : int8(2);
	}

	EditedBlockCells.Add(Cell, EditState);
	BlockEditSnapshot = MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>(EditedBlockCells);
	BlockEditCenterSnapshot = EditedBlockCenters.Num() > 0
		? MakeShared<FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe>(EditedBlockCenters)
		: nullptr;
	RebuildChunksNearEditedCell(Cell);
}

void ASmoothVoxelOctreeWorld::RebuildChunksNearEditedCell(const FIntVector& Cell)
{
	check(IsInGameThread());
	if (ActiveChunks.Num() == 0 || DesiredNodes.Num() == 0)
	{
		return;
	}

	const float Step = GetEffectiveBlockSize();
	const FVector Center = BlockCellCenter(Cell);
	const FVector EditExtent(Step * 0.55f);
	const FBox EditBounds(Center - EditExtent, Center + EditExtent);

	for (const TPair<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*>& Pair : ActiveChunks)
	{
		if (!DesiredNodes.Contains(Pair.Key) || !IsValid(Pair.Value))
		{
			continue;
		}

		const FVector* ChunkOrigin = DesiredChunkOrigins.Find(Pair.Key);
		const float* VoxelSize = DesiredVoxelSizes.Find(Pair.Key);
		const uint8* FaceMask = DesiredFaceMasks.Find(Pair.Key);
		const bool* bCollision = DesiredCollisionFlags.Find(Pair.Key);
		if (!ChunkOrigin || !VoxelSize || !FaceMask || !bCollision)
		{
			continue;
		}

		// Rebuild chunks whose samples or transition borders can be influenced
		// by this cell. Expanding by one local LOD step covers coarse neighbours.
		const FVector Padding(*VoxelSize);
		const FVector ChunkMax = *ChunkOrigin + FVector(
			static_cast<float>(DesiredCellsPerAxis) * *VoxelSize);
		const FBox ChunkBounds(*ChunkOrigin - Padding, ChunkMax + Padding);
		if (!ChunkBounds.Intersect(EditBounds))
		{
			continue;
		}

		const FSVoxelGenerationSettings GeneratorSettings = MakeGenerationSettings(
			*ChunkOrigin, Pair.Key.OriginCells, DesiredBaseVoxelSize, *VoxelSize);
		Pair.Value->QueueBuild(
			Pair.Key, DesiredCellsPerAxis, *VoxelSize, *FaceMask, GeneratorSettings, *bCollision);
		EnqueueChunkBuild(Pair.Value);
	}
}

void ASmoothVoxelOctreeWorld::RemoveBlockAtWorld(const FVector& WorldPosition)
{
	ApplyBlockCellEdit(WorldPositionToBlockCell(WorldPosition), EVoxelBlock::Air);
}

void ASmoothVoxelOctreeWorld::SetBlockAtWorld(const FVector& WorldPosition, EVoxelBlock Block)
{
	ApplyBlockCellEdit(WorldPositionToBlockCell(WorldPosition), Block);
}

bool ASmoothVoxelOctreeWorld::BreakBlockByRay()
{
	FHitResult Hit;
	if (!TraceInteractionRay(Hit))
	{
		return false;
	}

	FIntVector Cell = FIntVector::ZeroValue;
	EVoxelBlock HitBlock = EVoxelBlock::Air;
	FVector MiningEditCenter = FVector::ZeroVector;
	ASmoothVoxelPlacedCubeBlock* HitCube = Cast<ASmoothVoxelPlacedCubeBlock>(Hit.GetActor());
	if (IsValid(HitCube) && HitCube->GetOwner() == this)
	{
		// Cube collision is exact, so use the cell stored on the actor rather
		// than trying to infer it again from a collision-face normal.
		Cell = HitCube->BlockCell;
		HitBlock = HitCube->BlockType;
	}
	else
	{
		ASmoothVoxelTransvoxelChunk* HitChunk = Cast<ASmoothVoxelTransvoxelChunk>(Hit.GetActor());
		if (!HitChunk || HitChunk->GetOwner() != this)
		{
			return false;
		}

		const float Step = GetEffectiveBlockSize();
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		const float ProbeDistance = FMath::Max(1.0f, Step * 0.2f);
		const FVector ProbeAlongNormal = Hit.ImpactPoint + Normal * ProbeDistance;
		const FVector ProbeAgainstNormal = Hit.ImpactPoint - Normal * ProbeDistance;
		const float DensityAlong = SampleCurrentDensityAt(ProbeAlongNormal);
		const float DensityAgainst = SampleCurrentDensityAt(ProbeAgainstNormal);

		// Collision normals can be flipped on the underside of a surface.
		// Pick the side that the density field confirms is solid instead of
		// trusting the triangle winding, which caused mining from below to miss
		// the interior and leave seemingly invisible faces.
		if (FMath::Max(DensityAlong, DensityAgainst) <= 0.0f)
		{
			return false;
		}
		const FVector InsidePoint = DensityAlong >= DensityAgainst
			? ProbeAlongNormal
			: ProbeAgainstNormal;
		MiningEditCenter = InsidePoint;
		Cell = WorldPositionToBlockCell(InsidePoint);

		const int8* ExistingEdit = EditedBlockCells.Find(Cell);
		if (ExistingEdit && *ExistingEdit <= -5)
		{
			return false;
		}

		HitBlock = GetBlockTypeForCell(Cell);
		if (HitBlock == EVoxelBlock::Air)
		{
			const FSVoxelGenerationSettings Settings = MakeBlockQuerySettings();
			const double SurfaceHeight = FSVoxelGenerator::SampleSurfaceHeight(
				static_cast<double>(InsidePoint.X),
				static_cast<double>(InsidePoint.Y),
				Settings);
			const double Depth = SurfaceHeight - static_cast<double>(InsidePoint.Z);
			HitBlock = Depth <= Step * 1.25 ? EVoxelBlock::Grass :
				(Depth <= Step * 4.0 ? EVoxelBlock::Dirt : EVoxelBlock::Stone);
		}
	}

	if (!UVoxelBlockLibrary::CanBreakBlockFromTable(BlockDataTable, HitBlock))
	{
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	UVoxelInventoryComponent* Inventory = PlayerPawn
		? PlayerPawn->FindComponentByClass<UVoxelInventoryComponent>()
		: nullptr;
	const EVoxelBlock DropBlock = UVoxelBlockLibrary::GetBlockDropFromTable(BlockDataTable, HitBlock);

	if (IsValid(HitCube))
	{
		const int32 HitsToBreak = FMath::Clamp(
			FMath::CeilToInt(UVoxelBlockLibrary::GetBlockDurabilityFromTable(BlockDataTable, HitBlock)) + 1,
			2,
			6);
		if (HitCube->GetDamageStage() + 1 < HitsToBreak)
		{
			const int32 NewDamageStage = HitCube->RegisterDamageHit();
			HitCube->ConfigureBlock(
				HitBlock, Cell, GetEffectiveBlockSize(), TerrainMaterial, NewDamageStage);
			return true;
		}
	}

	int8 NextMiningState = -1;
	if (!IsValid(HitCube))
	{
		if (const int8* ExistingEdit = EditedBlockCells.Find(Cell))
		{
			NextMiningState = *ExistingEdit < 0
				? static_cast<int8>(FMath::Max<int32>(-5, static_cast<int32>(*ExistingEdit) - 1))
				: int8(-1);
		}
	}

	const bool bFinishesMiningCell = IsValid(HitCube) || NextMiningState <= -5;
	if (Inventory && bFinishesMiningCell && DropBlock != EVoxelBlock::Air &&
		!Inventory->CanAddBlock(DropBlock, 1))
	{
		// Never remove a block if its drop cannot fit into the inventory.
		return false;
	}

	if (IsValid(HitCube))
	{
		// Destroying a placed cube removes its override and reveals whatever
		// procedural terrain originally occupied that cell.
		ApplyBlockCellEdit(Cell, EVoxelBlock::Air);
	}
	else
	{
		ApplyBlockCellEdit(Cell, EVoxelBlock::Air, NextMiningState, &MiningEditCenter);
	}

	if (Inventory && bFinishesMiningCell && DropBlock != EVoxelBlock::Air)
	{
		Inventory->AddBlock(DropBlock, 1);
	}
	return true;
}

bool ASmoothVoxelOctreeWorld::PlaceBlockByRay()
{
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (UVoxelInventoryComponent* Inventory = PlayerPawn->FindComponentByClass<UVoxelInventoryComponent>())
		{
			return PlaceBlockByRayWithType(Inventory->GetSelectedBlock());
		}
	}
	return PlaceBlockByRayWithType(EVoxelBlock::Dirt);
}

bool ASmoothVoxelOctreeWorld::PlaceBlockByRayWithType(EVoxelBlock BlockToPlace)
{
	if (!UVoxelBlockLibrary::CanPlaceBlockFromTable(BlockDataTable, BlockToPlace))
	{
		return false;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	UVoxelInventoryComponent* Inventory = PlayerPawn
		? PlayerPawn->FindComponentByClass<UVoxelInventoryComponent>()
		: nullptr;
	if (Inventory &&
		(Inventory->GetSelectedBlock() != BlockToPlace || Inventory->GetSelectedQuantity() <= 0))
	{
		return false;
	}

	FHitResult Hit;
	if (!TraceInteractionRay(Hit))
	{
		return false;
	}
	ASmoothVoxelTransvoxelChunk* HitChunk = Cast<ASmoothVoxelTransvoxelChunk>(Hit.GetActor());
	if (!HitChunk || HitChunk->GetOwner() != this)
	{
		return false;
	}

	const float Step = GetEffectiveBlockSize();
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	const FIntVector Cell = WorldPositionToBlockCell(Hit.ImpactPoint + Normal * (Step * 0.51f));
	const int8* ExistingEdit = EditedBlockCells.Find(Cell);
	if (ExistingEdit && *ExistingEdit > 0)
	{
		return false;
	}
	if ((!ExistingEdit || *ExistingEdit == 0) && SampleCurrentDensityAt(BlockCellCenter(Cell)) > 0.0f)
	{
		return false;
	}

	ApplyBlockCellEdit(Cell, BlockToPlace);
	if (Inventory)
	{
		Inventory->RemoveFromSelectedSlot(1);
	}
	return true;
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
			EnqueueChunkBuild(Chunk);
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
	// This queue contains only completed CPU meshes, avoiding an O(active chunks)
	// scan on every rendered frame when the terrain is idle.
	CompletedChunkBuilds.RemoveAll([](const TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>& WeakChunk)
	{
		return !WeakChunk.IsValid() || !WeakChunk->HasPendingBuildApply();
	});

	int32 BestIndex = INDEX_NONE;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < CompletedChunkBuilds.Num(); ++Index)
	{
		ASmoothVoxelTransvoxelChunk* Chunk = CompletedChunkBuilds[Index].Get();
		if (!IsValid(Chunk))
		{
			continue;
		}
		const float DistanceSquared =
			FVector::DistSquared(Chunk->GetActorLocation(), CameraLocation);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = Index;
		}
	}

	if (BestIndex != INDEX_NONE)
	{
		ASmoothVoxelTransvoxelChunk* Chunk = CompletedChunkBuilds[BestIndex].Get();
		CompletedChunkBuilds.RemoveAtSwap(BestIndex, 1, false);
		if (IsValid(Chunk))
		{
			Chunk->ApplyPendingBuild();
		}
	}
}

void ASmoothVoxelOctreeWorld::NotifyWorkerBuildFinished()
{
	check(IsInGameThread());
	InFlightMeshBuildCount = FMath::Max(0, InFlightMeshBuildCount - 1);
}

void ASmoothVoxelOctreeWorld::EnqueueChunkBuild(ASmoothVoxelTransvoxelChunk* Chunk)
{
	if (IsValid(Chunk))
	{
		QueuedChunkBuilds.AddUnique(TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>(Chunk));
	}
}

void ASmoothVoxelOctreeWorld::EnqueueCompletedChunk(ASmoothVoxelTransvoxelChunk* Chunk)
{
	if (IsValid(Chunk))
	{
		CompletedChunkBuilds.AddUnique(TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>(Chunk));
	}
}

void ASmoothVoxelOctreeWorld::LaunchQueuedBuilds(const FVector& CameraLocation)
{
	const int32 Limit = FMath::Clamp(MaxConcurrentBuilds, 1, 2);
	if (InFlightMeshBuildCount >= Limit || QueuedChunkBuilds.Num() == 0)
	{
		return;
	}

	QueuedChunkBuilds.RemoveAll([this](const TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>& WeakChunk)
	{
		ASmoothVoxelTransvoxelChunk* Chunk = WeakChunk.Get();
		return !IsValid(Chunk) ||
			!DesiredNodes.Contains(Chunk->GetNode()) ||
			!Chunk->IsBuildQueued() ||
			Chunk->IsBuildInProgress();
	});

	if (QueuedChunkBuilds.Num() == 0)
	{
		return;
	}

	// Sort far-to-near so Pop() launches the nearest requests first. Sorting is
	// only performed when a worker slot is available, not every frame.
	QueuedChunkBuilds.Sort([CameraLocation](
		const TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>& A,
		const TWeakObjectPtr<ASmoothVoxelTransvoxelChunk>& B)
	{
		return FVector::DistSquared(A->GetActorLocation(), CameraLocation) >
			FVector::DistSquared(B->GetActorLocation(), CameraLocation);
	});

	while (InFlightMeshBuildCount < Limit && QueuedChunkBuilds.Num() > 0)
	{
		TWeakObjectPtr<ASmoothVoxelTransvoxelChunk> WeakChunk = QueuedChunkBuilds.Pop(false);
		ASmoothVoxelTransvoxelChunk* Chunk = WeakChunk.Get();
		if (IsValid(Chunk) && DesiredNodes.Contains(Chunk->GetNode()) &&
			Chunk->IsBuildQueued() && !Chunk->IsBuildInProgress())
		{
			Chunk->StartQueuedBuild();
			++InFlightMeshBuildCount;
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
	// After a stable selection every desired node has one actor. Avoid walking
	// the full map each frame when there are no obsolete chunks to retire.
	if (ActiveChunks.Num() <= DesiredNodes.Num())
	{
		return;
	}

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
