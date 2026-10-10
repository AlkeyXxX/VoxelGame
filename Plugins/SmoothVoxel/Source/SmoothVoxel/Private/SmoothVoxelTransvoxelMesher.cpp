#include "SmoothVoxelTransvoxelMesher.h"

#include "SmoothVoxelOctree.h"
#include "TransvoxelCore/transvoxel.h"
#include "Math/UnrealMathUtility.h"

namespace
{
	FORCEINLINE int32 SampleIndex(int32 X, int32 Y, int32 Z, int32 N)
	{
		return X + Y * N + Z * N * N;
	}

	const int32 RegularCornerOffsets[8][3] =
	{
		{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
		{0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}
	};

	FVector SampleOutwardNormal(
		const FVector& WorldPosition,
		const FSVoxelGenerationSettings& Settings)
	{
		// Use a fixed world-space finite-difference step across all LODs so
		// coincident seam vertices receive normals from the same density field.
		const float H = 5.0f;
		const float Dx =
			FSVoxelGenerator::SampleDensity(WorldPosition.X + H, WorldPosition.Y, WorldPosition.Z, Settings) -
			FSVoxelGenerator::SampleDensity(WorldPosition.X - H, WorldPosition.Y, WorldPosition.Z, Settings);
		const float Dy =
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y + H, WorldPosition.Z, Settings) -
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y - H, WorldPosition.Z, Settings);
		const float Dz =
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y, WorldPosition.Z + H, Settings) -
			FSVoxelGenerator::SampleDensity(WorldPosition.X, WorldPosition.Y, WorldPosition.Z - H, Settings);

		// Density is positive in solid terrain, so -gradient points out of the solid.
		return FVector(-Dx, -Dy, -Dz).GetSafeNormal();
	}

	FVector TransitionSampleToChunkLocal(
		uint8 FaceBit,
		int32 CellU,
		int32 CellV,
		int32 CellsPerAxis,
		float CoarseStep,
		const TvVec3& Sample)
	{
		FVector FaceOrigin = FVector::ZeroVector;
		FVector AxisU = FVector(1.0f, 0.0f, 0.0f);
		FVector AxisV = FVector(0.0f, 1.0f, 0.0f);
		FVector Inward = FVector(0.0f, 0.0f, 1.0f);

		const float ChunkSize = static_cast<float>(CellsPerAxis) * CoarseStep;

		switch (FaceBit)
		{
		case SVoxelTransitionFace_NegativeX:
			FaceOrigin = FVector(0.0f, 0.0f, 0.0f);
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
			FaceOrigin = FVector(0.0f, 0.0f, 0.0f);
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
			FaceOrigin = FVector(0.0f, 0.0f, 0.0f);
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

		const float FineStep = CoarseStep * 0.5f;
		return FaceOrigin
			+ AxisU * ((static_cast<float>(CellU) * 2.0f + Sample.x) * FineStep)
			+ AxisV * ((static_cast<float>(CellV) * 2.0f + Sample.y) * FineStep)
			+ Inward * (Sample.z * CoarseStep);
	}

	void AppendCellToMesh(
		const FVector* CellLocalVertices,
		int32 VertexCount,
		const TvTriangle* CellTriangles,
		int32 TriangleCount,
		const FSVoxelGenerationSettings& Settings,
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
			const FVector WorldPosition = Settings.ChunkWorldOrigin + CellLocalVertices[I];
			CellNormals[I] = SampleOutwardNormal(WorldPosition, Settings);

			OutMesh.Vertices.Add(CellLocalVertices[I]);
			OutMesh.Normals.Add(CellNormals[I]);
			OutMesh.UV0.Add(FVector2D(WorldPosition.X * 0.0005f, WorldPosition.Y * 0.0005f));
		}

		for (int32 I = 0; I < TriangleCount; ++I)
		{
			uint32 A = CellTriangles[I].a;
			uint32 B = CellTriangles[I].b;
			uint32 C = CellTriangles[I].c;

			if (A >= static_cast<uint32>(VertexCount) ||
				B >= static_cast<uint32>(VertexCount) ||
				C >= static_cast<uint32>(VertexCount))
			{
				continue;
			}

			const FVector EdgeA = CellLocalVertices[B] - CellLocalVertices[A];
			const FVector EdgeB = CellLocalVertices[C] - CellLocalVertices[A];
			const FVector FaceNormal = FVector::CrossProduct(EdgeA, EdgeB);
			if (FaceNormal.SizeSquared() <= SMALL_NUMBER)
			{
				continue;
			}

			const FVector ExpectedNormal = (CellNormals[A] + CellNormals[B] + CellNormals[C]).GetSafeNormal();
			if (FVector::DotProduct(FaceNormal, ExpectedNormal) < 0.0f)
			{
				Swap(B, C);
			}

			OutMesh.Triangles.Add(BaseVertex + static_cast<int32>(A));
			OutMesh.Triangles.Add(BaseVertex + static_cast<int32>(B));
			OutMesh.Triangles.Add(BaseVertex + static_cast<int32>(C));
		}
	}
}

void FSVoxelTransvoxelMesher::Generate(
	const FSVoxelGenerationSettings& Settings,
	int32 CellsPerAxis,
	float VoxelSize,
	uint8 TransitionFaceMask,
	FSVoxelMeshData& OutMesh)
{
	OutMesh.Reset();

	if (CellsPerAxis < 1 || CellsPerAxis > 128 ||
		!FMath::IsFinite(VoxelSize) || VoxelSize <= 0.0f)
	{
		return;
	}

	const int32 SamplesPerAxis = CellsPerAxis + 1;
	const int64 SampleCount64 =
		static_cast<int64>(SamplesPerAxis) * SamplesPerAxis * SamplesPerAxis;
	if (SampleCount64 > MAX_int32)
	{
		return;
	}

	const int32 SampleCount = static_cast<int32>(SampleCount64);
	TArray<float> DensityField;
	DensityField.SetNumUninitialized(SampleCount);

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
						WorldPosition.X,
						WorldPosition.Y,
						WorldPosition.Z,
						Settings);
			}
		}
	}

	OutMesh.Vertices.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Normals.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.UV0.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Triangles.Reserve(CellsPerAxis * CellsPerAxis * 12);

	for (int32 Z = 0; Z < CellsPerAxis; ++Z)
	{
		for (int32 Y = 0; Y < CellsPerAxis; ++Y)
		{
			for (int32 X = 0; X < CellsPerAxis; ++X)
			{
				if (((TransitionFaceMask & SVoxelTransitionFace_NegativeX) && X == 0) ||
					((TransitionFaceMask & SVoxelTransitionFace_PositiveX) && X == CellsPerAxis - 1) ||
					((TransitionFaceMask & SVoxelTransitionFace_NegativeY) && Y == 0) ||
					((TransitionFaceMask & SVoxelTransitionFace_PositiveY) && Y == CellsPerAxis - 1) ||
					((TransitionFaceMask & SVoxelTransitionFace_NegativeZ) && Z == 0) ||
					((TransitionFaceMask & SVoxelTransitionFace_PositiveZ) && Z == CellsPerAxis - 1))
				{
					continue;
				}

				float Samples[TV_REGULAR_SAMPLE_COUNT];
				for (int32 Corner = 0; Corner < TV_REGULAR_SAMPLE_COUNT; ++Corner)
				{
					const int32 CX = X + RegularCornerOffsets[Corner][0];
					const int32 CY = Y + RegularCornerOffsets[Corner][1];
					const int32 CZ = Z + RegularCornerOffsets[Corner][2];
					Samples[Corner] = DensityField[SampleIndex(CX, CY, CZ, SamplesPerAxis)];
				}

				TvVec3 CellVertices[TV_REGULAR_MAX_VERTICES];
				TvTriangle CellTriangles[TV_REGULAR_MAX_TRIANGLES];
				const TvBuildInfo BuildInfo = tv_build_regular_cell(
					Samples,
					Settings.IsoLevel,
					tv_vec3(
						static_cast<float>(X) * VoxelSize,
						static_cast<float>(Y) * VoxelSize,
						static_cast<float>(Z) * VoxelSize),
					tv_vec3(VoxelSize, VoxelSize, VoxelSize),
					CellVertices,
					TV_REGULAR_MAX_VERTICES,
					CellTriangles,
					TV_REGULAR_MAX_TRIANGLES);

				if (BuildInfo.result != TV_OK ||
					BuildInfo.vertex_count <= 0 ||
					BuildInfo.triangle_count <= 0)
				{
					continue;
				}

				FVector LocalVertices[TV_REGULAR_MAX_VERTICES];
				for (int32 I = 0; I < BuildInfo.vertex_count; ++I)
				{
					LocalVertices[I] = FVector(
						CellVertices[I].x,
						CellVertices[I].y,
						CellVertices[I].z);
				}

				AppendCellToMesh(
					LocalVertices,
					BuildInfo.vertex_count,
					CellTriangles,
					BuildInfo.triangle_count,
					Settings,
					OutMesh);
			}
		}
	}

	const uint8 FaceBits[6] =
	{
		SVoxelTransitionFace_NegativeX,
		SVoxelTransitionFace_PositiveX,
		SVoxelTransitionFace_NegativeY,
		SVoxelTransitionFace_PositiveY,
		SVoxelTransitionFace_NegativeZ,
		SVoxelTransitionFace_PositiveZ
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
				float Samples[TV_TRANSITION_SAMPLE_COUNT];
				for (int32 SampleIndexInCell = 0;
					SampleIndexInCell < TV_TRANSITION_SAMPLE_COUNT;
					++SampleIndexInCell)
				{
					const TvVec3 SamplePosition = tv_transition_sample_position(SampleIndexInCell);
					const FVector LocalPosition = TransitionSampleToChunkLocal(
						FaceBit,
						U,
						V,
						CellsPerAxis,
						VoxelSize,
						SamplePosition);
					const FVector WorldPosition = Settings.ChunkWorldOrigin + LocalPosition;

					Samples[SampleIndexInCell] = FSVoxelGenerator::SampleDensity(
						WorldPosition.X,
						WorldPosition.Y,
						WorldPosition.Z,
						Settings);
				}

				TvVec3 TransitionVertices[TV_TRANSITION_MAX_VERTICES];
				TvTriangle TransitionTriangles[TV_TRANSITION_MAX_TRIANGLES];
				const TvBuildInfo BuildInfo = tv_build_transition_cell(
					Samples,
					Settings.IsoLevel,
					tv_vec3(0.0f, 0.0f, 0.0f),
					tv_vec3(1.0f, 1.0f, 1.0f),
					TransitionVertices,
					TV_TRANSITION_MAX_VERTICES,
					TransitionTriangles,
					TV_TRANSITION_MAX_TRIANGLES);

				if (BuildInfo.result != TV_OK ||
					BuildInfo.vertex_count <= 0 ||
					BuildInfo.triangle_count <= 0)
				{
					continue;
				}

				FVector LocalVertices[TV_TRANSITION_MAX_VERTICES];
				for (int32 I = 0; I < BuildInfo.vertex_count; ++I)
				{
					const TvVec3& P = TransitionVertices[I];
					LocalVertices[I] = TransitionSampleToChunkLocal(
						FaceBit,
						U,
						V,
						CellsPerAxis,
						VoxelSize,
						P);
				}

				AppendCellToMesh(
					LocalVertices,
					BuildInfo.vertex_count,
					TransitionTriangles,
					BuildInfo.triangle_count,
					Settings,
					OutMesh);
			}
		}
	}
}
