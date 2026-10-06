#include "SmoothVoxelChunk.h"

#include "ProceduralMeshComponent.h"
#include "SmoothVoxelGenerator.h"
#include "Async/AsyncWork.h"
#include "Async/Async.h"
#include "Materials/MaterialInterface.h"

namespace
{
    class FSVoxelChunkTask : public FNonAbandonableTask
    {
        friend class FAsyncTask<FSVoxelChunkTask>;

    public:
        FSVoxelGenerationSettings Settings;
        TSharedPtr<FSVoxelMeshData, ESPMode::ThreadSafe> Result;
        TWeakObjectPtr<ASmoothVoxelChunk> Owner;

        FSVoxelChunkTask(
            const FSVoxelGenerationSettings& InSettings,
            ASmoothVoxelChunk* InOwner)
            : Settings(InSettings)
            , Result(MakeShared<FSVoxelMeshData, ESPMode::ThreadSafe>())
            , Owner(InOwner)
        {
        }

        void DoWork()
        {
            FSVoxelGenerator::Generate(Settings, *Result);

            TSharedPtr<FSVoxelMeshData, ESPMode::ThreadSafe> LocalResult = Result;
            TWeakObjectPtr<ASmoothVoxelChunk> LocalOwner = Owner;

            AsyncTask(ENamedThreads::GameThread,
                [LocalOwner, LocalResult]()
                {
                    if (!LocalOwner.IsValid())
                    {
                        return;
                    }

                    LocalOwner->ApplyMesh(MoveTemp(*LocalResult));
                });
        }

        FORCEINLINE TStatId GetStatId() const
        {
            RETURN_QUICK_DECLARE_CYCLE_STAT(
                FSVoxelChunkTask,
                STATGROUP_ThreadPoolAsyncTasks);
        }
    };
}

ASmoothVoxelChunk::ASmoothVoxelChunk()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("VoxelMesh"));
    RootComponent = Mesh;

    Mesh->bUseAsyncCooking = true;
    Mesh->bUseComplexAsSimpleCollision = true;
    Mesh->SetMobility(EComponentMobility::Static);
}

void ASmoothVoxelChunk::BeginPlay()
{
    Super::BeginPlay();

    if (bGenerateOnBeginPlay)
    {
        GenerateChunk();
    }
}

void ASmoothVoxelChunk::GenerateChunk()
{
    if (bGenerationStarted)
    {
        return;
    }

    bGenerationStarted = true;
    bGenerationComplete = false;

    FSVoxelGenerationSettings S;

    S.CellsPerAxis = CellsPerAxis;
    S.VoxelSize = VoxelSize;
    S.IsoLevel = IsoLevel;

    S.Seed = Seed;

    S.BaseHeight = BaseHeight;
    S.HeightAmplitude = HeightAmplitude;
    S.NoiseScale = NoiseScale;

    S.DetailScale = DetailScale;
    S.DetailAmplitude = DetailAmplitude;

    S.CaveScale = CaveScale;
    S.CaveThreshold = CaveThreshold;
    S.CaveStrength = bEnableCaves ? CaveStrength : 0.0f;
    S.bEnableCaves = bEnableCaves;
    S.ChunkWorldOrigin = GetActorLocation();

    FAsyncTask<FSVoxelChunkTask>* Task =
        new FAsyncTask<FSVoxelChunkTask>(S, this);

    Task->StartBackgroundTask();

    AsyncTask(
        ENamedThreads::AnyThread,
        [Task]()
        {
            while (!Task->IsDone())
            {
                FPlatformProcess::SleepNoStats(0.001f);
            }

            delete Task;
        });
}

void ASmoothVoxelChunk::ApplyMesh(FSVoxelMeshData&& Data)
{
    check(IsInGameThread());

    if (Data.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("SmoothVoxel: generated empty chunk."));
        bGenerationComplete = true;
        return;
    }

    if (Material)
    {
        Mesh->SetMaterial(0, Material);
    }

    Mesh->CreateMeshSection_LinearColor(
        0,
        Data.Vertices,
        Data.Triangles,
        Data.Normals,
        Data.UV0,
        TArray<FVector2D>(),
        TArray<FVector2D>(),
        TArray<FVector2D>(),
        TArray<FLinearColor>(),
        TArray<FProcMeshTangent>(),
        bGenerateCollision);

    bGenerationComplete = true;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("SmoothVoxel: chunk ready. Vertices=%d Triangles=%d"),
        Data.Vertices.Num(),
        Data.Triangles.Num() / 3);
}
