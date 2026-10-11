#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelTransvoxelMesher.h"
#include "SmoothVoxelOctreeWorld.h"
 
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelWorldConfigurationDeterminismTest,
	"SmoothVoxel.WorldGeneration.SizePresetsAndGlobalSeedDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelWorldConfigurationDeterminismTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("2K preset must define a 2 km world window"),
		ASmoothVoxelOctreeWorld::GetWorldSizeCentimetersForPreset(ESmoothVoxelWorldSize::Size2Km),
		200000.0f);
	TestEqual(
		TEXT("4K preset must define a 4 km world window"),
		ASmoothVoxelOctreeWorld::GetWorldSizeCentimetersForPreset(ESmoothVoxelWorldSize::Size4Km),
		400000.0f);
	TestEqual(
		TEXT("8K preset must define an 8 km world window"),
		ASmoothVoxelOctreeWorld::GetWorldSizeCentimetersForPreset(ESmoothVoxelWorldSize::Size8Km),
		800000.0f);

	TestEqual(
		TEXT("2K preset must use root LOD 6"),
		ASmoothVoxelOctreeWorld::GetRootLodForWorldSizePreset(ESmoothVoxelWorldSize::Size2Km),
		6);
	TestEqual(
		TEXT("4K preset must use root LOD 7"),
		ASmoothVoxelOctreeWorld::GetRootLodForWorldSizePreset(ESmoothVoxelWorldSize::Size4Km),
		7);
	TestEqual(
		TEXT("8K preset must use root LOD 8"),
		ASmoothVoxelOctreeWorld::GetRootLodForWorldSizePreset(ESmoothVoxelWorldSize::Size8Km),
		8);

	TestEqual(
		TEXT("The 4K world should preserve LOD 4 under the 2048-leaf budget"),
		ASmoothVoxelOctreeWorld::GetEffectiveMaxLodForBudget(7, 4, 2048),
		4);
	TestEqual(
		TEXT("The 8K world should automatically use a coarser far LOD under the 2048-leaf budget"),
		ASmoothVoxelOctreeWorld::GetEffectiveMaxLodForBudget(8, 4, 2048),
		5);
	TestEqual(
		TEXT("Explicitly increasing the budget can restore LOD 4 coverage for the 8K world"),
		ASmoothVoxelOctreeWorld::GetEffectiveMaxLodForBudget(8, 4, 8192),
		4);

	const float BaseSteps[] =
	{
		ASmoothVoxelOctreeWorld::GetWorldSizeCentimetersForPreset(ESmoothVoxelWorldSize::Size2Km) /
			(32.0f * static_cast<float>(1 << ASmoothVoxelOctreeWorld::GetRootLodForWorldSizePreset(ESmoothVoxelWorldSize::Size2Km))),
		ASmoothVoxelOctreeWorld::GetWorldSizeCentimetersForPreset(ESmoothVoxelWorldSize::Size4Km) /
			(32.0f * static_cast<float>(1 << ASmoothVoxelOctreeWorld::GetRootLodForWorldSizePreset(ESmoothVoxelWorldSize::Size4Km))),
		ASmoothVoxelOctreeWorld::GetWorldSizeCentimetersForPreset(ESmoothVoxelWorldSize::Size8Km) /
			(32.0f * static_cast<float>(1 << ASmoothVoxelOctreeWorld::GetRootLodForWorldSizePreset(ESmoothVoxelWorldSize::Size8Km)))
	};
	TestTrue(
		TEXT("Changing the map size must not change the default base voxel spacing"),
		FMath::IsNearlyEqual(BaseSteps[0], BaseSteps[1], 0.001f) &&
		FMath::IsNearlyEqual(BaseSteps[1], BaseSteps[2], 0.001f));

	FSVoxelGenerationSettings Settings;
	Settings.BaseHeight = 900.0f;
	Settings.HeightAmplitude = 1800.0f;
	Settings.DetailAmplitude = 60.0f;
	Settings.NoiseScale = 0.00005f;
	Settings.DetailScale = 0.0000375f;
	Settings.BottomDepth = 2000.0f;
	Settings.bEnableCaves = false;
	Settings.Seed = 1337;

	const FVector LocalSamples[] =
	{
		FVector(1234.25f, 4567.5f, 700.0f),
		FVector(37891.75f, 8421.25f, 930.0f),
		FVector(901.5f, 72113.25f, 1200.0f),
		FVector(65535.0f, 31415.5f, 850.0f)
	};
	const double ChunkOriginCm = -100000.0;
	constexpr float StableBaseVoxelSize = 97.65625f;
	FSVoxelGenerationSettings ChunkSettings = Settings;
	ChunkSettings.BaseVoxelSize = StableBaseVoxelSize;
	ChunkSettings.ChunkOriginCells = FIntVector(-1024, -1024, 0);
	ChunkSettings.bUseChunkLatticeCoordinates = true;

	bool bDifferentSeedChangedDensity = false;
	FSVoxelGenerationSettings DifferentSeedSettings = Settings;
	DifferentSeedSettings.Seed = Settings.Seed + 1;

	for (const FVector& LocalPosition : LocalSamples)
	{
		const double WorldX = ChunkOriginCm + LocalPosition.X;
		const double WorldY = ChunkOriginCm + LocalPosition.Y;
		const double WorldZ = LocalPosition.Z;
		const float DirectDensity =
			FSVoxelGenerator::SampleDensity(WorldX, WorldY, WorldZ, Settings);
		const float RepeatDensity =
			FSVoxelGenerator::SampleDensity(WorldX, WorldY, WorldZ, Settings);
		const float ChunkDensity =
			FSVoxelGenerator::SampleDensityAtChunkLocal(LocalPosition, ChunkSettings);
		const float DifferentSeedDensity =
			FSVoxelGenerator::SampleDensity(WorldX, WorldY, WorldZ, DifferentSeedSettings);

		TestTrue(
			TEXT("A fixed seed and world coordinate must reproduce identical density"),
			FMath::IsNearlyEqual(DirectDensity, RepeatDensity, 0.0001f));
		TestTrue(
			TEXT("Sampling the same global coordinate through a chunk lattice must preserve density"),
			FMath::IsNearlyEqual(DirectDensity, ChunkDensity, 0.001f));
		bDifferentSeedChangedDensity |=
			!FMath::IsNearlyEqual(DirectDensity, DifferentSeedDensity, 0.001f);
	}

	TestTrue(
		TEXT("Changing the world seed must alter at least some procedural terrain samples"),
		bDifferentSeedChangedDensity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelBiomesAndPOIPlateausTest,
	"SmoothVoxel.WorldGeneration.BiomesAndPOIPlateausAreDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelBiomesAndPOIPlateausTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.Seed = 1337;
	Settings.BaseHeight = 900.0f;
	Settings.HeightAmplitude = 2500.0f;
	Settings.DetailAmplitude = 60.0f;
	Settings.BiomeScale = 0.00001f;
	Settings.bEnableBiomes = true;
	Settings.bEnablePOIPlateaus = true;
	Settings.POISiteSpacing = 65000.0f;
	Settings.POIPlateauRadius = 6500.0f;
	Settings.POIPlateauBlendDistance = 5000.0f;

	TSet<uint8> FoundBiomes;
	bool bDifferentSeedChangedBiome = false;
	FSVoxelGenerationSettings DifferentSeedSettings = Settings;
	DifferentSeedSettings.Seed += 1;
	for (double X = -350000.0; X <= 350000.0; X += 25000.0)
	{
		for (double Y = -350000.0; Y <= 350000.0; Y += 25000.0)
		{
			const ESVoxelBiome Biome = FSVoxelGenerator::SampleBiome(X, Y, Settings);
			const ESVoxelBiome RepeatedBiome = FSVoxelGenerator::SampleBiome(X, Y, Settings);
			TestEqual(TEXT("Biome type is deterministic for a fixed seed and coordinate"),
				static_cast<uint8>(Biome), static_cast<uint8>(RepeatedBiome));
			FoundBiomes.Add(static_cast<uint8>(Biome));
			bDifferentSeedChangedBiome |= Biome != FSVoxelGenerator::SampleBiome(X, Y, DifferentSeedSettings);
		}
	}
	TestTrue(TEXT("The map should contain several distinct biome regions"), FoundBiomes.Num() >= 3);
	TestTrue(TEXT("Changing the seed should change at least one biome sample"), bDifferentSeedChangedBiome);

	double PlateauX = 0.0;
	double PlateauY = 0.0;
	bool bFoundStableCore = false;
	for (double X = -150000.0; X <= 150000.0 && !bFoundStableCore; X += 2500.0)
	{
		for (double Y = -150000.0; Y <= 150000.0; Y += 2500.0)
		{
			if (FSVoxelGenerator::SamplePOISuitability(X, Y, Settings) > 0.99f &&
				FSVoxelGenerator::SamplePOISuitability(X + 500.0, Y, Settings) > 0.99f &&
				FSVoxelGenerator::SamplePOISuitability(X - 500.0, Y, Settings) > 0.99f &&
				FSVoxelGenerator::SamplePOISuitability(X, Y + 500.0, Settings) > 0.99f &&
				FSVoxelGenerator::SamplePOISuitability(X, Y - 500.0, Settings) > 0.99f)
			{
				PlateauX = X;
				PlateauY = Y;
				bFoundStableCore = true;
				break;
			}
		}
	}
	TestTrue(TEXT("A generated map should expose stable flat clearing candidates"), bFoundStableCore);
	if (bFoundStableCore)
	{
		const double CenterHeight = FSVoxelGenerator::SampleSurfaceHeight(PlateauX, PlateauY, Settings);
		TestTrue(TEXT("The clearing core remains level across X"),
			FMath::IsNearlyEqual(CenterHeight, FSVoxelGenerator::SampleSurfaceHeight(PlateauX + 500.0, PlateauY, Settings), 0.001));
		TestTrue(TEXT("The clearing core remains level across Y"),
			FMath::IsNearlyEqual(CenterHeight, FSVoxelGenerator::SampleSurfaceHeight(PlateauX, PlateauY - 500.0, Settings), 0.001));
	}
	return true;
}

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
	TestEqual(TEXT("Every output vertex has a biome tint"), RegularMesh.VertexColors.Num(), RegularMesh.Vertices.Num());

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
		TestEqual(TEXT("Transition output biome tints match vertices"), TransitionMesh.VertexColors.Num(), TransitionMesh.Vertices.Num());

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

	TestEqual(TEXT("Every vertex should have one normal"), Mesh.Normals.Num(), Mesh.Vertices.Num());
	TestEqual(TEXT("Every vertex should have one UV"), Mesh.UV0.Num(), Mesh.Vertices.Num());

	TArray<int32> VertexUseCount;
	VertexUseCount.Init(0, Mesh.Vertices.Num());
	for (const int32 VertexIndex : Mesh.Triangles)
	{
		if (VertexUseCount.IsValidIndex(VertexIndex))
		{
			++VertexUseCount[VertexIndex];
		}
	}

	int32 SharedIndexedVertices = 0;
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		if (!Mesh.Tangents.IsValidIndex(Index) || !Mesh.Normals.IsValidIndex(Index))
		{
			continue;
		}
		const FVector Tangent = Mesh.Tangents[Index].TangentX.GetSafeNormal();
		TestTrue(TEXT("Every tangent must be finite"), !Tangent.ContainsNaN());
		TestTrue(TEXT("Every tangent must be nonzero"), Tangent.SizeSquared() > 0.5f);
		TestTrue(
			TEXT("Tangent must be orthogonal to its normal"),
			FMath::Abs(FVector::DotProduct(Tangent, Mesh.Normals[Index])) < 0.01f);

		if (VertexUseCount[Index] > 1)
		{
			// An indexed shared vertex has one stored tangent frame regardless
			// of how many adjacent regular-cell triangles reference it. This is
			// the robust continuity invariant; duplicate position records are no
			// longer required after vertex-cache welding was introduced.
			++SharedIndexedVertices;
		}
	}

	TestTrue(
		TEXT("Regular-cell triangles should reuse indexed shared vertices with a single tangent frame"),
		SharedIndexedVertices > 0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelMixedBoundaryOffsetTest,
	"SmoothVoxel.Transvoxel.TransitionOffsetPreservesAdjacentSameLodFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelMixedBoundaryOffsetTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.ChunkWorldOrigin = FVector::ZeroVector;
	Settings.BaseHeight = 75.0f;
	Settings.HeightAmplitude = 0.0f;
	Settings.DetailAmplitude = 0.0f;
	Settings.BottomDepth = 2000.0f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	constexpr int32 CellsPerAxis = 4;
	constexpr float CoarseStep = 200.0f;
	const float ChunkSize = CellsPerAxis * CoarseStep;

	// The -Y face transitions to finer terrain, while +X remains adjacent to
	// a same-LOD chunk. Boundary vertices must move for -Y without leaving +X.
	FSVoxelMeshData Mesh;
	FSVoxelTransvoxelMesher::Generate(
		Settings, CellsPerAxis, CoarseStep, SVoxelTransitionFace_NegativeY, Mesh);

	bool bFoundOffsetAtMixedFaceJunction = false;
	for (const FVector& Vertex : Mesh.Vertices)
	{
		if (FMath::IsNearlyEqual(Vertex.X, ChunkSize, 0.01f) &&
			FMath::IsNearlyEqual(Vertex.Y, CoarseStep * 0.25f, 0.01f) &&
			FMath::IsNearlyEqual(Vertex.Z, Settings.BaseHeight, 0.01f))
		{
			bFoundOffsetAtMixedFaceJunction = true;
			break;
		}
	}

	TestTrue(
		TEXT("A vertex on a same-LOD face must retain that face while still receiving the adjacent LOD transition offset"),
		bFoundOffsetAtMixedFaceJunction);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelMinedCellDensityTest,
	"SmoothVoxel.Transvoxel.MinedCellCutsAlignedVoxelCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelMinedCellDensityTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.BaseHeight = 250.0f;
	Settings.HeightAmplitude = 0.0f;
	Settings.DetailAmplitude = 0.0f;
	Settings.NoiseScale = 0.0f;
	Settings.DetailScale = 0.0f;
	Settings.BaseVoxelSize = 50.0f;
	Settings.BottomDepth = 1000.0f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	const float BeforeCut =
		FSVoxelGenerator::SampleDensity(125.0, 125.0, 225.0, Settings);
	TSharedPtr<FSVoxelMinedNodeSet, ESPMode::ThreadSafe> MinedNodes =
		MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>();
	const FIntVector MinedCell(2, 2, 4);
	MinedNodes->Add(MinedCell);
	Settings.MinedNodes = MinedNodes;

	const float AtMinedCellCenter =
		FSVoxelGenerator::SampleDensity(125.0, 125.0, 225.0, Settings);
	const float OutsideMinedCell =
		FSVoxelGenerator::SampleDensity(225.0, 125.0, 225.0, Settings);
	TestTrue(TEXT("A mined voxel cell must become air at its centre"), AtMinedCellCenter < 0.0f);
	TestTrue(TEXT("Mining one voxel cell must not clear a distant cell"), OutsideMinedCell > 0.0f);
	TestTrue(TEXT("Fixture must be solid before applying the mined-node snapshot"), BeforeCut > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelHardCubePlacementTerrainUnchangedTest,
	"SmoothVoxel.Transvoxel.HardCubePlacementDoesNotChangeTerrainMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelHardCubePlacementTerrainUnchangedTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.BaseHeight = 250.0f;
	Settings.HeightAmplitude = 0.01f; // Avoid exact zero density on the entire sample plane.
	Settings.DetailAmplitude = 0.0f;
	Settings.NoiseScale = 0.00005f;
	Settings.DetailScale = 0.0f;
	Settings.BaseVoxelSize = 50.0f;
	Settings.NormalSampleStep = 5.0f;
	Settings.BottomDepth = 1000.0f;
	Settings.bEnableCaves = false;

	TSharedPtr<FSVoxelBlockEditMap, ESPMode::ThreadSafe> Edits =
		MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>();
	// Hard cubes are separate actors and must not excavate or distort the
	// terrain density field. Generate an unedited baseline before enabling the
	// logical +2 occupancy marker, then require exactly the same terrain mesh.
	Edits->Add(FIntVector(2, 2, 4), int8(2));
	FSVoxelMeshData BaselineMesh;
	FSVoxelTransvoxelMesher::Generate(Settings, 16, 50.0f, 0, BaselineMesh);
	Settings.BlockEdits = Edits;

	FSVoxelMeshData Mesh;
	FSVoxelTransvoxelMesher::Generate(Settings, 16, 50.0f, 0, Mesh);
	TestTrue(TEXT("Terrain should still generate triangles beside a separately rendered hard cube"),
		Mesh.Triangles.Num() > 0);
	TestEqual(TEXT("Mesh triangle count must be divisible by three"),
		Mesh.Triangles.Num() % 3, 0);
	TestEqual(TEXT("Hard-cube occupancy must not add or remove terrain vertices"),
		Mesh.Vertices.Num(), BaselineMesh.Vertices.Num());
	TestEqual(TEXT("Hard-cube occupancy must not add or remove terrain triangle indices"),
		Mesh.Triangles.Num(), BaselineMesh.Triangles.Num());
	TestTrue(TEXT("Hard-cube occupancy must leave terrain topology unchanged"),
		Mesh.Triangles == BaselineMesh.Triangles);

	bool bSameGeometry = Mesh.Vertices.Num() == BaselineMesh.Vertices.Num();
	for (int32 Index = 0; bSameGeometry && Index < Mesh.Vertices.Num(); ++Index)
	{
		bSameGeometry &= Mesh.Vertices[Index].Equals(BaselineMesh.Vertices[Index], 0.001f);
	}
	TestTrue(TEXT("Hard-cube occupancy must not shift terrain surface intersections"), bSameGeometry);

	bool bSameNormals = Mesh.Normals.Num() == BaselineMesh.Normals.Num();
	for (int32 Index = 0; bSameNormals && Index < Mesh.Normals.Num(); ++Index)
	{
		bSameNormals &= Mesh.Normals[Index].Equals(BaselineMesh.Normals[Index], 0.001f);
	}
	TestTrue(TEXT("Hard-cube occupancy must not change surrounding terrain normals"), bSameNormals);

	bool bAllIndicesValid = true;
	bool bNoDuplicateIndices = true;
	bool bNoZeroAreaTriangles = true;
	for (int32 I = 0; I + 2 < Mesh.Triangles.Num(); I += 3)
	{
		const int32 A = Mesh.Triangles[I];
		const int32 B = Mesh.Triangles[I + 1];
		const int32 C = Mesh.Triangles[I + 2];
		if (!Mesh.Vertices.IsValidIndex(A) ||
			!Mesh.Vertices.IsValidIndex(B) ||
			!Mesh.Vertices.IsValidIndex(C))
		{
			bAllIndicesValid = false;
			continue;
		}
		if (A == B || B == C || C == A)
		{
			bNoDuplicateIndices = false;
			continue;
		}
		const FVector Cross = FVector::CrossProduct(
			Mesh.Vertices[B] - Mesh.Vertices[A],
			Mesh.Vertices[C] - Mesh.Vertices[A]);
		bNoZeroAreaTriangles &= Cross.SizeSquared() > SMALL_NUMBER;
	}
	TestTrue(TEXT("Hard-cube placement must not emit invalid triangle indices"), bAllIndicesValid);
	TestTrue(TEXT("Hard-cube placement must not collapse triangles"), bNoDuplicateIndices);
	TestTrue(TEXT("Hard-cube placement must not emit zero-area triangles"), bNoZeroAreaTriangles);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelBlockEditDensityTest,
	"SmoothVoxel.Transvoxel.BlockEditsCreateAndRemoveSolidVolumes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelBlockEditDensityTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.BaseHeight = 0.0f;
	Settings.HeightAmplitude = 0.0f;
	Settings.DetailAmplitude = 0.0f;
	Settings.NoiseScale = 0.0f;
	Settings.DetailScale = 0.0f;
	Settings.BaseVoxelSize = 100.0f;
	Settings.BottomDepth = 2000.0f;
	Settings.bEnableCaves = false;
	Settings.IsoLevel = 0.0f;

	TSharedPtr<FSVoxelBlockEditMap, ESPMode::ThreadSafe> Edits =
		MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>();
	Edits->Add(FIntVector(1, 0, 0), int8(1));   // Smooth soil cell above the surface.
	Edits->Add(FIntVector(1, 1, 1), int8(1));   // Smooth cell must be found near its boundary.
	Edits->Add(FIntVector(0, 0, 0), int8(-5)); // Fully mined octahedron wins where it overlaps a smooth addition.
	Edits->Add(FIntVector(0, 0, -1), int8(-1)); // Carve one cell below the surface.
	Edits->Add(FIntVector(2, 0, -1), int8(2));  // Hard cube overlaps otherwise solid terrain.
	Settings.BlockEdits = Edits;

	const float PlacedCellDensity = FSVoxelGenerator::SampleDensity(150.0, 50.0, 50.0, Settings);
	const float SmoothCellBoundaryDensity = FSVoxelGenerator::SampleDensity(101.0, 150.0, 150.0, Settings);
	const float OverlapCarveWinsDensity = FSVoxelGenerator::SampleDensity(100.0, 50.0, 50.0, Settings);
	const float RemovedCellDensity = FSVoxelGenerator::SampleDensity(50.0, 50.0, -50.0, Settings);
	const float HardCubeCellDensity = FSVoxelGenerator::SampleDensity(250.0, 50.0, -50.0, Settings);
	const float UnmodifiedDensity = FSVoxelGenerator::SampleDensity(250.0, 250.0, 50.0, Settings);

	TestTrue(TEXT("A smooth soil edit must add positive density inside its cell"), PlacedCellDensity > 0.0f);
	TestTrue(TEXT("The rounded smooth edit must remain continuous near its cell boundary"), SmoothCellBoundaryDensity > 0.0f);
	TestTrue(TEXT("A carved cell must remain air even where it overlaps a neighbouring smooth addition"), OverlapCarveWinsDensity < 0.0f);
	TestTrue(TEXT("An air edit must carve negative density out of solid terrain"), RemovedCellDensity < 0.0f);
	TestTrue(TEXT("A hard-cube occupancy marker must not carve an otherwise solid terrain sample"), HardCubeCellDensity > 0.0f);
	TestTrue(TEXT("Samples outside edited cells must preserve the procedural density field"), UnmodifiedDensity < 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothVoxelTransvoxelEditedMeshTrianglesTest,
	"SmoothVoxel.Transvoxel.BlockEditsProduceValidTrianglesAndNormals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothVoxelTransvoxelEditedMeshTrianglesTest::RunTest(const FString& Parameters)
{
	FSVoxelGenerationSettings Settings;
	Settings.BaseHeight = 250.0f;
	Settings.HeightAmplitude = 0.0f;
	Settings.DetailAmplitude = 0.0f;
	Settings.NoiseScale = 0.0f;
	Settings.DetailScale = 0.0f;
	Settings.BaseVoxelSize = 50.0f;
	Settings.NormalSampleStep = 25.0f;
	Settings.BottomDepth = 1000.0f;
	Settings.bEnableCaves = false;

	TSharedPtr<FSVoxelBlockEditMap, ESPMode::ThreadSafe> Edits =
		MakeShared<FSVoxelBlockEditMap, ESPMode::ThreadSafe>();
	Edits->Add(FIntVector(2, 2, 5), int8(1)); // Smooth soil addition above terrain.
	Edits->Add(FIntVector(4, 2, 4), int8(2)); // Hard cube cavity intersecting the surface.
	Settings.BlockEdits = Edits;

	TSharedPtr<FSVoxelMinedNodeSet, ESPMode::ThreadSafe> FirstMinedNodes =
		MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>();
	FirstMinedNodes->Add(FIntVector(2, 2, 4)); // Stable node at (100, 100, 200).
	Settings.MinedNodes = FirstMinedNodes;

	const float MinedNodeDensity =
		FSVoxelGenerator::SampleDensity(100.0, 100.0, 200.0, Settings);
	TestTrue(TEXT("A completed terrain hit must turn the selected lattice sample into air"),
		MinedNodeDensity < 0.0f);

	FSVoxelMeshData Mesh;
	FSVoxelTransvoxelMesher::Generate(Settings, 16, 50.0f, 0, Mesh);
	TestTrue(TEXT("Edited terrain fixture must produce a nonempty surface"),
		Mesh.Vertices.Num() > 0 && Mesh.Triangles.Num() > 0);
	TestEqual(TEXT("Triangle indices must be emitted in triples"), Mesh.Triangles.Num() % 3, 0);
	TestEqual(TEXT("Each edited vertex must have a normal"), Mesh.Normals.Num(), Mesh.Vertices.Num());
	TestEqual(TEXT("Each edited vertex must have a UV"), Mesh.UV0.Num(), Mesh.Vertices.Num());

	const auto HasConsistentGeometry = [](const FSVoxelMeshData& Candidate)
	{
		if (Candidate.Vertices.Num() == 0 ||
			Candidate.Triangles.Num() == 0 ||
			Candidate.Triangles.Num() % 3 != 0 ||
			Candidate.Normals.Num() != Candidate.Vertices.Num())
		{
			return false;
		}
		for (int32 Triangle = 0; Triangle < Candidate.Triangles.Num(); Triangle += 3)
		{
			const int32 A = Candidate.Triangles[Triangle];
			const int32 B = Candidate.Triangles[Triangle + 1];
			const int32 C = Candidate.Triangles[Triangle + 2];
			if (!Candidate.Vertices.IsValidIndex(A) ||
				!Candidate.Vertices.IsValidIndex(B) ||
				!Candidate.Vertices.IsValidIndex(C) ||
				A == B || B == C || C == A)
			{
				return false;
			}

			const FVector Cross = FVector::CrossProduct(
				Candidate.Vertices[B] - Candidate.Vertices[A],
				Candidate.Vertices[C] - Candidate.Vertices[A]);
			if (!FMath::IsFinite(Cross.X) ||
				!FMath::IsFinite(Cross.Y) ||
				!FMath::IsFinite(Cross.Z) ||
				Cross.SizeSquared() <= SMALL_NUMBER)
			{
				return false;
			}

			const FVector ExpectedOutward = (
				Candidate.Normals[A] +
				Candidate.Normals[B] +
				Candidate.Normals[C]).GetSafeNormal();
			if (ExpectedOutward.SizeSquared() > SMALL_NUMBER &&
				FVector::DotProduct(Cross, ExpectedOutward) > 0.001f)
			{
				return false;
			}
		}
		return true;
	};

	TestTrue(TEXT("First mining edit must have finite-area triangles with consistent winding"),
		HasConsistentGeometry(Mesh));

	// Two completed strikes in a row are separate grid nodes, not -1/-2 states
	// on a fractional hit point. Rebuild each snapshot and inspect every face.
	TSharedPtr<FSVoxelMinedNodeSet, ESPMode::ThreadSafe> StageTwoNodes =
		MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>(*FirstMinedNodes);
	StageTwoNodes->Add(FIntVector(3, 2, 4));
	FSVoxelGenerationSettings StageTwoSettings = Settings;
	StageTwoSettings.MinedNodes = StageTwoNodes;
	FSVoxelMeshData StageTwoMesh;
	FSVoxelTransvoxelMesher::Generate(StageTwoSettings, 16, 50.0f, 0, StageTwoMesh);
	TestTrue(
		TEXT("Second mining edit must not introduce back-face-culled or collapsed triangles"),
		HasConsistentGeometry(StageTwoMesh));

	TSharedPtr<FSVoxelMinedNodeSet, ESPMode::ThreadSafe> StageFiveNodes =
		MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>(*StageTwoNodes);
	StageFiveNodes->Add(FIntVector(4, 2, 4));
	StageFiveNodes->Add(FIntVector(5, 2, 4));
	StageFiveNodes->Add(FIntVector(6, 2, 4));
	FSVoxelGenerationSettings StageFiveSettings = Settings;
	StageFiveSettings.MinedNodes = StageFiveNodes;
	FSVoxelMeshData StageFiveMesh;
	FSVoxelTransvoxelMesher::Generate(StageFiveSettings, 16, 50.0f, 0, StageFiveMesh);
	TestTrue(
		TEXT("Repeated mining edits must not accumulate back-face-culled or collapsed triangles"),
		HasConsistentGeometry(StageFiveMesh));

	// Regression for a tunnel that breaks through the terrain roof exactly at
	// a chunk corner. This makes the edited SDF and transition faces meet at the
	// same place, the worst case for tiny/cull-reversed triangles.
	FSVoxelGenerationSettings RoofSettings;
	// Reproduce the reported sequence: dig a three-cell shaft from the
	// surface, then a 1-wide, 2-high horizontal tunnel, leaving cell Z=14 as
	// one full voxel layer between the tunnel and the sky.
	RoofSettings.BaseHeight = 749.0f;
	RoofSettings.HeightAmplitude = 0.0f;
	RoofSettings.DetailAmplitude = 0.0f;
	RoofSettings.NoiseScale = 0.0f;
	RoofSettings.DetailScale = 0.0f;
	RoofSettings.BaseVoxelSize = 50.0f;
	RoofSettings.NormalSampleStep = 25.0f;
	RoofSettings.BottomDepth = 2000.0f;
	RoofSettings.bEnableCaves = false;
	RoofSettings.IsoLevel = 0.0f;
	TSharedPtr<FSVoxelMinedNodeSet, ESPMode::ThreadSafe> RoofMinedNodes =
		MakeShared<FSVoxelMinedNodeSet, ESPMode::ThreadSafe>();
	RoofMinedNodes->Add(FIntVector(0, 0, 12));
	RoofMinedNodes->Add(FIntVector(0, 0, 13));
	RoofMinedNodes->Add(FIntVector(0, 0, 14));
	for (int32 X = 1; X <= 3; ++X)
	{
		RoofMinedNodes->Add(FIntVector(X, 0, 12));
		RoofMinedNodes->Add(FIntVector(X, 0, 13));
	}
	RoofSettings.MinedNodes = RoofMinedNodes;

	const uint8 EditedTransitionMasks[] =
	{
		SVoxelTransitionFace_NegativeX,
		SVoxelTransitionFace_NegativeY,
		static_cast<uint8>(SVoxelTransitionFace_NegativeX | SVoxelTransitionFace_NegativeY),
		static_cast<uint8>(SVoxelTransitionFace_NegativeX |
			SVoxelTransitionFace_NegativeY | SVoxelTransitionFace_PositiveZ)
	};
	for (const uint8 FaceMask : EditedTransitionMasks)
	{
		FSVoxelMeshData EditedTransitionMesh;
		FSVoxelTransvoxelMesher::Generate(
			RoofSettings, 16, 50.0f, FaceMask, EditedTransitionMesh);
		TestTrue(
			TEXT("A one-cell-thick mined roof must generate valid source geometry and winding"),
			HasConsistentGeometry(EditedTransitionMesh));

		const int32 OriginalVertexCount = EditedTransitionMesh.Vertices.Num();
		const int32 OriginalTriangleIndexCount = EditedTransitionMesh.Triangles.Num();
		TMap<FVector, FVector> SourceNormalsByPosition;
		for (int32 VertexIndex = 0; VertexIndex < OriginalVertexCount; ++VertexIndex)
		{
			if (EditedTransitionMesh.Normals.IsValidIndex(VertexIndex))
			{
				SourceNormalsByPosition.Add(
					EditedTransitionMesh.Vertices[VertexIndex],
					EditedTransitionMesh.Normals[VertexIndex]);
			}
		}

		const int32 TwoSidedTriangleCount =
			FSVoxelTransvoxelMesher::AddTwoSidedEditedSurfaceTriangles(
				RoofSettings, EditedTransitionMesh);
		TestTrue(
			TEXT("The one-cell-thick roof fixture must generate edited surface triangles"),
			TwoSidedTriangleCount > 0);
		TestEqual(
			TEXT("Every edited surface triangle must gain exactly one reverse-wound copy"),
			EditedTransitionMesh.Triangles.Num(),
			OriginalTriangleIndexCount + TwoSidedTriangleCount * 3);

		bool bBackfaceNormalsMatchOriginal = true;
		for (int32 VertexIndex = OriginalVertexCount;
			VertexIndex < EditedTransitionMesh.Vertices.Num();
			++VertexIndex)
		{
			const FVector* SourceNormal =
				SourceNormalsByPosition.Find(EditedTransitionMesh.Vertices[VertexIndex]);
			if (!SourceNormal ||
				!EditedTransitionMesh.Normals.IsValidIndex(VertexIndex) ||
				FVector::DotProduct(
					*SourceNormal, EditedTransitionMesh.Normals[VertexIndex]) < 0.999f)
			{
				bBackfaceNormalsMatchOriginal = false;
				break;
			}
		}
		TestTrue(
			TEXT("Reverse-wound roof copies must preserve source normals to avoid inverted lighting"),
			bBackfaceNormalsMatchOriginal);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
