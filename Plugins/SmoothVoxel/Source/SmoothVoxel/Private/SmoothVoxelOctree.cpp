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
		int32 RootSpan,
		int32 CellsPerAxis,
		int32 MaxLod,
		const FSVoxelNodeLookup& Lookup,
		FSVoxelOctreeNode& OutNode)
	{
		if (Cell.X < 0 || Cell.Y < 0 || Cell.Z < 0 ||
			Cell.X >= RootSpan || Cell.Y >= RootSpan || Cell.Z >= RootSpan)
		{
			return false;
		}

		// Leaves are aligned to their own span, so checking each LOD from fine
		// to coarse gives a deterministic point-to-leaf lookup.
		for (int32 Lod = 0; Lod <= MaxLod; ++Lod)
		{
			const int32 Span = CellsPerAxis * (1 << Lod);
			const FIntVector Origin(
				(Cell.X / Span) * Span,
				(Cell.Y / Span) * Span,
				(Cell.Z / Span) * Span);

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
				// Probe the centre cell immediately across the given face.
				FIntVector Probe = Node.OriginCells + FIntVector(
					Span / 2,
					Span / 2,
					Span / 2);

				if (Direction.X < 0)
				{
					Probe.X = Node.OriginCells.X - 1;
				}
				else if (Direction.X > 0)
				{
					Probe.X = Node.OriginCells.X + Span;
				}
				else if (Direction.Y < 0)
				{
					Probe.Y = Node.OriginCells.Y - 1;
				}
				else if (Direction.Y > 0)
				{
					Probe.Y = Node.OriginCells.Y + Span;
				}
				else if (Direction.Z < 0)
				{
					Probe.Z = Node.OriginCells.Z - 1;
				}
				else
				{
					Probe.Z = Node.OriginCells.Z + Span;
				}

				FSVoxelOctreeNode Neighbour;
				if (!FindLeafContainingCell(
					Probe,
					RootSpan,
					Settings.CellsPerAxis,
					Settings.MaxLod,
					Lookup,
					Neighbour))
				{
					continue;
				}

				if (Neighbour.Lod > Node.Lod + 1)
				{
					OutSplitSet.Add(Neighbour);
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
		FSVoxelOctreeNode(FIntVector::ZeroValue, Settings.MaxLod),
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
