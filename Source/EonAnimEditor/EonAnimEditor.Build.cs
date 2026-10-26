using UnrealBuildTool;

public class EonAnimEditor : ModuleRules
{
	public EonAnimEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Eon"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "UnrealEd"
		});
	}
}
