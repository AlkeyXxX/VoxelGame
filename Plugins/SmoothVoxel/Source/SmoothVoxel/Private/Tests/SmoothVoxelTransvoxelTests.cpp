#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SmoothVoxelTransvoxelMesher.h"
#include "TransvoxelCore/transvoxel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelCellTableTest,
	"SmoothVoxel.Transvoxel.AllCellCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelCellTableTest::RunTest(const FString& Parameters)
{
	for (int32 CaseIndex = 0; CaseIndex < TVC_REGULAR_CASE_COUNT; ++CaseIndex)
	{
		float Samples[TV_REGULAR_SAMPLE_COUNT];
		for (int32 Corner = 0; Corner < TV_REGULAR_SAMPLE_COUNT; ++Corner)
		{
			Samples[Corner] = (CaseIndex & (1 << Corner)) ? -1.0f : 1.0f;
		}

		TvVec3 Vertices[TV_REGULAR_MAX_VERTICES];
		TvTriangle Triangles[TV_REGULAR_MAX_TRIANGLES];
		const TvBuildInfo Info = tv_build_regular_cell(
			Samples, 0.0f, tv_vec3(0.0f, 0.0f, 0.0f), tv_vec3(1.0f, 1.0f, 1.0f),
			Vertices, TV_REGULAR_MAX_VERTICES, Triangles, TV_REGULAR_MAX_TRIANGLES);

		TestEqual(TEXT("Regular case should build"), Info.result, TV_OK);
		TestTrue(TEXT("Regular vertex count is bounded"), Info.vertex_count >= 0 && Info.vertex_count <= TV_REGULAR_MAX_VERTICES);
		TestTrue(TEXT("Regular triangle count is bounded"), Info.triangle_count >= 0 && Info.triangle_count <= TV_REGULAR_MAX_TRIANGLES);
		for (int32 T = 0; T < Info.triangle_count; ++T)
		{
			TestTrue(TEXT("Regular triangle indices are valid"),
				Triangles[T].a < static_cast<uint32>(Info.vertex_count) &&
				Triangles[T].b < static_cast<uint32>(Info.vertex_count) &&
				Triangles[T].c < static_cast<uint32>(Info.vertex_count));
		}
	}

	for (int32 CaseIndex = 0; CaseIndex < TVC_TRANSITION_CASE_COUNT; ++CaseIndex)
	{
		float Samples[TV_TRANSITION_SAMPLE_COUNT];
		for (int32 Sample = 0; Sample < TV_TRANSITION_SAMPLE_COUNT; ++Sample)
		{
			Samples[Sample] = 1.0f;
		}
		for (int32 Corner = 0; Corner < TV_TRANSITION_HIGH_SAMPLE_COUNT; ++Corner)
		{
			Samples[Corner] = (CaseIndex & (1 << Corner)) ? -1.0f : 1.0f;
		}
		tv_transition_fill_derived_samples(Samples);

		TvVec3 Vertices[TV_TRANSITION_MAX_VERTICES];
		TvTriangle Triangles[TV_TRANSITION_MAX_TRIANGLES];
		const TvBuildInfo Info = tv_build_transition_cell(
			Samples, 0.0f, tv_vec3(0.0f, 0.0f, 0.0f), tv_vec3(1.0f, 1.0f, 1.0f),
			Vertices, TV_TRANSITION_MAX_VERTICES, Triangles, TV_TRANSITION_MAX_TRIANGLES);

		TestEqual(TEXT("Transition case should build"), Info.result, TV_OK);
		TestTrue(TEXT("Transition vertex count is bounded"), Info.vertex_count >= 0 && Info.vertex_count <= TV_TRANSITION_MAX_VERTICES);
		TestTrue(TEXT("Transition triangle count is bounded"), Info.triangle_count >= 0 && Info.triangle_count <= TV_TRANSITION_MAX_TRIANGLES);
		for (int32 T = 0; T < Info.triangle_count; ++T)
		{
			TestTrue(TEXT("Transition triangle indices are valid"),
				Triangles[T].a < static_cast<uint32>(Info.vertex_count) &&
				Triangles[T].b < static_cast<uint32>(Info.vertex_count) &&
				Triangles[T].c < static_cast<uint32>(Info.vertex_count));
		}
	}

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
