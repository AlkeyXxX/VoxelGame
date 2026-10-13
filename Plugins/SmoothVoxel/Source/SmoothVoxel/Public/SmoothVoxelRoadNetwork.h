#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmoothVoxelGenerator.h"
#include "VoxelTypes.h"
#include "SmoothVoxelRoadNetwork.generated.h"

class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;

/**
 * Deterministic road ribbons linking the same seed-stable candidate sites used
 * for POI plateaus. The road mesh samples the analytical voxel terrain height,
 * so it can be rebuilt independently from Octree chunk streaming.
 */
UCLASS()
class SMOOTHVOXEL_API ASmoothVoxelRoadNetwork : public AActor
{
    GENERATED_BODY()

public:
    ASmoothVoxelRoadNetwork();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Roads")
    UProceduralMeshComponent* Mesh = nullptr;

    void ConfigureRoadNetwork(
        const FVector& WorldCenter,
        float WorldSize,
        float SiteSpacing,
        float RoadWidth,
        float SampleSpacing,
        float SurfaceOffset,
        UMaterialInterface* InDirtRoadMaterial,
        UMaterialInterface* InAsphaltRoadMaterial,
        const FSVoxelGenerationSettings& GenerationSettings,
        bool bEnableCollision);

    /** Full immutable snapshot used by interaction ray-marching. */
    TSharedPtr<const FSVoxelRoadBlockMap, ESPMode::ThreadSafe> GetRoadBlockSnapshot() const
    {
        return RoadBlockSnapshot;
    }

    /** Continuous road deck heights, shared with terrain density workers. */
    TSharedPtr<const FSVoxelRoadSurfaceHeightMap, ESPMode::ThreadSafe> GetRoadSurfaceHeightSnapshot() const
    {
        return RoadSurfaceHeightSnapshot;
    }

    /** Full terrain-native fill heights for interaction density samples. */
    TSharedPtr<const FSVoxelRoadEmbankmentHeightMap, ESPMode::ThreadSafe> GetRoadEmbankmentFillSnapshot() const
    {
        return RoadEmbankmentFillHeightSnapshot;
    }

    /** Return only natural-landscape fill columns near a chunk plus an LOD border. */
    TSharedPtr<const FSVoxelRoadEmbankmentHeightMap, ESPMode::ThreadSafe> GetRoadEmbankmentFillSnapshotForChunk(
        const FIntVector& ChunkOriginCells,
        int32 CellsPerAxis,
        float BaseVoxelSize,
        float VoxelSize) const;

    TSharedPtr<const FSVoxelRoadSampleBinSet, ESPMode::ThreadSafe> GetRoadSampleBinsSnapshot() const
    {
        return RoadSampleBinsSnapshot;
    }

    /** Return only road cells that overlap a chunk plus a small LOD/transition border. */
    TSharedPtr<const FSVoxelRoadBlockMap, ESPMode::ThreadSafe> GetRoadBlockSnapshotForChunk(
        const FIntVector& ChunkOriginCells,
        int32 CellsPerAxis,
        float BaseVoxelSize,
        float VoxelSize) const;

    UMaterialInterface* GetActiveDirtRoadMaterial() const { return ActiveDirtRoadMaterial; }
    UMaterialInterface* GetActiveAsphaltRoadMaterial() const { return ActiveAsphaltRoadMaterial; }

    bool GetRoadBlockTypeAtCell(const FIntVector& Cell, EVoxelBlock& OutBlock) const;

private:
    void BuildRoadMesh(
        const FVector& WorldCenter,
        float WorldSize,
        float SiteSpacing,
        float RoadWidth,
        float SampleSpacing,
        float SurfaceOffset,
        const FSVoxelGenerationSettings& GenerationSettings,
        bool bEnableCollision);

    UPROPERTY()
    UMaterialInterface* FallbackRoadMaterial = nullptr;

    UPROPERTY()
    UMaterialInterface* FallbackAsphaltRoadMaterial = nullptr;

    UPROPERTY()
    UMaterialInterface* ActiveDirtRoadMaterial = nullptr;

    UPROPERTY()
    UMaterialInterface* ActiveAsphaltRoadMaterial = nullptr;

    TMap<FIntVector, EVoxelBlock> RoadBlockTypes;
    TMap<FIntVector, double> RoadSurfaceHeights;
    TMap<FIntPoint, double> RoadEmbankmentFillHeights;
    TSharedPtr<const FSVoxelRoadBlockMap, ESPMode::ThreadSafe> RoadBlockSnapshot;
    TSharedPtr<const FSVoxelRoadSurfaceHeightMap, ESPMode::ThreadSafe> RoadSurfaceHeightSnapshot;
    TSharedPtr<const FSVoxelRoadEmbankmentHeightMap, ESPMode::ThreadSafe> RoadEmbankmentFillHeightSnapshot;
    TSharedPtr<const FSVoxelRoadSampleBinSet, ESPMode::ThreadSafe> RoadSampleBinsSnapshot;
    // 32x32-cell XY buckets make per-chunk snapshot extraction independent
    // of the total number of road and fill columns in the full world.
    TMap<FIntPoint, TArray<FIntVector>> RoadBlockBins;
    TMap<FIntPoint, TArray<FIntPoint>> RoadEmbankmentFillBins;

    // Seed-stable landmarks are instanced so large worlds do not spawn hundreds
    // of independent actors.
    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* POIWoodCubes = nullptr;
    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* POIStoneCubes = nullptr;
    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* POIDarkCubes = nullptr;
    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* POIRoofCones = nullptr;
    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* POIWoodCylinders = nullptr;
    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* POIStoneCylinders = nullptr;
    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* POIStoneSpheres = nullptr;
};
