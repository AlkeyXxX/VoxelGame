using UnrealBuildTool;

public class SmoothVoxel : ModuleRules
{
	public SmoothVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Legacy module headers (including VoxelWorld.h) currently live here.
		// Expose the module root so other project modules can include them.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"UMG",
				"Slate",
				"SlateCore",
				"ProceduralMeshComponent"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			});
	}
}
