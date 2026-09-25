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

		// Bitwise determinism (Docs/architecture.md section 11, prior-art OQ-3 / ROB-10): precise floating
		// point, no fast-math, no FMA contraction for this module whatever the target default is.
		// VERIFY(UE 5.8): property and enum names (ModuleRules.FPSemantics / FPSemanticsMode.Precise exist
		// since UE 5.1). Second safeguard: every core .cpp includes rb/Core/FpGuard.h first.
		FPSemantics = FPSemanticsMode.Precise;

		// No RTTI, no exceptions (the core never throws); the standalone build checks the same flags.
		bUseRTTI = false;
		bEnableExceptions = false;

		// Public/ and Private/ are on the include path through bAddDefaultIncludePaths (default true): public
		// headers are included as "rb/...", private ones as "rb/Core/FpGuard.h" or relative ("SimInternal.h").
		bAddDefaultIncludePaths = true;

		PublicDependencyModuleNames.Add("Core");
	}
}
