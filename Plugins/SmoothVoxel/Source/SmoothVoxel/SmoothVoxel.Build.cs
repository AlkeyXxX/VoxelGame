using UnrealBuildTool;

public class SmoothVoxel : ModuleRules
{
	public SmoothVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
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
