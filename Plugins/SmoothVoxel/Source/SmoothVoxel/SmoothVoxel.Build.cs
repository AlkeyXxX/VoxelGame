using UnrealBuildTool;

public class SmoothVoxel : ModuleRules
{
	public SmoothVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Заголовки SmoothVoxel лежат прямо в корне модуля.
		// Делаем этот каталог видимым для зависимых модулей.
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
