using UnrealBuildTool;

// Engine-agnostic pool physics + rules core. The sources under Public/rb and
// Private/rb must not include any Unreal header: they are also compiled
// standalone via the root CMakeLists.txt for unit tests and the rbsim tool.
public class BilliardsCore : ModuleRules
{
	public BilliardsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bUseUnity = false;

		PublicDependencyModuleNames.Add("Core");
	}
}
