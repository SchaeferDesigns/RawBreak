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
