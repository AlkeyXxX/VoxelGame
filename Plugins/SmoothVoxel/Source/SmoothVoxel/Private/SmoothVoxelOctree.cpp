#include "SmoothVoxelOctree.h"

namespace
{
	struct FSVoxelNodeLookup
	{
		TMap<FSVoxelOctreeNode, int32> IndexByNode;
	};

	FVector MakeNodeWorldMin(
		const FSVoxelOctreeNode& Node,
		const FSVoxelOctreeSelectionSettings& Settings)
	{
		return Node.GetWorldOrigin(Settings.WorldOrigin, Settings.BaseVoxelSize);
	}

	void AddChildren(
		const FSVoxelOctreeNode& Parent,
		int32 CellsPerAxis,
		TArray<FSVoxelOctreeNode>& InOutLeaves)
	{
		if (Parent.Lod <= 0)
		{
			return;
		}

		const int32 ChildLod = Parent.Lod - 1;
		const int32 ChildSpan = CellsPerAxis * (1 << ChildLod);

		for (int32 Z = 0; Z < 2; ++Z)
		{
			for (int32 Y = 0; Y < 2; ++Y)
			{
				for (int32 X = 0; X < 2; ++X)
				{
					const FIntVector ChildOrigin = Parent.OriginCells + FIntVector(
						X * ChildSpan,
						Y * ChildSpan,
						Z * ChildSpan);

					InOutLeaves.Add(FSVoxelOctreeNode(ChildOrigin, ChildLod));
				}
			}
		}
	}

	bool SelectNode(
		const FSVoxelOctreeNode& Node,
		const FVector& CameraWorldLocation,
		const FSVoxelOctreeSelectionSettings& Settings,
		TArray<FSVoxelOctreeNode>& OutLeaves)
	{
		const float NodeSize = Node.GetWorldSize(
			Settings.CellsPerAxis,
			Settings.BaseVoxelSize);

		const FVector WorldMin = MakeNodeWorldMin(Node, Settings);
		const FBox Bounds(WorldMin, WorldMin + FVector(NodeSize));
		const float SplitDistance = NodeSize * Settings.SplitDistanceMultiplier;

		const bool bShouldSplit =
			Node.Lod > 0 &&
			Bounds.ComputeSquaredDistanceToPoint(CameraWorldLocation) <
				FMath::Square(SplitDistance) &&
			OutLeaves.Num() + 8 <= Settings.MaxLeafCount;

		if (!bShouldSplit)
		{
			if (OutLeaves.Num() >= Settings.MaxLeafCount)
			{
				return false;
			}

			OutLeaves.Add(Node);
			return true;
		}

		TArray<FSVoxelOctreeNode> Children;
		Children.Reserve(8);
		AddChildren(Node, Settings.CellsPerAxis, Children);

		for (const FSVoxelOctreeNode& Child : Children)
		{
			if (!SelectNode(Child, CameraWorldLocation, Settings, OutLeaves))
			{
				return false;
			}
		}

		return true;
	}

	void BuildLookup(
		const TArray<FSVoxelOctreeNode>& Leaves,
		FSVoxelNodeLookup& OutLookup)
	{
		OutLookup.IndexByNode.Reset();
		OutLookup.IndexByNode.Reserve(Leaves.Num());

		for (int32 Index = 0; Index < Leaves.Num(); ++Index)
		{
			OutLookup.IndexByNode.Add(Leaves[Index], Index);
		}
	}

	bool FindLeafContainingCell(
		const FIntVector& Cell,
		const FIntVector& RootOriginCells,
		int32 RootSpan,
		int32 CellsPerAxis,
		int32 MaxLod,
		const FSVoxelNodeLookup& Lookup,
		FSVoxelOctreeNode& OutNode)
	{
		const FIntVector RelativeCell = Cell - RootOriginCells;
		if (RelativeCell.X < 0 || RelativeCell.Y < 0 || RelativeCell.Z < 0 ||
			RelativeCell.X >= RootSpan || RelativeCell.Y >= RootSpan || RelativeCell.Z >= RootSpan)
		{
			return false;
		}

		// Work in root-local cell coordinates, but return nodes keyed by
		// absolute world-lattice origins. Overlapping stream windows can then
		// reuse chunks without relocating them to different terrain.
		for (int32 Lod = 0; Lod <= MaxLod; ++Lod)
		{
			const int32 Span = CellsPerAxis * (1 << Lod);
			const FIntVector Origin = RootOriginCells + FIntVector(
				(RelativeCell.X / Span) * Span,
				(RelativeCell.Y / Span) * Span,
				(RelativeCell.Z / Span) * Span);

			const FSVoxelOctreeNode Candidate(Origin, Lod);
			if (Lookup.IndexByNode.Contains(Candidate))
			{
				OutNode = Candidate;
				return true;
			}
		}

		return false;
	}

	bool CollectCoarseLeavesThatNeedSplitting(
		const TArray<FSVoxelOctreeNode>& Leaves,
		const FSVoxelOctreeSelectionSettings& Settings,
		int32 RootSpan,
		TSet<FSVoxelOctreeNode>& OutSplitSet)
	{
		OutSplitSet.Reset();

		FSVoxelNodeLookup Lookup;
		BuildLookup(Leaves, Lookup);

		const FIntVector Directions[6] =
		{
			FIntVector(-1, 0, 0),
			FIntVector(1, 0, 0),
			FIntVector(0, -1, 0),
			FIntVector(0, 1, 0),
			FIntVector(0, 0, -1),
			FIntVector(0, 0, 1)
		};

		for (const FSVoxelOctreeNode& Node : Leaves)
		{
			const int32 Span = Node.GetSpanInBaseCells(Settings.CellsPerAxis);

			for (const FIntVector& Direction : Directions)
		{
			const int32 TangentOffsets[2] = { Span / 4, (Span * 3) / 4 };

			// At the boundary between two octree levels a face can overlap
			// several leaves. Probe all four quadrants; a single centre probe
			// can miss an unbalanced neighbour near a face edge.
			for (int32 UIndex = 0; UIndex < 2; ++UIndex)
			{
				for (int32 VIndex = 0; VIndex < 2; ++VIndex)
				{
					FIntVector Probe = Node.OriginCells + FIntVector(
						Span / 2,
						Span / 2,
						Span / 2);

					if (Direction.X != 0)
					{
						Probe.Y = Node.OriginCells.Y + TangentOffsets[UIndex];
						Probe.Z = Node.OriginCells.Z + TangentOffsets[VIndex];
						Probe.X = Direction.X < 0
							? Node.OriginCells.X - 1
							: Node.OriginCells.X + Span;
					}
					else if (Direction.Y != 0)
					{
						Probe.X = Node.OriginCells.X + TangentOffsets[UIndex];
						Probe.Z = Node.OriginCells.Z + TangentOffsets[VIndex];
						Probe.Y = Direction.Y < 0
							? Node.OriginCells.Y - 1
							: Node.OriginCells.Y + Span;
					}
					else
					{
						Probe.X = Node.OriginCells.X + TangentOffsets[UIndex];
						Probe.Y = Node.OriginCells.Y + TangentOffsets[VIndex];
						Probe.Z = Direction.Z < 0
							? Node.OriginCells.Z - 1
							: Node.OriginCells.Z + Span;
					}

					FSVoxelOctreeNode Neighbour;
					if (FindLeafContainingCell(
						Probe,
						Settings.RootOriginCells,
						RootSpan,
						Settings.CellsPerAxis,
						Settings.MaxLod,
						Lookup,
						Neighbour) &&
						Neighbour.Lod > Node.Lod + 1)
					{
						OutSplitSet.Add(Neighbour);
					}
				}
			}
		}
		}

		return true;
	}

	bool SplitSelectedLeaves(
		TArray<FSVoxelOctreeNode>& InOutLeaves,
		const TSet<FSVoxelOctreeNode>& SplitSet,
		const FSVoxelOctreeSelectionSettings& Settings)
	{
		if (SplitSet.Num() == 0)
		{
			return true;
		}

		const int64 RequiredLeafCount =
			static_cast<int64>(InOutLeaves.Num()) +
			static_cast<int64>(SplitSet.Num()) * 7;

		if (RequiredLeafCount > Settings.MaxLeafCount)
		{
			return false;
		}

		// Remove in reverse index order, preserving the validity of earlier
		// indexes, then append all eight children for each selected parent.
		TArray<FSVoxelOctreeNode> Parents;
		Parents.Reserve(SplitSet.Num());

		for (int32 Index = InOutLeaves.Num() - 1; Index >= 0; --Index)
		{
			if (SplitSet.Contains(InOutLeaves[Index]))
			{
				Parents.Add(InOutLeaves[Index]);
				InOutLeaves.RemoveAt(Index, 1, false);
			}
		}

		for (const FSVoxelOctreeNode& Parent : Parents)
		{
			AddChildren(Parent, Settings.CellsPerAxis, InOutLeaves);
		}

		return true;
	}
}

bool FSVoxelOctreeSelector::BuildBalancedLeaves(
	const FVector& CameraWorldLocation,
	const FSVoxelOctreeSelectionSettings& InSettings,
	TArray<FSVoxelOctreeNode>& OutLeaves)
{
	OutLeaves.Reset();

	if (InSettings.CellsPerAxis <= 0 ||
		InSettings.BaseVoxelSize <= 0.0f ||
		InSettings.MaxLeafCount < 1)
	{
		return false;
	}

	FSVoxelOctreeSelectionSettings Settings = InSettings;
	Settings.MaxLod = FMath::Clamp(Settings.MaxLod, 0, 10);
	Settings.SplitDistanceMultiplier = FMath::Max(0.01f, Settings.SplitDistanceMultiplier);
	Settings.MaxLeafCount = FMath::Max(1, Settings.MaxLeafCount);

	const int32 RootSpan = Settings.CellsPerAxis * (1 << Settings.MaxLod);
	OutLeaves.Reserve(FMath::Min(Settings.MaxLeafCount, 4096));

	if (!SelectNode(
		FSVoxelOctreeNode(Settings.RootOriginCells, Settings.MaxLod),
		CameraWorldLocation,
		Settings,
		OutLeaves))
	{
		OutLeaves.Reset();
		return false;
	}

	TSet<FSVoxelOctreeNode> SplitSet;

	// Each successful balancing pass increases the leaf count. The hard cap
	// bounds work and also guarantees the loop cannot split indefinitely.
	for (int32 Pass = 0; Pass <= Settings.MaxLeafCount; ++Pass)
	{
		CollectCoarseLeavesThatNeedSplitting(
			OutLeaves,
			Settings,
			RootSpan,
			SplitSet);

		if (SplitSet.Num() == 0)
		{
			return true;
		}

		if (!SplitSelectedLeaves(OutLeaves, SplitSet, Settings))
		{
			OutLeaves.Reset();
			return false;
		}
	}

	OutLeaves.Reset();
	return false;
}


bool FSVoxelOctreeSelector::BuildTransitionFaceMasks(
	const TArray<FSVoxelOctreeNode>& Leaves,
	int32 CellsPerAxis,
	int32 MaxLod,
	TArray<uint8>& OutFaceMasks,
	const FIntVector& RootOriginCells)
{
	OutFaceMasks.Reset();

	if (Leaves.Num() == 0 || CellsPerAxis <= 0)
	{
		return false;
	}

	MaxLod = FMath::Clamp(MaxLod, 0, 10);
	const int32 RootSpan = CellsPerAxis * (1 << MaxLod);

	TArray<FSVoxelOctreeNode> OrderedLeaves = Leaves;
	FSVoxelNodeLookup Lookup;
	BuildLookup(OrderedLeaves, Lookup);

	const FIntVector Directions[6] =
	{
		FIntVector(-1, 0, 0),
		FIntVector(1, 0, 0),
		FIntVector(0, -1, 0),
		FIntVector(0, 1, 0),
		FIntVector(0, 0, -1),
		FIntVector(0, 0, 1)
	};
	const uint8 FaceBits[6] =
	{
		SVoxelTransitionFace_NegativeX,
		SVoxelTransitionFace_PositiveX,
		SVoxelTransitionFace_NegativeY,
		SVoxelTransitionFace_PositiveY,
		SVoxelTransitionFace_NegativeZ,
		SVoxelTransitionFace_PositiveZ
	};

	OutFaceMasks.Reserve(Leaves.Num());

	for (const FSVoxelOctreeNode& Node : Leaves)
	{
		uint8 Mask = 0;
		const int32 Span = Node.GetSpanInBaseCells(CellsPerAxis);

		for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
		{
			const FIntVector& Direction = Directions[FaceIndex];
			const int32 TangentOffsets[2] = { Span / 4, (Span * 3) / 4 };
			bool bFoundFinerNeighbour = false;

			// Under 2:1 balance a coarse face may meet up to four finer leaves.
			// Probe the centre of each quadrant so a culled empty/solid neighbour
			// at the face centre cannot hide a transition needed elsewhere.
			for (int32 UIndex = 0; UIndex < 2 && !bFoundFinerNeighbour; ++UIndex)
			{
				for (int32 VIndex = 0; VIndex < 2 && !bFoundFinerNeighbour; ++VIndex)
				{
					FIntVector Probe = Node.OriginCells + FIntVector(
						Span / 2,
						Span / 2,
						Span / 2);

					if (FMath::Abs(Direction.X) > 0)
					{
						Probe.Y = Node.OriginCells.Y + TangentOffsets[UIndex];
						Probe.Z = Node.OriginCells.Z + TangentOffsets[VIndex];
						Probe.X = Direction.X < 0
							? Node.OriginCells.X - 1
							: Node.OriginCells.X + Span;
					}
					else if (FMath::Abs(Direction.Y) > 0)
					{
						Probe.X = Node.OriginCells.X + TangentOffsets[UIndex];
						Probe.Z = Node.OriginCells.Z + TangentOffsets[VIndex];
						Probe.Y = Direction.Y < 0
							? Node.OriginCells.Y - 1
							: Node.OriginCells.Y + Span;
					}
					else
					{
						Probe.X = Node.OriginCells.X + TangentOffsets[UIndex];
						Probe.Y = Node.OriginCells.Y + TangentOffsets[VIndex];
						Probe.Z = Direction.Z < 0
							? Node.OriginCells.Z - 1
							: Node.OriginCells.Z + Span;
					}

					FSVoxelOctreeNode Neighbour;
					if (FindLeafContainingCell(
						Probe,
						RootOriginCells,
						RootSpan,
						CellsPerAxis,
						MaxLod,
						Lookup,
						Neighbour) &&
						Neighbour.Lod < Node.Lod)
					{
						Mask |= FaceBits[FaceIndex];
						bFoundFinerNeighbour = true;
					}
				}
			}
		}

		OutFaceMasks.Add(Mask);
	}

	return OutFaceMasks.Num() == Leaves.Num();
}
