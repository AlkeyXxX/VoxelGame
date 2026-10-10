#include "VoxelRWGPlanner.h"
#include "Algo/Reverse.h"
#include <queue>

namespace
{
    constexpr float RWGBlockedCost = 1000000.0f;

    struct FRWGOpenNode
    {
        int32 Index;
        float G;
        float F;
        bool operator<(const FRWGOpenNode& Other) const { return F > Other.F; }
    };

    float Distance2D(const FVector& A, const FVector& B)
    {
        return FVector2D(A.X - B.X, A.Y - B.Y).Size();
    }

    float OctileDistance(int32 X1, int32 Y1, int32 X2, int32 Y2)
    {
        const int32 DX = FMath::Abs(X1 - X2);
        const int32 DY = FMath::Abs(Y1 - Y2);
        const int32 DMin = FMath::Min(DX, DY);
        const int32 DMax = FMath::Max(DX, DY);
        return static_cast<float>(DMax - DMin) + 1.41421356f * DMin;
    }

    // Catmull-Rom interpolation rounds A* grid corners without changing the
    // broad route. Short resampled segments let voxel stamping and bridge
    // generation follow a smoother centerline than the 32-block search grid.
    FVector CatmullRomPoint(
        const FVector& P0,
        const FVector& P1,
        const FVector& P2,
        const FVector& P3,
        float T)
    {
        const float T2 = T * T;
        const float T3 = T2 * T;
        return (P1 * 2.0f +
            (P2 - P0) * T +
            (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * T2 +
            (-P0 + P1 * 3.0f - P2 * 3.0f + P3) * T3) * 0.5f;
    }

    TArray<FVector> SmoothRoadPolyline(const TArray<FVector>& SourcePoints)
    {
        TArray<FVector> Result;
        if (SourcePoints.Num() < 3)
        {
            return SourcePoints;
        }

        constexpr float TargetSpacingBlocks = 4.0f;
        Result.Reserve(SourcePoints.Num() * 6);
        Result.Add(SourcePoints[0]);

        for (int32 Segment = 0; Segment < SourcePoints.Num() - 1; ++Segment)
        {
            const FVector& P0 = SourcePoints[FMath::Max(0, Segment - 1)];
            const FVector& P1 = SourcePoints[Segment];
            const FVector& P2 = SourcePoints[Segment + 1];
            const FVector& P3 = SourcePoints[FMath::Min(SourcePoints.Num() - 1, Segment + 2)];
            const float Length = Distance2D(P1, P2);
            const int32 Steps = FMath::Clamp(
                FMath::CeilToInt(Length / TargetSpacingBlocks), 1, 2048);

            for (int32 Step = 1; Step <= Steps; ++Step)
            {
                const float T = static_cast<float>(Step) / static_cast<float>(Steps);
                Result.Add(CatmullRomPoint(P0, P1, P2, P3, T));
            }
        }

        // Keep entrances and settlement endpoints exactly at their requested
        // locations even though interior corners are rounded.
        Result[0] = SourcePoints[0];
        Result.Last() = SourcePoints.Last();
        return Result;
    }

    FString CsvNumber(float Value) { return FString::SanitizeFloat(Value); }
}

int32 FVoxelRWGPlanner::GridIndex(int32 X, int32 Y) const
{
    return X + Y * GridWidth;
}

FVector FVoxelRWGPlanner::GridPosition(int32 X, int32 Y) const
{
    const float WX = FMath::Min(float(X * Settings.GridSpacing), float(Settings.WorldBlocksX - 1));
    const float WY = FMath::Min(float(Y * Settings.GridSpacing), float(Settings.WorldBlocksY - 1));
    const int32 Index = GridIndex(X, Y);
    return FVector(WX, WY, GridHeights.IsValidIndex(Index) ? GridHeights[Index] + 0.8f : 0.8f);
}

int32 FVoxelRWGPlanner::WorldToGridX(float X) const
{
    return FMath::Clamp(FMath::RoundToInt(X / float(Settings.GridSpacing)), 0, GridWidth - 1);
}

int32 FVoxelRWGPlanner::WorldToGridY(float Y) const
{
    return FMath::Clamp(FMath::RoundToInt(Y / float(Settings.GridSpacing)), 0, GridHeight - 1);
}

bool FVoxelRWGPlanner::Generate(
    const FVoxelRWGPlanSettings& InSettings,
    const FVoxelWorldGenerator& Generator)
{
    Settings = InSettings;
    Settings.WorldBlocksX = FMath::Max(2, Settings.WorldBlocksX);
    Settings.WorldBlocksY = FMath::Max(2, Settings.WorldBlocksY);
    Settings.GridSpacing = FMath::Clamp(Settings.GridSpacing, 8, 128);
    Settings.CellSizeBlocks = FMath::Clamp(Settings.CellSizeBlocks, 128, 2048);
    Settings.TargetSettlementCount = FMath::Clamp(Settings.TargetSettlementCount, 1, 64);
    Settings.TargetPOICount = FMath::Clamp(Settings.TargetPOICount, 0, 512);
    Settings.MinimumSettlementDistance = FMath::Max(64.0f, Settings.MinimumSettlementDistance);
    Settings.MinimumPOIDistance = FMath::Max(8.0f, Settings.MinimumPOIDistance);
    Settings.EdgeMargin = FMath::Clamp(Settings.EdgeMargin, 16.0f,
        0.2f * FMath::Min(Settings.WorldBlocksX, Settings.WorldBlocksY));

    Cells.Reset();
    Settlements.Reset();
    POIs.Reset();
    Roads.Reset();
    ConnectedSettlementCount = 0;
    FailedSettlementRoadCount = 0;
    GridHeights.Reset();
    GridCosts.Reset();
    GridWaterFlags.Reset();

    GridWidth = FMath::Max(2, (Settings.WorldBlocksX - 1 + Settings.GridSpacing - 1) / Settings.GridSpacing + 1);
    GridHeight = FMath::Max(2, (Settings.WorldBlocksY - 1 + Settings.GridSpacing - 1) / Settings.GridSpacing + 1);

    BuildTerrainCostField(Generator);
    BuildCellGrid(Generator);
    PlaceSettlements(Generator);
    if (Settlements.Num() == 0)
    {
        return false;
    }
    PlacePOIs(Generator);
    BuildRoadNetwork(Generator);
    return true;
}

void FVoxelRWGPlanner::BuildTerrainCostField(const FVoxelWorldGenerator& Generator)
{
    const int32 CellCount = GridWidth * GridHeight;
    GridHeights.SetNumZeroed(CellCount);
    GridCosts.SetNumZeroed(CellCount);
    GridWaterFlags.SetNumZeroed(CellCount);

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 WX = FMath::Min(X * Settings.GridSpacing, Settings.WorldBlocksX - 1);
            const int32 WY = FMath::Min(Y * Settings.GridSpacing, Settings.WorldBlocksY - 1);
            const float Height = Generator.GetSurfaceHeightFloat(WX, WY);
            const FVoxelWaterColumn Water = Generator.GetWaterColumn(WX, WY, Height);
            const int32 Index = GridIndex(X, Y);
            GridHeights[Index] = Height;

            const bool bWater = Water.WaterSurfaceBlockZ != INDEX_NONE;
            const bool bDeepWater = bWater && Height < float(Settings.SeaLevel) - 0.75f;
            GridWaterFlags[Index] = bWater ? 1 : 0;
            // Deep basins remain blocked; shallow river cells are traversable but
            // expensive so A* prefers a dry detour and only crosses where useful.
            GridCosts[Index] = bDeepWater ? RWGBlockedCost : (bWater ? 28.0f : 1.0f);
        }
    }

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = GridIndex(X, Y);
            if (GridCosts[Index] >= RWGBlockedCost) { continue; }

            const int32 XL = FMath::Max(0, X - 1);
            const int32 XR = FMath::Min(GridWidth - 1, X + 1);
            const int32 YD = FMath::Max(0, Y - 1);
            const int32 YU = FMath::Min(GridHeight - 1, Y + 1);
            const float DX = FMath::Abs(GridHeights[GridIndex(XR, Y)] - GridHeights[GridIndex(XL, Y)]);
            const float DY = FMath::Abs(GridHeights[GridIndex(X, YU)] - GridHeights[GridIndex(X, YD)]);
            const float Grade = (DX + DY) / (2.0f * float(Settings.GridSpacing));
            GridCosts[Index] += FMath::Min(Grade * 18.0f, 24.0f);

            const float WX = FMath::Min(float(X * Settings.GridSpacing), float(Settings.WorldBlocksX - 1));
            const float WY = FMath::Min(float(Y * Settings.GridSpacing), float(Settings.WorldBlocksY - 1));
            const float Edge = FMath::Min(
                FMath::Min(WX, float(Settings.WorldBlocksX - 1) - WX),
                FMath::Min(WY, float(Settings.WorldBlocksY - 1) - WY));
            if (Edge < Settings.EdgeMargin) { GridCosts[Index] += 5.0f; }
        }
    }
}

void FVoxelRWGPlanner::BuildCellGrid(const FVoxelWorldGenerator& Generator)
{
    CellColumns = FMath::Max(1, FMath::DivideAndRoundUp(Settings.WorldBlocksX, Settings.CellSizeBlocks));
    CellRows = FMath::Max(1, FMath::DivideAndRoundUp(Settings.WorldBlocksY, Settings.CellSizeBlocks));
    Cells.Reserve(CellColumns * CellRows);

    for (int32 CellY = 0; CellY < CellRows; ++CellY)
    {
        for (int32 CellX = 0; CellX < CellColumns; ++CellX)
        {
            const int32 MinX = CellX * Settings.CellSizeBlocks;
            const int32 MinY = CellY * Settings.CellSizeBlocks;
            const int32 MaxX = FMath::Min(Settings.WorldBlocksX - 1, MinX + Settings.CellSizeBlocks - 1);
            const int32 MaxY = FMath::Min(Settings.WorldBlocksY - 1, MinY + Settings.CellSizeBlocks - 1);
            const float CenterX = (float(MinX) + float(MaxX)) * 0.5f;
            const float CenterY = (float(MinY) + float(MaxY)) * 0.5f;
            const int32 SampleX = FMath::Clamp(FMath::RoundToInt(CenterX), 0, Settings.WorldBlocksX - 1);
            const int32 SampleY = FMath::Clamp(FMath::RoundToInt(CenterY), 0, Settings.WorldBlocksY - 1);
            const float Height = Generator.GetSurfaceHeightFloat(SampleX, SampleY);
            const EVoxelBiome Biome = Generator.GetBiome(SampleX, SampleY, FMath::RoundToInt(Height));
            const EVoxelLandform Landform = Generator.GetLandform(SampleX, SampleY);

            // A per-cell random component gives every cell a stable identity;
            // broad low-frequency noise groups cities and towns into regions.
            const uint32 CellSeedBits =
                static_cast<uint32>(Settings.Seed) ^
                (static_cast<uint32>(CellX + 1) * 73856093u) ^
                (static_cast<uint32>(CellY + 1) * 19349663u);
            FRandomStream CellRandom(static_cast<int32>(CellSeedBits));
            const FVector2D MacroPosition(
                (float(CellX) + float(Settings.Seed) * 0.0011f) * 0.55f,
                (float(CellY) - float(Settings.Seed) * 0.0017f) * 0.55f);
            const float MacroScore = FMath::PerlinNoise2D(MacroPosition) * 0.5f + 0.5f;
            const float UrbanScore = CellRandom.FRand() * 0.62f + MacroScore * 0.38f;

            FVoxelRWGCell Cell;
            Cell.Id = CellY * CellColumns + CellX;
            Cell.GridCoord = FIntPoint(CellX, CellY);
            Cell.Center = FVector(CenterX, CenterY, Height);
            Cell.Biome = Biome;
            Cell.Landform = Landform;
            Cell.UrbanScore = UrbanScore;
            Cell.Type = EVoxelRWGCellType::Wilderness;

            if (UrbanScore >= 0.84f)
            {
                Cell.Type = EVoxelRWGCellType::City;
            }
            else if (UrbanScore >= 0.68f)
            {
                Cell.Type = EVoxelRWGCellType::Town;
            }
            else if (UrbanScore >= 0.49f)
            {
                Cell.Type = EVoxelRWGCellType::Rural;
            }

            // Dense urban layouts are less suitable for steep mountain cells.
            if (Landform == EVoxelLandform::Mountains)
            {
                if (Cell.Type == EVoxelRWGCellType::City)
                {
                    Cell.Type = EVoxelRWGCellType::Town;
                }
                else if (Cell.Type == EVoxelRWGCellType::Town)
                {
                    Cell.Type = EVoxelRWGCellType::Rural;
                }
            }

            // The snowy macro-biome can contain towns, but city centers are
            // downweighted; desert townships occasionally use an industrial role.
            if (Biome == EVoxelBiome::Snow && Cell.Type == EVoxelRWGCellType::City)
            {
                Cell.Type = EVoxelRWGCellType::Town;
            }
            else if (Biome == EVoxelBiome::Desert &&
                (Cell.Type == EVoxelRWGCellType::Town || Cell.Type == EVoxelRWGCellType::Rural) &&
                CellRandom.FRand() < 0.16f)
            {
                Cell.Type = EVoxelRWGCellType::Industrial;
            }

            const float InnerMargin = FMath::Min(48.0f, float(Settings.CellSizeBlocks) * 0.12f);
            const float CandidateMinX = FMath::Min(float(MaxX), float(MinX) + InnerMargin);
            const float CandidateMaxX = FMath::Max(CandidateMinX, float(MaxX) - InnerMargin);
            const float CandidateMinY = FMath::Min(float(MaxY), float(MinY) + InnerMargin);
            const float CandidateMaxY = FMath::Max(CandidateMinY, float(MaxY) - InnerMargin);

            // Each cell tries several deterministic candidate sites. If the
            // cell is submerged, too steep, or too close to the map edge, it
            // remains wilderness instead of receiving an invalid hub.
            for (int32 Attempt = 0; Attempt < 12; ++Attempt)
            {
                FVector2D Candidate(CenterX, CenterY);
                if (Attempt > 0)
                {
                    Candidate.X = CellRandom.FRandRange(CandidateMinX, CandidateMaxX);
                    Candidate.Y = CellRandom.FRandRange(CandidateMinY, CandidateMaxY);
                }
                if (!IsValidSite(Candidate, Generator, 0.20f))
                {
                    continue;
                }
                Cell.bBuildable = true;
                Cell.SuggestedHubPosition = GetBlockPosition(Candidate.X, Candidate.Y, Generator);
                break;
            }

            if (!Cell.bBuildable)
            {
                Cell.Type = EVoxelRWGCellType::Wilderness;
            }
            Cells.Add(Cell);
        }
    }

    // Smooth the map classification without blurring terrain or biome data.
    // An isolated city seed becomes a town; cells with no nearby hub potential
    // remain wilderness/rural, reducing salt-and-pepper zoning.
    TArray<EVoxelRWGCellType> SmoothedTypes;
    SmoothedTypes.SetNum(Cells.Num());
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
    {
        const FVoxelRWGCell& Cell = Cells[Index];
        EVoxelRWGCellType Type = Cell.Type;
        int32 NeighborUrbanCount = 0;
        const int32 DX[4] = { -1, 1, 0, 0 };
        const int32 DY[4] = { 0, 0, -1, 1 };
        for (int32 Direction = 0; Direction < 4; ++Direction)
        {
            const int32 NX = Cell.GridCoord.X + DX[Direction];
            const int32 NY = Cell.GridCoord.Y + DY[Direction];
            if (NX < 0 || NX >= CellColumns || NY < 0 || NY >= CellRows)
            {
                continue;
            }
            const FVoxelRWGCell& Neighbor = Cells[NY * CellColumns + NX];
            if (Neighbor.Type == EVoxelRWGCellType::City ||
                Neighbor.Type == EVoxelRWGCellType::Town)
            {
                ++NeighborUrbanCount;
            }
        }
        if (Type == EVoxelRWGCellType::City && NeighborUrbanCount == 0)
        {
            Type = EVoxelRWGCellType::Town;
        }
        else if (Type == EVoxelRWGCellType::Wilderness && Cell.bBuildable &&
            NeighborUrbanCount >= 2 && Cell.UrbanScore >= 0.38f)
        {
            Type = EVoxelRWGCellType::Rural;
        }
        SmoothedTypes[Index] = Type;
    }
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
    {
        Cells[Index].Type = SmoothedTypes[Index];
    }
}

void FVoxelRWGPlanner::ClaimCellForSettlement(const FVoxelRWGSettlement& Settlement)
{
    if (CellColumns <= 0 || CellRows <= 0)
    {
        return;
    }
    const int32 CellX = FMath::Clamp(
        FMath::FloorToInt(Settlement.Position.X / float(Settings.CellSizeBlocks)),
        0, CellColumns - 1);
    const int32 CellY = FMath::Clamp(
        FMath::FloorToInt(Settlement.Position.Y / float(Settings.CellSizeBlocks)),
        0, CellRows - 1);
    const int32 Index = CellY * CellColumns + CellX;
    if (!Cells.IsValidIndex(Index))
    {
        return;
    }
    FVoxelRWGCell& Cell = Cells[Index];
    Cell.SettlementId = Settlement.Id;
    Cell.bBuildable = true;
    switch (Settlement.Type)
    {
    case EVoxelRWGSettlementType::City:
        Cell.Type = EVoxelRWGCellType::City;
        break;
    case EVoxelRWGSettlementType::Town:
    case EVoxelRWGSettlementType::Village:
        Cell.Type = EVoxelRWGCellType::Town;
        break;
    case EVoxelRWGSettlementType::Industrial:
        Cell.Type = EVoxelRWGCellType::Industrial;
        break;
    case EVoxelRWGSettlementType::Rural:
    default:
        Cell.Type = EVoxelRWGCellType::Rural;
        break;
    }
}

FVector FVoxelRWGPlanner::GetBlockPosition(
    float X, float Y, const FVoxelWorldGenerator& Generator) const
{
    const int32 IX = FMath::Clamp(FMath::RoundToInt(X), 0, Settings.WorldBlocksX - 1);
    const int32 IY = FMath::Clamp(FMath::RoundToInt(Y), 0, Settings.WorldBlocksY - 1);
    return FVector(X, Y, Generator.GetSurfaceHeightFloat(IX, IY) + 0.8f);
}

bool FVoxelRWGPlanner::IsValidSite(
    const FVector2D& Position,
    const FVoxelWorldGenerator& Generator,
    float MaxGrade) const
{
    if (Position.X < Settings.EdgeMargin || Position.Y < Settings.EdgeMargin ||
        Position.X >= Settings.WorldBlocksX - Settings.EdgeMargin ||
        Position.Y >= Settings.WorldBlocksY - Settings.EdgeMargin)
    {
        return false;
    }

    const int32 X = FMath::RoundToInt(Position.X);
    const int32 Y = FMath::RoundToInt(Position.Y);
    const float H = Generator.GetSurfaceHeightFloat(X, Y);
    if (H < float(Settings.SeaLevel) + 0.25f ||
        Generator.GetWaterColumn(X, Y, H).WaterSurfaceBlockZ != INDEX_NONE)
    {
        return false;
    }

    const int32 Probe = FMath::Max(Settings.GridSpacing * 2, 24);
    const float Left = Generator.GetSurfaceHeightFloat(FMath::Max(0, X - Probe), Y);
    const float Right = Generator.GetSurfaceHeightFloat(FMath::Min(Settings.WorldBlocksX - 1, X + Probe), Y);
    const float Down = Generator.GetSurfaceHeightFloat(X, FMath::Max(0, Y - Probe));
    const float Up = Generator.GetSurfaceHeightFloat(X, FMath::Min(Settings.WorldBlocksY - 1, Y + Probe));
    const float Grade = (FMath::Abs(Right - Left) + FMath::Abs(Up - Down)) /
        (4.0f * float(Probe));
    return Grade <= MaxGrade;
}

void FVoxelRWGPlanner::PlaceSettlements(const FVoxelWorldGenerator& Generator)
{
    FRandomStream FallbackRandom(Settings.Seed ^ 0x51A77E);
    const float MinDimension = float(FMath::Min(Settings.WorldBlocksX, Settings.WorldBlocksY));
    const float MinDistance = FMath::Min(Settings.MinimumSettlementDistance, MinDimension * 0.24f);
    const FVector2D Center(float(Settings.WorldBlocksX) * 0.5f, float(Settings.WorldBlocksY) * 0.5f);
    int32 NextId = 1000;

    const auto IsFarEnough = [this, MinDistance](const FVector2D& Candidate)
    {
        for (const FVoxelRWGSettlement& Existing : Settlements)
        {
            if (FVector2D(
                Existing.Position.X - Candidate.X,
                Existing.Position.Y - Candidate.Y).Size() < MinDistance)
            {
                return false;
            }
        }
        return true;
    };

    const auto AddSettlement = [this, &Generator, &NextId](const FVector2D& Candidate,
        EVoxelRWGSettlementType Type, float Radius)
    {
        FVoxelRWGSettlement Settlement;
        Settlement.Id = NextId++;
        Settlement.Type = Type;
        Settlement.Position = GetBlockPosition(Candidate.X, Candidate.Y, Generator);
        Settlement.Biome = Generator.GetBiome(
            FMath::RoundToInt(Candidate.X),
            FMath::RoundToInt(Candidate.Y),
            FMath::RoundToInt(Settlement.Position.Z));
        Settlement.Radius = Radius;
        Settlements.Add(Settlement);
        ClaimCellForSettlement(Settlement);
    };

    // Keep the stable starter area from the existing terrain generator.
    if (IsValidSite(Center, Generator, 0.16f))
    {
        AddSettlement(Center, EVoxelRWGSettlementType::Village, 230.0f);
    }

    // Use cell zoning as the primary source of hub candidates. Highest urban
    // potential is considered first; distance and terrain validation still win.
    TArray<int32> CandidateCells;
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
    {
        const FVoxelRWGCell& Cell = Cells[Index];
        if (Cell.bBuildable &&
            Cell.Type != EVoxelRWGCellType::Wilderness &&
            Cell.SettlementId == INDEX_NONE)
        {
            CandidateCells.Add(Index);
        }
    }
    CandidateCells.Sort([this](int32 A, int32 B)
    {
        if (Cells[A].UrbanScore == Cells[B].UrbanScore)
        {
            return Cells[A].Id < Cells[B].Id;
        }
        return Cells[A].UrbanScore > Cells[B].UrbanScore;
    });

    for (int32 CellIndex : CandidateCells)
    {
        if (Settlements.Num() >= Settings.TargetSettlementCount)
        {
            break;
        }

        const FVoxelRWGCell& Cell = Cells[CellIndex];
        const FVector2D Candidate(Cell.SuggestedHubPosition.X, Cell.SuggestedHubPosition.Y);
        if (!IsValidSite(Candidate, Generator, 0.22f) || !IsFarEnough(Candidate))
        {
            continue;
        }

        EVoxelRWGSettlementType Type = EVoxelRWGSettlementType::Rural;
        float Radius = 185.0f;
        switch (Cell.Type)
        {
        case EVoxelRWGCellType::City:
            Type = EVoxelRWGSettlementType::City;
            Radius = 420.0f;
            break;
        case EVoxelRWGCellType::Town:
            Type = EVoxelRWGSettlementType::Town;
            Radius = 330.0f;
            break;
        case EVoxelRWGCellType::Industrial:
            Type = EVoxelRWGSettlementType::Industrial;
            Radius = 280.0f;
            break;
        case EVoxelRWGCellType::Rural:
        default:
            if (Cell.UrbanScore > 0.59f)
            {
                Type = EVoxelRWGSettlementType::Village;
                Radius = 245.0f;
            }
            break;
        }

        AddSettlement(Candidate, Type, Radius);
    }

    // If too few zoned cells were usable (water, steep hills, or distance
    // exclusions), fill the remaining slots with valid dry sites. Their type
    // is still inferred from the cell zoning where possible.
    const int32 MaxAttempts = Settings.TargetSettlementCount * 900;
    for (int32 Attempt = 0;
        Attempt < MaxAttempts && Settlements.Num() < Settings.TargetSettlementCount;
        ++Attempt)
    {
        const FVector2D Candidate(
            FallbackRandom.FRandRange(Settings.EdgeMargin, Settings.WorldBlocksX - Settings.EdgeMargin),
            FallbackRandom.FRandRange(Settings.EdgeMargin, Settings.WorldBlocksY - Settings.EdgeMargin));
        if (!IsValidSite(Candidate, Generator, 0.20f) || !IsFarEnough(Candidate))
        {
            continue;
        }

        const int32 CellX = FMath::Clamp(
            FMath::FloorToInt(Candidate.X / float(Settings.CellSizeBlocks)),
            0, CellColumns - 1);
        const int32 CellY = FMath::Clamp(
            FMath::FloorToInt(Candidate.Y / float(Settings.CellSizeBlocks)),
            0, CellRows - 1);
        const int32 CellIndex = CellY * CellColumns + CellX;
        if (Cells.IsValidIndex(CellIndex) && Cells[CellIndex].SettlementId != INDEX_NONE)
        {
            continue;
        }

        EVoxelRWGSettlementType Type = EVoxelRWGSettlementType::Rural;
        float Radius = 185.0f;
        if (Cells.IsValidIndex(CellIndex))
        {
            const FVoxelRWGCell& Cell = Cells[CellIndex];
            if (Cell.Type == EVoxelRWGCellType::City)
            {
                Type = EVoxelRWGSettlementType::City;
                Radius = 420.0f;
            }
            else if (Cell.Type == EVoxelRWGCellType::Town)
            {
                Type = EVoxelRWGSettlementType::Town;
                Radius = 330.0f;
            }
            else if (Cell.Type == EVoxelRWGCellType::Industrial)
            {
                Type = EVoxelRWGSettlementType::Industrial;
                Radius = 280.0f;
            }
            else if (Cell.Type == EVoxelRWGCellType::Rural && Cell.UrbanScore > 0.59f)
            {
                Type = EVoxelRWGSettlementType::Village;
                Radius = 245.0f;
            }
        }
        else if (FallbackRandom.FRand() < 0.16f)
        {
            Type = EVoxelRWGSettlementType::Village;
            Radius = 245.0f;
        }

        AddSettlement(Candidate, Type, Radius);
    }
}

void FVoxelRWGPlanner::PlacePOIs(const FVoxelWorldGenerator& Generator)
{
    if (Settlements.Num() == 0 || Settings.TargetPOICount <= 0) { return; }
    FRandomStream Random(Settings.Seed ^ 0x60B17E);
    int32 Attempts = 0;
    const int32 MaxAttempts = Settings.TargetPOICount * 250;

    while (POIs.Num() < Settings.TargetPOICount && Attempts < MaxAttempts)
    {
        const int32 AttemptIndex = Attempts++;
        const FVoxelRWGSettlement& Hub = Settlements[AttemptIndex % Settlements.Num()];
        const float Angle = Random.FRandRange(0.0f, 6.28318530718f);
        const float Distance = FMath::Sqrt(Random.FRand()) * Hub.Radius * 0.88f;
        const FVector2D Candidate(
            Hub.Position.X + FMath::Cos(Angle) * Distance,
            Hub.Position.Y + FMath::Sin(Angle) * Distance);
        if (!IsValidSite(Candidate, Generator, 0.24f)) { continue; }

        bool bTooClose = false;
        for (const FVoxelRWGPOI& Existing : POIs)
        {
            if (FVector2D(Existing.Position.X - Candidate.X, Existing.Position.Y - Candidate.Y).Size() <
                Settings.MinimumPOIDistance) { bTooClose = true; break; }
        }
        if (bTooClose) { continue; }

        FVoxelRWGPOI P;
        P.Id = 2000 + POIs.Num();
        P.SettlementId = Hub.Id;
        P.Position = GetBlockPosition(Candidate.X, Candidate.Y, Generator);
        P.Biome = Generator.GetBiome(FMath::RoundToInt(Candidate.X), FMath::RoundToInt(Candidate.Y),
            FMath::RoundToInt(P.Position.Z));
        const float Roll = Random.FRand();
        if (Hub.Type == EVoxelRWGSettlementType::Industrial && Roll < 0.42f) P.Type = EVoxelRWGPOIType::Industrial;
        else if (P.Biome == EVoxelBiome::Desert && Roll < 0.34f) P.Type = EVoxelRWGPOIType::Farm;
        else if (Roll < 0.10f) P.Type = EVoxelRWGPOIType::Trader;
        else if (Roll < 0.23f) P.Type = EVoxelRWGPOIType::Commercial;
        else if (Roll < 0.34f) P.Type = EVoxelRWGPOIType::Civic;
        else if (Roll < 0.45f) P.Type = EVoxelRWGPOIType::Utility;
        else if (Roll < 0.60f) P.Type = EVoxelRWGPOIType::Ruin;
        else if (Roll < 0.72f) P.Type = EVoxelRWGPOIType::Farm;
        else P.Type = EVoxelRWGPOIType::Residential;
        POIs.Add(P);
    }
}

int32 FVoxelRWGPlanner::FindNearestWalkable(
    int32 X, int32 Y, bool bAllowWater, int32 MaxSearchRadius) const
{
    X = FMath::Clamp(X, 0, GridWidth - 1);
    Y = FMath::Clamp(Y, 0, GridHeight - 1);
    MaxSearchRadius = FMath::Clamp(MaxSearchRadius, 0, 8);

    for (int32 Radius = 0; Radius <= MaxSearchRadius; ++Radius)
    {
        int32 Best = INDEX_NONE;
        float BestCost = TNumericLimits<float>::Max();
        for (int32 DY = -Radius; DY <= Radius; ++DY)
        {
            for (int32 DX = -Radius; DX <= Radius; ++DX)
            {
                if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Radius) { continue; }
                const int32 NX = X + DX, NY = Y + DY;
                if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight) { continue; }
                const int32 Index = GridIndex(NX, NY);
                const float Cost = GridCosts[Index];
                if (Cost >= RWGBlockedCost) { continue; }

                const bool bWater = GridWaterFlags.IsValidIndex(Index) && GridWaterFlags[Index] != 0;
                if (bWater && !bAllowWater) { continue; }

                // Prefer the cheapest candidate on the nearest ring. For local
                // roads the search radius is deliberately small so snapping
                // cannot leap to a distant bank or unrelated street.
                const float CandidateScore = Cost + Radius * 0.001f;
                if (CandidateScore < BestCost)
                {
                    BestCost = CandidateScore;
                    Best = Index;
                }
            }
        }
        if (Best != INDEX_NONE) { return Best; }
    }
    return INDEX_NONE;
}

bool FVoxelRWGPlanner::FindPath(
    int32 StartX, int32 StartY, int32 GoalX, int32 GoalY,
    bool bAllowWaterCrossing, TArray<int32>& OutPath) const
{
    OutPath.Reset();
    if (GridWidth <= 0 || GridHeight <= 0) { return false; }
    const int32 EndpointSearchRadius = bAllowWaterCrossing ? 8 : 2;
    const int32 Start = FindNearestWalkable(StartX, StartY, bAllowWaterCrossing, EndpointSearchRadius);
    const int32 Goal = FindNearestWalkable(GoalX, GoalY, bAllowWaterCrossing, EndpointSearchRadius);
    if (Start == INDEX_NONE || Goal == INDEX_NONE) { return false; }
    if (Start == Goal) { OutPath.Add(Start); return true; }

    const int32 Count = GridWidth * GridHeight;
    TArray<float> G; G.Init(TNumericLimits<float>::Max(), Count);
    TArray<int32> Previous; Previous.Init(INDEX_NONE, Count);
    TArray<uint8> Closed; Closed.SetNumZeroed(Count);
    const int32 GX = Goal % GridWidth, GY = Goal / GridWidth;

    std::priority_queue<FRWGOpenNode> Open;
    G[Start] = 0.0f;
    FRWGOpenNode First;
    First.Index = Start; First.G = 0.0f;
    First.F = OctileDistance(Start % GridWidth, Start / GridWidth, GX, GY);
    Open.push(First);

    const int32 DX[8] = { -1, 1, 0, 0, -1, 1, -1, 1 };
    const int32 DY[8] = { 0, 0, -1, 1, -1, -1, 1, 1 };

    while (!Open.empty())
    {
        const FRWGOpenNode Current = Open.top(); Open.pop();
        if (Closed[Current.Index]) { continue; }
        Closed[Current.Index] = 1;

        if (Current.Index == Goal)
        {
            int32 Trace = Goal;
            while (Trace != INDEX_NONE)
            {
                OutPath.Add(Trace);
                if (Trace == Start) { break; }
                Trace = Previous[Trace];
            }
            if (OutPath.Num() == 0 || OutPath.Last() != Start) { OutPath.Reset(); return false; }
            Algo::Reverse(OutPath);
            return true;
        }

        const int32 CX = Current.Index % GridWidth, CY = Current.Index / GridWidth;
        for (int32 D = 0; D < 8; ++D)
        {
            const int32 NX = CX + DX[D], NY = CY + DY[D];
            if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight) { continue; }
            const int32 Next = GridIndex(NX, NY);
            if (Closed[Next] || GridCosts[Next] >= RWGBlockedCost) { continue; }
            if (!bAllowWaterCrossing && GridWaterFlags.IsValidIndex(Next) && GridWaterFlags[Next] != 0)
            {
                continue;
            }
            const bool bDiagonal = D >= 4;
            if (bDiagonal)
            {
                const int32 SideX = GridIndex(CX + DX[D], CY);
                const int32 SideY = GridIndex(CX, CY + DY[D]);
                if (GridCosts[SideX] >= RWGBlockedCost || GridCosts[SideY] >= RWGBlockedCost)
                {
                    continue;
                }
                if (!bAllowWaterCrossing &&
                    ((GridWaterFlags.IsValidIndex(SideX) && GridWaterFlags[SideX] != 0) ||
                     (GridWaterFlags.IsValidIndex(SideY) && GridWaterFlags[SideY] != 0)))
                {
                    continue;
                }
            }

            const float Step = bDiagonal ? 1.41421356f : 1.0f;
            const float CandidateG = G[Current.Index] +
                Step * 0.5f * (GridCosts[Current.Index] + GridCosts[Next]);
            if (CandidateG < G[Next])
            {
                G[Next] = CandidateG;
                Previous[Next] = Current.Index;
                FRWGOpenNode Node;
                Node.Index = Next; Node.G = CandidateG;
                Node.F = CandidateG + OctileDistance(NX, NY, GX, GY);
                Open.push(Node);
            }
        }
    }
    return false;
}

bool FVoxelRWGPlanner::BuildRoad(
    int32 FromId, const FVector& From, int32 ToId, const FVector& To,
    EVoxelRWGRoadType Type, const FVoxelWorldGenerator& Generator)
{
    const bool bAllowWaterCrossing = Type != EVoxelRWGRoadType::Local;
    TArray<int32> Path;
    if (!FindPath(WorldToGridX(From.X), WorldToGridY(From.Y),
        WorldToGridX(To.X), WorldToGridY(To.Y), bAllowWaterCrossing, Path)) { return false; }

    FVoxelRWGRoad Road;
    Road.Id = 3000 + Roads.Num();
    Road.FromId = FromId;
    Road.ToId = ToId;
    Road.Type = Type;
    Road.Points.Add(From);

    for (int32 Index : Path)
    {
        const FVector P = GridPosition(Index % GridWidth, Index / GridWidth);
        if (Road.Points.Num() == 0 || Distance2D(Road.Points.Last(), P) > 1.0f) { Road.Points.Add(P); }
    }
    if (Road.Points.Num() == 0 || Distance2D(Road.Points.Last(), To) > 1.0f) { Road.Points.Add(To); }

    // Replace coarse A* corners with a resampled Catmull-Rom curve.
    // Preserve the original safe path as fallback if rounding a local driveway
    // would move it into a narrow water channel.
    TArray<FVector> RawPoints = Road.Points;
    Road.Points = SmoothRoadPolyline(RawPoints);

    const auto IsDryPolyline = [this, &Generator](const TArray<FVector>& Points)
    {
        for (int32 SegmentIndex = 1; SegmentIndex < Points.Num(); ++SegmentIndex)
        {
            const FVector& A = Points[SegmentIndex - 1];
            const FVector& B = Points[SegmentIndex];
            const float Length = Distance2D(A, B);
            const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / 4.0f), 1, 4096);
            for (int32 Step = 0; Step <= Steps; ++Step)
            {
                const float T = static_cast<float>(Step) / static_cast<float>(Steps);
                const int32 WX = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(A.X, B.X, T)), 0, Settings.WorldBlocksX - 1);
                const int32 WY = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(A.Y, B.Y, T)), 0, Settings.WorldBlocksY - 1);
                const float Height = Generator.GetSurfaceHeightFloat(WX, WY);
                if (Generator.GetWaterColumn(WX, WY, Height).WaterSurfaceBlockZ != INDEX_NONE)
                {
                    return false;
                }
            }
        }
        return true;
    };

    if (!bAllowWaterCrossing && !IsDryPolyline(Road.Points))
    {
        Road.Points = RawPoints;
        if (!IsDryPolyline(Road.Points))
        {
            return false;
        }
    }

    Roads.Add(MoveTemp(Road));
    return true;
}

void FVoxelRWGPlanner::BuildRoadNetwork(const FVoxelWorldGenerator& Generator)
{
    TSet<uint64> ConnectedPairs;
    TSet<uint64> FailedPairs;
    const auto PairKey = [](int32 A, int32 B) -> uint64
    {
        const uint32 Low = uint32(FMath::Min(A, B));
        const uint32 High = uint32(FMath::Max(A, B));
        return (uint64(Low) << 32) | uint64(High);
    };

    if (Settlements.Num() >= 2)
    {
        // Prim's algorithm produces a deterministic minimum-spanning backbone.
        TSet<int32> Connected;
        Connected.Add(0);
        while (Connected.Num() < Settlements.Num())
        {
            float BestDistance = TNumericLimits<float>::Max();
            int32 BestFrom = INDEX_NONE, BestTo = INDEX_NONE;
            for (int32 FromIndex = 0; FromIndex < Settlements.Num(); ++FromIndex)
            {
                if (!Connected.Contains(FromIndex)) { continue; }
                for (int32 ToIndex = 0; ToIndex < Settlements.Num(); ++ToIndex)
                {
                    if (Connected.Contains(ToIndex) || FailedPairs.Contains(PairKey(FromIndex, ToIndex))) { continue; }
                    const float D = Distance2D(Settlements[FromIndex].Position, Settlements[ToIndex].Position);
                    if (D < BestDistance) { BestDistance = D; BestFrom = FromIndex; BestTo = ToIndex; }
                }
            }
            if (BestFrom == INDEX_NONE || BestTo == INDEX_NONE) { break; }

            const FVoxelRWGSettlement& A = Settlements[BestFrom];
            const FVoxelRWGSettlement& B = Settlements[BestTo];
            const bool bHighway = BestDistance >
                float(FMath::Min(Settings.WorldBlocksX, Settings.WorldBlocksY)) * 0.27f;
            const bool bRuralConnection =
                A.Type == EVoxelRWGSettlementType::Rural ||
                A.Type == EVoxelRWGSettlementType::Village ||
                B.Type == EVoxelRWGSettlementType::Rural ||
                B.Type == EVoxelRWGSettlementType::Village;
            const EVoxelRWGRoadType Type = bHighway
                ? EVoxelRWGRoadType::Main
                : (bRuralConnection ? EVoxelRWGRoadType::Rural : EVoxelRWGRoadType::Connector);
            if (BuildRoad(A.Id, A.Position, B.Id, B.Position, Type, Generator))
            {
                ConnectedPairs.Add(PairKey(BestFrom, BestTo));
                Connected.Add(BestTo);
            }
            else
            {
                // Don't retry a route that cannot cross the current landmass;
                // the next iteration tries the next-best reachable connection.
                FailedPairs.Add(PairKey(BestFrom, BestTo));
                ++FailedSettlementRoadCount;
            }
        }
        ConnectedSettlementCount = Connected.Num();
    }
    else if (Settlements.Num() == 1)
    {
        ConnectedSettlementCount = 1;
    }

    if (Settlements.Num() >= 2)
    {
        // Some nearby towns get loop roads so the result is not only a tree.
        FRandomStream Random(Settings.Seed ^ 0x70AD5);
        int32 Extra = 0;
        const int32 ExtraLimit = FMath::Max(1, Settlements.Num() / 4);
        const float MaxLoop = float(FMath::Min(Settings.WorldBlocksX, Settings.WorldBlocksY)) * 0.36f;
        for (int32 A = 0; A < Settlements.Num() && Extra < ExtraLimit; ++A)
        {
            for (int32 B = A + 1; B < Settlements.Num() && Extra < ExtraLimit; ++B)
            {
                if (ConnectedPairs.Contains(PairKey(A, B))) { continue; }
                const float D = Distance2D(Settlements[A].Position, Settlements[B].Position);
                if (D > MaxLoop || Random.FRand() > 0.19f) { continue; }
                const bool bRuralConnection =
                    Settlements[A].Type == EVoxelRWGSettlementType::Rural ||
                    Settlements[A].Type == EVoxelRWGSettlementType::Village ||
                    Settlements[B].Type == EVoxelRWGSettlementType::Rural ||
                    Settlements[B].Type == EVoxelRWGSettlementType::Village;
                const EVoxelRWGRoadType LoopType = bRuralConnection
                    ? EVoxelRWGRoadType::Rural : EVoxelRWGRoadType::Connector;
                if (BuildRoad(Settlements[A].Id, Settlements[A].Position,
                    Settlements[B].Id, Settlements[B].Position, LoopType, Generator))
                {
                    ConnectedPairs.Add(PairKey(A, B));
                    ++Extra;
                }
            }
        }
    }

    // The MST rooted at the stable center village naturally forms a tree,
    // which can look like spokes radiating out from the map center. Add a sparse
    // perimeter loop between outer settlements so routes also traverse the
    // full map instead of ending at a handful of spokes.
    if (Settlements.Num() >= 6)
    {
        const float MinDimension = float(FMath::Min(Settings.WorldBlocksX, Settings.WorldBlocksY));
        const FVector2D MapCenter(
            float(Settings.WorldBlocksX) * 0.5f,
            float(Settings.WorldBlocksY) * 0.5f);
        const float OuterRadius = MinDimension * 0.20f;

        TArray<int32> OuterSettlementIndices;
        for (int32 Index = 0; Index < Settlements.Num(); ++Index)
        {
            const FVector& Position = Settlements[Index].Position;
            if (FVector2D(Position.X - MapCenter.X, Position.Y - MapCenter.Y).Size() >= OuterRadius)
            {
                OuterSettlementIndices.Add(Index);
            }
        }

        OuterSettlementIndices.Sort([this, &MapCenter](int32 A, int32 B)
        {
            const FVector& PA = Settlements[A].Position;
            const FVector& PB = Settlements[B].Position;
            const float AngleA = FMath::Atan2(PA.Y - MapCenter.Y, PA.X - MapCenter.X);
            const float AngleB = FMath::Atan2(PB.Y - MapCenter.Y, PB.X - MapCenter.X);
            if (!FMath::IsNearlyEqual(AngleA, AngleB))
            {
                return AngleA < AngleB;
            }
            return Settlements[A].Id < Settlements[B].Id;
        });

        if (OuterSettlementIndices.Num() >= 4)
        {
            for (int32 RingIndex = 0; RingIndex < OuterSettlementIndices.Num(); ++RingIndex)
            {
                const int32 FromIndex = OuterSettlementIndices[RingIndex];
                const int32 ToIndex = OuterSettlementIndices[
                    (RingIndex + 1) % OuterSettlementIndices.Num()];
                if (FromIndex == ToIndex || ConnectedPairs.Contains(PairKey(FromIndex, ToIndex)))
                {
                    continue;
                }

                const FVoxelRWGSettlement& A = Settlements[FromIndex];
                const FVoxelRWGSettlement& B = Settlements[ToIndex];
                const float Distance = Distance2D(A.Position, B.Position);
                const EVoxelRWGRoadType Type = Distance >= MinDimension * 0.16f
                    ? EVoxelRWGRoadType::Main
                    : EVoxelRWGRoadType::Connector;

                if (BuildRoad(A.Id, A.Position, B.Id, B.Position, Type, Generator))
                {
                    ConnectedPairs.Add(PairKey(FromIndex, ToIndex));
                }
            }
        }
    }

    // Local POI access must remain on dry land. A nearest street across a
    // narrow lake/river is not a valid entrance: try it, but if it fails the
    // exact water check, fall back to the parent settlement's side of the POI.
    const auto IsDrySegment = [&Generator, this](const FVector& A, const FVector& B)
    {
        const float Length = Distance2D(A, B);
        const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / 8.0f), 1, 4096);
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const float T = static_cast<float>(Step) / static_cast<float>(Steps);
            const int32 WX = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(A.X, B.X, T)), 0, Settings.WorldBlocksX - 1);
            const int32 WY = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(A.Y, B.Y, T)), 0, Settings.WorldBlocksY - 1);
            const float Height = Generator.GetSurfaceHeightFloat(WX, WY);
            if (Generator.GetWaterColumn(WX, WY, Height).WaterSurfaceBlockZ != INDEX_NONE)
            {
                return false;
            }
        }
        return true;
    };

    for (const FVoxelRWGPOI& POI : POIs)
    {
        const FVoxelRWGSettlement* Hub = Settlements.FindByPredicate(
            [&POI](const FVoxelRWGSettlement& S) { return S.Id == POI.SettlementId; });
        if (!Hub) { continue; }

        const float EntranceInset = POI.Radius + 4.0f;
        FVector NearestBackbonePoint = FVector::ZeroVector;
        int32 NearestBackboneId = INDEX_NONE;
        float BestDistanceSquared = TNumericLimits<float>::Max();

        for (const FVoxelRWGRoad& ExistingRoad : Roads)
        {
            if (ExistingRoad.Type == EVoxelRWGRoadType::Local || ExistingRoad.Points.Num() < 2)
            {
                continue;
            }

            for (int32 Segment = 1; Segment < ExistingRoad.Points.Num(); ++Segment)
            {
                const FVector& A = ExistingRoad.Points[Segment - 1];
                const FVector& B = ExistingRoad.Points[Segment];
                const FVector2D Delta(B.X - A.X, B.Y - A.Y);
                const float LengthSquared = Delta.SizeSquared();
                const FVector2D FromSegmentStart(POI.Position.X - A.X, POI.Position.Y - A.Y);
                const float T = LengthSquared > SMALL_NUMBER
                    ? FMath::Clamp((FromSegmentStart.X * Delta.X + FromSegmentStart.Y * Delta.Y) / LengthSquared, 0.0f, 1.0f)
                    : 0.0f;
                const FVector Candidate = FMath::Lerp(A, B, T);
                const float DistanceSquared = FVector2D(
                    Candidate.X - POI.Position.X, Candidate.Y - POI.Position.Y).SizeSquared();
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    NearestBackbonePoint = Candidate;
                    NearestBackboneId = ExistingRoad.FromId;
                }
            }
        }

        bool bConnectedToRoad = false;
        if (NearestBackboneId != INDEX_NONE &&
            BestDistanceSquared <= FMath::Square(192.0f))
        {
            const FVector2D TowardStreet(
                NearestBackbonePoint.X - POI.Position.X,
                NearestBackbonePoint.Y - POI.Position.Y);
            const float DistanceToStreet = TowardStreet.Size();
            const FVector2D Direction = TowardStreet / FMath::Max(DistanceToStreet, 1.0f);
            const FVector Entrance(
                POI.Position.X + Direction.X * EntranceInset,
                POI.Position.Y + Direction.Y * EntranceInset,
                POI.Position.Z);

            if (DistanceToStreet <= EntranceInset + 6.0f &&
                IsDrySegment(NearestBackbonePoint, POI.Position))
            {
                // The existing street already reaches the dry side of the POI.
                bConnectedToRoad = true;
            }
            else
            {
                bConnectedToRoad = BuildRoad(
                    NearestBackboneId, NearestBackbonePoint,
                    POI.Id, Entrance, EVoxelRWGRoadType::Local, Generator);
            }
        }

        if (bConnectedToRoad)
        {
            continue;
        }

        // Safe fallback: connect from the assigned settlement only if a fully
        // dry local path exists. If water separates the POI, leave it unconnected
        // rather than drawing an implausible driveway across the water.
        const FVector2D TowardHub(
            Hub->Position.X - POI.Position.X,
            Hub->Position.Y - POI.Position.Y);
        const float DistanceToHub = TowardHub.Size();
        const FVector2D HubDirection = TowardHub / FMath::Max(DistanceToHub, 1.0f);
        const FVector HubEntrance(
            POI.Position.X + HubDirection.X * EntranceInset,
            POI.Position.Y + HubDirection.Y * EntranceInset,
            POI.Position.Z);

        if (DistanceToHub <= EntranceInset + 6.0f &&
            IsDrySegment(Hub->Position, POI.Position))
        {
            continue;
        }

        BuildRoad(Hub->Id, Hub->Position, POI.Id, HubEntrance,
            EVoxelRWGRoadType::Local, Generator);
    }
}

FString FVoxelRWGPlanner::SettlementTypeName(EVoxelRWGSettlementType Type)
{
    switch (Type)
    {
    case EVoxelRWGSettlementType::City: return TEXT("City");
    case EVoxelRWGSettlementType::Town: return TEXT("Town");
    case EVoxelRWGSettlementType::Village: return TEXT("Village");
    case EVoxelRWGSettlementType::Rural: return TEXT("Rural");
    case EVoxelRWGSettlementType::Industrial: return TEXT("Industrial");
    default: return TEXT("Village");
    }
}
FString FVoxelRWGPlanner::POITypeName(EVoxelRWGPOIType Type)
{
    switch (Type)
    {
    case EVoxelRWGPOIType::Residential: return TEXT("Residential");
    case EVoxelRWGPOIType::Commercial: return TEXT("Commercial");
    case EVoxelRWGPOIType::Industrial: return TEXT("Industrial");
    case EVoxelRWGPOIType::Farm: return TEXT("Farm");
    case EVoxelRWGPOIType::Civic: return TEXT("Civic");
    case EVoxelRWGPOIType::Trader: return TEXT("Trader");
    case EVoxelRWGPOIType::Utility: return TEXT("Utility");
    case EVoxelRWGPOIType::Ruin: return TEXT("Ruin");
    default: return TEXT("Residential");
    }
}
FString FVoxelRWGPlanner::RoadTypeName(EVoxelRWGRoadType Type)
{
    switch (Type)
    {
    case EVoxelRWGRoadType::Main: return TEXT("Main");
    case EVoxelRWGRoadType::Connector: return TEXT("Connector");
    case EVoxelRWGRoadType::Rural: return TEXT("Rural");
    case EVoxelRWGRoadType::Local: return TEXT("Local");
    default: return TEXT("Connector");
    }
}
FString FVoxelRWGPlanner::BiomeName(EVoxelBiome Biome)
{
    switch (Biome)
    {
    case EVoxelBiome::Forest: return TEXT("Forest");
    case EVoxelBiome::Desert: return TEXT("Desert");
    case EVoxelBiome::Snow: return TEXT("Snow");
    case EVoxelBiome::Mountain: return TEXT("MountainLegacy");
    case EVoxelBiome::Plains:
    default: return TEXT("Plains");
    }
}

FString FVoxelRWGPlanner::CellTypeName(EVoxelRWGCellType Type)
{
    switch (Type)
    {
    case EVoxelRWGCellType::Wilderness: return TEXT("Wilderness");
    case EVoxelRWGCellType::Rural: return TEXT("Rural");
    case EVoxelRWGCellType::Town: return TEXT("Town");
    case EVoxelRWGCellType::City: return TEXT("City");
    case EVoxelRWGCellType::Industrial: return TEXT("Industrial");
    default: return TEXT("Wilderness");
    }
}

FString FVoxelRWGPlanner::GetSummary() const
{
    int32 Main = 0, Connector = 0, Local = 0;
    int32 CityCells = 0, TownCells = 0, RuralCells = 0, IndustrialCells = 0, WildernessCells = 0;
    for (const FVoxelRWGRoad& Road : Roads)
    {
        if (Road.Type == EVoxelRWGRoadType::Main) ++Main;
        else if (Road.Type == EVoxelRWGRoadType::Connector) ++Connector;
        else ++Local;
    }
    for (const FVoxelRWGCell& Cell : Cells)
    {
        switch (Cell.Type)
        {
        case EVoxelRWGCellType::City: ++CityCells; break;
        case EVoxelRWGCellType::Town: ++TownCells; break;
        case EVoxelRWGCellType::Rural: ++RuralCells; break;
        case EVoxelRWGCellType::Industrial: ++IndustrialCells; break;
        case EVoxelRWGCellType::Wilderness: default: ++WildernessCells; break;
        }
    }
    return FString::Printf(
        TEXT("RWG plan seed=%d; cells=%d (%dx%d @ %d blocks; city=%d, town=%d, rural=%d, industrial=%d, wilderness=%d); settlements=%d/%d; connected hubs=%d/%d; failed hub links=%d; POIs=%d/%d; roads=%d (main=%d, connector=%d, local=%d); route grid=%dx%d @ %d blocks"),
        Settings.Seed, Cells.Num(), CellColumns, CellRows, Settings.CellSizeBlocks,
        CityCells, TownCells, RuralCells, IndustrialCells, WildernessCells,
        Settlements.Num(), Settings.TargetSettlementCount,
        ConnectedSettlementCount, Settlements.Num(), FailedSettlementRoadCount,
        POIs.Num(), Settings.TargetPOICount, Roads.Num(), Main, Connector, Local,
        GridWidth, GridHeight, Settings.GridSpacing);
}

FString FVoxelRWGPlanner::ToCSV() const
{
    FString CSV = TEXT("Record,Id,Subtype,FromId,ToId,PointIndex,X,Y,Z,Biome,SettlementId,Radius,Value\n");
    CSV += FString::Printf(TEXT("META,0,Seed,,,,,,,,,,%d\n"), Settings.Seed);
    CSV += FString::Printf(TEXT("META,0,WorldSize,,,,%d,%d,,,,,%d\n"),
        Settings.WorldBlocksX, Settings.WorldBlocksY, Settings.GridSpacing);
    CSV += FString::Printf(TEXT("META,0,CellSize,,,,,,,,,,%d\n"), Settings.CellSizeBlocks);

    for (const FVoxelRWGCell& Cell : Cells)
    {
        CSV += FString::Printf(TEXT("CELL,%d,%s,%d,%d,%d,%s,%s,%s,%s,%d,%s,%s\n"),
            Cell.Id,
            *CellTypeName(Cell.Type),
            Cell.GridCoord.X,
            Cell.GridCoord.Y,
            Cell.bBuildable ? 1 : 0,
            *CsvNumber(Cell.Center.X),
            *CsvNumber(Cell.Center.Y),
            *CsvNumber(Cell.Center.Z),
            *BiomeName(Cell.Biome),
            Cell.SettlementId,
            *CsvNumber(float(Settings.CellSizeBlocks)),
            *CsvNumber(Cell.UrbanScore));
    }

    for (const FVoxelRWGSettlement& S : Settlements)
    {
        CSV += FString::Printf(TEXT("SETTLEMENT,%d,%s,,,,%s,%s,%s,%s,,%s,\n"),
            S.Id, *SettlementTypeName(S.Type), *CsvNumber(S.Position.X), *CsvNumber(S.Position.Y),
            *CsvNumber(S.Position.Z), *BiomeName(S.Biome), *CsvNumber(S.Radius));
    }
    for (const FVoxelRWGPOI& P : POIs)
    {
        CSV += FString::Printf(TEXT("POI,%d,%s,,,,%s,%s,%s,%s,%d,%s,\n"),
            P.Id, *POITypeName(P.Type), *CsvNumber(P.Position.X), *CsvNumber(P.Position.Y),
            *CsvNumber(P.Position.Z), *BiomeName(P.Biome), P.SettlementId, *CsvNumber(P.Radius));
    }
    for (const FVoxelRWGRoad& R : Roads)
    {
        for (int32 I = 0; I < R.Points.Num(); ++I)
        {
            const FVector& P = R.Points[I];
            CSV += FString::Printf(TEXT("ROAD_POINT,%d,%s,%d,%d,%d,%s,%s,%s,,,,\n"),
                R.Id, *RoadTypeName(R.Type), R.FromId, R.ToId, I,
                *CsvNumber(P.X), *CsvNumber(P.Y), *CsvNumber(P.Z));
        }
    }
    return CSV;
}
