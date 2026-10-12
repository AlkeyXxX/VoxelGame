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
    const double RoadBlockSize = FMath::Max(
        1.0, std::abs(static_cast<double>(GenerationSettings.BaseVoxelSize)));
    const double DirtRoadWidth = RoadBlockSize * 3.0;
    const double OrdinaryRoadWidth = RoadBlockSize * 10.0;
    const double HighwayRoadWidth = RoadBlockSize * 20.0;
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
        const double PaddingBase = FMath::Clamp(ChordLength * 0.35, 8000.0, 60000.0);
        for (int32 Attempt = 0; Attempt < 2; ++Attempt)
        {
            const double AttemptMaxGrade = Attempt == 0 ? MaxRouteGrade : 0.70;
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
                    // Permit large terrain-height deltas only for a near-perpendicular
                    // river crossing; the resulting path is replaced by a bank-to-bank
                    // bridge deck after routing.
                    const double AllowedGrade = bCrossRiver ? 1.25 : AttemptMaxGrade;
                    if (Grade > AllowedGrade)
                    {
                        continue;
                    }

                    const double CostGrade = bCrossRiver ? FMath::Min(Grade, 0.12) : Grade;
                    double StepCost = TravelDistance * (1.0 + CostGrade * CostGrade * 100.0) + RiverPenalty;

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
            for (int32 PointIndex = Reversed.Num() - 1; PointIndex >= 0; --PointIndex)
            {
                OutRoute.Add(Reversed[PointIndex]);
            }
            OutRoute.Add(End);
            return true;
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
        bool bRecordAsNetworkRoad)
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
        if (!FindSafeRoute(
            Start, End, RoutePoints, bPreferStraight, bAllowRiverBankFollowing))
        {
            ++SkippedRouteCount;
            UE_LOG(LogTemp, Verbose,
                TEXT("SmoothVoxel roads: could not route from site (%d,%d) to (%d,%d)."),
                GridCell.X, GridCell.Y, OtherGridCell.X, OtherGridCell.Y);
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

        TArray<float> RiverInfluences;
        TArray<float> RiverBankInfluences;
        TArray<uint8> bBridgeCrossing;
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

            const FVector2D Before = Path[FMath::Max(0, SampleIndex - 1)];
            const FVector2D After = Path[FMath::Min(SampleCount, SampleIndex + 1)];
            const FVector2D RoadDirection = (After - Before).GetSafeNormal();
            if (RoadDirection.SizeSquared() <= 0.5f)
            {
                continue;
            }

            const double GradientStep = RouteGridStep;
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
            bBridgeCrossing[SampleIndex] =
                (RiverInfluences[SampleIndex] > 0.02f ||
                 RiverBankInfluences[SampleIndex] > 0.35f) &&
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
            const bool bSpanIsBuildable =
                SpanLength <= 18000.0 && BankHeightDifference <= 6000.0;

            if (bSpanIsBuildable)
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
                ++BridgesInThisSegment;
            }
            else
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("SmoothVoxel roads: omitted an unsafe river crossing (span %.0f cm, bank delta %.0f cm)."),
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
        BuiltBridgeCount += BridgesInThisSegment;

        if (bRecordAsNetworkRoad)
        {
            AccessRoadPaths.Add(Path);
            if (FMath::IsNearlyEqual(FullRoadWidth, OrdinaryRoadWidth, 1.0))
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

    const int32 TargetMajorCityCount = Sites.Num() <= 1
        ? Sites.Num()
        : FMath::Clamp(
            FMath::RoundToInt(FMath::Sqrt(static_cast<float>(Sites.Num())) / 1.5f),
            2,
            FMath::Min(12, Sites.Num()));

    TArray<FIntPoint> MajorCityCells;
    TSet<FIntPoint> MajorCitySet;
    MajorCityCells.Reserve(TargetMajorCityCount);
    MajorCitySet.Reserve(TargetMajorCityCount);
    const double MinimumCitySeparationSquared = FMath::Square(Spacing * 1.25);
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        if (MajorCityCells.Num() >= TargetMajorCityCount)
        {
            break;
        }
        const FVector2D* CandidatePosition = Sites.Find(Candidate);
        bool bFarEnough = CandidatePosition != nullptr;
        for (const FIntPoint& ExistingCell : MajorCityCells)
        {
            const FVector2D* ExistingPosition = Sites.Find(ExistingCell);
            if (CandidatePosition && ExistingPosition &&
                FVector2D::DistSquared(*CandidatePosition, *ExistingPosition) <
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
    // The spacing guard is best-effort; small maps still get enough city nodes.
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        if (MajorCityCells.Num() >= TargetMajorCityCount)
        {
            break;
        }
        if (!MajorCitySet.Contains(Candidate))
        {
            MajorCityCells.Add(Candidate);
            MajorCitySet.Add(Candidate);
        }
    }

    TMap<FIntPoint, FVector2D> MajorCityAxis;
    TMap<FIntPoint, int32> MajorCityDegree;
    TMap<FIntPoint, TArray<FVector2D>> MajorCityDirections;
    if (MajorCityCells.Num() >= 2)
    {
        // Prim-style minimum spanning tree: every major city belongs to one
        // connected highway backbone, without a ring of redundant long roads.
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
                const FVector2D* From = Sites.Find(FromCell);
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
                    const FVector2D* To = Sites.Find(ToCell);
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

            const FVector2D* From = Sites.Find(BestFrom);
            const FVector2D* To = Sites.Find(BestTo);
            if (From && To)
            {
                if (!MajorCityAxis.Contains(BestFrom))
                {
                    MajorCityAxis.Add(BestFrom, (*To - *From).GetSafeNormal());
                }
                if (!MajorCityAxis.Contains(BestTo))
                {
                    MajorCityAxis.Add(BestTo, (*From - *To).GetSafeNormal());
                }
                ++MajorCityDegree.FindOrAdd(BestFrom);
                ++MajorCityDegree.FindOrAdd(BestTo);
                MajorCityDirections.FindOrAdd(BestFrom).Add((*To - *From).GetSafeNormal());
                MajorCityDirections.FindOrAdd(BestTo).Add((*From - *To).GetSafeNormal());
                BuildSegment(
                    BestFrom, BestTo, *From, *To,
                    HighwayRoadWidth, true, true);
            }
            ConnectedCities.Add(BestTo);
        }
    }

    // Smaller hubs are sparse. Their spanning tree connects them to the
    // intercity backbone with ordinary roads; no fixed all-map rectangular grid.
    TArray<FIntPoint> RoadHubCells = MajorCityCells;
    TSet<FIntPoint> RoadHubSet = MajorCitySet;
    for (const FIntPoint& Candidate : SortedSiteCells)
    {
        if (MajorCitySet.Contains(Candidate))
        {
            continue;
        }
        if ((StableSiteHash(Candidate) % 1000u) < 240u)
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
                const FVector2D* From = Sites.Find(FromCell);
                if (!From)
                {
                    continue;
                }
                for (const FIntPoint& ToCell : RoadHubCells)
                {
                    if (ConnectedHubs.Contains(ToCell))
                    {
                        continue;
                    }
                    const FVector2D* To = Sites.Find(ToCell);
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

            // All major centres are connected by the highway tree, so an
            // additional ordinary road between two major cities is unnecessary.
            const bool bBothMajor =
                MajorCitySet.Contains(BestFrom) && MajorCitySet.Contains(BestTo);
            if (!bBothMajor)
            {
                const FVector2D* From = Sites.Find(BestFrom);
                const FVector2D* To = Sites.Find(BestTo);
                if (From && To)
                {
                    BuildSegment(
                        BestFrom, BestTo, *From, *To,
                        OrdinaryRoadWidth, false, true);
                }
            }
            ConnectedHubs.Add(BestTo);
        }
    }

    // Compact 4-by-3 block layouts are generated only around major cities.
    // The highway corridor occupies the central axis, avoiding stacked meshes.
    const double CityBlockSize = FMath::Clamp(Spacing * 0.13, 4500.0, 7500.0);
    const auto IsRoadPointInBounds = [&](const FVector2D& Point, double FullWidth)
    {
        const double HalfWidth = FullWidth * 0.5;
        return Point.X - HalfWidth >= MinX && Point.X + HalfWidth <= MaxX &&
            Point.Y - HalfWidth >= MinY && Point.Y + HalfWidth <= MaxY;
    };

    for (const FIntPoint& CityCell : MajorCityCells)
    {
        const FVector2D* City = Sites.Find(CityCell);
        if (!City)
        {
            continue;
        }

        FVector2D Axis(1.0f, 0.0f);
        if (const FVector2D* SavedAxis = MajorCityAxis.Find(CityCell))
        {
            Axis = SavedAxis->GetSafeNormal();
        }
        const FVector2D Perpendicular(-Axis.Y, Axis.X);

        // Ensure the central street continues across the urban grid. Extend the
        // highway-width axis only on sides without an aligned incoming/outgoing
        // highway, avoiding duplicate ribbons where the backbone already passes.
        const TArray<FVector2D>* CityDirections = MajorCityDirections.Find(CityCell);
        for (const double Sign : { -1.0, 1.0 })
        {
            const FVector2D SideDirection = Axis * static_cast<float>(Sign);
            bool bHasHighwayOnSide = false;
            if (CityDirections)
            {
                for (const FVector2D& Direction : *CityDirections)
                {
                    if (FVector2D::DotProduct(Direction, SideDirection) > 0.65)
                    {
                        bHasHighwayOnSide = true;
                        break;
                    }
                }
            }
            if (bHasHighwayOnSide)
            {
                continue;
            }
            const FVector2D ExtensionEnd =
                *City + SideDirection * static_cast<float>(CityBlockSize * 2.0);
            if (IsRoadPointInBounds(*City, HighwayRoadWidth) &&
                IsRoadPointInBounds(ExtensionEnd, HighwayRoadWidth))
            {
                BuildSegment(
                    CityCell, CityCell, *City, ExtensionEnd,
                    HighwayRoadWidth, true, true);
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
                static_cast<float>(static_cast<double>(Offset) * CityBlockSize);
            const FVector2D Start = *City + OffsetVector -
                Axis * static_cast<float>(CityBlockSize * 1.5);
            const FVector2D End = *City + OffsetVector +
                Axis * static_cast<float>(CityBlockSize * 1.5);
            if (IsRoadPointInBounds(Start, OrdinaryRoadWidth) &&
                IsRoadPointInBounds(End, OrdinaryRoadWidth))
            {
                BuildSegment(
                    CityCell, CityCell, Start, End,
                    OrdinaryRoadWidth, false, true);
            }
        }

        for (int32 Row = 0; Row < 4; ++Row)
        {
            const double Along = (static_cast<double>(Row) - 1.5) * CityBlockSize;
            const FVector2D OffsetVector = Axis * static_cast<float>(Along);
            const FVector2D Start = *City + OffsetVector -
                Perpendicular * static_cast<float>(CityBlockSize * 2.0);
            const FVector2D End = *City + OffsetVector +
                Perpendicular * static_cast<float>(CityBlockSize * 2.0);
            if (IsRoadPointInBounds(Start, OrdinaryRoadWidth) &&
                IsRoadPointInBounds(End, OrdinaryRoadWidth))
            {
                BuildSegment(
                    CityCell, CityCell, Start, End,
                    OrdinaryRoadWidth, false, true);
            }
        }
    }

    // Rare country lanes connect ordinary roads crosswise, rather than forming
    // a ring around every POI or another rectangular network.
    const int32 DesiredRuralConnectorCount = FMath::Max(1, Sites.Num() / 24);
    for (int32 ConnectorIndex = 0;
        ConnectorIndex < DesiredRuralConnectorCount && OrdinaryRoadPaths.Num() >= 2;
        ++ConnectorIndex)
    {
        bool bFoundConnector = false;
        FVector2D BestStart = FVector2D::ZeroVector;
        FVector2D BestEnd = FVector2D::ZeroVector;
        double BestScore = TNumericLimits<double>::Max();

        for (int32 Attempt = 0; Attempt < 24 && !bFoundConnector; ++Attempt)
        {
            const uint32 PickHash = StableSiteHash(FIntPoint(
                ConnectorIndex * 137 + Attempt * 31 + 17,
                ConnectorIndex * 83 - Attempt * 19 - 11));
            const int32 PathAIndex =
                static_cast<int32>(PickHash % static_cast<uint32>(OrdinaryRoadPaths.Num()));
            const TArray<FVector2D>& PathA = OrdinaryRoadPaths[PathAIndex];
            if (PathA.Num() < 4)
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

            for (int32 PathBIndex = 0; PathBIndex < OrdinaryRoadPaths.Num(); ++PathBIndex)
            {
                if (PathBIndex == PathAIndex)
                {
                    continue;
                }
                const TArray<FVector2D>& PathB = OrdinaryRoadPaths[PathBIndex];
                if (PathB.Num() < 4)
                {
                    continue;
                }
                const int32 Stride = FMath::Max(1, PathB.Num() / 28);
                for (int32 IndexB = 1; IndexB + 1 < PathB.Num(); IndexB += Stride)
                {
                    const FVector2D End = PathB[IndexB];
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

                    const double Score =
                        std::abs(LinkLength / Spacing - 0.75) +
                        AlignmentA * 0.35 + AlignmentB * 0.35;
                    if (Score < BestScore)
                    {
                        BestScore = Score;
                        BestStart = Start;
                        BestEnd = End;
                        bFoundConnector = true;
                    }
                }
            }
        }

        if (bFoundConnector)
        {
            BuildSegment(
                FIntPoint::ZeroValue, FIntPoint::ZeroValue,
                BestStart, BestEnd, DirtRoadWidth, false, false);
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
                    DirtRoadWidth, false, false);
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
        TEXT("SmoothVoxel roads: %d major cities, %d hubs; %d total segments (%d highway, %d ordinary, %d dirt) from %d sites (%d samples), %d bridges; skipped %d routes and %d unsafe bridges."),
        MajorCityCells.Num(), RoadHubCells.Num(), RoadSegmentCount,
        HighwayRoadSegmentCount, OrdinaryRoadSegmentCount, DirtRoadSegmentCount,
        Sites.Num(), TotalRoadSamples, BuiltBridgeCount,
        SkippedRouteCount, SkippedUnsafeBridgeCount);
}
