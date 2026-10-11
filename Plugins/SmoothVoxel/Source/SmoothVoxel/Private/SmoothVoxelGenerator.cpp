
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelMCTables.h"
#include "Math/UnrealMathUtility.h"
#include <cmath>

namespace
{
    FORCEINLINE int32 Idx(
        int32 X,
        int32 Y,
        int32 Z,
        int32 N)
    {
        return X + Y * N + Z * N * N;
    }

    FORCEINLINE double Fade(double T)
    {
        return T * T * (3.0 - 2.0 * T);
    }

    FORCEINLINE double Lerp3D(
        double C000,
        double C100,
        double C010,
        double C110,
        double C001,
        double C101,
        double C011,
        double C111,
        double TX,
        double TY,
        double TZ)
    {
        const double X00 = FMath::Lerp(C000, C100, TX);
        const double X10 = FMath::Lerp(C010, C110, TX);
        const double X01 = FMath::Lerp(C001, C101, TX);
        const double X11 = FMath::Lerp(C011, C111, TX);

        const double Y0 = FMath::Lerp(X00, X10, TY);
        const double Y1 = FMath::Lerp(X01, X11, TY);
        return FMath::Lerp(Y0, Y1, TZ);
    }

    FORCEINLINE int64 FloorToInt64(double Value)
    {
        return static_cast<int64>(std::floor(Value));
    }

    FORCEINLINE uint64 MixHash64(uint64 H)
    {
        H ^= H >> 30;
        H *= 0xBF58476D1CE4E5B9ull;
        H ^= H >> 27;
        H *= 0x94D049BB133111EBull;
        H ^= H >> 31;
        return H;
    }

    FORCEINLINE uint32 HashLattice2D(int64 X, int64 Y, int32 Seed)
    {
        uint64 H =
            static_cast<uint64>(X) * 0x9E3779B185EBCA87ull ^
            static_cast<uint64>(Y) * 0xC2B2AE3D27D4EB4Full ^
            static_cast<uint64>(static_cast<uint32>(Seed)) * 0x165667B19E3779F9ull;
        return static_cast<uint32>(MixHash64(H) >> 40) & 0x00FFFFFFu;
    }

    FORCEINLINE uint32 HashLattice3D(int64 X, int64 Y, int64 Z, int32 Seed)
    {
        uint64 H =
            static_cast<uint64>(X) * 0x9E3779B185EBCA87ull ^
            static_cast<uint64>(Y) * 0xC2B2AE3D27D4EB4Full ^
            static_cast<uint64>(Z) * 0x165667B19E3779F9ull ^
            static_cast<uint64>(static_cast<uint32>(Seed)) * 0x85EBCA77C2B2AE63ull;
        return static_cast<uint32>(MixHash64(H) >> 40) & 0x00FFFFFFu;
    }

    FORCEINLINE double SmoothMaximum(double A, double B, double BlendWidth)
    {
        if (BlendWidth <= SMALL_NUMBER)
        {
            return FMath::Max(A, B);
        }

        const double H = FMath::Clamp(
            0.5 + 0.5 * (A - B) / BlendWidth,
            0.0,
            1.0);
        return FMath::Lerp(B, A, H) + BlendWidth * H * (1.0 - H);
    }

    FORCEINLINE double BoxSdf(
        double X,
        double Y,
        double Z,
        double CenterX,
        double CenterY,
        double CenterZ,
        double HalfExtent,
        double RoundRadius)
    {
        const double CoreHalfExtent = FMath::Max(0.0, HalfExtent - RoundRadius);
        const double QX = std::abs(X - CenterX) - CoreHalfExtent;
        const double QY = std::abs(Y - CenterY) - CoreHalfExtent;
        const double QZ = std::abs(Z - CenterZ) - CoreHalfExtent;
        const double OutsideX = FMath::Max(QX, 0.0);
        const double OutsideY = FMath::Max(QY, 0.0);
        const double OutsideZ = FMath::Max(QZ, 0.0);
        const double OutsideDistance = std::sqrt(
            OutsideX * OutsideX + OutsideY * OutsideY + OutsideZ * OutsideZ);
        const double InsideDistance = FMath::Min(
            FMath::Max(QX, FMath::Max(QY, QZ)),
            0.0);
        return OutsideDistance + InsideDistance - RoundRadius;
    }

    FORCEINLINE float ApplyBlockEdits(
        double X,
        double Y,
        double Z,
        float Density,
        const FSVoxelGenerationSettings& Settings)
    {
        const bool bHasBlockEdits =
            Settings.BlockEdits.IsValid() && Settings.BlockEdits->Num() > 0;
        const bool bHasMinedNodes =
            Settings.MinedNodes.IsValid() && Settings.MinedNodes->Num() > 0;
        if (!bHasBlockEdits && !bHasMinedNodes)
        {
            return Density;
        }

        const double Step = FMath::Max(1.0, static_cast<double>(Settings.BaseVoxelSize));
        const int32 BaseX = static_cast<int32>(std::floor(X / Step));
        const int32 BaseY = static_cast<int32>(std::floor(Y / Step));
        const int32 BaseZ = static_cast<int32>(std::floor(Z / Step));

        // Block placement is a visual/collision actor and must not carve
        // procedural terrain. State +2 is only a logical occupancy marker.
        // Terrain mining uses MinedNodes as integer voxel-cell coordinates.
        const double RoundRadius = Step * 0.28;
        const double SmoothBlend = Step * 0.10;

        int8 CandidateStates[27];
        double CandidateSdfs[27];
        int32 CandidateCount = 0;

        // A completed mining hit removes exactly one grid cell. The cut is
        // centred at (cell + 0.5) * Step, not at a lattice corner: repeated
        // hits while looking vertically down therefore remove successive cells
        // in the same column and create flat floors/walls instead of a drifting
        // octahedral tunnel.
        if (bHasMinedNodes)
        {
            // Make the cell corners just inside the cutter. If the box
            // ended exactly on the lattice corners, their density could remain
            // zero/solid and the marching-cubes surface would leave a paper-thin
            // cap instead of a complete excavated cell.
            const double CutHalfExtent = Step * 0.5 + FMath::Max(0.05, Step * 0.005);
            const double CutRoundRadius = 0.0;
            for (int32 DZ = -1; DZ <= 1; ++DZ)
            {
                for (int32 DY = -1; DY <= 1; ++DY)
                {
                    for (int32 DX = -1; DX <= 1; ++DX)
                    {
                        const FIntVector Cell(BaseX + DX, BaseY + DY, BaseZ + DZ);
                        if (!Settings.MinedNodes->Contains(Cell))
                        {
                            continue;
                        }

                        const double CenterX = (static_cast<double>(Cell.X) + 0.5) * Step;
                        const double CenterY = (static_cast<double>(Cell.Y) + 0.5) * Step;
                        const double CenterZ = (static_cast<double>(Cell.Z) + 0.5) * Step;
                        const double CellSdf = BoxSdf(
                            X, Y, Z, CenterX, CenterY, CenterZ,
                            CutHalfExtent, CutRoundRadius);
                        Density = FMath::Min(Density, static_cast<float>(CellSdf));
                    }
                }
            }
        }

        if (bHasBlockEdits)
        {
            for (int32 DZ = -1; DZ <= 1; ++DZ)
            {
                for (int32 DY = -1; DY <= 1; ++DY)
                {
                    for (int32 DX = -1; DX <= 1; ++DX)
                    {
                        const FIntVector Cell(BaseX + DX, BaseY + DY, BaseZ + DZ);
                        const int8* EditState = Settings.BlockEdits->Find(Cell);
                        if (!EditState || *EditState == 0 || *EditState == 2 ||
                            *EditState > 2 || *EditState < -5)
                        {
                            continue;
                        }

                        double CenterX = (static_cast<double>(Cell.X) + 0.5) * Step;
                        double CenterY = (static_cast<double>(Cell.Y) + 0.5) * Step;
                        double CenterZ = (static_cast<double>(Cell.Z) + 0.5) * Step;
                        if (Settings.BlockEditCenters.IsValid())
                        {
                            if (const FVector* EditCenter = Settings.BlockEditCenters->Find(Cell))
                            {
                                CenterX = static_cast<double>(EditCenter->X);
                                CenterY = static_cast<double>(EditCenter->Y);
                                CenterZ = static_cast<double>(EditCenter->Z);
                            }
                        }

                        CandidateStates[CandidateCount] = *EditState;
                        if (*EditState == 1)
                        {
                            CandidateSdfs[CandidateCount] = BoxSdf(
                                X, Y, Z, CenterX, CenterY, CenterZ,
                                Step * 0.5, RoundRadius);
                        }
                        else
                        {
                            // Legacy cell-removal edits use the same axis-aligned
                            // voxel footprint as ray mining. The tiny overhang
                            // makes the shared boundary unambiguously air for the
                            // marching-cubes sampler without cutting into the
                            // centre of a neighbouring cell.
                            const double CutHalfExtent = Step * 0.5 +
                                FMath::Max(0.05, Step * 0.005);
                            CandidateSdfs[CandidateCount] = BoxSdf(
                                X, Y, Z, CenterX, CenterY, CenterZ,
                                CutHalfExtent, 0.0);
                        }
                        ++CandidateCount;
                    }
                }
            }

            // Add soil/sand before cuts so excavations always win overlaps.
            for (int32 Index = 0; Index < CandidateCount; ++Index)
            {
                if (CandidateStates[Index] == 1)
                {
                    Density = static_cast<float>(SmoothMaximum(
                        static_cast<double>(Density),
                        -CandidateSdfs[Index],
                        SmoothBlend));
                }
            }

            for (int32 Index = 0; Index < CandidateCount; ++Index)
            {
                const int8 State = CandidateStates[Index];
                if (State < 0)
                {
                    // Cell removal is a Boolean box cut, just like a completed
                    // mining hit. A smooth minimum rounds the walls and can
                    // leak into adjacent cells, defeating voxel-aligned mining.
                    Density = FMath::Min(Density, static_cast<float>(CandidateSdfs[Index]));
                }
            }
        }


        return Density;
    }
}


// ============================================================
// 2D HASH NOISE
// ============================================================

float FSVoxelGenerator::HashNoise2D(
    double X,
    double Y,
    int32 Seed)
{
    // Use double for fractional noise coordinates and integer hashing for
    // lattice points. A sine hash of large float inputs loses mantissa bits and
    // can cluster/repeat values across distant rows of terrain.
    const int64 IX = FloorToInt64(X);
    const int64 IY = FloorToInt64(Y);
    const double FX = X - static_cast<double>(IX);
    const double FY = Y - static_cast<double>(IY);

    const double A = static_cast<double>(HashLattice2D(IX,     IY,     Seed)) / 16777215.0;
    const double B = static_cast<double>(HashLattice2D(IX + 1, IY,     Seed)) / 16777215.0;
    const double C = static_cast<double>(HashLattice2D(IX,     IY + 1, Seed)) / 16777215.0;
    const double D = static_cast<double>(HashLattice2D(IX + 1, IY + 1, Seed)) / 16777215.0;

    const double SX = Fade(FX);
    const double SY = Fade(FY);
    const double AB = FMath::Lerp(A, B, SX);
    const double CD = FMath::Lerp(C, D, SX);
    return static_cast<float>(FMath::Lerp(AB, CD, SY) * 2.0 - 1.0);
}


// ============================================================
// 2D FBM
// ============================================================

float FSVoxelGenerator::FBM2D(
    double X,
    double Y,
    int32 Seed)
{
    double Sum = 0.0;
    double Amplitude = 1.0;
    double Frequency = 1.0;
    double Normalization = 0.0;

    for (int32 Octave = 0; Octave < 3; ++Octave)
    {
        Sum += static_cast<double>(HashNoise2D(
            X * Frequency,
            Y * Frequency,
            Seed + Octave * 101)) * Amplitude;

        Normalization += Amplitude;
        Amplitude *= 0.5;
        Frequency *= 2.0;
    }

    return static_cast<float>(Sum / Normalization);
}


// ============================================================
// 3D HASH
// ============================================================

float FSVoxelGenerator::HashNoise3D(
    int64 X,
    int64 Y,
    int64 Z,
    int32 Seed)
{
    return static_cast<float>(HashLattice3D(X, Y, Z, Seed)) / 16777215.0f;
}


// ============================================================
// SMOOTH 3D VALUE NOISE
// ============================================================

float FSVoxelGenerator::SmoothNoise3D(
    double X,
    double Y,
    double Z,
    int32 Seed)
{
    const int64 IX = FloorToInt64(X);
    const int64 IY = FloorToInt64(Y);
    const int64 IZ = FloorToInt64(Z);

    const double FX = X - static_cast<double>(IX);
    const double FY = Y - static_cast<double>(IY);
    const double FZ = Z - static_cast<double>(IZ);

    const double TX = Fade(FX);
    const double TY = Fade(FY);
    const double TZ = Fade(FZ);

    const double C000 = HashNoise3D(IX,     IY,     IZ,     Seed);
    const double C100 = HashNoise3D(IX + 1, IY,     IZ,     Seed);
    const double C010 = HashNoise3D(IX,     IY + 1, IZ,     Seed);
    const double C110 = HashNoise3D(IX + 1, IY + 1, IZ,     Seed);
    const double C001 = HashNoise3D(IX,     IY,     IZ + 1, Seed);
    const double C101 = HashNoise3D(IX + 1, IY,     IZ + 1, Seed);
    const double C011 = HashNoise3D(IX,     IY + 1, IZ + 1, Seed);
    const double C111 = HashNoise3D(IX + 1, IY + 1, IZ + 1, Seed);

    return static_cast<float>(Lerp3D(
        C000, C100, C010, C110, C001, C101, C011, C111,
        TX, TY, TZ) * 2.0 - 1.0);
}


// ============================================================
// 3D FBM
// ============================================================

float FSVoxelGenerator::FBM3D(
    double X,
    double Y,
    double Z,
    int32 Seed)
{
    double Sum = 0.0;
    double Amplitude = 1.0;
    double Frequency = 1.0;
    double Normalization = 0.0;

    for (int32 Octave = 0; Octave < 3; ++Octave)
    {
        Sum += static_cast<double>(SmoothNoise3D(
            X * Frequency,
            Y * Frequency,
            Z * Frequency,
            Seed + Octave * 137)) * Amplitude;

        Normalization += Amplitude;
        Amplitude *= 0.5;
        Frequency *= 2.0;
    }

    return static_cast<float>(Sum / Normalization);
}


// ============================================================
// DENSITY
// ============================================================

void FSVoxelGenerator::SampleBiomeClimate(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S,
    float& OutTemperature,
    float& OutMoisture,
    float& OutMountainSignal)
{
    const double Scale = FMath::Max(0.00000001, std::abs(static_cast<double>(S.BiomeScale)));
    OutTemperature = FBM2D(X * Scale * 0.60, Y * Scale * 0.60, S.Seed + 1709);
    OutMoisture = HashNoise2D(X * Scale * 0.85, Y * Scale * 0.85, S.Seed + 2903);
    OutMountainSignal = HashNoise2D(X * Scale * 1.45, Y * Scale * 1.45, S.Seed + 3907);
}

float FSVoxelGenerator::SampleTerrainNoise(
    double WorldX,
    double WorldY,
    const FSVoxelGenerationSettings& Settings)
{
    return FBM2D(
        WorldX * static_cast<double>(Settings.NoiseScale),
        WorldY * static_cast<double>(Settings.NoiseScale),
        Settings.Seed);
}

ESVoxelBiome FSVoxelGenerator::SampleBiome(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    if (!S.bEnableBiomes)
    {
        return ESVoxelBiome::Plains;
    }

    float Temperature = 0.0f;
    float Moisture = 0.0f;
    float MountainSignal = 0.0f;
    SampleBiomeClimate(X, Y, S, Temperature, Moisture, MountainSignal);

    if (Temperature < -0.38f || (MountainSignal > 0.26f && Temperature < -0.08f))
    {
        return ESVoxelBiome::Snow;
    }
    if (Temperature > 0.18f && Moisture < 0.05f)
    {
        return ESVoxelBiome::Desert;
    }
    if (MountainSignal > 0.32f)
    {
        return ESVoxelBiome::Mountain;
    }
    if (Moisture > 0.12f)
    {
        return ESVoxelBiome::Forest;
    }
    return ESVoxelBiome::Plains;
}

double FSVoxelGenerator::SampleUnflattenedSurfaceHeight(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    const double Macro = FBM2D(
        X * static_cast<double>(S.NoiseScale),
        Y * static_cast<double>(S.NoiseScale),
        S.Seed);
    const double Detail = FBM2D(
        X * static_cast<double>(S.DetailScale),
        Y * static_cast<double>(S.DetailScale),
        S.Seed + 777);

    double ReliefMultiplier = 1.0;
    double DetailMultiplier = 1.0;
    double MountainLift = 0.0;
    double DuneLift = 0.0;

    if (S.bEnableBiomes)
    {
        float Temperature = 0.0f;
        float Moisture = 0.0f;
        float MountainSignal = 0.0f;
        SampleBiomeClimate(X, Y, S, Temperature, Moisture, MountainSignal);

        const auto SmoothStep = [](float Edge0, float Edge1, float Value)
        {
            const float T = FMath::Clamp(
                (Value - Edge0) / FMath::Max(0.0001f, Edge1 - Edge0), 0.0f, 1.0f);
            return T * T * (3.0f - 2.0f * T);
        };

        // The continuous climate masks shape the terrain; discrete biome labels
        // are for gameplay and materials. Smooth masks prevent cliffs at borders.
        const float MountainBlend = SmoothStep(0.10f, 0.52f, MountainSignal);
        const float ForestBlend = SmoothStep(-0.10f, 0.48f, Moisture);
        const float DesertBlend = SmoothStep(0.05f, 0.50f, Temperature) *
            (1.0f - SmoothStep(-0.18f, 0.42f, Moisture));
        const float ColdBlend = SmoothStep(0.12f, 0.58f, -Temperature);

        ReliefMultiplier = 0.42f + ForestBlend * 0.22f + DesertBlend * 0.08f +
            MountainBlend * 1.25f + ColdBlend * 0.14f;
        DetailMultiplier = 1.0f + DesertBlend * 0.65f + MountainBlend * 0.15f;
        MountainLift = static_cast<double>(FMath::Max(0.0f, MountainSignal)) *
            static_cast<double>(FMath::Abs(S.HeightAmplitude)) * 0.30 *
            static_cast<double>(MountainBlend);

        if (DesertBlend > 0.001f)
        {
            const float Dune = FMath::Abs(HashNoise2D(
                X * static_cast<double>(S.BiomeScale) * 2.3,
                Y * static_cast<double>(S.BiomeScale) * 2.3,
                S.Seed + 4909));
            DuneLift = static_cast<double>(Dune) *
                static_cast<double>(FMath::Abs(S.HeightAmplitude)) * 0.10 *
                static_cast<double>(DesertBlend);
        }
    }

    return static_cast<double>(S.BaseHeight) +
        Macro * static_cast<double>(S.HeightAmplitude) * ReliefMultiplier +
        Detail * static_cast<double>(S.DetailAmplitude) * DetailMultiplier +
        MountainLift + DuneLift;
}

float FSVoxelGenerator::GetPOIPlateauInfluence(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S,
    double& OutSiteX,
    double& OutSiteY)
{
    OutSiteX = X;
    OutSiteY = Y;
    if (!S.bEnablePOIPlateaus)
    {
        return 0.0f;
    }

    const double Spacing = FMath::Max(20000.0, std::abs(static_cast<double>(S.POISiteSpacing)));
    const int64 GridX = static_cast<int64>(std::round(X / Spacing));
    const int64 GridY = static_cast<int64>(std::round(Y / Spacing));
    const uint32 SiteHash = HashLattice2D(GridX, GridY, S.Seed + 5167);

    // Split one stable hash into two bounded jitters. Radius plus jitter stays
    // under half the grid spacing, so no terrain sample is influenced by two sites.
    const double JitterX = (static_cast<double>(SiteHash & 0x0FFFu) / 4095.0 - 0.5) * 0.30;
    const double JitterY = (static_cast<double>((SiteHash >> 12) & 0x0FFFu) / 4095.0 - 0.5) * 0.30;
    OutSiteX = static_cast<double>(GridX) * Spacing + JitterX * Spacing;
    OutSiteY = static_cast<double>(GridY) * Spacing + JitterY * Spacing;

    const double CoreRadius = FMath::Clamp(static_cast<double>(S.POIPlateauRadius), 0.0, Spacing * 0.25);
    const double MaximumInfluenceRadius = Spacing * 0.30;
    const double BlendDistance = FMath::Clamp(
        static_cast<double>(S.POIPlateauBlendDistance),
        0.0,
        FMath::Max(0.0, MaximumInfluenceRadius - CoreRadius));
    const double OuterRadius = CoreRadius + BlendDistance;
    if (OuterRadius <= 0.0)
    {
        return 0.0f;
    }

    const double DX = X - OutSiteX;
    const double DY = Y - OutSiteY;
    const double DistanceSquared = DX * DX + DY * DY;
    if (DistanceSquared > OuterRadius * OuterRadius)
    {
        return 0.0f;
    }

    // Protect steep mountain zones, but allow clearings in plains, forests,
    // deserts and snowy biomes.
    if (SampleBiome(OutSiteX, OutSiteY, S) == ESVoxelBiome::Mountain)
    {
        return 0.0f;
    }

    const double Distance = std::sqrt(DistanceSquared);
    if (Distance <= CoreRadius)
    {
        return 1.0f;
    }
    if (BlendDistance <= SMALL_NUMBER)
    {
        return 0.0f;
    }

    const float T = FMath::Clamp(static_cast<float>((Distance - CoreRadius) / BlendDistance), 0.0f, 1.0f);
    return 1.0f - T * T * (3.0f - 2.0f * T);
}

float FSVoxelGenerator::SamplePOISuitability(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    double SiteX = X;
    double SiteY = Y;
    return GetPOIPlateauInfluence(X, Y, S, SiteX, SiteY);
}

FLinearColor FSVoxelGenerator::GetBiomeTint(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    if (!S.bEnableBiomes)
    {
        return FLinearColor::White;
    }

    FLinearColor Tint;
    switch (SampleBiome(X, Y, S))
    {
    case ESVoxelBiome::Forest:
        Tint = FLinearColor(0.22f, 0.39f, 0.16f, 1.0f);
        break;
    case ESVoxelBiome::Desert:
        Tint = FLinearColor(0.68f, 0.48f, 0.25f, 1.0f);
        break;
    case ESVoxelBiome::Mountain:
        Tint = FLinearColor(0.38f, 0.36f, 0.33f, 1.0f);
        break;
    case ESVoxelBiome::Snow:
        Tint = FLinearColor(0.78f, 0.83f, 0.88f, 1.0f);
        break;
    case ESVoxelBiome::Plains:
    default:
        Tint = FLinearColor(0.46f, 0.49f, 0.25f, 1.0f);
        break;
    }

    const float ClearingWeight = SamplePOISuitability(X, Y, S);
    const FLinearColor BareEarth(0.40f, 0.32f, 0.22f, 1.0f);
    return FMath::Lerp(Tint, BareEarth, ClearingWeight * 0.45f);
}

void FSVoxelGenerator::SampleRiverMaskAndField(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S,
    float& OutInfluence,
    float& OutSignedField)
{
    OutInfluence = 0.0f;
    OutSignedField = 0.0f;
    if (!S.bEnableRivers)
    {
        return;
    }

    const double Scale = FMath::Max(0.00000001, std::abs(static_cast<double>(S.RiverScale)));
    const double HalfWidth = FMath::Max(0.0, std::abs(static_cast<double>(S.RiverHalfWidth)));
    if (HalfWidth <= 0.0)
    {
        return;
    }

    // Domain warp makes the zero-contours meander instead of looking like a
    // repeated grid. The signed field also provides a stable local channel
    // normal, allowing river water to stay level across the width of its bed.
    const double WarpScale = Scale * 0.37;
    const double WarpX = FBM2D(X * WarpScale, Y * WarpScale, S.Seed + 8111);
    const double WarpY = FBM2D(X * WarpScale + 19.7, Y * WarpScale - 31.3, S.Seed + 8231);
    const double WarpDistance = HalfWidth * 2.0;
    const double Field = FBM2D(
        (X + WarpX * WarpDistance) * Scale,
        (Y + WarpY * WarpDistance) * Scale,
        S.Seed + 8329);

    // Convert world-space channel width into a dimensionless band around the
    // field's zero-contour. Smooth falloff avoids hard cliffs at the carved banks.
    const double Threshold = FMath::Clamp(HalfWidth * Scale * 0.48, 0.008, 0.20);
    const float T = FMath::Clamp(
        static_cast<float>(std::abs(Field) / Threshold),
        0.0f,
        1.0f);
    const float SmoothT = T * T * (3.0f - 2.0f * T);
    OutInfluence = 1.0f - SmoothT;
    OutSignedField = static_cast<float>(Field);
}

float FSVoxelGenerator::SampleRiverInfluence(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    float Influence = 0.0f;
    float SignedField = 0.0f;
    SampleRiverMaskAndField(X, Y, S, Influence, SignedField);
    return Influence;
}

double FSVoxelGenerator::SampleSurfaceHeight(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    const double NaturalHeight = SampleUnflattenedSurfaceHeight(X, Y, S);
    double SiteX = X;
    double SiteY = Y;
    const float PlateauWeight = GetPOIPlateauInfluence(X, Y, S, SiteX, SiteY);

    double SurfaceHeight = NaturalHeight;
    if (PlateauWeight > 0.0f)
    {
        // Keep the clearing at the terrain's natural centre height, with a smooth
        // falloff into surrounding ground. Interior samples are exactly planar.
        const double PlateauHeight = SampleUnflattenedSurfaceHeight(SiteX, SiteY, S);
        SurfaceHeight = FMath::Lerp(
            NaturalHeight,
            PlateauHeight,
            static_cast<double>(PlateauWeight));
    }

    // Rivers do not own a second, sloped water mesh. Instead, carve a smooth
    // channel down to a shared absolute water elevation. The globally level sea
    // plane then fills every channel, exactly like a lake; terrain above that
    // plane naturally masks the water and no river triangles can tear or float.
    if (S.bEnableRivers)
    {
        const double RiverInfluence = FMath::Clamp(
            static_cast<double>(SampleRiverInfluence(X, Y, S)),
            0.0,
            1.0);
        const double RiverBedTarget = static_cast<double>(S.RiverWaterLevel) -
            std::abs(static_cast<double>(S.RiverDepth));
        const double CarvedTarget = FMath::Min(SurfaceHeight, RiverBedTarget);
        SurfaceHeight = FMath::Lerp(SurfaceHeight, CarvedTarget, RiverInfluence);
    }
    return SurfaceHeight;
}

double FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(
    const FVector& LocalPosition,
    const FSVoxelGenerationSettings& Settings)
{
    double WorldX;
    double WorldY;
    if (Settings.bUseChunkLatticeCoordinates)
    {
        const double BaseStep = static_cast<double>(Settings.BaseVoxelSize);
        WorldX = static_cast<double>(Settings.ChunkOriginCells.X) * BaseStep +
            static_cast<double>(LocalPosition.X);
        WorldY = static_cast<double>(Settings.ChunkOriginCells.Y) * BaseStep +
            static_cast<double>(LocalPosition.Y);
    }
    else
    {
        WorldX = static_cast<double>(Settings.ChunkWorldOrigin.X) +
            static_cast<double>(LocalPosition.X);
        WorldY = static_cast<double>(Settings.ChunkWorldOrigin.Y) +
            static_cast<double>(LocalPosition.Y);
    }

    return SampleSurfaceHeight(WorldX, WorldY, Settings);
}

float FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
    const FVector& LocalPosition,
    double SurfaceHeight,
    const FSVoxelGenerationSettings& Settings)
{
    double WorldX;
    double WorldY;
    double WorldZ;
    if (Settings.bUseChunkLatticeCoordinates)
    {
        const double BaseStep = static_cast<double>(Settings.BaseVoxelSize);
        WorldX = static_cast<double>(Settings.ChunkOriginCells.X) * BaseStep +
            static_cast<double>(LocalPosition.X);
        WorldY = static_cast<double>(Settings.ChunkOriginCells.Y) * BaseStep +
            static_cast<double>(LocalPosition.Y);
        WorldZ = static_cast<double>(Settings.ChunkOriginCells.Z) * BaseStep +
            static_cast<double>(LocalPosition.Z);
    }
    else
    {
        WorldX = static_cast<double>(Settings.ChunkWorldOrigin.X) +
            static_cast<double>(LocalPosition.X);
        WorldY = static_cast<double>(Settings.ChunkWorldOrigin.Y) +
            static_cast<double>(LocalPosition.Y);
        WorldZ = static_cast<double>(Settings.ChunkWorldOrigin.Z) +
            static_cast<double>(LocalPosition.Z);
    }

    return DensityFromSurfaceHeight(WorldX, WorldY, WorldZ, SurfaceHeight, Settings);
}

float FSVoxelGenerator::Density(
    double X,
    double Y,
    double Z,
    const FSVoxelGenerationSettings& S)
{
    return DensityFromSurfaceHeight(X, Y, Z, SampleSurfaceHeight(X, Y, S), S);
}

float FSVoxelGenerator::DensityFromSurfaceHeight(
    double X,
    double Y,
    double Z,
    double SurfaceHeight,
    const FSVoxelGenerationSettings& S)
{
    // Positive = solid, negative = air.
    double D = SurfaceHeight - Z;

    // Preserve the solid-bottom behaviour of the original density field.
    if (Z < SurfaceHeight - S.BottomDepth)
    {
        D = 1.0;
    }

    if (S.bEnableCaves && S.CaveStrength > 0.0f)
    {
        const float Cave = FBM3D(
            X * static_cast<double>(S.CaveScale),
            Y * static_cast<double>(S.CaveScale),
            Z * static_cast<double>(S.CaveScale),
            S.Seed + 4242);

        const float Range = FMath::Max(0.001f, 1.0f - S.CaveThreshold);
        float T = (Cave - S.CaveThreshold) / Range;
        T = FMath::Clamp(T, 0.0f, 1.0f);
        const float CaveMask = T * T * (3.0f - 2.0f * T);

        if (D > 0.0)
        {
            const float CaveCarve = CaveMask * 600.0f * S.CaveStrength;
            D -= CaveCarve;
        }
    }

    return ApplyBlockEdits(X, Y, Z, static_cast<float>(D), S);
}

float FSVoxelGenerator::SampleDensity(
    double WorldX,
    double WorldY,
    double WorldZ,
    const FSVoxelGenerationSettings& Settings)
{
    return Density(WorldX, WorldY, WorldZ, Settings);
}

float FSVoxelGenerator::SampleDensityAtChunkLocal(
    const FVector& LocalPosition,
    const FSVoxelGenerationSettings& Settings)
{
    double WorldX;
    double WorldY;
    double WorldZ;
    if (Settings.bUseChunkLatticeCoordinates)
    {
        const double BaseStep = static_cast<double>(Settings.BaseVoxelSize);
        WorldX = static_cast<double>(Settings.ChunkOriginCells.X) * BaseStep + static_cast<double>(LocalPosition.X);
        WorldY = static_cast<double>(Settings.ChunkOriginCells.Y) * BaseStep + static_cast<double>(LocalPosition.Y);
        WorldZ = static_cast<double>(Settings.ChunkOriginCells.Z) * BaseStep + static_cast<double>(LocalPosition.Z);
    }
    else
    {
        WorldX = static_cast<double>(Settings.ChunkWorldOrigin.X) + static_cast<double>(LocalPosition.X);
        WorldY = static_cast<double>(Settings.ChunkWorldOrigin.Y) + static_cast<double>(LocalPosition.Y);
        WorldZ = static_cast<double>(Settings.ChunkWorldOrigin.Z) + static_cast<double>(LocalPosition.Z);
    }

    return Density(WorldX, WorldY, WorldZ, Settings);
}

void FSVoxelGenerator::ClampNoiseScalesForMaxSampleStep(
    FSVoxelGenerationSettings& Settings,
    float CoarsestVoxelStep)
{
    // FBM2D uses three octaves with frequency multipliers 1, 2 and 4.
    // Keep the highest octave below 0.35 noise-lattice units per coarsest
    // sample. Use the same clamped scales for every LOD so this remains one
    // deterministic world-space density field instead of different fields
    // which could disagree along transition seams.
    const float MaxNoiseScale =
        0.35f / (FMath::Max(1.0f, CoarsestVoxelStep) * 4.0f);
    Settings.NoiseScale = FMath::Min(FMath::Max(0.0f, Settings.NoiseScale), MaxNoiseScale);
    Settings.DetailScale = FMath::Min(FMath::Max(0.0f, Settings.DetailScale), MaxNoiseScale * 0.75f);
}


// ============================================================
// INTERPOLATION
// ============================================================

FVector FSVoxelGenerator::Interpolate(
    const FVector& A,
    const FVector& B,
    float VA,
    float VB,
    float Iso)
{
    const float Denominator = VB - VA;

    if (FMath::Abs(VA - Iso) < KINDA_SMALL_NUMBER)
    {
        return A;
    }

    if (FMath::Abs(VB - Iso) < KINDA_SMALL_NUMBER)
    {
        return B;
    }

    if (FMath::Abs(Denominator) < KINDA_SMALL_NUMBER)
    {
        return (A + B) * 0.5f;
    }

    const float T =
        FMath::Clamp(
            (Iso - VA) / Denominator,
            0.0f,
            1.0f);

    return FMath::Lerp(A, B, T);
}


// ============================================================
// NORMAL
// ============================================================

FVector FSVoxelGenerator::DensityGradient(
    double X,
    double Y,
    double Z,
    float Step,
    const FSVoxelGenerationSettings& S)
{
    const float Dx =
        Density(X + Step, Y, Z, S) -
        Density(X - Step, Y, Z, S);

    const float Dy =
        Density(X, Y + Step, Z, S) -
        Density(X, Y - Step, Z, S);

    const float Dz =
        Density(X, Y, Z + Step, S) -
        Density(X, Y, Z - Step, S);

    // IMPORTANT:
    //
    // D = SurfaceHeight - Z
    //
    // Therefore gradient points DOWN for a flat surface.
    // For the outward terrain normal we need the gradient
    // itself in this coordinate convention.

    return FVector(
        Dx,
        Dy,
        Dz).GetSafeNormal();
}


// ============================================================
// GENERATE
// ============================================================

void FSVoxelGenerator::Generate(
    const FSVoxelGenerationSettings& S,
    FSVoxelMeshData& OutMesh)
{
    OutMesh.Reset();

    const int32 Cells =
        FMath::Max(
            2,
            S.CellsPerAxis);

    const int32 N = Cells + 1;

    TArray<float> Field;

    Field.SetNumUninitialized(
        N * N * N);

    // --------------------------------------------------------
    // SAMPLE DENSITY FIELD
    // --------------------------------------------------------

    for (int32 Z = 0; Z < N; ++Z)
    {
        const float LocalZ =
            float(Z) * S.VoxelSize;

        const float WorldZ =
            S.ChunkWorldOrigin.Z + LocalZ;

        for (int32 Y = 0; Y < N; ++Y)
        {
            const float LocalY =
                float(Y) * S.VoxelSize;

            const float WorldY =
                S.ChunkWorldOrigin.Y + LocalY;

            for (int32 X = 0; X < N; ++X)
            {
                const float LocalX =
                    float(X) * S.VoxelSize;

                const float WorldX =
                    S.ChunkWorldOrigin.X + LocalX;

                Field[
                    Idx(
                        X,
                        Y,
                        Z,
                        N)] =
                    Density(
                        WorldX,
                        WorldY,
                        WorldZ,
                        S);
            }
        }
    }

    // --------------------------------------------------------
    // RESERVE
    // --------------------------------------------------------

    const int32 ApproxCells =
        Cells * Cells * Cells;

    OutMesh.Vertices.Reserve(
        ApproxCells * 2);

    OutMesh.Triangles.Reserve(
        ApproxCells * 6);

    OutMesh.Normals.Reserve(
        ApproxCells * 2);

    OutMesh.UV0.Reserve(
        ApproxCells * 2);

    // --------------------------------------------------------
    // MARCHING CUBES
    // --------------------------------------------------------

    FVector EdgeVertex[12];
    FVector EdgeNormal[12];

    for (int32 Z = 0; Z < Cells; ++Z)
    {
        for (int32 Y = 0; Y < Cells; ++Y)
        {
            for (int32 X = 0; X < Cells; ++X)
            {
                float V[8];
                FVector P[8];

                int32 CubeIndex = 0;

                // ------------------------------------------------
                // GET 8 CORNERS
                // ------------------------------------------------

                for (int32 C = 0; C < 8; ++C)
                {
                    const int32 CX =
                        X +
                        SmoothVoxelMCTables::CornerOffset[C][0];

                    const int32 CY =
                        Y +
                        SmoothVoxelMCTables::CornerOffset[C][1];

                    const int32 CZ =
                        Z +
                        SmoothVoxelMCTables::CornerOffset[C][2];

                    V[C] =
                        Field[
                            Idx(
                                CX,
                                CY,
                                CZ,
                                N)];

                    P[C] =
                        FVector(
                            float(CX) * S.VoxelSize,
                            float(CY) * S.VoxelSize,
                            float(CZ) * S.VoxelSize);

                    if (V[C] >= S.IsoLevel)
                    {
                        CubeIndex |= (1 << C);
                    }
                }

                // ------------------------------------------------
                // LOOKUP
                // ------------------------------------------------

                const int32 EdgeMask =
                    SmoothVoxelMCTables::EdgeTable[CubeIndex];

                if (EdgeMask == 0)
                {
                    continue;
                }

                // ------------------------------------------------
                // CALCULATE EDGE VERTICES
                // ------------------------------------------------

                for (int32 E = 0; E < 12; ++E)
                {
                    if ((EdgeMask & (1 << E)) == 0)
                    {
                        continue;
                    }

                    const int32 A =
                        SmoothVoxelMCTables::EdgeCorners[E][0];

                    const int32 B =
                        SmoothVoxelMCTables::EdgeCorners[E][1];

                    EdgeVertex[E] =
                        Interpolate(
                            P[A],
                            P[B],
                            V[A],
                            V[B],
                            S.IsoLevel);

                    // Use a smaller gradient step.
                    //
                    // 35 units was too large compared with
                    // the actual terrain features.

                    const float GradientStep =
                        FMath::Max(
                            5.0f,
                            S.VoxelSize * 0.20f);

                    EdgeNormal[E] =
                        DensityGradient(
                            S.ChunkWorldOrigin.X + EdgeVertex[E].X,
                            S.ChunkWorldOrigin.Y + EdgeVertex[E].Y,
                            S.ChunkWorldOrigin.Z + EdgeVertex[E].Z,
                            GradientStep,
                            S);
                }

                // ------------------------------------------------
                // TRIANGLES
                // ------------------------------------------------

                const int8* Row =
                    SmoothVoxelMCTables::TriTable[CubeIndex];

                for (int32 I = 0;
                     I < 16 && Row[I] != -1;
                     I += 3)
                {
                    const int32 E0 = Row[I + 0];
                    const int32 E1 = Row[I + 1];
                    const int32 E2 = Row[I + 2];

                    if (E0 < 0 ||
                        E1 < 0 ||
                        E2 < 0)
                    {
                        break;
                    }

                    const int32 Base =
                        OutMesh.Vertices.Num();

                    // ------------------------------------------------
                    // VERTICES
                    // ------------------------------------------------

                    OutMesh.Vertices.Add(
                        EdgeVertex[E0]);

                    OutMesh.Vertices.Add(
                        EdgeVertex[E1]);

                    OutMesh.Vertices.Add(
                        EdgeVertex[E2]);

                    // ------------------------------------------------
                    // NORMALS
                    // ------------------------------------------------

                    OutMesh.Normals.Add(
                        EdgeNormal[E0]);

                    OutMesh.Normals.Add(
                        EdgeNormal[E1]);

                    OutMesh.Normals.Add(
                        EdgeNormal[E2]);

                    // ------------------------------------------------
                    // UV
                    // ------------------------------------------------

                    const FVector2D UV0(
                        EdgeVertex[E0].X / S.VoxelSize * 0.05f,
                        EdgeVertex[E0].Y / S.VoxelSize * 0.05f);

                    const FVector2D UV1(
                        EdgeVertex[E1].X / S.VoxelSize * 0.05f,
                        EdgeVertex[E1].Y / S.VoxelSize * 0.05f);

                    const FVector2D UV2(
                        EdgeVertex[E2].X / S.VoxelSize * 0.05f,
                        EdgeVertex[E2].Y / S.VoxelSize * 0.05f);

                    OutMesh.UV0.Add(UV0);
                    OutMesh.UV0.Add(UV1);
                    OutMesh.UV0.Add(UV2);

                    // ------------------------------------------------
                    // INDEX BUFFER
                    // ------------------------------------------------

                    OutMesh.Triangles.Add(
                        Base + 0);

                    OutMesh.Triangles.Add(
                        Base + 1);

                    OutMesh.Triangles.Add(
                        Base + 2);
                }
            }
        }
    }
}

