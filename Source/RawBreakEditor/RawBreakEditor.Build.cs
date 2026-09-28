using UnrealBuildTool;

// Editor-only tooling for the headless pipeline (Docs/ue-architecture.md 2, 9): C++ functions exposed to
// editor Python (UBlueprintFunctionLibrary) that bake the procedural FDynamicMesh3 geometry of table, balls
// and cue into UStaticMesh assets (Nanite, distance fields, Lumen cards) under /Game/Generated.
public class RawBreakEditor : ModuleRules
{
	public RawBreakEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RawBreak",
			"BilliardsCore",
			"GeometryCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetRegistry",
			"AssetTools",
			// FDynamicMesh3 -> FMeshDescription -> UStaticMesh
			"MeshConversion",
			"MeshDescription",
			"StaticMeshDescription",
			"GeometryFramework",
		});
	}
}
