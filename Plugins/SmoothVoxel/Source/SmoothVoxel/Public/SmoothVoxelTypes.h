#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "SmoothVoxelTypes.generated.h"

USTRUCT(BlueprintType)
struct FSVoxelMeshData
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FVector> Vertices;

    UPROPERTY()
    TArray<int32> Triangles;

    UPROPERTY()
    TArray<FVector> Normals;

    UPROPERTY()
    TArray<FVector2D> UV0;

    UPROPERTY()
    TArray<FProcMeshTangent> Tangents;

    void Reset()
    {
        Vertices.Reset();
        Triangles.Reset();
        Normals.Reset();
        UV0.Reset();
        Tangents.Reset();
    }

    bool IsEmpty() const
    {
        return Vertices.Num() == 0 || Triangles.Num() == 0;
    }
};
