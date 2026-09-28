// Owner: WP-10 (validation & benchmarks). prior-art 9.7 SYS-01 ... SYS-06 (Tier D): the Corner-5 and Plus diamond systems and the
// slow one-rail kick on the 9-ft table (prior-art 6.9, 6.8), through the whole simulator with the table's own cushions
// (MakePhysicsParams: Mathavan 2010, the e_c law, k_f) and the VAL cloth (mu_s 0.2, mu_r 0.01, alpha_sp 10, g 9.81).
//
// Measurement (INTERPRETATION, as the systems are taught): the cue ball rolls (no slip) along the aim line through the two
// system points with running English; after each rail it slides, then rolls straight again - that post-slip rolling line,
// extended to a diamond line, is where "the track crosses the diamond line". Diamond numbers follow prior-art 6.9 (Delta = L/8,
// s = 93.66 mm behind the nose): Corner-5 D = 5 at the head-right corner point, D = 5 - (x + L/2) / (2 Delta) along the RAIL_RIGHT
// diamond line, D = 5 + (y + W/2) / Delta along RAIL_HEAD, F and T = (L/2 - x) / Delta on RAIL_LEFT / RAIL_RIGHT; Plus S = 1 +
// 2 (W/2 - y) / Delta on RAIL_FOOT, P and the result in diamonds from the foot end on RAIL_RIGHT. The cue ball starts on the aim
// line 0.35 m (along the line) inside the nose line it enters through; "English b_s" is the side spin a strike with contact offset
// b_s gives (R w_z = (5/2) (b_s / R) v, MOT B.6), running for the path. The speed is the rolling speed at the first rail.

#include "Validation/ValidationUtil.h"

#include "rb/Geometry/TableGeometry.h"

using namespace rb;
using simtest::kR;

namespace
{
	const val::Diamonds kD{};

	struct Rails
	{
		static bool Right(int Cushion) { return Cushion == 0 || Cushion == 1; }
		static bool Foot(int Cushion) { return Cushion == 2; }
		static bool Left(int Cushion) { return Cushion == 3 || Cushion == 4; }
		static bool Head(int Cushion) { return Cushion == 5; }
	};

	struct Track
	{
		bool Ok = false;
		int Rails = 0;           // cushion (nose) contacts of the cue ball
		int RailOf[8] = {};      // CushionId of each
		Vec2 LinePoint[8];       // a point of the rolling line after rail k
		Vec2 LineDirection[8];   // its direction (zero: never rolled again, or stopped)
		double SpeedAtFirstRail = 0.0;
		bool Pocketed = false;
		int Pocket = -1;
	};

	// The cue ball's rails and its post-slip rolling line after each (SYS measurement), from the event log.
	Track Follow(const ShotResult& R)
	{
		Track T;
		T.Ok = R.Status == SimStatus::Ok;
		for (std::size_t k = 0; k < R.Events.size(); ++k)
		{
			const ShotEvent& E = R.Events[k];
			if (E.A != 0)
			{
				continue;
			}
			if (E.Type == ShotEventType::BallPocketed)
			{
				T.Pocketed = true;
				T.Pocket = E.Feature;
			}
			if (E.Type != ShotEventType::BallCushion || T.Rails >= 8)
			{
				continue;
			}
			if (T.Rails == 0)
			{
				T.SpeedAtFirstRail = val::PlanSpeed(E.Pre[0].Velocity);
			}
			const int Index = T.Rails++;
			T.RailOf[Index] = E.Feature;
			// The rolling line: the first Sliding -> Rolling transition after this rail, before the next contact of the ball.
			T.LinePoint[Index] = XY(E.Post[0].Position);
			T.LineDirection[Index] = XY(E.Post[0].Velocity);
			for (std::size_t j = k + 1; j < R.Events.size(); ++j)
			{
				const ShotEvent& F = R.Events[j];
				if (F.A != 0 && F.B != 0)
				{
					continue;
				}
				if (F.Type == ShotEventType::MotionTransition && F.A == 0 && F.To == MotionState::Rolling)
				{
					T.LinePoint[Index] = XY(F.Post[0].Position);
					T.LineDirection[Index] = XY(F.Post[0].Velocity);
					break;
				}
				if (F.Type == ShotEventType::BallCushion || F.Type == ShotEventType::BallJaw || F.Type == ShotEventType::BallBall ||
					F.Type == ShotEventType::BallPocketEnter)
				{
					break;
				}
			}
		}
		return T;
	}

	// The cue ball rolling from the point 0.35 m inside the entry nose line along the line From -> To (plan points on diamond
	// lines), arriving at the first rail at SpeedAtRail with English Bs (fraction of R, + = running for a path that turns
	// clockwise seen from above ... the caller passes the sign), simulated to rest.
	Track Shoot(const Vec2& From, const Vec2& To, double SpeedAtRail, double EnglishSign, double Bs, Vec2* Start = nullptr)
	{
		const TableGeometry& T = simtest::NineFoot();
		const PhysicsParams P = val::TableParams(kTableNineFootPro);
		SimInput& In = val::Fresh(T, P);
		In.Record.LogTransitions = true;
		const Vec2 D = Normalized(To - From);
		// Entry: where the line crosses the first nose line (|y| = W/2 or |x| = L/2) coming from the diamond line.
		const double Ty = Abs(D.y) > 0.0 ? (Abs(From.y) - kD.HalfWidth) / Abs(D.y) : kInfinity;
		const double Tx = Abs(D.x) > 0.0 ? (Abs(From.x) - kD.HalfLength) / Abs(D.x) : kInfinity;
		const double Enter = Abs(From.y) > kD.HalfWidth && Abs(From.x) > kD.HalfLength ? Max(Tx, Ty) : (Abs(From.y) > kD.HalfWidth ? Ty : Tx);
		const Vec2 P0 = From + D * (Enter + 0.35);
		// Distance to the first rail contact (the nose contact line of the rail the line reaches first beyond P0).
		const double Rc = ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
		double ToRail = kInfinity;
		if (Abs(D.y) > 0.0)
		{
			ToRail = Min(ToRail, ((D.y > 0.0 ? kD.HalfWidth : -kD.HalfWidth) - (D.y > 0.0 ? Rc : -Rc) - P0.y) / D.y);
		}
		if (Abs(D.x) > 0.0)
		{
			ToRail = Min(ToRail, ((D.x > 0.0 ? kD.HalfLength : -kD.HalfLength) - (D.x > 0.0 ? Rc : -Rc) - P0.x) / D.x);
		}
		const double V0 = val::RollingStartSpeed(SpeedAtRail, ToRail, P);
		const double Wz = EnglishSign * 2.5 * V0 * Bs / kR; // R w_z = (5/2)(b_s / R) v with b_s = Bs R
		val::PlaceRollingSpin(In, 0, ToVec3(P0, kR), ToVec3(D * V0), Wz, MakeBallSpec(kR, kDefaultBallMass));
		if (Start != nullptr)
		{
			*Start = P0;
		}
		static ShotResult R;
		Simulator Sim;
		Sim.Run(In, R);
		return Follow(R);
	}

	Vec2 CornerFiveD(double DValue) // on the RAIL_RIGHT diamond line (<= 5) or RAIL_HEAD diamond line (>= 5)
	{
		if (DValue == 5.0)
		{
			return {-kD.EndLineX(), -kD.LongLineY()};
		}
		if (DValue < 5.0)
		{
			return {-kD.HalfLength + 2.0 * (5.0 - DValue) * kD.Spacing, -kD.LongLineY()};
		}
		return {-kD.EndLineX(), -kD.HalfWidth + (DValue - 5.0) * kD.Spacing};
	}

	Vec2 CornerFiveF(double F) { return {kD.HalfLength - F * kD.Spacing, kD.LongLineY()}; }

	struct CornerFive
	{
		bool Ok = false;
		double T = 0.0;          // third-rail number
		double FourthShort = 0.0;// diamonds short of the head-left corner on the fourth rail
		bool FourthPocketed = false;
		double SpeedAtFirstRail = 0.0;
	};

	CornerFive RunCornerFive(double DValue, double F, double SpeedAtRail, double Bs)
	{
		CornerFive C;
		// Path RAIL_LEFT -> RAIL_FOOT -> RAIL_RIGHT turns clockwise: running English is w_z > 0 at the first rail (+x, +y into
		// y = +W/2: the rail's friction on a w_z > 0 ball pushes it along +x).
		const Track T = Shoot(CornerFiveD(DValue), CornerFiveF(F), SpeedAtRail, +1.0, Bs);
		C.SpeedAtFirstRail = T.SpeedAtFirstRail;
		if (!T.Ok || T.Rails < 2 || !Rails::Left(T.RailOf[0]) || !Rails::Foot(T.RailOf[1]) || !(LengthSquared(T.LineDirection[1]) > 0.0))
		{
			return C;
		}
		const double X = val::CrossX(T.LinePoint[1], T.LineDirection[1], -kD.LongLineY());
		C.T = (kD.HalfLength - X) / kD.Spacing;
		C.Ok = IsFinite(C.T);
		if (T.Rails >= 3 && Rails::Right(T.RailOf[2]) && LengthSquared(T.LineDirection[2]) > 0.0)
		{
			const double X4 = val::CrossX(T.LinePoint[2], T.LineDirection[2], kD.LongLineY());
			C.FourthShort = (X4 + kD.HalfLength) / kD.Spacing;
		}
		C.FourthPocketed = T.Pocketed && T.Pocket == static_cast<int>(PocketId::HeadLeft);
		return C;
	}

	struct Calibration
	{
		double Speed = 0.0;
		double Bs = 0.0;
		double Error = kInfinity;
		CornerFive Run;
	};
}

// SYS-01 ... SYS-04: calibrate Corner-5 at D = 5, F = 3 (T = 2 within 0.25 diamond at a medium speed, 1.2 - 2.5 m/s at the first
// rail, running English b_s in [0, 0.5 R]); predict D = 4.5 / F = 2 -> T = 2.5 and D = 6 / F = 3 -> T = 3 (0.5 diamond); the 5-3-2
// path reaches RAIL_LEFT 0.3 - 1.5 diamonds short of the head-left corner.
RB_TEST(Integ_VAL_SYS01_SYS02_SYS03_SYS04_Slow_CornerFiveSystem)
{
	Calibration Best;
	for (int s = 0; s <= 13; ++s)
	{
		const double Speed = 1.2 + 0.1 * s;
		for (int b = 0; b <= 10; ++b)
		{
			const double Bs = 0.05 * b;
			const CornerFive C = RunCornerFive(5.0, 3.0, Speed, Bs);
			if (C.Ok && Abs(C.T - 2.0) < Best.Error)
			{
				Best = Calibration{Speed, Bs, Abs(C.T - 2.0), C};
			}
		}
	}
	std::printf("  SYS-01 calibration: speed at the first rail %.2f m/s, English %.2f R -> T = %.3f (target 2 +- 0.25)\n", Best.Speed, Best.Bs,
		Best.Run.T);
	RB_CHECK(Best.Error <= 0.25);
	const CornerFive Two = RunCornerFive(4.5, 2.0, Best.Speed, Best.Bs);
	const CornerFive Three = RunCornerFive(6.0, 3.0, Best.Speed, Best.Bs);
	std::printf("  SYS-02 D 4.5 / F 2: T = %.3f (expected 2.5 +- 0.5) %s\n", Two.T, val::Verdict(Two.Ok && Abs(Two.T - 2.5) <= 0.5));
	std::printf("  SYS-03 D 6 / F 3: T = %.3f (expected 3 +- 0.5) %s\n", Three.T, val::Verdict(Three.Ok && Abs(Three.T - 3.0) <= 0.5));
	// "Short of the corner" on the diamond grid (x = -L/2, the origin of the F / T numbers of 6.9; gated); from the 6.9 corner POINT
	// (the diamond lines' intersection, s further out) it is s / Delta = 0.295 diamond more (printed).
	std::printf("  SYS-04 5-3-2 fourth rail: %.2f diamonds short of the head-left corner on the grid (expected 0.3 - 1.5; %.2f from the 6.9 corner "
				"point), pocketed %d %s\n",
		Best.Run.FourthShort, Best.Run.FourthShort + kD.Inset / kD.Spacing, Best.Run.FourthPocketed ? 1 : 0,
		val::Verdict(!Best.Run.FourthPocketed && Best.Run.FourthShort >= 0.3 && Best.Run.FourthShort <= 1.5));
	RB_CHECK(Two.Ok && Abs(Two.T - 2.5) <= 0.5);
	RB_CHECK(Three.Ok && Abs(Three.T - 3.0) <= 0.5);
	RB_CHECK(!Best.Run.FourthPocketed && Best.Run.FourthShort >= 0.3 && Best.Run.FourthShort <= 1.5);
}

namespace
{
	struct PlusResult
	{
		bool Ok = false;
		double Crossing = 0.0; // diamonds from the foot end on the RAIL_RIGHT diamond line
	};

	// Plus system: from P (RAIL_RIGHT diamond line) through S (RAIL_FOOT diamond line), RAIL_FOOT -> RAIL_LEFT; the rolling line after
	// the second rail crosses the RAIL_RIGHT diamond line at P + S diamonds from the foot end. The path turns counter-clockwise
	// (+x, +y into the foot rail, then -x, +y into the left rail): running English is w_z < 0.
	PlusResult RunPlus(double PValue, double SValue, double SpeedAtRail, double Bs)
	{
		PlusResult Out;
		const Vec2 From{kD.HalfLength - PValue * kD.Spacing, -kD.LongLineY()};
		const Vec2 To{kD.EndLineX(), kD.HalfWidth - (SValue - 1.0) * kD.Spacing * 0.5};
		const Track T = Shoot(From, To, SpeedAtRail, -1.0, Bs);
		if (!T.Ok || T.Rails < 2 || !Rails::Foot(T.RailOf[0]) || !Rails::Left(T.RailOf[1]) || !(LengthSquared(T.LineDirection[1]) > 0.0))
		{
			return Out;
		}
		const double X = val::CrossX(T.LinePoint[1], T.LineDirection[1], -kD.LongLineY());
		Out.Crossing = (kD.HalfLength - X) / kD.Spacing;
		Out.Ok = IsFinite(Out.Crossing);
		return Out;
	}
}

// SYS-05: the Plus system - calibrate at P = 3, S = 5 (-> 8, the head-right corner, within 0.25), then P 3 / S 3 -> 6,
// P 2 / S 5 -> 7, P 4 / S 3 -> 7 within 0.5 diamond.
RB_TEST(Integ_VAL_SYS05_Slow_PlusSystem)
{
	double BestSpeed = 0.0;
	double BestBs = 0.0;
	double BestError = kInfinity;
	double BestCrossing = 0.0;
	for (int s = 0; s <= 13; ++s)
	{
		const double Speed = 1.2 + 0.1 * s;
		for (int b = 0; b <= 10; ++b)
		{
			const double Bs = 0.05 * b;
			const PlusResult R = RunPlus(3.0, 5.0, Speed, Bs);
			if (R.Ok && Abs(R.Crossing - 8.0) < BestError)
			{
				BestError = Abs(R.Crossing - 8.0);
				BestSpeed = Speed;
				BestBs = Bs;
				BestCrossing = R.Crossing;
			}
		}
	}
	std::printf("  SYS-05 calibration P 3 / S 5: speed %.2f m/s, English %.2f R -> %.3f (target 8 +- 0.25)\n", BestSpeed, BestBs, BestCrossing);
	RB_CHECK(BestError <= 0.25);
	struct Case
	{
		double P;
		double S;
	};
	const Case Cases[3] = {{3.0, 3.0}, {2.0, 5.0}, {4.0, 3.0}};
	for (const Case& C : Cases)
	{
		const PlusResult R = RunPlus(C.P, C.S, BestSpeed, BestBs);
		const bool Pass = R.Ok && Abs(R.Crossing - (C.P + C.S)) <= 0.5;
		std::printf("  SYS-05 P %.0f / S %.0f: %.3f (expected %.0f +- 0.5) %s\n", C.P, C.S, R.Crossing, C.P + C.S, val::Verdict(Pass));
		RB_CHECK(Pass);
	}
}

namespace
{
	struct KickResult
	{
		bool Ok = false;
		double Closest = kInfinity; // distance of the target from the cue ball's rolling line after the rail [m]
		double Along = 0.0;         // where that line crosses the target's y, minus the target x [m] (+ = long)
	};

	// One-rail kick off RAIL_LEFT: target at (XTarget, W/2 - Diamonds Delta); the cue ball at the same distance from the rail at
	// -XTarget aims at the target's mirror image across the RAIL_LEFT diamond line, arriving at the rail rolling at Speed. The
	// result is measured on the rolling line after the rail like SYS-01 ... 05 (INTERPRETATION: at 0.5 m/s at the rail a ball
	// stops after ~0.6 m on this cloth, short of any target 2 - 4 diamonds off the rail, so "tracks the mirror image" is a
	// statement about the rebound line, not about reaching the target).
	KickResult RunKick(double XTarget, double DiamondsOff, double Speed)
	{
		KickResult K;
		const double YTarget = kD.HalfWidth - DiamondsOff * kD.Spacing;
		const Vec2 Target{XTarget, YTarget};
		const Vec2 Mirror{XTarget, 2.0 * kD.LongLineY() - YTarget};
		const Vec2 Start{-XTarget, YTarget};
		const TableGeometry& T = simtest::NineFoot();
		const PhysicsParams P = val::TableParams(kTableNineFootPro);
		SimInput& In = val::Fresh(T, P);
		In.Record.LogTransitions = true;
		const Vec2 D = Normalized(Mirror - Start);
		const double Rc = ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
		const double ToRail = (kD.HalfWidth - Rc - Start.y) / D.y;
		const double V0 = val::RollingStartSpeed(Speed, ToRail, P);
		val::PlaceRollingSpin(In, 0, ToVec3(Start, kR), ToVec3(D * V0), 0.0, MakeBallSpec(kR, kDefaultBallMass));
		static ShotResult R;
		Simulator Sim;
		if (Sim.Run(In, R) != SimStatus::Ok)
		{
			return K;
		}
		const Track Tr = Follow(R);
		if (Tr.Rails < 1 || !Rails::Left(Tr.RailOf[0]) || !(LengthSquared(Tr.LineDirection[0]) > 0.0))
		{
			return K;
		}
		const Vec2 L0 = Tr.LinePoint[0];
		const Vec2 Ld = Normalized(Tr.LineDirection[0]);
		K.Ok = true;
		K.Closest = Abs(Cross(Ld, Target - L0));
		K.Along = val::CrossX(L0, Ld, YTarget) - XTarget;
		return K;
	}
}

// SYS-06: slow one-rail kick (0.5 m/s at the rail) aimed at the target's mirror image across the diamond line passes within one
// ball diameter of the target; at 2 m/s it misses consistently to one side (trend, reported).
RB_TEST(Integ_VAL_SYS06_Slow_OneRailKickMirrorLine)
{
	const KickResult Slow = RunKick(0.6, 3.0, 0.5);
	std::printf("  SYS-06 slow kick (0.5 m/s): the rebound line passes %.1f mm from the target (<= %.1f mm = 2R), crosses %+.1f mm along %s\n",
		1e3 * Slow.Closest, 2e3 * kR, 1e3 * Slow.Along, val::Verdict(Slow.Ok && Slow.Closest <= 2.0 * kR));
	RB_CHECK(Slow.Ok && Slow.Closest <= 2.0 * kR);
	int Long = 0;
	int Short = 0;
	for (double Off : {2.0, 2.5, 3.0})
	{
		const KickResult Fast = RunKick(0.6, Off, 2.0);
		RB_CHECK(Fast.Ok);
		const bool IsLong = Fast.Along > 0.0;
		Long += IsLong ? 1 : 0;
		Short += IsLong ? 0 : 1;
		std::printf("  SYS-06 fast kick (2 m/s), target %.1f diamonds off the rail: crosses %+.1f mm from the target (%s), line %.1f mm from it\n", Off,
			1e3 * Fast.Along, IsLong ? "long" : "short", 1e3 * Fast.Closest);
	}
	std::printf("  SYS-06 trend: fast kicks miss %s consistently (%d long, %d short)\n", Long == 3 ? "long" : (Short == 3 ? "short" : "NOT"), Long,
		Short);
	RB_CHECK(Long == 3 || Short == 3);
}
