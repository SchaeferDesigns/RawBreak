#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 3.8, 5.5.
#include "rb/Human/AiProfiles.h"

namespace rb::human
{
	AiProfile GetAiProfile(AiProfileId Id)
	{
		// TODO(WP-11): the 5.5 table (attributes, synthetic hand, habits, knowledge).
		AiProfile Profile;
		Profile.Id = Id;
		return Profile;
	}

	double CharacterAimBias(const AiCharacter& /*Character*/)
	{
		// TODO(WP-11): AimBiasMax (2 U01(HashKeys(CharacterSeed, 20, 1)) - 1).
		return 0.0;
	}

	IntendedStroke SyntheticHand(const PlannedStroke& Plan, const AiCharacter& /*Character*/, const StrokeSituation& /*Situation*/, double /*BallRadius*/,
		const NoiseKey& /*Key*/, const NoiseHistory& /*History*/, const HumanParams& /*Params*/)
	{
		// TODO(WP-11): 3.8 input flaws (channels 20-22 streak-guarded, 23-26 plain).
		IntendedStroke Stroke;
		Stroke.Azimuth = Plan.Azimuth;
		Stroke.Elevation = Plan.Elevation;
		Stroke.AxisOffsetA = Plan.AxisOffsetA;
		Stroke.AxisOffsetB = Plan.AxisOffsetB;
		Stroke.Speed = Plan.Speed;
		return Stroke;
	}
}
