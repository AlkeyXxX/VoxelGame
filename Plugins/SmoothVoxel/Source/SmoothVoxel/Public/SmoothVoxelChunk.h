#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmoothVoxelTypes.h"
#include "SmoothVoxelChunk.generated.h"

class UProceduralMeshComponent;

UCLASS(BlueprintType, Blueprintable)
class SMOOTHVOXEL_API ASmoothVoxelChunk : public AActor
{
    GENERATED_BODY()

public:
    ASmoothVoxelChunk();

    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel")
    UProceduralMeshComponent* Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Chunk", meta=(ClampMin="4", ClampMax="128"))
    int32 CellsPerAxis = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Chunk", meta=(ClampMin="1.0"))
    float VoxelSize = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    float IsoLevel = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    float BaseHeight = 900.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    float HeightAmplitude = 850.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    float NoiseScale = 0.0045f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    float DetailScale = 0.018f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Terrain")
    float DetailAmplitude = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
    bool bEnableCaves = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
    float CaveScale = 0.012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
    float CaveThreshold = 0.64f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Caves")
    float CaveStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Rendering")
    UMaterialInterface* Material = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Generation")
    bool bGenerateOnBeginPlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Collision")
    bool bGenerateCollision = true;

    UFUNCTION(BlueprintCallable, Category="Voxel")
    void GenerateChunk();

    UFUNCTION(BlueprintPure, Category="Voxel")
    bool IsGenerationComplete() const { return bGenerationComplete; }

private:
    bool bGenerationComplete = false;
    bool bGenerationStarted = false;

public:
    void ApplyMesh(FSVoxelMeshData&& Data);
};
