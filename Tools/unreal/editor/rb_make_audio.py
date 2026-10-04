"""Generated audio assets (M2-C; Docs/ue-architecture.md 18.5, Docs/specs/audio.md 4.2, 6.4, 7.1, 8.5).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_audio.py

Creates under /Game/Generated/Audio (RbAssetPaths::AudioDir; names are the contract of Source/RawBreak/Public/Audio/RbAudioAssets.h):
  Mix/SUBM_RB_Master             the root of the game's mix (parent: the engine's main submix) with the true-peak safety limiter
                                 DYN_RB_MasterLimiter (-1 dBFS, 5 ms lookahead; audio.md 4.2: it acts by <= 1 dB in the renders)
  Mix/SUBM_RB_World              diegetic sound; the pause mix puts FLT_RB_PauseLowPass (800 Hz) on it as an effect-chain override
  Mix/SUBM_RB_{Table,Foley,Ambience,Crowd,Voice,Jukebox}   children of World (the volume sliders drive their output volumes)
  Mix/SUBM_RB_Reverb_<Venue>     child of World: the convolution reverb CRV_RB_<Venue> (wet only) with the IR below
  Mix/SUBM_RB_{UI,MenuMusic}     children of Master (non-diegetic, no reverb, no pause filter)
  IR/IR_RB_<Venue>               AudioImpulseResponse from Tools/audio/out/ref/IR_RB_<Venue>.wav (Tools/audio/ir_synth.py: image
                                 sources + Sabine tail; dive bar RT60 0.8 / 0.6 / 0.5 s), normalisation 0 dB (the IR carries the
                                 room's diffuse-field level for a unit source at 1 m)
No sound waves: every M2 sound is synthesised at runtime (RawBreakAudioDsp). Idempotent (existing assets are updated in place,
so references between them stay valid). Prints a report and fails (RBUE_FAIL) when an asset cannot be made. Owner: M2-C.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

AUDIO_DIR = "/Game/Generated/Audio"
MIX_DIR = AUDIO_DIR + "/Mix"
IR_DIR = AUDIO_DIR + "/IR"
VENUES = ("TestRoom", "DiveBar")
WORLD_CHILDREN = ("Table", "Foley", "Ambience", "Crowd", "Voice", "Jukebox")
MASTER_CHILDREN = ("UI", "MenuMusic")
REF_DIR = os.path.join(unreal.Paths.project_dir(), "Tools", "audio", "out", "ref")


def tools():
	return unreal.AssetToolsHelpers.get_asset_tools()


def get_or_create(name: str, path: str, cls, factory):
	full = f"{path}/{name}"
	if unreal.EditorAssetLibrary.does_asset_exist(full):
		asset = unreal.EditorAssetLibrary.load_asset(full)
		if asset is not None and isinstance(asset, cls):
			return asset
		rb.delete_asset_if_exists(full)
	asset = tools().create_asset(name, path, cls, factory)
	if asset is None:
		rb.fail(f"could not create {full} ({cls.__name__})")
	return asset


def effect_preset(name: str, cls):
	factory = unreal.SoundSubmixEffectFactory()
	for prop in ("sound_effect_submixepreset_class", "sound_effect_submix_preset_class", "sound_effect_preset_class"):
		try:
			factory.set_editor_property(prop, cls)
			break
		except Exception:  # noqa: BLE001 - property name differs between versions
			continue
	return get_or_create(name, MIX_DIR, cls, factory)


def submix(name: str):
	return get_or_create(f"SUBM_RB_{name}", MIX_DIR, unreal.SoundSubmix, unreal.SoundSubmixFactory())


def set_enum(settings, prop: str, enum_cls_names, value_names):
	for cls_name in enum_cls_names:
		enum_cls = getattr(unreal, cls_name, None)
		if enum_cls is None:
			continue
		for v in value_names:
			if hasattr(enum_cls, v):
				settings.set_editor_property(prop, getattr(enum_cls, v))
				return True
	rb.log(f"note: could not set {prop} (enums {enum_cls_names} / {value_names})")
	return False


def apply_settings(preset, settings, checks):
	"""Stores the settings struct in the preset asset. SetSettings (BlueprintCallable) only updates the running effect instances; the
	asset keeps its Settings UPROPERTY, so it is written directly as well and read back (checks: property -> expected value)."""
	preset.set_settings(settings)
	preset.set_editor_property("settings", settings)
	stored = preset.get_editor_property("settings")
	for prop, want in checks.items():
		got = stored.get_editor_property(prop)
		ok = abs(got - want) < 1e-4 if isinstance(want, float) else got == want
		if not ok:
			rb.fail(f"{preset.get_name()}: {prop} = {got}, want {want}")
	rb.log(f"{preset.get_name()}: " + ", ".join(f"{k} {stored.get_editor_property(k)}" for k in checks))


def make_limiter():
	preset = effect_preset("DYN_RB_MasterLimiter", unreal.SubmixEffectDynamicsProcessorPreset)
	s = unreal.SubmixEffectDynamicsProcessorSettings()
	set_enum(s, "dynamics_processor_type", ["SubmixEffectDynamicsProcessorType"], ["LIMITER", "Limiter"])
	set_enum(s, "peak_mode", ["SubmixEffectDynamicsPeakMode"], ["PEAK", "Peak"])
	set_enum(s, "link_mode", ["SubmixEffectDynamicsChannelLinkMode"], ["PEAK", "Peak"])
	s.set_editor_property("threshold_db", -1.0)
	s.set_editor_property("look_ahead_msec", 5.0)
	s.set_editor_property("attack_time_msec", 1.0)
	s.set_editor_property("release_time_msec", 120.0)
	s.set_editor_property("knee_bandwidth_db", 0.0)
	s.set_editor_property("input_gain_db", 0.0)
	s.set_editor_property("output_gain_db", 0.0)
	s.set_editor_property("analog_mode", False)
	apply_settings(preset, s, {"dynamics_processor_type": unreal.SubmixEffectDynamicsProcessorType.LIMITER, "threshold_db": -1.0,
		"look_ahead_msec": 5.0, "attack_time_msec": 1.0, "release_time_msec": 120.0, "analog_mode": False})
	return preset


def make_pause_lowpass():
	preset = effect_preset("FLT_RB_PauseLowPass", unreal.SubmixEffectFilterPreset)
	s = unreal.SubmixEffectFilterSettings()
	set_enum(s, "filter_type", ["SubmixFilterType"], ["LOW_PASS", "LowPass"])
	set_enum(s, "filter_algorithm", ["SubmixFilterAlgorithm"], ["TWO_POLE", "TwoPole", "STATE_VARIABLE"])
	s.set_editor_property("filter_frequency", 800.0)
	s.set_editor_property("filter_q", 0.707)
	apply_settings(preset, s, {"filter_type": unreal.SubmixFilterType.LOW_PASS, "filter_frequency": 800.0, "filter_q": 0.707})
	return preset


def make_ir(venue: str):
	wav = os.path.join(REF_DIR, f"IR_RB_{venue}.wav")
	if not os.path.exists(wav):
		rb.fail(f"missing {wav} (run Tools/audio/ir_synth.py --ref)")
	# UE Python drops a bool return value of a function with out parameters: None on false, else the out parameters.
	result = unreal.RbAudioAssetTools.read_wav_file(wav)
	if not result:
		rb.fail(f"could not read {wav}")
	if len(result) == 4:
		result = result[1:]
	samples, channels, rate = result
	if channels <= 0:
		rb.fail(f"could not read {wav}")
	ir = get_or_create(f"IR_RB_{venue}", IR_DIR, unreal.AudioImpulseResponse, unreal.AudioImpulseResponseFactory())
	if not unreal.RbAudioAssetTools.set_impulse_response_data(ir, samples, channels, rate, 0.0):
		rb.fail(f"could not fill IR_RB_{venue}")
	n, ch, sr = unreal.RbAudioAssetTools.get_impulse_response_num_samples(ir)
	rb.log(f"IR_RB_{venue}: {n // max(ch, 1)} frames x {ch} channels at {sr} Hz from {os.path.basename(wav)}")
	return ir


def make_convolution(venue: str, ir):
	preset = effect_preset(f"CRV_RB_{venue}", unreal.SubmixEffectConvolutionReverbPreset)
	s = unreal.SubmixEffectConvolutionReverbSettings()
	s.set_editor_property("wet_volume_db", 0.0)
	s.set_editor_property("dry_volume_db", -96.0)
	s.set_editor_property("bypass", False)
	s.set_editor_property("mix_input_channel_format_to_impulse_response_format", True)
	s.set_editor_property("mix_reverb_output_to_output_channel_format", True)
	apply_settings(preset, s, {"wet_volume_db": 0.0, "dry_volume_db": -96.0, "bypass": False})
	preset.set_editor_property("impulse_response", ir)  # runs the BlueprintSetter SetImpulseResponse
	return preset


def link(child, parent):
	unreal.RbAudioAssetTools.link_submix(child, parent)


def main() -> None:
	rb.ensure_dir(AUDIO_DIR)
	rb.ensure_dir(MIX_DIR)
	rb.ensure_dir(IR_DIR)
	limiter = make_limiter()
	make_pause_lowpass()

	master = submix("Master")
	link(master, None)
	master.set_editor_property("submix_effect_chain", [limiter])
	master.set_editor_property("auto_disable", False)
	world = submix("World")
	link(world, master)
	world.set_editor_property("auto_disable", False)
	for name in WORLD_CHILDREN:
		child = submix(name)
		link(child, world)
	for name in MASTER_CHILDREN:
		child = submix(name)
		link(child, master)
	for venue in VENUES:
		ir = make_ir(venue)
		conv = make_convolution(venue, ir)
		reverb = submix(f"Reverb_{venue}")
		link(reverb, world)
		reverb.set_editor_property("submix_effect_chain", [conv])

	saved = unreal.EditorAssetLibrary.save_directory(AUDIO_DIR, only_if_is_dirty=False, recursive=True)
	if not saved:
		rb.fail("could not save /Game/Generated/Audio")
	# Report (the rb_make_all --compare metrics): the submix tree and the effect chains.
	for name in ("Master", "World") + WORLD_CHILDREN + MASTER_CHILDREN + tuple(f"Reverb_{v}" for v in VENUES):
		s = unreal.EditorAssetLibrary.load_asset(f"{MIX_DIR}/SUBM_RB_{name}")
		parent = s.get_editor_property("parent_submix")
		chain = [p.get_name() for p in s.get_editor_property("submix_effect_chain")]
		rb.log(f"SUBM_RB_{name}: parent {parent.get_name() if parent else '(main submix)'}, effects {chain}")
	rb.log("rb_make_audio: OK")


main()
