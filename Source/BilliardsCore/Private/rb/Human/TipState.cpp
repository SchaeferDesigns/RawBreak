#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.1, 4.2 (HF-T12, HF-T13, HF-T14, HF-B04). Oracle:
// Tools/reference/human-factors/chalk.py, equil.py.
#include "rb/Human/TipState.h"

#include "rb/Core/Constants.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"

namespace rb::human
{
	namespace
	{
		double HardnessRetention(TipHardness Hardness, const TipParams& Params)
		{
			switch (Hardness)
			{
			case TipHardness::Soft: return Params.RetentionSoft;
			case TipHardness::Hard: return Params.RetentionHard;
			case TipHardness::Medium: break;
			}
			return 1.0;
		}

		double GlazeHits(TipHardness Hardness, const TipParams& Params)
		{
			switch (Hardness)
			{
			case TipHardness::Soft: return Params.GlazeHitsSoft;
			case TipHardness::Hard: return Params.GlazeHitsHard;
			case TipHardness::Medium: break;
			}
			return Params.GlazeHitsMedium;
		}

		double OverhangFactor(TipHardness Hardness, const TipParams& Params)
		{
			switch (Hardness)
			{
			case TipHardness::Soft: return Params.OverhangSoft;
			case TipHardness::Hard: return Params.OverhangHard;
			case TipHardness::Medium: break;
			}
			return 1.0;
		}
	}

	TipContactPoint LookupTipContact(const TipState& Tip, double OffsetA, double OffsetB, const TipParams& Params)
	{
		TipContactPoint Contact;
		const double Rho = Sqrt(OffsetA * OffsetA + OffsetB * OffsetB);
		const double HalfWidth = 0.5 * Tip.Width;
		Contact.Q = HalfWidth > 0.0 ? Tip.DomeRadius * Rho / HalfWidth : kInfinity;
		Contact.Beta = Atan2(-OffsetB, -OffsetA);
		const double OverhangLimit = HalfWidth > 0.0 ? 1.0 + Max(0.0, Tip.Overhang) / HalfWidth : 1.0;
		Contact.Zone = Contact.Q <= 1.0 ? TipZone::Dome : (Contact.Q <= OverhangLimit ? TipZone::Overhang : TipZone::Ferrule);

		// 4.1 lookup weights: centre disc blended into the two nearest ring sectors.
		const double RingWeight = SmoothStep01((Contact.Q - 0.2) / 0.3);
		double S = Contact.Beta / (kPi / 3.0);
		S = S - 6.0 * Floor(S / 6.0);
		int K0 = static_cast<int>(Floor(S));
		K0 = K0 < 0 ? 0 : (K0 > 5 ? 5 : K0); // S in [0, 6); guards the rounding S == 6
		const double F = S - static_cast<double>(K0);
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			Contact.Weights[z] = 0.0;
		}
		Contact.Weights[0] = 1.0 - RingWeight;
		Contact.Weights[1 + K0] += RingWeight * (1.0 - F);
		Contact.Weights[1 + (K0 + 1) % 6] += RingWeight * F;

		double Coverage = 0.0;
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			Coverage += Contact.Weights[z] * Tip.Coverage[z];
		}
		Contact.Coverage = Coverage;
		switch (Contact.Zone)
		{
		case TipZone::Dome: Contact.Friction = Params.BareFriction + (Params.FreshFriction - Params.BareFriction) * Coverage; break;
		case TipZone::Overhang: Contact.Friction = Params.RimFriction; break;
		case TipZone::Ferrule: Contact.Friction = Params.FerruleFriction; break;
		}
		return Contact;
	}

	double HitSeverity(double Speed, double Rho, bool Miscue)
	{
		return (0.5 + 0.25 * Max(0.0, Speed)) * (0.6 + 2.0 * (Rho * Rho)) * (Miscue ? 3.0 : 1.0);
	}

	void ApplyTipWear(TipState& Tip, const TipContactPoint& Contact, double Severity, const TipParams& Params)
	{
		const double Sev = Max(0.0, Severity);
		const double Glaze = Clamp(Tip.Glaze, 0.0, 1.0);
		const double HitsPerGrade = ChalkGradeSpecFor(Tip.Chalk).HitsPerGrade * HardnessRetention(Tip.Hardness, Params) *
			(1.0 - Params.GlazeRetentionLoss * Glaze);
		if (HitsPerGrade > 0.0)
		{
			for (int z = 0; z < kTipZoneCount; ++z)
			{
				Tip.Coverage[z] *= Exp(-(Contact.Weights[z] * Sev) / HitsPerGrade);
			}
		}
		if (Params.ConditionWear)
		{
			Tip.DomeRadius = Tip.DomeRadius < Params.MaxDomeRadius ? Min(Params.MaxDomeRadius, Tip.DomeRadius + Params.DomeGrowthPerHit * Sev) : Tip.DomeRadius;
			const double NGlaze = GlazeHits(Tip.Hardness, Params);
			if (NGlaze > 0.0)
			{
				Tip.Glaze = Min(1.0, Glaze + (1.0 - Glaze) * Sev / NGlaze);
			}
			Tip.Overhang += Params.OverhangPerHit * Sev * OverhangFactor(Tip.Hardness, Params);
		}
		++Tip.Hits;
		if (Tip.BreakInHits > 0u)
		{
			--Tip.BreakInHits;
			if (Tip.BreakInHits == 0u)
			{
				Tip.Glaze = Max(0.0, (Tip.Glaze - kNewTipGlaze) / (1.0 - kNewTipGlaze));
			}
		}
	}

	double ChalkCap(const ChalkCube& Cube, const TipParams& Params)
	{
		const double GradeCap = ChalkGradeSpecFor(Cube.Grade).Cap;
		return Cube.BarCube ? Min(GradeCap, Params.BarCubeCap) : GradeCap;
	}

	void ApplyChalkTwist(TipState& Tip, const ChalkCube& Cube, double Sweep, const TipParams& Params)
	{
		const double H = Clamp(Sweep, 0.0, 1.0); // a ritual never beats the habit-1 result (principle 5)
		const double Glaze = Clamp(Tip.Glaze, 0.0, 1.0);
		const double Hollow = Clamp(Cube.Hollow, 0.0, 1.0);
		const double Cap = ChalkCap(Cube, Params);
		const double EtaCenter = Params.CenterTwistGain * (1.0 - Params.GlazeRetentionLoss * Glaze);
		const double EtaRing = (Params.RingTwistDrill + Params.RingTwistSweep * H) * (1.0 - Params.GlazeRetentionLoss * Glaze) * (1.0 - Params.HollowLoss * Hollow);
		Tip.Coverage[0] = Tip.Coverage[0] + Max(0.0, Cap - Tip.Coverage[0]) * EtaCenter;
		for (int z = 1; z < kTipZoneCount; ++z)
		{
			Tip.Coverage[z] = Tip.Coverage[z] + Max(0.0, Cap - Tip.Coverage[z]) * EtaRing;
		}
		Tip.Chalk = Cube.Grade;
	}

	int AutoChalkTwists(const TipState& Tip, const ChalkCube& Cube, const TipParams& Params)
	{
		double MinCoverage = Tip.Coverage[0];
		for (int z = 1; z < kTipZoneCount; ++z)
		{
			MinCoverage = Min(MinCoverage, Tip.Coverage[z]);
		}
		const double Missing = ChalkCap(Cube, Params) - MinCoverage;
		if (!(Missing > 0.0) || !(Params.TwistCoverageStep > 0.0))
		{
			return 0;
		}
		// ceil with a 1e-12 guard, so an exact multiple of the step (0.45 / 0.15 = 3.0000000000000004) is not rounded up.
		const double Twists = -Floor(-(Missing / Params.TwistCoverageStep - 1e-12));
		return Twists < 1.0 ? 1 : static_cast<int>(Twists);
	}

	double TwistDuration(double ChalkHabit, const TipParams& Params)
	{
		return Params.TwistSeconds * (1.0 - Params.TwistHabitSpeedup * Clamp(ChalkHabit, 0.0, 1.0));
	}

	void ScuffTip(TipState& Tip)
	{
		const TipParams Params;
		Tip.Glaze *= Params.ScuffGlazeFactor;
		Tip.Height = Max(0.0, Tip.Height - Params.ScuffHeightLoss);
	}

	void ShapeTip(TipState& Tip, double TargetDomeRadius)
	{
		const TipParams Params;
		if (TargetDomeRadius > 0.0)
		{
			Tip.DomeRadius = TargetDomeRadius;
		}
		Tip.Height = Max(0.0, Tip.Height - Params.ShapeHeightLoss);
	}

	void TrimTip(TipState& Tip)
	{
		Tip.Overhang = 0.0;
	}

	TipState Retip(TipHardness Hardness, double DomeRadius, double Width)
	{
		TipState Tip;
		for (int z = 0; z < kTipZoneCount; ++z)
		{
			Tip.Coverage[z] = 0.0;
		}
		Tip.Hardness = Hardness;
		Tip.DomeRadius = DomeRadius;
		Tip.Width = Width;
		Tip.Height = kNewTipHeight;
		Tip.Glaze = kNewTipGlaze;
		Tip.Overhang = 0.0;
		Tip.Loose = false;
		Tip.Hits = 0;
		Tip.BreakInHits = kTipBreakInHits;
		return Tip;
	}
}
