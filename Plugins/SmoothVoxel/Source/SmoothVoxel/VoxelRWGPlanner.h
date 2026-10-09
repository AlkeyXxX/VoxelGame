#pragma once
#include "CoreMinimal.h"
#include "VoxelWorldGenerator.h"

// Map-wide RWG coordinates are in voxel blocks, not Unreal centimeters.
enum class EVoxelRWGSettlementType : uint8 { City, Town, Village, Rural, Industrial };
enum class EVoxelRWGPOIType : uint8 { Residential, Commercial, Industrial, Farm, Civic, Trader, Utility, Ruin };
enum class EVoxelRWGRoadType : uint8 { Main, Connector, Local };

struct FVoxelRWGPlanSettings
{
    int32 Seed = 1337;
    int32 WorldBlocksX = 4096;
    int32 WorldBlocksY = 4096;
    int32 SeaLevel = 10;
    int32 GridSpacing = 32;
    int32 TargetSettlementCount = 12;
    int32 TargetPOICount = 72;
    float MinimumSettlementDistance = 540.0f;
    float MinimumPOIDistance = 42.0f;
    float EdgeMargin = 96.0f;
};

struct FVoxelRWGSettlement
{
    int32 Id = INDEX_NONE;
    EVoxelRWGSettlementType Type = EVoxelRWGSettlementType::Village;
    FVector Position = FVector::ZeroVector; // block-space XYZ
    EVoxelBiome Biome = EVoxelBiome::Plains;
    float Radius = 180.0f;
};

struct FVoxelRWGPOI
{
    int32 Id = INDEX_NONE;
    int32 SettlementId = INDEX_NONE;
    EVoxelRWGPOIType Type = EVoxelRWGPOIType::Residential;
    FVector Position = FVector::ZeroVector; // block-space XYZ
    EVoxelBiome Biome = EVoxelBiome::Plains;
    float Radius = 16.0f;
};

struct FVoxelRWGRoad
{
    int32 Id = INDEX_NONE;
    int32 FromId = INDEX_NONE;
    int32 ToId = INDEX_NONE;
    EVoxelRWGRoadType Type = EVoxelRWGRoadType::Connector;
    TArray<FVector> Points; // block-space XYZ, terrain-following
};

/*
 * Deterministic first-pass RWG planner: settlement placement, POI clusters,
 * a connected road backbone and terrain-costed A* road polylines. Data only:
 * no actor spawning or voxel mutations in this pass.
 */
class FVoxelRWGPlanner
{
public:
    bool Generate(const FVoxelRWGPlanSettings& InSettings, const FVoxelWorldGenerator& Generator);
    const TArray<FVoxelRWGSettlement>& GetSettlements() const { return Settlements; }
    const TArray<FVoxelRWGPOI>& GetPOIs() const { return POIs; }
    const TArray<FVoxelRWGRoad>& GetRoads() const { return Roads; }
    FString ToCSV() const;
    FString GetSummary() const;

private:
    FVoxelRWGPlanSettings Settings;
    TArray<FVoxelRWGSettlement> Settlements;
    TArray<FVoxelRWGPOI> POIs;
    TArray<FVoxelRWGRoad> Roads;
    int32 GridWidth = 0;
    int32 GridHeight = 0;
    TArray<float> GridHeights;
    TArray<float> GridCosts;

    int32 GridIndex(int32 X, int32 Y) const;
    FVector GridPosition(int32 X, int32 Y) const;
    int32 WorldToGridX(float X) const;
    int32 WorldToGridY(float Y) const;
    bool IsValidSite(const FVector2D& Position, const FVoxelWorldGenerator& Generator, float MaxGrade) const;
    void BuildTerrainCostField(const FVoxelWorldGenerator& Generator);
    void PlaceSettlements(const FVoxelWorldGenerator& Generator);
    void PlacePOIs(const FVoxelWorldGenerator& Generator);
    void BuildRoadNetwork();
    bool BuildRoad(int32 FromId, const FVector& From, int32 ToId, const FVector& To, EVoxelRWGRoadType Type);
    bool FindPath(int32 StartX, int32 StartY, int32 GoalX, int32 GoalY, TArray<int32>& OutPath) const;
    int32 FindNearestWalkable(int32 X, int32 Y) const;
    FVector GetBlockPosition(float X, float Y, const FVoxelWorldGenerator& Generator) const;

    static FString SettlementTypeName(EVoxelRWGSettlementType Type);
    static FString POITypeName(EVoxelRWGPOIType Type);
    static FString RoadTypeName(EVoxelRWGRoadType Type);
    static FString BiomeName(EVoxelBiome Biome);
};
