// Owner: WP-0 (architecture). Compiles every public header together and pins the design defaults
// that come from the verified specs (a default change must be a deliberate, reviewed edit here).
// Spec-ported numeric tests live in the work-package directories (Docs/architecture.md, section 17).

#include "rbtest.h"

#include <cstring>

#include "rb/Config.h"
#include "rb/Core/Assert.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Random.h"
#include "rb/Core/Tolerances.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Human/AiProfiles.h"
#include "rb/Human/BallMarks.h"
#include "rb/Human/Chores.h"
#include "rb/Human/CueState.h"
#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Progression.h"
#include "rb/Human/Skill.h"
#include "rb/Human/TipState.h"
#include "rb/Human/Venue.h"
#include "rb/Math/Aabb.h"
#include "rb/Math/Polynomial.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallBall.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Compliant.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Cushion.h"
#include "rb/Physics/Detect.h"
#include "rb/Physics/EventQueue.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ParamTable.h"
#include "rb/Physics/Playback.h"
#include "rb/Physics/PocketDrop.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Physics/Slate.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Rules/TableRules.h"
#include "rb/Shot/ShotRecord.h"
#include "rb/Shot/ShotRecordBuilder.h"
#include "rb/Version.h"

// Compile-time design constraints.
static_assert(rb::kMaxBalls >= 22, "kMaxBalls must cover snooker (22 balls)");
static_assert(rb::kMaxBalls <= 32, "per-ball bit masks are 32 bits");
static_assert(rb::kRailFeatureCount <= 32, "rail-feature masks are 32 bits");
static_assert(rb::RailFeatureOfJaw(rb::PocketId::HeadLeft, rb::JawSide::Outgoing) == 17, "rail feature numbering");
static_assert(rb::kBallRadius * 2.0 == rb::kBallDiameter, "ball constants consistent");
static_assert(rb::kMaxBallPairs == 276, "pair table size");
static_assert(rb::kMaxStrikes == 2, "the lag needs two strikes in one simulation");
static_assert(rb::kEventHeapCapacity >= 2 * rb::kMaxBalls + rb::kMaxBallPairs + rb::kMaxStrikes, "heap holds all live slots");

RB_TEST(Arch_CapacitiesCoverSnookerAndMasks)
{
	rb::BallSet Set;
	RB_CHECK(Set.Count == 0);
}

RB_TEST(Arch_MotionAndSlateDefaultsFromSpec)
{
	const rb::ClothParams Cloth = rb::kClothDefault;
	RB_CHECK(Cloth.SlidingFriction == 0.20);
	RB_CHECK(Cloth.RollingResistance == 0.010);
	RB_CHECK(Cloth.SpinDeceleration == 10.0);
	const rb::SlateParams Slate;
	RB_CHECK(Slate.Restitution == 0.6);
	RB_CHECK(Slate.MinBounceHeight == 0.002);
	RB_CHECK(Slate.MaxBounces == 10);
	RB_CHECK_NEAR(rb::MinBounceSpeed(Slate, rb::kStandardGravity), 0.1980571, 1e-7);
	RB_CHECK_NEAR(rb::SpinFrictionCoefficient(10.0, 0.028575, rb::kStandardGravity), 0.01166, 1e-5);
}

RB_TEST(Arch_CollisionDefaultsFromSpec)
{
	const rb::PhysicsParams P;
	RB_CHECK(P.Gravity == 9.80665);
	RB_CHECK(P.BallBall.Restitution == 0.95);
	RB_CHECK(P.BallBall.MuA == 9.951e-3 && P.BallBall.MuB == 0.108 && P.BallBall.MuC == 1.088);
	RB_CHECK(P.Cushion.OnClothModel == rb::CushionModel::Mathavan2010);
	RB_CHECK(P.Cushion.Friction == 0.14);
	RB_CHECK(P.Cushion.MathavanSplitAtSlipReversal); // architecture decision (review item 14): N by accuracy gate, split on
	RB_CHECK(P.Cushion.MathavanSteps > 0 && P.Cushion.MathavanSteps <= 200);
	RB_CHECK(P.Cushion.Restitution.Max == 0.97 && P.Cushion.Restitution.Slope == 0.035 && P.Cushion.Restitution.Min == 0.60);
	RB_CHECK(P.Cli.HertzStiffness == 8.0587e8);
	RB_CHECK(P.Cli.TsujiAlpha < 0.0); // derived from BallBall.Restitution at Run start (TsujiAlphaForRestitution)
	RB_CHECK(P.Cli.TimeStep == 1e-6);
	RB_CHECK(P.Cli.RigidTimeStep == 20e-6);
	RB_CHECK(P.Cli.SustainedSpeed == P.Numerics.RestSpeed);
	RB_CHECK(P.Numerics.CompliantMaxDuration == 50e-3);
	RB_CHECK(P.Numerics.MaxIslandSteps > 0);
	RB_CHECK(P.Origin == rb::ParamsOrigin::Unset);
	RB_CHECK(P.Pockets == rb::PocketModel::GeometricLevelA);
	RB_CHECK(P.Numerics.RestSpeed == 2e-3);
	RB_CHECK(P.Numerics.MaxEvents == 20000);
	RB_CHECK(P.Numerics.ZenoContactCount == 8 && P.Numerics.ZenoWindow == 10e-3);
	RB_CHECK(P.Numerics.ContactTol == 1e-9);
}

RB_TEST(Arch_CueDefaultsFromSpec)
{
	const rb::CueSpec Cue = rb::kCuePlaying19oz;
	RB_CHECK_NEAR(Cue.Mass, 0.5386409, 1e-7);
	RB_CHECK(Cue.TipRestitution == 0.73);
	RB_CHECK(Cue.TipFriction == 0.6);
	RB_CHECK_NEAR(0.170 / Cue.EndMass, 20.0, 1e-9);
	RB_CHECK_NEAR(rb::kCueBreak21oz.Mass, 0.5953400, 1e-7);
	RB_CHECK_NEAR(rb::kCueJump9oz.Mass, 0.2551457, 1e-7);
	const rb::PinchParams Pinch;
	RB_CHECK(Pinch.PinchRestitution == 0.2);
}

RB_TEST(Arch_TablePresetsFromEquipmentSpec)
{
	const rb::TableSpec T = rb::kTableNineFootPro;
	RB_CHECK(T.Length == 2.54 && T.Width == 1.27);
	RB_CHECK(T.CushionNoseHeight == 0.03629025);
	RB_CHECK_NEAR(T.Corner.CutAngle, 2.4783675, 1e-7);
	RB_CHECK_NEAR(T.Side.CutAngle, 1.8151424, 1e-7);
	RB_CHECK(T.Corner.Mouth == 0.1143 && T.Side.Mouth == 0.127);
	RB_CHECK(rb::GetTableSpec(rb::TablePreset::SevenFootBar).Corner.Mouth == 0.123825);
}

RB_TEST(Arch_RulesTolerancesFromSpec)
{
	const rb::RulesTolerances R;
	RB_CHECK(R.TieWindow == 0.5e-3);
	RB_CHECK(R.Frozen == 1.0e-4);
	RB_CHECK(R.PushDuration == 4.0e-3);
	RB_CHECK_NEAR(R.GrazeAngle * rb::kRadToDeg, 75.0, 1e-12);
	const rb::NumericsConfig N;
	RB_CHECK(N.LineCrossEps == R.Line);
	RB_CHECK(N.LeaveDistance == R.Leave);
	const rb::rules::RulesTable Table = rb::rules::MakeRulesTable(2.54, 1.27, 0.028575);
	RB_CHECK(Table.HeadStringX == -0.635 && Table.FootSpot.x == 0.635);
	RB_CHECK_NEAR(Table.BaulkX, -0.762, 1e-12);
	RB_CHECK(!rb::rules::AboveHeadString({-0.635, 0.0}, Table, R.Line));
	RB_CHECK(rb::rules::AboveHeadString({-0.6351, 0.0}, Table, R.Line));
}

RB_TEST(Arch_EventQueueStrictTotalOrder)
{
	rb::EventHeap<16> Heap;
	rb::QueuedEvent A;
	A.Time = 1.0;
	A.Tier = rb::EventTier::Transition;
	A.BallA = 3;
	rb::QueuedEvent B = A;
	B.Tier = rb::EventTier::BallBall;
	B.BallA = 5;
	rb::QueuedEvent C = A;
	C.Time = 0.5;
	rb::QueuedEvent D = A;
	D.Time = 2.0;
	D.FeatureSub = 1;
	rb::QueuedEvent E = D;
	E.FeatureSub = 0;
	RB_REQUIRE(Heap.Push(A) && Heap.Push(B) && Heap.Push(C) && Heap.Push(D) && Heap.Push(E));
	RB_CHECK(Heap.Top().Time == 0.5);
	Heap.Pop();
	RB_CHECK(Heap.Top().Tier == rb::EventTier::BallBall); // same time: ball-ball before transitions
	Heap.Pop();
	RB_CHECK(Heap.Top().BallA == 3);
	Heap.Pop();
	RB_CHECK(Heap.Top().FeatureSub == 0); // full key incl. the sub-index
	RB_CHECK(Heap.Compact([](const rb::QueuedEvent&) { return false; }) == 0);
}

RB_TEST(Arch_RandomIsDeterministic)
{
	rb::Rng A(42);
	rb::Rng B(42);
	for (int i = 0; i < 100; ++i)
	{
		RB_CHECK(A.NextU64() == B.NextU64());
	}
	rb::FixedVector<int, 4> V;
	RB_CHECK(V.PushBack(1) && V.PushBack(2) && V.PushBack(3) && V.PushBack(4));
	RB_CHECK(!V.PushBack(5));
	RB_CHECK(V.Size() == 4);
}

RB_TEST(Arch_StubsLinkAndReturnNeutralValues)
{
	rb::Simulator Sim;
	rb::SimInput Input;
	rb::ShotResult Result;
	const rb::SimStatus Status = Sim.Run(Input, Result);
	RB_CHECK(Status == rb::SimStatus::NotImplemented || Status == rb::SimStatus::InvalidInput || Status == rb::SimStatus::Ok);
	RB_CHECK(rb::CoreVersion() != nullptr);
}

RB_TEST(Arch_BallSpecDefaultIsExactSolidSphere)
{
	const rb::BallSpec Default;
	const rb::BallSpec Made = rb::MakeBallSpec(rb::kDefaultBallRadius, rb::kDefaultBallMass);
	RB_CHECK(Default.Radius == Made.Radius && Default.Mass == Made.Mass && Default.Inertia == Made.Inertia); // bitwise
	RB_CHECK(rb::kBallInertia == Made.Inertia);
	RB_CHECK_NEAR(rb::InertiaFactor(Default), 0.4, 1e-15);
	RB_CHECK_NEAR(Default.Inertia, 5.5555809e-5, 1e-11); // EQP T-UNIT-2
	RB_CHECK_NEAR(rb::SlideDuration(1.0, 0.2, 9.80665), 2.0 / (7.0 * 0.2 * 9.80665), 1e-15);
	const rb::Vec3 L = rb::CoriolisInvariant({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 0.028575);
	RB_CHECK_NEAR(L.x, 5.0 / 7.0, 1e-15);
}

RB_TEST(Arch_MakePhysicsParamsIsTheSingleTableSource)
{
	const rb::PhysicsParams Pro = rb::MakePhysicsParams(rb::kTableNineFootPro);
	RB_CHECK(Pro.Origin == rb::ParamsOrigin::Table);
	RB_CHECK(Pro.Cloth.SlidingFriction == rb::kClothWorstedFast.SlidingFriction && Pro.Cloth.RollingResistance == rb::kClothWorstedFast.RollingResistance);
	RB_CHECK(Pro.Cushion.FacingRestitutionScale == 1.0);
	const rb::PhysicsParams Bar = rb::MakePhysicsParams(rb::kTableSevenFootBar);
	RB_CHECK(Bar.Cloth.SlidingFriction == rb::kClothNappedBar.SlidingFriction);
	RB_CHECK(Bar.Cushion.FacingRestitutionScale == 0.85);
	RB_CHECK(Bar.PocketContacts.LinerRestitution == rb::kTableSevenFootBar.LinerRestitution);
	const rb::PhysicsParams Again = rb::MakePhysicsParams(rb::kTableSevenFootBar);
	for (int i = 0; i < rb::PhysicsParamCount(); ++i)
	{
		double A = 0.0;
		double B = 0.0;
		RB_CHECK(rb::GetPhysicsParam(Bar, rb::PhysicsParamAt(i).Key, A) && rb::GetPhysicsParam(Again, rb::PhysicsParamAt(i).Key, B));
		RB_CHECK(A == B); // deterministic
	}
}

RB_TEST(Arch_ParamTableRoundTrip)
{
	rb::PhysicsParams P = rb::MakePhysicsParams(rb::kTableNineFootPro);
	RB_CHECK(rb::PhysicsParamCount() >= 80);
	for (int i = 0; i < rb::PhysicsParamCount(); ++i)
	{
		const rb::PhysicsParamInfo Info = rb::PhysicsParamAt(i);
		RB_CHECK(Info.Key[0] != '\0');
		for (int j = 0; j < i; ++j)
		{
			RB_CHECK(std::strcmp(Info.Key, rb::PhysicsParamAt(j).Key) != 0); // unique keys
		}
		double Value = 0.0;
		RB_REQUIRE(rb::GetPhysicsParam(P, Info.Key, Value));
		RB_CHECK(rb::SetPhysicsParam(P, Info.Key, Value));
		double Again = 0.0;
		RB_CHECK(rb::GetPhysicsParam(P, Info.Key, Again) && Again == Value);
	}
	RB_CHECK(rb::SetPhysicsParam(P, "cloth.mu_s", 0.25) && P.Cloth.SlidingFriction == 0.25);
	RB_CHECK(!rb::SetPhysicsParam(P, "no.such.key", 1.0));
	RB_CHECK(!rb::SetPhysicsParam(P, "cushion.model", 7.0));        // enum out of range
	RB_CHECK(!rb::SetPhysicsParam(P, "slate.n_max", 2.5));          // not integral
	RB_CHECK(!rb::SetPhysicsParam(P, "cushion.mathavan_split", 2.0)); // not a bool
	RB_CHECK(rb::PhysicsParamAt(-1).Key[0] == '\0');
}

RB_TEST(Arch_RulesTableCarriesPerBallRadii)
{
	const rb::rules::RulesTable T = rb::rules::MakeRulesTable(2.54, 1.27, 0.028575);
	for (double R : T.BallRadius)
	{
		RB_CHECK(R == 0.028575);
	}
	RB_CHECK(T.NominalBallRadius == 0.028575);
	RB_CHECK(T.PocketCount == 0);
	const rb::ShotStartSnapshot Start;
	RB_CHECK(Start.Radius[0] == 0.0);
}

RB_TEST(Arch_AssertCompilesAwayWithoutSideEffects)
{
	int Calls = 0;
	RB_ASSERT(++Calls > 0 || true);
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	RB_CHECK(Calls == 1);
#else
	RB_CHECK(Calls == 0);
#endif
}

// ------------------------------------------------------------------------------------------------------------------
// Architecture v1.2: human-factors integration (rb::human, table tilt, chalk-mark cling). The numbered HF-* tests are
// ported by their packages (architecture.md 17.12); these tests pin the contract defaults and the neutral stubs.
// ------------------------------------------------------------------------------------------------------------------

static_assert(rb::human::StreakSlot(rb::human::NoiseChannel::TipA) == 0, "streak slots of the per-shot channels");
static_assert(rb::human::StreakSlot(rb::human::NoiseChannel::Flinch) == 4, "streak slots of the per-shot channels");
static_assert(rb::human::StreakSlot(rb::human::NoiseChannel::HandAim) == 5, "streak slots of the synthetic hand");
static_assert(rb::human::StreakSlot(rb::human::NoiseChannel::HandSpeed) == rb::human::kStreakChannelCount - 1, "streak slots of the synthetic hand");
static_assert(rb::human::StreakSlot(rb::human::NoiseChannel::HandPause) == -1, "plain synthetic-hand channels are not guarded");
static_assert(rb::human::StreakSlot(rb::human::NoiseChannel::DriftLat) == -1, "watchable processes are not guarded");
static_assert(rb::human::StreakChannelAt(6) == rb::human::NoiseChannel::HandSteer, "slot <-> channel");
static_assert(rb::human::kStreakWindow == 7 && rb::human::kStreakMaxPerEighth == 2, "Q1: no eighth more than twice in any 8 consecutive draws");
static_assert(3 * rb::human::kStreakMaxPerEighth <= rb::human::kStreakWindow && 4 * rb::human::kStreakMaxPerEighth > rb::human::kStreakWindow,
	"Q1: at most 3 eighths can be excluded, so the next draw is never known");
static_assert(rb::human::EighthOf(0.0) == 0 && rb::human::EighthOf(0.99999999999999989) == 7, "eighths of [0, 1)");
static_assert(rb::human::U01(~0ull) < 1.0 && rb::human::U01(0ull) == 0.0, "U01 in [0, 1)");
static_assert(rb::kTiltRefreshFeature > rb::kTransitionFeature, "a refresh sorts after a transition of the same ball");
static_assert(rb::human::kNoisePhiLo == 0.0062096653257761 && rb::human::kNoiseTruncation == 2.5, "truncation at +-2.5 sigma (3.2)");
static_assert((rb::human::kWatchableChannelMask & rb::human::kPerShotChannelMask) == 0u, "channel masks are disjoint");
static_assert(rb::human::ChannelBit(rb::human::NoiseChannel::Flinch) == (1u << 9), "channel bits");

RB_TEST(Arch_TiltAndClingDefaultsAreNeutral)
{
	const rb::PhysicsParams P;
	RB_CHECK(rb::IsLevel(P.Tilt)); // HF-B10: every MOT/COL/VAL test runs on a level table
	RB_CHECK(P.Tilt.Tolerance == 5e-5 && P.Tilt.RefreshMaxInterval == 2.0 && P.Tilt.NapResistance == 0.0);
	RB_CHECK(!P.ChalkCling);
	RB_CHECK(P.BallBall.ClingFactor == 1.0);
	RB_CHECK(P.BallBall.ChalkClingFactor == 2.5);
	const rb::SimBall Ball;
	RB_CHECK(Ball.ChalkMarks.IsEmpty());
	const rb::MotionSegment Seg;
	RB_CHECK(!Seg.Tilt.Active && !Seg.Tilt.EndsInRefresh);
	rb::TiltParams Tilt;
	Tilt.Slope = {0.0, 1e-3};
	RB_CHECK(!rb::IsLevel(Tilt));
	const rb::Vec2 Gt = rb::InPlaneGravity(Tilt, 9.80665);
	RB_CHECK(Gt.x == 0.0 && Gt.y == -9.80665e-3);

	// The table condition is the only extra input of the single parameter source; the default changes nothing.
	const rb::PhysicsParams Plain = rb::MakePhysicsParams(rb::kTableSevenFootBar);
	const rb::PhysicsParams Level = rb::MakePhysicsParams(rb::kTableSevenFootBar, rb::TableCondition{});
	rb::TableCondition Bar;
	Bar.Slope = {1.5e-3, -0.5e-3};
	Bar.BallCling = 1.3;
	Bar.ChalkCling = true;
	const rb::PhysicsParams Tilted = rb::MakePhysicsParams(rb::kTableSevenFootBar, Bar);
	RB_CHECK(Tilted.Origin == rb::ParamsOrigin::Table);
	RB_CHECK(Tilted.Tilt.Slope.x == 1.5e-3 && Tilted.Tilt.Slope.y == -0.5e-3);
	RB_CHECK(Tilted.BallBall.ClingFactor == 1.3 && Tilted.ChalkCling);
	int Differences = 0;
	for (int i = 0; i < rb::PhysicsParamCount(); ++i)
	{
		const char* Key = rb::PhysicsParamAt(i).Key;
		double A = 0.0;
		double B = 0.0;
		double C = 0.0;
		RB_CHECK(rb::GetPhysicsParam(Plain, Key, A) && rb::GetPhysicsParam(Level, Key, B) && rb::GetPhysicsParam(Tilted, Key, C));
		RB_CHECK(A == B);
		Differences += A != C ? 1 : 0;
	}
	RB_CHECK(Differences == 4); // tilt.slope_x, tilt.slope_y, ballball.k_cling, ballball.chalk_cling
	double Value = 0.0;
	RB_CHECK(rb::GetPhysicsParam(Tilted, "tilt.slope_x", Value) && Value == 1.5e-3);
	RB_CHECK(rb::GetPhysicsParam(Tilted, "ballball.chalk_cling", Value) && Value == 1.0);
}

RB_TEST(Arch_ScalarFunnelExpm1Log1pCbrt)
{
	RB_CHECK_NEAR(rb::Expm1(1e-12), 1e-12 + 5e-25, 1e-27); // x + x^2/2: exp(x) - 1 would be off by ~1e-16 (1e-4 relative)
	RB_CHECK_NEAR(rb::Expm1(-1.0), rb::Exp(-1.0) - 1.0, 1e-16);
	RB_CHECK_NEAR(rb::Log1p(1e-12), 1e-12 - 5e-25, 1e-27);
	RB_CHECK_NEAR(rb::Cbrt(27.0), 3.0, 1e-15);
	RB_CHECK(rb::Cbrt(-8.0) == -2.0);
	RB_CHECK(rb::Floor(-0.5) == -1.0 && rb::Floor(2.0) == 2.0);
}

RB_TEST(Arch_EventQueueTiltRefreshAfterContacts)
{
	// A refresh at exactly the time of a contact of the same ball pops after it (tier Transition) and after a motion
	// transition key of the same ball; it never forms an exact-simultaneity group (architecture 8.3, 8.11).
	rb::EventHeap<8> Heap;
	rb::QueuedEvent Refresh;
	Refresh.Time = 1.25;
	Refresh.Tier = rb::EventTier::Transition;
	Refresh.Kind = rb::QueuedEventKind::TiltRefresh;
	Refresh.BallA = 2;
	Refresh.FeatureKind = rb::kTiltRefreshFeature;
	rb::QueuedEvent Transition = Refresh;
	Transition.Kind = rb::QueuedEventKind::Transition;
	Transition.FeatureKind = rb::kTransitionFeature;
	rb::QueuedEvent Contact = Refresh;
	Contact.Kind = rb::QueuedEventKind::BallBall;
	Contact.Tier = rb::EventTier::BallBall;
	Contact.BallB = 5;
	Contact.FeatureKind = 0;
	RB_REQUIRE(Heap.Push(Refresh) && Heap.Push(Transition) && Heap.Push(Contact));
	RB_CHECK(Heap.Top().Kind == rb::QueuedEventKind::BallBall);
	Heap.Pop();
	RB_CHECK(Heap.Top().Kind == rb::QueuedEventKind::Transition);
	Heap.Pop();
	RB_CHECK(Heap.Top().Kind == rb::QueuedEventKind::TiltRefresh);
}

RB_TEST(Arch_HumanFactorsDefaultsFromSpec)
{
	namespace h = rb::human;
	// 3.3 table (values at attribute 25, rho) and the Q1 switch.
	const h::HumanParams P;
	RB_CHECK(P.NoiseScale == 1.0 && P.ChannelMask == 0u && P.StreakGuard);
	RB_CHECK(P.WarpSightLength == 0.45);
	RB_CHECK(P.DriftSigma == 0.9e-3 && P.DriftRho == 0.2 && P.DriftVerticalRatio == 0.5);
	RB_CHECK(P.TremorSigma == 0.03e-3);
	RB_CHECK(P.TipASigma == 0.20e-3 && P.TipARho == 0.25 && P.TipBSigma == 1.5e-3 && P.TipBRho == 0.2);
	RB_CHECK(P.OffsetKappa == 0.06 && P.OffsetKappaRho == 0.25);
	RB_CHECK_NEAR(P.ElevationSigma * rb::kRadToDeg, 0.4, 1e-12);
	RB_CHECK(P.SpeedSigma == 0.05 && P.SpeedRho == 0.3);
	RB_CHECK(P.FlinchLoss == 0.08 && P.PressureGainMax == 4.0 && P.GripDrop == 1.5e-3 && P.NerveRho == 0.125);
	RB_CHECK(P.DriftPressureExponent == 1.0 / 3.0 && P.SpeedPressureExponent == 0.5); // v1.1 refit (9.2 item 3)
	RB_CHECK(P.OffsetClamp == 0.90 && P.OffsetClamp < rb::kCueOffsetValidLimit);
	RB_CHECK(P.MaxSpeed == 12.0 && P.RampDuration == 0.1);
	RB_CHECK(P.Rules.Frozen == 1.0e-4 && P.Rules.FrozenEnvelope == 5.0e-3);
	RB_CHECK(h::BridgeSpecFor(h::BridgeType::Closed).BaseFactor == 1.0 && h::BridgeSpecFor(h::BridgeType::Closed).SlipSpeed == 6.0);
	RB_CHECK(h::BridgeSpecFor(h::BridgeType::Elevated).BaseFactor == 2.0 && h::BridgeSpecFor(h::BridgeType::Elevated).SlipSpeed == 2.5);
	RB_CHECK(h::BridgeSpecFor(h::BridgeType::Mechanical).BaseFactor == 1.8 && h::BridgeSpecFor(h::BridgeType::Mechanical).SlipSpeed == 3.0);
	RB_CHECK_NEAR(h::SkillScale(25.0, 0.2), 1.0, 1e-15);
	RB_CHECK_NEAR(h::SkillScale(100.0, 0.2), 0.2, 1e-15);
	RB_CHECK_NEAR(h::SkillScale(150.0, 0.2), 0.2, 1e-15); // clamped

	// 3.2 keys: 64-bit rollout shooter key (widened before the shift), channel masks.
	h::NoiseKey Key;
	Key.ShooterId = 0xFFFFFFFFu;
	RB_CHECK(h::ShooterKey(Key) == 0xFFFFFFFFull && !h::IsRolloutKey(Key));
	const h::NoiseKey Rollout = h::RolloutKey(Key, 2);
	RB_CHECK(Rollout.Purpose == 3u && h::ShooterKey(Rollout) == 0x3FFFFFFFFull && h::IsRolloutKey(Rollout));
	h::NoiseKey Address = Key;
	Address.ShotIndex = 0xFFFFFFFFu;
	RB_CHECK(Address.AddressIndex == 0u && h::ProcessShotKey(Address) == 0xFFFFFFFFull); // first get-down: the v1.2 key (HF-T04)
	Address.AddressIndex = 1u;
	RB_CHECK(h::ProcessShotKey(Address) == 0x1FFFFFFFFull); // a new get-down: new drift / tremor processes (3.2)

	// 4.1 chalk grades, 4.2 tip defaults.
	RB_CHECK(h::ChalkGradeSpecFor(h::ChalkGrade::RailRat).HitsPerGrade == 10.0 && h::ChalkGradeSpecFor(h::ChalkGrade::RailRat).Cap == 0.7);
	RB_CHECK(h::ChalkGradeSpecFor(h::ChalkGrade::OldBlue).HitsPerGrade == 18.0);
	RB_CHECK(h::ChalkGradeSpecFor(h::ChalkGrade::Tensile).HitsPerGrade == 30.0 && h::ChalkGradeSpecFor(h::ChalkGrade::Glasshouse).HitsPerGrade == 45.0);
	const h::TipState Tip;
	const h::TipParams TipModel;
	RB_CHECK(Tip.DomeRadius == 0.0106 && Tip.Width == 0.01275 && Tip.Coverage[0] == 1.0 && Tip.Coverage[6] == 1.0);
	RB_CHECK(TipModel.FreshFriction == 0.60 && TipModel.BareFriction == 0.35 && TipModel.RimFriction == 0.30 && TipModel.FerruleFriction == 0.20);
	RB_CHECK(h::BarChalkCube().BarCube && h::BarChalkCube().Grade == h::ChalkGrade::RailRat);
	const h::MarkParams Marks;
	RB_CHECK(Marks.Radius == 2.5e-3 && Marks.MiscueRadius == 4.0e-3);

	// Section 7: every product-owner decision (Q1 above; Q2 alcohol cosmetic for V1 with the intoxication hook, Q3 hidden
	// numbers, Q4 hot-seat guests, Q5 chores, Q6 money games in, Q7 no LD unlock gate).
	const h::ProductConfig Config;
	RB_CHECK(Config.Alcohol == h::AlcoholMode::CosmeticOnly);
	RB_CHECK(h::StrokeIntoxication(Config, 0.8) == 0.0); // V1: drinks never change a stroke
	h::ProductConfig Drunk = Config;
	Drunk.Alcohol = h::AlcoholMode::Mechanic;            // the later hook: one switch, no core change
	RB_CHECK(h::StrokeIntoxication(Drunk, 0.8) == 0.8 && h::StrokeIntoxication(Drunk, 3.0) == 1.0 && h::StrokeIntoxication(Drunk, -1.0) == 0.0);
	RB_CHECK(h::StrokeSituation{}.Intoxication == 0.0);
	RB_CHECK(P.IntoxicationCalmLevel == 0.25 && P.IntoxicationCalm == 0.5 && P.IntoxicationDriftGain == 1.0 && P.IntoxicationTremorGain == 1.0);
	RB_CHECK(Config.Attributes == h::AttributeVisibility::Hidden && !h::ShowAttributeNumbers(Config));
	RB_CHECK(Config.Money == h::MoneyGames::SideBetsAndHustling && h::MoneyGamesAllowed(Config));
	RB_CHECK(h::MoneyGameStakes(5.0, 100.0) == h::kStakesMoneyOrLeague && h::MoneyGameStakes(60.0, 100.0) == h::kStakesFinal);
	RB_CHECK_NEAR(h::MoneyGameStakes(30.0, 100.0), 0.8, 1e-12);
	RB_CHECK(h::MoneyGameStakes(1.0, 0.0) == h::kStakesFinal); // all in
	RB_CHECK(h::HotSeatGuestAttributes(Config).Nerve == 50.0 && h::HotSeatGuestAttributes(Config).Steadiness == 50.0 && !Config.HotSeatGuestEarnsXp);
	RB_CHECK(h::DefaultChoreSpeed(Config, true) == h::ChoreSpeed::Full && h::DefaultChoreSpeed(Config, false) == h::ChoreSpeed::Brisk);
	RB_CHECK(h::LowDeflectionShaftUnlocked(Config, h::UniformAttributes(0.0)));

	// 5.4 presets.
	const h::AssistSettings Pure = h::GetAssistSettings(h::DifficultyPreset::Pure);
	const h::AssistSettings Real = h::GetAssistSettings(h::DifficultyPreset::Real);
	const h::AssistSettings Assisted = h::GetAssistSettings(h::DifficultyPreset::Assisted);
	const h::AssistSettings Relaxed = h::GetAssistSettings(h::DifficultyPreset::Relaxed);
	RB_CHECK(Pure.AnyTipContactIsShot && !Pure.ObviousCallAssist && Pure.NoiseScale == 1.0 && Pure.SteeringGain == 0.25);
	RB_CHECK(!Real.AnyTipContactIsShot && Real.ObviousCallAssist && Real.NoiseScale == 1.0 && Real.Pressure == h::PressureMode::On);
	RB_CHECK(Assisted.NoiseScale == 0.6 && Assisted.SteeringGain == 0.10 && Assisted.Pressure == h::PressureMode::Subtle && Assisted.StrokeReportAlways);
	RB_CHECK(Relaxed.NoiseScale == 0.3 && Relaxed.SteeringGain == 0.0 && Relaxed.Pressure == h::PressureMode::Off && Relaxed.AimLine == h::AimLineAssist::Long);
	RB_CHECK(h::NoiseScaleFor(h::ImperfectionSetting::Scaled) == Assisted.NoiseScale && h::NoiseScaleFor(h::ImperfectionSetting::Low) == Relaxed.NoiseScale);
	RB_CHECK(h::NoiseScaleFor(h::ImperfectionSetting::Off) == 0.0);
}

RB_TEST(Arch_HumanStubsLinkAndStayNeutral)
{
	namespace h = rb::human;
	// Every rb::human entry point links (stubs of WP-11) and returns a neutral value; the intended stroke passes through
	// unchanged until WP-11 lands (the NoiseScale-0 identity is HF-T08).
	h::IntendedStroke Intended;
	Intended.Azimuth = 0.1;
	Intended.Speed = 2.0;
	const rb::CueSpec Cue = rb::kCuePlaying19oz;
	const h::NoiseKey Key;
	const h::NoiseHistory History = h::RebuildNoiseHistory(Key.MatchSeed, h::ShooterKey(Key), Key.ShooterShotIndex);
	RB_CHECK(History.NextIndex == 0u);
	const h::ExecutedStroke Stroke = h::ExecuteStroke(Intended, h::ShooterAttributes{}, h::StrokeSituation{}, h::TipState{}, h::CueBodyState{}, Cue,
		rb::BallSpec{}, rb::Vec3{}, nullptr, 0, Key, History, h::HumanParams{});
	RB_CHECK(Stroke.Error == rb::ErrorCode::NotImplemented || Stroke.Error == rb::ErrorCode::Ok);
	RB_CHECK(Stroke.Strike.Cue.Mass == Cue.Mass);
	const h::HandPose Pose = h::SampleHand(Intended, h::ShooterAttributes{}, h::StrokeSituation{}, h::CueBodyState{}, Cue, rb::BallSpec{}, Key, History,
		h::HumanParams{}, 1.5);
	RB_CHECK(Pose.Time == 1.5);
	h::NoiseHistory Advanced = History;
	h::AdvanceNoiseHistory(Advanced);
	RB_CHECK(Advanced.NextIndex == 1u);
	const h::AiCharacter Character{h::GetAiProfile(h::AiProfileId::LeaguePlayer), 7u};
	RB_CHECK(Character.Profile.Id == h::AiProfileId::LeaguePlayer);
	h::PlannedStroke Plan;
	Plan.Speed = 1.5;
	RB_CHECK(h::SyntheticHand(Plan, Character, h::StrokeSituation{}, 0.028575, Key, History, h::HumanParams{}).Speed == 1.5);
	rb::PhysicsParams Physics = rb::MakePhysicsParams(rb::kTableSevenFootBar);
	rb::SimBall Balls[rb::kMaxBalls];
	h::ApplyDiagnosisStep(h::DiagnosisStepAt(1), Physics, Balls);
	RB_CHECK(h::VenueBallSetSeed(1u, 0) == h::HashKeys(1u, h::kVenueBallSetPurpose, 0u));
	const rb::TableCondition Condition = h::MakeVenueTableCondition(1u, 0, h::VenueKind::DiveBar, true, false);
	RB_CHECK(!Condition.ChalkCling);
}
