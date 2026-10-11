#include "SmoothVoxelRoadNetwork.h"

#include "ProceduralMeshComponent.h"
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
    const int32 SamplesPerRoad = FMath::Max(
        2, FMath::CeilToInt(static_cast<float>(Spacing / SafeSampleSpacing)));
    const int32 ExpectedSamples = FMath::Clamp(
        SamplesPerRoad * Sites.Num() * 2,
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
    const double MaxRouteGrade = 0.30; // Hard limit: avoid cliff faces and canyon walls.

    struct FRoadGridSample
    {
        double Height = 0.0;
        float RiverInfluence = 0.0f;
        float RiverSignedField = 0.0f;
        float RiverBankInfluence = 0.0f;
        float LakeInfluence = 0.0f;
        ESVoxelBiome Biome = ESVoxelBiome::Plains;
        bool bAllowed = true;
    };

    TMap<FIntPoint, FRoadGridSample> RouteGridCache;
    RouteGridCache.Reserve(8192);

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
        if (GenerationSettings.bEnableLakes)
        {
            float LakeWaterLevel = static_cast<float>(GenerationSettings.RiverWaterLevel);
            FSVoxelGenerator::SampleLakeMaskAndLevel(
                X, Y, GenerationSettings, Sample.LakeInfluence, LakeWaterLevel);
        }
        Sample.Biome = FSVoxelGenerator::SampleBiome(X, Y, GenerationSettings);

        // A road must go around a lake, including its shoreline blend zone.
        // River channels remain candidates only for a near-perpendicular crossing.
        Sample.bAllowed = !GenerationSettings.bEnableLakes || Sample.LakeInfluence < 0.10f;
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
        TArray<FVector2D>& OutRoute) -> bool
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
            if (!SiteSample.bAllowed)
            {
                return false;
            }
            OutRoute.Add(Start);
            OutRoute.Add(End);
            return true;
        }

        // Never let a road terminate inside a lake or its shoreline mask.
        if (!GetRouteGridSample(StartCell).bAllowed ||
            !GetRouteGridSample(GoalCell).bAllowed)
        {
            return false;
        }

        // First search locally; if a river valley or ridge blocks the corridor,
        // retry with a wider detour. Both searches are bounded by the map window.
        const double PaddingBase = FMath::Clamp(ChordLength * 0.35, 8000.0, 30000.0);
        for (int32 Attempt = 0; Attempt < 2; ++Attempt)
        {
            const double Padding = PaddingBase * (Attempt == 0 ? 1.0 : 2.0);
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
                if (!Current.bAllowed && CurrentIndex != StartIndex && CurrentIndex != GoalIndex)
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
                    if (!Next.bAllowed && NextIndex != GoalIndex)
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
                        if (!SideX.bAllowed || !SideY.bAllowed)
                        {
                            continue;
                        }
                    }

                    const double TravelDistance = RouteGridStep * (bDiagonal ? 1.41421356237 : 1.0);
                    const double Grade = FMath::Abs(Next.Height - Current.Height) /
                        FMath::Max(1.0, TravelDistance);
                    if (Grade > MaxRouteGrade)
                    {
                        continue;
                    }

                    const FVector2D TravelDirection =
                        FVector2D(static_cast<float>(Direction[0]), static_cast<float>(Direction[1])).GetSafeNormal();
                    const float RiverInfluence = 0.5f * (Current.RiverInfluence + Next.RiverInfluence);
                    const float RiverBank = 0.5f * (Current.RiverBankInfluence + Next.RiverBankInfluence);
                    double RiverPenalty = 0.0;
                    if (GenerationSettings.bEnableRivers && RiverBank > 0.12f)
                    {
                        FVector2D RiverTangent;
                        if (GetRiverTangent(CurrentCell, RiverTangent))
                        {
                            const double ParallelAmount = FMath::Abs(
                                FVector2D::DotProduct(TravelDirection, RiverTangent));

                            // A road running down a river channel/canyon is never
                            // allowed. Perpendicular crossings are possible but costly.
                            if ((RiverInfluence > 0.10f && ParallelAmount > 0.55) ||
                                (RiverBank > 0.38f && ParallelAmount > 0.68))
                            {
                                continue;
                            }

                            RiverPenalty = TravelDistance * static_cast<double>(RiverBank) *
                                (ParallelAmount > 0.45 ? 18.0 : 5.0);
                            if (RiverInfluence > 0.10f)
                            {
                                RiverPenalty += TravelDistance * (4.0 + RiverInfluence * 10.0);
                            }
                        }
                        else
                        {
                            RiverPenalty = TravelDistance * static_cast<double>(RiverBank) * 8.0;
                        }
                    }

                    double StepCost = TravelDistance * (1.0 + Grade * Grade * 80.0) + RiverPenalty;
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

            OutRoute.Reserve(Reversed.Num() + 2);
            OutRoute.Add(Start);
            for (int32 PointIndex = Reversed.Num() - 2; PointIndex >= 0; --PointIndex)
            {
                OutRoute.Add(Reversed[PointIndex]);
            }
            OutRoute.Add(End);
            return true;
        }

        return false;
    };

    const auto BuildSegment = [&](const FIntPoint& GridCell, const FVector2D& Start, const FVector2D& End)
    {
        const FVector2D Chord = End - Start;
        const double ChordLength = static_cast<double>(Chord.Size());
        if (ChordLength < 1000.0)
        {
            return;
        }

        // Keep future POI footprints clear. Each neighbouring site gets road
        // access near the plateau edge instead of having asphalt run through its centre.
        const double SiteAccessOffset = FMath::Clamp(
            std::abs(static_cast<double>(GenerationSettings.POIPlateauRadius)) +
                std::abs(static_cast<double>(GenerationSettings.POIPlateauBlendDistance)) * 0.60 +
                HalfRoadWidth,
            0.0,
            ChordLength * 0.30);
        const FVector2D ChordDirection = (Chord / static_cast<float>(ChordLength)).GetSafeNormal();
        const FVector2D RouteStart = Start + ChordDirection * static_cast<float>(SiteAccessOffset);
        const FVector2D RouteEnd = End - ChordDirection * static_cast<float>(SiteAccessOffset);

        TArray<FVector2D> RoutePoints;
        if (!FindSafeRoute(RouteStart, RouteEnd, RoutePoints))
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("SmoothVoxel roads: skipped blocked route from site (%d,%d)."),
                GridCell.X, GridCell.Y);
            return;
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
        const int32 SampleCount = Path.Num() - 1;
        GroundHeights.Reserve(Path.Num());
        RoadLifts.SetNumZeroed(Path.Num());

        for (int32 SampleIndex = 0; SampleIndex < Path.Num(); ++SampleIndex)
        {
            const FVector2D& Point = Path[SampleIndex];
            const double Ground = FSVoxelGenerator::SampleSurfaceHeight(
                static_cast<double>(Point.X),
                static_cast<double>(Point.Y),
                GenerationSettings);
            GroundHeights.Add(Ground);

            // Only a route planner-approved, near-perpendicular river crossing
            // is raised to bridge deck height. Lakes are always routed around.
            if (!GenerationSettings.bEnableRivers)
            {
                continue;
            }

            float RiverInfluence = 0.0f;
            float RiverField = 0.0f;
            float RiverBankInfluence = 0.0f;
            FSVoxelGenerator::SampleRiverMaskAndField(
                static_cast<double>(Point.X),
                static_cast<double>(Point.Y),
                GenerationSettings,
                RiverInfluence,
                RiverField,
                &RiverBankInfluence);
            if (RiverInfluence <= 0.02f)
            {
                continue;
            }

            const FVector2D Before = Path[FMath::Max(0, SampleIndex - 1)];
            const FVector2D After = Path[FMath::Min(SampleCount, SampleIndex + 1)];
            FVector2D RoadDirection = (After - Before).GetSafeNormal();
            if (RoadDirection.SizeSquared() <= 0.5f)
            {
                continue;
            }

            const double GradientStep = FMath::Max(RouteGridStep, SafeSampleSpacing * 2.0);
            float FieldXP = 0.0f, FieldXM = 0.0f, FieldYP = 0.0f, FieldYM = 0.0f;
            float UnusedInfluence = 0.0f;
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X + GradientStep, Point.Y, GenerationSettings,
                UnusedInfluence, FieldXP);
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X - GradientStep, Point.Y, GenerationSettings,
                UnusedInfluence, FieldXM);
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X, Point.Y + GradientStep, GenerationSettings,
                UnusedInfluence, FieldYP);
            FSVoxelGenerator::SampleRiverMaskAndField(
                Point.X, Point.Y - GradientStep, GenerationSettings,
                UnusedInfluence, FieldYM);
            const FVector2D Gradient(FieldXP - FieldXM, FieldYP - FieldYM);
            if (Gradient.SizeSquared() <= 1.0e-10f)
            {
                continue;
            }
            const FVector2D RiverTangent(-Gradient.Y, Gradient.X);
            const FVector2D NormalizedRiverTangent = RiverTangent.GetSafeNormal();
            const double ParallelAmount = FMath::Abs(
                FVector2D::DotProduct(RoadDirection, NormalizedRiverTangent));
            if (ParallelAmount < 0.55)
            {
                const double BridgeBlend = SmoothStep(0.02, 0.75, RiverInfluence);
                const double DeckHeight = static_cast<double>(GenerationSettings.RiverWaterLevel) + 100.0;
                RoadLifts[SampleIndex] = FMath::Max(0.0, DeckHeight - Ground) * BridgeBlend;
            }
        }

        // Smooth bridge approaches without creating bridges over lake basins or
        // lifting any route that runs along the channel.
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
            const double CenterGround = GroundHeights[SampleIndex];
            const double CrossSectionCorrection = FMath::Max(
                0.0, CenterGround - (LeftGround + RightGround) * 0.5);
            const double TerrainClearance = FMath::Max(
                SafeSurfaceOffset,
                static_cast<double>(GenerationSettings.BaseVoxelSize) * 0.50);
            const double LeftZ = LeftGround + Lift + TerrainClearance + CrossSectionCorrection;
            const double RightZ = RightGround + Lift + TerrainClearance + CrossSectionCorrection;

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
