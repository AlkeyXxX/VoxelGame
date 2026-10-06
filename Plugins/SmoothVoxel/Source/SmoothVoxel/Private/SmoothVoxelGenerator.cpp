
#include "SmoothVoxelGenerator.h"
#include "SmoothVoxelMCTables.h"
#include "Math/UnrealMathUtility.h"

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

    FORCEINLINE float Fade(float T)
    {
        // Smooth C1 interpolation.
        return T * T * (3.0f - 2.0f * T);
    }

    FORCEINLINE float Lerp3D(
        float C000,
        float C100,
        float C010,
        float C110,
        float C001,
        float C101,
        float C011,
        float C111,
        float TX,
        float TY,
        float TZ)
    {
        const float X00 = FMath::Lerp(C000, C100, TX);
        const float X10 = FMath::Lerp(C010, C110, TX);
        const float X01 = FMath::Lerp(C001, C101, TX);
        const float X11 = FMath::Lerp(C011, C111, TX);

        const float Y0 = FMath::Lerp(X00, X10, TY);
        const float Y1 = FMath::Lerp(X01, X11, TY);

        return FMath::Lerp(Y0, Y1, TZ);
    }
}


// ============================================================
// 2D HASH NOISE
// ============================================================

float FSVoxelGenerator::HashNoise2D(
    float X,
    float Y,
    int32 Seed)
{
    const float IX = FMath::FloorToFloat(X);
    const float IY = FMath::FloorToFloat(Y);

    const float FX = X - IX;
    const float FY = Y - IY;

    auto Hash = [Seed](float A, float B)
    {
        const float V =
            FMath::Sin(
                A * 127.1f +
                B * 311.7f +
                float(Seed) * 74.7f)
            * 43758.5453f;

        return V - FMath::FloorToFloat(V);
    };

    const float A = Hash(IX,     IY);
    const float B = Hash(IX + 1, IY);
    const float C = Hash(IX,     IY + 1);
    const float D = Hash(IX + 1, IY + 1);

    const float SX = Fade(FX);
    const float SY = Fade(FY);

    const float AB = FMath::Lerp(A, B, SX);
    const float CD = FMath::Lerp(C, D, SX);

    return FMath::Lerp(AB, CD, SY) * 2.0f - 1.0f;
}


// ============================================================
// 2D FBM
// ============================================================

float FSVoxelGenerator::FBM2D(
    float X,
    float Y,
    int32 Seed)
{
    float Sum = 0.0f;
    float Amplitude = 1.0f;
    float Frequency = 1.0f;
    float Normalization = 0.0f;

    // 3 octaves instead of 4.
    //
    // This is intentional.
    // With 100-unit voxels, too many high-frequency octaves
    // create ugly spikes and aliasing.

    for (int32 Octave = 0; Octave < 3; ++Octave)
    {
        Sum += HashNoise2D(
            X * Frequency,
            Y * Frequency,
            Seed + Octave * 101)
            * Amplitude;

        Normalization += Amplitude;

        Amplitude *= 0.5f;
        Frequency *= 2.0f;
    }

    return Sum / Normalization;
}


// ============================================================
// 3D HASH
// ============================================================

float FSVoxelGenerator::HashNoise3D(
    int32 X,
    int32 Y,
    int32 Z,
    int32 Seed)
{
    // Integer hash.
    //
    // Unlike the old HashNoise3D(), this function is only used
    // at lattice points. SmoothNoise3D() interpolates between
    // them, producing continuous noise.

    uint32 H =
        uint32(X) * 374761393u +
        uint32(Y) * 668265263u +
        uint32(Z) * 2147483647u +
        uint32(Seed) * 1274126177u;

    H ^= H >> 13;
    H *= 1274126177u;
    H ^= H >> 16;

    const uint32 Masked = H & 0x00FFFFFFu;

    return float(Masked) / float(0x00FFFFFFu);
}


// ============================================================
// SMOOTH 3D VALUE NOISE
// ============================================================

float FSVoxelGenerator::SmoothNoise3D(
    float X,
    float Y,
    float Z,
    int32 Seed)
{
    const int32 IX = FMath::FloorToInt(X);
    const int32 IY = FMath::FloorToInt(Y);
    const int32 IZ = FMath::FloorToInt(Z);

    const float FX = X - float(IX);
    const float FY = Y - float(IY);
    const float FZ = Z - float(IZ);

    const float TX = Fade(FX);
    const float TY = Fade(FY);
    const float TZ = Fade(FZ);

    const float C000 = HashNoise3D(
        IX,
        IY,
        IZ,
        Seed);

    const float C100 = HashNoise3D(
        IX + 1,
        IY,
        IZ,
        Seed);

    const float C010 = HashNoise3D(
        IX,
        IY + 1,
        IZ,
        Seed);

    const float C110 = HashNoise3D(
        IX + 1,
        IY + 1,
        IZ,
        Seed);

    const float C001 = HashNoise3D(
        IX,
        IY,
        IZ + 1,
        Seed);

    const float C101 = HashNoise3D(
        IX + 1,
        IY,
        IZ + 1,
        Seed);

    const float C011 = HashNoise3D(
        IX,
        IY + 1,
        IZ + 1,
        Seed);

    const float C111 = HashNoise3D(
        IX + 1,
        IY + 1,
        IZ + 1,
        Seed);

    return Lerp3D(
        C000,
        C100,
        C010,
        C110,
        C001,
        C101,
        C011,
        C111,
        TX,
        TY,
        TZ)
        * 2.0f - 1.0f;
}


// ============================================================
// 3D FBM
// ============================================================

float FSVoxelGenerator::FBM3D(
    float X,
    float Y,
    float Z,
    int32 Seed)
{
    float Sum = 0.0f;
    float Amplitude = 1.0f;
    float Frequency = 1.0f;
    float Normalization = 0.0f;

    // 3 octaves is enough for caves.
    for (int32 Octave = 0; Octave < 3; ++Octave)
    {
        Sum += SmoothNoise3D(
            X * Frequency,
            Y * Frequency,
            Z * Frequency,
            Seed + Octave * 137)
            * Amplitude;

        Normalization += Amplitude;

        Amplitude *= 0.5f;
        Frequency *= 2.0f;
    }

    return Sum / Normalization;
}


// ============================================================
// DENSITY
// ============================================================

float FSVoxelGenerator::Density(
    float X,
    float Y,
    float Z,
    const FSVoxelGenerationSettings& S)
{
    // Positive = solid.
    // Negative = air.

    // --------------------------------------------------------
    // TERRAIN HEIGHT
    // --------------------------------------------------------

    const float Macro =
        FBM2D(
            X * S.NoiseScale,
            Y * S.NoiseScale,
            S.Seed);

    const float Detail =
        FBM2D(
            X * S.DetailScale,
            Y * S.DetailScale,
            S.Seed + 777);

    float SurfaceHeight =
        S.BaseHeight +
        Macro * S.HeightAmplitude +
        Detail * S.DetailAmplitude;

    // --------------------------------------------------------
    // BASE DENSITY
    // --------------------------------------------------------

    float D = SurfaceHeight - Z;

    // --------------------------------------------------------
    // BOTTOM
    // --------------------------------------------------------

    if (Z < SurfaceHeight - S.BottomDepth)
    {
        D = 1.0f;
    }

    // --------------------------------------------------------
    // CAVES
    // --------------------------------------------------------

    if (S.bEnableCaves && S.CaveStrength > 0.0f)
    {
        const float Cave =
            FBM3D(
                X * S.CaveScale,
                Y * S.CaveScale,
                Z * S.CaveScale,
                S.Seed + 4242);

        // Convert cave noise into a smooth 0..1 mask.
        //
        // Below threshold = no cave.
        // Above threshold = gradually stronger carving.

        const float Range =
            FMath::Max(
                0.001f,
                1.0f - S.CaveThreshold);

        float T =
            (Cave - S.CaveThreshold) / Range;

        T = FMath::Clamp(T, 0.0f, 1.0f);

        // Smoothstep.
        const float CaveMask =
            T * T * (3.0f - 2.0f * T);

        // Only carve existing solid.
        if (D > 0.0f)
        {
            // Much weaker than the old 2500 value.
            //
            // The old value was large enough to create huge,
            // aggressive density changes.

            const float CaveCarve =
                CaveMask *
                600.0f *
                S.CaveStrength;

            D -= CaveCarve;
        }
    }

    return D;
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
    float X,
    float Y,
    float Z,
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

