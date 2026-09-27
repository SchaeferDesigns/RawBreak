// Owner: WP-5 (event detection). Second adversarial review: contacts that begin inside the touching band instead of with
// a crossing (physics-collisions 3.3, 3.6, 4.10, 5.3, 5.5; prior-art 5.7). Regression tests of the review fixes:
//  * Detect_Review_TouchingPairThatTurnsInIsPressing: a pair touching with a rounding overlap (1e-15 m) that separated
//    slower than that overlap could open (1.5e-9 ... 1e-8 m/s, just above ApproachSpeedTol) and was then pressed back in
//    had NO event at all: the gap never became positive, so there was no downward crossing, and the start rules had seen a
//    separating pair. A topspin ball drove through a frozen object ball, a slip-pressed ball into the rail (0.05 mm after
//    10 ms and growing). Now a Pressing contact at the maximum;
//  * Detect_Review_ARestWithinTheTouchingBandIsNoCrossing: a ball rolling to rest with its center 1e-12 ... 5e-10 m inside
//    the drop-edge circle (or exactly on it) fell into the pocket, against collisions 5.5 ("use eps_touch to classify a
//    stopped ball exactly on the circle as not crossed"); a ball rolling to rest 0.5 nm against another produced a
//    zero-speed impact event;
//  * Detect_Review_FacingJunctionTouchWithoutApproachPressesIn: a ball entering the facing's range inside the 0.06 mm
//    jaw / facing step (collisions 5.3) without approaching it (parallel, or moving away) and then curved in by its slip
//    ran through the facing with no event (0.8 mm deep after 30 ms and growing);
//  * Detect_Review_AirborneJawKeepsTheNoseProfileRadius: with a nose profile radius r_n > 0 (a ParamTable key) the airborne
//    jaw ignored it (bare edge circle) while the nose tube and the on-cloth jaw include it: 1 mm deeper jaw contacts in
//    flight and an r_n step at the airborne nose / jaw junction.

#include "rbtest.h"

#include "DetectTestUtil.h"

#include <initializer_list>

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
	const Vec3 kPocketAxis{std::sqrt(0.5), std::sqrt(0.5), 0.0}; // CornerPocket(): from the table into the pocket

	// Spin that gives a ball moving with velocity V the plan slip U (collisions 0.2: u = v + w x (-R z)).
	Vec3 SpinForSlip(const Vec3& V, const Vec3& U)
	{
		const Vec3 D = U - V; // = (-R w_y, R w_x, 0)
		return {D.y / kR, -D.x / kR, 0.0};
	}

	// The facing of Detect_FacingOnShelfAndAirborneAndTopEdge: along +x from the origin at y = 0.7, pocket side -y.
	rb::Facing TestFacing() { return MakeFacing({0.0, 0.7}, {0.1, 0.7}, {0.0, -1.0}); }

	double FacingOffset(const rb::Facing& Face) { return (kR - (kNoseH - kR) * std::sin(Face.Backdraft)) / std::cos(Face.Backdraft); }

	// A ball sliding along +x at 1 m/s whose slip pulls it toward the facing (+y, a = mu_s g / 2 per unit of tau^2) and whose
	// plan distance to the facing line has its minimum (the gap's maximum) Dist at local time TauTop, starting at x = X0.
	MotionSegment CurvingIntoFacing(double X0, double TauTop, double Dist)
	{
		const Vec3 V0{1.0, 0.0, 0.0};
		const Vec3 W = SpinForSlip(V0, {0.0, -0.5, 0.0});
		const MotionSegment Probe = Sliding({X0, 0.0, kR}, V0, W);
		const double Ay = Probe.Accel2.y; // > 0
		const double Vy0 = -2.0 * Ay * TauTop;
		const double YTop = 0.7 - Dist;
		const Vec3 V{1.0, Vy0, 0.0};
		return Sliding({X0, YTop - Vy0 * TauTop - Ay * TauTop * TauTop, kR}, V, SpinForSlip(V, {0.0, -0.5 + Vy0, 0.0}));
	}
}

RB_TEST(Detect_Review_TouchingPairThatTurnsInIsPressing)
{
	// D-12 geometry (CB with pure topspin w = (0, 10, 0) frozen to an OB), plus a rounding overlap and a tiny separation.
	for (double Speed : {1.5e-9, 2e-9, 5e-9})
	{
		const MotionSegment A = Sliding({0.0, 0.0, kR}, {-Speed, 0.0, 0.0}, {0.0, 10.0, 0.0});
		const MotionSegment B = Stationary({2.0 * kR - 1e-15, 0.0, kR});
		RB_CHECK(Speed > Numerics().ApproachSpeedTol); // separating faster than v_eps: no start event
		const ContactPrediction P = rb::PredictBallBall(A, kR, B, kR, rb::kInfinity, Numerics());
		RB_REQUIRE(P.Found);
		RB_CHECK(P.Flags == rb::ContactFlags::Pressing); // not at the start: at the gap's maximum
		RB_CHECK_NEAR(P.Time, Speed / (2.0 * A.Accel2.x), 1e-13);
		// The gap never reached zero from above: without the event the balls interpenetrate (0.1 mm after 10 ms).
		RB_CHECK(rb::Length(rb::PositionAt(A, 0.01) - B.Pos0) - 2.0 * kR < -9e-5);
	}
	// Exactly touching (no overlap) the same pair separates into a positive gap first: a (slow) crossing, as before.
	const ContactPrediction Clean =
		rb::PredictBallBall(Sliding({0.0, 0.0, kR}, {-2e-9, 0.0, 0.0}, {0.0, 10.0, 0.0}), kR, Stationary({2.0 * kR, 0.0, kR}), kR, rb::kInfinity, Numerics());
	RB_REQUIRE(Clean.Found);
	RB_CHECK(Clean.Flags == 0);

	// Slip-pressed into the rail (7.1), moving off it at 3e-9 m/s with a 1e-15 m rounding overlap.
	const double Rc = NoseContactOffset();
	const rb::NoseSegment Nose = MakeNose({1.2, 0.635}, {-1.2, 0.635}, {0.0, -1.0});
	const MotionSegment Pressed = Sliding({0.2, 0.635 - Rc + 1e-15, kR}, {1.0, -3e-9, 0.0}, {-20.0, 0.0, 0.0});
	RB_CHECK(Pressed.Accel2.y > 0.0);
	const ContactPrediction Rail = rb::PredictNoseOnCloth(Pressed, kR, Nose, Rc, rb::kInfinity, Numerics());
	RB_REQUIRE(Rail.Found);
	RB_CHECK(Rail.Flags == rb::ContactFlags::Pressing);
	RB_CHECK_NEAR(Rail.Time, 3e-9 / (2.0 * Pressed.Accel2.y), 1e-13);

	// A region boundary: a ball on the drop-edge circle (within the band), creeping out at 2e-9 m/s while its slip pulls it
	// into the pocket: it crosses at the turn (DropEdge, no flag).
	const rb::PocketGeometry P = CornerPocket();
	const Vec3 OnCircle = rb::ToVec3(P.CaptureCenter, kR) - kPocketAxis * (P.DropEdgeRadius - 1e-15);
	const Vec3 Out = kPocketAxis * -2e-9;
	const MotionSegment Creep = Sliding(OnCircle, Out, SpinForSlip(Out, kPocketAxis * -0.5));
	const ContactPrediction Drop = rb::PredictDropEdge(Creep, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(Drop.Found);
	RB_CHECK(Drop.Flags == 0);
	RB_CHECK(Drop.Time < 1e-8);

	// A start deeper than the band (a corrupt 2 um overlap) is no touch: diagnosed, and no pressing event from its turn.
	const ContactPrediction Deep = rb::PredictBallBall(Sliding({0.0, 0.0, kR}, {-2e-9, 0.0, 0.0}, {0.0, 10.0, 0.0}), kR,
		Stationary({2.0 * kR - 2e-6, 0.0, kR}), kR, rb::kInfinity, Numerics());
	RB_CHECK(!Deep.Found);
	RB_CHECK(Deep.Flags == rb::ContactFlags::Overlap);
}

RB_TEST(Detect_Review_ARestWithinTheTouchingBandIsNoCrossing)
{
	// P-4 lip hang at the resolution of eps_touch: a ball rolled along the axis stops with its center Inside m inside a_d.
	const rb::PocketGeometry P = CornerPocket();
	const double V0 = 0.0990285;
	for (double Inside : {-1e-12, 0.0, 1e-12, 5e-10, 2e-9})
	{
		const Vec3 Stop = rb::ToVec3(P.CaptureCenter, kR) - kPocketAxis * (P.DropEdgeRadius - Inside);
		const MotionSegment Probe = Rolling(Stop, kPocketAxis * V0);
		const Vec3 Run = rb::PositionAt(Probe, Probe.TauEnd) - Stop; // stopping distance (0.05 m)
		const MotionSegment Roll = Rolling(Stop - Run, kPocketAxis * V0);
		const ContactPrediction E = rb::PredictDropEdge(Roll, kR, P, rb::kInfinity, Numerics());
		RB_CHECK(E.Found == (Inside > Numerics().ContactTol)); // falls only beyond eps_touch (1 nm)
	}

	// Rolling to rest against a resting ball: 0.5 nm "overlap" at rest is touching (no zero-speed impact); 2 nm is a hit.
	for (double Overlap : {5e-10, 2e-9})
	{
		const MotionSegment A = Rolling({0.0, 0.0, kR}, {0.5, 0.0, 0.0});
		const MotionSegment B = Stationary({rb::PositionAt(A, A.TauEnd).x + 2.0 * kR - Overlap, 0.0, kR});
		RB_CHECK(rb::PredictBallBall(A, kR, B, kR, rb::kInfinity, Numerics()).Found == (Overlap > 1e-9));
	}

	// ... and against the rail.
	const double Rc = NoseContactOffset();
	const rb::NoseSegment Nose = MakeNose({1.2, 0.635}, {-1.2, 0.635}, {0.0, -1.0});
	for (double Overlap : {5e-10, 2e-9})
	{
		const MotionSegment Probe = Rolling({0.0, 0.0, kR}, {0.0, 0.3, 0.0});
		const double Run = rb::PositionAt(Probe, Probe.TauEnd).y;
		const MotionSegment Roll = Rolling({0.0, 0.635 - Rc + Overlap - Run, kR}, {0.0, 0.3, 0.0});
		RB_CHECK(rb::PredictNoseOnCloth(Roll, kR, Nose, Rc, rb::kInfinity, Numerics()).Found == (Overlap > 1e-9));
	}

	// A crossing that is still moving at the window end is kept (TimeLimit just past a D-8 contact).
	const MotionSegment D8 = Rolling({0.0, 0.0, kR}, {0.0, 1.0, 0.0});
	const ContactPrediction Full = rb::PredictNoseOnCloth(D8, kR, Nose, Rc, rb::kInfinity, Numerics());
	RB_REQUIRE(Full.Found);
	const ContactPrediction Cut = rb::PredictNoseOnCloth(D8, kR, Nose, Rc, Full.Time + 1e-12, Numerics());
	RB_REQUIRE(Cut.Found);
	RB_CHECK_NEAR(Cut.Time, Full.Time, 1e-12);
}

RB_TEST(Detect_Review_FacingJunctionTouchWithoutApproachPressesIn)
{
	const rb::Facing Face = TestFacing();
	const double Sf = FacingOffset(Face);
	const double TauEntry = 0.001; // x = 0 (s = 0) at 1 m/s from x = -1 mm

	// Parallel to the face at the entry, Inside m within the 0.06 mm step, then curved in by the slip: Pressing at the entry.
	for (double Inside : {0.01e-3, 0.03e-3, 0.055e-3})
	{
		const MotionSegment S = CurvingIntoFacing(-0.001, TauEntry, Sf - Inside);
		RB_CHECK(S.Accel2.y > 0.0);
		const ContactPrediction P = rb::PredictFacingOnShelf(S, kR, Face, Sf, rb::kInfinity, Numerics());
		RB_REQUIRE(P.Found);
		RB_CHECK(P.Flags == rb::ContactFlags::Pressing);
		RB_CHECK_NEAR(P.Time, TauEntry, 1e-12);
		RB_CHECK(rb::PositionAt(S, 0.03).y - (0.7 - Sf) > 8e-4); // without it: 0.8 mm into the facing after 30 ms
	}

	// Entering while moving away from the face, turning back 2 ms later still inside the step: Pressing at the turn.
	const MotionSegment Away = CurvingIntoFacing(-0.001, TauEntry + 0.002, Sf - 0.03e-3);
	const ContactPrediction Turn = rb::PredictFacingOnShelf(Away, kR, Face, Sf, rb::kInfinity, Numerics());
	RB_REQUIRE(Turn.Found);
	RB_CHECK(Turn.Flags == rb::ContactFlags::Pressing);
	RB_CHECK_NEAR(Turn.Time, TauEntry + 0.002, 1e-12);

	// Turning back only after leaving the step (0.9 um in at the entry, 3 um out at the turn): an ordinary crossing from outside.
	const MotionSegment Out = CurvingIntoFacing(-0.001, TauEntry + 0.002, Sf + 0.003e-3);
	const ContactPrediction Cross = rb::PredictFacingOnShelf(Out, kR, Face, Sf, rb::kInfinity, Numerics());
	RB_REQUIRE(Cross.Found);
	RB_CHECK(Cross.Flags == 0);
	RB_CHECK(Cross.Time > TauEntry + 0.002);
	RB_CHECK_NEAR(0.7 - rb::PositionAt(Out, Cross.Time).y, Sf, 1e-12);

	// A window that starts inside the range and the step, separating, and turns back inside it: Pressing at the turn.
	const MotionSegment Inner = CurvingIntoFacing(0.01, 0.002, Sf - 0.03e-3);
	const ContactPrediction InnerTurn = rb::PredictFacingOnShelf(Inner, kR, Face, Sf, rb::kInfinity, Numerics());
	RB_REQUIRE(InnerTurn.Found);
	RB_CHECK(InnerTurn.Flags == rb::ContactFlags::Pressing);
	RB_CHECK_NEAR(InnerTurn.Time, 0.002, 1e-12);

	// Airborne at z = R (no gravity drift), the same parallel entry: the undercut face's distance R is the shelf's s_f there.
	MotionSegment Fly = CurvingIntoFacing(-0.001, TauEntry, Sf - 0.03e-3);
	Fly.State = rb::MotionState::Airborne;
	Fly.TauEnd = 0.05;
	const ContactPrediction Air = rb::PredictFacingAirborne(Fly, kR, Face, rb::kInfinity, Numerics());
	RB_REQUIRE(Air.Found);
	RB_CHECK(Air.Flags == rb::ContactFlags::Pressing);
	RB_CHECK_NEAR(Air.Time, TauEntry, 1e-12);

	// Unchanged: an approaching entry inside the step is the contact at the entry (no flag); a ball that never presses in
	// (parallel, no slip) has no event.
	const MotionSegment Approach = CurvingIntoFacing(-0.001, 0.0, Sf - 0.03e-3);
	const ContactPrediction Hit = rb::PredictFacingOnShelf(Approach, kR, Face, Sf, rb::kInfinity, Numerics());
	RB_REQUIRE(Hit.Found);
	RB_CHECK(Hit.Flags == 0);
	RB_CHECK_NEAR(Hit.Time, TauEntry, 1e-12);
	const MotionSegment Parallel = Rolling({-0.001, 0.7 - Sf + 0.03e-3, kR}, {1.0, 0.0, 0.0});
	RB_CHECK(!rb::PredictFacingOnShelf(Parallel, kR, Face, Sf, rb::kInfinity, Numerics()).Found);
}

RB_TEST(Detect_Review_AirborneJawKeepsTheNoseProfileRadius)
{
	// With a nose profile radius r_n (cushion.nose_profile_radius) the dispatcher rounds the noses by r_n on the cloth and in
	// flight and the jaws on the cloth (R_c(r_n)), but predicted the airborne jaw with the bare edge circle: a ball at z = R
	// met the jaw 1 mm later in flight than rolling, and the airborne nose tube (R + r_n) stepped down to R at the junction.
	const double Rn = 0.001;
	const double RcN = std::sqrt(rb::Square(kR + Rn) - rb::Square(kNoseH - kR)); // R_c(r_n), collisions 4.10
	const rb::JawArc Jaw = MakeJaw({0.3, 0.0}, 0.004, -rb::kPi, 0.5 * rb::kPi);
	MotionSegment S = Airborne({0.0, 0.0, kR}, {1.0, 0.0, 0.0});
	S.Accel2 = {};
	S.TauEnd = 1.0;
	const ContactPrediction J = rb::PredictJawArcAirborne(S, kR, Jaw, Rn, rb::kInfinity, Numerics());
	RB_REQUIRE(J.Found);
	RB_CHECK_NEAR(0.3 - rb::PositionAt(S, J.Time).x, Jaw.Radius + RcN, 1e-12);
	// r_n = 0 is the bare edge circle (the original overload).
	const ContactPrediction J0 = rb::PredictJawArcAirborne(S, kR, Jaw, rb::kInfinity, Numerics());
	const ContactPrediction J00 = rb::PredictJawArcAirborne(S, kR, Jaw, 0.0, rb::kInfinity, Numerics());
	RB_REQUIRE(J0.Found && J00.Found);
	RB_CHECK(J0.Time == J00.Time);
	RB_CHECK_NEAR(0.3 - rb::PositionAt(S, J0.Time).x, Jaw.Radius + NoseContactOffset(), 1e-12);

	// The dispatcher passes r_n to the airborne jaw.
	rb::TableGeometry T;
	T.JawArcs.PushBack(Jaw);
	T.OuterBoundary = {{-5.0, -5.0}, {5.0, 5.0}};
	rb::DetectOptions Options;
	Options.NoseProfileRadius = Rn;
	const rb::FeaturePrediction D = rb::PredictTableEvent(S, rb::BallSpec{}, {}, T, rb::EnvironmentSpec{}, Options, kG, rb::kInfinity, Numerics());
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == rb::TableFeatureKind::JawArc);
	RB_CHECK(D.Contact.Time == J.Time);

	// Watertight junction with the airborne nose (both tubes R + r_n): falling straight onto the nose's end point (the jaw's
	// tangent point) both touch at z = h + R + r_n.
	const rb::NoseSegment Nose = MakeNose({0.296, 0.5}, {0.296, 0.0}, {-1.0, 0.0}); // ends at the tangent point (0.296, 0)
	const MotionSegment Drop = Airborne({0.296 - 1e-4, 0.0, kNoseH + 0.1}, {});
	const ContactPrediction N = rb::PredictNoseAirborne(Drop, kR, Nose, Rn, rb::kInfinity, Numerics());
	const ContactPrediction A = rb::PredictJawArcAirborne(Drop, kR, Jaw, Rn, rb::kInfinity, Numerics());
	RB_REQUIRE(N.Found && A.Found);
	const double ZN = rb::PositionAt(Drop, N.Time).z;
	const double ZA = rb::PositionAt(Drop, A.Time).z;
	const double Expected = kNoseH + std::sqrt(rb::Square(kR + Rn) - 1e-8);
	RB_CHECK_NEAR(ZN, Expected, 1e-12);
	RB_CHECK_NEAR(ZA, Expected, 1e-12);
}
