#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "SmoothVoxelTypes.generated.h"

UENUM(BlueprintType)
enum class ESVoxelBiome : uint8
{
	Plains UMETA(DisplayName = "Plains"),
	Forest UMETA(DisplayName = "Forest"),
	Desert UMETA(DisplayName = "Desert"),
	Mountain UMETA(DisplayName = "Mountain"),
	Snow UMETA(DisplayName = "Snow"),
	// Appended values preserve the serialized IDs of the original five biomes.
	BurntForest UMETA(DisplayName = "Burnt Forest"),
	Wasteland UMETA(DisplayName = "Wasteland")
};

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

    // UV channel 1 carries the soil/dirt-road layer weight in X.
    UPROPERTY()
    TArray<FVector2D> UV1;

    UPROPERTY()
    TArray<FLinearColor> VertexColors;

    UPROPERTY()
    TArray<FProcMeshTangent> Tangents;

    void Reset()
    {
        Vertices.Reset();
        Triangles.Reset();
        Normals.Reset();
        UV0.Reset();
        UV1.Reset();
        VertexColors.Reset();
        Tangents.Reset();
    }

    bool IsEmpty() const
    {
        return Vertices.Num() == 0 || Triangles.Num() == 0;
    }
};
