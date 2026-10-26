using UnrealBuildTool;

public class Eon : ModuleRules
{
	public Eon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"AnimGraphRuntime", "AnimationCore"
		});
	}
}
