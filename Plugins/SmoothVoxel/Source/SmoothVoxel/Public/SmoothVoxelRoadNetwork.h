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
    TSharedPtr<const FSVoxelRoadBlockMap, ESPMode::ThreadSafe> RoadBlockSnapshot;
    // 32x32-cell XY buckets make per-chunk road snapshot extraction independent
    // of the total number of roads in the full world.
    TMap<FIntPoint, TArray<FIntVector>> RoadBlockBins;

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
