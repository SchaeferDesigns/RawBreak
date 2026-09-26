// Owner: WP-11 (player model). human-factors 3.9 (counterfactual diagnosis, stroke report): ARCH A-HUM-4 and HF-B06
// (Integ_: the constructed misses need the simulator of WP-1..WP-7 to judge make or miss).

#include "Human/HumanTestUtil.h"

#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Simulator.h"

#include <chrono>
#include <cmath>

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

// A-HUM-4: the fixed order of 3.9 and the overrides of each step.
RB_TEST(ARCH_HUM4_DiagnosisOrderAndOverrides)
{
	const MissCause Order[kDiagnosisStepCount] = {MissCause::Equipment, MissCause::Table, MissCause::HandDrift, MissCause::TipPlacementSpeed,
		MissCause::HumanLayer};
	for (int i = 0; i < kDiagnosisStepCount; ++i)
	{
		RB_CHECK(DiagnosisStepAt(i).Cause == Order[i]);
	}
	RB_CHECK(DiagnosisStepAt(5).Cause == MissCause::Input && DiagnosisStepAt(-1).Cause == MissCause::Input);
	RB_CHECK(DiagnosisStepAt(0).FreshChalkNoWarp && !DiagnosisStepAt(0).LevelCleanTable && DiagnosisStepAt(0).ChannelMask == 0u);
	RB_CHECK(DiagnosisStepAt(1).LevelCleanTable && !DiagnosisStepAt(1).FreshChalkNoWarp && DiagnosisStepAt(1).ChannelMask == 0u);
	RB_CHECK(DiagnosisStepAt(2).ChannelMask == kWatchableChannelMask);
	RB_CHECK(DiagnosisStepAt(3).ChannelMask == kPerShotChannelMask);
	RB_CHECK(DiagnosisStepAt(4).ChannelMask == (kWatchableChannelMask | kPerShotChannelMask));
	RB_CHECK((kWatchableChannelMask & ChannelBit(NoiseChannel::WarpRoll)) == 0u && (kPerShotChannelMask & ChannelBit(NoiseChannel::WarpRoll)) == 0u);

	// Step 1: fresh chalk (c_z = 1, not the bar cube's cap) and no warp; tip shape, glaze and overhang unchanged.
	HumanParams Human;
	TipState Tip;
	Tip.Coverage[2] = 0.1;
	Tip.Glaze = 0.4;
	Tip.Overhang = 0.3e-3;
	Tip.DomeRadius = 0.015;
	CueBodyState Body;
	Body.BowSag = 2e-3;
	Body.WarpKnown = false;
	ApplyDiagnosisStep(DiagnosisStepAt(0), Human, Tip, Body);
	for (int z = 0; z < kTipZoneCount; ++z)
	{
		RB_CHECK(Tip.Coverage[z] == 1.0);
	}
	RB_CHECK(Body.BowSag == 0.0 && Tip.Glaze == 0.4 && Tip.Overhang == 0.3e-3 && Tip.DomeRadius == 0.015 && Human.ChannelMask == 0u);
	// Steps 3-5 add their channel mask (single changes against the real shot: applied to copies).
	HumanParams Masked;
	Masked.ChannelMask = ChannelBit(NoiseChannel::HandAim);
	TipState TipCopy;
	CueBodyState BodyCopy;
	BodyCopy.BowSag = 1e-3;
	ApplyDiagnosisStep(DiagnosisStepAt(2), Masked, TipCopy, BodyCopy);
	RB_CHECK(Masked.ChannelMask == (kWatchableChannelMask | ChannelBit(NoiseChannel::HandAim)) && BodyCopy.BowSag == 1e-3);

	// Step 2: a level, clean table: Slope 0, NapPseudoSlope 0, ClingFactor 1, no chalk marks, ChalkCling off.
	PhysicsParams Physics = MakePhysicsParams(kTableSevenFootBar, TableCondition{{1.5e-3, -0.7e-3}, 1.3, true});
	Physics.Tilt.NapPseudoSlope = {2e-4, 0.0};
	static SimBall Balls[kMaxBalls];
	for (int i = 0; i < kMaxBalls; ++i)
	{
		Balls[i].ChalkMarks.Clear();
		Balls[i].ChalkMarks.PushBack(ChalkMark{{1.0, 0.0, 0.0}, 0.8, 2.5e-3});
	}
	PhysicsParams Untouched = Physics;
	ApplyDiagnosisStep(DiagnosisStepAt(0), Untouched, Balls); // not a table step: nothing changes
	RB_CHECK(Untouched.Tilt.Slope.x == 1.5e-3 && Balls[3].ChalkMarks.Size() == 1);
	ApplyDiagnosisStep(DiagnosisStepAt(1), Physics, Balls);
	RB_CHECK(IsLevel(Physics.Tilt) && Physics.BallBall.ClingFactor == 1.0 && !Physics.ChalkCling);
	for (int i = 0; i < kMaxBalls; ++i)
	{
		RB_CHECK(Balls[i].ChalkMarks.IsEmpty());
	}
	ApplyDiagnosisStep(DiagnosisStepAt(1), Physics, nullptr); // no ball list: tolerated
	HumanParams HumanStep2;
	TipState TipStep2;
	TipStep2.Coverage[1] = 0.2;
	CueBodyState BodyStep2;
	BodyStep2.BowSag = 1e-3;
	ApplyDiagnosisStep(DiagnosisStepAt(1), HumanStep2, TipStep2, BodyStep2); // a table step changes nothing on the human side
	RB_CHECK(TipStep2.Coverage[1] == 0.2 && BodyStep2.BowSag == 1e-3 && HumanStep2.ChannelMask == 0u);
}

RB_TEST(Human_StrokeReportShares)
{
	Setup S = MakeS0();
	S.CueBody.BowSag = 2e-3;
	S.CueBody.WarpKnown = false;
	S.Cue = kCueHouse19oz;
	const ExecutedStroke X = Execute(S);
	StrokeDelta Input;
	Input.Azimuth = 2e-3;
	const StrokeShares Shares = ComputeStrokeShares(X, Input, S.Cue, S.Ball);
	RB_CHECK_NEAR(Shares.Input + Shares.Hand + Shares.Equipment, 1.0, 1e-15);
	RB_CHECK(Shares.Input > 0.0 && Shares.Hand > 0.0 && Shares.Equipment > 0.0);
	// The warp alone is equipment; the hand alone is the hand; nothing at all gives no shares.
	Setup Quiet = S;
	Quiet.Params.NoiseScale = 0.0;
	const StrokeShares OnlyWarp = ComputeStrokeShares(Execute(Quiet), StrokeDelta{}, Quiet.Cue, Quiet.Ball);
	RB_CHECK(OnlyWarp.Equipment == 1.0 && OnlyWarp.Hand == 0.0 && OnlyWarp.Input == 0.0);
	Quiet.CueBody.BowSag = 0.0;
	const StrokeShares None = ComputeStrokeShares(Execute(Quiet), StrokeDelta{}, Quiet.Cue, Quiet.Ball);
	RB_CHECK(None.Input == 0.0 && None.Hand == 0.0 && None.Equipment == 0.0);
	// Budget-model direction error: a lateral axis shift is partly cancelled by squirt (natural pivot, 3.5).
	StrokeDelta Shift;
	Shift.Azimuth = 1e-3;
	Shift.AxisA = -1e-3 * (0.2 + kR) / kR;
	Setup Hand = MakeS0();
	Hand.Params.NoiseScale = 0.0;
	const ExecutedStroke Clean = Execute(Hand);
	const StrokeShares Two = ComputeStrokeShares(Clean, Shift, Hand.Cue, Hand.Ball);
	RB_CHECK(Two.Input == 1.0);
}

namespace
{
	struct ShotSetup
	{
		Setup Human;
		PhysicsParams Physics;
		Vec2 Target;      // pocket mouth
		double Aim = 0.0; // azimuth from the cue ball to the pocket
	};

	SimInput& Input()
	{
		static SimInput Storage; // large: kept off the stack
		return Storage;
	}

	bool Pocketed(Simulator& Sim, const TableGeometry& Table, const PhysicsParams& Physics, const Vec3& CueBall, const CueStrikeInput& Strike, ShotResult& Result)
	{
		SimInput& In = Input();
		In = SimInput{};
		In.Table = &Table;
		In.Params = Physics;
		In.Balls[0].InPlay = true;
		In.Balls[0].State.Position = CueBall;
		StrikeRequest Request;
		Request.Ball = 0;
		Request.Input = Strike;
		In.Strikes.PushBack(Request);
		In.Record.Trajectories = false;
		In.Record.EventStates = false;
		In.Record.LogTransitions = false;
		In.Record.LogObservers = false;
		if (Sim.Run(In, Result) != SimStatus::Ok)
		{
			return false;
		}
		return Result.Finals[0].Status == BallFinalStatus::Pocketed;
	}

	// The game's diagnosis loop (3.9): each step one change against the real shot, first make names the cause.
	MissCause Diagnose(Simulator& Sim, const TableGeometry& Table, const ShotSetup& Shot, ShotResult& Result)
	{
		for (int i = 0; i < kDiagnosisStepCount; ++i)
		{
			const DiagnosisStep Step = DiagnosisStepAt(i);
			Setup Human = Shot.Human;
			PhysicsParams Physics = Shot.Physics;
			ApplyDiagnosisStep(Step, Human.Params, Human.Tip, Human.CueBody);
			ApplyDiagnosisStep(Step, Physics, nullptr);
			const ExecutedStroke X = Execute(Human);
			if (Pocketed(Sim, Table, Physics, Human.BallPosition, X.Strike, Result))
			{
				return Step.Cause;
			}
		}
		return MissCause::Input;
	}
}

// HF-B01 (core half, shot part): the same input log, keys and state give the same executed stroke and a bitwise identical
// shot (event-log hash) from two independent simulators.
RB_TEST(Integ_HF_B01_ShotHashReproducible)
{
	static TableGeometry Table;
	RB_REQUIRE(BuildTableGeometry(kTableNineFootPro, Table) == ErrorCode::Ok);
	std::uint64_t Hashes[2] = {};
	for (int Run = 0; Run < 2; ++Run)
	{
		Setup S = MakeS0();
		S.BallPosition = {-0.6, 0.1, kR};
		S.Intended.AxisOffsetA = 0.2;
		const ExecutedStroke X = Execute(S);
		Simulator Sim;
		ShotResult Result;
		Pocketed(Sim, Table, MakePhysicsParams(kTableNineFootPro), S.BallPosition, X.Strike, Result);
		RB_REQUIRE(Result.Status == SimStatus::Ok);
		std::uint64_t H = StrokeHash(X);
		for (const ShotEvent& Event : Result.Events)
		{
			H = HashKeys(H, Bits(Event.Time), static_cast<std::uint64_t>(Event.Type), static_cast<std::uint64_t>(static_cast<std::uint8_t>(Event.A)),
				Bits(Event.NormalSpeed), Bits(Event.NormalImpulse));
		}
		const BallFinal& Final = Result.Finals[0];
		Hashes[Run] = HashKeys(H, Bits(Final.State.Position.x), Bits(Final.State.Position.y), Bits(Final.Time));
	}
	RB_CHECK(Hashes[0] == Hashes[1]);
}

// HF-B06: every miss yields a diagnosis within 3 s; constructed cases for equipment, table, hand drift and input.
RB_TEST(Integ_HF_B06_DiagnosisOfConstructedMisses)
{
	static TableGeometry Table;
	RB_REQUIRE(BuildTableGeometry(kTableNineFootPro, Table) == ErrorCode::Ok);
	RB_REQUIRE(Table.Pockets.Size() == 6);
	const PocketGeometry& Pocket = Table.Pockets[0];
	Simulator Sim;
	ShotResult Result;

	ShotSetup Base;
	Base.Physics = MakePhysicsParams(kTableNineFootPro);
	Base.Target = Pocket.MouthMid;
	const Vec2 Start = Pocket.MouthMid - Pocket.Axis * 1.5;
	Base.Aim = std::atan2(Pocket.Axis.y, Pocket.Axis.x);
	Base.Human = MakeS0();
	Base.Human.BallPosition = {Start.x, Start.y, kR};
	Base.Human.Cue = kCuePlaying19oz;
	Base.Human.Intended.Azimuth = Base.Aim;
	Base.Human.Intended.Elevation = 0.0;
	Base.Human.Intended.Speed = 1.2;
	Base.Human.Params.NoiseScale = 0.0;

	// The clean shot goes in.
	RB_REQUIRE(Pocketed(Sim, Table, Base.Physics, Base.Human.BallPosition, Execute(Base.Human).Strike, Result));

	const auto Timed = [&](const ShotSetup& Shot, MissCause Expected) {
		RB_CHECK(!Pocketed(Sim, Table, Shot.Physics, Shot.Human.BallPosition, Execute(Shot.Human).Strike, Result)); // the real shot misses
		const auto T0 = std::chrono::steady_clock::now();
		const MissCause Cause = Diagnose(Sim, Table, Shot, Result);
		const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
		RB_CHECK(Cause == Expected);
		RB_CHECK(Seconds < 3.0);
	};

	// (1) Bare zone -> "equipment": right English a = 0.45 on a bare right side of the tip miscues; with fresh chalk it does not.
	{
		ShotSetup Shot = Base;
		Shot.Human.Intended.AxisOffsetA = 0.45 * (kR + Shot.Human.Tip.DomeRadius) / kR;
		// Aim compensated for the squirt of the fresh-chalk stroke (the planner's job), so the real shot fails only on chalk.
		BallState Rest;
		Rest.Position = Shot.Human.BallPosition;
		const StrikeResult Fresh = StrikeCueBall(Execute(Shot.Human).Strike, Rest, Shot.Human.Ball, ClothParamsFor(kTableNineFootPro.Cloth), SlateParams{},
			PinchParams{}, kStandardGravity, NumericsConfig{});
		Shot.Human.Intended.Azimuth += Base.Aim - std::atan2(Fresh.State.Velocity.y, Fresh.State.Velocity.x);
		RB_REQUIRE(Pocketed(Sim, Table, Shot.Physics, Shot.Human.BallPosition, Execute(Shot.Human).Strike, Result));
		Shot.Human.Tip.Coverage[3] = 0.0;
		Shot.Human.Tip.Coverage[4] = 0.0;
		Shot.Human.Tip.Coverage[5] = 0.0;
		RB_CHECK(Execute(Shot.Human).PredictedMiscue);
		Timed(Shot, MissCause::Equipment);
	}
	// (2) A slow shot missed only because of the slope -> "table".
	{
		ShotSetup Shot = Base;
		Shot.Human.Intended.Speed = 1.0;
		const Vec2 Across = PerpCcw(Pocket.Axis);
		Shot.Physics.Tilt.Slope = Across * 3e-3;
		RB_REQUIRE(ValidatePhysicsParams(Shot.Physics) == ErrorCode::Ok);
		Timed(Shot, MissCause::Table);
	}
	// (3) A drift-only perturbation (per-shot channels masked in the real shot) -> "hand drift".
	{
		ShotSetup Shot = Base;
		Shot.Human.Params.NoiseScale = 1.0;
		Shot.Human.Params.ChannelMask = kPerShotChannelMask;
		Shot.Human.Attributes = UniformAttributes(0.0);
		Shot.Human.Situation.Bridge = BridgeType::Elevated;
		Shot.Human.Situation.StanceDifficulty = 1.0;
		Shot.Human.Situation.OffHand = true;
		Shot.Human.Situation.BridgeLength = 0.10;
		Shot.Human.Intended.Elevation = 2.0 * kDegToRad;
		Shot.Human.Intended.Speed = 1.0;
		// A moment where the lateral drift is large and the vertical one small.
		const WatchableProcess Lat = MakeWatchableProcess(Shot.Human.Key, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
		const WatchableProcess Vert = MakeWatchableProcess(Shot.Human.Key, NoiseChannel::DriftVert, kDriftBandLo, kDriftBandHi);
		for (double T = 1.5; T < 8.0; T += 0.01)
		{
			if (std::fabs(ProcessValue(Lat, T)) > 1.2 && std::fabs(ProcessValue(Vert, T)) < 0.5)
			{
				Shot.Human.Intended.TimeDown = T;
				break;
			}
		}
		Timed(Shot, MissCause::HandDrift);
	}
	// (4) Zero human layer with a steered input 3 deg off the line -> "input".
	{
		ShotSetup Shot = Base;
		Shot.Human.Intended.Azimuth += 3.0 * kDegToRad;
		Timed(Shot, MissCause::Input);
	}
}
