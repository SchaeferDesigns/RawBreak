using UnrealBuildTool;

public class RawBreakTarget : TargetRules
{
	public RawBreakTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "RawBreak", "BilliardsCore" });
	}
}
