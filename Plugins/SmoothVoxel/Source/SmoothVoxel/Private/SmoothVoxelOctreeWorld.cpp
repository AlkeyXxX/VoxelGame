#include "SmoothVoxelOctreeWorld.h"

#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "ProceduralMeshComponent.h"
#include "SmoothVoxelTransvoxelMesher.h"
#include "SmoothVoxelGenerator.h"
#include "TimerManager.h"
#include "../VoxelBlockLibrary.h"
#include "../VoxelInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "UObject/ConstructorHelpers.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"

namespace
{
	// Placed blocks must keep exact grid-aligned cube geometry. Fusing adjacent
	// dirt/sand blocks into the smooth terrain SDF produced unstable seams and
	// visible mesh artifacts as the second block was added.
	bool IsSmoothTerrainBlock(EVoxelBlock Block)
	{
		return false;
	}

	FIntVector LocalPositionToVoxelCell(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings)
	{
		const double BaseStep = FMath::Max(1.0, static_cast<double>(Settings.BaseVoxelSize));
		const double WorldX = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.X) * BaseStep + LocalPosition.X
			: static_cast<double>(Settings.ChunkWorldOrigin.X) + LocalPosition.X;
		const double WorldY = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Y) * BaseStep + LocalPosition.Y
			: static_cast<double>(Settings.ChunkWorldOrigin.Y) + LocalPosition.Y;
		const double WorldZ = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Z) * BaseStep + LocalPosition.Z
			: static_cast<double>(Settings.ChunkWorldOrigin.Z) + LocalPosition.Z;
		return FIntVector(
			FMath::FloorToInt(WorldX / BaseStep),
			FMath::FloorToInt(WorldY / BaseStep),
			FMath::FloorToInt(WorldZ / BaseStep));
	}

	int32 RepairEditedTriangleWinding(
		FSVoxelMeshData& Data,
		const FSVoxelGenerationSettings& Settings)
	{
		if (Data.Triangles.Num() < 3 || Data.Vertices.Num() == 0)
		{
			return 0;
		}

		const float BaseStep = FMath::Max(1.0f, Settings.BaseVoxelSize);
		const float LodStep = FMath::Max(BaseStep, Settings.VoxelSize);
		const float ProbeDistances[] =
		{
			FMath::Max(1.0f, BaseStep * 0.025f),
			FMath::Max(1.0f, BaseStep * 0.05f),
			FMath::Max(1.0f, BaseStep * 0.1f),
			FMath::Max(1.0f, BaseStep * 0.2f),
			FMath::Max(1.0f, BaseStep * 0.4f),
			FMath::Max(1.0f, BaseStep * 0.8f),
			FMath::Max(1.0f, LodStep * 0.25f),
			FMath::Max(1.0f, LodStep * 0.5f)
		};

		TSet<FIntVector> EditedNeighborhood;
		const auto AddEditedNeighborhood = [&EditedNeighborhood](const FIntVector& Cell)
		{
			for (int32 DZ = -2; DZ <= 2; ++DZ)
			{
				for (int32 DY = -2; DY <= 2; ++DY)
				{
					for (int32 DX = -2; DX <= 2; ++DX)
					{
						EditedNeighborhood.Add(Cell + FIntVector(DX, DY, DZ));
					}
				}
			}
		};
		if (Settings.MinedNodes.IsValid())
		{
			for (const FIntVector& Cell : *Settings.MinedNodes)
			{
				AddEditedNeighborhood(Cell);
			}
		}
		if (Settings.BlockEdits.IsValid())
		{
			for (const TPair<FIntVector, int8>& Edit : *Settings.BlockEdits)
			{
				if (Edit.Value != 0 && Edit.Value != 2)
				{
					AddEditedNeighborhood(Edit.Key);
				}
			}
		}
		if (EditedNeighborhood.Num() == 0)
		{
			return 0;
		}

		int32 CorrectedTriangles = 0;
		for (int32 Index = 0; Index + 2 < Data.Triangles.Num(); Index += 3)
		{
			const int32 A = Data.Triangles[Index];
			const int32 B = Data.Triangles[Index + 1];
			const int32 C = Data.Triangles[Index + 2];
			if (!Data.Vertices.IsValidIndex(A) ||
				!Data.Vertices.IsValidIndex(B) ||
				!Data.Vertices.IsValidIndex(C))
			{
				continue;
			}

			const FVector& PositionA = Data.Vertices[A];
			const FVector& PositionB = Data.Vertices[B];
			const FVector& PositionC = Data.Vertices[C];
			const FVector Cross = FVector::CrossProduct(
				PositionB - PositionA, PositionC - PositionA);
			if (Cross.SizeSquared() <= SMALL_NUMBER)
			{
				continue;
			}

			const FVector Center = (PositionA + PositionB + PositionC) / 3.0f;
			if (!EditedNeighborhood.Contains(LocalPositionToVoxelCell(Center, Settings)))
			{
				continue;
			}

			const FVector FaceDirection = Cross.GetSafeNormal();
			for (const float ProbeDistance : ProbeDistances)
			{
				const float AlongDensity = FSVoxelGenerator::SampleDensityAtChunkLocal(
					Center + FaceDirection * ProbeDistance, Settings);
				const float AgainstDensity = FSVoxelGenerator::SampleDensityAtChunkLocal(
					Center - FaceDirection * ProbeDistance, Settings);
				const bool bAlongIsSolid = AlongDensity > Settings.IsoLevel;
				const bool bAgainstIsSolid = AgainstDensity > Settings.IsoLevel;
				if (bAlongIsSolid == bAgainstIsSolid)
				{
					continue;
				}

				// This component's front-face convention expects the raw index
				// cross product to point into solid. Reverse faces that point into air.
				if (!bAlongIsSolid && bAgainstIsSolid)
				{
					Swap(Data.Triangles[Index + 1], Data.Triangles[Index + 2]);
					++CorrectedTriangles;
				}
				break;
			}
		}
		return CorrectedTriangles;
	}
}

ASmoothVoxelTransvoxelChunk::ASmoothVoxelTransvoxelChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransvoxelMesh"));
	RootComponent = Mesh;
	// Terrain chunks are rebuilt frequently after mining. Keep their
	// triangle collision synchronous with the committed render mesh; overlapping
	// async cooks can briefly leave invisible/stale collision triangles behind.
	Mesh->bUseAsyncCooking = false;
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

 	const int32 CorrectedWindingCount = RepairEditedTriangleWinding(Data, PendingSettings);
	if (CorrectedWindingCount > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SmoothVoxel: repaired winding on %d terrain triangles near voxel edits."),
			CorrectedWindingCount);
	}

	// Keep recently mined surfaces visible from both sides as a final fallback.
	// Build reverse-wound copies only near edits, then place them in a render-only
	// section so overlapping back faces never enter the player collision mesh.
	const int32 OriginalVertexCount = Data.Vertices.Num();
	const int32 OriginalTriangleIndexCount = Data.Triangles.Num();
	const int32 OriginalNormalCount = Data.Normals.Num();
	const int32 OriginalUVCount = Data.UV0.Num();
	const int32 OriginalTangentCount = Data.Tangents.Num();
	const int32 OriginalVertexColorCount = Data.VertexColors.Num();
	FSVoxelMeshData EditedBackfaceData;
	const int32 TwoSidedTriangleCount =
		FSVoxelTransvoxelMesher::AddTwoSidedEditedSurfaceTriangles(PendingSettings, Data);
	if (TwoSidedTriangleCount > 0)
	{
		EditedBackfaceData.Vertices.Reserve(Data.Vertices.Num() - OriginalVertexCount);
		for (int32 Index = OriginalVertexCount; Index < Data.Vertices.Num(); ++Index)
		{
			EditedBackfaceData.Vertices.Add(Data.Vertices[Index]);
		}

		EditedBackfaceData.Triangles.Reserve(Data.Triangles.Num() - OriginalTriangleIndexCount);
		for (int32 Index = OriginalTriangleIndexCount; Index < Data.Triangles.Num(); ++Index)
		{
			EditedBackfaceData.Triangles.Add(Data.Triangles[Index] - OriginalVertexCount);
		}

		for (int32 Index = OriginalNormalCount; Index < Data.Normals.Num(); ++Index)
		{
			EditedBackfaceData.Normals.Add(Data.Normals[Index]);
		}
		for (int32 Index = OriginalUVCount; Index < Data.UV0.Num(); ++Index)
		{
			EditedBackfaceData.UV0.Add(Data.UV0[Index]);
		}
		for (int32 Index = OriginalTangentCount; Index < Data.Tangents.Num(); ++Index)
		{
			EditedBackfaceData.Tangents.Add(Data.Tangents[Index]);
		}
		for (int32 Index = OriginalVertexColorCount; Index < Data.VertexColors.Num(); ++Index)
		{
			EditedBackfaceData.VertexColors.Add(Data.VertexColors[Index]);
		}

		// Strip the render-only copies back out of the main terrain data. Section
		// zero retains the original collision topology; section one is visual only.
		Data.Vertices.SetNum(OriginalVertexCount);
		Data.Triangles.SetNum(OriginalTriangleIndexCount);
		Data.Normals.SetNum(OriginalNormalCount);
		Data.UV0.SetNum(OriginalUVCount);
		Data.Tangents.SetNum(OriginalTangentCount);
		Data.VertexColors.SetNum(OriginalVertexColorCount);

		UE_LOG(LogTemp, Verbose,
			TEXT("SmoothVoxel: added render-only two-sided coverage for %d edited terrain triangles."),
			TwoSidedTriangleCount);
	}

	// Drop the previous render section and its collision body before applying
	// the new snapshot. Combined with synchronous cooking on terrain chunks,
	// this prevents a stale triangle from surviving a mined-cell rebuild.
	Mesh->ClearAllMeshSections();
	if (!Data.IsEmpty())
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
			Data.VertexColors,
			Data.Tangents,
			bCreateCollision);
	}

	if (!EditedBackfaceData.IsEmpty())
	{
		Mesh->CreateMeshSection_LinearColor(
			1,
			EditedBackfaceData.Vertices,
			EditedBackfaceData.Triangles,
			EditedBackfaceData.Normals,
			EditedBackfaceData.UV0,
			TArray<FVector2D>(),
			TArray<FVector2D>(),
			TArray<FVector2D>(),
			EditedBackfaceData.VertexColors,
			EditedBackfaceData.Tangents,
			false);
		Mesh->SetMaterial(1, Mesh->GetMaterial(0));
	}

	// Only fine terrain casts shadows; distant coarse chunks otherwise multiply
	// draw calls in every dynamic shadow pass without meaningful visual gain.
	Mesh->SetCastShadow(RequestedNode.Lod <= 1);
	Mesh->SetCollisionEnabled(
		bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	CompletedRevision = BuildRevision;
	bBuildFailed = false;
}

ASmoothVoxelWaterSurface::ASmoothVoxelWaterSurface()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterSurfaceMesh"));
	RootComponent = Mesh;
	Mesh->bUseAsyncCooking = false;
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	// Keep the same calm material that made the existing lakes look good.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterAsset(
		TEXT("/Game/StarterContent/Materials/M_Water_Lake.M_Water_Lake"));
	if (WaterAsset.Succeeded())
	{
		FallbackWaterMaterial = WaterAsset.Object;
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SmoothVoxel water: Starter Content M_Water_Lake was not found. Assign WaterMaterial on BP_OctreeWorld."));
	}
}

void ASmoothVoxelWaterSurface::ConfigureSurface(
	const FVector& WorldCenter,
	float WorldSize,
	float InSeaLevel,
	float InTileSize,
	int32 InResolution,
	UMaterialInterface* InMaterial,
	const FSVoxelGenerationSettings& InGenerationSettings,
	bool bEnableCollision)
{
	check(IsInGameThread());

	const float SafeWorldSize = FMath::Max(100.0f, WorldSize);
	const float SafeTileSize = FMath::Max(100.0f, InTileSize);
	const int32 RequestedResolution = FMath::Clamp(InResolution, 16, 512);
	const float RiverHalfWidthForGrid = FMath::Max(
		250.0f, FMath::Abs(InGenerationSettings.RiverHalfWidth));
	const int32 ResolutionForRiverWidth = InGenerationSettings.bEnableRivers
		? FMath::CeilToInt(SafeWorldSize / RiverHalfWidthForGrid)
		: 16;
	const int32 SafeResolution = FMath::Clamp(
		FMath::Max(RequestedResolution, ResolutionForRiverWidth), 16, 4096);
	UMaterialInterface* EffectiveMaterial = InMaterial
		? InMaterial
		: FallbackWaterMaterial;
	if (!EffectiveMaterial)
	{
		EffectiveMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	const FSVoxelGenerationSettings& Old = ConfiguredGenerationSettings;
	const bool bSameGeneratorSettings =
		Old.Seed == InGenerationSettings.Seed &&
		Old.bEnableBiomes == InGenerationSettings.bEnableBiomes &&
		FMath::IsNearlyEqual(Old.BiomeScale, InGenerationSettings.BiomeScale) &&
		Old.bEnablePOIPlateaus == InGenerationSettings.bEnablePOIPlateaus &&
		FMath::IsNearlyEqual(Old.POISiteSpacing, InGenerationSettings.POISiteSpacing) &&
		FMath::IsNearlyEqual(Old.POIPlateauRadius, InGenerationSettings.POIPlateauRadius) &&
		FMath::IsNearlyEqual(Old.POIPlateauBlendDistance, InGenerationSettings.POIPlateauBlendDistance) &&
		FMath::IsNearlyEqual(Old.FlatlandRegionScale, InGenerationSettings.FlatlandRegionScale) &&
		FMath::IsNearlyEqual(Old.FlatlandCoverage, InGenerationSettings.FlatlandCoverage) &&
		FMath::IsNearlyEqual(Old.FlatlandNoiseAmplitude, InGenerationSettings.FlatlandNoiseAmplitude) &&
		FMath::IsNearlyEqual(Old.HillScale, InGenerationSettings.HillScale) &&
		FMath::IsNearlyEqual(Old.HillHeight, InGenerationSettings.HillHeight) &&
		FMath::IsNearlyEqual(Old.DesertHillCoverage, InGenerationSettings.DesertHillCoverage) &&
		FMath::IsNearlyEqual(Old.MountainBiomeThreshold, InGenerationSettings.MountainBiomeThreshold) &&
		FMath::IsNearlyEqual(Old.MountainHeight, InGenerationSettings.MountainHeight) &&
		FMath::IsNearlyEqual(Old.BaseHeight, InGenerationSettings.BaseHeight) &&
		FMath::IsNearlyEqual(Old.HeightAmplitude, InGenerationSettings.HeightAmplitude) &&
		FMath::IsNearlyEqual(Old.NoiseScale, InGenerationSettings.NoiseScale) &&
		FMath::IsNearlyEqual(Old.DetailScale, InGenerationSettings.DetailScale) &&
		FMath::IsNearlyEqual(Old.DetailAmplitude, InGenerationSettings.DetailAmplitude) &&
		Old.bEnableRivers == InGenerationSettings.bEnableRivers &&
		FMath::IsNearlyEqual(Old.RiverScale, InGenerationSettings.RiverScale) &&
		FMath::IsNearlyEqual(Old.RiverHalfWidth, InGenerationSettings.RiverHalfWidth) &&
		FMath::IsNearlyEqual(Old.RiverDepth, InGenerationSettings.RiverDepth) &&
		FMath::IsNearlyEqual(Old.RiverWaterLevel, InGenerationSettings.RiverWaterLevel) &&
		Old.bEnableLakes == InGenerationSettings.bEnableLakes &&
		FMath::IsNearlyEqual(Old.LakeSiteSpacing, InGenerationSettings.LakeSiteSpacing) &&
		FMath::IsNearlyEqual(Old.LakeRadius, InGenerationSettings.LakeRadius) &&
		FMath::IsNearlyEqual(Old.LakeShoreBlendDistance, InGenerationSettings.LakeShoreBlendDistance) &&
		FMath::IsNearlyEqual(Old.LakeDepth, InGenerationSettings.LakeDepth) &&
		FMath::IsNearlyEqual(Old.LakeChance, InGenerationSettings.LakeChance) &&
		FMath::IsNearlyEqual(Old.HillLakeChance, InGenerationSettings.HillLakeChance);

	const bool bUnchanged = bHasConfiguration &&
		ConfiguredWorldCenter.Equals(WorldCenter, KINDA_SMALL_NUMBER) &&
		FMath::IsNearlyEqual(ConfiguredWorldSize, SafeWorldSize) &&
		FMath::IsNearlyEqual(ConfiguredSeaLevel, InSeaLevel) &&
		FMath::IsNearlyEqual(ConfiguredTileSize, SafeTileSize) &&
		ConfiguredResolution == SafeResolution &&
		ActiveMaterial == EffectiveMaterial &&
		bConfiguredCollision == bEnableCollision &&
		bSameGeneratorSettings;
	if (bUnchanged)
	{
		return;
	}

	ConfiguredWorldCenter = WorldCenter;
	ConfiguredWorldSize = SafeWorldSize;
	ConfiguredSeaLevel = InSeaLevel;
	ConfiguredTileSize = SafeTileSize;
	ConfiguredResolution = SafeResolution;
	ConfiguredGenerationSettings = InGenerationSettings;
	bConfiguredCollision = bEnableCollision;
	bHasConfiguration = true;
	ActiveMaterial = EffectiveMaterial;

	SetActorLocation(FVector(WorldCenter.X, WorldCenter.Y, InSeaLevel));
	BuildSurfaceMesh(SafeWorldSize, SafeTileSize, SafeResolution, InGenerationSettings, bEnableCollision);
}

void ASmoothVoxelWaterSurface::BuildSurfaceMesh(
	float WorldSize,
	float InTileSize,
	int32 InResolution,
	const FSVoxelGenerationSettings& GenerationSettings,
	bool bEnableCollision)
{
	if (!Mesh)
	{
		return;
	}

	const int32 Resolution = FMath::Clamp(InResolution, 16, 4096);
	const int32 Side = Resolution + 1;
	const float HalfExtent = FMath::Max(100.0f, WorldSize) * 0.5f;
	const float Step = (HalfExtent * 2.0f) / static_cast<float>(Resolution);
	const float SafeTileSize = FMath::Max(100.0f, InTileSize);
	const int32 VertexCount = Side * Side;

	// Lakes and oceans keep their simple, perfectly level quad.
	TArray<FVector> SeaVertices;
	TArray<int32> SeaTriangles;
	TArray<FVector> SeaNormals;
	TArray<FVector2D> SeaUVs;
	TArray<FLinearColor> SeaColors;
	TArray<FProcMeshTangent> SeaTangents;

	SeaVertices.Add(FVector(-HalfExtent, -HalfExtent, 0.0f));
	SeaVertices.Add(FVector( HalfExtent, -HalfExtent, 0.0f));
	SeaVertices.Add(FVector( HalfExtent,  HalfExtent, 0.0f));
	SeaVertices.Add(FVector(-HalfExtent,  HalfExtent, 0.0f));

	// Keep the working lake surface winding unchanged.
	SeaTriangles.Add(0);
	SeaTriangles.Add(2);
	SeaTriangles.Add(1);
	SeaTriangles.Add(0);
	SeaTriangles.Add(3);
	SeaTriangles.Add(2);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		SeaNormals.Add(FVector::UpVector);
		SeaColors.Add(FLinearColor::White);
		SeaTangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
	}
	SeaUVs.Add(FVector2D(0.0f, 0.0f));
	SeaUVs.Add(FVector2D(WorldSize / SafeTileSize, 0.0f));
	SeaUVs.Add(FVector2D(WorldSize / SafeTileSize, WorldSize / SafeTileSize));
	SeaUVs.Add(FVector2D(0.0f, WorldSize / SafeTileSize));

	// Rivers and lakes are clipped as separate flat-top polygons. The wetness
	// field combines the deterministic body mask with the actual terrain/water
	// intersection. No geometry is generated outside the shoreline, and every
	// visible vertex stays on one fixed elevation for its water body.
	struct FWaterClipVertex
	{
		FVector2D XY;
		float Wetness = -1.0f;
		float WaterLevel = 0.0f;
	};

	TArray<FVector> RiverVertices;
	TArray<int32> RiverTriangles;
	TArray<FVector> RiverNormals;
	TArray<FVector2D> RiverUVs;
	TArray<FLinearColor> RiverColors;
	TArray<FProcMeshTangent> RiverTangents;
	TArray<FVector> LakeVertices;
	TArray<int32> LakeTriangles;
	TArray<FVector> LakeNormals;
	TArray<FVector2D> LakeUVs;
	TArray<FLinearColor> LakeColors;
	TArray<FProcMeshTangent> LakeTangents;

	RiverVertices.Reserve(FMath::Min(Resolution * Resolution / 8, 1000000));
	RiverTriangles.Reserve(FMath::Min(Resolution * Resolution * 2, 4000000));
	LakeVertices.Reserve(FMath::Min(Resolution * Resolution / 32, 250000));
	LakeTriangles.Reserve(FMath::Min(Resolution * Resolution / 8, 1000000));

	TArray<float> RiverWetness;
	TArray<float> LakeWetness;
	TArray<float> LakeLevels;
	RiverWetness.SetNumUninitialized(VertexCount);
	LakeWetness.SetNumUninitialized(VertexCount);
	LakeLevels.SetNumUninitialized(VertexCount);

	const double RiverLevel = static_cast<double>(GenerationSettings.RiverWaterLevel);
	const double RiverDepthForMask = FMath::Max(
		100.0, std::abs(static_cast<double>(GenerationSettings.RiverDepth)));
	const double LakeDepthForMask = FMath::Max(
		100.0, std::abs(static_cast<double>(GenerationSettings.LakeDepth)));

	for (int32 Y = 0; Y <= Resolution; ++Y)
	{
		for (int32 X = 0; X <= Resolution; ++X)
		{
			const int32 Index = Y * Side + X;
			const double WorldX = static_cast<double>(ConfiguredWorldCenter.X) -
				static_cast<double>(HalfExtent) + static_cast<double>(X) * Step;
			const double WorldY = static_cast<double>(ConfiguredWorldCenter.Y) -
				static_cast<double>(HalfExtent) + static_cast<double>(Y) * Step;
			float RiverInfluence = 0.0f;
			float SignedField = 0.0f;
			if (GenerationSettings.bEnableRivers)
			{
				FSVoxelGenerator::SampleRiverMaskAndField(
					WorldX, WorldY, GenerationSettings, RiverInfluence, SignedField);
			}
			RiverInfluence = FMath::Clamp(RiverInfluence, 0.0f, 1.0f);

			float LakeInfluence = 0.0f;
			float LakeLevel = static_cast<float>(RiverLevel);
			if (GenerationSettings.bEnableLakes)
			{
				FSVoxelGenerator::SampleLakeMaskAndLevel(
					WorldX, WorldY, GenerationSettings, LakeInfluence, LakeLevel);
			}
			LakeInfluence = FMath::Clamp(LakeInfluence, 0.0f, 1.0f);
			LakeLevels[Index] = LakeLevel;

			// The water is dry whenever its body mask is zero, regardless of terrain height.
			// Sampling surface height only under candidate water avoids evaluating complex
			// terrain FBM across millions of dry grid points at adaptive river resolutions.
			double Ground = 0.0;
			if (RiverInfluence > 0.0001f || LakeInfluence > 0.0001f)
			{
				Ground = FSVoxelGenerator::SampleSurfaceHeight(
					WorldX, WorldY, GenerationSettings);
			}

			const double RiverHead = (RiverLevel - Ground) / RiverDepthForMask;
			RiverWetness[Index] = FMath::Min(
				RiverInfluence - 0.0001f,
				static_cast<float>(FMath::Clamp(RiverHead, -1.0, 1.0)));

			const double LakeHead = (static_cast<double>(LakeLevel) - Ground) / LakeDepthForMask;
			LakeWetness[Index] = FMath::Min(
				LakeInfluence - 0.0001f,
				static_cast<float>(FMath::Clamp(LakeHead, -1.0, 1.0)));
		}
	}

	const auto EmitClippedTriangle = [this, HalfExtent, SafeTileSize](
		const FWaterClipVertex& A,
		const FWaterClipVertex& B,
		const FWaterClipVertex& C,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		TArray<FLinearColor>& OutColors,
		TArray<FProcMeshTangent>& OutTangents)
	{
		const FWaterClipVertex Input[3] = { A, B, C };
		// Clipping one triangle against one scalar half-space yields at most
		// a convex quad, so fixed stack storage avoids per-triangle allocations.
		FWaterClipVertex Polygon[4];
		int32 PolygonCount = 0;
		FWaterClipVertex Previous = Input[2];
		bool bPreviousInside = Previous.Wetness > 0.0f;

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FWaterClipVertex Current = Input[Corner];
			const bool bCurrentInside = Current.Wetness > 0.0f;
			if (bCurrentInside != bPreviousInside)
			{
				const float Denominator = Previous.Wetness - Current.Wetness;
				const float T = FMath::Abs(Denominator) > SMALL_NUMBER
					? FMath::Clamp(Previous.Wetness / Denominator, 0.0f, 1.0f)
					: 0.5f;
				FWaterClipVertex Crossing;
				Crossing.XY = Previous.XY + (Current.XY - Previous.XY) * T;
				Crossing.WaterLevel = bPreviousInside
					? Previous.WaterLevel
					: Current.WaterLevel;
				Crossing.Wetness = 0.0f;
				Polygon[PolygonCount++] = Crossing;
			}
			if (bCurrentInside)
			{
				Polygon[PolygonCount++] = Current;
			}
			Previous = Current;
			bPreviousInside = bCurrentInside;
		}

		if (PolygonCount < 3)
		{
			return;
		}
		for (int32 FanIndex = 1; FanIndex + 1 < PolygonCount; ++FanIndex)
		{
			const FWaterClipVertex TriangleVertices[3] =
			{
				Polygon[0], Polygon[FanIndex], Polygon[FanIndex + 1]
			};
			const int32 FirstVertex = OutVertices.Num();
			for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
			{
				const FWaterClipVertex& Vertex = TriangleVertices[VertexIndex];
				OutVertices.Add(FVector(
					Vertex.XY.X,
					Vertex.XY.Y,
					Vertex.WaterLevel - ConfiguredSeaLevel));
				OutNormals.Add(FVector::UpVector);
				OutUVs.Add(FVector2D(
					(Vertex.XY.X + HalfExtent) / SafeTileSize,
					(Vertex.XY.Y + HalfExtent) / SafeTileSize));
				OutColors.Add(FLinearColor::White);
				OutTangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
			}
			OutTriangles.Add(FirstVertex);
			OutTriangles.Add(FirstVertex + 1);
			OutTriangles.Add(FirstVertex + 2);
		}
	};

	const auto MakeClipVertex = [HalfExtent, Step, Side](
		int32 X, int32 Y, const TArray<float>& Wetness, const TArray<float>& Levels)
	{
		const int32 Index = Y * Side + X;
		FWaterClipVertex V;
		V.XY = FVector2D(
			-HalfExtent + static_cast<float>(X) * Step,
			-HalfExtent + static_cast<float>(Y) * Step);
		V.Wetness = Wetness[Index];
		V.WaterLevel = Levels[Index];
		return V;
	};

	TArray<float> RiverLevels;
	RiverLevels.Init(static_cast<float>(RiverLevel), VertexCount);
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const FWaterClipVertex A_R = MakeClipVertex(X, Y, RiverWetness, RiverLevels);
			const FWaterClipVertex B_R = MakeClipVertex(X + 1, Y, RiverWetness, RiverLevels);
			const FWaterClipVertex C_R = MakeClipVertex(X + 1, Y + 1, RiverWetness, RiverLevels);
			const FWaterClipVertex D_R = MakeClipVertex(X, Y + 1, RiverWetness, RiverLevels);

			if (GenerationSettings.bEnableRivers)
			{
				EmitClippedTriangle(A_R, C_R, B_R,
					RiverVertices, RiverTriangles, RiverNormals, RiverUVs, RiverColors, RiverTangents);
				EmitClippedTriangle(A_R, D_R, C_R,
					RiverVertices, RiverTriangles, RiverNormals, RiverUVs, RiverColors, RiverTangents);
			}

			if (GenerationSettings.bEnableLakes)
			{
				const FWaterClipVertex A_L = MakeClipVertex(X, Y, LakeWetness, LakeLevels);
				const FWaterClipVertex B_L = MakeClipVertex(X + 1, Y, LakeWetness, LakeLevels);
				const FWaterClipVertex C_L = MakeClipVertex(X + 1, Y + 1, LakeWetness, LakeLevels);
				const FWaterClipVertex D_L = MakeClipVertex(X, Y + 1, LakeWetness, LakeLevels);
				EmitClippedTriangle(A_L, C_L, B_L,
					LakeVertices, LakeTriangles, LakeNormals, LakeUVs, LakeColors, LakeTangents);
				EmitClippedTriangle(A_L, D_L, C_L,
					LakeVertices, LakeTriangles, LakeNormals, LakeUVs, LakeColors, LakeTangents);
			}
		}
	}

	Mesh->ClearAllMeshSections();
	Mesh->CreateMeshSection_LinearColor(
		0,
		SeaVertices,
		SeaTriangles,
		SeaNormals,
		SeaUVs,
		TArray<FVector2D>(),
		TArray<FVector2D>(),
		TArray<FVector2D>(),
		SeaColors,
		SeaTangents,
		bEnableCollision);
	if (GenerationSettings.bEnableRivers && RiverTriangles.Num() > 0)
	{
		Mesh->CreateMeshSection_LinearColor(
			1, RiverVertices, RiverTriangles, RiverNormals, RiverUVs,
			TArray<FVector2D>(), TArray<FVector2D>(), TArray<FVector2D>(),
			RiverColors, RiverTangents, bEnableCollision);
	}
	if (GenerationSettings.bEnableLakes && LakeTriangles.Num() > 0)
	{
		Mesh->CreateMeshSection_LinearColor(
			2, LakeVertices, LakeTriangles, LakeNormals, LakeUVs,
			TArray<FVector2D>(), TArray<FVector2D>(), TArray<FVector2D>(),
			LakeColors, LakeTangents, bEnableCollision);
	}
	if (ActiveMaterial)
	{
		Mesh->SetMaterial(0, ActiveMaterial);
		if (GenerationSettings.bEnableRivers && RiverTriangles.Num() > 0)
		{
			Mesh->SetMaterial(1, ActiveMaterial);
		}
		if (GenerationSettings.bEnableLakes && LakeTriangles.Num() > 0)
		{
			Mesh->SetMaterial(2, ActiveMaterial);
		}
}
	Mesh->SetCollisionResponseToAllChannels(bEnableCollision ? ECR_Block : ECR_Ignore);
	Mesh->SetCollisionEnabled(
		bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
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
	// Keep the cube geometry identical on every hit. Damage feedback will be
	// shown by UI later; it must not change the collision/visual block size.
	const float Lo = 0.0f;
	const float Hi = Size;
	const FVector GridAlignedOrigin(
		static_cast<float>(InCell.X) * Size,
		static_cast<float>(InCell.Y) * Size,
		static_cast<float>(InCell.Z) * Size);
	// Keep the actor transform consistent with the logical voxel cell.
	SetActorLocation(GridAlignedOrigin);

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

	// The Octree world owns its block rules and does not read them from a legacy world actor.
	static ConstructorHelpers::FObjectFinder<UDataTable> BlockTableAsset(
		TEXT("/Game/Voxel/Data/DT_VoxelBlocks.DT_VoxelBlocks"));
	if (BlockTableAsset.Succeeded())
	{
		BlockDataTable = BlockTableAsset.Object;
	}
}

void ASmoothVoxelOctreeWorld::BeginPlay()
{
	Super::BeginPlay();
	TryBindPlayerInteractionInput();
	UpdateWaterSurface();
}

void ASmoothVoxelOctreeWorld::UpdateWaterSurface()
{
	check(IsInGameThread());
	if (!GetWorld())
	{
		return;
	}

	if (!bEnableWater)
	{
		if (IsValid(WaterSurfaceActor))
		{
			WaterSurfaceActor->SetActorHiddenInGame(true);
			WaterSurfaceActor->SetActorEnableCollision(false);
		}
		return;
	}

	if (!IsValid(WaterSurfaceActor))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		WaterSurfaceActor = GetWorld()->SpawnActor<ASmoothVoxelWaterSurface>(
			FVector(RootWorldCenter.X, RootWorldCenter.Y, SeaLevel),
			FRotator::ZeroRotator,
			SpawnParams);
		if (!IsValid(WaterSurfaceActor))
		{
			UE_LOG(LogTemp, Warning, TEXT("SmoothVoxelOctreeWorld: failed to spawn the water surface actor."));
			return;
		}
	}

	WaterSurfaceActor->SetActorHiddenInGame(false);
	WaterSurfaceActor->SetActorEnableCollision(bWaterCollision);
	const int32 WaterCellsPerAxis = FMath::Clamp(CellsPerAxis, 4, 128);
	const int32 WaterRootLod = GetRootLodForWorldSize();
	const float WaterSettingsStep = FMath::Max(
		1.0f,
		GetWorldSizeCentimeters() /
			(static_cast<float>(WaterCellsPerAxis) * static_cast<float>(1 << WaterRootLod)));
	const FSVoxelGenerationSettings WaterGenerationSettings = MakeGenerationSettings(
		FVector::ZeroVector,
		FIntVector::ZeroValue,
		WaterSettingsStep,
		WaterSettingsStep,
		false);
	WaterSurfaceActor->ConfigureSurface(
		RootWorldCenter,
		GetWorldSizeCentimeters(),
		SeaLevel,
		WaterTileSize,
		WaterGridResolution,
		WaterMaterial,
		WaterGenerationSettings,
		bWaterCollision);
}

void ASmoothVoxelOctreeWorld::TryBindPlayerInteractionInput()
{
	if (!bEnableBlockEditing || !bBindPlayerInteractionInput || !GetWorld())
	{
		if (InteractionInputController.IsValid())
		{
			DisableInput(InteractionInputController.Get());
		}
		InteractionInputController.Reset();
		return;
	}

	APlayerController* PlayerController =
		UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PlayerController))
	{
		// PlayerController may not exist yet during this actor's BeginPlay.
		// Tick retries the binding until the controller is available.
		return;
	}

	if (InteractionInputController.Get() == PlayerController && bInteractionActionsBound)
	{
		return;
	}

	if (InteractionInputController.IsValid())
	{
		DisableInput(InteractionInputController.Get());
	}

	EnableInput(PlayerController);
	if (!InputComponent)
	{
		return;
	}

	if (!bInteractionActionsBound)
	{
		InputComponent->BindAction(
			FName(TEXT("BreakBlock")), IE_Pressed,
			this, &ASmoothVoxelOctreeWorld::HandleBreakBlockInput);
		InputComponent->BindAction(
			FName(TEXT("PlaceBlock")), IE_Pressed,
			this, &ASmoothVoxelOctreeWorld::HandlePlaceBlockInput);
		bInteractionActionsBound = true;
	}

	InteractionInputController = PlayerController;
}

void ASmoothVoxelOctreeWorld::HandleBreakBlockInput()
{
	(void)BreakBlockByRay();
}

void ASmoothVoxelOctreeWorld::HandlePlaceBlockInput()
{
	(void)PlaceBlockByRay();
}

void ASmoothVoxelOctreeWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (InteractionInputController.IsValid())
	{
		DisableInput(InteractionInputController.Get());
	}
	InteractionInputController.Reset();

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

	if (IsValid(WaterSurfaceActor))
	{
		WaterSurfaceActor->Destroy();
	}
	WaterSurfaceActor = nullptr;

	Super::EndPlay(EndPlayReason);
}

void ASmoothVoxelOctreeWorld::RegenerateWorld()
{
	bForceUpdate = true;
}

void ASmoothVoxelOctreeWorld::StartNewGame()
{
	for (TPair<FIntVector, ASmoothVoxelPlacedCubeBlock*>& Pair : PlacedCubeBlocks)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	PlacedCubeBlocks.Reset();

	EditedBlockCells.Reset();
	EditedBlockCenters.Reset();
	EditedBlockTypes.Reset();
	PrePlacedBlockEditStates.Reset();
	PrePlacedBlockEditCenters.Reset();
	PrePlacedBlockEditTypes.Reset();
	MinedTerrainNodes.Reset();

	// Workers only receive immutable snapshots. Resetting these pointers ensures
	// all new builds use the untouched deterministic terrain for this seed.
	BlockEditSnapshot.Reset();
	BlockEditCenterSnapshot.Reset();
	MinedNodeSnapshot.Reset();

	bForceUpdate = true;
}

float ASmoothVoxelOctreeWorld::GetWorldSizeCentimeters() const
{
	return GetWorldSizeCentimetersForPreset(WorldSize);
}

ESVoxelBiome ASmoothVoxelOctreeWorld::GetBiomeAtWorldPosition(const FVector& WorldPosition) const
{
	const float Step = FMath::Max(1.0f, BaseVoxelSize);
	const FSVoxelGenerationSettings Settings =
		MakeGenerationSettings(FVector::ZeroVector, FIntVector::ZeroValue, Step, Step, false);
	return FSVoxelGenerator::SampleBiome(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		Settings);
}

float ASmoothVoxelOctreeWorld::GetPOISuitabilityAtWorldPosition(const FVector& WorldPosition) const
{
	const float Step = FMath::Max(1.0f, BaseVoxelSize);
	const FSVoxelGenerationSettings Settings =
		MakeGenerationSettings(FVector::ZeroVector, FIntVector::ZeroValue, Step, Step, false);
	return FSVoxelGenerator::SamplePOISuitability(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		Settings);
}

bool ASmoothVoxelOctreeWorld::GetOctreeNodeAtWorldPosition(
	const FVector& WorldPosition,
	FSVoxelOctreeNode& OutNode) const
{
	OutNode = FSVoxelOctreeNode();

	const float HalfSize = GetWorldSizeCentimeters() * 0.5f;
	const FVector WorldMin = RootWorldCenter - FVector(HalfSize);
	const FVector WorldMax = RootWorldCenter + FVector(HalfSize);
	if (WorldPosition.X < WorldMin.X || WorldPosition.Y < WorldMin.Y ||
		WorldPosition.Z < WorldMin.Z || WorldPosition.X >= WorldMax.X ||
		WorldPosition.Y >= WorldMax.Y || WorldPosition.Z >= WorldMax.Z)
	{
		return false;
	}

	const FIntVector Cell = WorldPositionToBlockCell(WorldPosition);
	for (const FSVoxelOctreeNode& Node : DesiredNodes)
	{
		if (!IsValid(ActiveChunks.FindRef(Node)))
		{
			continue;
		}

		const int32 Span = Node.GetSpanInBaseCells(CellsPerAxis);
		if (Cell.X >= Node.OriginCells.X && Cell.X < Node.OriginCells.X + Span &&
			Cell.Y >= Node.OriginCells.Y && Cell.Y < Node.OriginCells.Y + Span &&
			Cell.Z >= Node.OriginCells.Z && Cell.Z < Node.OriginCells.Z + Span)
		{
			OutNode = Node;
			return true;
		}
	}

	return false;
}

bool ASmoothVoxelOctreeWorld::GetBlockDebugInfoForCell(
	const FIntVector& Cell,
	EVoxelBlock& OutBlock,
	FIntVector& OutNodeOriginCells,
	int32& OutLod) const
{
	OutBlock = EVoxelBlock::Air;
	OutNodeOriginCells = FIntVector::ZeroValue;
	OutLod = INDEX_NONE;

	FSVoxelOctreeNode Node;
	if (!GetOctreeNodeAtWorldPosition(BlockCellCenter(Cell), Node))
	{
		return false;
	}

	const EVoxelBlock Block = GetBlockTypeForCell(Cell);
	if (Block == EVoxelBlock::Air)
	{
		return false;
	}

	OutBlock = Block;
	OutNodeOriginCells = Node.OriginCells;
	OutLod = Node.Lod;
	return true;
}

bool ASmoothVoxelOctreeWorld::GetBlockDebugInfoAtWorldPosition(
	const FVector& WorldPosition,
	EVoxelBlock& OutBlock,
	FIntVector& OutCell,
	FIntVector& OutNodeOriginCells,
	int32& OutLod) const
{
	OutBlock = EVoxelBlock::Air;
	OutCell = WorldPositionToBlockCell(WorldPosition);
	OutNodeOriginCells = FIntVector::ZeroValue;
	OutLod = INDEX_NONE;

	if (GetBlockDebugInfoForCell(OutCell, OutBlock, OutNodeOriginCells, OutLod))
	{
		return true;
	}

	// The impact point can be inside a solid cell even when that cell's centre
	// falls outside the density field on steep slopes. Use the validated sample
	// as a fallback so the debug overlay still reports the hit terrain block.
	FSVoxelOctreeNode Node;
	if (!GetOctreeNodeAtWorldPosition(WorldPosition, Node) ||
		SampleCurrentDensityAt(WorldPosition) <= 0.0f)
	{
		return false;
	}

	const FSVoxelGenerationSettings Settings = MakeBlockQuerySettings();
	const double SurfaceHeight = FSVoxelGenerator::SampleSurfaceHeight(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		Settings);
	const double Depth = SurfaceHeight - static_cast<double>(WorldPosition.Z);
	const float Step = GetEffectiveBlockSize();
	OutBlock = Depth <= Step * 1.25 ? EVoxelBlock::Grass :
		(Depth <= Step * 4.0 ? EVoxelBlock::Dirt : EVoxelBlock::Stone);
	OutNodeOriginCells = Node.OriginCells;
	OutLod = Node.Lod;
	return true;
}

void ASmoothVoxelOctreeWorld::GetTerrainDebugInfoAtWorldPosition(
	const FVector& WorldPosition,
	int32& OutSurfaceHeight,
	float& OutTerrainNoise,
	float& OutTemperature,
	float& OutMoisture,
	FString& OutBiomeName) const
{
	const float Step = GetEffectiveBlockSize();
	const FSVoxelGenerationSettings Settings =
		MakeGenerationSettings(FVector::ZeroVector, FIntVector::ZeroValue, Step, Step, false);

	const double SurfaceHeight = FSVoxelGenerator::SampleSurfaceHeight(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		Settings);
	OutSurfaceHeight = FMath::RoundToInt(SurfaceHeight / static_cast<double>(Step));
	OutTerrainNoise = FSVoxelGenerator::SampleTerrainNoise(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		Settings);

	float SignedTemperature = 0.0f;
	float SignedMoisture = 0.0f;
	float MountainSignal = 0.0f;
	FSVoxelGenerator::SampleBiomeClimate(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		Settings,
		SignedTemperature,
		SignedMoisture,
		MountainSignal);
	OutTemperature = FMath::Clamp(SignedTemperature * 0.5f + 0.5f, 0.0f, 1.0f);
	OutMoisture = FMath::Clamp(SignedMoisture * 0.5f + 0.5f, 0.0f, 1.0f);

	switch (FSVoxelGenerator::SampleBiome(
		static_cast<double>(WorldPosition.X),
		static_cast<double>(WorldPosition.Y),
		Settings))
	{
	case ESVoxelBiome::Forest:
		OutBiomeName = TEXT("Forest");
		break;
	case ESVoxelBiome::Desert:
		OutBiomeName = TEXT("Desert");
		break;
	case ESVoxelBiome::Mountain:
		OutBiomeName = TEXT("Mountain");
		break;
	case ESVoxelBiome::Snow:
		OutBiomeName = TEXT("Snow");
		break;
	case ESVoxelBiome::Plains:
	default:
		OutBiomeName = TEXT("Plains");
		break;
	}
}

float ASmoothVoxelOctreeWorld::GetWorldSizeCentimetersForPreset(
	ESmoothVoxelWorldSize InWorldSize)
{
	switch (InWorldSize)
	{
	case ESmoothVoxelWorldSize::Size2Km:
		return 200000.0f;
	case ESmoothVoxelWorldSize::Size8Km:
		return 800000.0f;
	case ESmoothVoxelWorldSize::Size4Km:
	default:
		return 400000.0f;
	}
}

int32 ASmoothVoxelOctreeWorld::GetRootLodForWorldSizePreset(
	ESmoothVoxelWorldSize InWorldSize)
{
	switch (InWorldSize)
	{
	case ESmoothVoxelWorldSize::Size2Km:
		return 6;
	case ESmoothVoxelWorldSize::Size8Km:
		return 8;
	case ESmoothVoxelWorldSize::Size4Km:
	default:
		return 7;
	}
}

int32 ASmoothVoxelOctreeWorld::GetEffectiveMaxLodForBudget(
	int32 RootLod,
	int32 RequestedMaxLod,
	int32 MaxLeafCount)
{
	const int32 SafeRootLod = FMath::Clamp(RootLod, 0, 10);
	const int32 SafeRequestedMaxLod = FMath::Clamp(RequestedMaxLod, 0, SafeRootLod);
	const int32 SafeLeafBudget = FMath::Max(1, MaxLeafCount);

	// Each mandatory octree level multiplies the minimum leaf count by eight.
	// Choose the coarsest allowed far-world LOD that fits this budget; near the
	// player, optional refinement still follows SplitDistanceMultiplier.
	int32 AffordableDepth = 0;
	int64 MinimumLeaves = 1;
	while (MinimumLeaves <= static_cast<int64>(SafeLeafBudget) / 8)
	{
		MinimumLeaves *= 8;
		++AffordableDepth;
	}

	const int32 BudgetLimitedMaxLod = FMath::Max(0, SafeRootLod - AffordableDepth);
	return FMath::Clamp(
		FMath::Max(SafeRequestedMaxLod, BudgetLimitedMaxLod),
		0,
		SafeRootLod);
}

FSVoxelGenerationSettings ASmoothVoxelOctreeWorld::MakeGenerationSettings(
	const FVector& ChunkOrigin,
	const FIntVector& ChunkOriginCells,
	float EffectiveBaseVoxelSize,
	float VoxelSize,
	bool bFilterBlockEdits) const
{
	FSVoxelGenerationSettings Settings;
	Settings.CellsPerAxis = CellsPerAxis;
	Settings.VoxelSize = VoxelSize;
	Settings.BaseVoxelSize = EffectiveBaseVoxelSize;
	Settings.ChunkOriginCells = ChunkOriginCells;
	Settings.bUseChunkLatticeCoordinates = true;
	Settings.NormalSampleStep = FMath::Max(5.0f, EffectiveBaseVoxelSize * 0.5f);
	Settings.IsoLevel = 0.0f;
	Settings.Seed = Seed;
	Settings.bEnableBiomes = bEnableBiomes;
	Settings.BiomeScale = BiomeScale;
	Settings.bEnablePOIPlateaus = bEnablePOIPlateaus;
	Settings.POISiteSpacing = POISiteSpacing;
	Settings.POIPlateauRadius = POIPlateauRadius;
	Settings.POIPlateauBlendDistance = POIPlateauBlendDistance;
	Settings.FlatlandRegionScale = FlatlandRegionScale;
	Settings.FlatlandCoverage = FlatlandCoverage;
	Settings.FlatlandNoiseAmplitude = FlatlandNoiseAmplitude;
	Settings.HillScale = HillScale;
	Settings.HillHeight = HillHeight;
	Settings.DesertHillCoverage = DesertHillCoverage;
	Settings.MountainBiomeThreshold = MountainBiomeThreshold;
	Settings.MountainHeight = MountainHeight;
	Settings.BaseHeight = BaseHeight;
	Settings.HeightAmplitude = HeightAmplitude;
	Settings.NoiseScale = NoiseScale;
	Settings.DetailScale = DetailScale;

	const int32 SafeMaxLod = FMath::Clamp(MaxLod, 0, 4);
	const float CoarsestVoxelStep =
		FMath::Max(1.0f, EffectiveBaseVoxelSize) * static_cast<float>(1 << SafeMaxLod);
	FSVoxelGenerator::ClampNoiseScalesForMaxSampleStep(Settings, CoarsestVoxelStep);
	Settings.DetailAmplitude = DetailAmplitude;
	Settings.bEnableRivers = bEnableRivers && bEnableWater;
	Settings.RiverScale = RiverScale;
	Settings.RiverHalfWidth = RiverHalfWidth;
	Settings.RiverDepth = RiverDepth;
	Settings.RiverWaterLevel = RiverWaterLevel;
	Settings.bEnableLakes = bEnableLakes && bEnableWater;
	Settings.LakeSiteSpacing = LakeSiteSpacing;
	Settings.LakeRadius = LakeRadius;
	Settings.LakeShoreBlendDistance = LakeShoreBlendDistance;
	Settings.LakeDepth = LakeDepth;
	Settings.LakeChance = LakeChance;
	Settings.HillLakeChance = HillLakeChance;
	Settings.BottomDepth = BottomDepth;
	Settings.bEnableCaves = bEnableCaves;
	Settings.CaveScale = CaveScale;
	Settings.CaveThreshold = CaveThreshold;
	Settings.CaveStrength = CaveStrength;
	Settings.ChunkWorldOrigin = ChunkOrigin;

	const bool bHasBlockEdits =
		BlockEditSnapshot.IsValid() && BlockEditSnapshot->Num() > 0;
	const bool bHasMinedNodes =
		MinedNodeSnapshot.IsValid() && MinedNodeSnapshot->Num() > 0;
	if (bFilterBlockEdits && (bHasBlockEdits || bHasMinedNodes))
	{
		const double EditGridStep = FMath::Max(1.0, static_cast<double>(EffectiveBaseVoxelSize));
		// Mined cells are centred half a base step inside their integer cells.
		// Include extra padding so neighbours/transition faces sample the same edits.
		const double Padding = static_cast<double>(VoxelSize) + EditGridStep * 1.5;
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
		if (bHasBlockEdits)
		{
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
		}

		FSVoxelMinedNodeSet LocalMinedNodes;
		if (bHasMinedNodes)
		{
			for (const FIntVector& Node : *MinedNodeSnapshot)
			{
				// MinedTerrainNodes now stores voxel-cell coordinates. Bounds
				// and SDF centres must use cell centres, not density-lattice corners.
				const double NodeX = (static_cast<double>(Node.X) + 0.5) * EditGridStep;
				const double NodeY = (static_cast<double>(Node.Y) + 0.5) * EditGridStep;
				const double NodeZ = (static_cast<double>(Node.Z) + 0.5) * EditGridStep;
				if (NodeX >= MinX && NodeX <= MaxX &&
					NodeY >= MinY && NodeY <= MaxY &&
					NodeZ >= MinZ && NodeZ <= MaxZ)
				{
					LocalMinedNodes.Add(Node);
				}
			}
		}

		if (LocalEdits.Num() > 0)
		{
			Settings.BlockEdits =
				MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>(LocalEdits);
		}
		if (LocalEditCenters.Num() > 0)
		{
			Settings.BlockEditCenters =
				MakeShared<FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe>(LocalEditCenters);
		}
		if (LocalMinedNodes.Num() > 0)
		{
			Settings.MinedNodes =
				MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>(LocalMinedNodes);
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
		MakeGenerationSettings(FVector::ZeroVector, FIntVector::ZeroValue, Step, Step, false);
	// Interaction probes need the complete immutable world snapshots.
	Settings.BlockEdits = BlockEditSnapshot;
	Settings.BlockEditCenters = BlockEditCenterSnapshot;
	Settings.MinedNodes = MinedNodeSnapshot;
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

	const float MaxDistance = FMath::Max(100.0f, InteractionDistance);
	const FVector Direction = RayDirection.GetSafeNormal();
	const float MarchStep = FMath::Clamp(GetEffectiveBlockSize() * 0.25f, 5.0f, 20.0f);
	const FSVoxelGenerationSettings DensitySettings = MakeBlockQuerySettings();
	const auto SampleRayDensity = [&DensitySettings](const FVector& Position)
	{
		return FSVoxelGenerator::SampleDensity(
			static_cast<double>(Position.X),
			static_cast<double>(Position.Y),
			static_cast<double>(Position.Z),
			DensitySettings);
	};

	FHitResult PhysicalHit;
	if (GetWorld()->LineTraceSingleByChannel(
		PhysicalHit,
		RayStart,
		RayStart + Direction * MaxDistance,
		ECC_Visibility,
		Params))
	{
		// Cubes have exact box geometry and do not depend on terrain recooking.
		if (Cast<ASmoothVoxelPlacedCubeBlock>(PhysicalHit.GetActor()))
		{
			OutHit = PhysicalHit;
			return true;
		}

		if (!Cast<ASmoothVoxelTransvoxelChunk>(PhysicalHit.GetActor()))
		{
			// Preserve ordinary interaction behaviour for non-terrain blockers.
			OutHit = PhysicalHit;
			return true;
		}

		// Do not trust terrain triangles as the authoritative mining target:
		// async collision can be stale, and transition-mesh seams can expose a
		// farther triangle through a gap. Continue below and select the first
		// crossing of the current immutable density snapshot instead.
	}

	// Physics missed or reported a stale face: ray-march the exact same density
	// snapshot as mesh generation. This also handles underside mining reliably.
	float PreviousDistance = 0.0f;
	float PreviousDensity = SampleRayDensity(RayStart);
	float HitDistance = -1.0f;
	FVector HitNormal = -Direction;

	for (float Distance = MarchStep; Distance <= MaxDistance; Distance += MarchStep)
	{
		const float CurrentDensity = SampleRayDensity(RayStart + Direction * Distance);
		const bool bPreviousSolid = PreviousDensity > 0.0f;
		const bool bCurrentSolid = CurrentDensity > 0.0f;
		if (bPreviousSolid != bCurrentSolid)
		{
			float Low = PreviousDistance;
			float High = Distance;
			const bool bLowSolid = bPreviousSolid;
			for (int32 Iteration = 0; Iteration < 8; ++Iteration)
			{
				const float Middle = (Low + High) * 0.5f;
				const float MiddleDensity = SampleRayDensity(RayStart + Direction * Middle);
				if ((MiddleDensity > 0.0f) == bLowSolid)
				{
					Low = Middle;
				}
				else
				{
					High = Middle;
				}
			}
			HitDistance = (Low + High) * 0.5f;
			HitNormal = bPreviousSolid ? Direction : -Direction;
			break;
		}

		PreviousDistance = Distance;
		PreviousDensity = CurrentDensity;
	}

	if (HitDistance < 0.0f)
	{
		return false;
	}

	const FVector HitPoint = RayStart + Direction * HitDistance;
	ASmoothVoxelTransvoxelChunk* HitChunk = nullptr;
	float BestChunkDistanceSquared = TNumericLimits<float>::Max();
	for (const TPair<FSVoxelOctreeNode, ASmoothVoxelTransvoxelChunk*>& Pair : ActiveChunks)
	{
		ASmoothVoxelTransvoxelChunk* Candidate = Pair.Value;
		if (!DesiredNodes.Contains(Pair.Key) || !IsValid(Candidate) ||
			!Candidate->Mesh || !Candidate->Mesh->IsVisible())
		{
			continue;
		}

		const FBox MeshBounds = Candidate->Mesh->Bounds.GetBox().ExpandBy(FVector(MarchStep * 2.0f));
		if (!MeshBounds.IsInsideOrOn(HitPoint))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), HitPoint);
		if (DistanceSquared < BestChunkDistanceSquared)
		{
			BestChunkDistanceSquared = DistanceSquared;
			HitChunk = Candidate;
		}
	}

	if (!IsValid(HitChunk))
	{
		return false;
	}

	// Fallback hit results are oriented toward the air from which the ray came.
	// BreakBlockByRay then validates the inside/outside side against density.
	OutHit = FHitResult(ForceInit);
	OutHit.bBlockingHit = true;
	OutHit.Distance = HitDistance;
	OutHit.Location = HitPoint;
	OutHit.ImpactPoint = HitPoint;
	OutHit.Normal = HitNormal;
	OutHit.ImpactNormal = HitNormal;
	OutHit.Actor = HitChunk;
	OutHit.Component = HitChunk->Mesh;
	return true;
}

bool ASmoothVoxelOctreeWorld::ApplyBlockCellEdit(
	const FIntVector& Cell,
	EVoxelBlock Block,
	int8 EditStateOverride,
	const FVector* EditCenterOverride)
{
	ASmoothVoxelPlacedCubeBlock* ExistingCube = PlacedCubeBlocks.FindRef(Cell);
	const bool bRemoveBlock = Block == EVoxelBlock::Air;
	const bool bRemovingPlacedCube = bRemoveBlock && IsValid(ExistingCube);
	const bool bSmoothBlock = !bRemoveBlock && IsSmoothTerrainBlock(Block);
	const int8 RestoreEditState = bRemovingPlacedCube
		? PrePlacedBlockEditStates.FindRef(Cell)
		: int8(0);
	int8 EditState = EditStateOverride;
	if (EditState == 0)
	{
		EditState = bRemoveBlock ? int8(-5) : (bSmoothBlock ? int8(1) : int8(2));
	}

	// The edit map also tracks explicit legacy air cuts. Hard cubes are
	// separately rendered and never alter the density field by themselves.

	if (bRemoveBlock)
	{
		// Explicit removals without a placed cube leave an air override. When
		// breaking a cube, the saved pre-placement edit is restored below instead.
		if (IsValid(ExistingCube))
		{
			ExistingCube->Destroy();
		}
		PlacedCubeBlocks.Remove(Cell);
		EditedBlockTypes.Remove(Cell);
		if (EditCenterOverride && !EditedBlockCenters.Contains(Cell) && !bRemovingPlacedCube)
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
		// Hard blocks reserve a logical cell and use a separate flat-shaded cube
		// mesh. State +2 is intentionally ignored by the terrain density sampler,
		// so placing a cube cannot excavate soil underneath it.
		ASmoothVoxelPlacedCubeBlock* Cube = ExistingCube;
		const bool bCreatedCube = !IsValid(Cube);
		if (bCreatedCube)
		{
			if (!GetWorld())
			{
				return false;
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
				return false;
			}
			PlacedCubeBlocks.Add(Cell, Cube);

			// Remember the density edit being temporarily replaced by this cube.
			// A cube may be placed into a cell which was already explicitly mined;
			// breaking it must restore that air override rather than refill terrain.
			if (!PrePlacedBlockEditStates.Contains(Cell))
			{
				const int8* PreviousState = EditedBlockCells.Find(Cell);
				PrePlacedBlockEditStates.Add(Cell, PreviousState ? *PreviousState : int8(0));
				if (const FVector* PreviousCenter = EditedBlockCenters.Find(Cell))
				{
					PrePlacedBlockEditCenters.Add(Cell, *PreviousCenter);
				}
				else
				{
					PrePlacedBlockEditCenters.Remove(Cell);
				}
				if (const EVoxelBlock* PreviousType = EditedBlockTypes.Find(Cell))
				{
					PrePlacedBlockEditTypes.Add(Cell, *PreviousType);
				}
				else
				{
					PrePlacedBlockEditTypes.Remove(Cell);
				}
			}
		}

		Cube->ConfigureBlock(
			Block, Cell, GetEffectiveBlockSize(), TerrainMaterial,
			Cube->GetDamageStage());
		EditedBlockCenters.Remove(Cell);
		EditedBlockTypes.Add(Cell, Block);
		EditState = EditStateOverride != 0 ? EditStateOverride : int8(2);
	}

	// Mined-cell cuts are persistent terrain edits, independent from hard-cube
	// occupancy. A cube can cover an already mined cell without restoring it.

	if (bRemovingPlacedCube)
	{
		// The hard cube never excavated the procedural field. Remove its
		// reservation and restore the exact previous cell state instead of
		// turning block removal into a free mining action.
		if (RestoreEditState == 0)
		{
			EditedBlockCells.Remove(Cell);
		}
		else
		{
			EditedBlockCells.Add(Cell, RestoreEditState);
		}
		if (const FVector* PreviousCenter = PrePlacedBlockEditCenters.Find(Cell))
		{
			EditedBlockCenters.Add(Cell, *PreviousCenter);
		}
		else
		{
			EditedBlockCenters.Remove(Cell);
		}
		if (const EVoxelBlock* PreviousType = PrePlacedBlockEditTypes.Find(Cell))
		{
			EditedBlockTypes.Add(Cell, *PreviousType);
		}
		PrePlacedBlockEditStates.Remove(Cell);
		PrePlacedBlockEditCenters.Remove(Cell);
		PrePlacedBlockEditTypes.Remove(Cell);
	}
	else
	{
		EditedBlockCells.Add(Cell, EditState);
	}
	BlockEditSnapshot = MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>(EditedBlockCells);
	BlockEditCenterSnapshot.Reset();
	if (EditedBlockCenters.Num() > 0)
	{
		BlockEditCenterSnapshot =
			MakeShared<FSVoxelBlockEditCenterMap, ESPMode::ThreadSafe>(EditedBlockCenters);
	}
	RebuildChunksNearEditedCell(Cell);
	return true;
}

void ASmoothVoxelOctreeWorld::RebuildChunksNearEditedCell(
	const FIntVector& Cell,
	bool bGridNode)
{
	check(IsInGameThread());
	if (ActiveChunks.Num() == 0 || DesiredNodes.Num() == 0)
	{
		return;
	}

	const float Step = GetEffectiveBlockSize();
	const FVector Center = bGridNode
		? FVector(
			static_cast<float>(Cell.X) * Step,
			static_cast<float>(Cell.Y) * Step,
			static_cast<float>(Cell.Z) * Step)
		: BlockCellCenter(Cell);
	const float EditRadius = Step * (bGridNode ? 1.1f : 0.55f);
	const FVector EditExtent(EditRadius);
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

void ASmoothVoxelOctreeWorld::SchedulePostMiningMeshRepair(const FIntVector& Cell)
{
	if (!GetWorld())
	{
		return;
	}

	// Mining changes a global density snapshot while nearby chunk meshes are
	// rebuilt asynchronously. Run one follow-up repair after the edits have
	// settled; each committed mesh rechecks triangle winding against the latest
	// density field and fixes faces that would otherwise be back-face culled.
	const TWeakObjectPtr<ASmoothVoxelOctreeWorld> WeakThis(this);
	FTimerHandle RepairTimer;
	GetWorld()->GetTimerManager().SetTimer(
		RepairTimer,
		FTimerDelegate::CreateLambda([WeakThis, Cell]()
		{
			if (!WeakThis.IsValid())
			{
				return;
			}
			ASmoothVoxelOctreeWorld* World = WeakThis.Get();
			if (World && World->GetWorld())
			{
				World->RebuildChunksNearEditedCell(Cell, false);
			}
			}),
		0.5f,
		false);
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
	// A Blueprint may call this method on the same input event that this actor
	// handles directly. Permit only one mining action per game frame.
	if (LastBreakInteractionFrame == GFrameCounter)
	{
		return false;
	}
	LastBreakInteractionFrame = GFrameCounter;

	FHitResult Hit;
	if (!TraceInteractionRay(Hit))
	{
		return false;
	}

	FIntVector Cell = FIntVector::ZeroValue;
	EVoxelBlock HitBlock = EVoxelBlock::Air;
	FVector InsidePoint = Hit.ImpactPoint;
	ASmoothVoxelPlacedCubeBlock* HitCube = Cast<ASmoothVoxelPlacedCubeBlock>(Hit.GetActor());
	if (IsValid(HitCube) && HitCube->GetOwner() == this)
	{
		// Cube collision already identifies the exact logical cell.
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
		// TraceInteractionRay returns the current density-field crossing, with
		// ImpactNormal pointing toward the air side. Probe inward using a small,
		// adaptive offset: a quarter-cell jump can skip the cell at a wall,
		// especially when mining horizontally from inside a tunnel.
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		const float ProbeScales[] = { 0.005f, 0.01f, 0.02f, 0.04f, 0.08f, 0.16f, 0.32f };
		bool bFoundSolidProbe = false;
		for (const float ProbeScale : ProbeScales)
		{
			const float ProbeDistance = FMath::Max(0.5f, Step * ProbeScale);
			const FVector CandidatePoint = Hit.ImpactPoint - Normal * ProbeDistance;
			if (SampleCurrentDensityAt(CandidatePoint) > 0.0f)
			{
				InsidePoint = CandidatePoint;
				bFoundSolidProbe = true;
				break;
			}
		}

		if (!bFoundSolidProbe)
		{
			// No solid sample behind this exact crossing means the density data
			// changed between trace and interaction. Do not mine an unrelated cell.
			return false;
		}
		Cell = WorldPositionToBlockCell(InsidePoint);
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

	// Durability and hit-progress are intentionally disabled for now: every
	// valid mining hit breaks exactly one block/cell. Inventory capacity is
	// still checked before applying the edit so drops are never lost.
	if (Inventory && DropBlock != EVoxelBlock::Air &&
		!Inventory->CanAddBlock(DropBlock, 1))
	{
		return false;
	}

	if (IsValid(HitCube))
	{
		if (!ApplyBlockCellEdit(Cell, EVoxelBlock::Air))
		{
			return false;
		}
		if (Inventory && DropBlock != EVoxelBlock::Air)
		{
			Inventory->AddBlock(DropBlock, 1);
		}
		SchedulePostMiningMeshRepair(Cell);
		return true;
	}

	// The point was validated on the solid side of the ray crossing above.
	// Mine that cell immediately rather than accumulating hidden progress.
	const FIntVector MinedCell = WorldPositionToBlockCell(InsidePoint);
	if (SampleCurrentDensityAt(InsidePoint) <= 0.0f)
	{
		return false;
	}

	MinedTerrainNodes.Add(MinedCell);
	MinedNodeSnapshot =
		MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>(MinedTerrainNodes);
	RebuildChunksNearEditedCell(MinedCell, false);
	SchedulePostMiningMeshRepair(MinedCell);

	if (Inventory && DropBlock != EVoxelBlock::Air)
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
	// Prevent duplicate placement if both the player Blueprint and the Octree
	// world's direct action binding receive the same click.
	if (LastPlaceInteractionFrame == GFrameCounter)
	{
		return false;
	}
	LastPlaceInteractionFrame = GFrameCounter;

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

	const float Step = GetEffectiveBlockSize();
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	const FVector AbsNormal(
		FMath::Abs(Normal.X), FMath::Abs(Normal.Y), FMath::Abs(Normal.Z));
	FIntVector FaceStep = FIntVector::ZeroValue;
	if (AbsNormal.X >= AbsNormal.Y && AbsNormal.X >= AbsNormal.Z)
	{
		FaceStep.X = Normal.X >= 0.0f ? 1 : -1;
	}
	else if (AbsNormal.Y >= AbsNormal.Z)
	{
		FaceStep.Y = Normal.Y >= 0.0f ? 1 : -1;
	}
	else
	{
		FaceStep.Z = Normal.Z >= 0.0f ? 1 : -1;
	}
	const FVector FaceDirection(
		static_cast<float>(FaceStep.X),
		static_cast<float>(FaceStep.Y),
		static_cast<float>(FaceStep.Z));
	FIntVector Cell = FIntVector::ZeroValue;
	FVector CubeWorldOrigin = FVector::ZeroVector;
	if (ASmoothVoxelPlacedCubeBlock* HitCube = Cast<ASmoothVoxelPlacedCubeBlock>(Hit.GetActor()))
	{
		if (HitCube->GetOwner() != this)
		{
			return false;
		}

		// Continue from the cube's logical cell, not its transform. This keeps
		// towers and walls aligned even when the ray hits an edge.
		Cell = HitCube->BlockCell + FaceStep;
	}
	else
	{
		ASmoothVoxelTransvoxelChunk* HitChunk = Cast<ASmoothVoxelTransvoxelChunk>(Hit.GetActor());
		if (!HitChunk || HitChunk->GetOwner() != this)
		{
			return false;
		}

		// Use the dominant face normal to select an adjacent grid cell. The
		// terrain stays smooth, while all placed cubes remain globally aligned.
		// Bias just inside the terrain before flooring so an exact grid-plane
		// hit chooses the cell on the solid side, then step outward one cell.
		const float GridBias = FMath::Max(0.05f, Step * 0.001f);
		Cell = WorldPositionToBlockCell(Hit.ImpactPoint - FaceDirection * GridBias) + FaceStep;

		// Marching-cubes walls are rounded at cell edges. There the smoothed
		// impact normal can point mostly upward even when the player is aiming
		// at the side of a mined pit, which would put the cube one cell too high.
		// If the normal-based cell is not part of the excavation, prefer the
		// nearest unoccupied mined cell next to the impact point. Mined cells are
		// the authoritative voxel grid for the cavity; the mesh surface is not.
		if (!MinedTerrainNodes.Contains(Cell))
		{
			const FIntVector HitCell = WorldPositionToBlockCell(Hit.ImpactPoint);
			float BestDistanceSquared = FMath::Square(Step * 1.5f);
			bool bFoundMinedCell = false;

			for (int32 DZ = -1; DZ <= 1; ++DZ)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						const FIntVector Candidate = HitCell + FIntVector(DX, DY, DZ);
						if (!MinedTerrainNodes.Contains(Candidate))
						{
							continue;
						}

						const int8* CandidateEdit = EditedBlockCells.Find(Candidate);
						if (CandidateEdit && *CandidateEdit > 0)
						{
							continue;
						}
						if (IsValid(PlacedCubeBlocks.FindRef(Candidate)))
						{
							continue;
						}

						const float CandidateDistanceSquared = FVector::DistSquared(
							BlockCellCenter(Candidate), Hit.ImpactPoint);
						if (CandidateDistanceSquared <= BestDistanceSquared)
						{
							BestDistanceSquared = CandidateDistanceSquared;
							Cell = Candidate;
							bFoundMinedCell = true;
						}
					}
				}
			}

			(void)bFoundMinedCell;
		}
	}

	CubeWorldOrigin = FVector(
		static_cast<float>(Cell.X) * Step,
		static_cast<float>(Cell.Y) * Step,
		static_cast<float>(Cell.Z) * Step);

	// Never allow a face-adjacent placement to extend beyond the selected
	// finite world window.
	const float WorldHalfSize = GetWorldSizeCentimeters() * 0.5f;
	const float WorldMinX = RootWorldCenter.X - WorldHalfSize;
	const float WorldMaxX = RootWorldCenter.X + WorldHalfSize;
	const float WorldMinY = RootWorldCenter.Y - WorldHalfSize;
	const float WorldMaxY = RootWorldCenter.Y + WorldHalfSize;
	const float BoundsTolerance = FMath::Max(0.05f, Step * 0.001f);
	if (CubeWorldOrigin.X < WorldMinX - BoundsTolerance ||
		CubeWorldOrigin.Y < WorldMinY - BoundsTolerance ||
		CubeWorldOrigin.X + Step > WorldMaxX + BoundsTolerance ||
		CubeWorldOrigin.Y + Step > WorldMaxY + BoundsTolerance)
	{
		return false;
	}

	// Reject placements whose target cube would occupy any part of the player
	// pawn's collision. Shrink the query box by a tiny epsilon so a block
	// touching the player's feet or side is still placeable face-to-face.
	if (IsValid(PlayerPawn) && GetWorld())
	{
		FCollisionObjectQueryParams PawnObjectQuery;
		PawnObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
		FCollisionQueryParams PawnOverlapParams(
			SCENE_QUERY_STAT(SmoothVoxelPlacementPlayerOverlap), false);
		PawnOverlapParams.AddIgnoredActor(this);

		TArray<FOverlapResult> PlayerOverlaps;
		const FVector TargetCenter = CubeWorldOrigin + FVector(Step * 0.5f);
		const float PlayerOverlapTolerance = FMath::Max(0.05f, Step * 0.001f);
		const float HalfExtent = FMath::Max(0.1f, Step * 0.5f - PlayerOverlapTolerance);
		const FCollisionShape TargetShape = FCollisionShape::MakeBox(FVector(HalfExtent));
		GetWorld()->OverlapMultiByObjectType(
			PlayerOverlaps,
			TargetCenter,
			FQuat::Identity,
			PawnObjectQuery,
			TargetShape,
			PawnOverlapParams);
		for (const FOverlapResult& Overlap : PlayerOverlaps)
		{
			if (Overlap.GetActor() == PlayerPawn ||
				(Overlap.Component.IsValid() && Overlap.Component->GetOwner() == PlayerPawn))
			{
				return false;
			}
		}
	}

	// Keep the overlap guard as a safety net for stale or inconsistent edit
	// state, while allowing exact face-to-face contact for stacking.
	const FVector TargetMin = CubeWorldOrigin;
	const FVector TargetMax = TargetMin + FVector(Step);
	const float OverlapTolerance = FMath::Max(0.05f, Step * 0.001f);
	for (const TPair<FIntVector, ASmoothVoxelPlacedCubeBlock*>& Pair : PlacedCubeBlocks)
	{
		ASmoothVoxelPlacedCubeBlock* ExistingCube = Pair.Value;
		if (!IsValid(ExistingCube))
		{
			continue;
		}
		const FVector ExistingMin = ExistingCube->GetActorLocation();
		const FVector ExistingMax = ExistingMin + FVector(Step);
		const float OverlapX = FMath::Min(TargetMax.X, ExistingMax.X) -
			FMath::Max(TargetMin.X, ExistingMin.X);
		const float OverlapY = FMath::Min(TargetMax.Y, ExistingMax.Y) -
			FMath::Max(TargetMin.Y, ExistingMin.Y);
		const float OverlapZ = FMath::Min(TargetMax.Z, ExistingMax.Z) -
			FMath::Max(TargetMin.Z, ExistingMin.Z);
		if (OverlapX > OverlapTolerance &&
			OverlapY > OverlapTolerance &&
			OverlapZ > OverlapTolerance)
		{
			return false;
		}
	}

	const int8* ExistingEdit = EditedBlockCells.Find(Cell);
	if (ExistingEdit && *ExistingEdit > 0)
	{
		return false;
	}
	if (ASmoothVoxelPlacedCubeBlock* ExistingCube = PlacedCubeBlocks.FindRef(Cell))
	{
		if (IsValid(ExistingCube))
		{
			return false;
		}
	}

	// Placement is validated by logical occupancy, not by excavating the
	// landscape to fit a cube. Terrain may overlap the cube around a curved
	// slope, but the scalar field stays unchanged, so placing/breaking blocks
	// cannot secretly mine or refill the ground.
	if (!ApplyBlockCellEdit(Cell, BlockToPlace))
	{
		return false;
	}
	if (Inventory)
	{
		Inventory->RemoveFromSelectedSlot(1);
	}
	return true;
}

void ASmoothVoxelOctreeWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Water is an independent world-owned surface; keep it in sync even when
	// terrain auto-generation is paused or the Octree selection is unchanged.
	UpdateWaterSurface();

	// Bind player input independently from mesh generation so startup ordering
	// cannot make the world permanently miss the first PlayerController.
	if (!InteractionInputController.IsValid() || !bInteractionActionsBound)
	{
		TryBindPlayerInteractionInput();
	}

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
	// Root LOD 6/7/8 maps to 2K/4K/8K respectively. Derive the sample
	// spacing from the selected dimensions so all three sizes have exact,
	// centred boundaries and keep the same world-space noise lattice.
	const float SelectedWorldSizeCm = GetWorldSizeCentimeters();
	const int32 RequestedMaxLod = FMath::Clamp(MaxLod, 0, 4);
	SelectionSettings.RootLod = FMath::Clamp(
		GetRootLodForWorldSize(), RequestedMaxLod, 8);
	// Keep the runtime safety budget conservative even when an existing level
	// instance still has the old 8192 default serialized in its properties.
	SelectionSettings.MaxLeafCount = FMath::Clamp(MaxLeafCount, 32, 2048);
	SelectionSettings.MaxLod = GetEffectiveMaxLodForBudget(
		SelectionSettings.RootLod, RequestedMaxLod, SelectionSettings.MaxLeafCount);

	const float RootCellCount = static_cast<float>(
		SelectionSettings.CellsPerAxis * (1 << SelectionSettings.RootLod));
	const float MaxCellSizeForWorldBound = SelectedWorldSizeCm / RootCellCount;
	SelectionSettings.BaseVoxelSize = FMath::Max(1.0f, MaxCellSizeForWorldBound);
	BaseVoxelSize = SelectionSettings.BaseVoxelSize;

	SelectionSettings.SplitDistanceMultiplier = FMath::Clamp(SplitDistanceMultiplier, 0.05f, 4.0f);

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
	// Biome shaping can amplify mountain relief and add dune/mountain lift.
	// Bound that full range here, otherwise high mountain chunks could be
	// culled above the old plains-only height envelope.
	const float MaxBiomeRelief =
		FMath::Abs(HillHeight) +
		FMath::Abs(MountainHeight) +
		FMath::Abs(FlatlandNoiseAmplitude) +
		FMath::Abs(DetailAmplitude) +
		FMath::Abs(LakeDepth);
	const float MinSurfaceHeight = BaseHeight - MaxBiomeRelief;
	const float MaxSurfaceHeight = BaseHeight + MaxBiomeRelief;
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

	UE_LOG(LogTemp, Log,
		TEXT("SmoothVoxelOctreeWorld: %0.1f km map, %d visible candidate chunks, root LOD %d, far LOD %d, budget %d."),
		SelectedWorldSizeCm / 100000.0f,
		Leaves.Num(),
		SelectionSettings.RootLod,
		SelectionSettings.MaxLod,
		SelectionSettings.MaxLeafCount);

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

		FSVoxelGenerationSettings BiomeSettings;
		BiomeSettings.Seed = Seed;
		BiomeSettings.bEnableBiomes = bEnableBiomes;
		BiomeSettings.BiomeScale = BiomeScale;
		const double ChunkCenterX = static_cast<double>(ChunkOrigin->X) +
			static_cast<double>(DesiredCellsPerAxis) * static_cast<double>(*VoxelSize) * 0.5;
		const double ChunkCenterY = static_cast<double>(ChunkOrigin->Y) +
			static_cast<double>(DesiredCellsPerAxis) * static_cast<double>(*VoxelSize) * 0.5;
		UMaterialInterface* SelectedBiomeMaterial = TerrainMaterial;
		switch (FSVoxelGenerator::SampleBiome(ChunkCenterX, ChunkCenterY, BiomeSettings))
		{
		case ESVoxelBiome::Forest:
			SelectedBiomeMaterial = ForestBiomeMaterial ? ForestBiomeMaterial : TerrainMaterial;
			break;
		case ESVoxelBiome::Desert:
			SelectedBiomeMaterial = DesertBiomeMaterial ? DesertBiomeMaterial : TerrainMaterial;
			break;
		case ESVoxelBiome::Mountain:
			SelectedBiomeMaterial = MountainBiomeMaterial ? MountainBiomeMaterial : TerrainMaterial;
			break;
		case ESVoxelBiome::Snow:
			SelectedBiomeMaterial = SnowBiomeMaterial ? SnowBiomeMaterial : TerrainMaterial;
			break;
		case ESVoxelBiome::Plains:
		default:
			SelectedBiomeMaterial = PlainsBiomeMaterial ? PlainsBiomeMaterial : TerrainMaterial;
			break;
		}
		Chunk->SetTerrainMaterial(SelectedBiomeMaterial);

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
