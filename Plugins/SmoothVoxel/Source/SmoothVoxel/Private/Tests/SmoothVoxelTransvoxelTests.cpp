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

	for (const FVector& Normal : RegularMesh.Normals)
	{
		TestTrue(TEXT("Output normal must be finite"), !Normal.ContainsNaN());
		TestTrue(TEXT("Flat top normals point out of solid"), Normal.Z > 0.5f);
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

		for (const int32 Index : TransitionMesh.Triangles)
		{
			TestTrue(TEXT("Triangle index is within the vertex buffer"),
				Index >= 0 && Index < TransitionMesh.Vertices.Num());
		}
		for (const FVector& Vertex : TransitionMesh.Vertices)
		{
			TestTrue(TEXT("Output vertex must be finite"), !Vertex.ContainsNaN());
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

#endif // WITH_DEV_AUTOMATION_TESTS
