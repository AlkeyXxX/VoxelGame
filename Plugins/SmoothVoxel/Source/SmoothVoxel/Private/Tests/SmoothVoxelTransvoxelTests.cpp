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

	FSVoxelMeshData TransitionMesh;
	FSVoxelTransvoxelMesher::Generate(
		Settings, 4, 200.0f, SVoxelTransitionFace_PositiveX, TransitionMesh);
	TestTrue(TEXT("Transition-enabled chunk should generate triangles"), TransitionMesh.Triangles.Num() > 0);
	TestEqual(TEXT("Transition output normals match vertices"), TransitionMesh.Normals.Num(), TransitionMesh.Vertices.Num());

	for (const int32 Index : TransitionMesh.Triangles)
	{
		TestTrue(TEXT("Triangle index is within the vertex buffer"),
			Index >= 0 && Index < TransitionMesh.Vertices.Num());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
