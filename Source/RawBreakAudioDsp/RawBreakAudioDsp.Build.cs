using UnrealBuildTool;

// Physics-driven table audio DSP (Docs/specs/audio.md 3, 5, 8.1; Docs/ue-architecture.md 18.5): contact pulse shapes, ball
// radiation kernels, modal banks of rails / bed / pockets / cue, rolling noise, the per-voice impact renderer, the per-table
// shot audio clock (sample-accurate scheduling) and the plan-time presentation envelope. NO UObjects and no audio device:
// everything is unit-testable offline (RawBreak.Unit.Audio.*, golden vectors of Tools/audio/out/ref). Uses only UE Core
// (containers, math, atomics). The C++ port of Tools/audio/click_synth.py (runtime_render). Owner: M2-C.
public class RawBreakAudioDsp : ModuleRules
{
	public RawBreakAudioDsp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});
	}
}
