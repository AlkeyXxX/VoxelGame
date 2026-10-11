#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SmoothVoxelTransvoxelMesher.h"

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
				TEXT("LOD-aware normal smoothing must keep shared coarse/fine seam normals aligned"),
				Pair.Value.Equals(*FineNormal, 0.001f));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
