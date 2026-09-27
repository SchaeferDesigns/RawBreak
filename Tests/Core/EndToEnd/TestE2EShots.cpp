// Integration round 2 (end-to-end, every package): the basic shots of the game through the whole core, each checked against a
// physical expectation of the specs. Architecture tests A-E2E-3 .. A-E2E-14:
//   3 stop shot, 4 draw, 5 follow (motion A.2 / A.5 closed forms and the Coriolis invariant after a real ball-ball impact),
//   6 one-rail bank, 7 corner pocket, 8 side pocket (pocket state machine), 9 jump shot (motion T-B14 through the loop),
//   10 masse curve (motion T-B15 through the loop), 11 frozen-ball combination (CLI island), 12 jaw rattle, 13 the lag (two
//   strikes, rules.md 4.1), 14 a slow roll on a tilted table (human-factors 4.5).
// Every shot also passes the whole-shot invariants of EndToEndUtil.h (rest, no overlap / cushion penetration, energy never
// rising between events on a level table, clean diagnostics) and is re-run once for bitwise determinism.

#include "rbtest.h"

#include "EndToEnd/EndToEndUtil.h"

#include "rb/Equipment/Cue.h"
#include "rb/Rules/Lag.h"
#include "rb/Shot/ShotRecordBuilder.h"

using namespace rb;

namespace
{
	constexpr double kR = simtest::kR;

	const TableGeometry& NineFoot() { return simtest::Table(kTableNineFootPro); }

	SimInput& Shot(const TableGeometry& T)
	{
		static SimInput In;
		In = SimInput{};
		In.Table = &T;
		In.Params = MakePhysicsParams(T.Spec);
		return In;
	}

	ShotResult& Result(int Slot = 0)
	{
		static ShotResult R[3];
		return R[Slot];
	}

	// Runs the shot, checks the whole-shot invariants and bitwise determinism with a second simulator; dumps on failure.
	bool RunChecked(const SimInput& In, ShotResult& R, bool LevelTable = true)
	{
		Simulator Sim;
		if (Sim.Run(In, R) != SimStatus::Ok)
		{
			RB_CHECK(R.Status == SimStatus::Ok);
			e2e::Dump(R, 60);
			return false;
		}
		const e2e::ShotInvariants I = e2e::CheckInvariants(R, In, LevelTable);
		RB_CHECK(I.AtRest);
		RB_CHECK(I.Overlap <= 1e-6);
		RB_CHECK(I.CushionPenetration <= 1e-6);
		RB_CHECK(!LevelTable || I.EnergyRise <= 1e-9);
		RB_CHECK(I.Clean);
		Simulator Other;
		ShotResult& Again = Result(2);
		Other.Run(In, Again);
		RB_CHECK(e2e::BitwiseEqual(R, Again));
		const bool Good = I.AtRest && I.Overlap <= 1e-6 && I.CushionPenetration <= 1e-6 && (!LevelTable || I.EnergyRise <= 1e-9) && I.Clean;
		if (!Good)
		{
			e2e::Dump(R, 80);
		}
		return Good;
	}

	// Closed-form travel of a ball sliding with velocity V (plan) and no spin until it rolls, then rolling to rest (motion A.2:
	// 12 v^2 / (49 mu_s g) sliding for k = 2/5; A.3: rolling at L_c = 5/7 v to rest over L_c^2 / (2 mu_r g)).
	double StunTravel(double V, const ClothParams& Cloth, double G)
	{
		const double Lc = 5.0 / 7.0 * V;
		return 12.0 * V * V / (49.0 * Cloth.SlidingFriction * G) + Lc * Lc / (2.0 * Cloth.RollingResistance * G);
	}

	// The first BallBall event of balls A < B.
	const ShotEvent* Contact(const ShotResult& R, int A, int B) { return e2e::FirstEvent(R, ShotEventType::BallBall, A, B); }

	// The first Sliding -> Rolling transition of Ball after time From (its state: the velocity at the start of rolling).
	const ShotEvent* StartsRolling(const ShotResult& R, int Ball, double From)
	{
		for (const ShotEvent& E : R.Events)
		{
			if (E.Type == ShotEventType::MotionTransition && E.A == Ball && E.Time > From && E.From == MotionState::Sliding && E.To == MotionState::Rolling)
			{
				return &E;
			}
		}
		return nullptr;
	}

	Vec2 SetupStopShot(SimInput& In, const Vec2& ObjectAt, const Vec2& Dir, double Speed, double B)
	{
		return e2e::SetupStunShot(In, 0, 1, ObjectAt, Dir, Speed, B);
	}
}

// A-E2E-3 stop shot: the cue ball arrives at a full hit with no spin (stun, motion A.4), so the equal-mass impulse leaves it
// (1 - e_b)/2 of its speed and gives the object ball (1 + e_b)/2 (collisions 2, full hit: no throw); the cue ball then slides and
// rolls to rest exactly as the closed forms say (a stop shot creeps by the 2.5 % that e_b < 1 leaves).
RB_TEST(Integ_ARCH_E2E3_StopShot)
{
	SimInput& In = Shot(NineFoot());
	const Vec2 Start = SetupStopShot(In, {0.45, 0.0}, {1.0, 0.0}, 2.5, -0.35);
	RB_REQUIRE(Start.x > -1.1);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	const ShotEvent* Hit = Contact(R, 0, 1);
	RB_REQUIRE(Hit != nullptr);
	const double V = Hit->Pre[0].Velocity.x;
	const double E = In.Params.BallBall.Restitution;
	std::printf("  stop shot: impact at %.4f s, v %.4f m/s, cue-ball spin across the path %.3g m/s\n", Hit->Time, V, Hit->Pre[0].Omega.y * kR);
	RB_CHECK(std::fabs(Hit->Pre[0].Omega.y * kR) <= 1e-6 * V); // stun
	RB_CHECK_NEAR(Hit->Post[0].Velocity.x, 0.5 * (1.0 - E) * V, 1e-9 * V);
	RB_CHECK_NEAR(Hit->Post[1].Velocity.x, 0.5 * (1.0 + E) * V, 1e-9 * V);
	RB_CHECK_NEAR(Hit->Post[0].Velocity.y, 0.0, 1e-12);
	const ShotEvent* Stop = nullptr; // the cue ball's first stop after the impact (the object ball may come back off the foot rail later)
	for (const ShotEvent& X : R.Events)
	{
		if (Stop == nullptr && X.Type == ShotEventType::MotionTransition && X.A == 0 && X.Time > Hit->Time && X.To == MotionState::Stationary)
		{
			Stop = &X;
		}
	}
	RB_REQUIRE(Stop != nullptr);
	const double Creep = Stop->Pre[0].Position.x - Hit->Pre[0].Position.x;
	const double Expected = StunTravel(Hit->Post[0].Velocity.x, In.Params.Cloth, In.Params.Gravity);
	std::printf("  stop shot: cue ball creeps %.6f m (closed form %.6f m), object ball leaves at %.4f m/s\n", Creep, Expected, Hit->Post[1].Velocity.x);
	RB_CHECK_NEAR(Creep, Expected, 1e-9);
	RB_CHECK(Creep > 0.0 && Creep < 0.02); // it stops (within 2 cm of the impact)
	RB_CHECK(R.Finals[0].Status == BallFinalStatus::OnTable);
}

// A-E2E-4 draw shot: b = -0.5 at 3 m/s, the object ball 0.5 m away: the cue ball still spins backwards at the impact, so after
// losing its speed it draws back; its velocity when it starts rolling is the Coriolis invariant of its post-impact state (motion
// A.5; T-A7 checks it for single segments, here through an event loop impact).
RB_TEST(Integ_ARCH_E2E4_DrawShot)
{
	SimInput& In = Shot(NineFoot());
	simtest::Place(In, 0, {-0.6, 0.1, kR});
	simtest::Place(In, 1, {-0.1, 0.1, kR});
	StrikeRequest S = simtest::Strike(0, 3.0, 0.0, 0.0, 0.0, -0.5);
	S.Input.SquirtEnabled = false;
	In.Strikes.PushBack(S);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	const ShotEvent* Hit = Contact(R, 0, 1);
	RB_REQUIRE(Hit != nullptr);
	RB_CHECK(Hit->Pre[0].Omega.y < 0.0); // still backspin at the impact
	const Vec3 Lc = CoriolisInvariant(Hit->Post[0].Velocity, Hit->Post[0].Omega, kR);
	const ShotEvent* Roll = StartsRolling(R, 0, Hit->Time);
	RB_REQUIRE(Roll != nullptr);
	std::printf("  draw: L_c after impact (%.6f, %.6f), velocity at the start of rolling (%.6f, %.6f)\n", Lc.x, Lc.y, Roll->Pre[0].Velocity.x,
		Roll->Pre[0].Velocity.y);
	RB_CHECK(Lc.x < -0.2);
	RB_CHECK_NEAR(Roll->Pre[0].Velocity.x, Lc.x, 1e-9 * Length(Lc) + 1e-12);
	RB_CHECK_NEAR(Roll->Pre[0].Velocity.y, Lc.y, 1e-9 * Length(Lc) + 1e-12);
	const double Back = Hit->Pre[0].Position.x - R.Finals[0].State.Position.x;
	std::printf("  draw: the cue ball comes back %.3f m\n", Back);
	RB_CHECK(Back > 0.15);
}

// A-E2E-5 follow shot: b = +0.5, the same layout: the cue ball keeps its topspin through the impact and follows forward at the
// Coriolis invariant of its post-impact state.
RB_TEST(Integ_ARCH_E2E5_FollowShot)
{
	SimInput& In = Shot(NineFoot());
	simtest::Place(In, 0, {-0.6, 0.1, kR});
	simtest::Place(In, 1, {-0.1, 0.1, kR});
	StrikeRequest S = simtest::Strike(0, 3.0, 0.0, 0.0, 0.0, 0.5);
	S.Input.SquirtEnabled = false;
	In.Strikes.PushBack(S);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	const ShotEvent* Hit = Contact(R, 0, 1);
	RB_REQUIRE(Hit != nullptr);
	RB_CHECK(Hit->Pre[0].Omega.y > 0.0);
	const Vec3 Lc = CoriolisInvariant(Hit->Post[0].Velocity, Hit->Post[0].Omega, kR);
	const ShotEvent* Roll = StartsRolling(R, 0, Hit->Time);
	RB_REQUIRE(Roll != nullptr);
	std::printf("  follow: L_c after impact (%.6f, %.6f), velocity at the start of rolling (%.6f, %.6f)\n", Lc.x, Lc.y, Roll->Pre[0].Velocity.x,
		Roll->Pre[0].Velocity.y);
	RB_CHECK(Lc.x > 0.2);
	RB_CHECK_NEAR(Roll->Pre[0].Velocity.x, Lc.x, 1e-9 * Length(Lc) + 1e-12);
	RB_CHECK_NEAR(Roll->Pre[0].Velocity.y, Lc.y, 1e-9 * Length(Lc) + 1e-12);
	RB_CHECK(Roll->Pre[0].Position.x > Hit->Pre[0].Position.x); // it follows
}

// A-E2E-6 one-rail bank: a rolling cue ball (natural roll, b = 0.4, motion T-B4) at 45 deg into the left long rail (C4): one
// cushion contact, the normal velocity reversed with an effective restitution inside the collisions 4.8 range, the tangential
// velocity not increased, energy lost, and the ball leaves at an angle close to the mirror angle (topspin bends it forward).
RB_TEST(Integ_ARCH_E2E6_OneRailBank)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = Shot(T);
	simtest::Place(In, 0, {-1.0, -0.2, kR});
	StrikeRequest S = simtest::Strike(0, 2.0, 0.25 * kPi, 0.0, 0.0, 0.4);
	S.Input.SquirtEnabled = false;
	In.Strikes.PushBack(S);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	const ShotEvent* Rail = e2e::FirstEvent(R, ShotEventType::BallCushion, 0);
	RB_REQUIRE(Rail != nullptr);
	RB_CHECK(Rail->Feature == static_cast<std::uint8_t>(CushionId::LeftHead));
	RB_CHECK(e2e::FirstEvent(R, ShotEventType::BallCushion, 0) == e2e::FirstEvent(R, ShotEventType::BallCushion)); // the first rail event
	const Vec3 In0 = Rail->Pre[0].Velocity;
	const Vec3 Out = Rail->Post[0].Velocity;
	const double Ratio = -Out.y / In0.y;
	const double Incidence = std::atan2(In0.x, In0.y) / kDegToRad;
	const double Rebound = std::atan2(Out.x, -Out.y) / kDegToRad;
	std::printf("  bank: in (%.4f, %.4f) out (%.4f, %.4f): normal ratio %.3f, angle in %.2f deg out %.2f deg (from the normal)\n", In0.x, In0.y, Out.x, Out.y,
		Ratio, Incidence, Rebound);
	RB_CHECK(In0.y > 0.0 && Out.y < 0.0);
	RB_CHECK(Ratio > 0.5 && Ratio < 0.98);
	RB_CHECK(Out.x <= In0.x + 1e-12);
	RB_CHECK(e2e::Energy(Rail->Post[0], In.Balls[0].Spec, In.Params.Gravity) < e2e::Energy(Rail->Pre[0], In.Balls[0].Spec, In.Params.Gravity));
	RB_CHECK(std::fabs(Rebound - Incidence) < 20.0);
}

// A-E2E-7 corner pocket: a straight-in stop shot along the axis of the foot-right corner pocket (P2) from 0.35 m: the object
// ball drops (BallPocketEnter, then BallPocketed in P2, pocketed below z = -R), the cue ball stops near the impact point.
RB_TEST(Integ_ARCH_E2E7_CornerPocket)
{
	const TableGeometry& T = NineFoot();
	const PocketGeometry& P = T.Pockets[static_cast<int>(PocketId::FootRight)];
	SimInput& In = Shot(T);
	SetupStopShot(In, P.MouthMid - P.Axis * 0.35, P.Axis, 2.0, -0.3);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	RB_CHECK(R.Finals[1].Status == BallFinalStatus::Pocketed);
	RB_CHECK(R.Finals[1].Pocket == PocketId::FootRight);
	RB_CHECK(R.Finals[1].State.Position.z <= -kR + 1e-9);
	const ShotEvent* Enter = e2e::FirstEvent(R, ShotEventType::BallPocketEnter, 1);
	const ShotEvent* Pocketed = e2e::FirstEvent(R, ShotEventType::BallPocketed, 1);
	RB_REQUIRE(Enter != nullptr && Pocketed != nullptr);
	RB_CHECK(Enter->Time < Pocketed->Time && Pocketed->Feature == static_cast<std::uint8_t>(PocketId::FootRight));
	RB_CHECK(R.Finals[0].Status == BallFinalStatus::OnTable);
	RB_CHECK(Length(XY(R.Finals[0].State.Position) - XY(Contact(R, 0, 1)->Pre[0].Position)) < 0.02);
	RB_CHECK(R.Record.End.Balls[1].Status == BallEndStatus::Pocketed && R.Record.End.Balls[1].Pocket == PocketId::FootRight);
}

// A-E2E-8 side pocket: the same straight in along the axis of the right side pocket (P1) from 0.3 m.
RB_TEST(Integ_ARCH_E2E8_SidePocket)
{
	const TableGeometry& T = NineFoot();
	const PocketGeometry& P = T.Pockets[static_cast<int>(PocketId::SideRight)];
	SimInput& In = Shot(T);
	SetupStopShot(In, P.MouthMid - P.Axis * 0.3, P.Axis, 1.8, -0.3);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	RB_CHECK(R.Finals[1].Status == BallFinalStatus::Pocketed);
	RB_CHECK(R.Finals[1].Pocket == PocketId::SideRight);
	RB_CHECK(e2e::FirstEvent(R, ShotEventType::BallPocketed, 1) != nullptr);
	RB_CHECK(R.Finals[0].Status == BallFinalStatus::OnTable);
}

// A-E2E-9 jump shot: motion T-B14 (V = 4 m/s, theta = 50 deg, 9 oz cue, e_tip = 0.85; MOT parameters pinned) through the event
// loop: the hop reaches 0.212434 m, lands after 0.4162907 s at dx = 0.8488254 m (exact) - the follow-through tip does not
// re-resolve the strike's own contact (integration fix) - and it clears a ball 15 cm ahead (BallJumpedOver in the record, no
// contact with it).
RB_TEST(Integ_ARCH_E2E9_JumpShotOverABall)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = Shot(T);
	In.Params = simtest::ColParams(); // g 9.80665, cloth (0.2, 0.010, 10), e_slate 0.6, h_min 2 mm (motion 9 / T-B14)
	const BallSpec Mot = MakeBallSpec(kR, 0.170);
	simtest::Place(In, 0, {-0.8, 0.0, kR}, {}, {}, Mot);
	simtest::Place(In, 1, {-0.65, 0.0, kR}, {}, {}, Mot);
	StrikeRequest S;
	S.Ball = 0;
	S.Input.Speed = 4.0;
	S.Input.Elevation = 50.0 * kDegToRad;
	S.Input.Cue = kCuePlaying19oz;
	S.Input.Cue.Mass = 9.0 * kOunce;
	S.Input.Cue.TipRestitution = 0.85;
	S.Input.Cue.EndMass = 0.170 / 20.0;
	S.Input.LambdaOverride = -1.0; // jump-cue schedule (M <= 0.35 kg): lambda(50 deg) = 0
	S.Input.SquirtEnabled = false;
	In.Strikes.PushBack(S);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	RB_CHECK(e2e::CountEvents(R, ShotEventType::TipRecontact) == 0);
	const ShotEvent* Up = e2e::FirstEvent(R, ShotEventType::BallAirborne, 0);
	const ShotEvent* Land = e2e::FirstEvent(R, ShotEventType::BallSlate, 0);
	RB_REQUIRE(Up != nullptr && Land != nullptr);
	std::printf("  jump: apex %.7f m above R, first landing %.7f s at dx %.7f m\n", Up->Value - kR, Land->Time, Land->Pre[0].Position.x + 0.8);
	RB_CHECK_NEAR(Up->Value - kR, 0.212434, 1e-6);
	RB_CHECK_NEAR(Land->Time, 0.4162907, 1e-7);
	RB_CHECK_NEAR(Land->Pre[0].Position.x + 0.8, 0.8488254, 1e-7);
	RB_CHECK(Contact(R, 0, 1) == nullptr || Contact(R, 0, 1)->Time > Land->Time); // never touched on the way over
	int Jumped = 0;
	for (const RecordEvent& E : R.Record.Events)
	{
		Jumped += E.Type == RecordEventType::BallJumpedOver && E.A == 0 && E.B == 1 ? 1 : 0;
	}
	RB_CHECK(Jumped == 1);
}

// A-E2E-10 masse: motion T-B15 (V = 2.5 m/s, theta = 75 deg, a = 0.4, b = -0.3, 19 oz, e_tip = 0.73; the playing-cue pinch
// schedule gives lambda = 1): the path curves, and the velocity at the start of rolling is L_c = (-0.126261, -1.184610) m/s,
// direction -96.08 deg (1e-6, the Coriolis invariant is unchanged by the sliding curve and the slate hop).
RB_TEST(Integ_ARCH_E2E10_MasseCurve)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = Shot(T);
	In.Params = simtest::ColParams();
	simtest::Place(In, 0, {-0.6, 0.45, kR}, {}, {}, MakeBallSpec(kR, 0.170));
	StrikeRequest S;
	S.Ball = 0;
	S.Input.Speed = 2.5;
	S.Input.Elevation = 75.0 * kDegToRad;
	S.Input.OffsetA = 0.4;
	S.Input.OffsetB = -0.3;
	S.Input.Cue = kCuePlaying19oz;
	S.Input.Cue.TipRestitution = 0.73;
	S.Input.Cue.EndMass = 0.170 / 20.0;
	S.Input.LambdaOverride = 1.0;
	S.Input.SquirtEnabled = false;
	In.Strikes.PushBack(S);
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	RB_CHECK(R.Strikes[0].Result.Error == ErrorCode::Ok && !R.Strikes[0].Result.Miscue);
	const ShotEvent* Roll = StartsRolling(R, 0, 0.0);
	RB_REQUIRE(Roll != nullptr);
	RB_CHECK(e2e::FirstEvent(R, ShotEventType::BallCushion, 0) == nullptr || e2e::FirstEvent(R, ShotEventType::BallCushion, 0)->Time > Roll->Time);
	std::printf("  masse: velocity at the start of rolling (%.6f, %.6f) at t = %.4f s, position (%.4f, %.4f)\n", Roll->Pre[0].Velocity.x,
		Roll->Pre[0].Velocity.y, Roll->Time, Roll->Pre[0].Position.x, Roll->Pre[0].Position.y);
	RB_CHECK_NEAR(Roll->Pre[0].Velocity.x, -0.126261, 1e-6);
	RB_CHECK_NEAR(Roll->Pre[0].Velocity.y, -1.184610, 1e-6);
	// The path curved: the initial velocity points along the stroke (+x), the rolling velocity nearly along -y.
	const Vec3 V0 = R.Strikes[0].Result.State.Velocity;
	RB_CHECK(V0.x > 0.0);
	RB_CHECK(std::fabs(std::atan2(Roll->Pre[0].Velocity.y, Roll->Pre[0].Velocity.x) / kDegToRad - (-96.0839)) < 1e-3);
}

// A-E2E-11 frozen-ball combination: object balls 1 and 2 frozen (touching) on the line into the foot-right corner pocket, the cue
// ball hits ball 1 full on that line: the contact runs through the CLI island (touching balls), ball 2 is driven into P2 and ball
// 1 stays nearly where it was (momentum passes through the frozen pair, collisions 3.9).
RB_TEST(Integ_ARCH_E2E11_FrozenBallCombination)
{
	const TableGeometry& T = NineFoot();
	const PocketGeometry& P = T.Pockets[static_cast<int>(PocketId::FootRight)];
	SimInput& In = Shot(T);
	// The cue ball stuns into ball 1 (no spin at the impact, so it does not follow into the pair); ball 2 frozen ahead of it.
	const Vec2 One = P.MouthMid - P.Axis * (0.3 + 2.0 * kR);
	SetupStopShot(In, One, P.Axis, 2.2, -0.3);
	const Vec2 Two = One + P.Axis * (2.0 * kR);
	simtest::Place(In, 2, ToVec3(Two, kR));
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	RB_CHECK(R.Diagnostics.Islands >= 1);
	RB_CHECK(R.Finals[2].Status == BallFinalStatus::Pocketed && R.Finals[2].Pocket == PocketId::FootRight);
	RB_CHECK(R.Finals[1].Status == BallFinalStatus::OnTable && R.Finals[0].Status == BallFinalStatus::OnTable);
	const double OneMoved = Length(XY(R.Finals[1].State.Position) - One);
	const double CueMoved = Length(XY(R.Finals[0].State.Position) - (One - P.Axis * (2.0 * kR)));
	std::printf("  combination: ball 2 %s, ball 1 moved %.3f m, cue ball %.3f m from the impact point\n",
		R.Finals[2].Status == BallFinalStatus::Pocketed ? "pocketed" : "not pocketed", OneMoved, CueMoved);
	RB_CHECK(OneMoved < 0.3);  // the momentum went through the frozen pair
	RB_CHECK(CueMoved < 0.05); // the stun stopped the cue ball
	bool OneTwo = false;
	for (const RecordEvent& E : R.Record.Events)
	{
		OneTwo = OneTwo || (E.Type == RecordEventType::BallBall && E.A == 1 && E.B == 2);
	}
	RB_CHECK(OneTwo); // the rules see the 1-2 contact
}

// A-E2E-11b a ball hanging on the lip of the foot-right corner pocket (its center 0.2 mm outside the drop-edge circle a_d, collisions
// 5.5 "hanging on the lip") with ball 1 frozen behind it, the cue ball stunned into ball 1: the hanging ball is pushed over the
// drop edge while ball 1 still presses it inside the CLI island (integration fix: the island holds it until they separate, so
// ball 1 never passes into a pocket-state ball) and drops; nothing overlaps, energy never rises.
RB_TEST(Integ_ARCH_E2E11b_HangingBallPushedIn)
{
	const TableGeometry& T = NineFoot();
	const PocketGeometry& P = T.Pockets[static_cast<int>(PocketId::FootRight)];
	SimInput& In = Shot(T);
	const Vec2 Lip = P.CaptureCenter - P.Axis * (P.DropEdgeRadius + 2e-4);
	const Vec2 One = Lip - P.Axis * (2.0 * kR);
	SetupStopShot(In, One, P.Axis, 1.2, -0.25);
	simtest::Place(In, 2, ToVec3(Lip, kR));
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	RB_CHECK(R.Diagnostics.Islands >= 1);
	RB_CHECK(R.Finals[2].Status == BallFinalStatus::Pocketed && R.Finals[2].Pocket == PocketId::FootRight);
	RB_CHECK(R.Finals[1].Status == BallFinalStatus::OnTable);
	RB_CHECK(R.Diagnostics.OverlapWarnings == 0);
	// Without the stroke the hanging ball stays on the lip (a valid resting state; its center is outside a_d).
	SimInput& Rest = Shot(T);
	simtest::Place(Rest, 2, ToVec3(Lip, kR));
	ShotResult& Quiet = Result(1);
	Simulator Sim;
	RB_REQUIRE(Sim.Run(Rest, Quiet) == SimStatus::Ok);
	RB_CHECK(Quiet.Finals[2].Status == BallFinalStatus::OnTable);
	// A ball at rest with its center over the hole is not a valid input (no support there; pocket states are the simulator's).
	SimInput& Bad = Shot(T);
	simtest::Place(Bad, 2, ToVec3(P.CaptureCenter - P.Axis * (P.DropEdgeRadius - 0.01), kR));
	RB_CHECK(Sim.Run(Bad, Quiet) == SimStatus::InvalidInput && Quiet.Diagnostics.InputError == ErrorCode::InvalidState);
}

// A-E2E-12 jaw rattle: a fast ball driven along the foot rail into the foot-right corner pocket a few mm off the rail hits the
// near jaw, crosses to the far jaw and back (collisions 5.5 "rattle in the jaws"): jaw contacts on both sides, then it either drops
// or rattles out; every contact is logged as BallJaw (rules: rail contacts of that pocket).
RB_TEST(Integ_ARCH_E2E12_JawRattle)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = Shot(T);
	const double Rc = ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
	const double Y = -T.HalfWidth + Rc + 0.012; // 12 mm off the right long rail (C0, y = -W/2)
	simtest::PlaceRolling(In, 1, {0.55, Y, kR}, {4.0, -0.0105 * 4.0, 0.0});
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	int Sides[2] = {};
	int Jaws = 0;
	for (const ShotEvent& E : R.Events)
	{
		if (E.Type == ShotEventType::BallJaw && E.A == 1)
		{
			++Jaws;
			++Sides[(E.SubFeature & 0x0F) != 0 ? 1 : 0];
		}
	}
	std::printf("  rattle: %d jaw contacts (incoming %d, outgoing %d), ball %s\n", Jaws, Sides[0], Sides[1],
		R.Finals[1].Status == BallFinalStatus::Pocketed ? "pocketed" : "rattled out");
	RB_CHECK(Sides[0] >= 1 && Sides[1] >= 1);
	RB_CHECK(Jaws >= 2);
}

// A-E2E-13 the lag (rules.md 4.1): two cue balls struck at t = 0 by two cues from the lag positions behind the head string toward
// the foot rail; both come back after exactly one foot-rail contact, each ball's facts come from the shared record (its own tip
// interval of its own strike), and EvaluateLag gives the win to the ball that stops closer to the head cushion.
RB_TEST(Integ_ARCH_E2E13_LagTwoStrikes)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = Shot(T);
	double Radii[kMaxBalls] = {};
	Radii[0] = kR;
	Radii[1] = kR;
	const rules::RulesTable Rules = BuildRulesTable(T, kR, Radii, kMaxBalls);
	Vec2 First;
	Vec2 Second;
	rules::LagStartPositions(Rules, First, Second);
	simtest::Place(In, 0, ToVec3(First, kR));
	simtest::Place(In, 1, ToVec3(Second, kR));
	// Lag speeds for the fast worsted cloth of the 9-ft table (mu_r = 0.007): one length and back to 0.2-0.5 m from the head cushion.
	In.Strikes.PushBack(simtest::Strike(0, 0.95, 0.0, 0.0, 0.0, 0.2));
	In.Strikes.PushBack(simtest::Strike(1, 0.97, 0.0, 0.0, 0.0, 0.2));
	ShotResult& R = Result();
	RB_REQUIRE(RunChecked(In, R));
	RB_REQUIRE(R.Strikes.Size() == 2);
	const rules::LagBallFacts A = rules::DeriveLagBallFacts(R.Record, 0, Rules, RulesTolerances{});
	const rules::LagBallFacts B = rules::DeriveLagBallFacts(R.Record, 1, Rules, RulesTolerances{});
	const rules::LagResult Lag = rules::EvaluateLag(A, B, RulesTolerances{});
	std::printf("  lag: ball 0 d = %.4f m (%d foot contacts, bad %d), ball 1 d = %.4f m (%d, bad %d), outcome %d\n", A.Distance, A.FootCushionContacts, A.Bad,
		B.Distance, B.FootCushionContacts, B.Bad, static_cast<int>(Lag.Outcome));
	RB_CHECK(A.FootCushionContacts == 1 && B.FootCushionContacts == 1);
	RB_CHECK(!A.Bad && !B.Bad);
	RB_CHECK(A.Distance > 0.0 && B.Distance > 0.0);
	RB_CHECK(Lag.Outcome == (A.Distance < B.Distance ? rules::LagOutcome::FirstWins : rules::LagOutcome::SecondWins));
	// Each ball carries its own strike's tip interval.
	int Own = 0;
	for (const TipContact& C : R.Record.Stroke.TipContacts)
	{
		Own += (C.Ball == 0 && C.Strike == 0) || (C.Ball == 1 && C.Strike == 1) ? 1 : 0;
	}
	RB_CHECK(Own == 2 && R.Record.Stroke.TipContacts.Size() == 2);
}

// A-E2E-14 slow roll on a tilted table (human-factors 4.5, a dive-bar slope of 2 mm/m across the table): the rolling ball drifts
// toward -Slope (downhill), is carried by tilt chain pieces (TiltRefresh nodes), and still comes to rest (|s| <= 0.7 mu_r); on the
// level table the same stroke runs straight. The drift has the size of a rolling ball's in-plane gravity g s / (1 + k) over the
// roll (between a quarter and twice 1/2 a t^2 with the level roll time).
RB_TEST(Integ_ARCH_E2E14_TiltedTableSlowRoll)
{
	const TableGeometry& T = NineFoot();
	double FinalY[2] = {};
	double RollTime = 0.0;
	for (int Tilted = 0; Tilted < 2; ++Tilted)
	{
		SimInput& In = Shot(T);
		if (Tilted != 0)
		{
			TableCondition Condition;
			Condition.Slope = {0.0, 2.0e-3};
			In.Params = MakePhysicsParams(T.Spec, Condition);
			RB_REQUIRE(ValidatePhysicsParams(In.Params) == ErrorCode::Ok);
		}
		simtest::Place(In, 0, {-1.0, 0.0, kR});
		In.Strikes.PushBack(simtest::Strike(0, 0.3, 0.0, 0.0, 0.0, 0.4)); // about 0.3 m/s rolling: 0.7 m on the worsted cloth
		ShotResult& R = Result(Tilted);
		RB_REQUIRE(RunChecked(In, R, Tilted == 0));
		RB_CHECK(e2e::FirstEvent(R, ShotEventType::BallCushion, 0) == nullptr); // a slow roll in open table
		FinalY[Tilted] = R.Finals[0].State.Position.y;
		if (Tilted == 0)
		{
			RollTime = R.StopTime;
			RB_CHECK(R.Diagnostics.TiltRefreshes == 0);
		}
		else
		{
			RB_CHECK(R.Diagnostics.TiltRefreshes >= 1);
			std::printf("  tilt: %d tilt refreshes, level final y %.6f, tilted final y %.6f, stop %.3f s\n", R.Diagnostics.TiltRefreshes, FinalY[0], FinalY[1],
				R.StopTime);
		}
	}
	RB_CHECK(std::fabs(FinalY[0]) < 1e-9);
	const double Drift = -FinalY[1]; // downhill = -y
	const double Scale = 0.5 * (kStandardGravity * 2.0e-3 / 1.4) * RollTime * RollTime;
	std::printf("  tilt: drift %.4f m downhill, 1/2 a t^2 scale %.4f m\n", Drift, Scale);
	RB_CHECK(Drift > 0.25 * Scale && Drift < 2.0 * Scale);
}
