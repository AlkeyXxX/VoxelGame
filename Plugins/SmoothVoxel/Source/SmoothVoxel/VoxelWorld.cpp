
#include "VoxelWorld.h"
#include "VoxelChunk.h"
#include "VoxelWorldGenerator.h"
#include "VoxelRWGPlanner.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "DrawDebugHelpers.h"
#include "Materials/Material.h"
#include "VoxelWorldSaveGame.h"
#include "VoxelBlockLibrary.h"
#include "VoxelInventoryComponent.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "InputCoreTypes.h"

#include "Kismet/GameplayStatics.h"


namespace
{
    /*
     * Floor division для отрицательных координат.
     *
     * Обычный C++ int division обрезает к нулю,
     * а нам нужно математическое floor division.
     */
    FORCEINLINE int32 FastFloorDiv(
        int32 A,
        int32 B)
    {
        check(B > 0);

        if (A >= 0)
        {
            return A / B;
        }

        return -(
            ((-A) + B - 1) / B
        );
    }


    /*
     * Положительный modulo.
     */
    FORCEINLINE int32 PositiveModulo(
        int32 A,
        int32 B)
    {
        check(B > 0);

        const int32 Result = A % B;

        return Result < 0
            ? Result + B
            : Result;
    }


    /*
     * Получить ray из центра экрана.
     *
     * Это правильный вариант для UE 4.27.
     */
    bool GetCenterScreenRay(
        APlayerController* PC,
        FVector& OutStart,
        FVector& OutDirection)
    {
        if (!PC)
        {
            return false;
        }


        int32 ViewportSizeX = 0;
        int32 ViewportSizeY = 0;


        PC->GetViewportSize(
            ViewportSizeX,
            ViewportSizeY);


        if (ViewportSizeX <= 0 ||
            ViewportSizeY <= 0)
        {
            return false;
        }


        const float ScreenX =
            static_cast<float>(ViewportSizeX) * 0.5f;

        const float ScreenY =
            static_cast<float>(ViewportSizeY) * 0.5f;


        return PC->DeprojectScreenPositionToWorld(
            ScreenX,
            ScreenY,
            OutStart,
            OutDirection);
    }

    /*
     * Generate one voxel from a precomputed global column profile. Chunk
     * generation and Marching Cubes halo sampling share this helper so
     * shorelines and procedural riverbeds agree exactly at chunk borders.
     */
    uint8 GetGeneratedBlockFromColumn(
        int32 TerrainHeight,
        int32 EffectiveHeight,
        int32 WaterSurfaceBlockZ,
        EVoxelBiome Biome,
        EVoxelLandform Landform,
        int32 WorldZ,
        int32 SeaLevel,
        int32 BeachWidth)
    {
        if (WorldZ > EffectiveHeight)
        {
            return WaterSurfaceBlockZ != INDEX_NONE &&
                   WorldZ <= WaterSurfaceBlockZ
                ? uint8(EVoxelBlock::Water)
                : uint8(EVoxelBlock::Air);
        }

        if (WorldZ == EffectiveHeight)
        {
            const bool bCarvedWaterBed =
                WaterSurfaceBlockZ != INDEX_NONE &&
                EffectiveHeight < WaterSurfaceBlockZ;

            if (bCarvedWaterBed)
            {
                return uint8(EVoxelBlock::Sand);
            }

            const bool bBeach =
                Biome != EVoxelBiome::Snow &&
                TerrainHeight < SeaLevel &&
                TerrainHeight >= SeaLevel - BeachWidth;

            if (bBeach)
            {
                return uint8(EVoxelBlock::Sand);
            }

            if (Biome == EVoxelBiome::Snow)
            {
                return uint8(EVoxelBlock::Snow);
            }

            if (Landform == EVoxelLandform::Mountains)
            {
                return Biome == EVoxelBiome::Desert
                    ? uint8(EVoxelBlock::Sandstone)
                    : uint8(EVoxelBlock::Grass);
            }

            if (Biome == EVoxelBiome::Desert)
            {
                return uint8(EVoxelBlock::Sand);
            }

            return uint8(EVoxelBlock::Grass);
        }

        if (Biome == EVoxelBiome::Desert &&
            Landform == EVoxelLandform::Mountains &&
            WorldZ >= EffectiveHeight - 5)
        {
            return uint8(EVoxelBlock::Sandstone);
        }

        if (Biome == EVoxelBiome::Desert &&
            WorldZ >= EffectiveHeight - 3)
        {
            return uint8(EVoxelBlock::Sand);
        }

        if (Landform == EVoxelLandform::Mountains &&
            WorldZ >= EffectiveHeight - 3)
        {
            return Biome == EVoxelBiome::Snow
                ? uint8(EVoxelBlock::Stone)
                : uint8(EVoxelBlock::Grass);
        }

        if (WorldZ >= EffectiveHeight - 3)
        {
            return uint8(EVoxelBlock::Dirt);
        }

        return uint8(EVoxelBlock::Stone);
    }


    /*
     * Pure voxel-data generation. This function deliberately uses no
     * UObject or AVoxelWorld members, so it is safe on a worker thread.
     */
    void BuildGeneratedChunkData(
        const FVoxelWorldGenerator& Generator,
        const FIntVector& ChunkCoord,
        int32 ChunkSize,
        int32 SeaLevel,
        int32 BeachWidth,
        const TMap<int32, uint8>& ChunkModifications,
        const TSharedPtr<TMap<FIntPoint, FVoxelRWGRoadStamp>, ESPMode::ThreadSafe>& RoadStamps,
        const TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe>& CancellationToken,
        TArray<uint8>& OutBlocks,
        TArray<uint8>& OutBaseBlocks,
        TArray<uint8>& OutBiomes,
        TArray<uint8>& OutModificationFlags)
    {
        const int32 BlockCount =
            ChunkSize * ChunkSize * ChunkSize;

        OutBlocks.SetNumZeroed(BlockCount);
        OutBaseBlocks.SetNumZeroed(BlockCount);
        OutBiomes.SetNumZeroed(BlockCount);
        OutModificationFlags.SetNumZeroed(BlockCount);

        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            if (CancellationToken.IsValid() &&
                static_cast<bool>(*CancellationToken))
            {
                return;
            }

            for (int32 X = 0; X < ChunkSize; ++X)
            {
                const int32 WorldX =
                    ChunkCoord.X * ChunkSize + X;

                const int32 WorldY =
                    ChunkCoord.Y * ChunkSize + Y;

                const float SmoothSurfaceHeight =
                    Generator.GetSurfaceHeightFloat(WorldX, WorldY);

                const int32 Height =
                    FMath::RoundToInt(SmoothSurfaceHeight);

                const FVoxelWaterColumn WaterColumn =
                    Generator.GetWaterColumn(
                        WorldX,
                        WorldY,
                        SmoothSurfaceHeight);

                const int32 WaterSurfaceBlockZ =
                    WaterColumn.WaterSurfaceBlockZ;

                const int32 EffectiveHeight =
                    WaterColumn.EffectiveTerrainHeight;

                const EVoxelBiome Biome =
                    Generator.GetBiome(WorldX, WorldY, Height);

                const EVoxelLandform Landform =
                    Generator.GetLandform(WorldX, WorldY);

                const FVoxelRWGRoadStamp* RoadStamp = nullptr;
                if (WaterSurfaceBlockZ == INDEX_NONE && RoadStamps.IsValid())
                {
                    RoadStamp = RoadStamps->Find(FIntPoint(WorldX, WorldY));
                }

                for (int32 Z = 0; Z < ChunkSize; ++Z)
                {
                    const int32 WorldZ =
                        ChunkCoord.Z * ChunkSize + Z;

                    uint8 Block =
                        GetGeneratedBlockFromColumn(
                            Height,
                            EffectiveHeight,
                            WaterSurfaceBlockZ,
                            Biome,
                            Landform,
                            WorldZ,
                            SeaLevel,
                            BeachWidth);

                    if (RoadStamp && RoadStamp->SurfaceZ >= 0.0f)
                    {
                        const int32 RoadSurfaceBlockZ = FMath::RoundToInt(RoadStamp->SurfaceZ);
                        if (WorldZ > RoadSurfaceBlockZ && WorldZ <= EffectiveHeight)
                        {
                            Block = uint8(EVoxelBlock::Air); // cut high ground
                        }
                        else if (WorldZ == RoadSurfaceBlockZ)
                        {
                            Block = RoadStamp->SurfaceBlock;
                        }
                        else if (WorldZ > EffectiveHeight && WorldZ < RoadSurfaceBlockZ)
                        {
                            Block = RoadStamp->FillBlock; // fill a low road bed
                        }
                    }

                    const int32 LocalIndex =
                        X + Y * ChunkSize + Z * ChunkSize * ChunkSize;

                    OutBaseBlocks[LocalIndex] = Block;
                    OutBiomes[LocalIndex] =
                        Block == uint8(EVoxelBlock::Water)
                            ? 4
                            : uint8(Biome);

                    if (const uint8* ModifiedBlock =
                        ChunkModifications.Find(LocalIndex))
                    {
                        OutBlocks[LocalIndex] = *ModifiedBlock;
                        OutModificationFlags[LocalIndex] = 1;
                    }
                    else
                    {
                        OutBlocks[LocalIndex] = Block;
                        OutModificationFlags[LocalIndex] = 0;
                    }
                }
            }
        }
    }


}

void FVoxelMarchingCubesDataSnapshot::Build(
    FVoxelMarchingCubesBuildInput& OutData) const
{
    OutData.Init(ChunkSize);
    OutData.VoxelSize = VoxelSize;
    OutData.UVScalePerBlock = UVScalePerBlock;
    OutData.CancellationToken = CancellationToken;

    if (ChunkSize <= 0)
    {
        return;
    }

    const int32 Side = ChunkSize + 2;

    OutData.TerrainSurfaceHeights.SetNumZeroed(
        Side * Side);
    OutData.TerrainSurfaceBlocks.Init(
        uint8(EVoxelBlock::Air),
        Side * Side);

    const int32 WorldBlocksX = WorldSizeX * ChunkSize;
    const int32 WorldBlocksY = WorldSizeY * ChunkSize;
    const int32 WorldBlocksZ = WorldSizeZ * ChunkSize;

    /*
     * All work below uses only immutable value data. Surface height,
     * biome, and landform are computed once per XY column rather than once
     * for every Z sample; this is important for a 34^3 MC halo.
     */
    for (int32 Y = -1; Y <= ChunkSize; ++Y)
    {
        if (CancellationToken.IsValid() &&
            static_cast<bool>(*CancellationToken))
        {
            return;
        }

        const int32 WorldY = ChunkCoord.Y * ChunkSize + Y;

        for (int32 X = -1; X <= ChunkSize; ++X)
        {
            const int32 WorldX = ChunkCoord.X * ChunkSize + X;
            const bool bValidColumn =
                WorldX >= 0 && WorldX < WorldBlocksX &&
                WorldY >= 0 && WorldY < WorldBlocksY;

            int32 Height = 0;
            int32 EffectiveHeight = 0;
            int32 WaterSurfaceBlockZ = INDEX_NONE;
            float LocalSurfaceHeight =
                -static_cast<float>(ChunkCoord.Z * ChunkSize) - 2.0f;
            EVoxelBiome Biome = EVoxelBiome::Plains;
            EVoxelLandform Landform = EVoxelLandform::Flatlands;

            if (bValidColumn)
            {
                const float SmoothSurfaceHeight =
                    Generator.GetSurfaceHeightFloat(WorldX, WorldY);

                Height =
                    FMath::RoundToInt(SmoothSurfaceHeight);

                const FVoxelWaterColumn WaterColumn =
                    Generator.GetWaterColumn(
                        WorldX,
                        WorldY,
                        SmoothSurfaceHeight);

                WaterSurfaceBlockZ =
                    WaterColumn.WaterSurfaceBlockZ;

                EffectiveHeight =
                    WaterColumn.EffectiveTerrainHeight;

                LocalSurfaceHeight =
                    WaterColumn.EffectiveSurfaceHeight -
                    static_cast<float>(ChunkCoord.Z * ChunkSize);

                Biome = Generator.GetBiome(WorldX, WorldY, Height);
                Landform = Generator.GetLandform(WorldX, WorldY);
            }

            const FVoxelRWGRoadStamp* RoadStamp = nullptr;
            if (bValidColumn && WaterSurfaceBlockZ == INDEX_NONE && RoadStamps.IsValid())
            {
                RoadStamp = RoadStamps->Find(FIntPoint(WorldX, WorldY));
                if (RoadStamp && RoadStamp->SurfaceZ != INDEX_NONE)
                {
                    LocalSurfaceHeight = static_cast<float>(RoadStamp->SurfaceZ) -
                        static_cast<float>(ChunkCoord.Z * ChunkSize);
                }
            }

            const int32 SurfaceColumnIndex =
                (X + 1) + (Y + 1) * Side;
            OutData.TerrainSurfaceHeights[SurfaceColumnIndex] =
                LocalSurfaceHeight;

            // Pass the exact road surface block to the MC material picker.
            // This keeps road Stone/Dirt visible when neighboring Grass shares
            // the same sampled Z and would otherwise win the old 3x3 tie-break.
            if (RoadStamp &&
                RoadStamp->bRoadSurface &&
                RoadStamp->SurfaceZ >= 0.0f)
            {
                OutData.TerrainSurfaceBlocks[SurfaceColumnIndex] =
                    RoadStamp->SurfaceBlock;
            }

            const int32 SourceChunkX =
                X < 0 ? -1 : (X >= ChunkSize ? 1 : 0);
            const int32 LocalX =
                X < 0 ? ChunkSize - 1 :
                (X >= ChunkSize ? 0 : X);

            const int32 SourceChunkY =
                Y < 0 ? -1 : (Y >= ChunkSize ? 1 : 0);
            const int32 LocalY =
                Y < 0 ? ChunkSize - 1 :
                (Y >= ChunkSize ? 0 : Y);

            for (int32 Z = -1; Z <= ChunkSize; ++Z)
            {
                const int32 WorldZ = ChunkCoord.Z * ChunkSize + Z;
                const int32 SourceChunkZ =
                    Z < 0 ? -1 : (Z >= ChunkSize ? 1 : 0);
                const int32 LocalZ =
                    Z < 0 ? ChunkSize - 1 :
                    (Z >= ChunkSize ? 0 : Z);

                uint8 Block = uint8(EVoxelBlock::Air);

                if (bValidColumn &&
                    WorldZ >= 0 && WorldZ < WorldBlocksZ)
                {
                    const FIntVector SourceChunkCoord(
                        ChunkCoord.X + SourceChunkX,
                        ChunkCoord.Y + SourceChunkY,
                        ChunkCoord.Z + SourceChunkZ);

                    const int32 SourceLocalIndex =
                        LocalX +
                        LocalY * ChunkSize +
                        LocalZ * ChunkSize * ChunkSize;

                    const TMap<int32, uint8>* SourceModifications =
                        ChunkModifications.Find(SourceChunkCoord);

                    const bool bIsModified =
                        SourceModifications &&
                        SourceModifications->Contains(SourceLocalIndex);

                    if (!bIsModified)
                    {
                        Block = GetGeneratedBlockFromColumn(
                            Height,
                            EffectiveHeight,
                            WaterSurfaceBlockZ,
                            Biome,
                            Landform,
                            WorldZ,
                            SeaLevel,
                            BeachWidth);

                        if (RoadStamp && RoadStamp->SurfaceZ >= 0.0f)
                        {
                            const int32 RoadSurfaceBlockZ = FMath::RoundToInt(RoadStamp->SurfaceZ);
                            if (WorldZ > RoadSurfaceBlockZ && WorldZ <= EffectiveHeight)
                            {
                                Block = uint8(EVoxelBlock::Air);
                            }
                            else if (WorldZ == RoadSurfaceBlockZ)
                            {
                                Block = RoadStamp->SurfaceBlock;
                            }
                            else if (WorldZ > EffectiveHeight && WorldZ < RoadSurfaceBlockZ)
                            {
                                Block = RoadStamp->FillBlock;
                            }
                        }
                    }
                }

                const int32 Index =
                    (X + 1) +
                    (Y + 1) * Side +
                    (Z + 1) * Side * Side;

                OutData.Blocks[Index] = Block;
            }
        }
    }
}



AVoxelWorld::AVoxelWorld()
{
    /*
     * Streaming проверяет позицию игрока каждый кадр,
     * но сама тяжёлая работа выполняется только
     * при смене чанка или пока есть недогруженные chunks.
     */
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.0f;
}


void AVoxelWorld::BeginPlay()
{
    Super::BeginPlay();

    /*
     * Debug fly управляется через сам VoxelWorld, поэтому не требуется
     * менять parent у BP_VoxelPlayer / BP_VoxelPlayerController.
     */
    if (APlayerController* PC =
        GetWorld()
            ? GetWorld()->GetFirstPlayerController()
            : nullptr)
    {
        EnableInput(PC);

        /*
         * AVoxelWorld получает только debug input, но не должен переводить
         * PlayerController в UI input mode. Явно возвращаем игровой режим
         * и захват мыши после регистрации debug bindings.
         */
        PC->bShowMouseCursor = false;
        FInputModeGameOnly GameOnlyInput;
        PC->SetInputMode(GameOnlyInput);

        if (InputComponent)
        {
            InputComponent->BindAction(
                TEXT("ToggleDebugFly"),
                IE_Pressed,
                this,
                &AVoxelWorld::ToggleDebugFly);

            InputComponent->BindAction(
                TEXT("GenerateRWGLayout"),
                IE_Pressed,
                this,
                &AVoxelWorld::GenerateRWGLayoutFromInput);

            InputComponent->BindAction(
                TEXT("DebugFlyUp"),
                IE_Pressed,
                this,
                &AVoxelWorld::DebugFlyUpPressed);

            InputComponent->BindAction(
                TEXT("DebugFlyUp"),
                IE_Released,
                this,
                &AVoxelWorld::DebugFlyUpReleased);

            InputComponent->BindAction(
                TEXT("DebugFlyDown"),
                IE_Pressed,
                this,
                &AVoxelWorld::DebugFlyDownPressed);

            InputComponent->BindAction(
                TEXT("DebugFlyDown"),
                IE_Released,
                this,
                &AVoxelWorld::DebugFlyDownReleased);

            InputComponent->BindAction(
                TEXT("DebugFlyBoost"),
                IE_Pressed,
                this,
                &AVoxelWorld::DebugFlyBoostPressed);

            InputComponent->BindAction(
                TEXT("DebugFlyBoost"),
                IE_Released,
                this,
                &AVoxelWorld::DebugFlyBoostReleased);
        }
    }

    GenerateWorld();
}


void AVoxelWorld::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    /*
     * Перед выходом из мира сохраняем накопленные изменения.
     * SaveGame содержит только delta-данные, поэтому для текущего
     * этапа это маленькая операция.
     */
    SaveWorld();

    Super::EndPlay(EndPlayReason);
}


void AVoxelWorld::Tick(
    float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bDebugFlyMode)
    {
        if (APawn* Pawn =
            UGameplayStatics::GetPlayerPawn(
                GetWorld(),
                0))
        {
            Pawn->AddMovementInput(
                FVector::UpVector,
                DebugFlyVerticalInput);
        }
    }

    /*
     * Keep streaming decisions throttled to 10 Hz, but drain the mesh
     * rebuild queue every frame so each frame handles only a small amount
     * of synchronous snapshot work.
     */
    TimeSinceLastStreamingUpdate += DeltaSeconds;
    if (TimeSinceLastStreamingUpdate >= 0.1f)
    {
        TimeSinceLastStreamingUpdate =
            FMath::Fmod(TimeSinceLastStreamingUpdate, 0.1f);
        UpdateChunkStreaming();
    }

    ProcessPendingMeshUploads();
    ProcessPendingChunkMeshRebuilds();
    UpdateUnderwaterEffect();

    if (AutoSaveInterval > 0.0f)
    {
        TimeSinceLastAutoSave += DeltaSeconds;

        if (TimeSinceLastAutoSave >= AutoSaveInterval)
        {
            TimeSinceLastAutoSave = 0.0f;
            SaveWorld();
        }
    }
}


void AVoxelWorld::ConfigureWorldGenerator()
{
    FVoxelWorldGenerationSettings Settings;

    Settings.Seed = Seed;
    Settings.WorldBlocksX =
        FMath::Max(1, WorldSizeX * ChunkSize);
    Settings.WorldBlocksY =
        FMath::Max(1, WorldSizeY * ChunkSize);
    Settings.MaxTerrainHeight =
        FMath::Max(1, WorldSizeZ * ChunkSize - 3);
    Settings.BaseHeight = BaseHeight;
    Settings.HeightVariation = HeightVariation;
    Settings.NoiseScale = NoiseScale;
    Settings.DetailNoiseScale = DetailNoiseScale;
    Settings.DetailHeightVariation = DetailHeightVariation;
    Settings.PlateauScale = PlateauScale;
    Settings.PlateauHeightStep = PlateauHeightStep;
    Settings.PlateauStrength = PlateauStrength;
    Settings.SeaLevel = SeaLevel;
    Settings.TemperatureScale = TemperatureScale;
    Settings.MoistureScale = MoistureScale;

    WorldGenerator.Configure(Settings);
}


/*
 * Calculate the center start point from the same generator used by chunks.
 * Configure here as well because GameMode may request a start location
 * before AVoxelWorld::BeginPlay has finished initializing the world.
 */
FVector AVoxelWorld::GetCenterSpawnLocation()
{
    ConfigureWorldGenerator();

    const int32 WorldBlocksX =
        FMath::Max(1, WorldSizeX * ChunkSize);

    const int32 WorldBlocksY =
        FMath::Max(1, WorldSizeY * ChunkSize);

    const int32 CenterBlockX =
        FMath::Clamp(WorldBlocksX / 2, 0, WorldBlocksX - 1);

    const int32 CenterBlockY =
        FMath::Clamp(WorldBlocksY / 2, 0, WorldBlocksY - 1);

    const int32 SurfaceHeight =
        WorldGenerator.GetSurfaceHeight(
            CenterBlockX,
            CenterBlockY);

    return GetActorLocation() +
        FVector(
            (static_cast<float>(CenterBlockX) + 0.5f) * VoxelSize,
            (static_cast<float>(CenterBlockY) + 0.5f) * VoxelSize,
            (static_cast<float>(SurfaceHeight) + 2.5f) * VoxelSize);
}


void AVoxelWorld::GenerateRWGLayoutFromInput()
{
    GenerateRWGLayoutAndExport();
}


void AVoxelWorld::ClearRWGRoadSurface()
{
    if (RWGRoadMesh)
    {
        RWGRoadMesh->ClearAllMeshSections();
    }
}

void AVoxelWorld::BuildRWGRoadTerrainStamps(const FVoxelRWGPlanner& Planner)
{
    TSharedPtr<TMap<FIntPoint, FVoxelRWGRoadStamp>, ESPMode::ThreadSafe> NewStamps =
        MakeShared<TMap<FIntPoint, FVoxelRWGRoadStamp>, ESPMode::ThreadSafe>();

    const int32 SafeWorldBlocksX = FMath::Max(1, WorldSizeX * ChunkSize);
    const int32 SafeWorldBlocksY = FMath::Max(1, WorldSizeY * ChunkSize);
    const int32 SafeWorldBlocksZ = FMath::Max(2, WorldSizeZ * ChunkSize);
    constexpr float ShoulderWidthBlocks = 3.0f;
    constexpr float LateralSampleStep = 0.75f;

    for (const FVoxelRWGRoad& Road : Planner.GetRoads())
    {
        if (Road.Points.Num() < 2)
        {
            continue;
        }

        // Smooth the continuous ground profile along each road by a weighted
        // ~32-block window. Stop averaging at water/dry transitions so bridge
        // approaches are not pulled toward a riverbed.
        TArray<FVector> StampedRoadPoints = Road.Points;
        TArray<float> RawProfileHeights;
        TArray<uint8> ProfileWaterFlags;
        RawProfileHeights.SetNumZeroed(Road.Points.Num());
        ProfileWaterFlags.SetNumZeroed(Road.Points.Num());

        for (int32 PointIndex = 0; PointIndex < Road.Points.Num(); ++PointIndex)
        {
            const FVector& Point = Road.Points[PointIndex];
            const int32 PX = FMath::Clamp(FMath::RoundToInt(Point.X), 0, SafeWorldBlocksX - 1);
            const int32 PY = FMath::Clamp(FMath::RoundToInt(Point.Y), 0, SafeWorldBlocksY - 1);
            const float NativeHeight = WorldGenerator.GetSurfaceHeightFloat(PX, PY);
            const FVoxelWaterColumn PointWater =
                WorldGenerator.GetWaterColumn(PX, PY, NativeHeight);
            const bool bWater = PointWater.WaterSurfaceBlockZ != INDEX_NONE;
            ProfileWaterFlags[PointIndex] = bWater ? 1 : 0;
            RawProfileHeights[PointIndex] = bWater
                ? Point.Z - 0.8f
                : PointWater.EffectiveSurfaceHeight;
        }

        TArray<float> SmoothedProfileHeights;
        SmoothedProfileHeights.SetNumZeroed(Road.Points.Num());
        constexpr int32 ProfileSmoothRadius = 4;
        for (int32 PointIndex = 0; PointIndex < Road.Points.Num(); ++PointIndex)
        {
            float WeightedHeight = 0.0f;
            float TotalWeight = 0.0f;
            for (int32 Offset = -ProfileSmoothRadius; Offset <= ProfileSmoothRadius; ++Offset)
            {
                const int32 SampleIndex = PointIndex + Offset;
                if (!RawProfileHeights.IsValidIndex(SampleIndex) ||
                    ProfileWaterFlags[SampleIndex] != ProfileWaterFlags[PointIndex])
                {
                    continue;
                }

                const float Weight = float(ProfileSmoothRadius + 1 - FMath::Abs(Offset));
                WeightedHeight += RawProfileHeights[SampleIndex] * Weight;
                TotalWeight += Weight;
            }
            SmoothedProfileHeights[PointIndex] = TotalWeight > 0.0f
                ? WeightedHeight / TotalWeight
                : RawProfileHeights[PointIndex];
            StampedRoadPoints[PointIndex].Z = SmoothedProfileHeights[PointIndex] + 0.8f;
        }

        float WidthBlocks = RWGRoadWidthLocalBlocks;
        uint8 RoadSurfaceBlock = uint8(EVoxelBlock::Dirt);
        uint8 RoadPriority = 1;
        if (Road.Type == EVoxelRWGRoadType::Main)
        {
            WidthBlocks = RWGRoadWidthMainBlocks;
            RoadSurfaceBlock = uint8(EVoxelBlock::Stone);
            RoadPriority = 4;
        }
        else if (Road.Type == EVoxelRWGRoadType::Connector)
        {
            WidthBlocks = RWGRoadWidthConnectorBlocks;
            RoadSurfaceBlock = uint8(EVoxelBlock::Stone);
            RoadPriority = 3;
        }
        else if (Road.Type == EVoxelRWGRoadType::Rural)
        {
            WidthBlocks = RWGRoadWidthRuralBlocks;
            RoadSurfaceBlock = uint8(EVoxelBlock::Dirt);
            RoadPriority = 2;
        }

        const float HalfWidth = FMath::Max(0.5f, WidthBlocks * 0.5f);
        const float StampHalfWidth = HalfWidth + ShoulderWidthBlocks;

        for (int32 SegmentIndex = 1; SegmentIndex < StampedRoadPoints.Num(); ++SegmentIndex)
        {
            const FVector& A = StampedRoadPoints[SegmentIndex - 1];
            const FVector& B = StampedRoadPoints[SegmentIndex];
            const FVector2D Delta(B.X - A.X, B.Y - A.Y);
            const float SegmentLength = Delta.Size();
            if (SegmentLength <= SMALL_NUMBER)
            {
                continue;
            }

            const FVector2D Direction = Delta / SegmentLength;
            const FVector2D Side(-Direction.Y, Direction.X);
            const int32 Steps = FMath::Clamp(FMath::CeilToInt(SegmentLength / 1.5f), 1, 8192);

            for (int32 Step = SegmentIndex == 1 ? 0 : 1; Step <= Steps; ++Step)
            {
                const float T = static_cast<float>(Step) / static_cast<float>(Steps);
                const float CenterX = FMath::Lerp(A.X, B.X, T);
                const float CenterY = FMath::Lerp(A.Y, B.Y, T);
                // The route's continuous height profile is smoothed before
                // this point. Allow controlled terrain cuts/fills without rounding
                // each sample to a block; that removes the washboard effect.
                float CenterSurfaceZ = FMath::Lerp(A.Z, B.Z, T) - 0.8f;
                const int32 CenterBlockX = FMath::Clamp(
                    FMath::RoundToInt(CenterX), 0, SafeWorldBlocksX - 1);
                const int32 CenterBlockY = FMath::Clamp(
                    FMath::RoundToInt(CenterY), 0, SafeWorldBlocksY - 1);
                const float CenterNativeHeight =
                    WorldGenerator.GetSurfaceHeightFloat(CenterBlockX, CenterBlockY);
                const FVoxelWaterColumn CenterWater = WorldGenerator.GetWaterColumn(
                    CenterBlockX, CenterBlockY, CenterNativeHeight);
                if (CenterWater.WaterSurfaceBlockZ == INDEX_NONE)
                {
                    constexpr float MaxCutBlocks = 4.0f;
                    constexpr float MaxFillBlocks = 2.0f;
                    CenterSurfaceZ = FMath::Clamp(
                        CenterSurfaceZ,
                        CenterWater.EffectiveSurfaceHeight - MaxCutBlocks,
                        CenterWater.EffectiveSurfaceHeight + MaxFillBlocks);
                }

                for (float Lateral = -StampHalfWidth;
                     Lateral <= StampHalfWidth + KINDA_SMALL_NUMBER;
                     Lateral += LateralSampleStep)
                {
                    const float SampleX = CenterX + Side.X * Lateral;
                    const float SampleY = CenterY + Side.Y * Lateral;
                    const int32 BlockX = FMath::RoundToInt(SampleX);
                    const int32 BlockY = FMath::RoundToInt(SampleY);
                    if (BlockX < 0 || BlockX >= SafeWorldBlocksX ||
                        BlockY < 0 || BlockY >= SafeWorldBlocksY)
                    {
                        continue;
                    }

                    const float NativeHeight =
                        WorldGenerator.GetSurfaceHeightFloat(BlockX, BlockY);
                    const FVoxelWaterColumn Water =
                        WorldGenerator.GetWaterColumn(BlockX, BlockY, NativeHeight);

                    // Leave water columns intact. A separate raised bridge-deck
                    // mesh spans the water while the road itself stays terrain-integrated.
                    if (Water.WaterSurfaceBlockZ != INDEX_NONE)
                    {
                        continue;
                    }

                    const int32 NativeSurfaceBlockZ = Water.EffectiveTerrainHeight;
                    const float NativeSurfaceZ = Water.EffectiveSurfaceHeight;
                    const bool bRoadSurface = FMath::Abs(Lateral) <= HalfWidth + 0.25f;
                    const float ShoulderAlpha = FMath::Clamp(
                        (FMath::Abs(Lateral) - HalfWidth) / ShoulderWidthBlocks,
                        0.0f, 1.0f);

                    // Keep the lane on its continuous center profile. Only the
                    // shoulders blend back into unmodified land, avoiding a
                    // per-column copy of high-frequency terrain noise.
                    const float StampedHeight = bRoadSurface
                        ? CenterSurfaceZ
                        : FMath::Lerp(CenterSurfaceZ, NativeSurfaceZ, ShoulderAlpha);
                    const float SurfaceZ = FMath::Clamp(
                        StampedHeight, 1.0f, static_cast<float>(SafeWorldBlocksZ - 2));

                    uint8 SurfaceBlock = RoadSurfaceBlock;
                    uint8 Priority = RoadPriority;
                    if (!bRoadSurface)
                    {
                        const EVoxelBiome Biome = WorldGenerator.GetBiome(
                            BlockX, BlockY, NativeSurfaceBlockZ);
                        const EVoxelLandform Landform = WorldGenerator.GetLandform(BlockX, BlockY);
                        if (Biome == EVoxelBiome::Snow)
                        {
                            SurfaceBlock = uint8(EVoxelBlock::Snow);
                        }
                        else if (Biome == EVoxelBiome::Desert)
                        {
                            SurfaceBlock = Landform == EVoxelLandform::Mountains
                                ? uint8(EVoxelBlock::Sandstone)
                                : uint8(EVoxelBlock::Sand);
                        }
                        else
                        {
                            SurfaceBlock = uint8(EVoxelBlock::Grass);
                        }
                        Priority = 0;
                    }

                    FVoxelRWGRoadStamp Stamp;
                    Stamp.SurfaceZ = SurfaceZ;
                    Stamp.SurfaceBlock = SurfaceBlock;
                    Stamp.FillBlock = uint8(EVoxelBlock::Dirt);
                    Stamp.Priority = Priority;
                    Stamp.bRoadSurface = bRoadSurface;

                    const FIntPoint Key(BlockX, BlockY);
                    FVoxelRWGRoadStamp* Existing = NewStamps->Find(Key);
                    if (!Existing)
                    {
                        NewStamps->Add(Key, Stamp);
                    }
                    else if (bRoadSurface && !Existing->bRoadSurface)
                    {
                        *Existing = Stamp;
                    }
                    else if (bRoadSurface && Existing->bRoadSurface)
                    {
                        Existing->SurfaceZ =
                            (Existing->SurfaceZ + Stamp.SurfaceZ) * 0.5f;
                        if (Stamp.Priority > Existing->Priority)
                        {
                            Existing->SurfaceBlock = Stamp.SurfaceBlock;
                            Existing->Priority = Stamp.Priority;
                        }
                    }
                    else if (!Existing->bRoadSurface)
                    {
                        // On overlapping shoulders use the less invasive cut/fill target.
                        if (FMath::Abs(float(NativeSurfaceZ) - float(Stamp.SurfaceZ)) <
                            FMath::Abs(float(NativeSurfaceZ) - float(Existing->SurfaceZ)))
                        {
                            *Existing = Stamp;
                        }
                    }
                }
            }
        }
    }

    RWGRoadSurfaceStamps = NewStamps;

    TSharedPtr<TMap<FIntPoint, float>, ESPMode::ThreadSafe> NewLODHeights =
        MakeShared<TMap<FIntPoint, float>, ESPMode::ThreadSafe>();
    for (const TPair<FIntPoint, FVoxelRWGRoadStamp>& Pair : *NewStamps)
    {
        NewLODHeights->Add(Pair.Key, Pair.Value.SurfaceZ);
    }
    RWGRoadLODHeights = NewLODHeights;

    UE_LOG(LogTemp, Display, TEXT("RWG terrain stamps built: %d XY columns (LOD heights=%d); dry road surface and shoulder cut/fill."),
        RWGRoadSurfaceStamps.IsValid() ? RWGRoadSurfaceStamps->Num() : 0,
        RWGRoadLODHeights.IsValid() ? RWGRoadLODHeights->Num() : 0);
}


void AVoxelWorld::BuildRWGRoadSurface(const FVoxelRWGPlanner& Planner)
{
    if (!bBuildRWGRoadSurface)
    {
        ClearRWGRoadSurface();
        return;
    }

    if (!RWGRoadMesh)
    {
        RWGRoadMesh = NewObject<UProceduralMeshComponent>(this, TEXT("RWGRoadSurface"));
        if (!RWGRoadMesh)
        {
            UE_LOG(LogTemp, Warning, TEXT("RWG: failed to allocate road surface component."));
            return;
        }

        AddInstanceComponent(RWGRoadMesh);
        RWGRoadMesh->SetMobility(EComponentMobility::Movable);
        RWGRoadMesh->bUseAsyncCooking = true;
        RWGRoadMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        RWGRoadMesh->SetCollisionObjectType(ECC_WorldStatic);
        RWGRoadMesh->SetCollisionResponseToAllChannels(ECR_Block);
        RWGRoadMesh->SetGenerateOverlapEvents(false);
        RWGRoadMesh->SetCanEverAffectNavigation(false);
        RWGRoadMesh->SetVisibility(true, true);
        RWGRoadMesh->SetHiddenInGame(false);
        RWGRoadMesh->CastShadow = false;

        if (GetRootComponent())
        {
            RWGRoadMesh->SetupAttachment(GetRootComponent());
        }
        else
        {
            SetRootComponent(RWGRoadMesh);
        }
        RWGRoadMesh->RegisterComponent();
    }

    RWGRoadMesh->ClearAllMeshSections();
    RWGRoadMesh->SetCollisionEnabled(
        bEnableRWGRoadCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    // Do not inherit a voxel/terrain material by default: it may depend on
    // per-voxel texture data that the road ribbon does not provide.
    UMaterialInterface* EffectiveRoadMaterial = RWGRoadMaterial
        ? RWGRoadMaterial
        : UMaterial::GetDefaultMaterial(MD_Surface);

    const int32 SafeWorldBlocksX = FMath::Max(1, WorldSizeX * ChunkSize);
    const int32 SafeWorldBlocksY = FMath::Max(1, WorldSizeY * ChunkSize);

    const auto SampleSurfaceHeight = [this, SafeWorldBlocksX, SafeWorldBlocksY](float X, float Y)
    {
        const int32 BlockX = FMath::Clamp(FMath::RoundToInt(X), 0, SafeWorldBlocksX - 1);
        const int32 BlockY = FMath::Clamp(FMath::RoundToInt(Y), 0, SafeWorldBlocksY - 1);
        if (RWGRoadSurfaceStamps.IsValid())
        {
            if (const FVoxelRWGRoadStamp* Stamp =
                RWGRoadSurfaceStamps->Find(FIntPoint(BlockX, BlockY)))
            {
                // The continuous MC boundary sits roughly one block above
                // the stamped top block; bridge ends meet that surface.
                if (Stamp->SurfaceZ >= 0.0f)
                {
                    return Stamp->SurfaceZ + 1.12f;
                }
            }
        }

        const float RawHeight = WorldGenerator.GetSurfaceHeightFloat(BlockX, BlockY);
        const FVoxelWaterColumn Water = WorldGenerator.GetWaterColumn(BlockX, BlockY, RawHeight);

        // Use the discrete top of the generated voxel column instead of the
        // continuous noise height, then lift the ribbon above the MC surface.
        const int32 TopVisibleBlockZ = Water.WaterSurfaceBlockZ != INDEX_NONE
            ? FMath::Max(Water.EffectiveTerrainHeight, Water.WaterSurfaceBlockZ)
            : Water.EffectiveTerrainHeight;
        return static_cast<float>(TopVisibleBlockZ) + 1.12f;
    };

    const auto IsWaterAt = [this, SafeWorldBlocksX, SafeWorldBlocksY](float X, float Y)
    {
        const int32 BlockX = FMath::Clamp(FMath::RoundToInt(X), 0, SafeWorldBlocksX - 1);
        const int32 BlockY = FMath::Clamp(FMath::RoundToInt(Y), 0, SafeWorldBlocksY - 1);
        const float RawHeight = WorldGenerator.GetSurfaceHeightFloat(BlockX, BlockY);
        return WorldGenerator.GetWaterColumn(BlockX, BlockY, RawHeight).WaterSurfaceBlockZ != INDEX_NONE;
    };

    const auto BuildRoadTypeSection = [this, &Planner, &SampleSurfaceHeight, &IsWaterAt,
        EffectiveRoadMaterial](EVoxelRWGRoadType RoadType, int32 SectionIndex, float WidthBlocks,
        const FLinearColor& Tint)
    {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> VertexColors;
        TArray<FProcMeshTangent> Tangents;

        const float HalfWidth = FMath::Max(0.5f, WidthBlocks * 0.5f);

        for (const FVoxelRWGRoad& Road : Planner.GetRoads())
        {
            if (Road.Type != RoadType || Road.Points.Num() < 2)
            {
                continue;
            }

            TArray<FVector> Centerline;
            TArray<uint8> CenterlineWaterFlags;
            for (int32 SegmentIndex = 1; SegmentIndex < Road.Points.Num(); ++SegmentIndex)
            {
                const FVector& A = Road.Points[SegmentIndex - 1];
                const FVector& B = Road.Points[SegmentIndex];
                const float SegmentLength = FVector2D(B.X - A.X, B.Y - A.Y).Size();
                const int32 Steps = FMath::Clamp(FMath::CeilToInt(SegmentLength / 8.0f), 1, 2048);

                for (int32 Step = SegmentIndex == 1 ? 0 : 1; Step <= Steps; ++Step)
                {
                    const float T = static_cast<float>(Step) / static_cast<float>(Steps);
                    const float X = FMath::Lerp(A.X, B.X, T);
                    const float Y = FMath::Lerp(A.Y, B.Y, T);
                    const float Z = SampleSurfaceHeight(X, Y);
                    Centerline.Emplace(X, Y, Z);
                    CenterlineWaterFlags.Add(IsWaterAt(X, Y) ? 1 : 0);
                }
            }

            if (Centerline.Num() < 2)
            {
                continue;
            }

            // Dry-road appearance comes from road stamps in voxel terrain.
            // Build a procedural ribbon only for complete water crossings, where
            // the voxel terrain intentionally remains water.
            TArray<FIntPoint> BridgeRuns;
            for (int32 Index = 0; Index < Centerline.Num();)
            {
                if (CenterlineWaterFlags[Index] == 0)
                {
                    ++Index;
                    continue;
                }

                const int32 WaterStart = Index;
                while (Index < Centerline.Num() && CenterlineWaterFlags[Index] != 0)
                {
                    ++Index;
                }
                const int32 WaterEndExclusive = Index;
                if (WaterStart <= 0 || WaterEndExclusive >= Centerline.Num())
                {
                    continue;
                }

                const int32 StartLand = WaterStart - 1;
                const int32 EndLand = WaterEndExclusive;
                float TotalLength = 0.0f;
                for (int32 PointIndex = StartLand + 1; PointIndex <= EndLand; ++PointIndex)
                {
                    TotalLength += FVector2D(
                        Centerline[PointIndex].X - Centerline[PointIndex - 1].X,
                        Centerline[PointIndex].Y - Centerline[PointIndex - 1].Y).Size();
                }

                if (TotalLength <= SMALL_NUMBER)
                {
                    continue;
                }

                float Travelled = 0.0f;
                for (int32 PointIndex = WaterStart; PointIndex < WaterEndExclusive; ++PointIndex)
                {
                    Travelled += FVector2D(
                        Centerline[PointIndex].X - Centerline[PointIndex - 1].X,
                        Centerline[PointIndex].Y - Centerline[PointIndex - 1].Y).Size();
                    const float Alpha = FMath::Clamp(Travelled / TotalLength, 0.0f, 1.0f);
                    Centerline[PointIndex].Z = FMath::Lerp(
                        Centerline[StartLand].Z, Centerline[EndLand].Z, Alpha);
                }
                BridgeRuns.Add(FIntPoint(StartLand, EndLand));
            }

            // Each bridge is a separate strip from bank to bank. There are no
            // mesh triangles laid over dry terrain, so chunk/LOD transitions
            // cannot bury or visually double the normal road surface.
            for (const FIntPoint& BridgeRun : BridgeRuns)
            {
                const int32 StartPoint = BridgeRun.X;
                const int32 EndPoint = BridgeRun.Y;
                const int32 VertexStart = Vertices.Num();
                float DistanceAlongRoad = 0.0f;

                for (int32 PointIndex = StartPoint; PointIndex <= EndPoint; ++PointIndex)
                {
                    const FVector& Center = Centerline[PointIndex];
                    const FVector& Prev = Centerline[FMath::Max(0, PointIndex - 1)];
                    const FVector& Next = Centerline[FMath::Min(Centerline.Num() - 1, PointIndex + 1)];
                    FVector2D Direction(Next.X - Prev.X, Next.Y - Prev.Y);
                    if (Direction.SizeSquared() <= SMALL_NUMBER)
                    {
                        Direction = FVector2D(1.0f, 0.0f);
                    }
                    else
                    {
                        Direction.Normalize();
                    }

                    const FVector2D Side(-Direction.Y, Direction.X);
                    const float LeftX = Center.X + Side.X * HalfWidth;
                    const float LeftY = Center.Y + Side.Y * HalfWidth;
                    const float RightX = Center.X - Side.X * HalfWidth;
                    const float RightY = Center.Y - Side.Y * HalfWidth;

                    const float LeftZ = IsWaterAt(LeftX, LeftY)
                        ? Center.Z : FMath::Max(Center.Z, SampleSurfaceHeight(LeftX, LeftY));
                    const float RightZ = IsWaterAt(RightX, RightY)
                        ? Center.Z : FMath::Max(Center.Z, SampleSurfaceHeight(RightX, RightY));

                    const FVector Left(LeftX * VoxelSize, LeftY * VoxelSize, LeftZ * VoxelSize);
                    const FVector Right(RightX * VoxelSize, RightY * VoxelSize, RightZ * VoxelSize);

                    if (PointIndex > StartPoint)
                    {
                        DistanceAlongRoad += FVector2D(
                            Center.X - Prev.X, Center.Y - Prev.Y).Size();
                    }
                    const float V = DistanceAlongRoad * TerrainUVScalePerBlock;
                    const float UAcrossRoad = WidthBlocks * TerrainUVScalePerBlock;

                    Vertices.Add(Left);
                    Vertices.Add(Right);
                    Normals.Add(FVector::UpVector);
                    Normals.Add(FVector::UpVector);
                    UVs.Add(FVector2D(0.0f, V));
                    UVs.Add(FVector2D(UAcrossRoad, V));
                    VertexColors.Add(Tint);
                    VertexColors.Add(Tint);
                    const FVector Tangent(Direction.X, Direction.Y, 0.0f);
                    Tangents.Add(FProcMeshTangent(Tangent, false));
                    Tangents.Add(FProcMeshTangent(Tangent, false));
                }

                for (int32 PointIndex = StartPoint; PointIndex < EndPoint; ++PointIndex)
                {
                    const int32 L0 = VertexStart + (PointIndex - StartPoint) * 2;
                    const int32 R0 = L0 + 1;
                    const int32 L1 = L0 + 2;
                    const int32 R1 = L0 + 3;
                    Triangles.Add(L0);
                    Triangles.Add(L1);
                    Triangles.Add(R0);
                    Triangles.Add(R0);
                    Triangles.Add(L1);
                    Triangles.Add(R1);
                }
            }
        }

        if (Vertices.Num() > 0 && Triangles.Num() > 0)
        {
            RWGRoadMesh->CreateMeshSection_LinearColor(
                SectionIndex, Vertices, Triangles, Normals, UVs,
                VertexColors, Tangents, bEnableRWGRoadCollision);
            RWGRoadMesh->SetMaterial(SectionIndex, EffectiveRoadMaterial);
        }

        UE_LOG(LogTemp, Display,
            TEXT("RWG road section %d: vertices=%d triangles=%d"),
            SectionIndex, Vertices.Num(), Triangles.Num());
    };

    // Dry roads are not emitted as a floating ribbon. Only water crossings
    // get mesh sections; their width follows the same road class as the terrain.
    BuildRoadTypeSection(EVoxelRWGRoadType::Main, 0, RWGRoadWidthMainBlocks,
        FLinearColor(0.12f, 0.12f, 0.12f, 1.0f));
    BuildRoadTypeSection(EVoxelRWGRoadType::Connector, 1, RWGRoadWidthConnectorBlocks,
        FLinearColor(0.20f, 0.19f, 0.17f, 1.0f));
    BuildRoadTypeSection(EVoxelRWGRoadType::Rural, 2, RWGRoadWidthRuralBlocks,
        FLinearColor(0.30f, 0.22f, 0.14f, 1.0f));
    BuildRoadTypeSection(EVoxelRWGRoadType::Local, 3, RWGRoadWidthLocalBlocks,
        FLinearColor(0.28f, 0.26f, 0.22f, 1.0f));

    UE_LOG(LogTemp, Display,
        TEXT("RWG road/bridge mesh built: %d road paths; terrain widths main=%.1f connector=%.1f rural=%.1f driveway=%.1f blocks; bridge collision %s."),
        Planner.GetRoads().Num(),
        RWGRoadWidthMainBlocks, RWGRoadWidthConnectorBlocks, RWGRoadWidthRuralBlocks,
        RWGRoadWidthLocalBlocks,
        bEnableRWGRoadCollision ? TEXT("enabled") : TEXT("disabled"));
}


bool AVoxelWorld::GenerateRWGLayoutAndExport()
{
    ConfigureWorldGenerator();

    FVoxelRWGPlanSettings PlanSettings;
    PlanSettings.Seed = Seed;
    PlanSettings.WorldBlocksX = FMath::Max(1, WorldSizeX * ChunkSize);
    PlanSettings.WorldBlocksY = FMath::Max(1, WorldSizeY * ChunkSize);
    PlanSettings.SeaLevel = SeaLevel;
    PlanSettings.GridSpacing = RWGGridSpacing;
    PlanSettings.CellSizeBlocks = RWGCellSizeBlocks;
    PlanSettings.TargetSettlementCount = RWGTargetSettlementCount;
    PlanSettings.TargetPOICount = RWGTargetPOICount;

    FVoxelRWGPlanner Planner;
    if (!Planner.Generate(PlanSettings, WorldGenerator))
    {
        UE_LOG(LogTemp, Error, TEXT("RWG layout generation failed: no valid settlement site for seed %d."), Seed);
        return false;
    }

    const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RWG"));
    if (!FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDirectory))
    {
        UE_LOG(LogTemp, Error, TEXT("RWG could not create output directory: %s"), *OutputDirectory);
        return false;
    }

    const FString OutputPath = FPaths::Combine(OutputDirectory, TEXT("WorldLayout.csv"));
    if (!FFileHelper::SaveStringToFile(Planner.ToCSV(), *OutputPath))
    {
        UE_LOG(LogTemp, Error, TEXT("RWG could not write layout file: %s"), *OutputPath);
        return false;
    }

    // Publish an immutable stamp map first, then regenerate loaded chunks
    // against it. Future streamed chunks will use the same map automatically.
    BuildRWGRoadTerrainStamps(Planner);
    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            Pair.Value->SetRoadMaterial(RWGRoadMaterial);
            GenerateChunkBlocksAsync(Pair.Value);
        }
    }

    BuildRWGRoadSurface(Planner);

    // Refresh existing far-LOD rings immediately; otherwise distant terrain
    // would keep the pre-road silhouette until the player changes chunks.
    FIntVector LODCenterChunk;
    if (GetStreamingCenterChunk(LODCenterChunk))
    {
        UpdateFarLOD(LODCenterChunk);
    }

    if (bDrawRWGDebugPreview && GetWorld())
    {
        // F2 may be pressed more than once in PIE. Clear the previous RWG
        // overlay so previews do not accumulate into a dense wireframe dome.
        FlushPersistentDebugLines(GetWorld());

        const auto ToWorldPosition = [this](const FVector& P)
        {
            return GetActorLocation() + FVector(P.X * VoxelSize, P.Y * VoxelSize, P.Z * VoxelSize);
        };

        const int32 SafeWorldBlocksX = FMath::Max(1, WorldSizeX * ChunkSize);
        const int32 SafeWorldBlocksY = FMath::Max(1, WorldSizeY * ChunkSize);

        // RoadPlanner's A* polyline is sampled at a coarse grid interval.
        // Densify each segment and re-sample the exact terrain/water surface;
        // otherwise long straight lines can pass through hills and appear to stop.
        const auto SampleRoadSurface = [this, SafeWorldBlocksX, SafeWorldBlocksY](float X, float Y)
        {
            const int32 BlockX = FMath::Clamp(FMath::RoundToInt(X), 0, SafeWorldBlocksX - 1);
            const int32 BlockY = FMath::Clamp(FMath::RoundToInt(Y), 0, SafeWorldBlocksY - 1);
            const float RawHeight = WorldGenerator.GetSurfaceHeightFloat(BlockX, BlockY);
            const FVoxelWaterColumn WaterColumn =
                WorldGenerator.GetWaterColumn(BlockX, BlockY, RawHeight);
            const float VisibleSurface = WaterColumn.WaterSurfaceBlockZ != INDEX_NONE
                ? FMath::Max(WaterColumn.EffectiveSurfaceHeight,
                    static_cast<float>(WaterColumn.WaterSurfaceBlockZ))
                : WaterColumn.EffectiveSurfaceHeight;
            return VisibleSurface + 1.25f;
        };

        if (bDrawRWGCellOverlay)
        {
            const auto DrawCellEdge = [this, &ToWorldPosition, &SampleRoadSurface](float X0, float Y0,
                float X1, float Y1, const FColor& Color, float Thickness)
            {
                const float Length = FVector2D(X1 - X0, Y1 - Y0).Size();
                const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / 64.0f), 1, 64);
                FVector Previous(X0, Y0, SampleRoadSurface(X0, Y0) + 2.0f);
                for (int32 Step = 1; Step <= Steps; ++Step)
                {
                    const float T = float(Step) / float(Steps);
                    const float X = FMath::Lerp(X0, X1, T);
                    const float Y = FMath::Lerp(Y0, Y1, T);
                    const FVector Current(X, Y, SampleRoadSurface(X, Y) + 2.0f);
                    DrawDebugLine(GetWorld(), ToWorldPosition(Previous), ToWorldPosition(Current),
                        Color, true, RWGDebugDrawDuration, 0, Thickness);
                    Previous = Current;
                }
            };

            for (const FVoxelRWGCell& Cell : Planner.GetCells())
            {
                FColor CellColor(55, 65, 75);
                float Thickness = 0.75f;
                switch (Cell.Type)
                {
                case EVoxelRWGCellType::Rural:
                    CellColor = FColor(55, 120, 70);
                    break;
                case EVoxelRWGCellType::Town:
                    CellColor = FColor(190, 150, 45);
                    Thickness = 1.1f;
                    break;
                case EVoxelRWGCellType::City:
                    CellColor = FColor(220, 65, 65);
                    Thickness = 1.4f;
                    break;
                case EVoxelRWGCellType::Industrial:
                    CellColor = FColor(160, 80, 200);
                    Thickness = 1.1f;
                    break;
                case EVoxelRWGCellType::Wilderness:
                default:
                    break;
                }

                const float MinX = float(Cell.GridCoord.X * RWGCellSizeBlocks);
                const float MinY = float(Cell.GridCoord.Y * RWGCellSizeBlocks);
                const float MaxX = FMath::Min(float(SafeWorldBlocksX - 1), MinX + float(RWGCellSizeBlocks));
                const float MaxY = FMath::Min(float(SafeWorldBlocksY - 1), MinY + float(RWGCellSizeBlocks));

                DrawCellEdge(MinX, MinY, MaxX, MinY, CellColor, Thickness);
                DrawCellEdge(MaxX, MinY, MaxX, MaxY, CellColor, Thickness);
                DrawCellEdge(MaxX, MaxY, MinX, MaxY, CellColor, Thickness);
                DrawCellEdge(MinX, MaxY, MinX, MinY, CellColor, Thickness);
            }
        }

        // When a road mesh is enabled, skip the second line-only overlay:
        // those guide lines follow the waterline and can appear below a bridge deck.
        if (!bBuildRWGRoadSurface)
        {
            for (const FVoxelRWGRoad& Road : Planner.GetRoads())
            {
                FColor Color = FColor(70, 160, 255);
                float Thickness = 2.0f;
                if (Road.Type == EVoxelRWGRoadType::Main)
                {
                    Color = FColor(255, 150, 35);
                    Thickness = 6.0f;
                }
                else if (Road.Type == EVoxelRWGRoadType::Rural)
                {
                    Color = FColor(145, 100, 55);
                    Thickness = 3.5f;
                }
                else if (Road.Type == EVoxelRWGRoadType::Local)
                {
                    Color = FColor(90, 220, 170);
                    Thickness = 1.5f;
                }

                for (int32 I = 1; I < Road.Points.Num(); ++I)
                {
                    const FVector& A = Road.Points[I - 1];
                    const FVector& B = Road.Points[I];
                    const float SegmentLength = FVector2D(B.X - A.X, B.Y - A.Y).Size();
                    const int32 Steps = FMath::Clamp(
                        FMath::CeilToInt(SegmentLength / 8.0f), 1, 512);

                    FVector Previous(
                        A.X, A.Y, SampleRoadSurface(A.X, A.Y));

                    for (int32 Step = 1; Step <= Steps; ++Step)
                    {
                        const float T = static_cast<float>(Step) / static_cast<float>(Steps);
                        const float X = FMath::Lerp(A.X, B.X, T);
                        const float Y = FMath::Lerp(A.Y, B.Y, T);
                        const FVector Current(X, Y, SampleRoadSurface(X, Y));

                        DrawDebugLine(GetWorld(), ToWorldPosition(Previous),
                            ToWorldPosition(Current), Color, true,
                            RWGDebugDrawDuration, 0, Thickness);
                        Previous = Current;
                    }
                }
            }
        }

        // Markers intentionally stay small. The hub radius is a logical
        // settlement footprint in blocks; drawing it as a 3D sphere creates
        // a huge wireframe dome over the terrain.
        for (const FVoxelRWGSettlement& Hub : Planner.GetSettlements())
        {
            const FColor Color = Hub.Type == EVoxelRWGSettlementType::City
                ? FColor::Red : FColor::Yellow;
            DrawDebugSphere(GetWorld(), ToWorldPosition(Hub.Position),
                2.5f * VoxelSize, 8, Color,
                true,
                RWGDebugDrawDuration, 0, 2.0f);
        }

        for (const FVoxelRWGPOI& POI : Planner.GetPOIs())
        {
            DrawDebugSphere(GetWorld(), ToWorldPosition(POI.Position),
                0.9f * VoxelSize, 6, FColor::Green,
                true,
                RWGDebugDrawDuration, 0, 1.5f);
        }
    }

    const FString Summary = Planner.GetSummary();
    UE_LOG(LogTemp, Display, TEXT("%s"), *Summary);
    UE_LOG(LogTemp, Display, TEXT("RWG layout exported to: %s"), *OutputPath);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Green,
            Summary + TEXT("\nCSV: ") + OutputPath);
    }
    return true;
}


void AVoxelWorld::GenerateWorld()
{
    /*
     * Синхронизируем параметры генератора.
     */
    ConfigureWorldGenerator();

    // A full world regeneration clears the previous road plan and its terrain edits.
    RWGRoadSurfaceStamps.Reset();
    RWGRoadLODHeights.Reset();
    ClearRWGRoadSurface();

    /*
     * Удаляем только chunks, которые сейчас загружены.
     */
    ClearFarLOD();

    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            Pair.Value->Destroy();
        }
    }

    Chunks.Empty();
    PendingChunkMeshRebuilds.Empty();
    PendingChunkMeshResults.Empty();

    /*
     * GenerateWorld() означает полную генерацию мира заново,
     * поэтому старые runtime-изменения очищаем.
     * При обычном streaming изменения сюда не попадают.
     */
    ModifiedBlocks.Empty();

    bStreamingInitialized = false;
    LastStreamingCenter = FIntVector::ZeroValue;


    /*
     * Дальше мир создаётся streaming-системой
     * вокруг игрока небольшими порциями.
     */
    UpdateChunkStreaming();
}


bool AVoxelWorld::GetStreamingCenterChunk(
    FIntVector& OutChunkCoord) const
{
    UWorld* World = GetWorld();

    if (!World)
    {
        return false;
    }


    APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            World,
            0);


    if (PlayerPawn)
    {
        FIntVector PlayerBlock;

        if (WorldToBlock(
            PlayerPawn->GetActorLocation(),
            PlayerBlock))
        {
            OutChunkCoord =
                WorldBlockToChunk(
                    PlayerBlock);

            OutChunkCoord.X =
                FMath::Clamp(
                    OutChunkCoord.X,
                    0,
                    WorldSizeX - 1);

            OutChunkCoord.Y =
                FMath::Clamp(
                    OutChunkCoord.Y,
                    0,
                    WorldSizeY - 1);

            OutChunkCoord.Z =
                FMath::Clamp(
                    OutChunkCoord.Z,
                    0,
                    WorldSizeZ - 1);

            return true;
        }
    }


    /*
     * Если игрок ещё не появился,
     * начинаем с центра доступного мира.
     */
    OutChunkCoord =
        FIntVector(
            WorldSizeX / 2,
            WorldSizeY / 2,
            0);

    return true;
}


bool AVoxelWorld::IsChunkInsideWorld(
    const FIntVector& ChunkCoord) const
{
    return
        ChunkCoord.X >= 0 &&
        ChunkCoord.X < WorldSizeX &&
        ChunkCoord.Y >= 0 &&
        ChunkCoord.Y < WorldSizeY &&
        ChunkCoord.Z >= 0 &&
        ChunkCoord.Z < WorldSizeZ;
}


AVoxelChunk* AVoxelWorld::CreateChunk(
    const FIntVector& ChunkCoord)
{
    if (!IsChunkInsideWorld(ChunkCoord))
    {
        return nullptr;
    }


    if (AVoxelChunk* Existing =
        GetChunk(ChunkCoord))
    {
        return Existing;
    }


    UWorld* World = GetWorld();

    if (!World)
    {
        return nullptr;
    }


    const FVector Location =
        GetActorLocation() +
        FVector(
            ChunkCoord.X * ChunkSize * VoxelSize,
            ChunkCoord.Y * ChunkSize * VoxelSize,
            ChunkCoord.Z * ChunkSize * VoxelSize);


    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;


    AVoxelChunk* Chunk =
        World->SpawnActor<AVoxelChunk>(
            AVoxelChunk::StaticClass(),
            Location,
            FRotator::ZeroRotator,
            SpawnParams);


    if (!Chunk)
    {
        return nullptr;
    }


    Chunk->InitializeChunk(
        this,
        ChunkCoord);

    Chunk->SetVoxelMaterial(
        Material);

    Chunk->SetWaterMaterial(
        WaterMaterial);

    Chunk->SetRoadMaterial(
        RWGRoadMaterial);


    Chunks.Add(
        ChunkCoord,
        Chunk);

    /*
     * Expensive terrain generation runs on a worker. Mesh creation and
     * collision remain on the Game Thread through the normal rebuild path.
     */
    GenerateChunkBlocksAsync(Chunk);

    return Chunk;
}


void AVoxelWorld::UpdateChunkStreaming()
{
    FIntVector CenterChunk;

    if (!GetStreamingCenterChunk(
            CenterChunk))
    {
        return;
    }

    const bool bCenterChanged =
        !bStreamingInitialized ||
        CenterChunk != LastStreamingCenter;

    LastStreamingCenter =
        CenterChunk;

    bStreamingInitialized = true;

    /*
     * Full chunks are the only AVoxelChunk actors.
     * Far terrain is rendered by lightweight world-owned tiles.
     */
    const int32 EffectiveUnloadRadius =
        FMath::Max(
            UnloadRadius,
            StreamingRadius);

    TArray<FIntVector> ChunksToUnload;

    for (const TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        const FIntVector& Coord =
            Pair.Key;

        const int32 DistanceX =
            FMath::Abs(
                Coord.X - CenterChunk.X);

        const int32 DistanceY =
            FMath::Abs(
                Coord.Y - CenterChunk.Y);

        const int32 DistanceZ =
            FMath::Abs(
                Coord.Z - CenterChunk.Z);

        if (DistanceX > EffectiveUnloadRadius ||
            DistanceY > EffectiveUnloadRadius ||
            DistanceZ > EffectiveUnloadRadius)
        {
            ChunksToUnload.Add(
                Coord);
        }
    }

    for (const FIntVector& Coord :
        ChunksToUnload)
    {
        AVoxelChunk* Chunk =
            GetChunk(Coord);

        if (Chunk)
        {
            Chunk->Destroy();
        }

        Chunks.Remove(Coord);
        PendingChunkMeshRebuilds.Remove(Coord);
    }

    TArray<FIntVector> Candidates;

    for (int32 Z = CenterChunk.Z - StreamingRadius;
         Z <= CenterChunk.Z + StreamingRadius; ++Z)
    {
        for (int32 Y = CenterChunk.Y - StreamingRadius;
             Y <= CenterChunk.Y + StreamingRadius; ++Y)
        {
            for (int32 X = CenterChunk.X - StreamingRadius;
                 X <= CenterChunk.X + StreamingRadius; ++X)
            {
                const FIntVector Coord(
                    X,
                    Y,
                    Z);

                if (!IsChunkInsideWorld(
                        Coord))
                {
                    continue;
                }

                Candidates.Add(
                    Coord);
            }
        }
    }

    /*
     * Ближайшие Full chunks грузим первыми.
     */
    Candidates.Sort(
        [&CenterChunk](
            const FIntVector& A,
            const FIntVector& B)
        {
            const int32 AX =
                A.X - CenterChunk.X;

            const int32 AY =
                A.Y - CenterChunk.Y;

            const int32 AZ =
                A.Z - CenterChunk.Z;

            const int32 BX =
                B.X - CenterChunk.X;

            const int32 BY =
                B.Y - CenterChunk.Y;

            const int32 BZ =
                B.Z - CenterChunk.Z;

            const int32 DistanceA =
                AX * AX +
                AY * AY +
                AZ * AZ;

            const int32 DistanceB =
                BX * BX +
                BY * BY +
                BZ * BZ;

            return DistanceA < DistanceB;
        });

    int32 LoadedThisTick = 0;

    for (const FIntVector& Coord :
        Candidates)
    {
        if (LoadedThisTick >=
            MaxChunksPerTick)
        {
            break;
        }

        if (Chunks.Contains(Coord))
        {
            continue;
        }

        if (CreateChunk(Coord))
        {
            ++LoadedThisTick;
        }
    }

    /*
     * Far LOD follows the streaming center so its inner boundary stays
     * synchronized with the Full chunk ring. The build itself is performed
     * asynchronously, so this does not block the game thread while the
     * distant meshes are regenerated.
     */
    const int32 FarMoveX =
        FMath::Abs(
            CenterChunk.X -
            LastFarLODCenter.X);

    const int32 FarMoveY =
        FMath::Abs(
            CenterChunk.Y -
            LastFarLODCenter.Y);

    const int32 FarMove =
        FMath::Max(
            FarMoveX,
            FarMoveY);

    if (!bFarLODInitialized ||
        FarMove >= FMath::Max(1, LODUpdateChunkInterval))
    {
        LastFarLODCenter =
            CenterChunk;

        bFarLODInitialized = true;

        UpdateFarLOD(
            CenterChunk);
    }

    /*
     * Chunks which lost a neighbour are queued rather than all being
     * snapshotted in one frame. This is especially important at vehicle speed.
     */
    if (ChunksToUnload.Num() > 0)
    {
        for (const FIntVector& UnloadedCoord : ChunksToUnload)
        {
            QueueChunkFaceNeighborRebuilds(UnloadedCoord);
        }
    }

    if (!bCenterChanged &&
        LoadedThisTick == 0)
    {
        return;
    }
}

/*
 * Генерация terrain.
 */
bool AVoxelWorld::GetBlockDebugInfoAtWorld(
    const FVector& WorldPosition,
    EVoxelBlock& OutBlock,
    FIntVector& OutWorldBlock,
    FIntVector& OutLocalBlock,
    FIntVector& OutChunkCoord) const
{
    if (!WorldToBlock(
            WorldPosition,
            OutWorldBlock))
    {
        return false;
    }

    OutChunkCoord =
        WorldBlockToChunk(
            OutWorldBlock);

    OutLocalBlock =
        WorldBlockToLocal(
            OutWorldBlock);

    const AVoxelChunk* const* ChunkPtr =
        Chunks.Find(
            OutChunkCoord);

    if (!ChunkPtr || !*ChunkPtr)
    {
        OutBlock = EVoxelBlock::Air;
        return false;
    }

    OutBlock =
        static_cast<EVoxelBlock>(
            (*ChunkPtr)->GetBlock(
                OutLocalBlock.X,
                OutLocalBlock.Y,
                OutLocalBlock.Z));

    return true;
}

void AVoxelWorld::GetTerrainDebugInfo(
    int32 WorldX,
    int32 WorldY,
    int32& OutSurfaceHeight,
    float& OutTerrainNoise,
    float& OutTemperature,
    float& OutMoisture,
    FString& OutBiomeName) const
{
    OutSurfaceHeight =
        WorldGenerator.GetSurfaceHeight(
            WorldX,
            WorldY);

    OutTerrainNoise =
        WorldGenerator.GetTerrainNoise(
            WorldX,
            WorldY);

    OutTemperature =
        WorldGenerator.GetTemperature(
            WorldX,
            WorldY);

    OutMoisture =
        WorldGenerator.GetMoisture(
            WorldX,
            WorldY);

    const EVoxelBiome Biome =
        WorldGenerator.GetBiome(
            WorldX,
            WorldY,
            OutSurfaceHeight);

    switch (Biome)
    {
    case EVoxelBiome::Forest:
        OutBiomeName = TEXT("Forest");
        break;

    case EVoxelBiome::Desert:
        OutBiomeName = TEXT("Desert");
        break;

    case EVoxelBiome::Snow:
        OutBiomeName = TEXT("Snow");
        break;

    case EVoxelBiome::Mountain:
        OutBiomeName = TEXT("Mountain (legacy)");
        break;

    case EVoxelBiome::Plains:
    default:
        OutBiomeName = TEXT("Plains");
        break;
    }
}


void AVoxelWorld::GenerateChunkBlocks(
    AVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        return;
    }

    const FIntVector ChunkCoord = Chunk->GetChunkCoord();

    TMap<int32, uint8> ChunkModifications;
    if (const TMap<int32, uint8>* FoundMods =
        ModifiedBlocks.Find(ChunkCoord))
    {
        ChunkModifications = *FoundMods;
    }

    TArray<uint8> Blocks;
    TArray<uint8> BaseBlocks;
    TArray<uint8> Biomes;
    TArray<uint8> ModificationFlags;

    BuildGeneratedChunkData(
        WorldGenerator,
        ChunkCoord,
        ChunkSize,
        SeaLevel,
        BeachWidth,
        ChunkModifications,
        RWGRoadSurfaceStamps,
        TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe>(),
        Blocks,
        BaseBlocks,
        Biomes,
        ModificationFlags);

    Chunk->SetGeneratedData(
        MoveTemp(Blocks),
        MoveTemp(BaseBlocks),
        MoveTemp(Biomes),
        MoveTemp(ModificationFlags));
}


void AVoxelWorld::GenerateChunkBlocksAsync(
    AVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        return;
    }

    const FIntVector ChunkCoord = Chunk->GetChunkCoord();
    const uint32 ExpectedDataGenerationVersion =
        Chunk->GetDataGenerationVersion();
    const FVoxelWorldGenerator GeneratorCopy = WorldGenerator;
    const int32 LocalChunkSize = ChunkSize;
    const int32 LocalSeaLevel = SeaLevel;
    const int32 LocalBeachWidth = BeachWidth;
    const TSharedPtr<TMap<FIntPoint, FVoxelRWGRoadStamp>, ESPMode::ThreadSafe> RoadStampsCopy =
        RWGRoadSurfaceStamps;

    TMap<int32, uint8> ChunkModifications;
    if (const TMap<int32, uint8>* FoundMods =
        ModifiedBlocks.Find(ChunkCoord))
    {
        ChunkModifications = *FoundMods;
    }

    if (Chunk->DataGenerationCancellationToken.IsValid())
    {
        Chunk->DataGenerationCancellationToken->AtomicSet(true);
    }

    Chunk->DataGenerationCancellationToken =
        MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);

    const TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> GenerationCancellationToken =
        Chunk->DataGenerationCancellationToken;

    TWeakObjectPtr<AVoxelWorld> WeakWorld(this);
    TWeakObjectPtr<AVoxelChunk> WeakChunk(Chunk);

    Async(
        EAsyncExecution::ThreadPool,
        [
            WeakWorld,
            WeakChunk,
            GeneratorCopy,
            ExpectedDataGenerationVersion,
            ChunkCoord,
            LocalChunkSize,
            LocalSeaLevel,
            LocalBeachWidth,
            RoadStampsCopy,
            GenerationCancellationToken,
            ChunkModifications = MoveTemp(ChunkModifications)
        ]() mutable
        {
            TArray<uint8> Blocks;
            TArray<uint8> BaseBlocks;
            TArray<uint8> Biomes;
            TArray<uint8> ModificationFlags;

            BuildGeneratedChunkData(
                GeneratorCopy,
                ChunkCoord,
                LocalChunkSize,
                LocalSeaLevel,
                LocalBeachWidth,
                ChunkModifications,
                RoadStampsCopy,
                GenerationCancellationToken,
                Blocks,
                BaseBlocks,
                Biomes,
                ModificationFlags);

            if (static_cast<bool>(*GenerationCancellationToken))
            {
                return;
            }

            AsyncTask(
                ENamedThreads::GameThread,
                [
                    WeakWorld,
                    WeakChunk,
                    ExpectedDataGenerationVersion,
                    ChunkCoord,
                    GenerationCancellationToken,
                    Blocks = MoveTemp(Blocks),
                    BaseBlocks = MoveTemp(BaseBlocks),
                    Biomes = MoveTemp(Biomes),
                    ModificationFlags = MoveTemp(ModificationFlags)
                ]() mutable
                {
                    AVoxelWorld* World = WeakWorld.Get();
                    AVoxelChunk* ReadyChunk = WeakChunk.Get();

                    if (!World || !ReadyChunk ||
                        static_cast<bool>(*GenerationCancellationToken) ||
                        World->GetChunk(ChunkCoord) != ReadyChunk ||
                        ReadyChunk->GetDataGenerationVersion() !=
                            ExpectedDataGenerationVersion)
                    {
                        return;
                    }

                    ReadyChunk->SetGeneratedData(
                        MoveTemp(Blocks),
                        MoveTemp(BaseBlocks),
                        MoveTemp(Biomes),
                        MoveTemp(ModificationFlags));

                    /* Render the new chunk immediately; spread neighbour rebuilds across ticks. */
                    ReadyChunk->RebuildMesh();
                    World->QueueChunkFaceNeighborRebuilds(ChunkCoord);
                });
        });
}

void AVoxelWorld::SaveWorld()
{
    if (SaveSlotName.IsEmpty())
    {
        return;
    }

    UVoxelWorldSaveGame* SaveGame =
        Cast<UVoxelWorldSaveGame>(
            UGameplayStatics::CreateSaveGameObject(
                UVoxelWorldSaveGame::StaticClass()));

    if (!SaveGame)
    {
        return;
    }

    SaveGame->SaveVersion = 1;
    SaveGame->Seed = Seed;
    SaveGame->ChunkSize = ChunkSize;
    SaveGame->VoxelSize = VoxelSize;

    SaveGame->ModifiedBlocks.Reset();

    for (const TPair<FIntVector, TMap<int32, uint8>>& ChunkPair : ModifiedBlocks)
    {
        for (const TPair<int32, uint8>& BlockPair : ChunkPair.Value)
        {
            FVoxelSavedBlock& Record =
                SaveGame->ModifiedBlocks.AddDefaulted_GetRef();

            Record.ChunkCoord = ChunkPair.Key;
            Record.LocalIndex = BlockPair.Key;
            Record.Block = BlockPair.Value;
        }
    }

    SaveGame->ObjectStates.Reset();

    for (const TPair<int64, uint8>& ObjectPair : PersistentObjectStates)
    {
        FVoxelSavedObjectState& Record =
            SaveGame->ObjectStates.AddDefaulted_GetRef();

        Record.ObjectId = ObjectPair.Key;
        Record.State = ObjectPair.Value;
    }

    if (UGameplayStatics::SaveGameToSlot(
        SaveGame,
        SaveSlotName,
        SaveUserIndex))
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("Voxel world saved: %d block changes, %d object states."),
            SaveGame->ModifiedBlocks.Num(),
            SaveGame->ObjectStates.Num());
    }
}


bool AVoxelWorld::LoadWorld()
{
    if (SaveSlotName.IsEmpty() ||
        !UGameplayStatics::DoesSaveGameExist(
            SaveSlotName,
            SaveUserIndex))
    {
        return false;
    }

    UVoxelWorldSaveGame* SaveGame =
        Cast<UVoxelWorldSaveGame>(
            UGameplayStatics::LoadGameFromSlot(
                SaveSlotName,
                SaveUserIndex));

    if (!SaveGame ||
        SaveGame->SaveVersion != 1 ||
        SaveGame->Seed != Seed ||
        SaveGame->ChunkSize != ChunkSize ||
        !FMath::IsNearlyEqual(
            SaveGame->VoxelSize,
            VoxelSize))
    {
        return false;
    }

    ModifiedBlocks.Empty();
    PersistentObjectStates.Empty();

    for (const FVoxelSavedBlock& Record :
        SaveGame->ModifiedBlocks)
    {
        const int32 BlockCount =
            ChunkSize * ChunkSize * ChunkSize;

        if (Record.LocalIndex < 0 ||
            Record.LocalIndex >= BlockCount ||
            !IsChunkInsideWorld(Record.ChunkCoord))
        {
            continue;
        }

        ModifiedBlocks
            .FindOrAdd(Record.ChunkCoord)
            .Add(
                Record.LocalIndex,
                Record.Block);
    }

    for (const FVoxelSavedObjectState& Record :
        SaveGame->ObjectStates)
    {
        PersistentObjectStates.Add(
            Record.ObjectId,
            Record.State);
    }

    /*
     * Важно: загруженный SaveGame хранит только delta-изменения
     * относительно процедурного мира.
     *
     * Поэтому уже загруженные chunks нельзя просто дополнить
     * сохранёнными блоками поверх их текущего runtime-состояния:
     * иначе изменения, сделанные ПОСЛЕ сейва, останутся.
     *
     * Сначала полностью восстанавливаем процедурную базу,
     * затем GenerateChunkBlocks() накладывает сохранённые
     * ModifiedBlocks.
     */
    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            GenerateChunkBlocks(
                Pair.Value);
        }
    }

    /*
     * После восстановления данных перестраиваем mesh всех
     * загруженных chunks. Так изменения после сейва действительно
     * исчезают, а состояние возвращается ровно к моменту Save.
     */
    for (TPair<FIntVector, AVoxelChunk*>& Pair : Chunks)
    {
        if (Pair.Value)
        {
            Pair.Value->RebuildMesh();
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("Voxel world loaded: %d block changes, %d object states."),
        SaveGame->ModifiedBlocks.Num(),
        SaveGame->ObjectStates.Num());

    return true;
}


void AVoxelWorld::ApplyLoadedPersistenceToLoadedChunks()
{
    for (const TPair<FIntVector, TMap<int32, uint8>>& ChunkPair :
        ModifiedBlocks)
    {
        AVoxelChunk* Chunk =
            GetChunk(ChunkPair.Key);

        if (!Chunk)
        {
            continue;
        }

        for (const TPair<int32, uint8>& BlockPair :
            ChunkPair.Value)
        {
            const int32 LocalIndex = BlockPair.Key;

            const int32 X =
                LocalIndex % ChunkSize;

            const int32 Y =
                (LocalIndex / ChunkSize) % ChunkSize;

            const int32 Z =
                LocalIndex /
                (ChunkSize * ChunkSize);

            Chunk->SetBlock(
                X,
                Y,
                Z,
                BlockPair.Value);
        }

        RebuildChunkAndNeighbors(
            ChunkPair.Key);
    }
}


void AVoxelWorld::SetPersistentObjectState(
    int64 ObjectId,
    uint8 State)
{
    SetPersistentObjectStateInternal(
        ObjectId,
        State);
}


void AVoxelWorld::SetPersistentObjectStateInternal(
    int64 ObjectId,
    uint8 State)
{
    PersistentObjectStates.Add(
        ObjectId,
        State);
}


bool AVoxelWorld::GetPersistentObjectState(
    int64 ObjectId,
    uint8& OutState) const
{
    if (const uint8* State =
        PersistentObjectStates.Find(ObjectId))
    {
        OutState = *State;
        return true;
    }

    OutState = 0;
    return false;
}


/*
 * Получение чанка.
 *
 * ВАЖНО:
 * метод НЕ const.
 */
AVoxelChunk* AVoxelWorld::GetChunk(
    const FIntVector& ChunkCoord)
{
    AVoxelChunk* const* Found =
        Chunks.Find(ChunkCoord);


    return Found
        ? *Found
        : nullptr;
}


/*
 * Создаёт snapshots шести соседних сторон.
 */
void AVoxelWorld::BuildNeighborData(
    const FIntVector& ChunkCoord,
    FVoxelNeighborData& OutData) const
{
    OutData.Init(ChunkSize);


    /*
     * X-
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(-1, 0, 0)))
    {
        Neighbor->CopyXPlus(
            OutData.XMinus);
        Neighbor->CopyXPlusStructure(
            OutData.XMinusStructure);
    }


    /*
     * X+
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(1, 0, 0)))
    {
        Neighbor->CopyXMinus(
            OutData.XPlus);
        Neighbor->CopyXMinusStructure(
            OutData.XPlusStructure);
    }


    /*
     * Y-
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, -1, 0)))
    {
        Neighbor->CopyYPlus(
            OutData.YMinus);
        Neighbor->CopyYPlusStructure(
            OutData.YMinusStructure);
    }


    /*
     * Y+
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, 1, 0)))
    {
        Neighbor->CopyYMinus(
            OutData.YPlus);
        Neighbor->CopyYMinusStructure(
            OutData.YPlusStructure);
    }


    /*
     * Z-
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, 0, -1)))
    {
        Neighbor->CopyZPlus(
            OutData.ZMinus);
        Neighbor->CopyZPlusStructure(
            OutData.ZMinusStructure);
    }


    /*
     * Z+
     */
    if (const AVoxelChunk* Neighbor =
        Chunks.FindRef(
            ChunkCoord + FIntVector(0, 0, 1)))
    {
        Neighbor->CopyZMinus(
            OutData.ZPlus);
        Neighbor->CopyZMinusStructure(
            OutData.ZPlusStructure);
    }
}


/*
 * Expanded block snapshot for Marching Cubes.
 * The smooth field deliberately excludes player-modified cells so
 * construction blocks stay on the separate cubic layer.
 * Local block coordinates range from [-1, Size].
 */
void AVoxelWorld::CaptureMarchingCubesDataSnapshot(
    const FIntVector& ChunkCoord,
    FVoxelMarchingCubesDataSnapshot& OutSnapshot) const
{
    OutSnapshot.RoadStamps = RWGRoadSurfaceStamps;
    OutSnapshot.Generator = WorldGenerator;
    OutSnapshot.ChunkCoord = ChunkCoord;
    OutSnapshot.ChunkSize = ChunkSize;
    OutSnapshot.WorldSizeX = WorldSizeX;
    OutSnapshot.WorldSizeY = WorldSizeY;
    OutSnapshot.WorldSizeZ = WorldSizeZ;
    OutSnapshot.SeaLevel = SeaLevel;
    OutSnapshot.BeachWidth = BeachWidth;
    OutSnapshot.VoxelSize = VoxelSize;
    OutSnapshot.UVScalePerBlock = TerrainUVScalePerBlock;
    OutSnapshot.CancellationToken.Reset();
    OutSnapshot.ChunkModifications.Reset();

    /*
     * Only the target chunk and its 26 adjacent chunks can contribute
     * to the one-voxel halo. Never copy/read AVoxelChunk objects on a worker.
     */
    for (int32 Z = -1; Z <= 1; ++Z)
    {
        for (int32 Y = -1; Y <= 1; ++Y)
        {
            for (int32 X = -1; X <= 1; ++X)
            {
                const FIntVector SourceCoord =
                    ChunkCoord + FIntVector(X, Y, Z);

                if (const TMap<int32, uint8>* Mods =
                    ModifiedBlocks.Find(SourceCoord))
                {
                    OutSnapshot.ChunkModifications.Add(
                        SourceCoord,
                        *Mods);
                }
            }
        }
    }
}


void AVoxelWorld::BuildMarchingCubesData(
    const FIntVector& ChunkCoord,
    FVoxelMarchingCubesBuildInput& OutData) const
{
    FVoxelMarchingCubesDataSnapshot Snapshot;
    CaptureMarchingCubesDataSnapshot(ChunkCoord, Snapshot);
    Snapshot.Build(OutData);
}

bool AVoxelWorld::IsPositionInsideWater(
    const FVector& WorldPosition) const
{
    FIntVector WorldBlock;

    if (!WorldToBlock(
        WorldPosition,
        WorldBlock))
    {
        return false;
    }

    const FIntVector ChunkCoord =
        WorldBlockToChunk(WorldBlock);

    const AVoxelChunk* Chunk =
        Chunks.FindRef(ChunkCoord);

    if (!Chunk)
    {
        return false;
    }

    const FIntVector LocalBlock =
        WorldBlockToLocal(WorldBlock);

    return Chunk->GetBlock(
        LocalBlock.X,
        LocalBlock.Y,
        LocalBlock.Z) ==
        uint8(EVoxelBlock::Water);
}


void AVoxelWorld::UpdateUnderwaterEffect()
{
    APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);

    if (!PC || !PC->PlayerCameraManager)
    {
        return;
    }

    const bool bUnderwater =
        IsPositionInsideWater(
            PC->PlayerCameraManager->GetCameraLocation());

    if (bUnderwater == bUnderwaterEffectActive)
    {
        return;
    }

    bUnderwaterEffectActive = bUnderwater;

    if (bUnderwater)
    {
        /*
         * Keep the blue tint active for the whole time the camera
         * is inside a water voxel. StartCameraFade only animates a
         * transition unless it is explicitly held.
         */
        PC->PlayerCameraManager->SetManualCameraFade(
            0.38f,
            FLinearColor(
                0.02f,
                0.20f,
                0.55f,
                1.0f),
            false);
    }
    else
    {
        /*
         * Remove the underwater overlay immediately after leaving water.
         */
        PC->PlayerCameraManager->StopCameraFade();
    }
}


/*
 * World position -> world block.
 */
bool AVoxelWorld::WorldToBlock(
    const FVector& WorldPosition,
    FIntVector& OutBlock) const
{
    const FVector LocalPosition =
        WorldPosition - GetActorLocation();


    OutBlock.X =
        FMath::FloorToInt(
            LocalPosition.X / VoxelSize);


    OutBlock.Y =
        FMath::FloorToInt(
            LocalPosition.Y / VoxelSize);


    OutBlock.Z =
        FMath::FloorToInt(
            LocalPosition.Z / VoxelSize);


    return true;
}


/*
 * World block -> chunk coordinate.
 */
FIntVector AVoxelWorld::WorldBlockToChunk(
    const FIntVector& WorldBlock) const
{
    return FIntVector(
        FastFloorDiv(
            WorldBlock.X,
            ChunkSize),

        FastFloorDiv(
            WorldBlock.Y,
            ChunkSize),

        FastFloorDiv(
            WorldBlock.Z,
            ChunkSize));
}


/*
 * World block -> local block.
 */
FIntVector AVoxelWorld::WorldBlockToLocal(
    const FIntVector& WorldBlock) const
{
    return FIntVector(
        PositiveModulo(
            WorldBlock.X,
            ChunkSize),

        PositiveModulo(
            WorldBlock.Y,
            ChunkSize),

        PositiveModulo(
            WorldBlock.Z,
            ChunkSize));
}


/*
 * Изменение блока.
 */
void AVoxelWorld::SetBlockInternal(
    const FIntVector& WorldBlock,
    uint8 Block)
{
    const FIntVector ChunkCoord =
        WorldBlockToChunk(
            WorldBlock);


    const FIntVector LocalBlock =
        WorldBlockToLocal(
            WorldBlock);


    AVoxelChunk* Chunk =
        GetChunk(ChunkCoord);


    if (!Chunk)
    {
        return;
    }


    Chunk->SetBlock(
        LocalBlock.X,
        LocalBlock.Y,
        LocalBlock.Z,
        Block);

    Chunk->SetModificationFlag(
        LocalBlock.X,
        LocalBlock.Y,
        LocalBlock.Z,
        true);


    /*
     * Сохраняем изменение отдельно от runtime-данных чанка.
     *
     * Поэтому после Destroy() чанка streaming не теряет
     * изменения: при следующем CreateChunk() они будут
     * наложены поверх процедурной генерации.
     */
    const int32 LocalIndex =
        LocalBlock.X +
        LocalBlock.Y * ChunkSize +
        LocalBlock.Z * ChunkSize * ChunkSize;

    TMap<int32, uint8>& ChunkModifications =
        ModifiedBlocks.FindOrAdd(ChunkCoord);

    ChunkModifications.Add(
        LocalIndex,
        Block);


    /* Rebuild the edited chunk immediately; neighbour seams are coalesced and queued. */
    Chunk->RebuildMesh();
    QueueChunkNeighborhoodRebuilds(ChunkCoord);
}


/*
 * Удаление блока по World Position.
 */
void AVoxelWorld::RemoveBlockAtWorld(
    const FVector& WorldPosition)
{
    FIntVector WorldBlock;


    if (!WorldToBlock(
        WorldPosition,
        WorldBlock))
    {
        return;
    }


    SetBlockInternal(
        WorldBlock,
        uint8(EVoxelBlock::Air));
}


/*
 * Установка блока по World Position.
 */
void AVoxelWorld::SetBlockAtWorld(
    const FVector& WorldPosition,
    EVoxelBlock Block)
{
    FIntVector WorldBlock;


    if (!WorldToBlock(
        WorldPosition,
        WorldBlock))
    {
        return;
    }


    SetBlockInternal(
        WorldBlock,
        uint8(Block));
}


/*
 * Перестраиваем изменённый chunk
 * плюс шесть соседей.
 */
void AVoxelWorld::RebuildChunkAndNeighbors(
    const FIntVector& ChunkCoord)
{
    /*
     * MC density nodes near borders can depend on blocks in diagonal
     * neighbours because each node samples the surrounding voxel cells.
     */
    for (int32 Z = -1; Z <= 1; ++Z)
    {
        for (int32 Y = -1; Y <= 1; ++Y)
        {
            for (int32 X = -1; X <= 1; ++X)
            {
                if (AVoxelChunk* Chunk =
                    GetChunk(
                        ChunkCoord + FIntVector(X, Y, Z)))
                {
                    if (Chunk->HasGeneratedData())
                    {
                        Chunk->RebuildMesh();
                    }
                }
            }
        }
    }
}


/*
 * Coalesce streaming/edit neighbour updates. Face neighbours are inserted
 * before edge/corner neighbours because they are the most visible seams.
 */
void AVoxelWorld::QueueChunkNeighborhoodRebuilds(
    const FIntVector& ChunkCoord)
{
    for (int32 Distance = 1; Distance <= 3; ++Distance)
    {
        for (int32 Z = -1; Z <= 1; ++Z)
        {
            for (int32 Y = -1; Y <= 1; ++Y)
            {
                for (int32 X = -1; X <= 1; ++X)
                {
                    if (FMath::Abs(X) +
                        FMath::Abs(Y) +
                        FMath::Abs(Z) != Distance)
                    {
                        continue;
                    }

                    const FIntVector NeighborCoord =
                        ChunkCoord + FIntVector(X, Y, Z);

                    AVoxelChunk* Neighbor = GetChunk(NeighborCoord);

                    if (!Neighbor || !Neighbor->HasGeneratedData())
                    {
                        continue;
                    }

                    if (!PendingChunkMeshRebuilds.Contains(NeighborCoord))
                    {
                        PendingChunkMeshRebuilds.Add(NeighborCoord);
                    }
                }
            }
        }
    }
}


void AVoxelWorld::QueueChunkFaceNeighborRebuilds(
    const FIntVector& ChunkCoord)
{
    /*
     * Streaming only changes the six direct face neighbours relevant to
     * cubic block/water visibility. The 26-neighbour queue is reserved for
     * voxel edits, where diagonal cells can affect MC density nodes.
     */
    const FIntVector FaceOffsets[] =
    {
        FIntVector(-1, 0, 0),
        FIntVector(1, 0, 0),
        FIntVector(0, -1, 0),
        FIntVector(0, 1, 0),
        FIntVector(0, 0, -1),
        FIntVector(0, 0, 1)
    };

    for (const FIntVector& Offset : FaceOffsets)
    {
        const FIntVector NeighborCoord = ChunkCoord + Offset;
        AVoxelChunk* Neighbor = GetChunk(NeighborCoord);

        if (!Neighbor || !Neighbor->HasGeneratedData())
        {
            continue;
        }

        if (!PendingChunkMeshRebuilds.Contains(NeighborCoord))
        {
            PendingChunkMeshRebuilds.Add(NeighborCoord);
        }
    }
}


void AVoxelWorld::QueueChunkMeshResult(
    AVoxelChunk* Chunk,
    FVoxelMeshBuildOutput&& Output,
    uint32 Version)
{
    if (!Chunk ||
        Chunk->GetMeshGenerationVersion() != Version ||
        GetChunk(Chunk->GetChunkCoord()) != Chunk)
    {
        return;
    }

    /*
     * Keep at most one pending output per chunk. If a newer build finishes
     * before an older result is uploaded, replace the old geometry instead
     * of queuing stale, memory-heavy vertex arrays.
     */
    for (FPendingVoxelChunkMeshResult& Pending : PendingChunkMeshResults)
    {
        if (Pending.Chunk.Get() == Chunk)
        {
            Pending.Version = Version;
            Pending.Output = MoveTemp(Output);
            return;
        }
    }

    FPendingVoxelChunkMeshResult& Pending =
        PendingChunkMeshResults.AddDefaulted_GetRef();

    Pending.Chunk = Chunk;
    Pending.Version = Version;
    Pending.Output = MoveTemp(Output);
}


void AVoxelWorld::QueueFarLODMeshResult(
    UProceduralMeshComponent* Mesh,
    FVoxelTerrainLODMeshOutput&& Output,
    uint32 GenerationVersion)
{
    if (!Mesh ||
        GenerationVersion != FarLODGenerationVersion)
    {
        return;
    }

    /* Coalesce outputs so each LOD component has at most one pending upload. */
    for (FPendingVoxelLODMeshResult& Pending : PendingVoxelLODMeshResults)
    {
        if (Pending.Mesh.Get() == Mesh)
        {
            Pending.GenerationVersion = GenerationVersion;
            Pending.Output = MoveTemp(Output);
            return;
        }
    }

    FPendingVoxelLODMeshResult& Pending =
        PendingVoxelLODMeshResults.AddDefaulted_GetRef();

    Pending.Mesh = Mesh;
    Pending.GenerationVersion = GenerationVersion;
    Pending.Output = MoveTemp(Output);
}


void AVoxelWorld::ProcessPendingMeshUploads()
{
    const int32 UploadBudget =
        FMath::Clamp(MaxMeshSectionUploadsPerFrame, 1, 2);

    int32 UploadedThisFrame = 0;

    while (UploadedThisFrame < UploadBudget)
    {
        bool bUploaded = false;

        /*
         * Near chunks take priority over far LOD. Apply only one current
         * result at a time; stale outputs are discarded without touching UObjects.
         */
        int32 InspectedChunkResults = 0;

        while (PendingChunkMeshResults.Num() > 0 &&
               InspectedChunkResults < 8)
        {
            ++InspectedChunkResults;

            FPendingVoxelChunkMeshResult Pending =
                MoveTemp(PendingChunkMeshResults[0]);

            PendingChunkMeshResults.RemoveAt(0, 1, false);

            AVoxelChunk* Chunk = Pending.Chunk.Get();

            if (!Chunk ||
                Chunk->GetMeshGenerationVersion() != Pending.Version ||
                GetChunk(Chunk->GetChunkCoord()) != Chunk)
            {
                continue;
            }

            Chunk->ApplyMesh(
                MoveTemp(Pending.Output),
                Pending.Version,
                true);

            bUploaded = true;
            ++UploadedThisFrame;
            break;
        }

        if (bUploaded)
        {
            continue;
        }

        int32 InspectedLODResults = 0;

        while (PendingVoxelLODMeshResults.Num() > 0 &&
               InspectedLODResults < 8)
        {
            ++InspectedLODResults;

            FPendingVoxelLODMeshResult Pending =
                MoveTemp(PendingVoxelLODMeshResults[0]);

            PendingVoxelLODMeshResults.RemoveAt(0, 1, false);

            UProceduralMeshComponent* Mesh = Pending.Mesh.Get();

            if (!Mesh ||
                Pending.GenerationVersion != FarLODGenerationVersion)
            {
                continue;
            }

            Mesh->ClearMeshSection(0);
            Mesh->ClearMeshSection(1);

            if (Pending.Output.Vertices.Num() > 0 &&
                Pending.Output.Triangles.Num() > 0)
            {
                Mesh->CreateMeshSection_LinearColor(
                    0,
                    Pending.Output.Vertices,
                    Pending.Output.Triangles,
                    Pending.Output.Normals,
                    Pending.Output.UV0,
                    TArray<FVector2D>(),
                    TArray<FVector2D>(),
                    TArray<FVector2D>(),
                    Pending.Output.VertexColors,
                    TArray<FProcMeshTangent>(),
                    false);
            }

            if (Pending.Output.WaterVertices.Num() > 0 &&
                Pending.Output.WaterTriangles.Num() > 0)
            {
                Mesh->CreateMeshSection_LinearColor(
                    1,
                    Pending.Output.WaterVertices,
                    Pending.Output.WaterTriangles,
                    Pending.Output.WaterNormals,
                    Pending.Output.WaterUV0,
                    TArray<FVector2D>(),
                    TArray<FVector2D>(),
                    TArray<FVector2D>(),
                    Pending.Output.WaterVertexColors,
                    TArray<FProcMeshTangent>(),
                    false);
            }

            bUploaded = true;
            ++UploadedThisFrame;
            break;
        }

        if (!bUploaded)
        {
            break;
        }
    }
}


void AVoxelWorld::ProcessPendingChunkMeshRebuilds()
{
    /* Avoid flooding the worker pool when the player streams quickly. */
    const int32 RebuildBudget =
        FMath::Clamp(MaxChunkMeshRebuildsPerTick, 1, 2);

    int32 ProcessedEntries = 0;

    while (PendingChunkMeshRebuilds.Num() > 0 &&
           ProcessedEntries < RebuildBudget)
    {
        ++ProcessedEntries;

        const FIntVector Coord =
            PendingChunkMeshRebuilds[0];

        PendingChunkMeshRebuilds.RemoveAt(0, 1, false);

        AVoxelChunk* Chunk = GetChunk(Coord);

        if (!Chunk || !Chunk->HasGeneratedData())
        {
            continue;
        }

        Chunk->RebuildMesh();
    }
}

/*
 * Разрушение блока через ray из центра экрана.
 */
bool AVoxelWorld::BreakBlockByRay()
{
    APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);


    if (!PC)
    {
        return false;
    }


    FVector Start;
    FVector Direction;


    if (!GetCenterScreenRay(
        PC,
        Start,
        Direction))
    {
        return false;
    }


    const FVector End =
        Start +
        Direction * InteractionDistance;


    FHitResult Hit;


    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(VoxelBreak),
        true);


    Params.AddIgnoredActor(this);


    const bool bHit =
        GetWorld()->LineTraceSingleByChannel(
            Hit,
            Start,
            End,
            ECC_Visibility,
            Params);


    if (!bHit)
    {
        return false;
    }


    AVoxelChunk* HitChunk =
        Cast<AVoxelChunk>(
            Hit.GetActor());


    if (!HitChunk)
    {
        return false;
    }


    /*
     * На MC-поверхности ImpactPoint находится на сглаженной границе,
     * поэтому одного FloorToInt недостаточно. Идём по нормали внутрь
     * поверхности и берём первый реально ломаемый voxel.
     */
    FIntVector WorldBlock;
    AVoxelChunk* Chunk = nullptr;
    FIntVector LocalBlock;
    EVoxelBlock HitBlock = EVoxelBlock::Air;
    bool bFoundBreakableBlock = false;

    const float SampleStep =
        FMath::Max(
            VoxelSize * 0.05f,
            1.0f);

    const int32 MaxSamples = 16;

    for (int32 Sample = 1;
         Sample <= MaxSamples;
         ++Sample)
    {
        const FVector SamplePoint =
            Hit.ImpactPoint -
            Hit.ImpactNormal *
            (SampleStep * Sample);

        FIntVector CandidateWorldBlock;

        if (!WorldToBlock(
            SamplePoint,
            CandidateWorldBlock))
        {
            continue;
        }

        AVoxelChunk* CandidateChunk =
            GetChunk(
                WorldBlockToChunk(
                    CandidateWorldBlock));

        if (!CandidateChunk)
        {
            continue;
        }

        const FIntVector CandidateLocalBlock =
            WorldBlockToLocal(
                CandidateWorldBlock);

        const EVoxelBlock CandidateBlock =
            static_cast<EVoxelBlock>(
                CandidateChunk->GetBlock(
                    CandidateLocalBlock.X,
                    CandidateLocalBlock.Y,
                    CandidateLocalBlock.Z));

        if (!UVoxelBlockLibrary::CanBreakBlockFromTable(
            BlockDataTable,
            CandidateBlock))
        {
            continue;
        }

        WorldBlock = CandidateWorldBlock;
        Chunk = CandidateChunk;
        HitBlock = CandidateBlock;
        bFoundBreakableBlock = true;
        break;
    }

    if (!bFoundBreakableBlock)
    {
        return false;
    }

    /*
     * Вся игровая логика блока теперь смотрит в единый реестр.
     */
    if (!UVoxelBlockLibrary::CanBreakBlockFromTable(
        BlockDataTable,
        HitBlock))
    {
        return false;
    }

    const EVoxelBlock DropBlock =
        UVoxelBlockLibrary::GetBlockDropFromTable(
            BlockDataTable,
            HitBlock);

    APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            GetWorld(),
            0);

    UVoxelInventoryComponent* Inventory = nullptr;

    if (PlayerPawn)
    {
        Inventory =
            PlayerPawn->FindComponentByClass<
                UVoxelInventoryComponent>();
    }

    /*
     * Если у блока есть дроп и inventory установлен,
     * сначала убеждаемся, что предмет можно подобрать.
     * Тогда блок не исчезнет впустую при заполненном hotbar.
     */
    if (Inventory &&
        DropBlock != EVoxelBlock::Air &&
        !Inventory->CanAddBlock(
            DropBlock,
            1))
    {
        return false;
    }

    SetBlockInternal(
        WorldBlock,
        uint8(EVoxelBlock::Air));

    if (Inventory &&
        DropBlock != EVoxelBlock::Air)
    {
        Inventory->AddBlock(
            DropBlock,
            1);
    }

    return true;
}


/*
 * Установка блока рядом с поверхностью.
 */
bool AVoxelWorld::PlaceBlockByRay()
{
    /*
     * Старый Blueprint-вызов остаётся совместимым.
     *
     * Если на игроке есть inventory, берём блок из выбранного
     * слота. Если компонента ещё нет, для обратной совместимости
     * используем прежний Dirt.
     */
    if (APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            GetWorld(),
            0))
    {
        if (UVoxelInventoryComponent* Inventory =
            PlayerPawn->FindComponentByClass<
                UVoxelInventoryComponent>())
        {
            return PlaceBlockByRayWithType(
                Inventory->GetSelectedBlock());
        }
    }

    return PlaceBlockByRayWithType(
        EVoxelBlock::Dirt);
}


bool AVoxelWorld::PlaceBlockByRayWithType(
    EVoxelBlock BlockToPlace)
{
    APlayerController* PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);

    if (!PC)
    {
        return false;
    }

    if (!UVoxelBlockLibrary::CanPlaceBlockFromTable(
        BlockDataTable,
        BlockToPlace))
    {
        return false;
    }

    APawn* PlayerPawn =
        UGameplayStatics::GetPlayerPawn(
            GetWorld(),
            0);

    UVoxelInventoryComponent* Inventory = nullptr;

    if (PlayerPawn)
    {
        Inventory =
            PlayerPawn->FindComponentByClass<
                UVoxelInventoryComponent>();
    }

    /*
     * Если inventory подключён, выбранный слот обязан
     * содержать тот же блок, который мы ставим.
     */
    if (Inventory &&
        (Inventory->GetSelectedBlock() != BlockToPlace ||
         Inventory->GetSelectedQuantity() <= 0))
    {
        return false;
    }

    FVector Start;
    FVector Direction;

    if (!GetCenterScreenRay(
        PC,
        Start,
        Direction))
    {
        return false;
    }

    const FVector End =
        Start +
        Direction * InteractionDistance;

    FHitResult Hit;

    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(VoxelPlace),
        true);

    Params.AddIgnoredActor(this);

    const bool bHit =
        GetWorld()->LineTraceSingleByChannel(
            Hit,
            Start,
            End,
            ECC_Visibility,
            Params);

    if (!bHit)
    {
        return false;
    }

    AVoxelChunk* HitChunk =
        Cast<AVoxelChunk>(
            Hit.GetActor());

    if (!HitChunk)
    {
        return false;
    }

    /*
     * Marching Cubes surfaces are smooth and their hit normal is not
     * necessarily axis-aligned. A single 0.51-voxel offset can still land
     * in a solid voxel even when the point just outside the visible surface
     * is empty. Probe outwards in small steps and pick the nearest cell that
     * is actually available. Water is also replaceable by a placed block.
     */
    const FVector PlacementNormal =
        Hit.ImpactNormal.GetSafeNormal();

    if (PlacementNormal.IsNearlyZero())
    {
        return false;
    }

    const float ProbeStep =
        FMath::Max(VoxelSize * 0.05f, 1.0f);

    const float MaxProbeDistance =
        VoxelSize * 1.1f;

    FIntVector WorldBlock = FIntVector::ZeroValue;
    AVoxelChunk* TargetChunk = nullptr;
    bool bFoundPlacementCell = false;

    for (float Distance = ProbeStep;
         Distance <= MaxProbeDistance + KINDA_SMALL_NUMBER;
         Distance += ProbeStep)
    {
        const FVector ProbePoint =
            Hit.ImpactPoint + PlacementNormal * Distance;

        FIntVector CandidateWorldBlock;
        if (!WorldToBlock(ProbePoint, CandidateWorldBlock))
        {
            continue;
        }

        const FIntVector CandidateChunkCoord =
            WorldBlockToChunk(CandidateWorldBlock);

        AVoxelChunk* CandidateChunk =
            GetChunk(CandidateChunkCoord);

        if (!CandidateChunk || !CandidateChunk->HasGeneratedData())
        {
            continue;
        }

        const FIntVector CandidateLocalBlock =
            WorldBlockToLocal(CandidateWorldBlock);

        const EVoxelBlock CandidateBlock =
            static_cast<EVoxelBlock>(
                CandidateChunk->GetBlock(
                    CandidateLocalBlock.X,
                    CandidateLocalBlock.Y,
                    CandidateLocalBlock.Z));

        if (CandidateBlock != EVoxelBlock::Air &&
            CandidateBlock != EVoxelBlock::Water)
        {
            continue;
        }

        WorldBlock = CandidateWorldBlock;
        TargetChunk = CandidateChunk;
        bFoundPlacementCell = true;
        break;
    }

    if (!bFoundPlacementCell || !TargetChunk)
    {
        return false;
    }

    SetBlockInternal(
        WorldBlock,
        uint8(BlockToPlace));

    if (Inventory)
    {
        Inventory->RemoveFromSelectedSlot(1);
    }

    return true;
}




void AVoxelWorld::ToggleDebugFly()
{
    bDebugFlyMode = !bDebugFlyMode;
    DebugFlyVerticalInput = 0.0f;
    bDebugFlyBoost = false;
    ApplyDebugFlySettings(bDebugFlyMode);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("Debug Fly Mode: %s"),
        bDebugFlyMode ? TEXT("ON") : TEXT("OFF"));
}


void AVoxelWorld::DebugFlyUpPressed()
{
    if (bDebugFlyMode)
    {
        DebugFlyVerticalInput = 1.0f;
    }
}


void AVoxelWorld::DebugFlyUpReleased()
{
    if (bDebugFlyMode && DebugFlyVerticalInput > 0.0f)
    {
        DebugFlyVerticalInput = 0.0f;
    }
}


void AVoxelWorld::DebugFlyDownPressed()
{
    if (bDebugFlyMode)
    {
        DebugFlyVerticalInput = -1.0f;
    }
}


void AVoxelWorld::DebugFlyDownReleased()
{
    if (bDebugFlyMode && DebugFlyVerticalInput < 0.0f)
    {
        DebugFlyVerticalInput = 0.0f;
    }
}


void AVoxelWorld::DebugFlyBoostPressed()
{
    if (bDebugFlyMode)
    {
        bDebugFlyBoost = true;
        ApplyDebugFlySettings(true);
    }
}


void AVoxelWorld::DebugFlyBoostReleased()
{
    bDebugFlyBoost = false;

    if (bDebugFlyMode)
    {
        ApplyDebugFlySettings(true);
    }
}


void AVoxelWorld::ApplyDebugFlySettings(bool bEnable)
{
    UWorld* World = GetWorld();

    if (!World)
    {
        return;
    }

    APawn* Pawn =
        UGameplayStatics::GetPlayerPawn(
            World,
            0);

    ACharacter* Character =
        Cast<ACharacter>(Pawn);

    if (!Character)
    {
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    if (!Movement)
    {
        return;
    }

    if (bEnable)
    {
        APlayerController* PC =
            World->GetFirstPlayerController();

        if (PC)
        {
            PC->bShowMouseCursor = false;
            FInputModeGameOnly GameOnlyInput;
            PC->SetInputMode(GameOnlyInput);
        }

        Movement->SetMovementMode(MOVE_Flying);
        Movement->MaxFlySpeed = bDebugFlyBoost ? DebugFlyBoostSpeed : DebugFlySpeed;
        Movement->MaxAcceleration = DebugFlyAcceleration;
        Movement->BrakingDecelerationFlying = DebugFlyAcceleration;
        Movement->GravityScale = 0.0f;

    }
    else
    {
        Movement->SetMovementMode(MOVE_Walking);
        Movement->GravityScale = 1.0f;
        DebugFlyVerticalInput = 0.0f;
        bDebugFlyBoost = false;
    }
}


void AVoxelWorld::ClearFarLOD()
{
    ++FarLODGenerationVersion;

    if (FarLODCancellationToken.IsValid())
    {
        FarLODCancellationToken->AtomicSet(true);
        FarLODCancellationToken.Reset();
    }

    PendingVoxelLODMeshResults.Empty();

    UProceduralMeshComponent* FarMeshes[] =
    {
        FarLOD1Mesh,
        FarLOD2Mesh,
        FarLOD3Mesh,
        FarLOD4Mesh
    };

    for (UProceduralMeshComponent* Mesh : FarMeshes)
    {
        if (Mesh)
        {
            Mesh->ClearMeshSection(0);
            Mesh->ClearMeshSection(1);
            Mesh->DestroyComponent();
        }
    }

    FarLOD1Mesh = nullptr;
    FarLOD2Mesh = nullptr;
    FarLOD3Mesh = nullptr;
    FarLOD4Mesh = nullptr;

    bFarLODInitialized = false;
    LastFarLODCenter = FIntVector::ZeroValue;
}


namespace
{
    UProceduralMeshComponent* CreateFarLODMeshComponent(
        AVoxelWorld* Owner,
        const TCHAR* Name,
        UMaterialInterface* Material,
        UMaterialInterface* WaterMaterial)
    {
        if (!Owner)
        {
            return nullptr;
        }

        UProceduralMeshComponent* Mesh =
            NewObject<UProceduralMeshComponent>(
                Owner,
                Name);

        if (!Mesh)
        {
            return nullptr;
        }

        /*
         * Far LOD is generated and replaced at runtime.
         * Keep the same movable lighting path as normal voxel chunks;
         * Static mobility can make runtime procedural meshes receive
         * noticeably darker/different lighting.
         */
        Mesh->SetMobility(
            EComponentMobility::Movable);

        Mesh->SetCollisionEnabled(
            ECollisionEnabled::NoCollision);

        Mesh->SetGenerateOverlapEvents(
            false);

        Mesh->SetMaterial(0, Material);
        Mesh->SetMaterial(1, WaterMaterial);

        Mesh->RegisterComponent();

        return Mesh;
    }
}


void AVoxelWorld::UpdateFarLOD(
    const FIntVector& CenterChunk)
{
    ++FarLODGenerationVersion;

    /*
     * Do not let old large-ring builds continue consuming the worker pool
     * after the player enters a new chunk. Their results would be discarded
     * anyway, so cancel the previous generation cooperatively.
     */
    if (FarLODCancellationToken.IsValid())
    {
        FarLODCancellationToken->AtomicSet(true);
    }

    FarLODCancellationToken =
        MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);

    const TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> LocalCancellationToken =
        FarLODCancellationToken;

    PendingVoxelLODMeshResults.Empty();

    const uint32 LocalGeneration =
        FarLODGenerationVersion;

    if (!FarLOD1Mesh)
    {
        FarLOD1Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD1"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD2Mesh)
    {
        FarLOD2Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD2"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD3Mesh)
    {
        FarLOD3Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD3"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD4Mesh)
    {
        FarLOD4Mesh =
            CreateFarLODMeshComponent(
                this,
                TEXT("FarLOD4"),
                Material,
                WaterMaterial);
    }

    if (!FarLOD1Mesh ||
        !FarLOD2Mesh ||
        !FarLOD3Mesh ||
        !FarLOD4Mesh)
    {
        return;
    }

    const FVoxelWorldGenerator GeneratorCopy =
        WorldGenerator;
    const TSharedPtr<TMap<FIntPoint, int32>, ESPMode::ThreadSafe> RoadLODHeightsCopy =
        RWGRoadLODHeights;

    const int32 LODWorldSizeX =
        WorldSizeX;

    const int32 LODWorldSizeY =
        WorldSizeY;

    const int32 LODChunkSize =
        ChunkSize;

    const float LODVoxelSize =
        VoxelSize;

    const int32 LODBeachWidth =
        BeachWidth;

    const int32 LODSeaLevel =
        SeaLevel;

    TWeakObjectPtr<AVoxelWorld> WeakWorld(this);

    /*
     * Builds one lightweight mesh for a complete ring.
     * No AVoxelChunk actors are created in this path.
     *
     * IMPORTANT:
     * Capture local copies of all AVoxelWorld properties.
     * Capturing class fields directly would require capturing this,
     * which is not allowed by this C++ lambda capture form.
     */
    auto ScheduleLOD =
        [
            WeakWorld,
            GeneratorCopy,
            RoadLODHeightsCopy,
            CenterChunk,
            LODWorldSizeX,
            LODWorldSizeY,
            LODChunkSize,
            LODVoxelSize,
            LODBeachWidth,
            LODSeaLevel,
            LocalGeneration,
            LocalCancellationToken
        ](
            UProceduralMeshComponent* Mesh,
            int32 SampleStep,
            int32 InnerRadius,
            int32 OuterRadius)
        {
            if (!Mesh || InnerRadius >= OuterRadius)
            {
                return;
            }

            FVoxelTerrainLODBuildInput BuildInput;

            BuildInput.Generator =
                GeneratorCopy;
            BuildInput.RoadSurfaceHeights =
                RoadLODHeightsCopy;

            BuildInput.WorldSizeX =
                LODWorldSizeX;

            BuildInput.WorldSizeY =
                LODWorldSizeY;

            BuildInput.ChunkSize =
                LODChunkSize;

            BuildInput.VoxelSize =
                LODVoxelSize;

            BuildInput.BeachWidth =
                LODBeachWidth;

            BuildInput.SeaLevel =
                LODSeaLevel;

            BuildInput.CenterChunk =
                CenterChunk;

            BuildInput.SampleStep =
                SampleStep;

            BuildInput.InnerRadiusChunks =
                InnerRadius;

            BuildInput.OuterRadiusChunks =
                OuterRadius;

            BuildInput.CancellationToken =
                LocalCancellationToken;

            TWeakObjectPtr<UProceduralMeshComponent> WeakMesh(
                Mesh);

            Async(
                EAsyncExecution::ThreadPool,
                [
                    BuildInput = MoveTemp(BuildInput),
                    WeakMesh,
                    WeakWorld,
                    LocalGeneration
                ]() mutable
                {
                    FVoxelTerrainLODMeshOutput Output;

                    FVoxelTerrainLODMesher::Build(
                        BuildInput,
                        Output);

                    if (BuildInput.CancellationToken.IsValid() &&
                        static_cast<bool>(*BuildInput.CancellationToken))
                    {
                        return;
                    }

                    AsyncTask(
                        ENamedThreads::GameThread,
                        [
                            Output = MoveTemp(Output),
                            WeakMesh,
                            WeakWorld,
                            LocalGeneration
                        ]() mutable
                        {
                            AVoxelWorld* World = WeakWorld.Get();
                            UProceduralMeshComponent* Mesh = WeakMesh.Get();

                            if (!World ||
                                !Mesh ||
                                World->FarLODGenerationVersion != LocalGeneration)
                            {
                                return;
                            }

                            World->QueueFarLODMeshResult(
                                Mesh,
                                MoveTemp(Output),
                                LocalGeneration);
                        });
                });
        };

    /*
     * Replacing the complete mesh section on update keeps the ring
     * deterministic and prevents stale geometry from previous centers.
     */
    /*
     * Keep mountain silhouettes and the broad terrain profile faithful at
     * distance. Each ring uses a sampling step that doubles outward; the
     * water surface has its own finer grid in FVoxelTerrainLODMesher.
     * Exact shared generator heights keep borders aligned between rings.
     */
    ScheduleLOD(
        FarLOD1Mesh,
        1,
        StreamingRadius + 1,
        LOD1Radius);

    ScheduleLOD(
        FarLOD2Mesh,
        2,
        LOD1Radius + 1,
        LOD2Radius);

    ScheduleLOD(
        FarLOD3Mesh,
        4,
        LOD2Radius + 1,
        LOD3Radius);

    ScheduleLOD(
        FarLOD4Mesh,
        8,
        LOD3Radius + 1,
        LOD4Radius);
}

