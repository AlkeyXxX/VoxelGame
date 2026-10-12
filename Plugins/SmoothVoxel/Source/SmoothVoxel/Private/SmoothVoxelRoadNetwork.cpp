#include "SmoothVoxelRoadNetwork.h"

#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include <cmath>
#include <queue>
#include <vector>

ASmoothVoxelRoadNetwork::ASmoothVoxelRoadNetwork()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadNetworkMesh"));
    RootComponent = Mesh;
    Mesh->bUseAsyncCooking = false;
    Mesh->bUseComplexAsSimpleCollision = true;
    Mesh->SetCollisionObjectType(ECC_WorldStatic);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(true);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoadAsset(
        TEXT("/Game/StarterContent/Materials/M_Ground_Gravel.M_Ground_Gravel"));
    if (RoadAsset.Succeeded())
    {
        FallbackRoadMaterial = RoadAsset.Object;
    }

    // Engine primitive meshes + Starter Content materials keep POIs self-contained.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(
        TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodAsset(
        TEXT("/Game/StarterContent/Materials/M_Wood_Oak.M_Wood_Oak"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> PineAsset(
        TEXT("/Game/StarterContent/Materials/M_Wood_Pine.M_Wood_Pine"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> DarkWoodAsset(
        TEXT("/Game/StarterContent/Materials/M_Wood_Walnut_Worn.M_Wood_Walnut_Worn"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> StoneAsset(
        TEXT("/Game/StarterContent/Materials/M_Rock_Slate.M_Rock_Slate"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SandstoneAsset(
        TEXT("/Game/StarterContent/Materials/M_Rock_Sandstone.M_Rock_Sandstone"));

    const auto CreatePOIInstances = [this](
        UHierarchicalInstancedStaticMeshComponent*& Component,
        const TCHAR* ComponentName,
        UStaticMesh* StaticMesh,
        UMaterialInterface* Material)
    {
        Component = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(ComponentName);
        Component->SetupAttachment(Mesh);
        if (StaticMesh) Component->SetStaticMesh(StaticMesh);
        if (Material) Component->SetMaterial(0, Material);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
    };

    CreatePOIInstances(POIWoodCubes, TEXT("POIWoodCubes"),
        CubeAsset.Succeeded() ? CubeAsset.Object : nullptr,
        WoodAsset.Succeeded() ? WoodAsset.Object : FallbackRoadMaterial);
    CreatePOIInstances(POIStoneCubes, TEXT("POIStoneCubes"),
        CubeAsset.Succeeded() ? CubeAsset.Object : nullptr,
        StoneAsset.Succeeded() ? StoneAsset.Object : FallbackRoadMaterial);
    CreatePOIInstances(POIDarkCubes, TEXT("POIDarkCubes"),
        CubeAsset.Succeeded() ? CubeAsset.Object : nullptr,
        DarkWoodAsset.Succeeded() ? DarkWoodAsset.Object : FallbackRoadMaterial);
    CreatePOIInstances(POIRoofCones, TEXT("POIRoofCones"),
        ConeAsset.Succeeded() ? ConeAsset.Object : nullptr,
        PineAsset.Succeeded() ? PineAsset.Object : FallbackRoadMaterial);
    CreatePOIInstances(POIWoodCylinders, TEXT("POIWoodCylinders"),
        CylinderAsset.Succeeded() ? CylinderAsset.Object : nullptr,
        PineAsset.Succeeded() ? PineAsset.Object : FallbackRoadMaterial);
    CreatePOIInstances(POIStoneCylinders, TEXT("POIStoneCylinders"),
        CylinderAsset.Succeeded() ? CylinderAsset.Object : nullptr,
        SandstoneAsset.Succeeded() ? SandstoneAsset.Object : FallbackRoadMaterial);
    CreatePOIInstances(POIStoneSpheres, TEXT("POIStoneSpheres"),
        SphereAsset.Succeeded() ? SphereAsset.Object : nullptr,
        StoneAsset.Succeeded() ? StoneAsset.Object : FallbackRoadMaterial);
}

void ASmoothVoxelRoadNetwork::ConfigureRoadNetwork(
    const FVector& WorldCenter,
    float WorldSize,
    float SiteSpacing,
    float RoadWidth,
    float SampleSpacing,
    float SurfaceOffset,
    UMaterialInterface* InRoadMaterial,
    const FSVoxelGenerationSettings& GenerationSettings,
    bool bEnableCollision)
{
    check(IsInGameThread());

    ActiveRoadMaterial = InRoadMaterial
        ? InRoadMaterial
        : (FallbackRoadMaterial ? FallbackRoadMaterial : UMaterial::GetDefaultMaterial(MD_Surface));

    SetActorLocation(FVector(WorldCenter.X, WorldCenter.Y, 0.0f));
    SetActorHiddenInGame(false);
    SetActorEnableCollision(bEnableCollision);
    if (Mesh)
    {
        Mesh->SetMaterial(0, ActiveRoadMaterial);
    }

    BuildRoadMesh(
        WorldCenter,
        WorldSize,
        SiteSpacing,
        RoadWidth,
        SampleSpacing,
        SurfaceOffset,
        GenerationSettings,
        bEnableCollision);
}

void ASmoothVoxelRoadNetwork::BuildRoadMesh(
    const FVector& WorldCenter,
    float WorldSize,
    float SiteSpacing,
    float RoadWidth,
    float SampleSpacing,
    float SurfaceOffset,
    const FSVoxelGenerationSettings& GenerationSettings,
    bool bEnableCollision)
{
    if (!Mesh)
    {
        return;
    }

    Mesh->ClearAllMeshSections();
    if (POIWoodCubes) POIWoodCubes->ClearInstances();
    if (POIStoneCubes) POIStoneCubes->ClearInstances();
    if (POIDarkCubes) POIDarkCubes->ClearInstances();
    if (POIRoofCones) POIRoofCones->ClearInstances();
    if (POIWoodCylinders) POIWoodCylinders->ClearInstances();
    if (POIStoneCylinders) POIStoneCylinders->ClearInstances();
    if (POIStoneSpheres) POIStoneSpheres->ClearInstances();

    const double SafeWorldSize = FMath::Max(100.0, std::abs(static_cast<double>(WorldSize)));
    const double HalfExtent = SafeWorldSize * 0.5;
    const double Spacing = FMath::Max(20000.0, std::abs(static_cast<double>(SiteSpacing)));
    const double RoadBlockSize = FMath::Max(
        1.0, std::abs(static_cast<double>(GenerationSettings.BaseVoxelSize)));
    const double DirtRoadWidth = RoadBlockSize * 3.0;
    const double OrdinaryRoadWidth = RoadBlockSize * 10.0;
    const double HighwayRoadWidth = RoadBlockSize * 20.0;
    const int32 CityBlockCount = FMath::Clamp(
        FMath::RoundToInt((Spacing * 0.13 / RoadBlockSize) * 0.5) * 2, 46, 74);
    const double CityBlockSize = static_cast<double>(CityBlockCount) * RoadBlockSize;
    const double CityPortDistance = CityBlockSize * 2.0;
    (void)RoadWidth; // Retained for compatibility; road classes now scale with block size.
    const double SafeSampleSpacing = FMath::Clamp(
        std::abs(static_cast<double>(SampleSpacing)), 250.0, 5000.0);
    const double SafeSurfaceOffset = FMath::Clamp(
        static_cast<double>(SurfaceOffset), 2.0, 100.0);

    const double MinX = static_cast<double>(WorldCenter.X) - HalfExtent;
    const double MaxX = static_cast<double>(WorldCenter.X) + HalfExtent;
    const double MinY = static_cast<double>(WorldCenter.Y) - HalfExtent;
    const double MaxY = static_cast<double>(WorldCenter.Y) + HalfExtent;

    // POI candidate sites use an absolute, seed-stable lattice. Roads reuse
    // those exact positions so future structures can sit on the road network.
    const int32 MinGridX = FMath::FloorToInt(MinX / Spacing) - 1;
    const int32 MaxGridX = FMath::CeilToInt(MaxX / Spacing) + 1;
    const int32 MinGridY = FMath::FloorToInt(MinY / Spacing) - 1;
    const int32 MaxGridY = FMath::CeilToInt(MaxY / Spacing) + 1;

    TMap<FIntPoint, FVector2D> Sites;
    Sites.Reserve((MaxGridX - MinGridX + 1) * (MaxGridY - MinGridY + 1));
    for (int32 GridY = MinGridY; GridY <= MaxGridY; ++GridY)
    {
        for (int32 GridX = MinGridX; GridX <= MaxGridX; ++GridX)
        {
            const FVector2D Site = FSVoxelGenerator::SamplePOISitePosition(
                static_cast<double>(GridX) * Spacing,
                static_cast<double>(GridY) * Spacing,
                GenerationSettings);
            if (Site.X < MinX || Site.X > MaxX || Site.Y < MinY || Site.Y > MaxY)
            {
                continue;
            }
            const FIntPoint SiteCell(GridX, GridY);
            float LakeInfluence = 0.0f;
            float LakeLevel = static_cast<float>(GenerationSettings.RiverWaterLevel);
            if (GenerationSettings.bEnableLakes)
            {
                FSVoxelGenerator::SampleLakeMaskAndLevel(
                    static_cast<double>(Site.X),
                    static_cast<double>(Site.Y),
                    GenerationSettings,
                    LakeInfluence,
                    LakeLevel);
            }

            // Do not reserve POI gateways on lake surfaces. The surrounding
            // roads can connect to the next viable site instead of dead-ending
            // at a site that cannot host a future structure.
            if (LakeInfluence >= 0.10f)
            {
                continue;
            }

            if (GenerationSettings.bEnablePOIPlateaus &&
                FSVoxelGenerator::SamplePOISuitability(
                    static_cast<double>(Site.X),
                    static_cast<double>(Site.Y),
                    GenerationSettings) <= 0.25f)
            {
                continue;
            }

            Sites.Add(SiteCell, Site);
        }
    }

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    int32 RoadSegmentCount = 0;
    int32 TotalRoadSamples = 0;
    int32 SkippedRouteCount = 0;
    int32 BuiltBridgeCount = 0;
    int32 SkippedUnsafeBridgeCount = 0;
    int32 DirtRoadSegmentCount = 0;
    int32 OrdinaryRoadSegmentCount = 0;
    int32 HighwayRoadSegmentCount = 0;
    // Avoid spawning a second isolated bridge alongside a crossing that already
    // has a complete road approach. Centres are committed only after the whole
    // road segment has passed validation and is actually added to the mesh.
    TArray<FVector2D> BuiltBridgeCenters;
    const double MinimumBridgeCenterSeparation = FMath::Max(
        Spacing * 0.25, RoadBlockSize * 16.0);
    const double MinimumBridgeApproachLength = RoadBlockSize * 5.0;
    // Limit urban river crossings to a couple of deliberate bridge locations per city.
    TMap<FIntPoint, int32> CityBridgeCrossingCounts;
    const int32 SamplesPerRoad = FMath::Max(
        2, FMath::CeilToInt(static_cast<float>(Spacing / SafeSampleSpacing)));
    // Reserve room for the hierarchy, urban blocks, POI driveways, and rural links.
    const int32 ExpectedSamples = FMath::Clamp(
        SamplesPerRoad * Sites.Num() * 8,
        0, 300000);
    Vertices.Reserve(ExpectedSamples * 2);
    Triangles.Reserve(ExpectedSamples * 6);
    Normals.Reserve(ExpectedSamples * 2);
    UV0.Reserve(ExpectedSamples * 2);
    VertexColors.Reserve(ExpectedSamples * 2);
    Tangents.Reserve(ExpectedSamples * 2);

    const FLinearColor RoadVertexColor(0.38f, 0.30f, 0.21f, 1.0f);
    const auto SmoothStep = [](double Edge0, double Edge1, double Value)
    {
        const double T = FMath::Clamp(
            (Value - Edge0) / FMath::Max(0.000001, Edge1 - Edge0), 0.0, 1.0);
        return T * T * (3.0 - 2.0 * T);
    };

    // Route on a deterministic world-aligned navigation grid instead of drawing
    // a Bezier directly between neighbouring POIs. The cache is shared by all
    // routes, so each coarse terrain sample is evaluated only once per generation.
    const double RouteGridStep = FMath::Clamp(
        FMath::Max(SafeSampleSpacing * 4.0, 2000.0), 2000.0, 5000.0);
    const int32 WorldGridMinX = FMath::CeilToInt(MinX / RouteGridStep);
    const int32 WorldGridMaxX = FMath::FloorToInt(MaxX / RouteGridStep);
    const int32 WorldGridMinY = FMath::CeilToInt(MinY / RouteGridStep);
    const int32 WorldGridMaxY = FMath::FloorToInt(MaxY / RouteGridStep);
    const double MaxRouteGrade = 0.30; // Preferred maximum for ordinary road segments.
    struct FRoadGridSample
    {
        double Height = 0.0;
        float RiverInfluence = 0.0f;
        float RiverSignedField = 0.0f;
        float RiverBankInfluence = 0.0f;
        float LakeInfluence = 0.0f;
        ESVoxelBiome Biome = ESVoxelBiome::Plains;
        bool bAllowed = true;
        bool bWideRoadAllowed = true;
    };

    TMap<FIntPoint, FRoadGridSample> RouteGridCache;
    RouteGridCache.Reserve(FMath::Clamp(Sites.Num() * 128, 8192, 160000));

    // Retain wide-road paths in XY for short POI driveways and rural cross-links.
    TArray<TArray<FVector2D>> AccessRoadPaths;
    TArray<TArray<FVector2D>> OrdinaryRoadPaths;
    AccessRoadPaths.Reserve(Sites.Num() * 2);
    OrdinaryRoadPaths.Reserve(Sites.Num());

    auto GetRouteGridSample = [&](const FIntPoint& GridCell) -> FRoadGridSample&
    {
        if (FRoadGridSample* Existing = RouteGridCache.Find(GridCell))
        {
            return *Existing;
        }

        FRoadGridSample Sample;
        const double X = static_cast<double>(GridCell.X) * RouteGridStep;
        const double Y = static_cast<double>(GridCell.Y) * RouteGridStep;
        Sample.Height = FSVoxelGenerator::SampleSurfaceHeight(X, Y, GenerationSettings);
        if (GenerationSettings.bEnableRivers)
        {
            FSVoxelGenerator::SampleRiverMaskAndField(
                X, Y, GenerationSettings,
                Sample.RiverInfluence,
                Sample.RiverSignedField,
                &Sample.RiverBankInfluence);
        }
        bool bLakeClearanceAllowed = true;
        if (GenerationSettings.bEnableLakes)
        {
            float LakeWaterLevel = static_cast<float>(GenerationSettings.RiverWaterLevel);
            FSVoxelGenerator::SampleLakeMaskAndLevel(
                X, Y, GenerationSettings, Sample.LakeInfluence, LakeWaterLevel);
            if (Sample.LakeInfluence < 0.10f)
            {
                // Dilate lake clearance by three base blocks so ordinary road
                // centre lines do not scrape the shoreline. Diagonal probes make
                // the buffer less dependent on the orientation of the lake edge.
                const double LakeDirections[8][2] =
                {
                    { 1.0, 0.0 }, { -1.0, 0.0 }, { 0.0, 1.0 }, { 0.0, -1.0 },
                    { 0.70710678, 0.70710678 }, { 0.70710678, -0.70710678 },
                    { -0.70710678, 0.70710678 }, { -0.70710678, -0.70710678 }
                };
                const double ClearanceDistance =
                    RoadBlockSize * 3.0 + OrdinaryRoadWidth * 0.5;
                for (const auto& Direction : LakeDirections)
                {
                    float NearbyLakeInfluence = 0.0f;
                    float NearbyWaterLevel = static_cast<float>(GenerationSettings.RiverWaterLevel);
                    FSVoxelGenerator::SampleLakeMaskAndLevel(
                        X + Direction[0] * ClearanceDistance,
                        Y + Direction[1] * ClearanceDistance,
                        GenerationSettings, NearbyLakeInfluence, NearbyWaterLevel);
                    if (NearbyLakeInfluence >= 0.10f)
                    {
                        bLakeClearanceAllowed = false;
                        break;
                    }
                }
            }
        }
        Sample.Biome = FSVoxelGenerator::SampleBiome(X, Y, GenerationSettings);

        // Lake safety includes the shoreline mask and a three-block clearance.
        // River channels are handled separately so deliberate bridges remain possible.
        Sample.bAllowed = !GenerationSettings.bEnableLakes || Sample.LakeInfluence < 0.10f;
        Sample.bWideRoadAllowed = Sample.bAllowed && bLakeClearanceAllowed;
        return RouteGridCache.Add(GridCell, Sample);
    };

    auto GetRiverTangent = [&](const FIntPoint& GridCell, FVector2D& OutTangent) -> bool
    {
        OutTangent = FVector2D::ZeroVector;
        if (!GenerationSettings.bEnableRivers)
        {
            return false;
        }

        const float FieldXP = GetRouteGridSample(GridCell + FIntPoint(1, 0)).RiverSignedField;
        const float FieldXM = GetRouteGridSample(GridCell + FIntPoint(-1, 0)).RiverSignedField;
        const float FieldYP = GetRouteGridSample(GridCell + FIntPoint(0, 1)).RiverSignedField;
        const float FieldYM = GetRouteGridSample(GridCell + FIntPoint(0, -1)).RiverSignedField;
        const FVector2D Gradient(FieldXP - FieldXM, FieldYP - FieldYM);
        if (Gradient.SizeSquared() <= 1.0e-10f)
        {
            return false;
        }

        // The signed river field changes across the channel; its perpendicular
        // is the approximate direction along the channel.
        OutTangent = FVector2D(-Gradient.Y, Gradient.X).GetSafeNormal();
        return OutTangent.SizeSquared() > 0.5f;
    };

    auto CountRiverCrossingsOnLine = [&](const FVector2D& A, const FVector2D& B) -> int32
    {
        if (!GenerationSettings.bEnableRivers)
        {
            return 0;
        }
        const FVector2D Chord = B - A;
        const double Length = Chord.Size();
        if (Length < 1.0)
        {
            return 0;
        }
        const FVector2D Direction = Chord / static_cast<float>(Length);
        const int32 Steps = FMath::Max(
            1, FMath::CeilToInt(static_cast<float>(Length / (RouteGridStep * 0.5))));
        bool bWasInsideCrossing = false;
        int32 CrossingCount = 0;
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const FVector2D Point =
                A + Chord * static_cast<float>(static_cast<double>(Step) / Steps);
            const FIntPoint Cell(
                FMath::RoundToInt(Point.X / RouteGridStep),
                FMath::RoundToInt(Point.Y / RouteGridStep));
            const FRoadGridSample Sample = GetRouteGridSample(Cell);
            const bool bInRiverEnvelope =
                Sample.RiverInfluence > 0.02f || Sample.RiverBankInfluence > 0.12f;
            if (!bInRiverEnvelope)
            {
                bWasInsideCrossing = false;
                continue;
            }

            FVector2D RiverTangent = FVector2D::ZeroVector;
            const bool bHasTangent = GetRiverTangent(Cell, RiverTangent);
            const double ParallelAmount = bHasTangent
                ? FMath::Abs(FVector2D::DotProduct(Direction, RiverTangent))
                : 1.0;
            const bool bPerpendicularCrossing = bHasTangent && ParallelAmount < 0.82;
            if (bPerpendicularCrossing && !bWasInsideCrossing)
            {
                ++CrossingCount;
            }
            bWasInsideCrossing = bPerpendicularCrossing;
        }
        return CrossingCount;
    };

    const auto SnapToCardinal = [](const FVector2D& Direction) -> FVector2D
    {
        if (FMath::Abs(Direction.X) >= FMath::Abs(Direction.Y))
        {
            return FVector2D(Direction.X >= 0.0f ? 1.0f : -1.0f, 0.0f);
        }
        return FVector2D(0.0f, Direction.Y >= 0.0f ? 1.0f : -1.0f);
    };

    // Test a direct road segment against cached terrain, lake and river rules.
    // This lets the route simplify A*'s lattice without cutting dangerous corners.
    auto CanTravelStraight = [&](
        const FVector2D& A,
        const FVector2D& B,
        double MaxGrade,
        bool bAllowRiverBankFollowing) -> bool
    {
        const FVector2D Chord = B - A;
        const double Length = Chord.Size();
        if (Length < 1.0)
        {
            const FRoadGridSample Sample = GetRouteGridSample(FIntPoint(
                FMath::RoundToInt(A.X / RouteGridStep),
                FMath::RoundToInt(A.Y / RouteGridStep)));
            return bAllowRiverBankFollowing ? Sample.bAllowed : Sample.bWideRoadAllowed;
        }
        const int32 Steps = FMath::Max(
            1, FMath::CeilToInt(static_cast<float>(Length / (RouteGridStep * 0.5))));
        const FVector2D Direction = Chord / static_cast<float>(Length);
        FIntPoint PreviousCell(
            FMath::RoundToInt(A.X / RouteGridStep),
            FMath::RoundToInt(A.Y / RouteGridStep));
        FRoadGridSample Previous = GetRouteGridSample(PreviousCell);
        if (bAllowRiverBankFollowing ? !Previous.bAllowed : !Previous.bWideRoadAllowed)
        {
            return false;
        }

        for (int32 Step = 1; Step <= Steps; ++Step)
        {
            const FVector2D Point = A + Chord *
                static_cast<float>(static_cast<double>(Step) / Steps);
            const FIntPoint Cell(
                FMath::RoundToInt(Point.X / RouteGridStep),
                FMath::RoundToInt(Point.Y / RouteGridStep));
            const FRoadGridSample Current = GetRouteGridSample(Cell);
            if (bAllowRiverBankFollowing ? !Current.bAllowed : !Current.bWideRoadAllowed)
            {
                return false;
            }

            // Height samples come from grid-cell centres. Divide by the actual
            // distance between those centres rather than the finer probe step,
            // otherwise each cell transition looks like an artificial cliff.
            const double CellDX = static_cast<double>(Cell.X - PreviousCell.X) * RouteGridStep;
            const double CellDY = static_cast<double>(Cell.Y - PreviousCell.Y) * RouteGridStep;
            const double SampleDistance = FMath::Sqrt(CellDX * CellDX + CellDY * CellDY);
            const double Grade = FMath::Abs(Current.Height - Previous.Height) /
                FMath::Max(1.0, SampleDistance);
            const float RiverInfluence = 0.5f *
                (Previous.RiverInfluence + Current.RiverInfluence);
            const float RiverBank = 0.5f *
                (Previous.RiverBankInfluence + Current.RiverBankInfluence);
            bool bHasRiverTangent = false;
            double ParallelAmount = 0.0;
            FVector2D RiverTangent = FVector2D::ZeroVector;
            if (GenerationSettings.bEnableRivers && RiverBank > 0.08f)
            {
                bHasRiverTangent = GetRiverTangent(Cell, RiverTangent);
                if (bHasRiverTangent)
                {
                    ParallelAmount = FMath::Abs(
                        FVector2D::DotProduct(Direction, RiverTangent));
                    const bool bParallelToRiver = ParallelAmount > 0.82;
                    if ((!bAllowRiverBankFollowing &&
                            ((RiverInfluence > 0.10f && bParallelToRiver) ||
                             (RiverBank > 0.38f && ParallelAmount > 0.94))) ||
                        (bAllowRiverBankFollowing &&
                            ((RiverInfluence > 0.35f && bParallelToRiver) ||
                             (RiverBank > 0.72f && ParallelAmount > 0.94))))
                    {
                        return false;
                    }
                }
            }
            const bool bCrossRiver = bHasRiverTangent &&
                RiverBank > 0.12f &&
                (RiverInfluence > 0.03f || RiverBank > 0.20f) &&
                ParallelAmount < 0.82;
            const bool bDiagonalThroughRiver = (RiverInfluence > 0.03f || RiverBank > 0.30f) &&
                FMath::Max(
                    FMath::Abs(static_cast<double>(Direction.X)),
                    FMath::Abs(static_cast<double>(Direction.Y))) < 0.985;
            if (bDiagonalThroughRiver || (bCrossRiver &&
                FMath::Max(
                    FMath::Abs(static_cast<double>(Direction.X)),
                    FMath::Abs(static_cast<double>(Direction.Y))) < 0.985))
            {
                return false;
            }
            if (Grade > (bCrossRiver ? 1.25 : MaxGrade) + 0.03)
            {
                return false;
            }

            PreviousCell = Cell;
            Previous = Current;
        }
        return true;
    };

    struct FRoadOpenNode
    {
        double Score = 0.0;
        int32 Index = INDEX_NONE;
        // std::priority_queue is a max heap by default; reverse the comparison
        // so the node with the lowest A* score is expanded first.
        bool operator<(const FRoadOpenNode& Other) const
        {
            return Score > Other.Score;
        }
    };

    auto FindSafeRoute = [&](const FVector2D& Start, const FVector2D& End,
        TArray<FVector2D>& OutRoute, bool bPreferStraight,
        bool bAllowRiverBankFollowing) -> bool
    {
        OutRoute.Reset();
        const double ChordLength = static_cast<double>((End - Start).Size());
        if (ChordLength < 1000.0)
        {
            return false;
        }

        const FIntPoint StartCell(
            FMath::Clamp(FMath::RoundToInt(Start.X / RouteGridStep), WorldGridMinX, WorldGridMaxX),
            FMath::Clamp(FMath::RoundToInt(Start.Y / RouteGridStep), WorldGridMinY, WorldGridMaxY));
        const FIntPoint GoalCell(
            FMath::Clamp(FMath::RoundToInt(End.X / RouteGridStep), WorldGridMinX, WorldGridMaxX),
            FMath::Clamp(FMath::RoundToInt(End.Y / RouteGridStep), WorldGridMinY, WorldGridMaxY));

        if (StartCell == GoalCell)
        {
            const FRoadGridSample& SiteSample = GetRouteGridSample(StartCell);
            if (bAllowRiverBankFollowing ? !SiteSample.bAllowed : !SiteSample.bWideRoadAllowed)
            {
                return false;
            }
            OutRoute.Add(Start);
            OutRoute.Add(End);
            return true;
        }

        // Never let a road terminate inside a lake or its shoreline mask.
        if ((bAllowRiverBankFollowing
                ? !GetRouteGridSample(StartCell).bAllowed
                : !GetRouteGridSample(StartCell).bWideRoadAllowed) ||
            (bAllowRiverBankFollowing
                ? !GetRouteGridSample(GoalCell).bAllowed
                : !GetRouteGridSample(GoalCell).bWideRoadAllowed))
        {
            return false;
        }

        // First search locally; if a river valley or ridge blocks the corridor,
        // retry with a wider detour. Both searches are bounded by the map window.
        const double PaddingBase = FMath::Clamp(ChordLength * 0.35, 8000.0, 60000.0);
        for (int32 Attempt = 0; Attempt < 4; ++Attempt)
        {
            const double AttemptMaxGrade = Attempt == 0
                ? MaxRouteGrade
                : (Attempt == 1 ? 0.50 : (Attempt == 2 ? 0.70 : 0.85));
            const double Padding = PaddingBase *
                (Attempt == 0 ? 1.0 : (Attempt == 1 ? 2.0 : (Attempt == 2 ? 3.0 : 4.0)));
            const int32 MinGridX = FMath::Max(
                WorldGridMinX, FMath::FloorToInt((FMath::Min(Start.X, End.X) - Padding) / RouteGridStep));
            const int32 MaxGridX = FMath::Min(
                WorldGridMaxX, FMath::CeilToInt((FMath::Max(Start.X, End.X) + Padding) / RouteGridStep));
            const int32 MinGridY = FMath::Max(
                WorldGridMinY, FMath::FloorToInt((FMath::Min(Start.Y, End.Y) - Padding) / RouteGridStep));
            const int32 MaxGridY = FMath::Min(
                WorldGridMaxY, FMath::CeilToInt((FMath::Max(Start.Y, End.Y) + Padding) / RouteGridStep));
            const int32 Width = MaxGridX - MinGridX + 1;
            const int32 Height = MaxGridY - MinGridY + 1;
            if (Width <= 1 || Height <= 1)
            {
                continue;
            }

            auto ToIndex = [MinGridX, MinGridY, Width](const FIntPoint& Cell)
            {
                return (Cell.Y - MinGridY) * Width + (Cell.X - MinGridX);
            };
            auto ToCell = [MinGridX, MinGridY, Width](int32 Index)
            {
                return FIntPoint(MinGridX + Index % Width, MinGridY + Index / Width);
            };

            const int32 NodeCount = Width * Height;
            const int32 StartIndex = ToIndex(StartCell);
            const int32 GoalIndex = ToIndex(GoalCell);
            TArray<double> Distance;
            TArray<int32> Parent;
            TArray<uint8> Closed;
            Distance.Init(TNumericLimits<double>::Max(), NodeCount);
            Parent.Init(INDEX_NONE, NodeCount);
            Closed.Init(0, NodeCount);

            const auto Heuristic = [&](const FIntPoint& A, const FIntPoint& B)
            {
                const int32 DX = FMath::Abs(A.X - B.X);
                const int32 DY = FMath::Abs(A.Y - B.Y);
                const int32 Diagonal = FMath::Min(DX, DY);
                const int32 Straight = DX + DY - 2 * Diagonal;
                return RouteGridStep * (static_cast<double>(Straight) +
                    static_cast<double>(Diagonal) * 1.41421356237);
            };

            std::priority_queue<FRoadOpenNode> Open;
            Distance[StartIndex] = 0.0;
            FRoadOpenNode InitialNode;
            InitialNode.Score = Heuristic(StartCell, GoalCell);
            InitialNode.Index = StartIndex;
            Open.push(InitialNode);

            const int32 Directions[8][2] =
            {
                { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
            };

            while (!Open.empty())
            {
                const FRoadOpenNode CurrentOpen = Open.top();
                Open.pop();
                const int32 CurrentIndex = CurrentOpen.Index;
                if (CurrentIndex < 0 || CurrentIndex >= NodeCount || Closed[CurrentIndex])
                {
                    continue;
                }
                if (CurrentIndex == GoalIndex)
                {
                    break;
                }
                Closed[CurrentIndex] = 1;

                const FIntPoint CurrentCell = ToCell(CurrentIndex);
                // Copy cached values before requesting neighbouring samples: TMap
                // insertions may rehash and invalidate references to its elements.
                const FRoadGridSample Current = GetRouteGridSample(CurrentCell);
                if ((bAllowRiverBankFollowing ? !Current.bAllowed : !Current.bWideRoadAllowed) &&
                    CurrentIndex != StartIndex && CurrentIndex != GoalIndex)
                {
                    continue;
                }

                for (const auto& Direction : Directions)
                {
                    const FIntPoint NextCell(
                        CurrentCell.X + Direction[0],
                        CurrentCell.Y + Direction[1]);
                    if (NextCell.X < MinGridX || NextCell.X > MaxGridX ||
                        NextCell.Y < MinGridY || NextCell.Y > MaxGridY)
                    {
                        continue;
                    }

                    const int32 NextIndex = ToIndex(NextCell);
                    if (Closed[NextIndex])
                    {
                        continue;
                    }

                    const FRoadGridSample Next = GetRouteGridSample(NextCell);
                    if ((bAllowRiverBankFollowing ? !Next.bAllowed : !Next.bWideRoadAllowed) &&
                        NextIndex != GoalIndex)
                    {
                        continue;
                    }

                    const bool bDiagonal = Direction[0] != 0 && Direction[1] != 0;
                    if (bDiagonal)
                    {
                        // Don't squeeze diagonally through a lake-mask corner.
                        const FRoadGridSample SideX =
                            GetRouteGridSample(CurrentCell + FIntPoint(Direction[0], 0));
                        const FRoadGridSample SideY =
                            GetRouteGridSample(CurrentCell + FIntPoint(0, Direction[1]));
                        if ((bAllowRiverBankFollowing
                                ? (!SideX.bAllowed || !SideY.bAllowed)
                                : (!SideX.bWideRoadAllowed || !SideY.bWideRoadAllowed)))
                        {
                            continue;
                        }
                    }

                    const double TravelDistance = RouteGridStep * (bDiagonal ? 1.41421356237 : 1.0);
                    const double Grade = FMath::Abs(Next.Height - Current.Height) /
                        FMath::Max(1.0, TravelDistance);
                    const FVector2D TravelDirection =
                        FVector2D(static_cast<float>(Direction[0]), static_cast<float>(Direction[1])).GetSafeNormal();
                    const float RiverInfluence = 0.5f * (Current.RiverInfluence + Next.RiverInfluence);
                    const float RiverBank = 0.5f * (Current.RiverBankInfluence + Next.RiverBankInfluence);
                    double RiverPenalty = 0.0;
                    double ParallelAmount = 0.0;
                    bool bHasRiverTangent = false;
                    FVector2D RiverTangent = FVector2D::ZeroVector;

                    if (GenerationSettings.bEnableRivers && RiverBank > 0.08f)
                    {
                        bHasRiverTangent = GetRiverTangent(CurrentCell, RiverTangent);
                        if (bHasRiverTangent)
                        {
                            ParallelAmount = FMath::Abs(
                                FVector2D::DotProduct(TravelDirection, RiverTangent));

                            // Keep ordinary roads and highways from following a
                            // river channel. Dirt lanes may run beside the water on
                            // the bank, but still cannot drive longitudinally through
                            // the wet channel itself.
                            const bool bParallelToRiver = ParallelAmount > 0.82;
                            if ((!bAllowRiverBankFollowing &&
                                    ((RiverInfluence > 0.10f && bParallelToRiver) ||
                                     (RiverBank > 0.38f && ParallelAmount > 0.94)) ) ||
                                (bAllowRiverBankFollowing &&
                                    ((RiverInfluence > 0.35f && bParallelToRiver) ||
                                     (RiverBank > 0.72f && ParallelAmount > 0.94))))
                            {
                                continue;
                            }

                            const bool bCrossingAngle = ParallelAmount < 0.82;
                            const double ParallelBankPenalty =
                                bAllowRiverBankFollowing ? 1.3 : 8.0;
                            const double ParallelChannelPenalty =
                                bAllowRiverBankFollowing ? 10.0 : 6.0;
                            RiverPenalty = TravelDistance * static_cast<double>(RiverBank) *
                                (bCrossingAngle ? 0.15 : ParallelBankPenalty);
                            if (RiverInfluence > 0.10f)
                            {
                                RiverPenalty += TravelDistance * RiverInfluence *
                                    (bCrossingAngle ? 0.25 : ParallelChannelPenalty);
                            }
                        }
                        else
                        {
                            RiverPenalty = TravelDistance * static_cast<double>(RiverBank) * 4.0;
                        }
                    }

                    const bool bCrossRiver = bHasRiverTangent &&
                        RiverBank > 0.12f &&
                        (RiverInfluence > 0.03f || RiverBank > 0.20f) &&
                        ParallelAmount < 0.82;
                    // Force cardinal movement through the river corridor even if
                    // the noisy field gives an uncertain local tangent. This keeps
                    // bridge approaches axis-aligned instead of producing diagonal decks.
                    if (bDiagonal &&
                        (bCrossRiver || RiverInfluence > 0.03f || RiverBank > 0.30f))
                    {
                        continue;
                    }
                    // Permit large terrain-height deltas only for a near-perpendicular
                    // river crossing; the resulting path is replaced by a bank-to-bank
                    // bridge deck after routing.
                    const double AllowedGrade = bCrossRiver ? 1.25 : AttemptMaxGrade;
                    if (Grade > AllowedGrade)
                    {
                        continue;
                    }

                    const double CostGrade = bCrossRiver ? FMath::Min(Grade, 0.12) : Grade;

                    // Penalize sharp heading changes so mild hills are crossed with a
                    // steady curve instead of a left-right switchback.
                    double TurnPenalty = 0.0;
                    if (Parent[CurrentIndex] != INDEX_NONE)
                    {
                        const FIntPoint PreviousCell = ToCell(Parent[CurrentIndex]);
                        const FVector2D PreviousDirection(
                            static_cast<float>(CurrentCell.X - PreviousCell.X),
                            static_cast<float>(CurrentCell.Y - PreviousCell.Y));
                        const double DirectionContinuity = FMath::Clamp(
                            static_cast<double>(FVector2D::DotProduct(
                                PreviousDirection.GetSafeNormal(), TravelDirection)),
                            -1.0, 1.0);
                        const double TurnSeverity = 1.0 - DirectionContinuity;
                        TurnPenalty = TravelDistance * TurnSeverity *
                            (bAllowRiverBankFollowing ? 0.30 : 0.75);
                    }

                    double StepCost = TravelDistance *
                        (1.0 + CostGrade * CostGrade * 8.0) + RiverPenalty + TurnPenalty;

                    // Highways favour a direct corridor, but terrain and river
                    // costs can still bend the path around an obstacle.
                    if (bPreferStraight)
                    {
                        const FVector2D Chord = End - Start;
                        const double ChordSizeSquared = Chord.SizeSquared();
                        if (ChordSizeSquared > 1.0)
                        {
                            const FVector2D Candidate(
                                static_cast<double>(NextCell.X) * RouteGridStep,
                                static_cast<double>(NextCell.Y) * RouteGridStep);
                            const double Projection = FMath::Clamp(
                                FVector2D::DotProduct(Candidate - Start, Chord) /
                                    ChordSizeSquared,
                                0.0, 1.0);
                            const double LateralDistance =
                                (Candidate - (Start + Chord * Projection)).Size();
                            const double NormalizedOffset =
                                LateralDistance / FMath::Max(Spacing, RouteGridStep);
                            StepCost += TravelDistance *
                                FMath::Min(4.0, NormalizedOffset * NormalizedOffset * 4.0);
                        }
                    }

                    // Biome is not a road-cost bias: outside of genuine mountain
                    // obstacles, desert, snow and temperate ground are treated equally.
                    if (Current.Biome == ESVoxelBiome::Mountain ||
                        Next.Biome == ESVoxelBiome::Mountain)
                    {
                        StepCost *= 2.5;
                    }

                    const double TentativeDistance = Distance[CurrentIndex] + StepCost;
                    if (TentativeDistance < Distance[NextIndex])
                    {
                        Distance[NextIndex] = TentativeDistance;
                        Parent[NextIndex] = CurrentIndex;
                        FRoadOpenNode NextOpenNode;
                        NextOpenNode.Score = TentativeDistance + Heuristic(NextCell, GoalCell);
                        NextOpenNode.Index = NextIndex;
                        Open.push(NextOpenNode);
                    }
                }
            }

            if (Parent[GoalIndex] == INDEX_NONE)
            {
                continue;
            }

            TArray<FVector2D> Reversed;
            for (int32 Index = GoalIndex; Index != INDEX_NONE; Index = Parent[Index])
            {
                const FIntPoint Cell = ToCell(Index);
                Reversed.Add(FVector2D(
                    static_cast<float>(static_cast<double>(Cell.X) * RouteGridStep),
                    static_cast<float>(static_cast<double>(Cell.Y) * RouteGridStep)));
                if (Index == StartIndex)
                {
                    break;
                }
            }

            if (Reversed.Num() < 2 || Parent[GoalIndex] == INDEX_NONE)
            {
                continue;
            }

            TArray<FVector2D> RawRoute;
            RawRoute.Reserve(Reversed.Num() + 2);
            RawRoute.Add(Start);
            for (int32 PointIndex = Reversed.Num() - 1; PointIndex >= 0; --PointIndex)
            {
                RawRoute.Add(Reversed[PointIndex]);
            }
            RawRoute.Add(End);

            // String-pull multiple grid nodes at once when a safe direct piece
            // exists. This removes short zig-zags but keeps lake and grade detours.
            OutRoute.Reset();
            OutRoute.Reserve(RawRoute.Num());
            int32 RouteIndex = 0;
            OutRoute.Add(RawRoute[0]);
            // Be a little more tolerant when string-pulling a direct piece over
            // rolling terrain, which avoids small zig-zags around harmless height noise.
            const double ShortcutGradeLimit = Attempt == 0 ? 0.38 : AttemptMaxGrade;
            constexpr int32 MaxShortcutLookAhead = 24;
            while (RouteIndex < RawRoute.Num() - 1)
            {
                int32 BestNextIndex = RouteIndex + 1;
                const int32 Farthest = FMath::Min(
                    RawRoute.Num() - 1, RouteIndex + MaxShortcutLookAhead);
                for (int32 Candidate = Farthest; Candidate >= RouteIndex + 2; --Candidate)
                {
                    if (CanTravelStraight(
                        RawRoute[RouteIndex], RawRoute[Candidate],
                        ShortcutGradeLimit, bAllowRiverBankFollowing))
                    {
                        BestNextIndex = Candidate;
                        break;
                    }
                }
                OutRoute.Add(RawRoute[BestNextIndex]);
                RouteIndex = BestNextIndex;
            }
            return OutRoute.Num() >= 2;
        }

        return false;
    };

    const auto BuildSegment = [&](
        const FIntPoint& GridCell,
        const FIntPoint& OtherGridCell,
        const FVector2D& Start,
        const FVector2D& End,
        double FullRoadWidth,
        bool bPreferStraight,
        bool bRecordAsNetworkRoad,
        bool bForceGridLine)
    {
        const double SegmentHalfRoadWidth =
            FMath::Max(100.0, std::abs(FullRoadWidth) * 0.5);
        const FVector2D Chord = End - Start;
        const double ChordLength = static_cast<double>(Chord.Size());
        if (ChordLength < 1000.0)
        {
            return;
        }

        TArray<FVector2D> RoutePoints;
        const bool bAllowRiverBankFollowing = FullRoadWidth <= DirtRoadWidth + 1.0;
        int32 PendingCityBridgeCrossings = 0;
        if (bForceGridLine)
        {
            // City roads remain grid-aligned, but may not pass over a steep valley
            // unless the later bridge pass can create a safe bank-to-bank deck.
            const double GridStreetGradeLimit = bForceGridLine ? 0.70 : MaxRouteGrade;
            if (!CanTravelStraight(Start, End, GridStreetGradeLimit, bAllowRiverBankFollowing))
            {
                ++SkippedRouteCount;
                return;
            }
            if (GenerationSettings.bEnableRivers && GridCell == OtherGridCell &&
                Sites.Contains(GridCell))
            {
                PendingCityBridgeCrossings = CountRiverCrossingsOnLine(Start, End);
                const int32* ExistingCountPtr = CityBridgeCrossingCounts.Find(GridCell);
                const int32 ExistingCount = ExistingCountPtr ? *ExistingCountPtr : 0;
                if (ExistingCount + PendingCityBridgeCrossings > 2)
                {
                    ++SkippedRouteCount;
                    return;
                }
            }
            RoutePoints.Add(Start);
            RoutePoints.Add(End);
        }
        else if (!FindSafeRoute(
            Start, End, RoutePoints, bPreferStraight, bAllowRiverBankFollowing))
        {
            ++SkippedRouteCount;
            UE_LOG(LogTemp, Verbose,
                TEXT("SmoothVoxel roads: could not route from site (%d,%d) to (%d,%d)."),
                GridCell.X, GridCell.Y, OtherGridCell.X, OtherGridCell.Y);
            return;
        }

        // Round corners into shallow curves. City-grid segments bypass this so
        // terrain adaptation cannot introduce diagonal streets inside a city.
        if (!bForceGridLine && RoutePoints.Num() >= 3)
        {
            TArray<FVector2D> RoundedRoute;
            RoundedRoute.Reserve(RoutePoints.Num() * 2);
            RoundedRoute.Add(RoutePoints[0]);
            for (int32 CornerIndex = 1; CornerIndex < RoutePoints.Num() - 1; ++CornerIndex)
            {
                const FVector2D& A = RoutePoints[CornerIndex - 1];
                const FVector2D& Corner = RoutePoints[CornerIndex];
                const FVector2D& C = RoutePoints[CornerIndex + 1];
                const FVector2D Incoming = (Corner - A).GetSafeNormal();
                const FVector2D Outgoing = (C - Corner).GetSafeNormal();
                const double InLength = FVector2D::Distance(A, Corner);
                const double OutLength = FVector2D::Distance(Corner, C);
                if (InLength < 1.0 || OutLength < 1.0 ||
                    FVector2D::DotProduct(Incoming, Outgoing) > 0.985)
                {
                    RoundedRoute.Add(Corner);
                    continue;
                }

                const double Radius = FMath::Min(
                    FMath::Max(FullRoadWidth * 2.0, RouteGridStep * 1.25),
                    FMath::Min(InLength, OutLength) * 0.45);
                if (Radius < 50.0)
                {
                    RoundedRoute.Add(Corner);
                    continue;
                }
                const FVector2D Entry = Corner - Incoming * static_cast<float>(Radius);
                const FVector2D Exit = Corner + Outgoing * static_cast<float>(Radius);
                TArray<FVector2D> Arc;
                Arc.Reserve(9);
                Arc.Add(Entry);
                for (int32 CurveStep = 1; CurveStep <= 8; ++CurveStep)
                {
                    const double T = static_cast<double>(CurveStep) / 8.0;
                    const double U = 1.0 - T;
                    Arc.Add(Entry * static_cast<float>(U * U) +
                        Corner * static_cast<float>(2.0 * U * T) +
                        Exit * static_cast<float>(T * T));
                }

                bool bSafe = CanTravelStraight(
                    RoundedRoute.Last(), Arc[0], 0.70, bAllowRiverBankFollowing);
                for (int32 ArcIndex = 1; bSafe && ArcIndex < Arc.Num(); ++ArcIndex)
                {
                    bSafe = CanTravelStraight(
                        Arc[ArcIndex - 1], Arc[ArcIndex], 0.70, bAllowRiverBankFollowing);
                }
                if (bSafe)
                {
                    RoundedRoute.Append(Arc);
                }
                else
                {
                    RoundedRoute.Add(Corner);
                }
            }
            RoundedRoute.Add(RoutePoints.Last());
            bool bRoundedPathSafe = true;
            for (int32 Index = 1; Index < RoundedRoute.Num(); ++Index)
            {
                if (!CanTravelStraight(
                    RoundedRoute[Index - 1], RoundedRoute[Index],
                    0.70, bAllowRiverBankFollowing))
                {
                    bRoundedPathSafe = false;
                    break;
                }
            }
            if (bRoundedPathSafe)
            {
                RoutePoints = MoveTemp(RoundedRoute);
            }
        }

        TArray<FVector2D> Path;
        TArray<double> GroundHeights;
        TArray<double> RoadLifts;
        Path.Add(RoutePoints[0]);
        for (int32 RouteIndex = 1; RouteIndex < RoutePoints.Num(); ++RouteIndex)
        {
            const FVector2D SegmentStart = RoutePoints[RouteIndex - 1];
            const FVector2D SegmentEnd = RoutePoints[RouteIndex];
            const FVector2D Segment = SegmentEnd - SegmentStart;
            const double SegmentLength = static_cast<double>(Segment.Size());
            if (SegmentLength < 1.0)
            {
                continue;
            }

            const int32 SegmentSamples = FMath::Max(
                1, FMath::CeilToInt(static_cast<float>(SegmentLength / SafeSampleSpacing)));
            for (int32 SampleIndex = 1; SampleIndex <= SegmentSamples; ++SampleIndex)
            {
                const double Alpha = static_cast<double>(SampleIndex) / static_cast<double>(SegmentSamples);
                const FVector2D Point(
                    static_cast<float>(FMath::Clamp(
                        SegmentStart.X + Segment.X * Alpha, MinX, MaxX)),
                    static_cast<float>(FMath::Clamp(
                        SegmentStart.Y + Segment.Y * Alpha, MinY, MaxY)));
                if (FVector2D::DistSquared(Path.Last(), Point) > 0.01f)
                {
                    Path.Add(Point);
                }
            }
        }

        if (Path.Num() < 2)
        {
            return;
        }

        // The A* navigation grid is intentionally coarse for large worlds. Verify
        // the final centreline at finer intervals so an interpolated road cannot
        // slip between grid nodes and graze a lake. The clearance includes the
        // road half-width, leaving at least three blocks between road edge and shore.
        if (GenerationSettings.bEnableLakes &&
            FullRoadWidth > DirtRoadWidth + 1.0)
        {
            const double LakeClearanceRadius =
                RoadBlockSize * 3.0 + SegmentHalfRoadWidth;
            const double LakeCheckStep = FMath::Max(50.0, RoadBlockSize * 2.0);
            const double LakeDirections[8][2] =
            {
                { 1.0, 0.0 }, { -1.0, 0.0 }, { 0.0, 1.0 }, { 0.0, -1.0 },
                { 0.70710678, 0.70710678 }, { 0.70710678, -0.70710678 },
                { -0.70710678, 0.70710678 }, { -0.70710678, -0.70710678 }
            };
            for (int32 SegmentIndex = 1; SegmentIndex < Path.Num(); ++SegmentIndex)
            {
                const FVector2D A = Path[SegmentIndex - 1];
                const FVector2D B = Path[SegmentIndex];
                const FVector2D LakeCheckChord = B - A;
                const int32 CheckSteps = FMath::Max(
                    1, FMath::CeilToInt(static_cast<float>(LakeCheckChord.Size() / LakeCheckStep)));
                for (int32 CheckIndex = 0; CheckIndex <= CheckSteps; ++CheckIndex)
                {
                    const FVector2D Point = A + LakeCheckChord *
                        static_cast<float>(static_cast<double>(CheckIndex) / CheckSteps);
                    float CentreLakeInfluence = 0.0f;
                    float CentreLakeLevel = static_cast<float>(GenerationSettings.RiverWaterLevel);
                    FSVoxelGenerator::SampleLakeMaskAndLevel(
                        Point.X, Point.Y, GenerationSettings,
                        CentreLakeInfluence, CentreLakeLevel);
                    if (CentreLakeInfluence >= 0.10f)
                    {
                        ++SkippedRouteCount;
                        return;
                    }

                    for (const auto& Direction : LakeDirections)
                    {
                        float NearbyLakeInfluence = 0.0f;
                        float NearbyLakeLevel = static_cast<float>(GenerationSettings.RiverWaterLevel);
                        FSVoxelGenerator::SampleLakeMaskAndLevel(
                            Point.X + Direction[0] * LakeClearanceRadius,
                            Point.Y + Direction[1] * LakeClearanceRadius,
                            GenerationSettings, NearbyLakeInfluence, NearbyLakeLevel);
                        if (NearbyLakeInfluence >= 0.10f)
                        {
                            ++SkippedRouteCount;
                            return;
                        }
                    }
                }
            }
        }

        int32 SampleCount = Path.Num() - 1;
        GroundHeights.Reserve(Path.Num());
        RoadLifts.SetNumZeroed(Path.Num());

        TArray<float> RiverInfluences;
        TArray<float> RiverBankInfluences;
        TArray<uint8> bBridgeCrossing;
        TArray<FVector2D> BridgeCentersInThisSegment;
        int32 BridgesInThisSegment = 0;
        RiverInfluences.SetNumZeroed(Path.Num());
        RiverBankInfluences.SetNumZeroed(Path.Num());
        bBridgeCrossing.SetNumZeroed(Path.Num());

        for (int32 SampleIndex = 0; SampleIndex < Path.Num(); ++SampleIndex)
        {
            const FVector2D& Point = Path[SampleIndex];
            const double Ground = FSVoxelGenerator::SampleSurfaceHeight(
                static_cast<double>(Point.X),
                static_cast<double>(Point.Y),
                GenerationSettings);
            GroundHeights.Add(Ground);

            if (!GenerationSettings.bEnableRivers)
            {
                continue;
            }

            float RiverField = 0.0f;
            FSVoxelGenerator::SampleRiverMaskAndField(
                static_cast<double>(Point.X),
                static_cast<double>(Point.Y),
                GenerationSettings,
                RiverInfluences[SampleIndex],
                RiverField,
                &RiverBankInfluences[SampleIndex]);

            // Identify the bank-to-bank corridor only if the road crosses the
            // local river direction. This avoids elevating segments that run
            // along the channel.
            if (RiverBankInfluences[SampleIndex] < 0.12f)
            {
                continue;
            }

            // Estimate the approach heading across several mesh samples. A
            // one-sample tangent can look diagonal at the rounded road-to-bridge
            // transition and accidentally suppress the entire bridge detection.
            const int32 HeadingRadius = FMath::Clamp(
                FMath::CeilToInt(static_cast<float>(RouteGridStep /
                    FMath::Max(1.0, SafeSampleSpacing))), 2, 6);
            const FVector2D HeadingStart =
                Path[FMath::Max(0, SampleIndex - HeadingRadius)];
            const FVector2D HeadingEnd =
                Path[FMath::Min(SampleCount, SampleIndex + HeadingRadius)];
            const FVector2D RoadDirection = (HeadingEnd - HeadingStart).GetSafeNormal();
            if (RoadDirection.SizeSquared() <= 0.5f)
            {
                continue;
            }

            const double GradientStep = FMath::Max(
                250.0, FMath::Min(RouteGridStep * 0.35, RoadBlockSize * 5.0));
            float FieldXP = 0.0f, FieldXM = 0.0f, FieldYP = 0.0f, FieldYM = 0.0f;
            float UnusedInfluence = 0.0f;
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X + GradientStep, Point.Y, GenerationSettings, UnusedInfluence, FieldXP);
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X - GradientStep, Point.Y, GenerationSettings, UnusedInfluence, FieldXM);
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X, Point.Y + GradientStep, GenerationSettings, UnusedInfluence, FieldYP);
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X, Point.Y - GradientStep, GenerationSettings, UnusedInfluence, FieldYM);
            const FVector2D Gradient(FieldXP - FieldXM, FieldYP - FieldYM);
            if (Gradient.SizeSquared() <= 1.0e-10f)
            {
                continue;
            }

            const FVector2D RiverTangent(-Gradient.Y, Gradient.X);
            const FVector2D NormalizedRiverTangent = RiverTangent.GetSafeNormal();
            const double ParallelAmount = FMath::Abs(
                FVector2D::DotProduct(RoadDirection, NormalizedRiverTangent));
            const bool bAxisAlignedBridgeApproach = FMath::Max(
                FMath::Abs(static_cast<double>(RoadDirection.X)),
                FMath::Abs(static_cast<double>(RoadDirection.Y))) >= 0.985;
            bBridgeCrossing[SampleIndex] =
                bAxisAlignedBridgeApproach &&
                (RiverInfluences[SampleIndex] > 0.02f ||
                 RiverBankInfluences[SampleIndex] > 0.12f) &&
                ParallelAmount < 0.82 ? 1 : 0;
        }

        // Raise the whole short crossing corridor to a smooth bank-to-bank deck,
        // rather than lifting only the wettest centre samples and leaving steep
        // drops just before the far shore.
        int32 CandidateIndex = 0;
        while (CandidateIndex < Path.Num())
        {
            if (!bBridgeCrossing[CandidateIndex])
            {
                ++CandidateIndex;
                continue;
            }

            const int32 RunStart = CandidateIndex;
            while (CandidateIndex + 1 < Path.Num() && bBridgeCrossing[CandidateIndex + 1])
            {
                ++CandidateIndex;
            }
            const int32 RunEnd = CandidateIndex;
            const int32 DeckStart = FMath::Max(0, RunStart - 1);
            const int32 DeckEnd = FMath::Min(Path.Num() - 1, RunEnd + 1);
            const double SpanLength = (Path[DeckEnd] - Path[DeckStart]).Size();
            const double BankHeightDifference = FMath::Abs(
                GroundHeights[DeckEnd] - GroundHeights[DeckStart]);
            const double BridgeApproachGrade = BankHeightDifference /
                FMath::Max(1.0, SpanLength);
            const bool bSpanIsBuildable =
                SpanLength <= 30000.0 &&
                BankHeightDifference <= 9000.0 &&
                BridgeApproachGrade <= 0.45;
            const FVector2D BridgeCenter =
                (Path[DeckStart] + Path[DeckEnd]) * 0.5f;
            const double StartApproachLength =
                (Path[DeckStart] - Path[0]).Size();
            const double EndApproachLength =
                (Path.Last() - Path[DeckEnd]).Size();
            const bool bHasRoadOnBothBanks =
                StartApproachLength >= MinimumBridgeApproachLength &&
                EndApproachLength >= MinimumBridgeApproachLength;

            bool bDuplicateBridgeCenter = false;
            if (bSpanIsBuildable && bHasRoadOnBothBanks)
            {
                for (const FVector2D& ExistingCenter : BuiltBridgeCenters)
                {
                    if (FVector2D::DistSquared(BridgeCenter, ExistingCenter) <
                        FMath::Square(MinimumBridgeCenterSeparation))
                    {
                        bDuplicateBridgeCenter = true;
                        break;
                    }
                }
                for (const FVector2D& ExistingCenter : BridgeCentersInThisSegment)
                {
                    if (FVector2D::DistSquared(BridgeCenter, ExistingCenter) <
                        FMath::Square(MinimumBridgeCenterSeparation))
                    {
                        bDuplicateBridgeCenter = true;
                        break;
                    }
                }
            }

            if (bSpanIsBuildable && bHasRoadOnBothBanks && !bDuplicateBridgeCenter)
            {
                const double MinimumDeckHeight =
                    static_cast<double>(GenerationSettings.RiverWaterLevel) + 150.0;
                const double StartDeckHeight = FMath::Max(
                    GroundHeights[DeckStart], MinimumDeckHeight);
                const double EndDeckHeight = FMath::Max(
                    GroundHeights[DeckEnd], MinimumDeckHeight);

                for (int32 DeckIndex = DeckStart; DeckIndex <= DeckEnd; ++DeckIndex)
                {
                    const double Alpha = DeckEnd == DeckStart ? 0.0 :
                        static_cast<double>(DeckIndex - DeckStart) /
                        static_cast<double>(DeckEnd - DeckStart);
                    const double DeckHeight = FMath::Lerp(
                        StartDeckHeight, EndDeckHeight, Alpha);
                    RoadLifts[DeckIndex] = FMath::Max(
                        RoadLifts[DeckIndex],
                        FMath::Max(0.0, DeckHeight - GroundHeights[DeckIndex]));
                }
                BridgeCentersInThisSegment.Add(BridgeCenter);
                ++BridgesInThisSegment;
            }
            else
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("SmoothVoxel roads: omitted river crossing (buildable=%d, both-bank approach=%d, duplicate=%d, span %.0f cm, bank delta %.0f cm)."),
                    bSpanIsBuildable ? 1 : 0,
                    bHasRoadOnBothBanks ? 1 : 0,
                    bDuplicateBridgeCenter ? 1 : 0,
                    SpanLength, BankHeightDifference);
                ++SkippedUnsafeBridgeCount;
                RoadLifts[RunStart] = -1.0;
            }
            ++CandidateIndex;
        }

        // Do not render an unsafe span as a road which simply disappears into
        // the channel. A later network edge may connect through a safer crossing.
        for (const double Lift : RoadLifts)
        {
            if (Lift < 0.0)
            {
                return;
            }
        }

        // A missed bridge classification must never let a road descend into the
        // actual riverbed and climb out again. Skip that route rather than draw
        // a drivable-looking ribbon through deep water.
        for (int32 SampleIndex = 0; SampleIndex < Path.Num(); ++SampleIndex)
        {
            const bool bInRiverCut =
                (RiverInfluences[SampleIndex] > 0.05f ||
                 RiverBankInfluences[SampleIndex] > 0.50f) &&
                GroundHeights[SampleIndex] <
                    static_cast<double>(GenerationSettings.RiverWaterLevel) + RoadBlockSize * 3.0;
            if (bInRiverCut && RoadLifts[SampleIndex] < 1.0)
            {
                ++SkippedUnsafeBridgeCount;
                ++SkippedRouteCount;
                UE_LOG(LogTemp, Verbose,
                    TEXT("SmoothVoxel roads: skipped a route that entered a river channel without a bridge."));
                return;
            }
        }
        const bool bIsOrdinaryRoad =
            FMath::IsNearlyEqual(FullRoadWidth, OrdinaryRoadWidth, 1.0);
        if (bIsOrdinaryRoad && GenerationSettings.bEnableRivers &&
            BridgesInThisSegment == 0 && Path.Num() > 2)
        {
            const double RiverSetbackDistance = RoadBlockSize * 4.5;
            const auto EstimateRiverDistance = [&](const FVector2D& Point, float EndpointInfluence)
                -> double
            {
                if (EndpointInfluence > 0.02f)
                {
                    return 0.0;
                }
                const double Directions[8][2] =
                {
                    { 1.0, 0.0 }, { -1.0, 0.0 }, { 0.0, 1.0 }, { 0.0, -1.0 },
                    { 0.7071, 0.7071 }, { 0.7071, -0.7071 },
                    { -0.7071, 0.7071 }, { -0.7071, -0.7071 }
                };
                for (int32 BlockStep = 1; BlockStep <= 5; ++BlockStep)
                {
                    for (const auto& Direction : Directions)
                    {
                        const float Influence = FSVoxelGenerator::SampleRiverInfluence(
                            Point.X + Direction[0] * RoadBlockSize * BlockStep,
                            Point.Y + Direction[1] * RoadBlockSize * BlockStep,
                            GenerationSettings);
                        if (Influence > 0.02f)
                        {
                            return RoadBlockSize * BlockStep;
                        }
                    }
                }
                return TNumericLimits<double>::Max();
            };

            bool bCouldNotMeetRiverSetback = false;
            auto TrimRiverEnd = [&](bool bTrimStart)
            {
                int32 EndpointIndex = bTrimStart ? 0 : Path.Num() - 1;
                const double RiverDistance = EstimateRiverDistance(
                    Path[EndpointIndex], RiverInfluences[EndpointIndex]);
                if (RiverDistance > RiverSetbackDistance)
                {
                    return;
                }

                const bool bEndpointStartsWet = RiverInfluences[EndpointIndex] > 0.02f;
                // For a wet endpoint, reach dry terrain first, then measure the
                // full setback from that bank. A dry endpoint only needs the
                // remaining distance to make a roughly 4.5-block gap.
                const double RequiredBacktrack = bEndpointStartsWet
                    ? RiverSetbackDistance
                    : FMath::Max(0.0, RiverSetbackDistance - RiverDistance);
                bool bReachedDryTerrain = !bEndpointStartsWet;
                double RemovedDistance = 0.0;

                while (Path.Num() > 2)
                {
                    EndpointIndex = bTrimStart ? 0 : Path.Num() - 1;
                    if (bEndpointStartsWet && !bReachedDryTerrain &&
                        RiverInfluences[EndpointIndex] <= 0.02f)
                    {
                        bReachedDryTerrain = true;
                        RemovedDistance = 0.0;
                    }
                    if (bReachedDryTerrain && RemovedDistance >= RequiredBacktrack)
                    {
                        break;
                    }

                    const int32 AdjacentIndex = bTrimStart ? 1 : Path.Num() - 2;
                    const double SegmentLength = FVector2D::Distance(
                        Path[EndpointIndex], Path[AdjacentIndex]);
                    const double RemainingDistance = FMath::Max(
                        0.0, RequiredBacktrack - RemovedDistance);

                    // Interpolate the endpoint when the mesh sampling step is
                    // larger than the setback, instead of cutting off a full extra block row.
                    if (!bEndpointStartsWet && SegmentLength > RemainingDistance + 1.0)
                    {
                        const FVector2D TowardRoad =
                            (Path[AdjacentIndex] - Path[EndpointIndex]).GetSafeNormal();
                        const FVector2D NewEndpoint = Path[EndpointIndex] +
                            TowardRoad * static_cast<float>(RemainingDistance);
                        Path[EndpointIndex] = NewEndpoint;
                        GroundHeights[EndpointIndex] = FSVoxelGenerator::SampleSurfaceHeight(
                            NewEndpoint.X, NewEndpoint.Y, GenerationSettings);
                        RoadLifts[EndpointIndex] = 0.0;
                        float NewRiverInfluence = 0.0f;
                        float NewRiverField = 0.0f;
                        float NewRiverBankInfluence = 0.0f;
                        FSVoxelGenerator::SampleRiverMaskAndField(
                            NewEndpoint.X, NewEndpoint.Y, GenerationSettings,
                            NewRiverInfluence, NewRiverField, &NewRiverBankInfluence);
                        RiverInfluences[EndpointIndex] = NewRiverInfluence;
                        RiverBankInfluences[EndpointIndex] = NewRiverBankInfluence;
                        bBridgeCrossing[EndpointIndex] = 0;
                        break;
                    }

                    if (bReachedDryTerrain)
                    {
                        RemovedDistance += SegmentLength;
                    }
                    if (bTrimStart)
                    {
                        Path.RemoveAt(0, 1, false);
                        GroundHeights.RemoveAt(0, 1, false);
                        RoadLifts.RemoveAt(0, 1, false);
                        RiverInfluences.RemoveAt(0, 1, false);
                        RiverBankInfluences.RemoveAt(0, 1, false);
                        bBridgeCrossing.RemoveAt(0, 1, false);
                    }
                    else
                    {
                        Path.RemoveAt(EndpointIndex, 1, false);
                        GroundHeights.RemoveAt(EndpointIndex, 1, false);
                        RoadLifts.RemoveAt(EndpointIndex, 1, false);
                        RiverInfluences.RemoveAt(EndpointIndex, 1, false);
                        RiverBankInfluences.RemoveAt(EndpointIndex, 1, false);
                        bBridgeCrossing.RemoveAt(EndpointIndex, 1, false);
                    }
                }
                if (Path.Num() <= 2 &&
                    (!bReachedDryTerrain || RemovedDistance + 1.0 < RequiredBacktrack))
                {
                    bCouldNotMeetRiverSetback = true;
                }
            };
            TrimRiverEnd(true);
            TrimRiverEnd(false);
            if (bCouldNotMeetRiverSetback)
            {
                ++SkippedRouteCount;
                UE_LOG(LogTemp, Verbose,
                    TEXT("SmoothVoxel roads: dropped a short ordinary-road stub that could not maintain river setback."));
                return;
            }
            SampleCount = Path.Num() - 1;
            if (SampleCount < 1)
            {
                return;
            }
        }

        if (bForceGridLine && PendingCityBridgeCrossings > 0)
        {
            // Do not emit an unbridged road ribbon over a river. Every classified
            // crossing must have yielded a safe deck in the bridge pass above.
            if (BridgesInThisSegment < PendingCityBridgeCrossings)
            {
                ++SkippedRouteCount;
                return;
            }
            CityBridgeCrossingCounts.FindOrAdd(GridCell) += PendingCityBridgeCrossings;
        }

        BuiltBridgeCount += BridgesInThisSegment;
        BuiltBridgeCenters.Append(BridgeCentersInThisSegment);

        if (bRecordAsNetworkRoad)
        {
            AccessRoadPaths.Add(Path);
            if (!bForceGridLine &&
                FMath::IsNearlyEqual(FullRoadWidth, OrdinaryRoadWidth, 1.0))
            {
                OrdinaryRoadPaths.Add(Path);
            }
        }

        const int32 FirstVertex = Vertices.Num();
        double TravelledDistance = 0.0;
        for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
        {
            const FVector2D& Point = Path[SampleIndex];
            const FVector2D Before = Path[FMath::Max(0, SampleIndex - 1)];
            const FVector2D After = Path[FMath::Min(SampleCount, SampleIndex + 1)];
            FVector2D Tangent2D = After - Before;
            if (Tangent2D.SizeSquared() < 0.001f)
            {
                // Duplicate route samples can occur where separately sampled
                // A* segments meet. Fall back to the nearest non-zero local
                // direction instead of relying on the removed chord variable.
                for (int32 Radius = 1; Radius <= SampleCount; ++Radius)
                {
                    const int32 BackIndex = SampleIndex - Radius;
                    const int32 ForwardIndex = SampleIndex + Radius;
                    if (BackIndex >= 0 && ForwardIndex <= SampleCount)
                    {
                        Tangent2D = Path[ForwardIndex] - Path[BackIndex];
                    }
                    else if (ForwardIndex <= SampleCount)
                    {
                        Tangent2D = Path[ForwardIndex] - Point;
                    }
                    else if (BackIndex >= 0)
                    {
                        Tangent2D = Point - Path[BackIndex];
                    }

                    if (Tangent2D.SizeSquared() >= 0.001f)
                    {
                        break;
                    }
                }
            }
            const float TangentLengthSquared = Tangent2D.SizeSquared();
            if (TangentLengthSquared > 0.001f)
            {
                Tangent2D *= FMath::InvSqrt(TangentLengthSquared);
            }
            else
            {
                Tangent2D = FVector2D(1.0f, 0.0f);
            }
            const FVector2D Perp(-Tangent2D.Y, Tangent2D.X);

            const FVector2D LeftXY = Point + Perp * static_cast<float>(SegmentHalfRoadWidth);
            const FVector2D RightXY = Point - Perp * static_cast<float>(SegmentHalfRoadWidth);

            const double Lift = RoadLifts[SampleIndex];
            const double CenterGround = GroundHeights[SampleIndex];
            const double TerrainClearance = FMath::Max(
                SafeSurfaceOffset,
                static_cast<double>(GenerationSettings.BaseVoxelSize) * 0.50);

            // Keep the carriageway level side-to-side so vehicles do not roll
            // with the hillside. Shoulders/embankments will later fill the edge gaps.
            const double RoadDeckZ = CenterGround + Lift + TerrainClearance;
            const double LeftZ = RoadDeckZ;
            const double RightZ = RoadDeckZ;

            const double DistanceBefore = static_cast<double>((Point - Before).Size());
            const double DistanceAfter = static_cast<double>((After - Point).Size());
            if (SampleIndex > 0)
            {
                TravelledDistance += DistanceBefore;
            }
            const double LengthForGradient = FMath::Max(
                1.0, DistanceBefore + DistanceAfter);
            const double DHAlong = SampleIndex == 0 || SampleIndex == SampleCount
                ? 0.0
                : (GroundHeights[SampleIndex + 1] + RoadLifts[SampleIndex + 1] -
                    GroundHeights[SampleIndex - 1] - RoadLifts[SampleIndex - 1]) /
                    LengthForGradient;
            const double DHAcross = (LeftZ - RightZ) / FMath::Max(1.0, SegmentHalfRoadWidth * 2.0);
            const FVector Normal = FVector(
                static_cast<float>(-Tangent2D.X * DHAlong - Perp.X * DHAcross),
                static_cast<float>(-Tangent2D.Y * DHAlong - Perp.Y * DHAcross),
                1.0f).GetSafeNormal();

            const FVector LeftVertex(
                static_cast<float>(LeftXY.X - WorldCenter.X),
                static_cast<float>(LeftXY.Y - WorldCenter.Y),
                static_cast<float>(LeftZ));
            const FVector RightVertex(
                static_cast<float>(RightXY.X - WorldCenter.X),
                static_cast<float>(RightXY.Y - WorldCenter.Y),
                static_cast<float>(RightZ));

            Vertices.Add(LeftVertex);
            Vertices.Add(RightVertex);
            Normals.Add(Normal);
            Normals.Add(Normal);
            UV0.Add(FVector2D(static_cast<float>(TravelledDistance / 1000.0), 0.0f));
            UV0.Add(FVector2D(static_cast<float>(TravelledDistance / 1000.0), 1.0f));
            VertexColors.Add(RoadVertexColor);
            VertexColors.Add(RoadVertexColor);
            Tangents.Add(FProcMeshTangent(FVector(Tangent2D.X, Tangent2D.Y, 0.0f), false));
            Tangents.Add(FProcMeshTangent(FVector(Tangent2D.X, Tangent2D.Y, 0.0f), false));

            if (SampleIndex > 0)
            {
                const int32 PreviousLeft = FirstVertex + (SampleIndex - 1) * 2;
                const int32 PreviousRight = PreviousLeft + 1;
                const int32 CurrentLeft = FirstVertex + SampleIndex * 2;
                const int32 CurrentRight = CurrentLeft + 1;

                // Match the voxel mesher's winding convention: the raw triangle
                // cross product points into the solid. With the upward vertex normals
                // above, reverse the index order so the road is front-facing from above.
                Triangles.Add(PreviousLeft);
                Triangles.Add(CurrentLeft);
                Triangles.Add(PreviousRight);

                Triangles.Add(PreviousRight);
                Triangles.Add(CurrentLeft);
                Triangles.Add(CurrentRight);
            }
        }

        ++RoadSegmentCount;
        if (FullRoadWidth <= DirtRoadWidth + 1.0)
        {
            ++DirtRoadSegmentCount;
        }
        else if (FullRoadWidth >= HighwayRoadWidth - 1.0)
        {
            ++HighwayRoadSegmentCount;
        }
        else
        {
            ++OrdinaryRoadSegmentCount;
        }
        TotalRoadSamples += SampleCount + 1;
    };

    // Classify stable POI sites into major city centres and smaller road hubs.
    // The classification depends only on seed and lattice coordinates.
    const auto StableSiteHash = [&GenerationSettings](const FIntPoint& Cell) -> uint32
    {
        uint32 Hash = static_cast<uint32>(GenerationSettings.Seed);
        Hash ^= static_cast<uint32>(Cell.X) * 0x9E3779B9u;
        Hash ^= static_cast<uint32>(Cell.Y) * 0x85EBCA6Bu;
        Hash ^= Hash >> 16;
        Hash *= 0x7FEB352Du;
        Hash ^= Hash >> 15;
        Hash *= 0x846CA68Bu;
        Hash ^= Hash >> 16;
        return Hash;
    };

    TArray<FIntPoint> SortedSiteCells;
    Sites.GetKeys(SortedSiteCells);
    SortedSiteCells.Sort([&](const FIntPoint& A, const FIntPoint& B)
    {
        const uint32 HashA = StableSiteHash(A);
        const uint32 HashB = StableSiteHash(B);
        return HashA == HashB
            ? (A.X == B.X ? A.Y < B.Y : A.X < B.X)
            : HashA < HashB;
    });

    // Materialize visible POIs at the same deterministic sites used by the road
    // planner. Each green-biome site gets one compact landmark; the road planner
    // below adds direct dirt access spurs for non-hub sites.
    int32 GreenPOICount = 0;
    int32 CabinPOICount = 0;
    int32 CampPOICount = 0;
    int32 RuinPOICount = 0;
    int32 LookoutPOICount = 0;
    for (const FIntPoint& SiteCell : SortedSiteCells)
    {
        const FVector2D* Site = Sites.Find(SiteCell);
        if (!Site) continue;

        const ESVoxelBiome Biome = FSVoxelGenerator::SampleBiome(
            Site->X, Site->Y, GenerationSettings);
        if (Biome != ESVoxelBiome::Forest && Biome != ESVoxelBiome::Plains)
        {
            continue;
        }

        const double GroundZ = FSVoxelGenerator::SampleSurfaceHeight(
            Site->X, Site->Y, GenerationSettings);
        const uint32 SiteHash = StableSiteHash(SiteCell);
        const float Yaw = static_cast<float>((SiteHash >> 8) % 360u);
        const FRotator SiteYaw(0.0f, Yaw, 0.0f);
        const auto PlacePOIPart = [&](
            UHierarchicalInstancedStaticMeshComponent* Component,
            float OffsetX,
            float OffsetY,
            float OffsetZ,
            const FVector& Scale,
            float LocalYaw = 0.0f,
            float Pitch = 0.0f)
        {
            if (!Component || !Component->GetStaticMesh()) return;
            const FVector RotatedOffset = SiteYaw.RotateVector(FVector(OffsetX, OffsetY, 0.0f));
            const FVector LocalLocation(
                static_cast<float>(Site->X - WorldCenter.X) + RotatedOffset.X,
                static_cast<float>(Site->Y - WorldCenter.Y) + RotatedOffset.Y,
                static_cast<float>(GroundZ) + OffsetZ);
            Component->AddInstance(FTransform(
                FRotator(Pitch, Yaw + LocalYaw, 0.0f),
                LocalLocation,
                Scale));
        };

        const uint32 Roll = SiteHash % 100u;
        const bool bForest = Biome == ESVoxelBiome::Forest;
        const bool bBuildCabin = bForest ? Roll < 48u : Roll < 28u;
        const bool bBuildCamp = bForest
            ? (Roll >= 48u && Roll < 73u)
            : (Roll >= 28u && Roll < 48u);
        const bool bBuildRuins = bForest
            ? (Roll >= 73u && Roll < 91u)
            : (Roll >= 48u && Roll < 77u);

        if (bBuildCabin)
        {
            // Timber waystation: foundation, cabin body, pitched roof, door,
            // windows, chimney and a small porch.
            PlacePOIPart(POIStoneCubes, 0, 0, 12, FVector(6.8f, 5.6f, 0.25f));
            PlacePOIPart(POIWoodCubes, 0, 0, 150, FVector(4.4f, 3.6f, 2.9f));
            PlacePOIPart(POIRoofCones, 0, 0, 335, FVector(5.4f, 4.8f, 2.25f));
            PlacePOIPart(POIDarkCubes, 0, -184, 88, FVector(0.75f, 0.16f, 1.75f));
            PlacePOIPart(POIDarkCubes, -124, -186, 190, FVector(0.62f, 0.14f, 0.56f));
            PlacePOIPart(POIDarkCubes, 124, -186, 190, FVector(0.62f, 0.14f, 0.56f));
            PlacePOIPart(POIStoneCylinders, 142, 92, 365, FVector(0.55f, 0.55f, 1.1f));
            PlacePOIPart(POIWoodCubes, 0, -258, 24, FVector(3.8f, 1.5f, 0.24f));
            ++CabinPOICount;
        }
        else if (bBuildCamp)
        {
            // Camps include a small tent, a ring of stones and crossed firewood.
            PlacePOIPart(POIWoodCubes, 190, 125, 12, FVector(3.5f, 2.9f, 0.22f));
            PlacePOIPart(POIRoofCones, 190, 125, 120, FVector(2.8f, 2.45f, 2.35f));
            PlacePOIPart(POIWoodCylinders, 80, 125, 62, FVector(0.23f, 0.23f, 1.25f));
            PlacePOIPart(POIWoodCylinders, 300, 125, 62, FVector(0.23f, 0.23f, 1.25f));
            for (int32 StoneIndex = 0; StoneIndex < 8; ++StoneIndex)
            {
                const float Angle = static_cast<float>(StoneIndex) * 45.0f;
                const FVector2D Offset(
                    FMath::Cos(FMath::DegreesToRadians(Angle)) * 92.0f,
                    FMath::Sin(FMath::DegreesToRadians(Angle)) * 92.0f);
                PlacePOIPart(POIStoneSpheres, Offset.X, Offset.Y, 22,
                    FVector(0.42f, 0.42f, 0.28f));
            }
            PlacePOIPart(POIWoodCylinders, -20, 0, 32,
                FVector(1.65f, 0.17f, 0.17f), 0.0f, 90.0f);
            PlacePOIPart(POIWoodCylinders, 0, -20, 42,
                FVector(1.65f, 0.17f, 0.17f), 90.0f, 90.0f);
            ++CampPOICount;
        }
        else if (bBuildRuins)
        {
            // Overgrown stone shrine with a broken lintel and fallen stones.
            PlacePOIPart(POIStoneCubes, 0, 0, 12, FVector(6.8f, 6.2f, 0.25f));
            PlacePOIPart(POIStoneCubes, -175, -145, 115, FVector(0.78f, 0.78f, 2.3f));
            PlacePOIPart(POIStoneCubes, 175, -145, 115, FVector(0.78f, 0.78f, 2.3f));
            PlacePOIPart(POIStoneCubes, -175, 145, 80, FVector(0.78f, 0.78f, 1.6f));
            PlacePOIPart(POIStoneCubes, 175, 145, 80, FVector(0.78f, 0.78f, 1.6f));
            PlacePOIPart(POIStoneCubes, 0, -145, 250, FVector(4.3f, 0.85f, 0.55f));
            PlacePOIPart(POIStoneCubes, 0, 145, 210, FVector(4.3f, 0.85f, 0.45f));
            PlacePOIPart(POIStoneCubes, 0, 0, 48, FVector(1.55f, 1.55f, 0.8f));
            PlacePOIPart(POIStoneSpheres, -365, 260, 26, FVector(0.7f, 0.7f, 0.42f));
            PlacePOIPart(POIStoneSpheres, 355, 285, 26, FVector(0.55f, 0.55f, 0.35f));
            ++RuinPOICount;
        }
        else
        {
            // Timber lookout tower, elevated platform, guardrails and roof.
            PlacePOIPart(POIStoneCubes, 0, 0, 12, FVector(4.6f, 4.6f, 0.25f));
            PlacePOIPart(POIWoodCubes, -145, -145, 230, FVector(0.36f, 0.36f, 4.6f));
            PlacePOIPart(POIWoodCubes, 145, -145, 230, FVector(0.36f, 0.36f, 4.6f));
            PlacePOIPart(POIWoodCubes, -145, 145, 230, FVector(0.36f, 0.36f, 4.6f));
            PlacePOIPart(POIWoodCubes, 145, 145, 230, FVector(0.36f, 0.36f, 4.6f));
            PlacePOIPart(POIWoodCubes, 0, 0, 465, FVector(4.0f, 4.0f, 0.3f));
            PlacePOIPart(POIWoodCubes, 0, -190, 505, FVector(3.8f, 0.18f, 0.48f));
            PlacePOIPart(POIWoodCubes, 0, 190, 505, FVector(3.8f, 0.18f, 0.48f));
            PlacePOIPart(POIWoodCubes, -190, 0, 505, FVector(0.18f, 3.8f, 0.48f));
            PlacePOIPart(POIWoodCubes, 190, 0, 505, FVector(0.18f, 3.8f, 0.48f));
            PlacePOIPart(POIWoodCubes, 0, 0, 580, FVector(2.5f, 2.5f, 1.5f));
            PlacePOIPart(POIRoofCones, 0, 0, 720, FVector(3.5f, 3.5f, 1.25f));
            ++LookoutPOICount;
        }
        ++GreenPOICount;
    }

    const int32 TargetMajorCityCount = Sites.Num() <= 1
        ? Sites.Num()
        : FMath::Clamp(
            FMath::RoundToInt(FMath::Sqrt(static_cast<float>(Sites.Num())) / 1.5f),
            2,
            FMath::Min(12, Sites.Num()));

    // Reserve major-city road grids for the desert and snowy regions.
    // Alternate the stable candidate lists so neither outer biome monopolizes
    // the highway backbone. The temperate region is served by smaller towns,
    // ordinary roads and rural driveways instead.
    TArray<FIntPoint> CityCandidateCells;
    CityCandidateCells.Reserve(SortedSiteCells.Num());
    TArray<FIntPoint> DesertCityCandidates;
    TArray<FIntPoint> SnowCityCandidates;
    TArray<FIntPoint> OtherCityCandidates;
    const double CityMargin = CityPortDistance + HighwayRoadWidth * 0.5;
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        const FVector2D* Position = Sites.Find(Candidate);
        if (!Position ||
            Position->X - CityMargin < MinX || Position->X + CityMargin > MaxX ||
            Position->Y - CityMargin < MinY || Position->Y + CityMargin > MaxY)
        {
            continue;
        }

        const ESVoxelBiome Biome = FSVoxelGenerator::SampleBiome(
            Position->X, Position->Y, GenerationSettings);
        if (Biome == ESVoxelBiome::Desert)
        {
            DesertCityCandidates.Add(Candidate);
        }
        else if (Biome == ESVoxelBiome::Snow)
        {
            SnowCityCandidates.Add(Candidate);
        }
        else
        {
            OtherCityCandidates.Add(Candidate);
        }
    }

    const int32 OuterCandidateCount = FMath::Max(
        DesertCityCandidates.Num(), SnowCityCandidates.Num());
    for (int32 CandidateIndex = 0; CandidateIndex < OuterCandidateCount; ++CandidateIndex)
    {
        if (DesertCityCandidates.IsValidIndex(CandidateIndex))
        {
            CityCandidateCells.Add(DesertCityCandidates[CandidateIndex]);
        }
        if (SnowCityCandidates.IsValidIndex(CandidateIndex))
        {
            CityCandidateCells.Add(SnowCityCandidates[CandidateIndex]);
        }
    }
    // Fallback candidates are used only if the outer regions cannot supply enough
    // spaced city centres on a small or unusually obstructed map.
    CityCandidateCells.Append(OtherCityCandidates);

    TArray<FIntPoint> MajorCityCells;
    TSet<FIntPoint> MajorCitySet;
    MajorCityCells.Reserve(TargetMajorCityCount);
    MajorCitySet.Reserve(TargetMajorCityCount);
    const double MinimumCitySeparationSquared = FMath::Square(Spacing * 1.25);
    for (const FIntPoint& Candidate : CityCandidateCells)
    {
        if (MajorCityCells.Num() >= TargetMajorCityCount)
        {
            break;
        }
        const FVector2D* Position = Sites.Find(Candidate);
        if (!Position ||
            Position->X - CityMargin < MinX || Position->X + CityMargin > MaxX ||
            Position->Y - CityMargin < MinY || Position->Y + CityMargin > MaxY)
        {
            continue;
        }

        bool bFarEnough = true;
        for (const FIntPoint& ExistingCell : MajorCityCells)
        {
            const FVector2D* ExistingPosition = Sites.Find(ExistingCell);
            if (ExistingPosition &&
                FVector2D::DistSquared(*Position, *ExistingPosition) <
                    MinimumCitySeparationSquared)
            {
                bFarEnough = false;
                break;
            }
        }
        if (bFarEnough)
        {
            MajorCityCells.Add(Candidate);
            MajorCitySet.Add(Candidate);
        }
    }
    // On small maps, fill any remaining city slots while still respecting margins.
    for (const FIntPoint& Candidate : CityCandidateCells)
    {
        if (MajorCityCells.Num() >= TargetMajorCityCount)
        {
            break;
        }
        const FVector2D* Position = Sites.Find(Candidate);
        if (!Position || MajorCitySet.Contains(Candidate) ||
            Position->X - CityMargin < MinX || Position->X + CityMargin > MaxX ||
            Position->Y - CityMargin < MinY || Position->Y + CityMargin > MaxY)
        {
            continue;
        }
        MajorCityCells.Add(Candidate);
        MajorCitySet.Add(Candidate);
    }

    // Small temperate settlements are deliberately more compact than outer-region
    // cities. They still join the ordinary-road tree, but do not become highway hubs.
    TArray<FIntPoint> SmallSettlementCells;
    const int32 DesiredSmallSettlementCount = FMath::Clamp(Sites.Num() / 16, 0, 18);
    const double SmallSettlementSeparationSquared = FMath::Square(Spacing * 1.60);
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        if (SmallSettlementCells.Num() >= DesiredSmallSettlementCount)
        {
            break;
        }
        if (MajorCitySet.Contains(Candidate))
        {
            continue;
        }
        const FVector2D* Position = Sites.Find(Candidate);
        if (!Position ||
            Position->X - CityMargin < MinX || Position->X + CityMargin > MaxX ||
            Position->Y - CityMargin < MinY || Position->Y + CityMargin > MaxY)
        {
            continue;
        }
        const ESVoxelBiome Biome = FSVoxelGenerator::SampleBiome(
            Position->X, Position->Y, GenerationSettings);
        if (Biome != ESVoxelBiome::Forest && Biome != ESVoxelBiome::Plains)
        {
            continue;
        }

        bool bSpacedFromOtherSettlements = true;
        for (const FIntPoint& ExistingCell : MajorCityCells)
        {
            const FVector2D* ExistingPosition = Sites.Find(ExistingCell);
            if (ExistingPosition &&
                FVector2D::DistSquared(*Position, *ExistingPosition) <
                    SmallSettlementSeparationSquared)
            {
                bSpacedFromOtherSettlements = false;
                break;
            }
        }
        for (const FIntPoint& ExistingCell : SmallSettlementCells)
        {
            const FVector2D* ExistingPosition = Sites.Find(ExistingCell);
            if (ExistingPosition &&
                FVector2D::DistSquared(*Position, *ExistingPosition) <
                    SmallSettlementSeparationSquared)
            {
                bSpacedFromOtherSettlements = false;
                break;
            }
        }
        if (bSpacedFromOtherSettlements)
        {
            SmallSettlementCells.Add(Candidate);
        }
    }

    // Explicitly seed the map centre with ordinary-road hubs. A random hub
    // roll alone can leave the core underserved when most of the visible network
    // is already tied into the outer desert/snow city backbone.
    TArray<FIntPoint> CoreTemperateCandidates;
    const FVector2D MapCenter2D(
        static_cast<float>(WorldCenter.X),
        static_cast<float>(WorldCenter.Y));
    const double CoreHubRadius = HalfExtent * 0.48;
    const double CoreHubRadiusSquared = FMath::Square(CoreHubRadius);
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        if (MajorCitySet.Contains(Candidate))
        {
            continue;
        }
        const FVector2D* Position = Sites.Find(Candidate);
        if (!Position ||
            FVector2D::DistSquared(*Position, MapCenter2D) > CoreHubRadiusSquared)
        {
            continue;
        }
        const ESVoxelBiome Biome = FSVoxelGenerator::SampleBiome(
            Position->X, Position->Y, GenerationSettings);
        if (Biome == ESVoxelBiome::Forest || Biome == ESVoxelBiome::Plains)
        {
            CoreTemperateCandidates.Add(Candidate);
        }
    }
    CoreTemperateCandidates.Sort([&](const FIntPoint& A, const FIntPoint& B)
    {
        const FVector2D& PA = Sites.FindChecked(A);
        const FVector2D& PB = Sites.FindChecked(B);
        const double DA = FVector2D::DistSquared(PA, MapCenter2D);
        const double DB = FVector2D::DistSquared(PB, MapCenter2D);
        if (!FMath::IsNearlyEqual(DA, DB, 1.0))
        {
            return DA < DB;
        }
        return StableSiteHash(A) < StableSiteHash(B);
    });

    TArray<FIntPoint> CoreTemperateHubCells;
    const int32 DesiredCoreHubCount = FMath::Clamp(Sites.Num() / 24, 6, 14);
    const double CoreHubSeparationSquared = FMath::Square(Spacing * 0.55);
    for (const FIntPoint& Candidate : CoreTemperateCandidates)
    {
        if (CoreTemperateHubCells.Num() >= DesiredCoreHubCount)
        {
            break;
        }
        const FVector2D& Position = Sites.FindChecked(Candidate);
        bool bFarEnough = true;
        for (const FIntPoint& ExistingCell : CoreTemperateHubCells)
        {
            if (FVector2D::DistSquared(
                    Position, Sites.FindChecked(ExistingCell)) <
                CoreHubSeparationSquared)
            {
                bFarEnough = false;
                break;
            }
        }
        if (bFarEnough)
        {
            CoreTemperateHubCells.Add(Candidate);
        }
    }

    // Guarantee a compact street grid close to the actual map centre instead of
    // allowing hash ordering to place every small town near the temperate edge.
    if (CoreTemperateHubCells.Num() > 0 &&
        !SmallSettlementCells.Contains(CoreTemperateHubCells[0]))
    {
        if (SmallSettlementCells.Num() > 0)
        {
            SmallSettlementCells.Pop(false);
        }
        SmallSettlementCells.Insert(CoreTemperateHubCells[0], 0);
    }

    // City centres lie on the global block lattice, not at seed-jittered site positions.
    TMap<FIntPoint, FVector2D> MajorCityCenters;
    TMap<FIntPoint, double> MajorCityUrbanScale;
    for (const FIntPoint& CityCell : MajorCityCells)
    {
        if (const FVector2D* Position = Sites.Find(CityCell))
        {
            MajorCityCenters.Add(CityCell, FVector2D(
                FMath::RoundToInt(Position->X / RoadBlockSize) * RoadBlockSize,
                FMath::RoundToInt(Position->Y / RoadBlockSize) * RoadBlockSize));

            const ESVoxelBiome CityBiome = FSVoxelGenerator::SampleBiome(
                Position->X, Position->Y, GenerationSettings);
            const bool bOuterBiome = CityBiome == ESVoxelBiome::Desert ||
                CityBiome == ESVoxelBiome::Snow;
            const bool bMegacity = bOuterBiome && StableSiteHash(CityCell) % 100u < 25u;
            const double UrbanScale = bMegacity ? 3.0 :
                ((CityBiome == ESVoxelBiome::Forest || CityBiome == ESVoxelBiome::Plains)
                    ? 0.60 : 1.0);
            MajorCityUrbanScale.Add(CityCell, UrbanScale);
        }
    }

    TMap<FIntPoint, FVector2D> MajorCityAxis;
    TMap<FIntPoint, int32> MajorCityDegree;
    TMap<FIntPoint, TArray<FVector2D>> MajorCityDirections;
    if (MajorCityCells.Num() >= 2)
    {
        // Prim tree connects every major city without creating redundant highway rings.
        TSet<FIntPoint> ConnectedCities;
        ConnectedCities.Add(MajorCityCells[0]);
        while (ConnectedCities.Num() < MajorCityCells.Num())
        {
            double BestDistanceSquared = TNumericLimits<double>::Max();
            FIntPoint BestFrom = FIntPoint::ZeroValue;
            FIntPoint BestTo = FIntPoint::ZeroValue;
            bool bFoundEdge = false;
            for (const FIntPoint& FromCell : MajorCityCells)
            {
                if (!ConnectedCities.Contains(FromCell))
                {
                    continue;
                }
                const FVector2D* From = MajorCityCenters.Find(FromCell);
                if (!From)
                {
                    continue;
                }
                for (const FIntPoint& ToCell : MajorCityCells)
                {
                    if (ConnectedCities.Contains(ToCell))
                    {
                        continue;
                    }
                    const FVector2D* To = MajorCityCenters.Find(ToCell);
                    if (!To)
                    {
                        continue;
                    }
                    const double DistanceSquared = FVector2D::DistSquared(*From, *To);
                    if (DistanceSquared < BestDistanceSquared)
                    {
                        BestDistanceSquared = DistanceSquared;
                        BestFrom = FromCell;
                        BestTo = ToCell;
                        bFoundEdge = true;
                    }
                }
            }
            if (!bFoundEdge)
            {
                break;
            }

            const FVector2D* From = MajorCityCenters.Find(BestFrom);
            const FVector2D* To = MajorCityCenters.Find(BestTo);
            if (From && To)
            {
                const FVector2D Direction = SnapToCardinal(*To - *From);
                const FVector2D ReverseDirection = Direction * -1.0f;
                if (!MajorCityAxis.Contains(BestFrom))
                {
                    MajorCityAxis.Add(BestFrom, Direction);
                }
                if (!MajorCityAxis.Contains(BestTo))
                {
                    MajorCityAxis.Add(BestTo, ReverseDirection);
                }
                ++MajorCityDegree.FindOrAdd(BestFrom);
                ++MajorCityDegree.FindOrAdd(BestTo);
                MajorCityDirections.FindOrAdd(BestFrom).AddUnique(Direction);
                MajorCityDirections.FindOrAdd(BestTo).AddUnique(ReverseDirection);

                // Intercity highways stop at cardinal gates; city approaches are
                // built separately as straight block-grid-aligned streets.
                const FVector2D FromPort = *From +
                    Direction * static_cast<float>(CityPortDistance);
                const FVector2D ToPort = *To +
                    ReverseDirection * static_cast<float>(CityPortDistance);
                BuildSegment(
                    BestFrom, BestTo, FromPort, ToPort,
                    HighwayRoadWidth, true, true, false);
            }
            ConnectedCities.Add(BestTo);
        }
    }

    const auto GetRoadNodePosition = [&](const FIntPoint& Cell) -> FVector2D
    {
        if (const FVector2D* CityCenter = MajorCityCenters.Find(Cell))
        {
            return *CityCenter;
        }
        if (const FVector2D* Site = Sites.Find(Cell))
        {
            return *Site;
        }
        return FVector2D::ZeroVector;
    };

    // Smaller hubs are sparse. Their spanning tree connects them to the
    // intercity backbone with ordinary roads; no fixed all-map rectangular grid.
    TArray<FIntPoint> RoadHubCells;
    TSet<FIntPoint> RoadHubSet;
    const auto AddRoadHub = [&](const FIntPoint& Cell)
    {
        if (Sites.Contains(Cell) && !RoadHubSet.Contains(Cell))
        {
            RoadHubCells.Add(Cell);
            RoadHubSet.Add(Cell);
        }
    };
    // Start the ordinary-road spanning tree inside the temperate core, then
    // connect small towns and the highway cities, so useful central links are
    // generated before the outer network can consume the route budget.
    for (const FIntPoint& CoreCell : CoreTemperateHubCells)
    {
        AddRoadHub(CoreCell);
    }
    for (const FIntPoint& SettlementCell : SmallSettlementCells)
    {
        AddRoadHub(SettlementCell);
    }
    for (const FIntPoint& MajorCityCell : MajorCityCells)
    {
        AddRoadHub(MajorCityCell);
    }
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        if (RoadHubSet.Contains(Candidate))
        {
            continue;
        }
        const uint32 HubRoll = StableSiteHash(Candidate) % 1000u;

        // The same deterministic hub rate in desert, snow and temperate regions
        // keeps ordinary-road density proportional to eligible POI-site density.
        // A moderate rate also spaces hubs far enough apart for longer intercity roads.
        if (HubRoll < 240u)
        {
            RoadHubCells.Add(Candidate);
            RoadHubSet.Add(Candidate);
        }
    }
    const int32 MinimumHubCount = FMath::Min(3, Sites.Num());
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        if (RoadHubCells.Num() >= MinimumHubCount)
        {
            break;
        }
        if (!RoadHubSet.Contains(Candidate))
        {
            RoadHubCells.Add(Candidate);
            RoadHubSet.Add(Candidate);
        }
    }

    if (RoadHubCells.Num() >= 2)
    {
        TSet<FIntPoint> ConnectedHubs;
        ConnectedHubs.Add(RoadHubCells[0]);
        while (ConnectedHubs.Num() < RoadHubCells.Num())
        {
            double BestDistanceSquared = TNumericLimits<double>::Max();
            FIntPoint BestFrom = FIntPoint::ZeroValue;
            FIntPoint BestTo = FIntPoint::ZeroValue;
            bool bFoundEdge = false;
            for (const FIntPoint& FromCell : RoadHubCells)
            {
                if (!ConnectedHubs.Contains(FromCell))
                {
                    continue;
                }
                if (!Sites.Contains(FromCell))
                {
                    continue;
                }
                const FVector2D From = GetRoadNodePosition(FromCell);
                for (const FIntPoint& ToCell : RoadHubCells)
                {
                    if (ConnectedHubs.Contains(ToCell))
                    {
                        continue;
                    }
                    if (!Sites.Contains(ToCell))
                    {
                        continue;
                    }
                    const FVector2D To = GetRoadNodePosition(ToCell);
                    const double DistanceSquared = FVector2D::DistSquared(From, To);
                    if (DistanceSquared < BestDistanceSquared)
                    {
                        BestDistanceSquared = DistanceSquared;
                        BestFrom = FromCell;
                        BestTo = ToCell;
                        bFoundEdge = true;
                    }
                }
            }
            if (!bFoundEdge)
            {
                break;
            }

            // All major centres are connected by the highway tree, so an
            // additional ordinary road between two major cities is unnecessary.
            const bool bBothMajor =
                MajorCitySet.Contains(BestFrom) && MajorCitySet.Contains(BestTo);
            if (!bBothMajor)
            {
                const FVector2D From = GetRoadNodePosition(BestFrom);
                const FVector2D To = GetRoadNodePosition(BestTo);
                FVector2D RouteStart = From;
                FVector2D RouteEnd = To;
                const FVector2D ConnectionDirection = SnapToCardinal(To - From);
                if (MajorCitySet.Contains(BestFrom))
                {
                    RouteStart += ConnectionDirection * static_cast<float>(CityPortDistance);
                    MajorCityDirections.FindOrAdd(BestFrom).AddUnique(ConnectionDirection);
                }
                if (MajorCitySet.Contains(BestTo))
                {
                    RouteEnd -= ConnectionDirection * static_cast<float>(CityPortDistance);
                    MajorCityDirections.FindOrAdd(BestTo).AddUnique(ConnectionDirection * -1.0f);
                }
                if (Sites.Contains(BestFrom) && Sites.Contains(BestTo))
                {
                    BuildSegment(
                        BestFrom, BestTo, RouteStart, RouteEnd,
                        OrdinaryRoadWidth, true, true, false);
                }
            }
            ConnectedHubs.Add(BestTo);
        }
    }

    // Compact 4-by-3 block layouts are generated only around major cities.
    // City centres and block intervals are snapped to the global block grid.
    const auto IsRoadPointInBounds = [&](const FVector2D& Point, double FullWidth)
    {
        const double HalfWidth = FullWidth * 0.5;
        return Point.X - HalfWidth >= MinX && Point.X + HalfWidth <= MaxX &&
            Point.Y - HalfWidth >= MinY && Point.Y + HalfWidth <= MaxY;
    };

    for (const FIntPoint& CityCell : MajorCityCells)
    {
        const FVector2D* City = MajorCityCenters.Find(CityCell);
        if (!City)
        {
            continue;
        }

        const double* SavedUrbanScale = MajorCityUrbanScale.Find(CityCell);
        const double UrbanScale = SavedUrbanScale ? *SavedUrbanScale : 1.0;
        const double LocalCityBlockSize = CityBlockSize * UrbanScale;

        FVector2D Axis(1.0f, 0.0f);
        if (const FVector2D* SavedAxis = MajorCityAxis.Find(CityCell))
        {
            Axis = SnapToCardinal(*SavedAxis);
        }
        const FVector2D Perpendicular(-Axis.Y, Axis.X);

        // Build straight cardinal central streets between the snapped city
        // centre and every highway/ordinary-road port.
        TArray<FVector2D> CityStreetDirections;
        if (const TArray<FVector2D>* CityDirections = MajorCityDirections.Find(CityCell))
        {
            for (const FVector2D& Direction : *CityDirections)
            {
                CityStreetDirections.AddUnique(SnapToCardinal(Direction));
            }
        }
        CityStreetDirections.AddUnique(Axis);
        CityStreetDirections.AddUnique(Axis * -1.0f);
        for (const FVector2D& SideDirection : CityStreetDirections)
        {
            const FVector2D ExtensionEnd =
                *City + SideDirection * static_cast<float>(CityPortDistance);
            if (IsRoadPointInBounds(*City, HighwayRoadWidth) &&
                IsRoadPointInBounds(ExtensionEnd, HighwayRoadWidth))
            {
                BuildSegment(
                    CityCell, CityCell, *City, ExtensionEnd,
                    HighwayRoadWidth, true, true, true);
            }
        }

        // Four streets to each side of the highway plus four cross streets.
        for (int32 Offset = -2; Offset <= 2; ++Offset)
        {
            if (Offset == 0)
            {
                continue;
            }
            const FVector2D OffsetVector = Perpendicular *
                static_cast<float>(static_cast<double>(Offset) * LocalCityBlockSize);
            const FVector2D Start = *City + OffsetVector -
                Axis * static_cast<float>(LocalCityBlockSize * 1.5);
            const FVector2D End = *City + OffsetVector +
                Axis * static_cast<float>(LocalCityBlockSize * 1.5);
            if (IsRoadPointInBounds(Start, OrdinaryRoadWidth) &&
                IsRoadPointInBounds(End, OrdinaryRoadWidth))
            {
                BuildSegment(
                    CityCell, CityCell, Start, End,
                    OrdinaryRoadWidth, false, true, true);
            }
        }

        for (int32 Row = 0; Row < 4; ++Row)
        {
            const double Along = (static_cast<double>(Row) - 1.5) * LocalCityBlockSize;
            const FVector2D OffsetVector = Axis * static_cast<float>(Along);
            const FVector2D Start = *City + OffsetVector -
                Perpendicular * static_cast<float>(LocalCityBlockSize * 2.0);
            const FVector2D End = *City + OffsetVector +
                Perpendicular * static_cast<float>(LocalCityBlockSize * 2.0);
            if (IsRoadPointInBounds(Start, OrdinaryRoadWidth) &&
                IsRoadPointInBounds(End, OrdinaryRoadWidth))
            {
                BuildSegment(
                    CityCell, CityCell, Start, End,
                    OrdinaryRoadWidth, false, true, true);
            }
        }
    }

    // Compact 3-by-3 street grids give a few green-biome hubs the feel of
    // small towns. They use ordinary roads only; no highway spokes or megacity scale.
    for (const FIntPoint& SettlementCell : SmallSettlementCells)
    {
        const FVector2D* Settlement = Sites.Find(SettlementCell);
        if (!Settlement)
        {
            continue;
        }

        const FVector2D Axis(1.0f, 0.0f);
        const FVector2D Perpendicular(0.0f, 1.0f);
        const double LocalBlockSize = CityBlockSize * 0.60;
        for (int32 Offset = -1; Offset <= 1; ++Offset)
        {
            const FVector2D OffsetVector = Perpendicular *
                static_cast<float>(static_cast<double>(Offset) * LocalBlockSize);
            const FVector2D Start = *Settlement + OffsetVector -
                Axis * static_cast<float>(LocalBlockSize * 1.25);
            const FVector2D End = *Settlement + OffsetVector +
                Axis * static_cast<float>(LocalBlockSize * 1.25);
            if (IsRoadPointInBounds(Start, OrdinaryRoadWidth) &&
                IsRoadPointInBounds(End, OrdinaryRoadWidth))
            {
                BuildSegment(
                    SettlementCell, SettlementCell, Start, End,
                    OrdinaryRoadWidth, false, true, true);
            }
        }

        for (int32 Row = -1; Row <= 1; ++Row)
        {
            const FVector2D OffsetVector = Axis *
                static_cast<float>(static_cast<double>(Row) * LocalBlockSize);
            const FVector2D Start = *Settlement + OffsetVector -
                Perpendicular * static_cast<float>(LocalBlockSize * 1.25);
            const FVector2D End = *Settlement + OffsetVector +
                Perpendicular * static_cast<float>(LocalBlockSize * 1.25);
            if (IsRoadPointInBounds(Start, OrdinaryRoadWidth) &&
                IsRoadPointInBounds(End, OrdinaryRoadWidth))
            {
                BuildSegment(
                    SettlementCell, SettlementCell, Start, End,
                    OrdinaryRoadWidth, false, true, true);
            }
        }
    }

    // Rare country lanes connect ordinary roads crosswise, rather than forming
    // a ring around every POI or another rectangular network.
    // Aim for a substantial rural network without letting new dirt lanes cross
    // existing roads or overlap them for long stretches.
    const int32 DesiredRuralConnectorCount = FMath::Min(120, FMath::Max(1, Sites.Num() / 3));
    int32 RuralConnectorsBuilt = 0;
    const double RuralConnectorEndpointSpacing = Spacing * 0.65;
    const double RuralConnectorEndpointSpacingSquared =
        FMath::Square(RuralConnectorEndpointSpacing);
    TSet<uint64> UsedRuralRoadPairs;
    TMap<int32, TArray<FVector2D>> RuralEndpointsByRoad;
    TMap<int32, int32> RuralConnectorCountByRoad;
    const auto IsRuralEndpointTooClose = [&](int32 RoadIndex, const FVector2D& Point)
    {
        const TArray<FVector2D>* ExistingEndpoints = RuralEndpointsByRoad.Find(RoadIndex);
        if (!ExistingEndpoints)
        {
            return false;
        }
        for (const FVector2D& ExistingPoint : *ExistingEndpoints)
        {
            if (FVector2D::DistSquared(Point, ExistingPoint) <
                RuralConnectorEndpointSpacingSquared)
            {
                return true;
            }
        }
        return false;
    };
    const auto RuralLinkIntersectsExistingRoad = [&](const FVector2D& LinkStart,
        const FVector2D& LinkEnd) -> bool
    {
        const FVector2D Link = LinkEnd - LinkStart;
        const double LinkLengthSquared = Link.SizeSquared();
        if (LinkLengthSquared < 1.0)
        {
            return true;
        }

        for (const TArray<FVector2D>& ExistingPath : AccessRoadPaths)
        {
            for (int32 SegmentIndex = 1; SegmentIndex < ExistingPath.Num(); ++SegmentIndex)
            {
                const FVector2D RoadA = ExistingPath[SegmentIndex - 1];
                const FVector2D RoadB = ExistingPath[SegmentIndex];
                const FVector2D RoadSegment = RoadB - RoadA;
                const FVector2D Offset = RoadA - LinkStart;
                const double Denominator =
                    Link.X * RoadSegment.Y - Link.Y * RoadSegment.X;
                if (FMath::Abs(Denominator) > 1.0)
                {
                    const double LinkT =
                        (Offset.X * RoadSegment.Y - Offset.Y * RoadSegment.X) / Denominator;
                    const double RoadT =
                        (Offset.X * Link.Y - Offset.Y * Link.X) / Denominator;
                    // The chosen connector must meet its target roads only at its ends.
                    if (LinkT > 0.06 && LinkT < 0.94 &&
                        RoadT >= 0.0 && RoadT <= 1.0)
                    {
                        return true;
                    }
                }

                // Also reject long near-parallel duplicates that do not intersect exactly.
                for (int32 ProbeIndex = 2; ProbeIndex <= 8; ++ProbeIndex)
                {
                    const double Alpha = static_cast<double>(ProbeIndex) / 10.0;
                    const FVector2D Probe = LinkStart + Link *
                        static_cast<float>(Alpha);
                    const double RoadSegmentLengthSquared =
                        static_cast<double>(RoadSegment.SizeSquared());
                    const double Projection = FMath::Clamp(
                        static_cast<double>(FVector2D::DotProduct(
                            Probe - RoadA, RoadSegment)) /
                            FMath::Max(1.0, RoadSegmentLengthSquared),
                        0.0, 1.0);
                    const FVector2D Closest = RoadA + RoadSegment *
                        static_cast<float>(Projection);
                    if (FVector2D::DistSquared(Probe, Closest) <
                        FMath::Square(OrdinaryRoadWidth * 0.60))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    for (int32 ConnectorIndex = 0;
        ConnectorIndex < DesiredRuralConnectorCount && OrdinaryRoadPaths.Num() >= 2;
        ++ConnectorIndex)
    {
        bool bFoundConnector = false;
        FVector2D BestStart = FVector2D::ZeroVector;
        FVector2D BestEnd = FVector2D::ZeroVector;
        int32 BestPathAIndex = INDEX_NONE;
        int32 BestPathBIndex = INDEX_NONE;
        double BestScore = TNumericLimits<double>::Max();

        for (int32 Attempt = 0; Attempt < 32 && !bFoundConnector; ++Attempt)
        {
            const uint32 PickHash = StableSiteHash(FIntPoint(
                ConnectorIndex * 137 + Attempt * 31 + 17,
                ConnectorIndex * 83 - Attempt * 19 - 11));
            const int32 PathAIndex =
                static_cast<int32>(PickHash % static_cast<uint32>(OrdinaryRoadPaths.Num()));
            if (RuralConnectorCountByRoad.FindRef(PathAIndex) >= 2)
            {
                continue;
            }
            const TArray<FVector2D>& PathA = OrdinaryRoadPaths[PathAIndex];
            if (PathA.Num() < 4 ||
                FVector2D::Distance(PathA[0], PathA.Last()) < Spacing * 0.55)
            {
                continue;
            }
            const int32 IndexA = 1 + static_cast<int32>(
                (PickHash >> 8) % static_cast<uint32>(PathA.Num() - 2));
            const FVector2D TangentA =
                (PathA[IndexA + 1] - PathA[IndexA - 1]).GetSafeNormal();
            if (TangentA.SizeSquared() < 0.5f)
            {
                continue;
            }
            const FVector2D Start = PathA[IndexA];
            if (IsRuralEndpointTooClose(PathAIndex, Start))
            {
                continue;
            }

            for (int32 PathBIndex = 0; PathBIndex < OrdinaryRoadPaths.Num(); ++PathBIndex)
            {
                if (PathBIndex == PathAIndex ||
                    RuralConnectorCountByRoad.FindRef(PathBIndex) >= 2)
                {
                    continue;
                }
                const int32 PairLow = FMath::Min(PathAIndex, PathBIndex);
                const int32 PairHigh = FMath::Max(PathAIndex, PathBIndex);
                const uint64 RuralPairKey =
                    (static_cast<uint64>(static_cast<uint32>(PairLow)) << 32) |
                    static_cast<uint64>(static_cast<uint32>(PairHigh));
                if (UsedRuralRoadPairs.Contains(RuralPairKey))
                {
                    continue;
                }
                const TArray<FVector2D>& PathB = OrdinaryRoadPaths[PathBIndex];
                if (PathB.Num() < 4 ||
                    FVector2D::Distance(PathB[0], PathB.Last()) < Spacing * 0.55)
                {
                    continue;
                }
                const int32 Stride = FMath::Max(1, PathB.Num() / 28);
                for (int32 IndexB = 1; IndexB + 1 < PathB.Num(); IndexB += Stride)
                {
                    const FVector2D End = PathB[IndexB];
                    if (IsRuralEndpointTooClose(PathBIndex, End))
                    {
                        continue;
                    }
                    const FVector2D Link = End - Start;
                    const double LinkLength = Link.Size();
                    if (LinkLength < Spacing * 0.45 || LinkLength > Spacing * 1.2)
                    {
                        continue;
                    }
                    const FVector2D LinkDirection = Link.GetSafeNormal();
                    const double AlignmentA = FMath::Abs(
                        FVector2D::DotProduct(LinkDirection, TangentA));
                    if (AlignmentA > 0.65)
                    {
                        continue;
                    }
                    const FVector2D TangentB =
                        (PathB[IndexB + 1] - PathB[IndexB - 1]).GetSafeNormal();
                    const double AlignmentB = FMath::Abs(
                        FVector2D::DotProduct(LinkDirection, TangentB));
                    if (AlignmentB > 0.65)
                    {
                        continue;
                    }

                    // Score geometry only. Do not pull rural connectors toward
                    // green biomes; use the same link-length and angle rules everywhere.
                    const double Score =
                        std::abs(LinkLength / Spacing - 0.75) +
                        AlignmentA * 0.35 + AlignmentB * 0.35;
                    if (Score < BestScore)
                    {
                        // Do expensive terrain and intersection checks only for candidates
                        // that would improve the current best connector.
                        if (!CanTravelStraight(Start, End, 0.50, true) ||
                            RuralLinkIntersectsExistingRoad(Start, End))
                        {
                            continue;
                        }

                        BestScore = Score;
                        BestStart = Start;
                        BestEnd = End;
                        BestPathAIndex = PathAIndex;
                        BestPathBIndex = PathBIndex;
                        bFoundConnector = true;
                    }
                }
            }
        }

        if (bFoundConnector)
        {
            const int32 SegmentsBeforeConnector = RoadSegmentCount;
            BuildSegment(
                FIntPoint::ZeroValue, FIntPoint::ZeroValue,
                BestStart, BestEnd, DirtRoadWidth, true, true, false);

            // A failed safety/bridge pass does not reserve the pair; a successful
            // link does, and its next link must be far enough along both roads.
            if (RoadSegmentCount > SegmentsBeforeConnector &&
                BestPathAIndex != INDEX_NONE && BestPathBIndex != INDEX_NONE)
            {
                const int32 PairLow = FMath::Min(BestPathAIndex, BestPathBIndex);
                const int32 PairHigh = FMath::Max(BestPathAIndex, BestPathBIndex);
                const uint64 RuralPairKey =
                    (static_cast<uint64>(static_cast<uint32>(PairLow)) << 32) |
                    static_cast<uint64>(static_cast<uint32>(PairHigh));
                UsedRuralRoadPairs.Add(RuralPairKey);
                ++RuralConnectorCountByRoad.FindOrAdd(BestPathAIndex);
                ++RuralConnectorCountByRoad.FindOrAdd(BestPathBIndex);
                RuralEndpointsByRoad.FindOrAdd(BestPathAIndex).Add(BestStart);
                RuralEndpointsByRoad.FindOrAdd(BestPathBIndex).Add(BestEnd);
                ++RuralConnectorsBuilt;
            }
        }
        else
        {
            // Remaining candidates are either unsafe or too close to existing links.
            break;
        }
    }

    // Non-hub POIs get at most one short dirt driveway to the nearest wide road.
    for (const FIntPoint& POICell : SortedSiteCells)
    {
        if (RoadHubSet.Contains(POICell))
        {
            continue;
        }
        const FVector2D* POI = Sites.Find(POICell);
        if (!POI)
        {
            continue;
        }

        double BestDistanceSquared = TNumericLimits<double>::Max();
        FVector2D BestRoadPoint = FVector2D::ZeroVector;
        for (const TArray<FVector2D>& RoadPath : AccessRoadPaths)
        {
            for (int32 PointIndex = 1; PointIndex < RoadPath.Num(); ++PointIndex)
            {
                const FVector2D& A = RoadPath[PointIndex - 1];
                const FVector2D& B = RoadPath[PointIndex];
                const FVector2D Segment = B - A;
                const double SegmentLengthSquared = Segment.SizeSquared();
                if (SegmentLengthSquared <= 1.0)
                {
                    continue;
                }
                const double Projection = FMath::Clamp(
                    FVector2D::DotProduct(*POI - A, Segment) / SegmentLengthSquared,
                    0.0, 1.0);
                const FVector2D CandidatePoint = A + Segment * Projection;
                const double DistanceSquared = FVector2D::DistSquared(*POI, CandidatePoint);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    BestRoadPoint = CandidatePoint;
                }
            }
        }

        if (BestDistanceSquared < TNumericLimits<double>::Max())
        {
            const double DrivewayDistance = std::sqrt(BestDistanceSquared);
            if (DrivewayDistance > OrdinaryRoadWidth * 0.75 &&
                DrivewayDistance <= Spacing * 0.70)
            {
                BuildSegment(
                    POICell, POICell, *POI, BestRoadPoint,
                    DirtRoadWidth, false, false, false);
            }
        }
    }

    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("SmoothVoxel roads: no road segments were generated."));
        return;
    }

    Mesh->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Triangles,
        Normals,
        UV0,
        TArray<FVector2D>(),
        TArray<FVector2D>(),
        TArray<FVector2D>(),
        VertexColors,
        Tangents,
        bEnableCollision);
    Mesh->SetMaterial(0, ActiveRoadMaterial);
    Mesh->SetCollisionEnabled(
        bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Mesh->SetCollisionResponseToAllChannels(bEnableCollision ? ECR_Block : ECR_Ignore);

    int32 TemperateSiteCount = 0;
    int32 DesertSiteCount = 0;
    int32 SnowSiteCount = 0;
    int32 TemperateHubCount = 0;
    int32 DesertHubCount = 0;
    int32 SnowHubCount = 0;
    int32 DesertMajorCityCount = 0;
    int32 SnowMajorCityCount = 0;
    const auto CountBiome = [&](const FVector2D& Position,
        int32& OutTemperate, int32& OutDesert, int32& OutSnow)
    {
        const ESVoxelBiome Biome = FSVoxelGenerator::SampleBiome(
            Position.X, Position.Y, GenerationSettings);
        if (Biome == ESVoxelBiome::Desert)
        {
            ++OutDesert;
        }
        else if (Biome == ESVoxelBiome::Snow)
        {
            ++OutSnow;
        }
        else
        {
            ++OutTemperate;
        }
    };
    for (const auto& SiteEntry : Sites)
    {
        CountBiome(SiteEntry.Value,
            TemperateSiteCount, DesertSiteCount, SnowSiteCount);
    }
    for (const FIntPoint& HubCell : RoadHubCells)
    {
        if (const FVector2D* HubPosition = Sites.Find(HubCell))
        {
            CountBiome(*HubPosition,
                TemperateHubCount, DesertHubCount, SnowHubCount);
        }
    }
    for (const FIntPoint& CityCell : MajorCityCells)
    {
        if (const FVector2D* CityPosition = Sites.Find(CityCell))
        {
            const ESVoxelBiome Biome = FSVoxelGenerator::SampleBiome(
                CityPosition->X, CityPosition->Y, GenerationSettings);
            if (Biome == ESVoxelBiome::Desert)
            {
                ++DesertMajorCityCount;
            }
            else if (Biome == ESVoxelBiome::Snow)
            {
                ++SnowMajorCityCount;
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("SmoothVoxel roads/POIs: %d green POIs (%d cabins, %d camps, %d ruins, %d lookouts); %d major cities (%d desert, %d snow), %d hubs (%d core temperate); %d total segments (%d highway, %d ordinary, %d dirt) from %d site candidates [temperate=%d, desert=%d, snow=%d]; hubs by biome [temperate=%d, desert=%d, snow=%d]; %d small towns, %d rural links, %d bridges; skipped %d routes and %d unsafe bridges."),
        GreenPOICount, CabinPOICount, CampPOICount, RuinPOICount, LookoutPOICount,
        MajorCityCells.Num(), DesertMajorCityCount, SnowMajorCityCount,
        RoadHubCells.Num(), CoreTemperateHubCells.Num(),
        RoadSegmentCount, HighwayRoadSegmentCount, OrdinaryRoadSegmentCount, DirtRoadSegmentCount,
        Sites.Num(), TemperateSiteCount, DesertSiteCount, SnowSiteCount,
        TemperateHubCount, DesertHubCount, SnowHubCount,
        SmallSettlementCells.Num(), RuralConnectorsBuilt, BuiltBridgeCount,
        SkippedRouteCount, SkippedUnsafeBridgeCount);
}
