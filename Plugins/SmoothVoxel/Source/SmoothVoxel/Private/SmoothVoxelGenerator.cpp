
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
    // Mountain regions span several kilometres so they form rare traversable
    // ranges instead of noisy sub-kilometre spikes.
    OutMountainSignal = HashNoise2D(
        X * Scale * 0.35, Y * Scale * 0.35, S.Seed + 3907);
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

void FSVoxelGenerator::SampleBiomeRegionWeights(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S,
    float& OutDesertWeight,
    float& OutWinterWeight,
    float& OutTemperateWeight)
{
    OutDesertWeight = 0.0f;
    OutWinterWeight = 0.0f;
    OutTemperateWeight = 1.0f;
    if (!S.bEnableBiomes)
    {
        return;
    }

    const double HalfWorldSize = FMath::Max(
        1000.0, std::abs(static_cast<double>(S.BiomeWorldSize)) * 0.5);
    const double NX = (X - static_cast<double>(S.BiomeWorldCenter.X)) / HalfWorldSize;
    const double NY = (Y - static_cast<double>(S.BiomeWorldCenter.Y)) / HalfWorldSize;

    const auto SmoothStep = [](double Edge0, double Edge1, double Value)
    {
        const double T = FMath::Clamp(
            (Value - Edge0) / FMath::Max(0.000001, Edge1 - Edge0), 0.0, 1.0);
        return T * T * (3.0 - 2.0 * T);
    };

    // One large, rounded central temperate region. Low-frequency warp adds
    // broad lobes so its edge is irregular, but never breaks into scattered islands.
    const double CentralEdgeNoise = FBM2D(
        NX * 3.2 + 13.7, NY * 2.7 - 8.4, S.Seed + 4811);
    const double CentralDetailNoise = FBM2D(
        NX * 6.1 - 4.3, NY * 5.4 + 11.9, S.Seed + 4819);
    const double CentralRadius = 0.54 +
        CentralEdgeNoise * 0.045 + CentralDetailNoise * 0.018;
    const double RadialDistance = std::sqrt(
        NX * NX * 0.96 * 0.96 + NY * NY * 1.04 * 1.04);
    const double OutsideCentral = SmoothStep(
        CentralRadius - 0.04, CentralRadius + 0.04, RadialDistance);

    // Split the outer map into desert on the left and winter on the right,
    // using broad and fine warp so the boundary curves across the top and bottom.
    const double MainBorderWarp = FBM2D(
        NX * 1.8 + 22.1, NY * 2.4 - 17.3, S.Seed + 4829);
    const double SmallBorderWarp = FBM2D(
        NX * 5.0 - 9.7, NY * 4.2 + 3.6, S.Seed + 4831);
    const double SplitX = 0.015 + MainBorderWarp * 0.14 + SmallBorderWarp * 0.035;
    const double DesertSide = 1.0 - SmoothStep(
        SplitX - 0.045, SplitX + 0.045, NX);

    OutDesertWeight = static_cast<float>(OutsideCentral * DesertSide);
    OutWinterWeight = static_cast<float>(OutsideCentral * (1.0 - DesertSide));
    OutTemperateWeight = static_cast<float>(1.0 - OutsideCentral);
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

    float DesertWeight = 0.0f;
    float WinterWeight = 0.0f;
    float TemperateWeight = 1.0f;
    SampleBiomeRegionWeights(
        X, Y, S, DesertWeight, WinterWeight, TemperateWeight);

    // Material selection is discrete, but the geography is map-relative and
    // contiguous. The terrain surface itself uses continuous weights.
    if (TemperateWeight < 0.5f)
    {
        return DesertWeight >= WinterWeight
            ? ESVoxelBiome::Desert
            : ESVoxelBiome::Snow;
    }

    const double HalfWorldSize = FMath::Max(
        1000.0, std::abs(static_cast<double>(S.BiomeWorldSize)) * 0.5);
    const double NX = (X - static_cast<double>(S.BiomeWorldCenter.X)) / HalfWorldSize;
    const double NY = (Y - static_cast<double>(S.BiomeWorldCenter.Y)) / HalfWorldSize;

    // Plains and forest form a small set of large coherent patches in the centre.
    const float ForestSignal = FBM2D(
        NX * 3.0 + 7.1, NY * 3.0 - 9.3, S.Seed + 4849);
    return ForestSignal > 0.02f ? ESVoxelBiome::Forest : ESVoxelBiome::Plains;
}

float FSVoxelGenerator::SampleFlatlandInfluence(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    float Temperature = 0.0f;
    float Moisture = 0.0f;
    float MountainSignal = 0.0f;
    SampleBiomeClimate(X, Y, S, Temperature, Moisture, MountainSignal);
    (void)Temperature;
    (void)Moisture;

    const double Scale = FMath::Max(0.00000001, std::abs(static_cast<double>(S.FlatlandRegionScale)));
    const float Region01 = FMath::Clamp(
        HashNoise2D(X * Scale, Y * Scale, S.Seed + 5549) * 0.5f + 0.5f,
        0.0f, 1.0f);
    float DesertWeight = 0.0f;
    float WinterWeight = 0.0f;
    float TemperateWeight = 1.0f;
    SampleBiomeRegionWeights(
        X, Y, S, DesertWeight, WinterWeight, TemperateWeight);
    const float EffectiveFlatlandCoverage = FMath::Clamp(
        FMath::Clamp(S.FlatlandCoverage, 0.2f, 0.9f) *
            FMath::Clamp(S.FlatlandCoverageMultiplier, 0.1f, 1.0f),
        0.05f, 0.9f);
    const float NonDesertHillCoverage = FMath::Clamp(
        1.0f - EffectiveFlatlandCoverage, 0.0f, 0.95f);
    const float DesertHillCoverage = FMath::Clamp(S.DesertHillCoverage, 0.0f, 0.3f);
    const float HillCoverage = FMath::Lerp(
        NonDesertHillCoverage, DesertHillCoverage, DesertWeight);
    const float BorderWidth = FMath::Lerp(0.075f, 0.025f, DesertWeight);
    const float T = FMath::Clamp(
        (Region01 - (1.0f - HillCoverage - BorderWidth)) /
        FMath::Max(0.0001f, BorderWidth * 2.0f), 0.0f, 1.0f);
    const float SmoothHillBlend = T * T * (3.0f - 2.0f * T);

    // Use the same broad mountain mask as height generation, but only in the
    // snowy region. Mountain climate noise no longer punches random holes in
    // central plains or river surfaces.
    const float MountainThreshold = FMath::Clamp(S.MountainBiomeThreshold, 0.3f, 0.95f);
    const float MountainT = FMath::Clamp(
        (MountainSignal - MountainThreshold) / 0.20f, 0.0f, 1.0f);
    const float MountainSignalBlend = MountainT * MountainT * (3.0f - 2.0f * MountainT);
    const float MountainBlend = S.bEnableBiomes
        ? MountainSignalBlend * WinterWeight
        : 0.0f;
    return (1.0f - SmoothHillBlend) * (1.0f - MountainBlend);
}

double FSVoxelGenerator::SampleUnflattenedSurfaceHeight(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    const double RegionScale = FMath::Max(0.00000001, std::abs(static_cast<double>(S.FlatlandRegionScale)));
    const float Region01 = FMath::Clamp(
        HashNoise2D(X * RegionScale, Y * RegionScale, S.Seed + 5549) * 0.5f + 0.5f,
        0.0f, 1.0f);

    float Temperature = 0.0f;
    float Moisture = 0.0f;
    float MountainSignal = 0.0f;
    SampleBiomeClimate(X, Y, S, Temperature, Moisture, MountainSignal);
    (void)Temperature;
    (void)Moisture;

    const auto SmoothStep = [](float Edge0, float Edge1, float Value)
    {
        const float T = FMath::Clamp(
            (Value - Edge0) / FMath::Max(0.0001f, Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    };

    // Terrain relief uses continuous map-relative weights to remain smooth
    // at regional edges; material biome IDs never switch terrain height abruptly.
    float DesertBlend = 0.0f;
    float WinterBlend = 0.0f;
    float TemperateBlend = 1.0f;
    SampleBiomeRegionWeights(
        X, Y, S, DesertBlend, WinterBlend, TemperateBlend);
    (void)WinterBlend;
    (void)TemperateBlend;

    const float EffectiveFlatlandCoverage = FMath::Clamp(
        FMath::Clamp(S.FlatlandCoverage, 0.2f, 0.9f) *
            FMath::Clamp(S.FlatlandCoverageMultiplier, 0.1f, 1.0f),
        0.05f, 0.9f);
    const float NonDesertHillCoverage = FMath::Clamp(
        1.0f - EffectiveFlatlandCoverage, 0.0f, 0.95f);
    const float DesertHillCoverage = FMath::Clamp(S.DesertHillCoverage, 0.0f, 0.3f);
    const float HillCoverage = FMath::Lerp(
        NonDesertHillCoverage, DesertHillCoverage, DesertBlend);
    const float BorderWidth = FMath::Lerp(0.075f, 0.025f, DesertBlend);
    const float HillBlend = SmoothStep(
        1.0f - HillCoverage - BorderWidth,
        1.0f - HillCoverage + BorderWidth,
        Region01);

    const float MountainThreshold = FMath::Clamp(S.MountainBiomeThreshold, 0.3f, 0.95f);
    float MountainBlend = S.bEnableBiomes
        ? SmoothStep(MountainThreshold, MountainThreshold + 0.20f, MountainSignal)
        : 0.0f;
    // Keep high-relief mountain forms in the difficult snowy region only.
    // WinterBlend is continuous, so the mountain range fades into the centre
    // instead of forming a crater or abrupt wall at its boundary.
    MountainBlend *= WinterBlend;

    const double HillNoise = FBM2D(
        X * static_cast<double>(S.HillScale),
        Y * static_cast<double>(S.HillScale),
        S.Seed + 1001);
    const double HillShape = SmoothStep(-0.35f, 0.55f, static_cast<float>(HillNoise));
    const double FineNoise = FBM2D(
        X * static_cast<double>(S.DetailScale),
        Y * static_cast<double>(S.DetailScale),
        S.Seed + 777);
    const double FineAmplitude = FMath::Lerp(
        static_cast<double>(S.FlatlandNoiseAmplitude),
        static_cast<double>(S.DetailAmplitude),
        static_cast<double>(HillBlend));
    const double MicroRelief = FineNoise * FineAmplitude;
    const double HillRelief = HillShape * static_cast<double>(FMath::Abs(S.HillHeight)) *
        static_cast<double>(HillBlend);

    // Replace some perfect plains with broad, gentle rolls instead of turning
    // every former flatland into a full-sized hill. The long wavelength keeps
    // slopes mild enough for roads; the hill mask fades this relief smoothly
    // where the larger hills begin.
    const double GentleReliefNoise = FBM2D(
        X * RegionScale * 0.40,
        Y * RegionScale * 0.40,
        S.Seed + 6311);
    const double GentleRelief = GentleReliefNoise *
        static_cast<double>(FMath::Abs(S.GentleReliefHeight)) *
        (1.0 - static_cast<double>(HillBlend)) *
        (1.0 - static_cast<double>(MountainBlend));

    // Even where the configured desert hill mask is sparse, keep broad,
    // drivable rolling terrain rather than kilometre-wide perfectly flat plains.
    // The relief fades out where real hills begin and is applied before POI
    // plateaus, so future city clearings can remain flat.
    const double DesertRollingNoise = FBM2D(
        X * RegionScale * 1.15,
        Y * RegionScale * 1.15,
        S.Seed + 6449);
    const double DesertRollingRelief = DesertRollingNoise * 1200.0 *
        static_cast<double>(DesertBlend) *
        (1.0 - static_cast<double>(HillBlend)) *
        (1.0 - static_cast<double>(MountainBlend));

    // Mountain height is a separate, rare macro-form. Its broad noise is
    // continuous across chunks/LODs and cannot create narrow, jagged peaks.
    const double MountainRegionNoise = FBM2D(
        X * RegionScale * 0.35,
        Y * RegionScale * 0.35,
        S.Seed + 6007);
    const double MountainShape = FMath::Clamp((MountainRegionNoise + 1.0) * 0.5, 0.0, 1.0);
    const double MountainLift = MountainShape * static_cast<double>(FMath::Abs(S.MountainHeight)) *
        static_cast<double>(MountainBlend);

    const float Dune = FMath::Abs(HashNoise2D(
        X * RegionScale * 1.35,
        Y * RegionScale * 1.35,
        S.Seed + 4909));
    const double DuneLift = static_cast<double>(Dune) *
        static_cast<double>(FMath::Abs(S.FlatlandNoiseAmplitude)) *
        1.5 * static_cast<double>(DesertBlend);

    return static_cast<double>(S.BaseHeight) + MicroRelief + HillRelief +
        GentleRelief + DesertRollingRelief + MountainLift + DuneLift;
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

FVector2D FSVoxelGenerator::SamplePOISitePosition(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S)
{
    // Match GetPOIPlateauInfluence exactly so road endpoints land at the same
    // deterministic grid sites used by future POI placement.
    const double Spacing = FMath::Max(
        20000.0, std::abs(static_cast<double>(S.POISiteSpacing)));
    const int64 GridX = static_cast<int64>(std::round(X / Spacing));
    const int64 GridY = static_cast<int64>(std::round(Y / Spacing));
    const uint32 SiteHash = HashLattice2D(GridX, GridY, S.Seed + 5167);
    const double JitterX =
        (static_cast<double>(SiteHash & 0x0FFFu) / 4095.0 - 0.5) * 0.30;
    const double JitterY =
        (static_cast<double>((SiteHash >> 12) & 0x0FFFu) / 4095.0 - 0.5) * 0.30;
    return FVector2D(
        static_cast<float>(static_cast<double>(GridX) * Spacing + JitterX * Spacing),
        static_cast<float>(static_cast<double>(GridY) * Spacing + JitterY * Spacing));
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
    float& OutSignedField,
    float* OutBankInfluence)
{
    OutInfluence = 0.0f;
    OutSignedField = 0.0f;
    if (OutBankInfluence)
    {
        *OutBankInfluence = 0.0f;
    }
    if (!S.bEnableRivers)
    {
        return;
    }

    // Use a denser world-scale field so 2-4 km maps contain multiple long
    // channels, while keeping RiverScale as the user-facing tuning control.
    const double Scale = FMath::Max(
        0.00000001, std::abs(static_cast<double>(S.RiverScale)) * 2.5);
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

    // Convert channel and broad valley widths into dimensionless bands around
    // the same signed field. The inner mask stays narrow enough for a riverbed;
    // the outer bank mask spans tens of metres and is faded at its outer edge.
    const double Threshold = FMath::Clamp(HalfWidth * Scale * 2.0, 0.0001, 0.20);
    // Extend the valley shoulder beyond the narrow channel so the cut blends
    // through the final part of a gorge instead of stopping at an abrupt-looking lip.
    const double BankHalfWidth = FMath::Max(HalfWidth * 12.0, 6000.0);
    const double BankThreshold = FMath::Max(
        Threshold,
        FMath::Min(BankHalfWidth * Scale * 2.0, 0.24));
    const float T = FMath::Clamp(
        static_cast<float>(std::abs(Field) / Threshold),
        0.0f,
        1.0f);
    const float SmoothT = T * T * (3.0f - 2.0f * T);
    OutInfluence = 1.0f - SmoothT;
    OutSignedField = static_cast<float>(Field);

    if (OutBankInfluence)
    {
        const float BankDistance01 = FMath::Clamp(
            static_cast<float>(std::abs(Field) / BankThreshold), 0.0f, 1.0f);
        const float BankFadeT = FMath::Clamp((BankDistance01 - 0.78f) / 0.22f, 0.0f, 1.0f);
        const float BankFade = BankFadeT * BankFadeT * (3.0f - 2.0f * BankFadeT);
        *OutBankInfluence = 1.0f - BankFade;
    }

    float DesertWeight = 0.0f;
    float WinterWeight = 0.0f;
    float TemperateWeight = 1.0f;
    SampleBiomeRegionWeights(
        X, Y, S, DesertWeight, WinterWeight, TemperateWeight);
    // Fade river channels and valley shoulders through the same broad desert
    // transition instead of cutting either off at a categorical biome edge.
    const float NonDesertWeight = 1.0f - DesertWeight;
    OutInfluence *= NonDesertWeight;
    if (OutBankInfluence)
    {
        *OutBankInfluence *= NonDesertWeight;
    }

    // Do not gate the channel by flatland coverage: at high hill coverage that
    // gate reduced the channel mask, so terrain could be cut without reaching
    // RiverWaterLevel and the same river appeared as a dry trench. Full-strength
    // centre lines now carve to the bed level and are rendered with matching water.
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


void FSVoxelGenerator::SampleLakeMaskAndLevel(
    double X,
    double Y,
    const FSVoxelGenerationSettings& S,
    float& OutInfluence,
    float& OutWaterLevel)
{
    OutInfluence = 0.0f;
    OutWaterLevel = S.RiverWaterLevel;
    if (!S.bEnableLakes)
    {
        return;
    }

    const double Spacing = FMath::Max(20000.0, std::abs(static_cast<double>(S.LakeSiteSpacing)));
    const int64 GridX = static_cast<int64>(std::round(X / Spacing));
    const int64 GridY = static_cast<int64>(std::round(Y / Spacing));
    const uint32 SiteHash = HashLattice2D(GridX, GridY, S.Seed + 7331);
    const double SiteChance = static_cast<double>((SiteHash >> 16) & 0xFFu) / 255.0;
    const double JitterX = (static_cast<double>(SiteHash & 0x0FFFu) / 4095.0 - 0.5) * 0.30;
    const double JitterY = (static_cast<double>((SiteHash >> 12) & 0x0FFFu) / 4095.0 - 0.5) * 0.30;
    const double SiteX = static_cast<double>(GridX) * Spacing + JitterX * Spacing;
    const double SiteY = static_cast<double>(GridY) * Spacing + JitterY * Spacing;

    const ESVoxelBiome Biome = SampleBiome(SiteX, SiteY, S);
    if (Biome == ESVoxelBiome::Desert || Biome == ESVoxelBiome::Mountain)
    {
        return;
    }

    float Temperature = 0.0f;
    float Moisture = 0.0f;
    float MountainSignal = 0.0f;
    SampleBiomeClimate(SiteX, SiteY, S, Temperature, Moisture, MountainSignal);
    (void)Temperature;
    (void)Moisture;
    float DesertWeight = 0.0f;
    float WinterWeight = 0.0f;
    float TemperateWeight = 1.0f;
    SampleBiomeRegionWeights(
        SiteX, SiteY, S, DesertWeight, WinterWeight, TemperateWeight);
    if (MountainSignal >= FMath::Clamp(S.MountainBiomeThreshold, 0.3f, 0.95f) &&
        WinterWeight > 0.5f)
    {
        return;
    }

    const float Flatness = SampleFlatlandInfluence(SiteX, SiteY, S);
    // Blend lake frequency across gently rolling terrain instead of switching
    // abruptly to the very rare hill-lake chance at one threshold. This keeps
    // lakes present when the terrain settings increase hill coverage.
    const float LakeFlatnessBlendT = FMath::Clamp((Flatness - 0.30f) / 0.35f, 0.0f, 1.0f);
    const float LakeFlatnessBlend = LakeFlatnessBlendT * LakeFlatnessBlendT *
        (3.0f - 2.0f * LakeFlatnessBlendT);
    const float CandidateChance = FMath::Lerp(
        FMath::Clamp(S.HillLakeChance, 0.0f, 1.0f),
        FMath::Clamp(S.LakeChance, 0.0f, 1.0f),
        LakeFlatnessBlend);
    if (SiteChance > static_cast<double>(CandidateChance))
    {
        return;
    }

    const double Radius = FMath::Clamp(
        std::abs(static_cast<double>(S.LakeRadius)), 500.0, Spacing * 0.25);
    const double Blend = FMath::Clamp(
        std::abs(static_cast<double>(S.LakeShoreBlendDistance)), 0.0,
        Spacing * 0.25 - Radius);
    const double DX = X - SiteX;
    const double DY = Y - SiteY;
    const uint32 ShapeHash = HashLattice2D(GridX, GridY, S.Seed + 7477);
    const double Angle = static_cast<double>(ShapeHash & 0xFFFFu) /
        65535.0 * 6.283185307179586;
    const double AxisX = 0.72 + static_cast<double>((ShapeHash >> 16) & 0xFFu) / 255.0 * 0.53;
    const double AxisY = 1.0 / AxisX;
    const double CosAngle = std::cos(Angle);
    const double SinAngle = std::sin(Angle);
    const double RotatedX = DX * CosAngle + DY * SinAngle;
    const double RotatedY = -DX * SinAngle + DY * CosAngle;
    const double EllipseDistance = std::sqrt(
        (RotatedX / AxisX) * (RotatedX / AxisX) +
        (RotatedY / AxisY) * (RotatedY / AxisY));

    // Stable per-site ellipse parameters plus subtle radial warp add bays and
    // peninsulas, avoiding both perfect circles and identical lake outlines.
    const double ShoreNoise = FBM2D(
        DX / Radius * 1.8 + 17.2,
        DY / Radius * 1.8 - 6.8,
        S.Seed + static_cast<int32>(ShapeHash & 0x7FFFu) + 7481);
    const double Distance = EllipseDistance / (1.0 + ShoreNoise * 0.085);
    if (Distance >= Radius + Blend)
    {
        return;
    }

    // A lake is allowed only where the complete shore footprint is nearly level.
    // This rejects hilltops and side slopes instead of cutting a flat basin that
    // can leave water suspended above the downhill terrain.
    const double CenterTerrainHeight = SampleUnflattenedSurfaceHeight(SiteX, SiteY, S);
    double MinimumTerrainHeight = CenterTerrainHeight;
    double MaximumTerrainHeight = CenterTerrainHeight;
    double MinimumOuterRimHeight = TNumericLimits<double>::Max();
    const double ShoreRadius = (Radius + Blend) * 1.12;
    const double RingRadii[2] = { Radius * 0.65, ShoreRadius };
    const int32 RingSamples[2] = { 8, 16 };
    constexpr double MaxLakeTerrainRelief = 300.0; // 3 m across the whole lake footprint.

    for (int32 RingIndex = 0; RingIndex < 2; ++RingIndex)
    {
        const double RingRadius = RingRadii[RingIndex];
        for (int32 SampleIndex = 0; SampleIndex < RingSamples[RingIndex]; ++SampleIndex)
        {
            const double SampleAngle = 6.283185307179586 * static_cast<double>(SampleIndex) /
                static_cast<double>(RingSamples[RingIndex]);
            const double EllipseX = RingRadius * std::cos(SampleAngle) * AxisX;
            const double EllipseY = RingRadius * std::sin(SampleAngle) * AxisY;
            const double OffsetX = EllipseX * CosAngle - EllipseY * SinAngle;
            const double OffsetY = EllipseX * SinAngle + EllipseY * CosAngle;
            const double RimHeight = SampleUnflattenedSurfaceHeight(
                SiteX + OffsetX, SiteY + OffsetY, S);

            MinimumTerrainHeight = FMath::Min(MinimumTerrainHeight, RimHeight);
            MaximumTerrainHeight = FMath::Max(MaximumTerrainHeight, RimHeight);
            if (RingIndex == 1)
            {
                MinimumOuterRimHeight = FMath::Min(MinimumOuterRimHeight, RimHeight);
            }

            // Stop immediately when this site is clearly on uneven terrain.
            if (MaximumTerrainHeight - MinimumTerrainHeight > MaxLakeTerrainRelief)
            {
                return;
            }
        }
    }

    // Keep the water plane below every sampled shore point by a safety margin.
    // The rim is sampled beyond the mask's maximum radial warp, so the lake is
    // enclosed by terrain on all sides even when shore noise is at its extreme.
    const double DesiredLakeLevel = Flatness >= 0.5f
        ? static_cast<double>(S.RiverWaterLevel)
        : CenterTerrainHeight - 50.0;
    constexpr double LakeShoreSafetyMargin = 100.0;
    OutWaterLevel = static_cast<float>(FMath::Min(
        DesiredLakeLevel,
        MinimumOuterRimHeight - LakeShoreSafetyMargin));

    if (Distance <= Radius || Blend <= SMALL_NUMBER)
    {
        OutInfluence = 1.0f;
    }
    else
    {
        const float T = FMath::Clamp(
            static_cast<float>((Distance - Radius) / Blend), 0.0f, 1.0f);
        OutInfluence = 1.0f - T * T * (3.0f - 2.0f * T);
    }
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
        // falloff into surrounding ground.
        const double PlateauHeight = SampleUnflattenedSurfaceHeight(SiteX, SiteY, S);
        SurfaceHeight = FMath::Lerp(
            NaturalHeight,
            PlateauHeight,
            static_cast<double>(PlateauWeight));
    }

    // Preserve a little smooth height variation even inside POI clearings, where
    // the plateau blend would otherwise flatten all interior noise to one value.
    // This is at least half a metre from crest to trough, with broad enough scale
    // to stay smooth instead of looking like block-sized bumps.
    const double MicroScale = FMath::Max(
        0.00000001, std::abs(static_cast<double>(S.DetailScale)));
    const double MicroNoise = FBM2D(
        X * MicroScale, Y * MicroScale, S.Seed + 8009);
    const double MicroAmplitude = FMath::Clamp(
        static_cast<double>(FMath::Max(50.0f, S.FlatlandNoiseAmplitude)),
        50.0, 75.0);
    const float FlatnessForMicroRelief = SampleFlatlandInfluence(X, Y, S);
    const double MicroWeight = FMath::Lerp(
        0.35, 1.0, static_cast<double>(FlatnessForMicroRelief));
    SurfaceHeight += MicroNoise * MicroAmplitude * MicroWeight;

    if (S.bEnableRivers)
    {
        float RiverInfluence = 0.0f;
        float RiverSignedField = 0.0f;
        float RiverBankInfluence = 0.0f;
        SampleRiverMaskAndField(
            X, Y, S, RiverInfluence, RiverSignedField, &RiverBankInfluence);
        RiverInfluence = FMath::Clamp(RiverInfluence, 0.0f, 1.0f);
        RiverBankInfluence = FMath::Clamp(RiverBankInfluence, 0.0f, 1.0f);

        // Carve a broad, smooth river valley into high ground before cutting the
        // narrow water channel. The bank profile starts just above water level at
        // the channel edge and rises smoothly back toward the surrounding hills.
        // This turns a sharp canyon into sloping banks and lets hills split into
        // two rounded shoulders when a river crosses a hilltop.
        if (RiverBankInfluence > 0.0001f)
        {
            const double RiverScale = FMath::Max(
                0.00000001, std::abs(static_cast<double>(S.RiverScale)) * 2.5);
            const double RiverHalfWidth = FMath::Max(
                0.0, std::abs(static_cast<double>(S.RiverHalfWidth)));
            const double ChannelThreshold = FMath::Clamp(
                RiverHalfWidth * RiverScale * 2.0, 0.0001, 0.20);
            const double BankHalfWidth = FMath::Max(RiverHalfWidth * 12.0, 6000.0);
            const double BankThreshold = FMath::Max(
                ChannelThreshold,
                FMath::Min(BankHalfWidth * RiverScale * 2.0, 0.24));
            const double BankProgress = FMath::Clamp(
                (std::abs(static_cast<double>(RiverSignedField)) - ChannelThreshold) /
                    FMath::Max(0.0001, BankThreshold - ChannelThreshold),
                0.0,
                1.0);
            const double SmoothBankProgress = BankProgress * BankProgress *
                (3.0 - 2.0 * BankProgress);
            const double BankRise = FMath::Clamp(
                std::abs(static_cast<double>(S.HillHeight)) * 1.15, 1500.0, 7000.0);
            const double BankProfileHeight = static_cast<double>(S.RiverWaterLevel) +
                150.0 + BankRise * SmoothBankProgress;
            const double CarvedBankHeight = FMath::Min(SurfaceHeight, BankProfileHeight);
            SurfaceHeight = FMath::Lerp(
                SurfaceHeight, CarvedBankHeight, static_cast<double>(RiverBankInfluence));
        }

        // Keep the actual narrow channel at its configured bed level. The broad
        // bank profile affects adjacent terrain only, not the water-channel depth.
        const double RiverBedLevel = static_cast<double>(S.RiverWaterLevel) -
            std::abs(static_cast<double>(S.RiverDepth));
        SurfaceHeight = FMath::Lerp(SurfaceHeight, RiverBedLevel, RiverInfluence);
    }

    float LakeInfluence = 0.0f;
    float LakeWaterLevel = S.RiverWaterLevel;
    SampleLakeMaskAndLevel(X, Y, S, LakeInfluence, LakeWaterLevel);
    if (LakeInfluence > 0.0f)
    {
        const double LakeBedLevel = static_cast<double>(LakeWaterLevel) -
            FMath::Min(500.0, std::abs(static_cast<double>(S.LakeDepth)));
        SurfaceHeight = FMath::Lerp(
            SurfaceHeight, LakeBedLevel, static_cast<double>(LakeInfluence));
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
    Settings.FlatlandRegionScale = FMath::Min(
        FMath::Max(0.0f, Settings.FlatlandRegionScale), MaxNoiseScale * 0.9f);
    Settings.HillScale = FMath::Min(
        FMath::Max(0.0f, Settings.HillScale), MaxNoiseScale);
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

