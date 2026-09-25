using UnrealBuildTool;

public class RawBreakEditorTarget : TargetRules
{
	public RawBreakEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "RawBreak", "BilliardsCore" });
	}
}
