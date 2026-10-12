#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmoothVoxelGenerator.h"
#include "VoxelTypes.h"
#include "SmoothVoxelRoadNetwork.generated.h"

class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UPrimitiveComponent;

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

    /** Resolve a destructible road-block HISM hit back to its world-aligned voxel cell. */
    bool GetRoadBlockForHit(
        const UPrimitiveComponent* HitComponent,
        int32 InstanceIndex,
        FIntVector& OutCell,
        EVoxelBlock& OutBlock) const;

    /** Remove only the hit road block; remaining instances retain their cell mapping. */
    bool RemoveRoadBlockForHit(
        const UPrimitiveComponent* HitComponent,
        int32 InstanceIndex);

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

    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* DirtRoadBlocks = nullptr;

    UPROPERTY()
    UHierarchicalInstancedStaticMeshComponent* AsphaltRoadBlocks = nullptr;

    // Instance indices returned by the HISM hit result map to these cells.
    TArray<FIntVector> DirtRoadBlockCells;
    TArray<FIntVector> AsphaltRoadBlockCells;
    TMap<FIntVector, EVoxelBlock> RoadBlockTypes;

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
