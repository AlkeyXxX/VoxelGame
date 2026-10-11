
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

    FORCEINLINE float ApplyBlockEdits(
        double X,
        double Y,
        double Z,
        float Density,
        const FSVoxelGenerationSettings& Settings)
    {
        if (!Settings.BlockEdits.IsValid() || Settings.BlockEdits->Num() == 0)
        {
            return Density;
        }

        const double Step = FMath::Max(1.0, static_cast<double>(Settings.BaseVoxelSize));
        const int32 BaseX = static_cast<int32>(std::floor(X / Step));
        const int32 BaseY = static_cast<int32>(std::floor(Y / Step));
        const int32 BaseZ = static_cast<int32>(std::floor(Z / Step));
        const double HalfExtent = Step * 0.55;

        // The 0.55-cell half extent slightly overlaps neighbouring cell volumes,
        // so check the 3x3x3 neighbourhood at boundaries. Never scan the full map.
        int8 CandidateStates[27];
        double CandidateBoxSdfs[27];
        int32 CandidateCount = 0;

        for (int32 DZ = -1; DZ <= 1; ++DZ)
        {
            for (int32 DY = -1; DY <= 1; ++DY)
            {
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    const FIntVector Cell(BaseX + DX, BaseY + DY, BaseZ + DZ);
                    const int8* EditState = Settings.BlockEdits->Find(Cell);
                    if (!EditState || *EditState == 0)
                    {
                        continue;
                    }

                    const double CenterX = (static_cast<double>(Cell.X) + 0.5) * Step;
                    const double CenterY = (static_cast<double>(Cell.Y) + 0.5) * Step;
                    const double CenterZ = (static_cast<double>(Cell.Z) + 0.5) * Step;
                    const double QX = std::abs(X - CenterX) - HalfExtent;
                    const double QY = std::abs(Y - CenterY) - HalfExtent;
                    const double QZ = std::abs(Z - CenterZ) - HalfExtent;
                    const double OutsideX = FMath::Max(QX, 0.0);
                    const double OutsideY = FMath::Max(QY, 0.0);
                    const double OutsideZ = FMath::Max(QZ, 0.0);
                    const double OutsideDistance = std::sqrt(
                        OutsideX * OutsideX + OutsideY * OutsideY + OutsideZ * OutsideZ);
                    const double InsideDistance = FMath::Min(
                        FMath::Max(QX, FMath::Max(QY, QZ)),
                        0.0);

                    CandidateStates[CandidateCount] = *EditState;
                    CandidateBoxSdfs[CandidateCount] = OutsideDistance + InsideDistance;
                    ++CandidateCount;
                }
            }
        }

        // Always union placed solids first and subtract carved cells second.
        // This makes breaking win at the tiny overlaps between adjacent cells.
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            for (int32 Index = 0; Index < CandidateCount; ++Index)
            {
                const double BoxSdf = CandidateBoxSdfs[Index];
                if (Pass == 0 && CandidateStates[Index] > 0)
                {
                    Density = FMath::Max(Density, static_cast<float>(-BoxSdf));
                }
                else if (Pass == 1 && CandidateStates[Index] < 0)
                {
                    Density = FMath::Min(Density, static_cast<float>(BoxSdf));
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

double FSVoxelGenerator::SampleSurfaceHeight(double X, double Y, const FSVoxelGenerationSettings& S)
{
    const double Macro = FBM2D(
        X * static_cast<double>(S.NoiseScale),
        Y * static_cast<double>(S.NoiseScale),
        S.Seed);

    const double Detail = FBM2D(
        X * static_cast<double>(S.DetailScale),
        Y * static_cast<double>(S.DetailScale),
        S.Seed + 777);

    return static_cast<double>(S.BaseHeight) +
        Macro * static_cast<double>(S.HeightAmplitude) +
        Detail * static_cast<double>(S.DetailAmplitude);
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

