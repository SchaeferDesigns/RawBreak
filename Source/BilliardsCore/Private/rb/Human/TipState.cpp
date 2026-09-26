#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.1, 4.2 (HF-T12, HF-T13, HF-T14, HF-B04). Oracle:
// Tools/reference/human-factors/chalk.py, equil.py.
#include "rb/Human/TipState.h"

namespace rb::human
{
	TipContactPoint LookupTipContact(const TipState& /*Tip*/, double /*OffsetA*/, double /*OffsetB*/, const TipParams& Params)
	{
		// TODO(WP-11): q, beta, zone, weights and mu (4.1 lookup, 3.6 zones).
		TipContactPoint Contact;
		Contact.Friction = Params.FreshFriction;
		return Contact;
	}

	double HitSeverity(double /*Speed*/, double /*Rho*/, bool /*Miscue*/)
	{
		// TODO(WP-11): (0.5 + 0.25 V) (0.6 + 2 rho^2) (Miscue ? 3 : 1).
		return 0.0;
	}

	void ApplyTipWear(TipState& /*Tip*/, const TipContactPoint& /*Contact*/, double /*Severity*/, const TipParams& /*Params*/)
	{
		// TODO(WP-11): coverage decay per zone weight, dome growth, glaze, overhang, hit count (4.1, 4.2).
	}

	double ChalkCap(const ChalkCube& /*Cube*/, const TipParams& /*Params*/)
	{
		// TODO(WP-11): grade cap; 0.7 for a bar cube.
		return 1.0;
	}

	void ApplyChalkTwist(TipState& /*Tip*/, const ChalkCube& /*Cube*/, double /*Sweep*/, const TipParams& /*Params*/)
	{
		// TODO(WP-11): the revolver rule (4.1, HF-T13).
	}

	int AutoChalkTwists(const TipState& /*Tip*/, const ChalkCube& /*Cube*/, const TipParams& /*Params*/)
	{
		// TODO(WP-11): ceil((cap - min c_z) / 0.15), 0 when nothing is missing.
		return 0;
	}

	double TwistDuration(double /*ChalkHabit*/, const TipParams& /*Params*/)
	{
		// TODO(WP-11): 0.4 (1 - 0.3 H_chalk) s.
		return 0.0;
	}

	void ScuffTip(TipState& /*Tip*/)
	{
		// TODO(WP-11): G *= 0.2, height -= 0.02 mm (4.2).
	}

	void ShapeTip(TipState& /*Tip*/, double /*TargetDomeRadius*/)
	{
		// TODO(WP-11): r_dome = target, height -= 0.1 mm (4.2).
	}

	void TrimTip(TipState& /*Tip*/)
	{
		// TODO(WP-11): overhang = 0 (4.2).
	}

	TipState Retip(TipHardness Hardness, double DomeRadius, double Width)
	{
		// TODO(WP-11): new tip, height 6 mm, glaze 0.3 until 50 hits (4.2).
		TipState Tip;
		Tip.Hardness = Hardness;
		Tip.DomeRadius = DomeRadius;
		Tip.Width = Width;
		return Tip;
	}
}
