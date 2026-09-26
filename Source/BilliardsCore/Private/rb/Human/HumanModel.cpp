#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 3.1, 3.4-3.7, 3.9, 4.1-4.3 (after the shot). Oracle:
// Tools/reference/human-factors/stroke.py (+ recompute_v12.py for the v1.2 streak-guarded draws).
#include "rb/Human/HumanModel.h"

namespace rb::human
{
	ExecutedStroke ExecuteStroke(const IntendedStroke& /*Intended*/, const ShooterAttributes& /*Attributes*/, const StrokeSituation& /*Situation*/,
		const TipState& /*Tip*/, const CueBodyState& /*CueBody*/, const CueSpec& Cue, const BallSpec& /*CueBall*/, const Vec3& /*CueBallPosition*/,
		const BallObstacle* /*OtherBalls*/, int /*OtherBallCount*/, const NoiseKey& /*Key*/, const NoiseHistory& /*History*/, const HumanParams& /*Params*/,
		const TipParams& /*TipModel*/)
	{
		// TODO(WP-11): 3.5 executed stroke (watchable channels, streak-guarded per-shot draws, warp, pivot geometry, clamps)
		// and 3.6 tip contact, friction and flags (HF-T05..T11, HF-S04..S06).
		ExecutedStroke Result;
		Result.Strike.Cue = Cue;
		return Result;
	}

	SituationFactors ComputeSituationFactors(const IntendedStroke& /*Intended*/, const ShooterAttributes& /*Attributes*/, const StrokeSituation& /*Situation*/,
		const HumanParams& /*Params*/, double /*Time*/)
	{
		// TODO(WP-11): multipliers and sigmas of 3.4 / 3.5.
		return {};
	}

	double SettleInEnvelope(double /*Time*/, const HumanParams& /*Params*/)
	{
		// TODO(WP-11): E(t) (HF-07).
		return 1.0;
	}

	double SettleFactor(double /*Time*/, double /*SettleStart*/, const HumanParams& /*Params*/)
	{
		// TODO(WP-11): k_set(t) (HF-06).
		return 1.0;
	}

	HandPose SampleHand(const IntendedStroke& /*Intended*/, const ShooterAttributes& /*Attributes*/, const StrokeSituation& /*Situation*/,
		const CueBodyState& /*CueBody*/, const CueSpec& /*Cue*/, const BallSpec& /*CueBall*/, const NoiseKey& /*Key*/, const NoiseHistory& /*History*/,
		const HumanParams& /*Params*/, double Time)
	{
		// TODO(WP-11): pose at Time with the same functions as ExecuteStroke; per-shot ramp from t_fwd (3.7, HF-B08, HF-B13).
		HandPose Pose;
		Pose.Time = Time;
		return Pose;
	}

	DiagnosisStep DiagnosisStepAt(int /*Index*/)
	{
		// TODO(WP-11): the fixed order of 3.9 (equipment, table, hand drift, per-shot, human layer).
		return {};
	}

	void ApplyDiagnosisStep(const DiagnosisStep& /*Step*/, HumanParams& /*Human*/, TipState& /*Tip*/, CueBodyState& /*CueBody*/)
	{
		// TODO(WP-11): fresh chalk and no warp; channel mask (3.9).
	}

	void ApplyDiagnosisStep(const DiagnosisStep& /*Step*/, PhysicsParams& /*Physics*/, SimBall* /*Balls*/)
	{
		// TODO(WP-11): level table and clean balls (3.9 step 2).
	}

	StrokeShares ComputeStrokeShares(const ExecutedStroke& /*Stroke*/, const StrokeDelta& /*InputError*/, const CueSpec& /*Cue*/, const BallSpec& /*CueBall*/)
	{
		// TODO(WP-11): cue-ball direction error shares (3.9 stroke report).
		return {};
	}

	void ApplyShotToEquipment(const ExecutedStroke& /*Stroke*/, const ShotResult& /*Result*/, int /*StrikeIndex*/, const Quat& /*StruckBallOrientation*/,
		TipState& /*Tip*/, BallChalkMarks* /*Marks*/, const TipParams& /*TipModel*/, const MarkParams& /*MarkModel*/)
	{
		// TODO(WP-11): tip wear, chalk-mark deposit on the struck ball, fading of every ball's marks (4.1-4.3).
	}
}
