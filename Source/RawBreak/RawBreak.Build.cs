using UnrealBuildTool;

// RAW BREAK game module (Docs/ue-architecture.md section 2). Everything that turns the engine-agnostic
// BilliardsCore into a playable first-person game: table/ball/cue actors, the simulation service, the
// player, the match bridge to rb::rules, UI, replay and the headless dev tooling.
public class RawBreak : ModuleRules
{
	public RawBreak(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// The rb headers are C++20 (defaulted comparisons, constexpr math); UE 5.8 defaults to C++20 already,
		// pinned here so an engine default change cannot break the core headers.
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			// Enhanced Input: actions + mapping context are created at runtime in C++ (URbInputSetup), no assets.
			"EnhancedInput",
			// Engine-agnostic physics + rules + player model (rb::).
			"BilliardsCore",
			// Procedural meshes: FDynamicMesh3 (GeometryCore) built from rb::TableGeometry and shown with
			// UDynamicMeshComponent (GeometryFramework). Chosen over ProceduralMeshComponent because both are engine
			// modules (no plugin), carry normals/tangents/UV/material-id overlays and polygroups, give complex collision
			// for cue sweeps, and the SAME FDynamicMesh3 converts to MeshDescription -> UStaticMesh in the editor bake
			// (Nanite, distance fields, Lumen cards, HWRT) - PMC has no such path (ue-architecture 5.3).
			"GeometryCore",
			"GeometryFramework",
			// Physically based camera (sensor, focal length, aperture) for the Eyes/Headcam camera model (plan 4.x).
			"CinematicCamera",
			// Code-only UI (no widget assets): Slate overlay for the debug/info panel and the glance key.
			"Slate",
			"SlateCore",
			// Settings (quality presets later hook into UGameUserSettings + scalability groups).
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// IWindowsMessageHandler for timestamped raw mouse input (plan 5.4).
			"ApplicationCore",
			// FImageView for the headless capture (PNG writing via FImageUtils).
			"ImageCore",
			"RenderCore",
		});
	}
}
