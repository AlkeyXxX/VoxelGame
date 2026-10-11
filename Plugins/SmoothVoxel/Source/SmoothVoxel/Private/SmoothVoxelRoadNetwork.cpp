#include "SmoothVoxelRoadNetwork.h"

#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

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

    const double SafeWorldSize = FMath::Max(100.0, std::abs(static_cast<double>(WorldSize)));
    const double HalfExtent = SafeWorldSize * 0.5;
    const double Spacing = FMath::Max(20000.0, std::abs(static_cast<double>(SiteSpacing)));
    const double HalfRoadWidth = FMath::Clamp(
        std::abs(static_cast<double>(RoadWidth)) * 0.5, 150.0, 1500.0);
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
            Sites.Add(FIntPoint(GridX, GridY), Site);
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
    const int32 ExpectedSamples = FMath::Clamp(
        FMath::CeilToInt(static_cast<float>(SafeWorldSize / SafeSampleSpacing)) * Sites.Num(),
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

    const auto BuildSegment = [&](const FIntPoint& GridCell, const FVector2D& Start, const FVector2D& End)
    {
        const FVector2D Chord = End - Start;
        const double ChordLength = static_cast<double>(Chord.Size());
        if (ChordLength < 1000.0)
        {
            return;
        }

        const FVector2D Direction = Chord / static_cast<float>(ChordLength);
        const FVector2D Perpendicular(-Direction.Y, Direction.X);

        // A small, deterministic quadratic bend avoids a rigid square-grid look
        // without creating sharp turns or moving the road away from POI centres.
        const uint32 CurveHash =
            static_cast<uint32>(GridCell.X) * 73856093u ^
            static_cast<uint32>(GridCell.Y) * 19349663u ^
            static_cast<uint32>(GenerationSettings.Seed) * 83492791u;
        const double CurveSignal =
            static_cast<double>(CurveHash & 0xFFFFu) / 65535.0 * 2.0 - 1.0;
        const double CurveOffset = FMath::Clamp(
            CurveSignal * ChordLength * 0.055, -5000.0, 5000.0);
        const FVector2D Control = (Start + End) * 0.5f +
            Perpendicular * static_cast<float>(CurveOffset);

        const int32 SampleCount = FMath::Clamp(
            FMath::CeilToInt(static_cast<float>(ChordLength / SafeSampleSpacing)), 2, 512);

        TArray<FVector2D> Path;
        TArray<double> GroundHeights;
        TArray<double> RoadLifts;
        Path.Reserve(SampleCount + 1);
        GroundHeights.Reserve(SampleCount + 1);
        RoadLifts.SetNumZeroed(SampleCount + 1);

        for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
        {
            const double T = static_cast<double>(SampleIndex) / static_cast<double>(SampleCount);
            const double OneMinusT = 1.0 - T;
            const double PX = OneMinusT * OneMinusT * Start.X +
                2.0 * OneMinusT * T * Control.X + T * T * End.X;
            const double PY = OneMinusT * OneMinusT * Start.Y +
                2.0 * OneMinusT * T * Control.Y + T * T * End.Y;
            const FVector2D Point(
                static_cast<float>(FMath::Clamp(PX, MinX, MaxX)),
                static_cast<float>(FMath::Clamp(PY, MinY, MaxY)));
            Path.Add(Point);

            const double Ground = FSVoxelGenerator::SampleSurfaceHeight(
                static_cast<double>(WorldCenter.X) + static_cast<double>(Point.X) - WorldCenter.X,
                static_cast<double>(WorldCenter.Y) + static_cast<double>(Point.Y) - WorldCenter.Y,
                GenerationSettings);
            GroundHeights.Add(Ground);

            // A road crossing a river is lifted above the water rather than
            // following the carved bed. The lift fades smoothly at the banks;
            // dedicated bridge meshes can replace this simple crossing later.
            if (GenerationSettings.bEnableRivers)
            {
                const float RiverInfluence = FMath::Clamp(
                    FSVoxelGenerator::SampleRiverInfluence(
                        static_cast<double>(Point.X),
                        static_cast<double>(Point.Y),
                        GenerationSettings),
                    0.0f, 1.0f);
                const double BridgeBlend = SmoothStep(0.02, 0.75, RiverInfluence);
                const double DeckHeight = static_cast<double>(GenerationSettings.RiverWaterLevel) + 100.0;
                RoadLifts[SampleIndex] = FMath::Max(0.0, DeckHeight - Ground) * BridgeBlend;
            }
        }

        // Spread bridge elevation over a few samples to avoid a sudden vertical
        // step where a road meets the river's sloping banks.
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            TArray<double> SmoothedLifts = RoadLifts;
            for (int32 SampleIndex = 1; SampleIndex < SampleCount; ++SampleIndex)
            {
                SmoothedLifts[SampleIndex] =
                    RoadLifts[SampleIndex - 1] * 0.25 +
                    RoadLifts[SampleIndex] * 0.50 +
                    RoadLifts[SampleIndex + 1] * 0.25;
            }
            RoadLifts = MoveTemp(SmoothedLifts);
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
                Tangent2D = Direction;
            }
            Tangent2D.Normalize();
            const FVector2D Perp(-Tangent2D.Y, Tangent2D.X);

            const FVector2D LeftXY = Point + Perp * static_cast<float>(HalfRoadWidth);
            const FVector2D RightXY = Point - Perp * static_cast<float>(HalfRoadWidth);

            const double LeftGround = FSVoxelGenerator::SampleSurfaceHeight(
                static_cast<double>(LeftXY.X), static_cast<double>(LeftXY.Y), GenerationSettings);
            const double RightGround = FSVoxelGenerator::SampleSurfaceHeight(
                static_cast<double>(RightXY.X), static_cast<double>(RightXY.Y), GenerationSettings);
            const double Lift = RoadLifts[SampleIndex];
            const double RoadCenterHeight = GroundHeights[SampleIndex] + Lift + SafeSurfaceOffset;
            const double LeftZ = LeftGround + Lift + SafeSurfaceOffset;
            const double RightZ = RightGround + Lift + SafeSurfaceOffset;

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
            const double DHAcross = (LeftZ - RightZ) / FMath::Max(1.0, HalfRoadWidth * 2.0);
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

                // Winding points the ribbon normals upward, matching the
                // ProceduralMeshComponent convention used by the voxel world.
                Triangles.Add(PreviousLeft);
                Triangles.Add(PreviousRight);
                Triangles.Add(CurrentLeft);

                Triangles.Add(PreviousRight);
                Triangles.Add(CurrentRight);
                Triangles.Add(CurrentLeft);
            }
        }

        ++RoadSegmentCount;
        TotalRoadSamples += SampleCount + 1;
    };

    for (int32 GridY = MinGridY; GridY <= MaxGridY; ++GridY)
    {
        for (int32 GridX = MinGridX; GridX <= MaxGridX; ++GridX)
        {
            const FVector2D* Site = Sites.Find(FIntPoint(GridX, GridY));
            if (!Site)
            {
                continue;
            }

            if (const FVector2D* EastSite = Sites.Find(FIntPoint(GridX + 1, GridY)))
            {
                BuildSegment(FIntPoint(GridX, GridY), *Site, *EastSite);
            }
            if (const FVector2D* NorthSite = Sites.Find(FIntPoint(GridX, GridY + 1)))
            {
                BuildSegment(FIntPoint(GridX, GridY), *Site, *NorthSite);
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

    UE_LOG(LogTemp, Log,
        TEXT("SmoothVoxel roads: generated %d road segments, %d sites and %d route samples."),
        RoadSegmentCount, Sites.Num(), TotalRoadSamples);
}
