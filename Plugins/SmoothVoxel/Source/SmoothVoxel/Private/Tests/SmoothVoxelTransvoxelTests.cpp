#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelTransvoxelMesher.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelCachedSurfaceHeightDensityTest,
	"SmoothVoxel.Transvoxel.CachedSurfaceHeightMatchesDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelCachedSurfaceHeightDensityTest::RunTest(const FString& Parameters)
{
	for (const bool bEnableCaves : { false, true })
	{
		FSVoxelGenerationSettings Settings;
		Settings.BaseHeight = 900.0f;
		Settings.HeightAmplitude = 6000.0f;
		Settings.DetailAmplitude = 60.0f;
		Settings.ChunkOriginCells = FIntVector(173, -91, 4);
		Settings.BaseVoxelSize = 97.65625f;
		Settings.bUseChunkLatticeCoordinates = true;
		Settings.bEnableCaves = bEnableCaves;
		Settings.CaveStrength = 1.0f;
		Settings.BottomDepth = 2000.0f;

		const FVector Samples[] =
		{
			FVector(0.0f, 0.0f, 500.0f),
			FVector(217.25f, 813.5f, 900.0f),
			FVector(1500.0f, 721.0f, 4500.0f),
			FVector(2810.0f, 1990.0f, -600.0f)
		};

		for (const FVector& LocalPosition : Samples)
		{
			const double SurfaceHeight =
				FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(LocalPosition, Settings);
			const float Reference =
				FSVoxelGenerator::SampleDensityAtChunkLocal(LocalPosition, Settings);
			const float Cached =
				FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
					LocalPosition, SurfaceHeight, Settings);
			TestTrue(
				TEXT("A cached column height must preserve exact density, including caves and bottom fill"),
				FMath::IsNearlyEqual(Reference, Cached, 0.001f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelCellTableTest,
	"SmoothVoxel.Transvoxel.OfficialTablesAllCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelCellTableTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("All official regular and transition table cases must be well-formed"),
		FSVoxelTransvoxelMesher::ValidateLookupTables());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelMesherPlaneTest,
	"SmoothVoxel.Transvoxel.FlatDensityField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelMesherPlaneTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.ChunkWorldOrigin = FVector::ZeroVector;
	Settings.BaseHeight = 75.0f;
	Settings.HeightAmplitude = 0.0f;
	Settings.DetailAmplitude = 0.0f;
	Settings.BottomDepth = 2000.0f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	FSVoxelMeshData RegularMesh;
	FSVoxelTransvoxelMesher::Generate(Settings, 4, 100.0f, 0, RegularMesh);
	TestTrue(TEXT("Flat terrain should generate triangles"), RegularMesh.Triangles.Num() > 0);
	TestEqual(TEXT("Every output vertex has a normal"), RegularMesh.Normals.Num(), RegularMesh.Vertices.Num());
	TestEqual(TEXT("Every output vertex has UVs"), RegularMesh.UV0.Num(), RegularMesh.Vertices.Num());
	TestEqual(TEXT("Every output vertex has tangents"), RegularMesh.Tangents.Num(), RegularMesh.Vertices.Num());

	for (const FVector& Normal : RegularMesh.Normals)
	{
		TestTrue(TEXT("Output normal must be finite"), !Normal.ContainsNaN());
		TestTrue(TEXT("Flat top normals point out of solid"), Normal.Z > 0.5f);
	}
	for (int32 Index = 0; Index < RegularMesh.Tangents.Num(); ++Index)
	{
		const FProcMeshTangent& Tangent = RegularMesh.Tangents[Index];
		TestTrue(TEXT("Output tangent must be finite"), !Tangent.TangentX.ContainsNaN());
		TestTrue(TEXT("Output tangent must be nonzero"), Tangent.TangentX.SizeSquared() > 0.5f);
		TestTrue(
			TEXT("Tangent must be orthogonal to the smooth normal"),
			FMath::Abs(FVector::DotProduct(Tangent.TangentX, RegularMesh.Normals[Index])) < 0.01f);
		TestTrue(
			TEXT("Flat XY-projected terrain should use a consistent world-X tangent"),
			Tangent.TangentX.X > 0.99f);
	}

	// The stored shading normals point outward/upward. Unreal's procedural
	// mesh front-face convention requires the opposite cross-product direction
	// for the indices; otherwise the top is culled and one-sided collision can
	// behave as if the surface were inverted.
	for (int32 Index = 0; Index + 2 < RegularMesh.Triangles.Num(); Index += 3)
	{
		const FVector& A = RegularMesh.Vertices[RegularMesh.Triangles[Index]];
		const FVector& B = RegularMesh.Vertices[RegularMesh.Triangles[Index + 1]];
		const FVector& C = RegularMesh.Vertices[RegularMesh.Triangles[Index + 2]];
		const FVector FaceNormal = FVector::CrossProduct(B - A, C - A);
		TestTrue(TEXT("Flat terrain triangle indices use UE front-face winding"), FaceNormal.Z < 0.0f);
	}

	const uint8 FaceMasksToTest[] =
	{
		SVoxelTransitionFace_NegativeX,
		SVoxelTransitionFace_PositiveX,
		SVoxelTransitionFace_NegativeY,
		SVoxelTransitionFace_PositiveY,
		SVoxelTransitionFace_NegativeZ,
		SVoxelTransitionFace_PositiveZ,
		static_cast<uint8>(SVoxelTransitionFace_PositiveX | SVoxelTransitionFace_PositiveY),
		static_cast<uint8>(SVoxelTransitionFace_PositiveX | SVoxelTransitionFace_PositiveY | SVoxelTransitionFace_PositiveZ)
	};

	for (const uint8 FaceMask : FaceMasksToTest)
	{
		FSVoxelMeshData TransitionMesh;
		FSVoxelTransvoxelMesher::Generate(Settings, 4, 200.0f, FaceMask, TransitionMesh);
		TestTrue(TEXT("Transition-enabled chunk should generate triangles"), TransitionMesh.Triangles.Num() > 0);
		TestEqual(TEXT("Transition output normals match vertices"), TransitionMesh.Normals.Num(), TransitionMesh.Vertices.Num());
		TestEqual(TEXT("Transition output UVs match vertices"), TransitionMesh.UV0.Num(), TransitionMesh.Vertices.Num());
		TestEqual(TEXT("Transition output tangents match vertices"), TransitionMesh.Tangents.Num(), TransitionMesh.Vertices.Num());

		for (const int32 Index : TransitionMesh.Triangles)
		{
			TestTrue(TEXT("Triangle index is within the vertex buffer"),
				Index >= 0 && Index < TransitionMesh.Vertices.Num());
		}
		for (const FVector& Vertex : TransitionMesh.Vertices)
		{
			TestTrue(TEXT("Output vertex must be finite"), !Vertex.ContainsNaN());
		}
		for (const FProcMeshTangent& Tangent : TransitionMesh.Tangents)
		{
			TestTrue(TEXT("Transition tangent must be finite"), !Tangent.TangentX.ContainsNaN());
			TestTrue(TEXT("Transition tangent must be nonzero"), Tangent.TangentX.SizeSquared() > 0.5f);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelLargeCoordinateUVTest,
	"SmoothVoxel.Transvoxel.UVsStayBoundedAtLargeWorldCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelLargeCoordinateUVTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.ChunkWorldOrigin = FVector::ZeroVector;
	Settings.ChunkOriginCells = FIntVector(2000019, 2000000, 0);
	Settings.BaseVoxelSize = 100.0f;
	Settings.bUseChunkLatticeCoordinates = true;
	Settings.BaseHeight = 75.0f;
	Settings.HeightAmplitude = 0.0f;
	Settings.DetailAmplitude = 0.0f;
	Settings.BottomDepth = 2000.0f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	FSVoxelMeshData Mesh;
	// The absolute world coordinate here is 2000 km, so unrebased UVs would
	// be around 100,000. The rendered UV values should instead remain bounded
	// because each chunk removes one shared integer tile offset.
	FSVoxelTransvoxelMesher::Generate(Settings, 4, 100.0f, 0, Mesh);
	TestTrue(TEXT("Large-coordinate fixture should generate terrain triangles"), Mesh.Triangles.Num() > 0);
	TestEqual(TEXT("Each generated vertex has a UV"), Mesh.UV0.Num(), Mesh.Vertices.Num());

	float MaxAbsoluteUv = 0.0f;
	for (const FVector2D& UV : Mesh.UV0)
	{
		TestTrue(TEXT("UV coordinates must remain finite"), FMath::IsFinite(UV.X) && FMath::IsFinite(UV.Y));
		MaxAbsoluteUv = FMath::Max(MaxAbsoluteUv, FMath::Max(FMath::Abs(UV.X), FMath::Abs(UV.Y)));
	}
	TestTrue(
		TEXT("Large absolute world coordinates must not produce large GPU UV values"),
		MaxAbsoluteUv < 2.0f);

	// Adjacent same-LOD chunks must retain the same repeating-texture phase at
	// their shared border while using a single continuous UV offset per chunk.
	FSVoxelGenerationSettings AdjacentSettings = Settings;
	AdjacentSettings.ChunkOriginCells.X += 4; // Chunk width is 4 base-lattice cells.
	FSVoxelMeshData AdjacentMesh;
	FSVoxelTransvoxelMesher::Generate(AdjacentSettings, 4, 100.0f, 0, AdjacentMesh);

	TMap<FIntPoint, FVector2D> AdjacentBorderUvs;
	for (int32 Index = 0; Index < AdjacentMesh.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = AdjacentMesh.Vertices[Index];
		if (FMath::IsNearlyZero(Vertex.X, 0.01f))
		{
			const FIntPoint Key(
				FMath::RoundToInt(Vertex.Y * 10.0f),
				FMath::RoundToInt(Vertex.Z * 10.0f));
			AdjacentBorderUvs.Add(Key, AdjacentMesh.UV0[Index]);
		}
	}

	int32 ComparedBorderUvs = 0;
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = Mesh.Vertices[Index];
		if (!FMath::IsNearlyEqual(Vertex.X, 400.0f, 0.01f))
		{
			continue;
		}

		const FIntPoint Key(
			FMath::RoundToInt(Vertex.Y * 10.0f),
			FMath::RoundToInt(Vertex.Z * 10.0f));
		const FVector2D* AdjacentUV = AdjacentBorderUvs.Find(Key);
		if (!AdjacentUV)
		{
			continue;
		}

		++ComparedBorderUvs;
		const FVector2D& CurrentUV = Mesh.UV0[Index];
		const auto WrappedDifference = [](float A, float B)
		{
			const float FA = A - static_cast<float>(FMath::FloorToInt(A));
			const float FB = B - static_cast<float>(FMath::FloorToInt(B));
			const float Delta = FMath::Abs(FA - FB);
			return FMath::Min(Delta, 1.0f - Delta);
		};
		TestTrue(
			TEXT("Repeating U phase must match across the chunk border"),
			WrappedDifference(CurrentUV.X, AdjacentUV->X) < 0.001f);
		TestTrue(
			TEXT("Repeating V phase must match across the chunk border"),
			WrappedDifference(CurrentUV.Y, AdjacentUV->Y) < 0.001f);
	}

	TestTrue(
		TEXT("Flat adjacent chunks must expose comparable border vertices"),
		ComparedBorderUvs > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelFlatInterfaceTest,
	"SmoothVoxel.Transvoxel.FlatCoarseFineInterface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelFlatInterfaceTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings CoarseSettings;
	CoarseSettings.ChunkWorldOrigin = FVector::ZeroVector;
	CoarseSettings.BaseHeight = 75.0f;
	CoarseSettings.HeightAmplitude = 0.0f;
	CoarseSettings.DetailAmplitude = 0.0f;
	CoarseSettings.BottomDepth = 2000.0f;
	CoarseSettings.bEnableCaves = false;
	CoarseSettings.IsoLevel = 0.0f;

	FSVoxelMeshData CoarseMesh;
	FSVoxelTransvoxelMesher::Generate(
		CoarseSettings, 4, 200.0f, SVoxelTransitionFace_PositiveX, CoarseMesh);

	FSVoxelGenerationSettings FineSettings = CoarseSettings;
	FineSettings.ChunkWorldOrigin = FVector(800.0f, 0.0f, 0.0f);

	FSVoxelMeshData FineMesh;
	FSVoxelTransvoxelMesher::Generate(
		FineSettings, 4, 100.0f, 0, FineMesh);

	TSet<int32> CoarseSeamY;
	TSet<int32> FineSeamY;

	for (const FVector& Vertex : CoarseMesh.Vertices)
	{
		if (FMath::IsNearlyEqual(Vertex.X, 800.0f, 0.01f) &&
			FMath::IsNearlyEqual(Vertex.Z, 75.0f, 0.01f) &&
			Vertex.Y >= -0.01f && Vertex.Y <= 400.01f)
		{
			CoarseSeamY.Add(FMath::RoundToInt(Vertex.Y));
		}
	}

	for (const FVector& Vertex : FineMesh.Vertices)
	{
		if (FMath::IsNearlyZero(Vertex.X, 0.01f) &&
			FMath::IsNearlyEqual(Vertex.Z, 75.0f, 0.01f) &&
			Vertex.Y >= -0.01f && Vertex.Y <= 400.01f)
		{
			FineSeamY.Add(FMath::RoundToInt(Vertex.Y));
		}
	}

	int32 SharedSeamSamples = 0;
	for (const int32 Y : CoarseSeamY)
	{
		if (FineSeamY.Contains(Y))
		{
			++SharedSeamSamples;
		}
	}

	TestTrue(
		TEXT("Coarse transition and fine mesh must share seam vertices on a flat plane"),
		SharedSeamSamples >= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelSlopedFacesTest,
	"SmoothVoxel.Transvoxel.SlopedTerrainAllFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelSlopedFacesTest::RunTest(const FString& Parameters)
{
	// Exercise a non-flat, world-space density field. This catches transition
	// code that uses a density value from one position for a different sample
	// position, which a perfectly flat plane cannot reveal.
	FSVoxelGenerationSettings Settings;
	Settings.ChunkWorldOrigin = FVector(-400.0f, -400.0f, 0.0f);
	Settings.BaseHeight = 75.0f;
	Settings.HeightAmplitude = 300.0f;
	Settings.DetailAmplitude = 45.0f;
	Settings.NoiseScale = 0.0012f;
	Settings.DetailScale = 0.0009f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	const uint8 FaceMasks[] =
	{
		SVoxelTransitionFace_NegativeX,
		SVoxelTransitionFace_PositiveX,
		SVoxelTransitionFace_NegativeY,
		SVoxelTransitionFace_PositiveY,
		SVoxelTransitionFace_NegativeZ,
		SVoxelTransitionFace_PositiveZ,
		static_cast<uint8>(SVoxelTransitionFace_PositiveX | SVoxelTransitionFace_PositiveY),
		static_cast<uint8>(SVoxelTransitionFace_PositiveX | SVoxelTransitionFace_PositiveY | SVoxelTransitionFace_PositiveZ)
	};

	for (const uint8 FaceMask : FaceMasks)
	{
		FSVoxelMeshData Mesh;
		FSVoxelTransvoxelMesher::Generate(Settings, 8, 100.0f, FaceMask, Mesh);

		TestTrue(TEXT("Sloped terrain with transitions should emit triangles"), Mesh.Triangles.Num() > 0);
		TestEqual(TEXT("Transition mesh has one normal per vertex"), Mesh.Normals.Num(), Mesh.Vertices.Num());
		TestEqual(TEXT("Transition mesh has one UV per vertex"), Mesh.UV0.Num(), Mesh.Vertices.Num());
		TestEqual(TEXT("Transition mesh has one tangent per vertex"), Mesh.Tangents.Num(), Mesh.Vertices.Num());

		for (const FVector& Vertex : Mesh.Vertices)
		{
			TestTrue(TEXT("Transition vertex must be finite"), !Vertex.ContainsNaN());
		}
		for (const FVector& Normal : Mesh.Normals)
		{
			TestTrue(TEXT("Transition normal must be finite"), !Normal.ContainsNaN());
		}
		for (int32 TriangleIndex = 0; TriangleIndex + 2 < Mesh.Triangles.Num(); TriangleIndex += 3)
		{
			const int32 AIndex = Mesh.Triangles[TriangleIndex];
			const int32 BIndex = Mesh.Triangles[TriangleIndex + 1];
			const int32 CIndex = Mesh.Triangles[TriangleIndex + 2];
			TestTrue(TEXT("Transition triangle indices must be valid"),
				AIndex >= 0 && AIndex < Mesh.Vertices.Num() &&
				BIndex >= 0 && BIndex < Mesh.Vertices.Num() &&
				CIndex >= 0 && CIndex < Mesh.Vertices.Num());
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelSharedTransitionEdgeTest,
	"SmoothVoxel.Transvoxel.MultiFaceTransitionSharesExactEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelSharedTransitionEdgeTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.ChunkWorldOrigin = FVector::ZeroVector;
	Settings.BaseHeight = 200.0f;
	Settings.HeightAmplitude = 120.0f;
	Settings.DetailAmplitude = 30.0f;
	Settings.NoiseScale = 0.0008f;
	Settings.DetailScale = 0.0006f;
	Settings.BottomDepth = 2000.0f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	const uint8 CornerMask = static_cast<uint8>(
		SVoxelTransitionFace_PositiveX | SVoxelTransitionFace_NegativeY);
	FSVoxelMeshData Mesh;
	FSVoxelTransvoxelMesher::Generate(Settings, 8, 200.0f, CornerMask, Mesh);

	const float ChunkSize = 1600.0f;
	int32 EdgeVertices = 0;
	for (const FVector& Vertex : Mesh.Vertices)
	{
		TestTrue(TEXT("Multi-face transition must not emit NaN vertices"), !Vertex.ContainsNaN());
		if (FMath::IsNearlyEqual(Vertex.X, ChunkSize, 0.01f) &&
			FMath::IsNearlyZero(Vertex.Y, 0.01f))
		{
			++EdgeVertices;
		}
	}

	TestTrue(
		TEXT("Adjacent transition faces must retain a shared exact edge at their intersection"),
		EdgeVertices > 0);
	TestEqual(TEXT("Every generated transition vertex has a normal"),
		Mesh.Normals.Num(), Mesh.Vertices.Num());
	TestEqual(TEXT("Every generated transition vertex has a tangent"),
		Mesh.Tangents.Num(), Mesh.Vertices.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelTangentContinuityTest,
	"SmoothVoxel.Transvoxel.TangentsContinuousAcrossRegularCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelTangentContinuityTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.ChunkWorldOrigin = FVector(-800.0f, -800.0f, 0.0f);
	Settings.BaseHeight = 200.0f;
	Settings.HeightAmplitude = 160.0f;
	Settings.DetailAmplitude = 25.0f;
	Settings.NoiseScale = 0.0008f;
	Settings.DetailScale = 0.0006f;
	Settings.BottomDepth = 2000.0f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	FSVoxelMeshData Mesh;
	FSVoxelTransvoxelMesher::Generate(Settings, 8, 100.0f, 0, Mesh);
	TestEqual(TEXT("Every vertex should receive one tangent"), Mesh.Tangents.Num(), Mesh.Vertices.Num());

	TMap<FIntVector, FVector> TangentByPosition;
	int32 DuplicatePositions = 0;
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		const FVector KeyPosition = Mesh.Vertices[Index] * 10.0f;
		const FIntVector Key(
			FMath::RoundToInt(KeyPosition.X),
			FMath::RoundToInt(KeyPosition.Y),
			FMath::RoundToInt(KeyPosition.Z));
		const FVector Tangent = Mesh.Tangents[Index].TangentX.GetSafeNormal();
		if (const FVector* Existing = TangentByPosition.Find(Key))
		{
			++DuplicatePositions;
			TestTrue(
				TEXT("Duplicate vertices on adjacent regular cells must have the same tangent frame"),
				FVector::DotProduct(*Existing, Tangent) > 0.999f);
		}
		else
		{
			TangentByPosition.Add(Key, Tangent);
		}
	}

	TestTrue(TEXT("Fixture should contain vertices shared by adjacent cells"), DuplicatePositions > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelSameLodNormalSeamTest,
	"SmoothVoxel.Transvoxel.NormalsMatchAcrossSameLodSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelSameLodNormalSeamTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings LeftSettings;
	LeftSettings.ChunkWorldOrigin = FVector::ZeroVector;
	LeftSettings.BaseHeight = 200.0f;
	LeftSettings.HeightAmplitude = 160.0f;
	LeftSettings.DetailAmplitude = 25.0f;
	LeftSettings.NoiseScale = 0.0008f;
	LeftSettings.DetailScale = 0.0006f;
	LeftSettings.BottomDepth = 2000.0f;
	LeftSettings.bEnableCaves = false;
	LeftSettings.IsoLevel = 0.0f;

	FSVoxelMeshData LeftMesh;
	// This chunk transitions on -Y but shares its +X face with a same-LOD
	// neighbour. Border normals along +X must not inherit the -Y fine step.
	FSVoxelTransvoxelMesher::Generate(
		LeftSettings, 8, 100.0f, SVoxelTransitionFace_NegativeY, LeftMesh);

	FSVoxelGenerationSettings RightSettings = LeftSettings;
	RightSettings.ChunkWorldOrigin = FVector(800.0f, 0.0f, 0.0f);
	FSVoxelMeshData RightMesh;
	FSVoxelTransvoxelMesher::Generate(RightSettings, 8, 100.0f, 0, RightMesh);

	TMap<FIntPoint, FVector> LeftNormals;
	TMap<FIntPoint, FVector> RightNormals;
	for (int32 Index = 0; Index < LeftMesh.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = LeftMesh.Vertices[Index];
		if (FMath::IsNearlyEqual(Vertex.X, 800.0f, 0.01f))
		{
			LeftNormals.Add(FIntPoint(
				FMath::RoundToInt(Vertex.Y * 10.0f),
				FMath::RoundToInt(Vertex.Z * 10.0f)), LeftMesh.Normals[Index]);
		}
	}
	for (int32 Index = 0; Index < RightMesh.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = RightMesh.Vertices[Index];
		if (FMath::IsNearlyZero(Vertex.X, 0.01f))
		{
			RightNormals.Add(FIntPoint(
				FMath::RoundToInt(Vertex.Y * 10.0f),
				FMath::RoundToInt(Vertex.Z * 10.0f)), RightMesh.Normals[Index]);
		}
	}

	int32 ComparedNormals = 0;
	for (const TPair<FIntPoint, FVector>& Pair : LeftNormals)
	{
		if (const FVector* RightNormal = RightNormals.Find(Pair.Key))
		{
			++ComparedNormals;
			TestTrue(
				TEXT("Same-LOD neighbour must use the same normal sampling step at the shared face"),
				FVector::DotProduct(Pair.Value, *RightNormal) > 0.999f);
		}
	}
	TestTrue(TEXT("Test fixture must contain shared surface vertices"), ComparedNormals > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelSlopedCoarseFineSeamTest,
	"SmoothVoxel.Transvoxel.SlopedCoarseFineSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelSlopedCoarseFineSeamTest::RunTest(const FString& Parameters)
{
	// A sloped height field makes the paired transition samples differ if the
	// four half-resolution values are incorrectly resampled at their offset
	// positions. On the outer boundary, the coarse transition contour must
	// match the fine chunk's regular-mesh contour.
	FSVoxelGenerationSettings CoarseSettings;
	CoarseSettings.ChunkWorldOrigin = FVector::ZeroVector;
	CoarseSettings.BaseHeight = 200.0f;
	CoarseSettings.HeightAmplitude = 120.0f;
	CoarseSettings.DetailAmplitude = 30.0f;
	CoarseSettings.NoiseScale = 0.0008f;
	CoarseSettings.DetailScale = 0.0006f;
	CoarseSettings.BottomDepth = 2000.0f;
	CoarseSettings.bEnableCaves = false;
	CoarseSettings.IsoLevel = 0.0f;

	FSVoxelMeshData CoarseMesh;
	FSVoxelTransvoxelMesher::Generate(
		CoarseSettings, 4, 200.0f, SVoxelTransitionFace_PositiveX, CoarseMesh);

	FSVoxelGenerationSettings FineSettings = CoarseSettings;
	FineSettings.ChunkWorldOrigin = FVector(800.0f, 0.0f, 0.0f);

	FSVoxelMeshData FineMesh;
	FSVoxelTransvoxelMesher::Generate(FineSettings, 8, 100.0f, 0, FineMesh);

	TSet<FIntPoint> CoarseSeam;
	TSet<FIntPoint> FineSeam;
	TMap<FIntPoint, FVector> CoarseSeamNormals;
	TMap<FIntPoint, FVector> FineSeamNormals;

	for (int32 Index = 0; Index < CoarseMesh.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = CoarseMesh.Vertices[Index];
		if (FMath::IsNearlyEqual(Vertex.X, 800.0f, 0.01f))
		{
			const FIntPoint Key(
				FMath::RoundToInt(Vertex.Y * 10.0f),
				FMath::RoundToInt(Vertex.Z * 10.0f));
			CoarseSeam.Add(Key);
			CoarseSeamNormals.Add(Key, CoarseMesh.Normals[Index]);
		}
	}

	for (int32 Index = 0; Index < FineMesh.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = FineMesh.Vertices[Index];
		if (FMath::IsNearlyZero(Vertex.X, 0.01f))
		{
			const FIntPoint Key(
				FMath::RoundToInt(Vertex.Y * 10.0f),
				FMath::RoundToInt(Vertex.Z * 10.0f));
			FineSeam.Add(Key);
			FineSeamNormals.Add(Key, FineMesh.Normals[Index]);
		}
	}

	TestTrue(TEXT("Sloped interface must contain a nonempty contour on the shared face"),
		CoarseSeam.Num() >= 3 && FineSeam.Num() >= 3);

	for (const FIntPoint& Point : CoarseSeam)
	{
		TestTrue(TEXT("Each coarse transition boundary vertex must exist on the fine mesh"),
			FineSeam.Contains(Point));
	}
	for (const FIntPoint& Point : FineSeam)
	{
		TestTrue(TEXT("Each fine boundary vertex must exist on the coarse transition mesh"),
			CoarseSeam.Contains(Point));
	}

	for (const TPair<FIntPoint, FVector>& Pair : CoarseSeamNormals)
	{
		const FVector* FineNormal = FineSeamNormals.Find(Pair.Key);
		if (FineNormal)
		{
			TestTrue(
				TEXT("World-space normal sampling must keep shared coarse/fine seam normals aligned"),
				Pair.Value.Equals(*FineNormal, 0.001f));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
