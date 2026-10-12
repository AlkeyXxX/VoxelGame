#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmoothVoxelGenerator.h"
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
        UMaterialInterface* InRoadMaterial,
        const FSVoxelGenerationSettings& GenerationSettings,
        bool bEnableCollision);

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
    UMaterialInterface* ActiveRoadMaterial = nullptr;

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
