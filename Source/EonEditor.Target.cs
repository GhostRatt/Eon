using UnrealBuildTool;

public class EonEditorTarget : TargetRules
{
	public EonEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Eon", "EonAnimEditor" });
	}
}
