using UnrealBuildTool;

// Maps the virtual shader directory /RawBreak to <Project>/Shaders so material Custom nodes can
// #include "/RawBreak/Private/RbBall.ush" (analytic ball decals, ball occlusion; ue-architecture 8.3).
// Must load at PostConfigInit (before any shader compiles), hence its own tiny module.
public class RawBreakShaders : ModuleRules
{
	public RawBreakShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore", "Projects" });
	}
}
