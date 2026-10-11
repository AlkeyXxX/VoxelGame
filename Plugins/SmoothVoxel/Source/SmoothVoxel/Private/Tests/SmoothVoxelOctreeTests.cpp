#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SmoothVoxelOctree.h"
#include "SmoothVoxelGenerator.h"

namespace
{
	bool DoNodesShareFace(const FSVoxelOctreeNode& A, const FSVoxelOctreeNode& B, int32 CellsPerAxis)
	{
		const int32 ASpan = A.GetSpanInBaseCells(CellsPerAxis);
		const int32 BSpan = B.GetSpanInBaseCells(CellsPerAxis);

		const int32 AMin[3] = { A.OriginCells.X, A.OriginCells.Y, A.OriginCells.Z };
		const int32 BMin[3] = { B.OriginCells.X, B.OriginCells.Y, B.OriginCells.Z };
		const int32 AMax[3] = { AMin[0] + ASpan, AMin[1] + ASpan, AMin[2] + ASpan };
		const int32 BMax[3] = { BMin[0] + BSpan, BMin[1] + BSpan, BMin[2] + BSpan };

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (AMax[Axis] != BMin[Axis] && BMax[Axis] != AMin[Axis])
			{
				continue;
			}

			const int32 U = (Axis + 1) % 3;
			const int32 V = (Axis + 2) % 3;
			const bool bOverlapU = FMath::Max(AMin[U], BMin[U]) < FMath::Min(AMax[U], BMax[U]);
			const bool bOverlapV = FMath::Max(AMin[V], BMin[V]) < FMath::Min(AMax[V], BMax[V]);

			if (bOverlapU && bOverlapV)
			{
				return true;
			}
		}

		return false;
	}

	uint8 RequiredCoarseFaceMask(
		const FSVoxelOctreeNode& A,
		const FSVoxelOctreeNode& B,
		int32 CellsPerAxis)
	{
		const int32 ASpan = A.GetSpanInBaseCells(CellsPerAxis);
		const int32 BSpan = B.GetSpanInBaseCells(CellsPerAxis);
		const int32 AMin[3] = { A.OriginCells.X, A.OriginCells.Y, A.OriginCells.Z };
		const int32 BMin[3] = { B.OriginCells.X, B.OriginCells.Y, B.OriginCells.Z };
		const int32 AMax[3] = { AMin[0] + ASpan, AMin[1] + ASpan, AMin[2] + ASpan };
		const int32 BMax[3] = { BMin[0] + BSpan, BMin[1] + BSpan, BMin[2] + BSpan };

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 U = (Axis + 1) % 3;
			const int32 V = (Axis + 2) % 3;
			const bool bOverlapU = FMath::Max(AMin[U], BMin[U]) < FMath::Min(AMax[U], BMax[U]);
			const bool bOverlapV = FMath::Max(AMin[V], BMin[V]) < FMath::Min(AMax[V], BMax[V]);
			if (!bOverlapU || !bOverlapV)
			{
				continue;
			}

			if (AMax[Axis] == BMin[Axis])
			{
				return Axis == 0 ? SVoxelTransitionFace_PositiveX
					: Axis == 1 ? SVoxelTransitionFace_PositiveY
					: SVoxelTransitionFace_PositiveZ;
			}
			if (AMin[Axis] == BMax[Axis])
			{
				return Axis == 0 ? SVoxelTransitionFace_NegativeX
					: Axis == 1 ? SVoxelTransitionFace_NegativeY
					: SVoxelTransitionFace_NegativeZ;
			}
		}
		return 0;
	}

	bool SameLeafSet(const TArray<FSVoxelOctreeNode>& A, const TArray<FSVoxelOctreeNode>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}

		TSet<FSVoxelOctreeNode> SetA;
		for (const FSVoxelOctreeNode& Node : A)
		{
			SetA.Add(Node);
		}

		for (const FSVoxelOctreeNode& Node : B)
		{
			if (!SetA.Contains(Node))
			{
				return false;
			}
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelOctreeBalanceTest,
	"SmoothVoxel.Octree.BalancedLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelOctreeBalanceTest::RunTest(const FString& Parameters)
{
	FSVoxelOctreeSelectionSettings Settings;
	Settings.CellsPerAxis = 8;
	Settings.BaseVoxelSize = 100.0f;
	Settings.MaxLod = 4;
	Settings.SplitDistanceMultiplier = 0.8f;
	Settings.MaxLeafCount = 4096;
	Settings.WorldOrigin = FVector(-6400.0f, -6400.0f, -6400.0f);

	TArray<FSVoxelOctreeNode> First;
	TArray<FSVoxelOctreeNode> Second;
	const FVector Camera(0.0f, 0.0f, 0.0f);

	TestTrue(TEXT("Octree selection should succeed"), FSVoxelOctreeSelector::BuildBalancedLeaves(Camera, Settings, First));
	TestTrue(TEXT("Repeated selection should succeed"), FSVoxelOctreeSelector::BuildBalancedLeaves(Camera, Settings, Second));
	TestTrue(TEXT("Selection must be deterministic"), SameLeafSet(First, Second));
	TestTrue(TEXT("Selection must stay under the leaf cap"), First.Num() <= Settings.MaxLeafCount);
	TestTrue(TEXT("Selection should not be empty"), First.Num() > 0);

	TArray<uint8> FaceMasks;
	TestTrue(TEXT("Transition masks should be generated for all leaves"),
		FSVoxelOctreeSelector::BuildTransitionFaceMasks(
			First,
			Settings.CellsPerAxis,
			Settings.MaxLod,
			FaceMasks));
	TestEqual(TEXT("One transition mask is returned per leaf"), FaceMasks.Num(), First.Num());

	bool bFoundAtLeastOneTransition = false;
	for (const uint8 FaceMask : FaceMasks)
	{
		bFoundAtLeastOneTransition |= FaceMask != 0;
		TestTrue(TEXT("Only the six known face bits are used"), (FaceMask & 0xC0u) == 0u);
	}
	TestTrue(TEXT("View-dependent hierarchy should expose fine/coarse interfaces"), bFoundAtLeastOneTransition);

	TSet<FSVoxelOctreeNode> UniqueLeaves;
	for (const FSVoxelOctreeNode& Node : First)
	{
		TestFalse(TEXT("Leaf entries must be unique"), UniqueLeaves.Contains(Node));
		UniqueLeaves.Add(Node);
	}

	for (int32 I = 0; I < First.Num(); ++I)
	{
		for (int32 J = I + 1; J < First.Num(); ++J)
		{
			if (DoNodesShareFace(First[I], First[J], Settings.CellsPerAxis))
			{
				TestTrue(
					TEXT("Face neighbours may differ by at most one LOD"),
					FMath::Abs(First[I].Lod - First[J].Lod) <= 1);

				if (First[I].Lod > First[J].Lod)
				{
					const uint8 RequiredMask = RequiredCoarseFaceMask(First[I], First[J], Settings.CellsPerAxis);
					TestTrue(TEXT("Each coarse/fine interface has a transition face bit"),
						(FaceMasks[I] & RequiredMask) != 0);
				}
				else if (First[J].Lod > First[I].Lod)
				{
					const uint8 RequiredMask = RequiredCoarseFaceMask(First[J], First[I], Settings.CellsPerAxis);
					TestTrue(TEXT("Each coarse/fine interface has a transition face bit"),
						(FaceMasks[J] & RequiredMask) != 0);
				}
			}
		}
	}

	FSVoxelOctreeSelectionSettings Invalid = Settings;
	Invalid.CellsPerAxis = 0;
	TArray<FSVoxelOctreeNode> InvalidResult;
	TestFalse(TEXT("Invalid settings should fail"), FSVoxelOctreeSelector::BuildBalancedLeaves(Camera, Invalid, InvalidResult));
	TestEqual(TEXT("Invalid selection returns no leaves"), InvalidResult.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelOctreeIndependentRootWindowTest,
	"SmoothVoxel.Octree.RootWindowCanExceedMaximumLeafLod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelOctreeIndependentRootWindowTest::RunTest(const FString& Parameters)
{
	FSVoxelOctreeSelectionSettings Settings;
	Settings.CellsPerAxis = 8;
	Settings.BaseVoxelSize = 100.0f;
	Settings.MaxLod = 2;
	Settings.RootLod = 4;
	Settings.SplitDistanceMultiplier = 0.75f;
	Settings.MaxLeafCount = 128;
	Settings.WorldOrigin = FVector::ZeroVector;
	Settings.RootOriginCells = FIntVector(-64, -64, -64);

	// Stay well outside the root so only the forced subdivision from RootLod
	// to MaxLod is exercised; no camera-proximity splits should occur.
	const FVector FarCamera(1000000.0f, 1000000.0f, 1000000.0f);
	TArray<FSVoxelOctreeNode> Leaves;
	TestTrue(TEXT("A larger root window should select successfully"),
		FSVoxelOctreeSelector::BuildBalancedLeaves(FarCamera, Settings, Leaves));

	const int32 ExpectedLeafCount = 1 << (3 * (Settings.RootLod - Settings.MaxLod));
	TestEqual(TEXT("Root is fully covered by MaxLod-sized leaves"),
		Leaves.Num(), ExpectedLeafCount);

	for (const FSVoxelOctreeNode& Node : Leaves)
	{
		TestTrue(TEXT("No rendered leaf may exceed MaxLod"), Node.Lod <= Settings.MaxLod);
	}

	TArray<uint8> FaceMasks;
	TestTrue(TEXT("Transition masks support a root larger than the leaf LOD"),
		FSVoxelOctreeSelector::BuildTransitionFaceMasks(
			Leaves, Settings.CellsPerAxis, Settings.MaxLod, FaceMasks,
			Settings.RootOriginCells, Settings.RootLod));
	TestEqual(TEXT("One transition mask is returned per selected leaf"),
		FaceMasks.Num(), Leaves.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelOctreeStreamingWindowOverlapTest,
	"SmoothVoxel.Octree.StreamingWindowKeepsWorldAlignedChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelOctreeStreamingWindowOverlapTest::RunTest(const FString& Parameters)
{
	FSVoxelOctreeSelectionSettings OldWindow;
	OldWindow.CellsPerAxis = 8;
	OldWindow.BaseVoxelSize = 100.0f;
	OldWindow.MaxLod = 4;
	OldWindow.SplitDistanceMultiplier = 0.8f;
	OldWindow.MaxLeafCount = 4096;
	OldWindow.WorldOrigin = FVector::ZeroVector;
	OldWindow.RootOriginCells = FIntVector(-64, -64, -64);

	FSVoxelOctreeSelectionSettings ShiftedWindow = OldWindow;
	// For an 8-cell, LOD-4 root this is a half-root shift on X. The overlap
	// [0,64] stays aligned at LOD 0..3 and should keep the same absolute keys.
	ShiftedWindow.RootOriginCells = FIntVector(0, -64, -64);

	TArray<FSVoxelOctreeNode> OldLeaves;
	TArray<FSVoxelOctreeNode> ShiftedLeaves;
	const FVector Camera(0.0f, 0.0f, 0.0f);
	TestTrue(TEXT("Initial streaming window should select leaves"),
		FSVoxelOctreeSelector::BuildBalancedLeaves(Camera, OldWindow, OldLeaves));
	TestTrue(TEXT("Shifted streaming window should select leaves"),
		FSVoxelOctreeSelector::BuildBalancedLeaves(Camera, ShiftedWindow, ShiftedLeaves));

	TSet<FSVoxelOctreeNode> ShiftedSet;
	for (const FSVoxelOctreeNode& Node : ShiftedLeaves)
	{
		ShiftedSet.Add(Node);
	}

	int32 SharedOverlapLeaves = 0;
	for (const FSVoxelOctreeNode& Node : OldLeaves)
	{
		const int32 Span = Node.GetSpanInBaseCells(OldWindow.CellsPerAxis);
		const bool bInsideOverlap =
			Node.OriginCells.X >= 0 && Node.OriginCells.X + Span <= 64 &&
			Node.OriginCells.Y >= -64 && Node.OriginCells.Y + Span <= 64 &&
			Node.OriginCells.Z >= -64 && Node.OriginCells.Z + Span <= 64;
		if (bInsideOverlap && ShiftedSet.Contains(Node))
		{
			++SharedOverlapLeaves;
		}
	}

	TestTrue(TEXT("At least one overlapping world-space leaf should retain its absolute node key"),
		SharedOverlapLeaves > 0);

	TArray<uint8> OldMasks;
	TArray<uint8> ShiftedMasks;
	TestTrue(TEXT("Initial window transition masks should succeed"),
		FSVoxelOctreeSelector::BuildTransitionFaceMasks(
			OldLeaves, OldWindow.CellsPerAxis, OldWindow.MaxLod, OldMasks, OldWindow.RootOriginCells));
	TestTrue(TEXT("Shifted window transition masks should succeed"),
		FSVoxelOctreeSelector::BuildTransitionFaceMasks(
			ShiftedLeaves, ShiftedWindow.CellsPerAxis, ShiftedWindow.MaxLod, ShiftedMasks, ShiftedWindow.RootOriginCells));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelNoiseBandwidthTest,
	"SmoothVoxel.Density.NoiseFitsMaximumLodBandwidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelNoiseBandwidthTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.NoiseScale = 0.0008f;  // Simulate an older Blueprint instance value.
	Settings.DetailScale = 0.0006f;

	const float CoarsestVoxelStep = 1600.0f; // 16 m with a 1 m base voxel (MaxLod 4).
	FSVoxelGenerator::ClampNoiseScalesForMaxSampleStep(Settings, CoarsestVoxelStep);

	TestTrue(
		TEXT("Highest macro-noise octave must stay below the coarse-grid aliasing limit"),
		Settings.NoiseScale * 4.0f * CoarsestVoxelStep <= 0.3501f);
	TestTrue(
		TEXT("Highest detail-noise octave must stay below the coarse-grid aliasing limit"),
		Settings.DetailScale * 4.0f * CoarsestVoxelStep <= 0.2626f);
	TestTrue(TEXT("Older high noise settings should be clamped"),
		Settings.NoiseScale < 0.0008f && Settings.DetailScale < 0.0006f);

	const float AtFineLod = FSVoxelGenerator::SampleDensity(1234.0f, -567.0f, 900.0f, Settings);
	FSVoxelGenerationSettings CoarseSettings = Settings;
	CoarseSettings.VoxelSize = CoarsestVoxelStep;
	const float AtCoarseLod = FSVoxelGenerator::SampleDensity(1234.0f, -567.0f, 900.0f, CoarseSettings);
	TestTrue(
		TEXT("All LODs must sample the same density field at a shared world coordinate"),
		FMath::IsNearlyEqual(AtFineLod, AtCoarseLod, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelDensityDeterminismTest,
	"SmoothVoxel.Density.DeterministicSampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelDensityDeterminismTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.Seed = 424242;
	Settings.bEnableCaves = false;

	const float A = FSVoxelGenerator::SampleDensity(1234.0f, -567.0f, 1000.0f, Settings);
	const float B = FSVoxelGenerator::SampleDensity(1234.0f, -567.0f, 1000.0f, Settings);

	TestTrue(TEXT("Density sample should be finite"), FMath::IsFinite(A));
	TestTrue(TEXT("Repeated density samples must be identical"), A == B);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelLargeCoordinateSamplingTest,
	"SmoothVoxel.Density.StableAtLargeChunkCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelLargeCoordinateSamplingTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings LeftSettings;
	LeftSettings.Seed = 1337;
	LeftSettings.BaseVoxelSize = 100.0f;
	LeftSettings.bUseChunkLatticeCoordinates = true;
	LeftSettings.ChunkOriginCells = FIntVector(10000000, -2500000, 0);
	LeftSettings.NoiseScale = 0.00005f;
	LeftSettings.DetailScale = 0.0000375f;
	LeftSettings.bEnableCaves = true;

	FSVoxelGenerationSettings RightSettings = LeftSettings;
	RightSettings.ChunkOriginCells.X += 32;

	// These are the same absolute point represented as the last edge of one
	// chunk and the first edge of its neighbour, at a very large world offset.
	const float LeftDensity = FSVoxelGenerator::SampleDensityAtChunkLocal(
		FVector(3200.0f, 1234.0f, 900.0f), LeftSettings);
	const float RightDensity = FSVoxelGenerator::SampleDensityAtChunkLocal(
		FVector(0.0f, 1234.0f, 900.0f), RightSettings);

	TestTrue(TEXT("Large-coordinate density must remain finite"), FMath::IsFinite(LeftDensity));
	TestTrue(TEXT("Neighbouring chunks must sample identical density at a shared absolute point"),
		FMath::IsNearlyEqual(LeftDensity, RightDensity, 0.0001f));

	const float RepeatedDensity = FSVoxelGenerator::SampleDensityAtChunkLocal(
		FVector(3200.0f, 1234.0f, 900.0f), LeftSettings);
	TestTrue(TEXT("Large-coordinate density sampling must be deterministic"),
		LeftDensity == RepeatedDensity);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelMiningCellCutTest,
	"SmoothVoxel.Density.MiningRemovesOneAlignedCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelMiningCellCutTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.BaseVoxelSize = 100.0f;
	Settings.VoxelSize = 100.0f;
	Settings.BaseHeight = 1000.0f;
	Settings.HeightAmplitude = 0.0f;
	Settings.DetailAmplitude = 0.0f;
	Settings.NoiseScale = 0.0f;
	Settings.DetailScale = 0.0f;
	Settings.bEnableCaves = false;

	const float SolidBeforeCut = FSVoxelGenerator::SampleDensity(50.0, 50.0, 50.0, Settings);
	TestTrue(TEXT("Unmined terrain is solid"), SolidBeforeCut > 0.0f);

	TSharedPtr<FSVoxelMinedNodeSet, ESPMode::ThreadSafe> MinedCells =
		MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>();
	MinedCells->Add(FIntVector(0, 0, 0));
	Settings.MinedNodes = MinedCells;

	const float InsideCut = FSVoxelGenerator::SampleDensity(50.0, 50.0, 50.0, Settings);
	const float AdjacentCell = FSVoxelGenerator::SampleDensity(150.0, 50.0, 50.0, Settings);
	TestTrue(TEXT("The selected voxel cell becomes air"), InsideCut < 0.0f);
	TestTrue(TEXT("The neighbouring cell remains solid"), AdjacentCell > 0.0f);

	FSVoxelGenerationSettings PlacementSettings = Settings;
	PlacementSettings.MinedNodes.Reset();
	const float TerrainBeforePlacement =
		FSVoxelGenerator::SampleDensity(150.0, 50.0, 50.0, PlacementSettings);

	TSharedPtr<FSVoxelBlockEditMap, ESPMode::ThreadSafe> BlockEdits =
		MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>();
	BlockEdits->Add(FIntVector(1, 0, 0), int8(2));
	PlacementSettings.BlockEdits = BlockEdits;
	const float DensityWithPlacedCube =
		FSVoxelGenerator::SampleDensity(150.0, 50.0, 50.0, PlacementSettings);
	TestTrue(
		TEXT("A placed hard cube does not carve the procedural density field"),
		FMath::IsNearlyEqual(DensityWithPlacedCube, TerrainBeforePlacement, KINDA_SMALL_NUMBER));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
