#include "SmoothVoxelTransvoxelMesher.h"

#include "SmoothVoxelOctree.h"
#include "Math/UnrealMathUtility.h"
#include <cmath>

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

		bool HasBlockEditNearPosition(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings)
	{
		const bool bHasBlockEdits =
			Settings.BlockEdits.IsValid() && Settings.BlockEdits->Num() > 0;
		const bool bHasMinedNodes =
			Settings.MinedNodes.IsValid() && Settings.MinedNodes->Num() > 0;
		if (!bHasBlockEdits && !bHasMinedNodes)
		{
			return false;
		}

		const double BaseStep = FMath::Max(1.0, static_cast<double>(Settings.BaseVoxelSize));
		const double WorldX = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.X) * BaseStep + LocalPosition.X
			: static_cast<double>(Settings.ChunkWorldOrigin.X) + LocalPosition.X;
		const double WorldY = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Y) * BaseStep + LocalPosition.Y
			: static_cast<double>(Settings.ChunkWorldOrigin.Y) + LocalPosition.Y;
		const double WorldZ = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Z) * BaseStep + LocalPosition.Z
			: static_cast<double>(Settings.ChunkWorldOrigin.Z) + LocalPosition.Z;

		const int32 CellX = static_cast<int32>(std::floor(WorldX / BaseStep));
		const int32 CellY = static_cast<int32>(std::floor(WorldY / BaseStep));
		const int32 CellZ = static_cast<int32>(std::floor(WorldZ / BaseStep));

		// Mined nodes sit on lattice vertices rather than cell centres. Check
		// them first because their sharper local curvature requires a short
		// gradient radius even when there are no ordinary block-cell edits.
		if (bHasMinedNodes)
		{
			// Edit snapshots are spatially filtered per chunk; most contain only
			// a handful of mined nodes, so a short linear scan avoids 125 hash
			// lookups for every emitted vertex near an edited chunk.
			if (Settings.MinedNodes->Num() <= 32)
			{
				for (const FIntVector& Node : *Settings.MinedNodes)
				{
					if (FMath::Abs(Node.X - CellX) <= 2 &&
						FMath::Abs(Node.Y - CellY) <= 2 &&
						FMath::Abs(Node.Z - CellZ) <= 2)
					{
						return true;
					}
				}
			}
			else
			{
				for (int32 DZ = -2; DZ <= 2; ++DZ)
				{
					for (int32 DY = -2; DY <= 2; ++DY)
					{
						for (int32 DX = -2; DX <= 2; ++DX)
						{
							if (Settings.MinedNodes->Contains(FIntVector(
								CellX + DX, CellY + DY, CellZ + DZ)))
							{
								return true;
							}
						}
					}
				}
			}
		}

		if (!bHasBlockEdits)
		{
			return false;
		}

		// Small edit snapshots are cheaper to scan linearly than checking 125
		// hash slots for every ordinary vertex in a neighbouring chunk.
		if (Settings.BlockEdits->Num() <= 32)
		{
			for (const TPair<FIntVector, int8>& Edit : *Settings.BlockEdits)
			{
				// A hard cube (state +2) is rendered as its own actor and does
				// not alter terrain density. It must not switch surrounding
				// terrain normals to the sharper edited-SDF sampling radius.
				if (Edit.Value == 0 || Edit.Value == 2)
				{
					continue;
				}
				if (FMath::Abs(Edit.Key.X - CellX) <= 2 &&
					FMath::Abs(Edit.Key.Y - CellY) <= 2 &&
					FMath::Abs(Edit.Key.Z - CellZ) <= 2)
				{
					return true;
				}
			}
			return false;
		}

		for (int32 DZ = -2; DZ <= 2; ++DZ)
		{
			for (int32 DY = -2; DY <= 2; ++DY)
			{
				for (int32 DX = -2; DX <= 2; ++DX)
				{
					if (const int8* EditState = Settings.BlockEdits->Find(
						FIntVector(CellX + DX, CellY + DY, CellZ + DZ)))
					{
						if (*EditState != 0 && *EditState != 2)
						{
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	FVector SampleOutwardNormal(
		const FVector& LocalPosition,
		const FSVoxelGenerationSettings& Settings,
		float SampleStep)
	{
		const bool bNearBlockEdit = HasBlockEditNearPosition(LocalPosition, Settings);

		// The global terrain normal radius (typically half a base voxel) is too
		// wide for the small SDF recesses made by the first mining hits. Using it
		// here averages the untouched hill through the crater and can orient the
		// newly exposed side faces as if they were still part of the hill.
		// Restrict only edited vertices to a local gradient sample; unedited
		// terrain keeps the established world-space step and the same cost.
		float H = FMath::Max(5.0f, SampleStep);
		if (bNearBlockEdit)
		{
			H = FMath::Min(
				H,
				FMath::Max(2.0f, Settings.BaseVoxelSize * 0.1f));
		}

		const double HeightXP = FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(
			LocalPosition + FVector(H, 0.0f, 0.0f), Settings);
		const double HeightXM = FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(
			LocalPosition - FVector(H, 0.0f, 0.0f), Settings);
		const double HeightYP = FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(
			LocalPosition + FVector(0.0f, H, 0.0f), Settings);
		const double HeightYM = FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(
			LocalPosition - FVector(0.0f, H, 0.0f), Settings);

		// A plain height field has an inexpensive analytical finite-difference
		// normal. Once mining/placement has modified the SDF, that shortcut is
		// no longer valid: it ignores the cut walls and made freshly exposed
		// triangles shade as if the original hill were still present.
		if ((!Settings.bEnableCaves || Settings.CaveStrength <= 0.0f) && !bNearBlockEdit)
		{
			return FVector(
				-static_cast<float>(HeightXP - HeightXM),
				-static_cast<float>(HeightYP - HeightYM),
				2.0f * H).GetSafeNormal();
		}

		// Caves and block edits alter the actual 3D density field. Reuse the
		// surface-height samples and compute the normal from that same field,
		// including the immutable block-edit snapshot captured for this chunk.
		const double HeightHere =
			FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(LocalPosition, Settings);
		const float Dx =
			FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
				LocalPosition + FVector(H, 0.0f, 0.0f), HeightXP, Settings) -
			FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
				LocalPosition - FVector(H, 0.0f, 0.0f), HeightXM, Settings);
		const float Dy =
			FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
				LocalPosition + FVector(0.0f, H, 0.0f), HeightYP, Settings) -
			FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
				LocalPosition - FVector(0.0f, H, 0.0f), HeightYM, Settings);
		const float Dz =
			FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
				LocalPosition + FVector(0.0f, 0.0f, H), HeightHere, Settings) -
			FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
				LocalPosition - FVector(0.0f, 0.0f, H), HeightHere, Settings);

		FVector OutwardNormal(-Dx, -Dy, -Dz);
		if (!OutwardNormal.Normalize())
		{
			// Exact corners of a Boolean SDF can have a zero central difference.
			// Fall back to the underlying height-field normal rather than emitting
			// a zero normal that can destabilize both shading and winding choice.
			OutwardNormal = FVector(
				-static_cast<float>(HeightXP - HeightXM),
				-static_cast<float>(HeightYP - HeightYM),
				2.0f * H).GetSafeNormal();
		}
		return OutwardNormal;
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

		// A vertex can lie on both a transitioning face and an adjacent
		// same-LOD face. It still needs the transition offset, but its coordinate
		// normal to the same-LOD face must remain fixed to avoid opening a crack.
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
		const uint8 FixedBorderMask =
			VertexBorderMask & static_cast<uint8>(~TransitionFaceMask);

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
		FVector TangentialDelta = Delta - Normal * FVector::DotProduct(Normal, Delta);

		// Tangent projection can introduce a component along a neighbouring
		// same-LOD face. Lock just that boundary axis rather than cancelling the
		// whole offset; this closes LOD gaps at mixed-face edges and corners.
		if (FixedBorderMask & (SVoxelTransitionFace_NegativeX | SVoxelTransitionFace_PositiveX))
		{
			TangentialDelta.X = 0.0f;
		}
		if (FixedBorderMask & (SVoxelTransitionFace_NegativeY | SVoxelTransitionFace_PositiveY))
		{
			TangentialDelta.Y = 0.0f;
		}
		if (FixedBorderMask & (SVoxelTransitionFace_NegativeZ | SVoxelTransitionFace_PositiveZ))
		{
			TangentialDelta.Z = 0.0f;
		}

		if (TangentialDelta.SizeSquared() <= SMALL_NUMBER)
		{
			return PrimaryPosition;
		}
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

		uint32 GetOrAddProjectedVertex(
		const FVector& LocalPosition,
		uint8 ProjectionAxis,
		const FSVoxelGenerationSettings& Settings,
		FSVoxelMeshData& OutMesh,
		TMap<FIntVector, uint32>& VertexCache)
	{
		// Keep sub-millimetre edge intersections distinct. Hard-cube cavities
		// are intentionally inset by fractions of a centimetre; quantizing to
		// 0.1 cm merged several different edge crossings onto one lattice node,
		// making valid triangles collapse and leaving real holes in the surface.
		// At 0.01 cm resolution, even the smallest deliberate cavity margin is
		// preserved while independently computed copies of a shared edge still
		// quantize to the same key.
		constexpr float VertexKeyScale = 100.0f; // 0.01 cm.
		const FIntVector VertexKey(
			FMath::RoundToInt(LocalPosition.X * VertexKeyScale),
			FMath::RoundToInt(LocalPosition.Y * VertexKeyScale),
			FMath::RoundToInt(LocalPosition.Z * VertexKeyScale));
		if (const uint32* ExistingIndex = VertexCache.Find(VertexKey))
		{
			return *ExistingIndex;
		}

		const FVector Normal = SampleOutwardNormal(
			LocalPosition, Settings, ComputeNormalSampleStep(Settings));

		double WorldX = 0.0;
		double WorldY = 0.0;
		GetWorldXYForUV(LocalPosition, Settings, WorldX, WorldY);
		const double WorldZ = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Z) * static_cast<double>(Settings.BaseVoxelSize) + static_cast<double>(LocalPosition.Z)
			: static_cast<double>(Settings.ChunkWorldOrigin.Z) + static_cast<double>(LocalPosition.Z);

		const double ChunkOriginWorldX = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.X) * static_cast<double>(Settings.BaseVoxelSize)
			: static_cast<double>(Settings.ChunkWorldOrigin.X);
		const double ChunkOriginWorldY = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Y) * static_cast<double>(Settings.BaseVoxelSize)
			: static_cast<double>(Settings.ChunkWorldOrigin.Y);
		const double ChunkOriginWorldZ = Settings.bUseChunkLatticeCoordinates
			? static_cast<double>(Settings.ChunkOriginCells.Z) * static_cast<double>(Settings.BaseVoxelSize)
			: static_cast<double>(Settings.ChunkWorldOrigin.Z);

		// Select UV axes once per triangle. A vertex shared between floor and
		// wall projections otherwise inherits whichever mapping was emitted
		// first, making texture coordinates jump after terrain edits.
		constexpr double UVWorldScale = 0.00125; // One repeat per 800 cm.
		double U = WorldX;
		double V = WorldY;
		double OriginU = ChunkOriginWorldX;
		double OriginV = ChunkOriginWorldY;
		FVector WorldU(1.0f, 0.0f, 0.0f);
		FVector WorldV(0.0f, 1.0f, 0.0f);
		switch (ProjectionAxis)
		{
		case 0: // X-facing wall: YZ projection.
			U = WorldY;
			V = WorldZ;
			OriginU = ChunkOriginWorldY;
			OriginV = ChunkOriginWorldZ;
			WorldU = FVector(0.0f, 1.0f, 0.0f);
			WorldV = FVector(0.0f, 0.0f, 1.0f);
			break;
		case 1: // Y-facing wall: XZ projection.
			U = WorldX;
			V = WorldZ;
			OriginU = ChunkOriginWorldX;
			OriginV = ChunkOriginWorldZ;
			WorldU = FVector(1.0f, 0.0f, 0.0f);
			WorldV = FVector(0.0f, 0.0f, 1.0f);
			break;
		default: // Horizontal / floor: XY projection.
			break;
		}

		// Keep GPU UV values small and chunk offsets repeat-aligned; perform
		// the rebase in double precision to avoid precision loss at large origins.
		const double TileOffsetU = std::floor(OriginU * UVWorldScale);
		const double TileOffsetV = std::floor(OriginV * UVWorldScale);
		const FVector2D UV(
			static_cast<float>(U * UVWorldScale - TileOffsetU),
			static_cast<float>(V * UVWorldScale - TileOffsetV));

		FVector Tangent = WorldU - Normal * FVector::DotProduct(Normal, WorldU);
		if (!Tangent.Normalize())
		{
			Tangent = (WorldV - Normal * FVector::DotProduct(Normal, WorldV)).GetSafeNormal();
		}
		FVector Bitangent = WorldV - Normal * FVector::DotProduct(Normal, WorldV);
		Bitangent.Normalize();
		const bool bFlipTangentY =
			FVector::DotProduct(FVector::CrossProduct(Normal, Tangent), Bitangent) < 0.0f;

		const uint32 NewIndex = static_cast<uint32>(OutMesh.Vertices.Num());
		VertexCache.Add(VertexKey, NewIndex);
		OutMesh.Vertices.Add(LocalPosition);
		OutMesh.Normals.Add(Normal);
		OutMesh.UV0.Add(UV);
		OutMesh.Tangents.Add(FProcMeshTangent(Tangent, bFlipTangentY));
		return NewIndex;
	}

	void AppendCellToMesh(
		const FVector* CellLocalVertices,
		int32 VertexCount,
		const uint8* TriangleIndices,
		int32 TriangleCount,
		bool bFlipWinding,
		const FSVoxelGenerationSettings& Settings,
		FSVoxelMeshData& OutMesh,
		TMap<FIntVector, uint32> VertexCaches[3])
	{
		if (VertexCount <= 0 || TriangleCount <= 0)
		{
			return;
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

			const FVector& PositionA = CellLocalVertices[A];
			const FVector& PositionB = CellLocalVertices[B];
			const FVector& PositionC = CellLocalVertices[C];
			const FVector FaceNormal = FVector::CrossProduct(
				PositionB - PositionA,
				PositionC - PositionA);
			if (FaceNormal.SizeSquared() <= SMALL_NUMBER)
			{
				continue;
			}

			const float AbsX = FMath::Abs(FaceNormal.X);
			const float AbsY = FMath::Abs(FaceNormal.Y);
			const float AbsZ = FMath::Abs(FaceNormal.Z);
			const uint8 ProjectionAxis = AbsX >= AbsY && AbsX >= AbsZ
				? 0
				: (AbsY >= AbsZ ? 1 : 2);
			TMap<FIntVector, uint32>& VertexCache = VertexCaches[ProjectionAxis];

			const uint32 GlobalA = GetOrAddProjectedVertex(
				PositionA, ProjectionAxis, Settings, OutMesh, VertexCache);
			uint32 GlobalB = GetOrAddProjectedVertex(
				PositionB, ProjectionAxis, Settings, OutMesh, VertexCache);
			uint32 GlobalC = GetOrAddProjectedVertex(
				PositionC, ProjectionAxis, Settings, OutMesh, VertexCache);

			if (GlobalA == GlobalB || GlobalB == GlobalC || GlobalC == GlobalA)
			{
				continue;
			}

			const FVector& MeshA = OutMesh.Vertices[static_cast<int32>(GlobalA)];
			const FVector& MeshB = OutMesh.Vertices[static_cast<int32>(GlobalB)];
			const FVector& MeshC = OutMesh.Vertices[static_cast<int32>(GlobalC)];
			const FVector MeshFaceNormal = FVector::CrossProduct(
				MeshB - MeshA, MeshC - MeshA);
			if (MeshFaceNormal.SizeSquared() <= SMALL_NUMBER)
			{
				continue;
			}

			FVector ExpectedNormal = (
				OutMesh.Normals[static_cast<int32>(GlobalA)] +
				OutMesh.Normals[static_cast<int32>(GlobalB)] +
				OutMesh.Normals[static_cast<int32>(GlobalC)]).GetSafeNormal();
			const FVector FaceDirection = MeshFaceNormal.GetSafeNormal();
			float Facing = FVector::DotProduct(FaceDirection, ExpectedNormal);
			bool bReverseWinding = false;
			bool bOrientationResolved = false;

			// Prefer a well-conditioned averaged normal. At sharp mining corners,
			// the three vertex normals can cancel or become nearly tangent, so
			// recompute the gradient locally before falling back to the table.
			if (ExpectedNormal.SizeSquared() > SMALL_NUMBER && FMath::Abs(Facing) >= 0.05f)
			{
				bReverseWinding = Facing > 0.0f;
				bOrientationResolved = true;
			}
			else
			{
				const FVector TriangleCenter = (MeshA + MeshB + MeshC) / 3.0f;
				const FVector LocalOutwardNormal = SampleOutwardNormal(
					TriangleCenter, Settings, ComputeNormalSampleStep(Settings));
				const float LocalFacing = FVector::DotProduct(FaceDirection, LocalOutwardNormal);
				if (LocalOutwardNormal.SizeSquared() > SMALL_NUMBER &&
					FMath::Abs(LocalFacing) >= 0.05f)
				{
					bReverseWinding = LocalFacing > 0.0f;
					bOrientationResolved = true;
				}
				else
				{
					// Density can resolve only the genuinely ambiguous cases. Do
					// not infer winding from density magnitudes when both probes are
					// on the same side of the isosurface: that produced random flips
					// as the edited SDF changed.
					const float ProbeDistance = FMath::Max(
						5.0f, Settings.BaseVoxelSize * 0.1f);
					const float DensityAlongFace = FSVoxelGenerator::SampleDensityAtChunkLocal(
						TriangleCenter + FaceDirection * ProbeDistance, Settings);
					const float DensityAgainstFace = FSVoxelGenerator::SampleDensityAtChunkLocal(
						TriangleCenter - FaceDirection * ProbeDistance, Settings);
					const bool bProbesStraddleSurface =
						(DensityAlongFace > Settings.IsoLevel) !=
						(DensityAgainstFace > Settings.IsoLevel);
					if (bProbesStraddleSurface)
					{
						// The raw cross must point into solid for this project's
						// procedural-mesh front-face convention.
						bReverseWinding = DensityAlongFace < DensityAgainstFace;
						bOrientationResolved = true;
					}
					else if (ExpectedNormal.SizeSquared() > SMALL_NUMBER &&
						FMath::Abs(Facing) > SMALL_NUMBER)
					{
						bReverseWinding = Facing > 0.0f;
						bOrientationResolved = true;
					}
				}
			}

			// If neither the gradient nor density can resolve a degenerate corner,
			// preserve the lookup table's original winding/transition flip.
			if (bOrientationResolved && bReverseWinding)
			{
				Swap(GlobalB, GlobalC);
			}

			OutMesh.Triangles.Add(static_cast<int32>(GlobalA));
			OutMesh.Triangles.Add(static_cast<int32>(GlobalB));
			OutMesh.Triangles.Add(static_cast<int32>(GlobalC));
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

	// Terrain height depends on X/Y only. Cache each column once instead of
	// recalculating two three-octave FBMs for all 33 Z samples in that column.
	// This preserves the same scalar field (including optional 3D cave carving).
	TArray<double> SurfaceHeightField;
	SurfaceHeightField.SetNumUninitialized(SamplesPerAxis * SamplesPerAxis);
	for (int32 Y = 0; Y < SamplesPerAxis; ++Y)
	{
		for (int32 X = 0; X < SamplesPerAxis; ++X)
		{
			const FVector LocalPosition(
				static_cast<float>(X) * VoxelSize,
				static_cast<float>(Y) * VoxelSize,
				0.0f);
			SurfaceHeightField[SampleIndex(X, Y, 0, SamplesPerAxis)] =
				FSVoxelGenerator::SampleSurfaceHeightAtChunkLocal(LocalPosition, MeshSettings);
		}
	}

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
					FSVoxelGenerator::SampleDensityAtChunkLocalWithSurfaceHeight(
						LocalPosition,
						SurfaceHeightField[SampleIndex(X, Y, 0, SamplesPerAxis)],
						MeshSettings);
			}
		}
	}

	OutMesh.Vertices.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Normals.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.UV0.Reserve(CellsPerAxis * CellsPerAxis * 6);
	OutMesh.Triangles.Reserve(CellsPerAxis * CellsPerAxis * 12);
	TMap<FIntVector, uint32> VertexCaches[3];
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		VertexCaches[Axis].Reserve(CellsPerAxis * CellsPerAxis * 4);
	}
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
					false, MeshSettings, OutMesh, VertexCaches);
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
					bTableFlip, MeshSettings, OutMesh, VertexCaches);
			}
		}
	}
}
