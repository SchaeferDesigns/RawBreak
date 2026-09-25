#include "rb/Core/FpGuard.h"
// Owner: WP-3 (ball-ball & compliant islands). Spec: physics-collisions 3.6 (pressing), 3.9, 6.2 (rigid mode), 7.3;
// Docs/architecture.md 8.8 (sustained contacts, geometric contact order).
#include "rb/Physics/Compliant.h"

namespace rb
{
	double HertzContactTime(double /*ApproachSpeed*/, double /*ReducedMass*/, double /*HertzStiffness*/)
	{
		// TODO(WP-3): T_H = 3.2181 (m*^2 / (K^2 v))^(1/5) (3.9.3).
		return 0.0;
	}

	double TsujiDamping(double /*TsujiAlpha*/, double /*ReducedMass*/, double /*HertzStiffness*/)
	{
		// TODO(WP-3): eta = alpha_T sqrt(m* K).
		return 0.0;
	}

	double TsujiAlphaForRestitution(double /*Restitution*/)
	{
		// TODO(WP-3): tabulated alpha_T(e) for the island integrator (0.03689 at 0.95, 0.05242 at 0.93), checked by bisection.
		return 0.03689;
	}

	double CueTipContactStiffness(double /*ContactTime*/, double /*BallMass*/, double /*CueMass*/)
	{
		// TODO(WP-3): pi^2 m_eff / T^2, m_eff = m M / (m + M).
		return 0.0;
	}

	double IslandJoinDistance(double /*ApproachSpeed*/, double /*ReducedMass*/, const CliParams& /*Params*/, double ContactTol)
	{
		// TODO(WP-3): max(eps_touch, 1.2 v_n T_H(v_n)) (3.9.2).
		return ContactTol;
	}

	void CompliantIsland::Reset(double StartTime, CliMode Mode, const CliParams& Params, const BallBallParams& BallBall, const ClothParams& Cloth,
		double Gravity, const NumericsConfig& Numerics)
	{
		// TODO(WP-3): full reset; this stub only stores the configuration.
		CurrentTime = StartTime;
		CurrentMode = Mode;
		Settings = Params;
		ContactModel = BallBall;
		ClothSettings = Cloth;
		Tolerances = Numerics;
		GravityAccel = Gravity;
		Steps = 0;
		ZeroForceSteps = 0;
		Bodies.Clear();
		Features.Clear();
		Contacts.Clear();
		for (bool& Active : TipActive)
		{
			Active = false;
		}
	}

	bool CompliantIsland::AddBody(const IslandBody& NewBody)
	{
		// TODO(WP-3): reject duplicates; bodies are stored in insertion order (processing order is the geometric contact key).
		return Bodies.PushBack(NewBody);
	}

	bool CompliantIsland::AddFeature(const IslandFeature& NewFeature)
	{
		// TODO(WP-3): de-duplicate by (SourceKind, SourceIndex, SourceSub).
		return Features.PushBack(NewFeature);
	}

	bool CompliantIsland::RemoveBody(int Ball, IslandBody& Out)
	{
		// TODO(WP-3): also drop every contact state of the body.
		const int Index = FindBody(Ball);
		if (Index < 0)
		{
			return false;
		}
		Out = Bodies[Index];
		Bodies.RemoveAt(Index);
		return true;
	}

	bool CompliantIsland::SetTip(int Strike, const IslandTip& Tip)
	{
		if (Strike < 0 || Strike >= kMaxStrikes)
		{
			return false;
		}
		Tips[Strike] = Tip;
		TipActive[Strike] = true;
		return true;
	}

	bool CompliantIsland::RemoveTip(int Strike, CueTipPath& Out)
	{
		// TODO(WP-3): return the tip's current state as a new analytic path starting at Time().
		if (Strike < 0 || Strike >= kMaxStrikes || !TipActive[Strike])
		{
			return false;
		}
		Out = Tips[Strike].Path;
		TipActive[Strike] = false;
		return true;
	}

	bool CompliantIsland::HasTip(int Strike) const
	{
		return Strike >= 0 && Strike < kMaxStrikes && TipActive[Strike];
	}

	void CompliantIsland::SetMode(CliMode NewMode)
	{
		// TODO(WP-3): mode switch keeps bodies, features and contact states (frozen friction coefficients).
		CurrentMode = NewMode;
	}

	void CompliantIsland::Step(IslandRecordList& /*NewRecords*/)
	{
		// TODO(WP-3): one Hertz/Tsuji (Jacobi, geometric contact order) or rigid (Gauss-Seidel, same order) step:
		// forces, friction frozen at first touch, cloth / plane support, tips, torques, first-touch and tip records.
		CurrentTime += CurrentMode == CliMode::Rigid ? Settings.RigidTimeStep : Settings.TimeStep;
		++Steps;
	}

	bool CompliantIsland::SustainedContact() const
	{
		// TODO(WP-3): every active contact slower than SustainedSpeed for SustainedSteps compliant steps.
		return false;
	}

	bool CompliantIsland::CanExit() const
	{
		// TODO(WP-3): zero force for ExitZeroForceSteps steps AND all separating AND all gaps >= 0 AND no pressing re-trigger.
		return true;
	}

	bool CompliantIsland::BodyAtRest(int /*Index*/) const
	{
		// TODO(WP-3): |v| <= EpsV and |w| R <= EpsWTimesRadius.
		return false;
	}

	double CompliantIsland::GapToIsland(const Vec3& /*Position*/, double /*Radius*/) const
	{
		// TODO(WP-3): min over bodies of |p - p_i| - (R + R_i).
		return kInfinity;
	}

	int CompliantIsland::FindBody(int Ball) const
	{
		for (int i = 0; i < Bodies.Size(); ++i)
		{
			if (Bodies[i].Ball == Ball)
			{
				return i;
			}
		}
		return -1;
	}
}
