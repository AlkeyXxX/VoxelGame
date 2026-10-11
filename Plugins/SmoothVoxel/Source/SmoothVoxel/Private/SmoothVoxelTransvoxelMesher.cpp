#include "SmoothVoxelTransvoxelMesher.h"

#include "SmoothVoxelOctree.h"
#include "Math/UnrealMathUtility.h"

// The upstream file is table data with its original copyright notice intact.
// Keep it isolated in this translation unit to avoid duplicate external symbols.
namespace SVoxelOfficialTransvoxel
{
#include "TransvoxelMIT/TransvoxelTables.inl"
}

namespace
{
	using namespace SVoxelOfficialTransvoxel;

	const int32 RegularCornerOffsets[8][3] =
	{
		{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
		{0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}
	};

	const int32 TransitionFaceSampleOffsets[13][3] =
	{
		{0, 0, 0}, {1, 0, 0}, {2, 0, 0},
		{0, 1, 0}, {1, 1, 0}, {2, 1, 0},
		{0, 2, 0}, {1, 2, 0}, {2, 2, 0},
		{0, 0, 1}, {2, 0, 1}, {0, 2, 1}, {2, 2, 1}
	};

	// The published transition table's case bit order differs from spatial
	// row-major ordering of the 3x3 grid.
	const int32 TransitionCaseBitToSample[9] = { 0, 1, 2, 5, 8, 7, 6, 3, 4 };

	FORCEINLINE int32 SampleIndex(int32 X, int32 Y, int32 Z, int32 N)
	{
		return X + Y * N + Z * N * N;
	}

	FVector SampleOutwardNormal(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings,
		float SampleStep)
	{
		const float H = FMath::Max(5.0f, SampleStep);
		const float Dx =
			FSVoxelGenerator::SampleDensityAtChunkLocal(
				LocalPosition + FVector(H, 0.0f, 0.0f), Settings) -
			FSVoxelGenerator::SampleDensityAtChunkLocal(
				LocalPosition - FVector(H, 0.0f, 0.0f), Settings);
		const float Dy =
			FSVoxelGenerator::SampleDensityAtChunkLocal(
				LocalPosition + FVector(0.0f, H, 0.0f), Settings) -
			FSVoxelGenerator::SampleDensityAtChunkLocal(
				LocalPosition - FVector(0.0f, H, 0.0f), Settings);
		const float Dz =
			FSVoxelGenerator::SampleDensityAtChunkLocal(
				LocalPosition + FVector(0.0f, 0.0f, H), Settings) -
			FSVoxelGenerator::SampleDensityAtChunkLocal(
				LocalPosition - FVector(0.0f, 0.0f, H), Settings);

		// Density is positive inside solid terrain, so its negative gradient
		// points out of the solid. Sampling relative to the integer chunk lattice
		// prevents large absolute float coordinates from contaminating normals.
		return FVector(-Dx, -Dy, -Dz).GetSafeNormal();
	}

	FVector TransitionSampleToChunkLocal(
		uint8 FaceBit,
		int32 CellU,
		int32 CellV,
		int32 CellsPerAxis,
		float CoarseStep,
		int32 SampleIndexInCell)
	{
		FVector FaceOrigin = FVector::ZeroVector;
		FVector AxisU(1.0f, 0.0f, 0.0f);
		FVector AxisV(0.0f, 1.0f, 0.0f);
		FVector Inward(0.0f, 0.0f, 1.0f);
		const float ChunkSize = static_cast<float>(CellsPerAxis) * CoarseStep;

		switch (FaceBit)
		{
		case SVoxelTransitionFace_NegativeX:
			AxisU = FVector(0.0f, 1.0f, 0.0f);
			AxisV = FVector(0.0f, 0.0f, 1.0f);
			Inward = FVector(1.0f, 0.0f, 0.0f);
			break;
		case SVoxelTransitionFace_PositiveX:
			FaceOrigin = FVector(ChunkSize, 0.0f, 0.0f);
			AxisU = FVector(0.0f, 1.0f, 0.0f);
			AxisV = FVector(0.0f, 0.0f, 1.0f);
			Inward = FVector(-1.0f, 0.0f, 0.0f);
			break;
		case SVoxelTransitionFace_NegativeY:
			AxisU = FVector(1.0f, 0.0f, 0.0f);
			AxisV = FVector(0.0f, 0.0f, 1.0f);
			Inward = FVector(0.0f, 1.0f, 0.0f);
			break;
		case SVoxelTransitionFace_PositiveY:
			FaceOrigin = FVector(0.0f, ChunkSize, 0.0f);
			AxisU = FVector(1.0f, 0.0f, 0.0f);
			AxisV = FVector(0.0f, 0.0f, 1.0f);
			Inward = FVector(0.0f, -1.0f, 0.0f);
			break;
		case SVoxelTransitionFace_NegativeZ:
			AxisU = FVector(1.0f, 0.0f, 0.0f);
			AxisV = FVector(0.0f, 1.0f, 0.0f);
			Inward = FVector(0.0f, 0.0f, 1.0f);
			break;
		case SVoxelTransitionFace_PositiveZ:
			FaceOrigin = FVector(0.0f, 0.0f, ChunkSize);
			AxisU = FVector(1.0f, 0.0f, 0.0f);
			AxisV = FVector(0.0f, 1.0f, 0.0f);
			Inward = FVector(0.0f, 0.0f, -1.0f);
			break;
		default:
			return FVector::ZeroVector;
		}

		const int32* Offset = TransitionFaceSampleOffsets[SampleIndexInCell];
		const float FineStep = CoarseStep * 0.5f;
		const float U = (static_cast<float>(CellU) * 2.0f + Offset[0]) * FineStep;
		const float V = (static_cast<float>(CellV) * 2.0f + Offset[1]) * FineStep;
		const float Depth = static_cast<float>(Offset[2]) * CoarseStep * 0.5f;

		// Keep the transition-cell lattice on the exact chunk face. Adjacent
		// transition faces must share the same edge coordinates; insetting each
		// face independently opens square holes at multi-face junctions.
		return FaceOrigin + AxisU * U + AxisV * V + Inward * Depth;
	}

	float ComputeNormalSampleStep(const FSVoxelGenerationSettings& Settings)
	{
		// Use one world-space gradient radius for every LOD and transition face.
		// LOD-dependent radii produce lighting bands aligned to chunk boundaries.
		return FMath::Max(5.0f, Settings.NormalSampleStep);
	}

	FVector ComputeTransitionSecondaryPosition(
		const FVector& PrimaryPosition,
		const FSVoxelGenerationSettings& Settings,
		uint8 TransitionFaceMask,
		float ChunkSize,
		float VoxelSize)
	{
		// The transition mesh occupies a strip inside the coarse chunk. The
		// regular surface in the same boundary cells must be moved to a secondary
		// position; simply moving the sampled cube corners before interpolation
		// deforms the isosurface and creates long, overlapping diagonal triangles.
		// This mirrors the secondary-position idea used by established
		// Transvoxel implementations, with a quarter-cell border offset.
		const float Step = FMath::Max(1.0f, VoxelSize);
		const float BorderWidth = Step;
		const float BorderOffset = Step * 0.25f;

		// A regular vertex that also lies on a chunk face without a transition
		// must not move; otherwise it would open a crack against a same-LOD
		// neighbour. This matches the border-mask guard in reference
		// Transvoxel implementations.
		uint8 VertexBorderMask = 0;
		if (FMath::IsNearlyZero(PrimaryPosition.X, 0.01f))
		{
			VertexBorderMask |= SVoxelTransitionFace_NegativeX;
		}
		if (FMath::IsNearlyEqual(PrimaryPosition.X, ChunkSize, 0.01f))
		{
			VertexBorderMask |= SVoxelTransitionFace_PositiveX;
		}
		if (FMath::IsNearlyZero(PrimaryPosition.Y, 0.01f))
		{
			VertexBorderMask |= SVoxelTransitionFace_NegativeY;
		}
		if (FMath::IsNearlyEqual(PrimaryPosition.Y, ChunkSize, 0.01f))
		{
			VertexBorderMask |= SVoxelTransitionFace_PositiveY;
		}
		if (FMath::IsNearlyZero(PrimaryPosition.Z, 0.01f))
		{
			VertexBorderMask |= SVoxelTransitionFace_NegativeZ;
		}
		if (FMath::IsNearlyEqual(PrimaryPosition.Z, ChunkSize, 0.01f))
		{
			VertexBorderMask |= SVoxelTransitionFace_PositiveZ;
		}
		if ((VertexBorderMask & static_cast<uint8>(~TransitionFaceMask)) != 0)
		{
			return PrimaryPosition;
		}

		FVector Delta = FVector::ZeroVector;

		if ((TransitionFaceMask & SVoxelTransitionFace_NegativeX) &&
			PrimaryPosition.X < BorderWidth)
		{
			const float Alpha = FMath::Clamp(1.0f - PrimaryPosition.X / BorderWidth, 0.0f, 1.0f);
			Delta.X += Alpha * BorderOffset;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_PositiveX) &&
			PrimaryPosition.X > ChunkSize - BorderWidth)
		{
			const float Alpha = FMath::Clamp(
				(PrimaryPosition.X - (ChunkSize - BorderWidth)) / BorderWidth, 0.0f, 1.0f);
			Delta.X -= Alpha * BorderOffset;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_NegativeY) &&
			PrimaryPosition.Y < BorderWidth)
		{
			const float Alpha = FMath::Clamp(1.0f - PrimaryPosition.Y / BorderWidth, 0.0f, 1.0f);
			Delta.Y += Alpha * BorderOffset;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_PositiveY) &&
			PrimaryPosition.Y > ChunkSize - BorderWidth)
		{
			const float Alpha = FMath::Clamp(
				(PrimaryPosition.Y - (ChunkSize - BorderWidth)) / BorderWidth, 0.0f, 1.0f);
			Delta.Y -= Alpha * BorderOffset;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_NegativeZ) &&
			PrimaryPosition.Z < BorderWidth)
		{
			const float Alpha = FMath::Clamp(1.0f - PrimaryPosition.Z / BorderWidth, 0.0f, 1.0f);
			Delta.Z += Alpha * BorderOffset;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_PositiveZ) &&
			PrimaryPosition.Z > ChunkSize - BorderWidth)
		{
			const float Alpha = FMath::Clamp(
				(PrimaryPosition.Z - (ChunkSize - BorderWidth)) / BorderWidth, 0.0f, 1.0f);
			Delta.Z -= Alpha * BorderOffset;
		}

		// Project the displacement into the local tangent plane, following the
		// standard secondary-position approach so movement is along the surface
		// rather than simply pushing vertices off the isosurface normal.
		// Most surface vertices are not moved by a transition. Avoid six
		// extra density samples for those vertices; they are expensive because
		// each sample evaluates the multi-octave terrain noise.
		if (Delta.SizeSquared() <= SMALL_NUMBER)
		{
			return PrimaryPosition;
		}

		const FVector SurfaceNormal = SampleOutwardNormal(
			PrimaryPosition, Settings, ComputeNormalSampleStep(Settings));
		const FVector Normal = SurfaceNormal.GetSafeNormal();
		const FVector TangentialDelta = Delta - Normal * FVector::DotProduct(Normal, Delta);
		return PrimaryPosition + TangentialDelta;
	}

	void DecodeEdge(uint16 EdgeCode, int32& OutSampleA, int32& OutSampleB)
	{
		// The low byte stores the endpoint indices; the high byte is the
		// upstream vertex-reuse hint and is not required by this non-indexed mesh.
		OutSampleB = EdgeCode & 0x0F;
		OutSampleA = (EdgeCode >> 4) & 0x0F;
	}

	// The ProceduralMeshComponent stores UVs as floats on the CPU but its
	// render vertex buffer uses half precision on the UE4.27 path. Keep UVs
	// bounded per emitted cell: subtracting an integer tile offset preserves
	// the repeating world-aligned texture while avoiding low precision at large
	// absolute UV values. Vertices are unique to each emitted cell, so every
	// triangle in that cell receives the same integer offset.
	void GetWorldXYForUV(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings,
		double& OutWorldX,
		double& OutWorldY)
	{
		OutWorldX = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.X) * static_cast<double>(Settings.BaseVoxelSize) + static_cast<double>(LocalPosition.X)
			: static_cast<double>(Settings.ChunkWorldOrigin.X) + static_cast<double>(LocalPosition.X);
		OutWorldY = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Y) * static_cast<double>(Settings.BaseVoxelSize) + static_cast<double>(LocalPosition.Y)
			: static_cast<double>(Settings.ChunkWorldOrigin.Y) + static_cast<double>(LocalPosition.Y);
	}

	void AppendCellToMesh(
		const FVector* CellLocalVertices,
		int32 VertexCount,
		const uint8* TriangleIndices,
		int32 TriangleCount,
		bool bFlipWinding,
		const FSVoxelGenerationSettings& Settings,
		uint8 TransitionFaceMask,
		float ChunkSize,
		FSVoxelMeshData& OutMesh,
		TMap<FIntVector, uint32>& VertexCache)
	{
		if (VertexCount <= 0 || TriangleCount <= 0)
		{
			return;
		}

		constexpr double UVWorldScale = 0.0005; // One texture repeat per 2000 cm.

		// Rebase UVs once per chunk, not once per cell. Cell-local rebasing kept
		// values small but introduced UV discontinuities on every marching-cubes
		// cell boundary for materials that scale or otherwise process UV0.
		// A single chunk-wide integer offset keeps UVs continuous within each
		// mesh. Neighbouring chunks differ only by whole texture repeats.
		const double ChunkOriginWorldX = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.X) * static_cast<double>(Settings.BaseVoxelSize)
			: static_cast<double>(Settings.ChunkWorldOrigin.X);
		const double ChunkOriginWorldY = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Y) * static_cast<double>(Settings.BaseVoxelSize)
			: static_cast<double>(Settings.ChunkWorldOrigin.Y);
		const double UvOffsetU = static_cast<double>(FMath::FloorToInt(
			static_cast<float>(ChunkOriginWorldX * UVWorldScale)));
		const double UvOffsetV = static_cast<double>(FMath::FloorToInt(
			static_cast<float>(ChunkOriginWorldY * UVWorldScale)));

		TArray<FVector> CellNormals;
		CellNormals.SetNumUninitialized(VertexCount);
		TArray<uint32> CellGlobalIndices;
		CellGlobalIndices.SetNumUninitialized(VertexCount);

		for (int32 I = 0; I < VertexCount; ++I)
		{
			const FVector LocalPosition = CellLocalVertices[I];
			// Quantize local positions to 0.1 cm. Adjacent marching-cubes cells
			// then share one render vertex/index instead of uploading duplicate
			// positions, normals, UVs and tangents. The same key also guarantees
			// matching cached normals at cell boundaries.
			const FIntVector VertexKey(
				FMath::RoundToInt(LocalPosition.X * 10.0f),
				FMath::RoundToInt(LocalPosition.Y * 10.0f),
				FMath::RoundToInt(LocalPosition.Z * 10.0f));

			if (const uint32* ExistingIndex = VertexCache.Find(VertexKey))
			{
				CellGlobalIndices[I] = *ExistingIndex;
				CellNormals[I] = OutMesh.Normals[static_cast<int32>(*ExistingIndex)];
				continue;
			}

			CellNormals[I] = SampleOutwardNormal(
				LocalPosition, Settings, ComputeNormalSampleStep(Settings));
			CellGlobalIndices[I] = static_cast<uint32>(OutMesh.Vertices.Num());
			VertexCache.Add(VertexKey, CellGlobalIndices[I]);
			OutMesh.Vertices.Add(LocalPosition);
			OutMesh.Normals.Add(CellNormals[I]);

			// Reconstruct world coordinates in double precision, then subtract
			// the shared chunk tile offset before conversion to the GPU UV format.
			double WorldX = 0.0;
			double WorldY = 0.0;
			GetWorldXYForUV(LocalPosition, Settings, WorldX, WorldY);
			OutMesh.UV0.Add(FVector2D(
				static_cast<float>(WorldX * UVWorldScale - UvOffsetU),
				static_cast<float>(WorldY * UVWorldScale - UvOffsetV)));

			// UV0 follows a fixed-scale world-XY projection within this chunk.
			// Subtracting a constant integer tile offset preserves its derivatives.
			const FVector Normal = CellNormals[I].GetSafeNormal();
			FVector Tangent;
			FVector Bitangent;
			if (FMath::Abs(Normal.Z) > 0.1f)
			{
				Tangent = FVector(1.0f, 0.0f, -Normal.X / Normal.Z).GetSafeNormal();
				Bitangent = FVector(0.0f, 1.0f, -Normal.Y / Normal.Z).GetSafeNormal();
			}
			else
			{
				// A height-field projection is singular on near-vertical walls;
				// use a stable fallback there and let cliffs/caves use triplanar UVs.
				const FVector WorldU(1.0f, 0.0f, 0.0f);
				const FVector WorldV(0.0f, 1.0f, 0.0f);
				Tangent = WorldU - Normal * FVector::DotProduct(Normal, WorldU);
				if (!Tangent.Normalize())
				{
					Tangent = (WorldV - Normal * FVector::DotProduct(Normal, WorldV)).GetSafeNormal();
				}
				Bitangent = WorldV - Normal * FVector::DotProduct(Normal, WorldV);
			}
			const bool bFlipTangentY =
				FVector::DotProduct(FVector::CrossProduct(Normal, Tangent), Bitangent) < 0.0f;
			OutMesh.Tangents.Add(FProcMeshTangent(Tangent, bFlipTangentY));
		}

		for (int32 I = 0; I < TriangleCount; ++I)
		{
			const int32 Offset = I * 3;
			const uint32 A = TriangleIndices[Offset];
			uint32 B = TriangleIndices[Offset + 1];
			uint32 C = TriangleIndices[Offset + 2];

			if (A >= static_cast<uint32>(VertexCount) ||
				B >= static_cast<uint32>(VertexCount) ||
				C >= static_cast<uint32>(VertexCount))
			{
				continue;
			}

			if (bFlipWinding)
			{
				Swap(B, C);
			}

			const FVector EdgeA = CellLocalVertices[B] - CellLocalVertices[A];
			const FVector EdgeB = CellLocalVertices[C] - CellLocalVertices[A];
			const FVector FaceNormal = FVector::CrossProduct(EdgeA, EdgeB);
			if (FaceNormal.SizeSquared() <= SMALL_NUMBER)
			{
				continue;
			}

			const FVector ExpectedNormal = (CellNormals[A] + CellNormals[B] + CellNormals[C]).GetSafeNormal();

			// Unreal's front-face convention for procedural triangles is opposite
			// to the conventional cross-product direction. Keep the supplied
			// shading normals pointing outward, but wind the indices for UE's
			// front-face culling and one-sided triangle-mesh collision.
			if (FVector::DotProduct(FaceNormal, ExpectedNormal) > 0.0f)
			{
				Swap(B, C);
			}

			OutMesh.Triangles.Add(static_cast<int32>(CellGlobalIndices[A]));
			OutMesh.Triangles.Add(static_cast<int32>(CellGlobalIndices[B]));
			OutMesh.Triangles.Add(static_cast<int32>(CellGlobalIndices[C]));
		}

	}
}

bool FSVoxelTransvoxelMesher::ValidateLookupTables()
{
	using namespace SVoxelOfficialTransvoxel;

	for (int32 CaseCode = 0; CaseCode < 256; ++CaseCode)
	{
		const uint8 ClassIndex = regularCellClass[CaseCode];
		if (ClassIndex >= 16)
		{
			return false;
		}

		const RegularCellData& Cell = regularCellData[ClassIndex];
		const int32 VertexCount = static_cast<int32>(Cell.GetVertexCount());
		const int32 TriangleCount = static_cast<int32>(Cell.GetTriangleCount());
		if (VertexCount > 12 || TriangleCount > 5)
		{
			return false;
		}

		for (int32 V = 0; V < VertexCount; ++V)
		{
			int32 A = 0;
			int32 B = 0;
			DecodeEdge(regularVertexData[CaseCode][V], A, B);
			if (A >= 8 || B >= 8)
			{
				return false;
			}
		}

		for (int32 I = 0; I < TriangleCount * 3; ++I)
		{
			if (Cell.vertexIndex[I] >= VertexCount)
			{
				return false;
			}
		}
	}

	for (int32 CaseCode = 0; CaseCode < 512; ++CaseCode)
	{
		const uint8 ClassAndFlip = transitionCellClass[CaseCode];
		const uint8 ClassIndex = ClassAndFlip & 0x7F;
		if (ClassIndex >= 56)
		{
			return false;
		}

		const TransitionCellData& Cell = transitionCellData[ClassIndex];
		const int32 VertexCount = static_cast<int32>(Cell.GetVertexCount());
		const int32 TriangleCount = static_cast<int32>(Cell.GetTriangleCount());
		if (VertexCount > 12 || TriangleCount > 12)
		{
			return false;
		}

		for (int32 V = 0; V < VertexCount; ++V)
		{
			int32 A = 0;
			int32 B = 0;
			DecodeEdge(transitionVertexData[CaseCode][V], A, B);
			if (A >= 13 || B >= 13)
			{
				return false;
			}
		}

		for (int32 I = 0; I < TriangleCount * 3; ++I)
		{
			if (Cell.vertexIndex[I] >= VertexCount)
			{
				return false;
			}
		}
	}

	return true;
}

void FSVoxelTransvoxelMesher::Generate(
	const FSVoxelGenerationSettings& Settings,
	int32 CellsPerAxis,
	float VoxelSize,
	uint8 TransitionFaceMask,
	FSVoxelMeshData& OutMesh)
{
	using namespace SVoxelOfficialTransvoxel;

	OutMesh.Reset();
	if (CellsPerAxis < 1 || CellsPerAxis > 128 ||
		!FMath::IsFinite(VoxelSize) || VoxelSize <= 0.0f)
	{
		return;
	}

	// The explicit mesher arguments are authoritative. Runtime callers already
	// pass these values through Settings, but automation fixtures and other
	// callers may not have prefilled them.
	FSVoxelGenerationSettings MeshSettings = Settings;
	MeshSettings.CellsPerAxis = CellsPerAxis;
	MeshSettings.VoxelSize = VoxelSize;

	const int32 SamplesPerAxis = CellsPerAxis + 1;
	const int64 SampleCount64 =
		static_cast<int64>(SamplesPerAxis) * SamplesPerAxis * SamplesPerAxis;
	if (SampleCount64 > MAX_int32)
	{
		return;
	}

	TArray<float> DensityField;
	DensityField.SetNumUninitialized(static_cast<int32>(SampleCount64));

	for (int32 Z = 0; Z < SamplesPerAxis; ++Z)
	{
		for (int32 Y = 0; Y < SamplesPerAxis; ++Y)
		{
			for (int32 X = 0; X < SamplesPerAxis; ++X)
			{
				const FVector LocalPosition(
					static_cast<float>(X) * VoxelSize,
					static_cast<float>(Y) * VoxelSize,
					static_cast<float>(Z) * VoxelSize);
				DensityField[SampleIndex(X, Y, Z, SamplesPerAxis)] =
					FSVoxelGenerator::SampleDensityAtChunkLocal(
						LocalPosition, MeshSettings);
			}
		}
	}

	OutMesh.Vertices.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Normals.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.UV0.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Triangles.Reserve(CellsPerAxis * CellsPerAxis * 12);
	TMap<FIntVector, uint32> VertexCache;
	VertexCache.Reserve(CellsPerAxis * CellsPerAxis * 4);
	const float ChunkSize = static_cast<float>(CellsPerAxis) * VoxelSize;

	// Generate regular cells at the selected LOD. Transition cells are emitted
	// on coarser faces adjacent to finer leaves, as identified by the octree.
	for (int32 Z = 0; Z < CellsPerAxis; ++Z)
	{
		for (int32 Y = 0; Y < CellsPerAxis; ++Y)
		{
			for (int32 X = 0; X < CellsPerAxis; ++X)
			{
				float Samples[8];
				int32 CaseCode = 0;

				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					const int32 CX = X + RegularCornerOffsets[Corner][0];
					const int32 CY = Y + RegularCornerOffsets[Corner][1];
					const int32 CZ = Z + RegularCornerOffsets[Corner][2];
					const float Value = DensityField[SampleIndex(CX, CY, CZ, SamplesPerAxis)];
					Samples[Corner] = Value;
					if (Value < Settings.IsoLevel)
					{
						CaseCode |= (1 << Corner);
					}
				}

				if (CaseCode == 0 || CaseCode == 255)
				{
					continue;
				}

				const RegularCellData& Cell = regularCellData[regularCellClass[CaseCode]];
				const int32 VertexCount = static_cast<int32>(Cell.GetVertexCount());
				const int32 TriangleCount = static_cast<int32>(Cell.GetTriangleCount());
				if (VertexCount <= 0 || TriangleCount <= 0)
				{
					continue;
				}

				FVector CellVertices[12];
				for (int32 V = 0; V < VertexCount; ++V)
				{
					int32 A = 0;
					int32 B = 0;
					DecodeEdge(regularVertexData[CaseCode][V], A, B);

					const float Denominator = Samples[B] - Samples[A];
					const float T = FMath::IsNearlyZero(Denominator)
						? 0.5f
						: FMath::Clamp((Settings.IsoLevel - Samples[A]) / Denominator, 0.0f, 1.0f);
					const FVector PA(
						static_cast<float>(X + RegularCornerOffsets[A][0]) * VoxelSize,
						static_cast<float>(Y + RegularCornerOffsets[A][1]) * VoxelSize,
						static_cast<float>(Z + RegularCornerOffsets[A][2]) * VoxelSize);
					const FVector PB(
						static_cast<float>(X + RegularCornerOffsets[B][0]) * VoxelSize,
						static_cast<float>(Y + RegularCornerOffsets[B][1]) * VoxelSize,
						static_cast<float>(Z + RegularCornerOffsets[B][2]) * VoxelSize);
					const FVector PrimaryPosition = FMath::Lerp(PA, PB, T);
					CellVertices[V] = ComputeTransitionSecondaryPosition(
						PrimaryPosition,
						MeshSettings,
						TransitionFaceMask,
						ChunkSize,
						VoxelSize);
				}

				AppendCellToMesh(
					CellVertices, VertexCount, Cell.vertexIndex, TriangleCount,
					false, MeshSettings, TransitionFaceMask, ChunkSize, OutMesh, VertexCache);
			}
		}
	}

	const uint8 FaceBits[6] =
	{
		SVoxelTransitionFace_NegativeX, SVoxelTransitionFace_PositiveX,
		SVoxelTransitionFace_NegativeY, SVoxelTransitionFace_PositiveY,
		SVoxelTransitionFace_NegativeZ, SVoxelTransitionFace_PositiveZ
	};

	for (const uint8 FaceBit : FaceBits)
	{
		if ((TransitionFaceMask & FaceBit) == 0)
		{
			continue;
		}

		for (int32 V = 0; V < CellsPerAxis; ++V)
		{
			for (int32 U = 0; U < CellsPerAxis; ++U)
			{
				float Samples[13];
				FVector SamplePositions[13];
				int32 CaseCode = 0;

				for (int32 I = 0; I < 13; ++I)
				{
					SamplePositions[I] = TransitionSampleToChunkLocal(
						FaceBit, U, V, CellsPerAxis, VoxelSize, I);
				}

				for (int32 I = 0; I < 9; ++I)
				{
					Samples[I] = FSVoxelGenerator::SampleDensityAtChunkLocal(
						SamplePositions[I], MeshSettings);
				}

				// Transvoxel requires corresponding full-resolution and half-resolution
				// corner samples to have identical scalar values (official Figure 4.16):
				// 0==9, 2==10, 6==11, 8==12. The latter four points are spatially
				// offset to form the transition cell, but their values are copied from
				// the boundary corners by design. Sampling the field again at those
				// offset positions breaks the table's assumptions, producing LOD-scaled
				// patches, corner holes, and dark seams.
				Samples[9] = Samples[0];
				Samples[10] = Samples[2];
				Samples[11] = Samples[6];
				Samples[12] = Samples[8];

				for (int32 Bit = 0; Bit < 9; ++Bit)
				{
					if (Samples[TransitionCaseBitToSample[Bit]] < Settings.IsoLevel)
					{
						CaseCode |= (1 << Bit);
					}
				}

				if (CaseCode == 0 || CaseCode == 511)
				{
					continue;
				}

				const uint8 ClassAndFlip = transitionCellClass[CaseCode];
				const uint8 ClassIndex = ClassAndFlip & 0x7F;
				const bool bTableFlip = (ClassAndFlip & 0x80) != 0;
				const TransitionCellData& Cell = transitionCellData[ClassIndex];
				const int32 VertexCount = static_cast<int32>(Cell.GetVertexCount());
				const int32 TriangleCount = static_cast<int32>(Cell.GetTriangleCount());
				if (VertexCount <= 0 || TriangleCount <= 0)
				{
					continue;
				}

				FVector CellVertices[12];
				for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
				{
					int32 A = 0;
					int32 B = 0;
					DecodeEdge(transitionVertexData[CaseCode][VertexIndex], A, B);
					const float Denominator = Samples[B] - Samples[A];
					const float T = FMath::IsNearlyZero(Denominator)
						? 0.5f
						: FMath::Clamp((Settings.IsoLevel - Samples[A]) / Denominator, 0.0f, 1.0f);
					CellVertices[VertexIndex] = FMath::Lerp(SamplePositions[A], SamplePositions[B], T);
				}

				AppendCellToMesh(
					CellVertices, VertexCount, Cell.vertexIndex, TriangleCount,
					bTableFlip, MeshSettings, TransitionFaceMask, ChunkSize, OutMesh, VertexCache);
			}
		}
	}
}
