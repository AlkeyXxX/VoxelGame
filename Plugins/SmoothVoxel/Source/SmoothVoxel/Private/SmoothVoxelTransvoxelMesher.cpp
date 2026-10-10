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
		const FVector& WorldPosition,
		const FSVoxelGenerationSettings& Settings,
		float SampleStep)
	{
		const float H = FMath::Max(5.0f, SampleStep);
		const float Dx =
			FSVoxelGenerator::SampleDensity(WorldPosition.X + H, WorldPosition.Y, WorldPosition.Z, Settings) -
			FSVoxelGenerator::SampleDensity(WorldPosition.X - H, WorldPosition.Y, WorldPosition.Z, Settings);
		const float Dy =
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y + H, WorldPosition.Z, Settings) -
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y - H, WorldPosition.Z, Settings);
		const float Dz =
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y, WorldPosition.Z + H, Settings) -
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y, WorldPosition.Z - H, Settings);

		// Density is positive inside solid terrain, so its negative gradient
		// points out of the solid.
		return FVector(-Dx, -Dy, -Dz).GetSafeNormal();
	}

	FVector AdjustBoundaryCorner(
		FVector Position,
		uint8 TransitionFaceMask,
		uint8 FaceToExclude,
		float ChunkSize,
		float HalfStep)
	{
		if ((TransitionFaceMask & SVoxelTransitionFace_NegativeX) &&
			!(FaceToExclude & SVoxelTransitionFace_NegativeX) &&
			FMath::IsNearlyZero(Position.X))
		{
			Position.X += HalfStep;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_PositiveX) &&
			!(FaceToExclude & SVoxelTransitionFace_PositiveX) &&
			FMath::IsNearlyEqual(Position.X, ChunkSize))
		{
			Position.X -= HalfStep;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_NegativeY) &&
			!(FaceToExclude & SVoxelTransitionFace_NegativeY) &&
			FMath::IsNearlyZero(Position.Y))
		{
			Position.Y += HalfStep;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_PositiveY) &&
			!(FaceToExclude & SVoxelTransitionFace_PositiveY) &&
			FMath::IsNearlyEqual(Position.Y, ChunkSize))
		{
			Position.Y -= HalfStep;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_NegativeZ) &&
			!(FaceToExclude & SVoxelTransitionFace_NegativeZ) &&
			FMath::IsNearlyZero(Position.Z))
		{
			Position.Z += HalfStep;
		}
		if ((TransitionFaceMask & SVoxelTransitionFace_PositiveZ) &&
			!(FaceToExclude & SVoxelTransitionFace_PositiveZ) &&
			FMath::IsNearlyEqual(Position.Z, ChunkSize))
		{
			Position.Z -= HalfStep;
		}
		return Position;
	}

	FVector TransitionSampleToChunkLocal(
		uint8 FaceBit,
		uint8 TransitionFaceMask,
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

		const FVector RawPosition = FaceOrigin + AxisU * U + AxisV * V + Inward * Depth;
		// At edges/corners where another face also transitions, use the same
		// half-cell inset as the neighboring transition strip.
		return AdjustBoundaryCorner(
			RawPosition, TransitionFaceMask, FaceBit, ChunkSize, CoarseStep * 0.5f);
	}

	float ComputeNormalSampleStep(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings,
		uint8 TransitionFaceMask,
		float ChunkSize)
	{
		const float CoarseNormalStep = FMath::Max(5.0f, Settings.VoxelSize * 0.5f);
		if (TransitionFaceMask == 0)
		{
			return CoarseNormalStep;
		}

		// Share the finer neighbour's normal sampling distance at the seam,
		// then blend towards the coarse LOD scale over one coarse cell.
		float DistanceToTransitionFace = TNumericLimits<float>::Max();
		if (TransitionFaceMask & SVoxelTransitionFace_NegativeX)
		{
			DistanceToTransitionFace = FMath::Min(DistanceToTransitionFace, FMath::Max(0.0f, LocalPosition.X));
		}
		if (TransitionFaceMask & SVoxelTransitionFace_PositiveX)
		{
			DistanceToTransitionFace = FMath::Min(DistanceToTransitionFace, FMath::Max(0.0f, ChunkSize - LocalPosition.X));
		}
		if (TransitionFaceMask & SVoxelTransitionFace_NegativeY)
		{
			DistanceToTransitionFace = FMath::Min(DistanceToTransitionFace, FMath::Max(0.0f, LocalPosition.Y));
		}
		if (TransitionFaceMask & SVoxelTransitionFace_PositiveY)
		{
			DistanceToTransitionFace = FMath::Min(DistanceToTransitionFace, FMath::Max(0.0f, ChunkSize - LocalPosition.Y));
		}
		if (TransitionFaceMask & SVoxelTransitionFace_NegativeZ)
		{
			DistanceToTransitionFace = FMath::Min(DistanceToTransitionFace, FMath::Max(0.0f, LocalPosition.Z));
		}
		if (TransitionFaceMask & SVoxelTransitionFace_PositiveZ)
		{
			DistanceToTransitionFace = FMath::Min(DistanceToTransitionFace, FMath::Max(0.0f, ChunkSize - LocalPosition.Z));
		}

		if (DistanceToTransitionFace == TNumericLimits<float>::Max())
		{
			return CoarseNormalStep;
		}

		const float FineNormalStep = FMath::Max(5.0f, Settings.VoxelSize * 0.25f);
		const float Blend = FMath::Clamp(
			DistanceToTransitionFace / FMath::Max(1.0f, Settings.VoxelSize),
			0.0f,
			1.0f);
		return FMath::Lerp(FineNormalStep, CoarseNormalStep, Blend);
	}

	void DecodeEdge(uint16 EdgeCode, int32& OutSampleA, int32& OutSampleB)
	{
		// The low byte stores the endpoint indices; the high byte is the
		// upstream vertex-reuse hint and is not required by this non-indexed mesh.
		OutSampleB = EdgeCode & 0x0F;
		OutSampleA = (EdgeCode >> 4) & 0x0F;
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
		FSVoxelMeshData& OutMesh)
	{
		if (VertexCount <= 0 || TriangleCount <= 0)
		{
			return;
		}

		const int32 BaseVertex = OutMesh.Vertices.Num();
		TArray<FVector> CellNormals;
		CellNormals.SetNumUninitialized(VertexCount);

		for (int32 I = 0; I < VertexCount; ++I)
		{
			const FVector LocalPosition = CellLocalVertices[I];
			const FVector WorldPosition = Settings.ChunkWorldOrigin + LocalPosition;
			const float NormalSampleStep = ComputeNormalSampleStep(
				LocalPosition, Settings, TransitionFaceMask, ChunkSize);
			CellNormals[I] = SampleOutwardNormal(WorldPosition, Settings, NormalSampleStep);
			OutMesh.Vertices.Add(LocalPosition);
			OutMesh.Normals.Add(CellNormals[I]);

			// Dominant-axis projection preserves XY mapping on terrain tops but
			// projects near-vertical rock surfaces onto their own plane.
			const FVector AbsNormal = CellNormals[I].GetAbs();
			FVector2D UV;
			if (AbsNormal.Z >= AbsNormal.X && AbsNormal.Z >= AbsNormal.Y)
			{
				UV = FVector2D(
					WorldPosition.X,
					CellNormals[I].Z >= 0.0f ? WorldPosition.Y : -WorldPosition.Y);
			}
			else if (AbsNormal.X >= AbsNormal.Y)
			{
				UV = FVector2D(
					CellNormals[I].X >= 0.0f ? WorldPosition.Y : -WorldPosition.Y,
					WorldPosition.Z);
			}
			else
			{
				UV = FVector2D(
					CellNormals[I].Y >= 0.0f ? WorldPosition.X : -WorldPosition.X,
					WorldPosition.Z);
			}
			OutMesh.UV0.Add(UV * 0.0005f);
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

			OutMesh.Triangles.Add(BaseVertex + static_cast<int32>(A));
			OutMesh.Triangles.Add(BaseVertex + static_cast<int32>(B));
			OutMesh.Triangles.Add(BaseVertex + static_cast<int32>(C));
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
				const FVector WorldPosition = Settings.ChunkWorldOrigin + FVector(
					static_cast<float>(X) * VoxelSize,
					static_cast<float>(Y) * VoxelSize,
					static_cast<float>(Z) * VoxelSize);
				DensityField[SampleIndex(X, Y, Z, SamplesPerAxis)] =
					FSVoxelGenerator::SampleDensity(
						WorldPosition.X, WorldPosition.Y, WorldPosition.Z, Settings);
			}
		}
	}

	OutMesh.Vertices.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Normals.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.UV0.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Triangles.Reserve(CellsPerAxis * CellsPerAxis * 12);
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
					const FVector PA = AdjustBoundaryCorner(
						FVector(
							static_cast<float>(X + RegularCornerOffsets[A][0]) * VoxelSize,
							static_cast<float>(Y + RegularCornerOffsets[A][1]) * VoxelSize,
							static_cast<float>(Z + RegularCornerOffsets[A][2]) * VoxelSize),
						TransitionFaceMask, 0, ChunkSize, VoxelSize * 0.5f);
					const FVector PB = AdjustBoundaryCorner(
						FVector(
							static_cast<float>(X + RegularCornerOffsets[B][0]) * VoxelSize,
							static_cast<float>(Y + RegularCornerOffsets[B][1]) * VoxelSize,
							static_cast<float>(Z + RegularCornerOffsets[B][2]) * VoxelSize),
						TransitionFaceMask, 0, ChunkSize, VoxelSize * 0.5f);
					CellVertices[V] = FMath::Lerp(PA, PB, T);
				}

				AppendCellToMesh(
					CellVertices, VertexCount, Cell.vertexIndex, TriangleCount,
					false, MeshSettings, TransitionFaceMask, ChunkSize, OutMesh);
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
						FaceBit, TransitionFaceMask, U, V, CellsPerAxis, VoxelSize, I);
				}

				for (int32 I = 0; I < 9; ++I)
				{
					const FVector WorldPosition = Settings.ChunkWorldOrigin + SamplePositions[I];
					Samples[I] = FSVoxelGenerator::SampleDensity(
						WorldPosition.X, WorldPosition.Y, WorldPosition.Z, Settings);
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
					bTableFlip, MeshSettings, TransitionFaceMask, ChunkSize, OutMesh);
			}
		}
	}
}
