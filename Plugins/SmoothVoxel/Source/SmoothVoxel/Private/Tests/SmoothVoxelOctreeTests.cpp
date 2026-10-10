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
		TestEqual(TEXT("Only the six known face bits are used"), FaceMask & ~0x3Fu, 0);
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

#endif // WITH_DEV_AUTOMATION_TESTS
