// Simulation service tests (UE-6a acceptance, Docs/ue-architecture.md 13): table context build + validation, InputHash
// sensitivity, compact copy (< 1 MB for a break), ROB-10 against the standalone build (A5: break9 scenario, both hashes ==
// Tools/rbsim/examples/break9.hash), worker hand-off in the same frame (and at the next frame's Tick for a shot submitted
// after the subsystem ticked), worker results bitwise equal to RunShotBlocking, SubmitShot refused while busy,
// CancelInFlight and Deinitialize with a shot in flight.
// Owner: UE-6a.

#include "Simulation/RbShot.h"
#include "Simulation/RbSimScenarios.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Simulation/RbTableContext.h"
#include "Tests/RbTestFlags.h"

#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

#include "rb/Physics/ParamTable.h"
#include "rb/Version.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbSimulationTests
{
	// Scenarios: RbSimScenarios (the ROB-10 break9 scenario is built by the module itself, the same function the
	// rb.SimDemo dev command uses).
	bool MakeBreak9(FRbShotRequest& Out, FString& Error) { return RbSimScenarios::MakeBreak9(Out, Error); }
	bool MakeTwoBallShot(FRbShotRequest& Out, FString& Error) { return RbSimScenarios::MakeTwoBall(Out, Error); }

	// --- Deep result hash: every field of the result (ResultHash covers events, finals and two counters only) ---------

	struct FHasher
	{
		uint64 H = 0xCBF29CE484222325ull;
		void U(uint64 V)
		{
			for (int i = 0; i < 8; ++i)
			{
				H ^= (V >> (8 * i)) & 0xFFu;
				H *= 0x100000001B3ull;
			}
		}
		void D(double V)
		{
			uint64 B = 0;
			FMemory::Memcpy(&B, &V, sizeof(B));
			U(B);
		}
		void V2(const rb::Vec2& V) { D(V.x); D(V.y); }
		void V3(const rb::Vec3& V) { D(V.x); D(V.y); D(V.z); }
		void Q(const rb::Quat& V) { D(V.w); D(V.x); D(V.y); D(V.z); }
		void Id(rb::BallId B) { U(static_cast<uint8>(B)); }
		void State(const rb::BallState& S) { V3(S.Position); V3(S.Velocity); V3(S.Omega); U(static_cast<uint64>(S.State)); }
	};

	uint64 DeepResultHash(const rb::ShotResult& R)
	{
		FHasher X;
		X.U(RbShot::ResultHash(R));
		X.U(R.BallsInPlay);
		X.U(static_cast<uint64>(R.Strikes.Size()));
		for (const rb::StrikeOutcome& S : R.Strikes)
		{
			const rb::StrikeResult& T = S.Result;
			X.Id(S.Ball);
			X.U(static_cast<uint64>(T.Error));
			X.State(T.State);
			X.V3(T.VelocityAfterTip);
			X.V3(T.OmegaAfterTip);
			X.V3(T.ImpulseDirection);
			X.D(T.Impulse);
			X.D(T.SquirtAngle);
			X.D(T.Lambda);
			X.D(T.Rho);
			X.D(T.MiscueLimit);
			X.U(T.Miscue);
			X.D(T.SeparationMargin);
			X.D(T.CueSpeedAfter);
			X.D(T.ContactDuration);
			X.U(T.SlateContact);
			X.U(T.SlateStick);
		}
		X.U(R.Events.size());
		for (const rb::ShotEvent& E : R.Events)
		{
			X.U(static_cast<uint64>(E.From));
			X.U(static_cast<uint64>(E.To));
		}
		for (const rb::BallTrack& Track : R.Tracks)
		{
			X.U(Track.Segments.size());
			for (const rb::TrajectorySegment& S : Track.Segments)
			{
				const rb::MotionSegment& M = S.Motion;
				X.U(static_cast<uint64>(S.Kind));
				X.D(S.T1);
				X.V3(S.EndPosition);
				X.Q(S.Orientation0);
				X.U(static_cast<uint64>(M.State));
				X.D(M.T0);
				X.D(M.TauEnd);
				X.D(M.Radius);
				X.D(M.SupportZ);
				X.V3(M.Pos0);
				X.V3(M.Vel0);
				X.V3(M.Accel2);
				X.V3(M.Omega0);
				X.V3(M.OmegaDotH);
				X.D(M.OmegaZRate);
				X.D(M.OmegaZStopTau);
				X.U(M.Tilt.Active);
				X.U(M.Tilt.EndsInRefresh);
				X.V2(M.Tilt.X0);
				X.V2(M.Tilt.G);
				X.D(M.Tilt.K);
				X.D(M.Tilt.Cs);
				X.V2(M.Tilt.XEnd);
			}
		}
		X.U(R.CueTips.size());
		for (const rb::CueTipSegment& C : R.CueTips)
		{
			X.U(static_cast<uint64>(C.Strike));
			X.U(static_cast<uint64>(C.Path.Strike));
			X.Id(C.Path.StruckBall);
			X.V3(C.Path.Start);
			X.V3(C.Path.Direction);
			X.D(C.Path.Speed0);
			X.D(C.Path.Deceleration);
			X.D(C.Path.StartTime);
			X.D(C.Path.StopTime);
			X.D(C.Path.DomeRadius);
			X.D(C.T1);
			X.U(static_cast<uint64>(C.Kind));
			X.V3(C.EndPosition);
		}
		for (const rb::BallFinal& F : R.Finals)
		{
			X.Q(F.Orientation);
			X.U(static_cast<uint64>(F.Pocket));
			X.U(static_cast<uint64>(F.OffReason));
		}
		const rb::ShotRecord& Rec = R.Record;
		const rb::ShotStartSnapshot& Start = Rec.Start;
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			X.U(static_cast<uint64>(Start.Presence[Id]));
			X.V2(Start.Position[Id]);
			X.D(Start.Radius[Id]);
			X.U(static_cast<uint64>(Start.State[Id]));
			X.U(Start.FrozenToRail[Id]);
		}
		X.U(Start.AllBallsAtRest);
		X.U(static_cast<uint64>(Start.InHand));
		X.V2(Start.PlacedPosition);
		X.U(Start.PlacementOverPocket);
		X.U(Start.TemplatePresent);
		X.D(Start.ShotClockElapsed);
		X.U(Start.FootOnFloor);
		X.U(static_cast<uint64>(Rec.Stroke.Strokes.Size()));
		for (const rb::StrokeInfo& S : Rec.Stroke.Strokes)
		{
			X.Id(S.Ball);
			X.U(S.TipClothContact);
			X.U(S.Miscue);
			X.D(S.CueElevation);
		}
		X.U(static_cast<uint64>(Rec.Stroke.NonTipContacts.Size()));
		for (const rb::NonTipContact& N : Rec.Stroke.NonTipContacts)
		{
			X.Id(N.Ball);
			X.U(static_cast<uint64>(N.Source));
			X.D(N.Time);
		}
		X.U(Rec.Stroke.Overflow);
		X.U(static_cast<uint64>(Rec.End.Supported.Size()));
		for (const rb::SupportedBall& S : Rec.End.Supported)
		{
			X.Id(S.Ball);
			X.U(static_cast<uint64>(S.Pocket));
			X.U(S.Supporters);
		}
		X.U(Rec.Events.size());
		for (const rb::RecordEvent& E : Rec.Events)
		{
			X.D(E.Time);
			X.U(E.Sequence);
			X.U(static_cast<uint64>(E.Type));
			X.Id(E.A);
			X.Id(E.B);
			X.U(E.Feature);
			X.U(static_cast<uint8>(E.Side));
			X.U(E.ContinuesInitialFreeze);
			X.U(E.OtherContactBefore);
			X.U(static_cast<uint64>(E.From));
			X.U(static_cast<uint64>(E.To));
			X.V2(E.PositionA);
			X.V2(E.PositionB);
			X.V3(E.Normal);
			X.D(E.CutAngle);
			X.D(E.ZMax);
			X.D(E.Value);
		}
		X.U(Rec.Start.FrozenToCueBall);
		X.U(static_cast<uint64>(Rec.Stroke.TipContacts.Size()));
		for (const rb::TipContact& T : Rec.Stroke.TipContacts)
		{
			X.Id(T.Ball);
			X.U(static_cast<uint64>(T.Strike));
			X.D(T.Start);
			X.D(T.End);
		}
		X.D(Rec.End.StopTime);
		for (const rb::BallEnd& B : Rec.End.Balls)
		{
			X.U(static_cast<uint64>(B.Status));
			X.V2(B.Position);
			X.U(static_cast<uint64>(B.Pocket));
			X.U(B.FrozenToRail);
			X.U(B.FrozenToBalls);
		}
		X.U(Rec.Truncated);
		const rb::SimDiagnostics& G = R.Diagnostics;
		X.U(static_cast<uint64>(G.InputError));
		X.U(static_cast<uint64>(G.EventsProcessed));
		X.U(static_cast<uint64>(G.StaleEventsSkipped));
		X.U(static_cast<uint64>(G.Predictions));
		X.U(static_cast<uint64>(G.Islands));
		X.U(static_cast<uint64>(G.IslandHandOffs));
		X.U(static_cast<uint64>(G.IslandSteps));
		X.U(static_cast<uint64>(G.IslandRigidSwitches));
		X.U(G.IslandBudgetExceeded);
		X.U(static_cast<uint64>(G.ZenoTriggers));
		X.U(static_cast<uint64>(G.PressingContacts));
		X.U(static_cast<uint64>(G.OverlapWarnings));
		X.U(static_cast<uint64>(G.MissedEvents));
		X.U(static_cast<uint64>(G.FeatureJoins));
		X.U(static_cast<uint64>(G.TiltRefreshes));
		X.U(G.IslandCapacityExceeded);
		X.U(G.EventLogOverflow);
		X.U(G.TrajectoryOverflow);
		X.U(G.CueTipOverflow);
		X.U(G.RecordOverflow);
		return X.H;
	}

	FString Hex(uint64 V) { return FString::Printf(TEXT("%016llx"), V); }

	bool CheckHash(FAutomationTestBase& Test, const TCHAR* What, uint64 Actual, uint64 Expected)
	{
		if (Actual != Expected)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s, expected %s"), What, *Hex(Actual), *Hex(Expected)));
			return false;
		}
		return true;
	}

	// Every vector of the result sized exactly to its content.
	bool IsExact(const rb::ShotResult& R, FString& Where)
	{
		if (R.Events.capacity() != R.Events.size())
		{
			Where = TEXT("Events");
			return false;
		}
		if (R.CueTips.capacity() != R.CueTips.size())
		{
			Where = TEXT("CueTips");
			return false;
		}
		if (R.Record.Events.capacity() != R.Record.Events.size())
		{
			Where = TEXT("Record.Events");
			return false;
		}
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			if (R.Tracks[Id].Segments.capacity() != R.Tracks[Id].Segments.size())
			{
				Where = FString::Printf(TEXT("Tracks[%d]"), Id);
				return false;
			}
		}
		return true;
	}

	bool ParamsEqual(const rb::PhysicsParams& A, const rb::PhysicsParams& B, FString& Key)
	{
		for (int i = 0; i < rb::PhysicsParamCount(); ++i)
		{
			const rb::PhysicsParamInfo Info = rb::PhysicsParamAt(i);
			double VA = 0.0;
			double VB = 0.0;
			rb::GetPhysicsParam(A, Info.Key, VA);
			rb::GetPhysicsParam(B, Info.Key, VB);
			if (FMemory::Memcmp(&VA, &VB, sizeof(double)) != 0)
			{
				Key = UTF8_TO_TCHAR(Info.Key);
				return false;
			}
		}
		return true;
	}

	// Reads "key value" lines of Tools/rbsim/examples/break9.hash.
	bool ReadHashFile(TMap<FString, FString>& Out, FString& Path)
	{
		Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/rbsim/examples/break9.hash")));
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
		{
			return false;
		}
		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			FString Key;
			FString Value;
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || !Line.Split(TEXT(" "), &Key, &Value))
			{
				continue;
			}
			Out.Add(Key, Value.TrimStartAndEnd());
		}
		return true;
	}

	// A manually ticked game world (FTestWorldWrapper) and its simulation service.
	struct FServiceWorld
	{
		FTestWorldWrapper Wrapper;
		URbSimulationSubsystem* Service = nullptr;
		TArray<TSharedRef<const FRbShot>> Received;

		bool Create(FAutomationTestBase& Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			Service = Wrapper.GetTestWorld()->GetSubsystem<URbSimulationSubsystem>();
			if (Service == nullptr)
			{
				Test.AddError(TEXT("URbSimulationSubsystem missing in a game world"));
				return false;
			}
			Service->OnShotSimulated.AddLambda([this](const TSharedRef<const FRbShot>& Shot) { Received.Add(Shot); });
			return true;
		}

		void Tick() { Wrapper.TickTestWorld(1.0f / 60.0f); }

		// Ticks until N shots arrived (bounded wall time). True if they did.
		bool TickUntil(int32 N, double TimeoutSeconds = 10.0)
		{
			const double End = FPlatformTime::Seconds() + TimeoutSeconds;
			while (Received.Num() < N && FPlatformTime::Seconds() < End)
			{
				Tick();
				FPlatformProcess::Sleep(0.0005f);
			}
			return Received.Num() >= N;
		}
	};

// The tests live in the namespace too: unity builds merge this file with others, so no helper name may leak.

// --- Table context --------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationTableContextPresets, "RawBreak.Unit.Simulation.TableContext_Presets", RB_UNIT_TEST_FLAGS)
bool FRbSimulationTableContextPresets::RunTest(const FString& Parameters)
{
	for (uint8 T = 0; T <= static_cast<uint8>(ERbTablePreset::SevenFootTrue); ++T)
	{
		for (uint8 B = 0; B <= static_cast<uint8>(ERbBallSetPreset::OldBarOversizedCue); ++B)
		{
			FRbTableSetup Setup;
			Setup.Table = static_cast<ERbTablePreset>(T);
			Setup.BallSet = static_cast<ERbBallSetPreset>(B);
			Setup.BallSetSeed = 7;
			FString Error;
			const TSharedPtr<const FRbTableContext> Ctx = FRbTableContext::Create(Setup, Error);
			const FString Name = FString::Printf(TEXT("table %d / balls %d"), T, B);
			if (!TestTrue(*(Name + TEXT(" builds: ") + Error), Ctx.IsValid()))
			{
				continue;
			}
			TestTrue(*(Name + TEXT(" spec preset")), Ctx->Spec.Preset == RbTypes::ToCore(Setup.Table));
			TestTrue(*(Name + TEXT(" geometry of the spec")), Ctx->Geometry.Spec.Preset == Ctx->Spec.Preset);
			TestTrue(*(Name + TEXT(" params origin Table")), Ctx->Physics.Origin == rb::ParamsOrigin::Table);
			FString Key;
			const bool bParams = ParamsEqual(Ctx->Physics, rb::MakePhysicsParams(Ctx->Spec), Key);
			TestTrue(*(Name + TEXT(" params == MakePhysicsParams(spec) (level clean table), first differing key ") + Key), bParams);
			TestEqual(*(Name + TEXT(" ball count")), Ctx->Balls.Count, rb::kPoolBallCount);
			TestEqual(*(Name + TEXT(" cue ball radius")), Ctx->BallRadius(0), Ctx->Balls.Balls[0].Radius);
			TestEqual(*(Name + TEXT(" radius beyond the set")), Ctx->BallRadius(40), rb::kDefaultBallRadius);
			TestEqual(*(Name + TEXT(" rules head spot x")), Ctx->RulesTable.HeadSpot.x, Ctx->Geometry.Landmarks.HeadSpot.x);
			TestEqual(*(Name + TEXT(" rules foot spot x")), Ctx->RulesTable.FootSpot.x, Ctx->Geometry.Landmarks.FootSpot.x);
			TestTrue(*(Name + TEXT(" no lamp by default")), Ctx->Environment.LampUndersideZ == rb::kInfinity);
			TestEqual(*(Name + TEXT(" bed height")), Ctx->BedHeight(), Ctx->Spec.BedHeight);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationTableContextValidation, "RawBreak.Unit.Simulation.TableContext_Validation", RB_UNIT_TEST_FLAGS)
bool FRbSimulationTableContextValidation::RunTest(const FString& Parameters)
{
	FString Error;
	{
		FRbTableSetup Setup; // a dive-bar-like slope, lamp and footprint within their limits
		Setup.Condition.Slope = {0.001, -0.0005};
		Setup.Condition.BallCling = 1.3;
		Setup.LampUndersideZ = 1.016;
		Setup.LampFootprint = rb::Aabb2{{-1.5, -0.8}, {1.5, 0.8}};
		const TSharedPtr<const FRbTableContext> Ctx = FRbTableContext::Create(Setup, Error);
		if (TestTrue(*(TEXT("valid venue condition builds: ") + Error), Ctx.IsValid()))
		{
			TestEqual(TEXT("slope x"), Ctx->Physics.Tilt.Slope.x, 0.001);
			TestEqual(TEXT("slope y"), Ctx->Physics.Tilt.Slope.y, -0.0005);
			TestEqual(TEXT("cling"), Ctx->Physics.BallBall.ClingFactor, 1.3);
			TestEqual(TEXT("lamp height"), Ctx->Environment.LampUndersideZ, 1.016);
			TestEqual(TEXT("lamp footprint lo x"), Ctx->Environment.LampFootprint.Lo.x, -1.5);
			TestEqual(TEXT("lamp footprint hi y"), Ctx->Environment.LampFootprint.Hi.y, 0.8);
		}
	}
	const auto Rejected = [this](const TCHAR* What, const FRbTableSetup& Setup)
	{
		FString Why;
		const TSharedPtr<const FRbTableContext> Ctx = FRbTableContext::Create(Setup, Why);
		TestFalse(*FString::Printf(TEXT("%s is rejected"), What), Ctx.IsValid());
		TestFalse(*FString::Printf(TEXT("%s gives an error text"), What), Why.IsEmpty());
		AddInfo(FString::Printf(TEXT("%s: %s"), What, *Why));
	};
	{
		FRbTableSetup Setup;
		Setup.Condition.Slope = {0.05, 0.0}; // 50 mm/m: balls would roll away (beyond the rolling-resistance limit)
		Rejected(TEXT("slope 50 mm/m"), Setup);
	}
	{
		FRbTableSetup Setup;
		Setup.Condition.BallCling = 0.0;
		Rejected(TEXT("cling factor 0"), Setup);
	}
	{
		FRbTableSetup Setup;
		Setup.LampUndersideZ = 0.0;
		Rejected(TEXT("lamp at the cloth"), Setup);
		Setup.LampUndersideZ = std::nan("");
		Rejected(TEXT("lamp height NaN"), Setup);
	}
	{
		FRbTableSetup Setup;
		Setup.LampFootprint = rb::Aabb2{{1.0, -0.5}, {-1.0, 0.5}};
		Rejected(TEXT("inverted lamp footprint"), Setup);
	}
	return true;
}

// --- Input / result helpers -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationInitSimInput, "RawBreak.Unit.Simulation.InitSimInput", RB_UNIT_TEST_FLAGS)
bool FRbSimulationInitSimInput::RunTest(const FString& Parameters)
{
	FString Error;
	FRbTableSetup Setup;
	Setup.LampUndersideZ = 1.2;
	const TSharedPtr<const FRbTableContext> Ctx = FRbTableContext::Create(Setup, Error);
	if (!TestTrue(*(TEXT("context: ") + Error), Ctx.IsValid()))
	{
		return false;
	}
	TUniquePtr<rb::SimInput> In = MakeUnique<rb::SimInput>();
	In->Strikes.PushBack(rb::StrikeRequest{}); // stale content must be cleared
	In->Balls[3].InPlay = true;
	RbShot::InitSimInput(*Ctx, *In);
	TestTrue(TEXT("table = the context's geometry"), In->Table == &Ctx->Geometry);
	FString Key;
	const bool bParams = ParamsEqual(In->Params, Ctx->Physics, Key);
	TestTrue(*(TEXT("params = the context's, first differing key ") + Key), bParams);
	TestEqual(TEXT("lamp"), In->Environment.LampUndersideZ, 1.2);
	TestEqual(TEXT("no strikes"), In->Strikes.Size(), 0);
	bool bNoneInPlay = true;
	bool bSpecs = true;
	for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
	{
		bNoneInPlay &= !In->Balls[Id].InPlay;
		if (Id < Ctx->Balls.Count)
		{
			bSpecs &= In->Balls[Id].Spec.Radius == Ctx->Balls.Balls[Id].Radius && In->Balls[Id].Spec.Mass == Ctx->Balls.Balls[Id].Mass;
		}
	}
	TestTrue(TEXT("no ball in play"), bNoneInPlay);
	TestTrue(TEXT("ball specs from the set"), bSpecs);
	const rb::RecordOptions& R = In->Record;
	TestTrue(TEXT("record everything (playback + rules)"), R.Trajectories && R.EventStates && R.LogTransitions && R.LogObservers && R.ShotRecord);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationInputHash, "RawBreak.Unit.Simulation.InputHash_Sensitivity", RB_UNIT_TEST_FLAGS)
bool FRbSimulationInputHash::RunTest(const FString& Parameters)
{
	FString Error;
	TUniquePtr<FRbShotRequest> Base = MakeUnique<FRbShotRequest>();
	if (!TestTrue(*(TEXT("break9 scenario: ") + Error), MakeBreak9(*Base, Error)))
	{
		return false;
	}
	const rb::SimInput& In0 = Base->Input;
	const uint64 H0 = RbShot::InputHash(In0);
	TUniquePtr<rb::SimInput> In = MakeUnique<rb::SimInput>(In0);
	TestTrue(TEXT("a copy hashes equal"), RbShot::InputHash(*In) == H0);

	// Balls out of play are not simulated and not hashed (the game fills their specs, rbsim does not).
	In->Balls[12].Spec = rb::MakeBallSpec(0.03, 0.2);
	In->Balls[12].State.Position = {1.0, 1.0, 1.0};
	TestTrue(TEXT("an out-of-play ball does not change the hash"), RbShot::InputHash(*In) == H0);

	const TSharedPtr<const FRbTableContext> Tight = [&]()
	{
		FRbTableSetup S;
		S.Table = ERbTablePreset::NineFootTight;
		FString E;
		return FRbTableContext::Create(S, E);
	}();
	if (!TestTrue(TEXT("tight table context"), Tight.IsValid()))
	{
		return false;
	}

	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(rb::SimInput&)> Change;
	};
	const FCase Cases[] = {
		{TEXT("no table"), [](rb::SimInput& I) { I.Table = nullptr; }},
		{TEXT("other table spec"), [&Tight](rb::SimInput& I) { I.Table = &Tight->Geometry; }},
		{TEXT("lamp height"), [](rb::SimInput& I) { I.Environment.LampUndersideZ = 1.0; }},
		{TEXT("lamp footprint"), [](rb::SimInput& I) { I.Environment.LampFootprint.Lo.x = -2.0; }},
		{TEXT("cloth mu_s one ulp"), [](rb::SimInput& I) { I.Params.Cloth.SlidingFriction = std::nextafter(I.Params.Cloth.SlidingFriction, 1.0); }},
		{TEXT("params origin"), [](rb::SimInput& I) { I.Params.Origin = rb::ParamsOrigin::Explicit; }},
		{TEXT("chalk cling switch"), [](rb::SimInput& I) { I.Params.ChalkCling = !I.Params.ChalkCling; }},
		{TEXT("cue ball x one ulp"), [](rb::SimInput& I) { I.Balls[0].State.Position.x = std::nextafter(I.Balls[0].State.Position.x, 0.0); }},
		{TEXT("ball 9 spin"), [](rb::SimInput& I) { I.Balls[9].State.Omega.z = 1e-3; }},
		{TEXT("ball 5 motion state"), [](rb::SimInput& I) { I.Balls[5].State.State = rb::MotionState::Spinning; }},
		{TEXT("ball 5 mass"), [](rb::SimInput& I) { I.Balls[5].Spec.Mass += 1e-6; }},
		{TEXT("ball 3 orientation"), [](rb::SimInput& I) { I.Balls[3].Orientation = rb::Quat{0.0, 1.0, 0.0, 0.0}; }},
		{TEXT("chalk mark on ball 2"), [](rb::SimInput& I) { rb::ChalkMark M; M.BodyDir = {1.0, 0.0, 0.0}; M.Strength = 0.5; M.Radius = 0.0025; I.Balls[2].ChalkMarks.PushBack(M); }},
		{TEXT("ball 12 put in play"), [](rb::SimInput& I) { I.Balls[12].InPlay = true; }},
		{TEXT("strike speed"), [](rb::SimInput& I) { I.Strikes[0].Input.Speed = 9.000001; }},
		{TEXT("strike elevation -0.0"), [](rb::SimInput& I) { I.Strikes[0].Input.Elevation = -0.0; }},
		{TEXT("strike lambda override"), [](rb::SimInput& I) { I.Strikes[0].Input.LambdaOverride = 0.5; }},
		{TEXT("strike squirt off"), [](rb::SimInput& I) { I.Strikes[0].Input.SquirtEnabled = false; }},
		{TEXT("strike tip touches cloth"), [](rb::SimInput& I) { I.Strikes[0].Input.TipTouchesCloth = true; }},
		{TEXT("cue mass"), [](rb::SimInput& I) { I.Strikes[0].Input.Cue.Mass += 0.001; }},
		{TEXT("jump cue"), [](rb::SimInput& I) { I.Strikes[0].Input.Cue.JumpCue = true; }},
		{TEXT("struck ball id"), [](rb::SimInput& I) { I.Strikes[0].Ball = 9; }},
		{TEXT("second strike"), [](rb::SimInput& I) { I.Strikes.PushBack(I.Strikes[0]); }},
		{TEXT("context in hand"), [](rb::SimInput& I) { I.Context.InHand = rb::CueBallInHand::Anywhere; }},
		{TEXT("context placed position"), [](rb::SimInput& I) { I.Context.PlacedPosition = {-0.735, 0.12}; }},
		{TEXT("context non-tip contact"), [](rb::SimInput& I) { rb::NonTipContact C; C.Ball = 3; C.Time = -0.5; I.Context.NonTipContacts.PushBack(C); }},
		{TEXT("context frozen tolerance"), [](rb::SimInput& I) { I.Context.FrozenTolerance = 2e-4; }},
		{TEXT("record: no trajectories"), [](rb::SimInput& I) { I.Record.Trajectories = false; }},
		{TEXT("record: no rules record"), [](rb::SimInput& I) { I.Record.ShotRecord = false; }},
	};
	TSet<uint64> Seen;
	Seen.Add(H0);
	for (const FCase& Case : Cases)
	{
		*In = In0;
		Case.Change(*In);
		const uint64 H = RbShot::InputHash(*In);
		TestTrue(*FString::Printf(TEXT("%s changes the hash"), Case.Name), H != H0);
		TestFalse(*FString::Printf(TEXT("%s gives a new value"), Case.Name), Seen.Contains(H));
		Seen.Add(H);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationCompactCopy, "RawBreak.Unit.Simulation.CompactCopy_Footprint", RB_UNIT_TEST_FLAGS)
bool FRbSimulationCompactCopy::RunTest(const FString& Parameters)
{
	FString Error;
	TUniquePtr<FRbShotRequest> Request = MakeUnique<FRbShotRequest>();
	if (!TestTrue(*(TEXT("break9 scenario: ") + Error), MakeBreak9(*Request, Error)))
	{
		return false;
	}

	// The pooled worker result: reserved to the simulator's capacity by Run.
	rb::Simulator Sim;
	TUniquePtr<rb::ShotResult> Work = MakeUnique<rb::ShotResult>();
	TestTrue(TEXT("break simulates Ok"), Sim.Run(Request->Input, *Work) == rb::SimStatus::Ok);
	const SIZE_T ReservedBytes = RbShot::HeapBytes(*Work);
	TestTrue(*FString::Printf(TEXT("the reserved work result is large (%.0f KB)"), ReservedBytes / 1024.0), ReservedBytes > 2u * 1024u * 1024u);

	TUniquePtr<rb::ShotResult> Compact = MakeUnique<rb::ShotResult>();
	RbShot::CopyCompact(*Work, *Compact);
	FString Where;
	const bool bCompactExact = IsExact(*Compact, Where);
	TestTrue(*(TEXT("compact copy: capacity == size everywhere, first offender ") + Where), bCompactExact);
	CheckHash(*this, TEXT("compact copy ResultHash"), RbShot::ResultHash(*Compact), RbShot::ResultHash(*Work));
	CheckHash(*this, TEXT("compact copy deep hash"), DeepResultHash(*Compact), DeepResultHash(*Work));

	// Into a reused result that still holds reserved buffers: shrunk to the content as well.
	TUniquePtr<rb::ShotResult> Reused = MakeUnique<rb::ShotResult>();
	rb::ReserveShotResult(*Reused, rb::ResultCapacity{});
	RbShot::CopyCompact(*Work, *Reused);
	const bool bReusedExact = IsExact(*Reused, Where);
	TestTrue(*(TEXT("copy into a reserved result: capacity == size everywhere, first offender ") + Where), bReusedExact);
	CheckHash(*this, TEXT("reused copy deep hash"), DeepResultHash(*Reused), DeepResultHash(*Work));

	// The immutable shot of a break: < 1 MB (replay history of 32 shots stays small, ue-architecture 6.7).
	const TSharedRef<FRbShot> Shot = URbSimulationSubsystem::RunShotBlocking(MoveTemp(*Request));
	const SIZE_T Footprint = RbShot::FootprintBytes(*Shot);
	TestTrue(*FString::Printf(TEXT("break shot footprint %.1f KB < 1 MB"), Footprint / 1024.0), Footprint < 1024u * 1024u);
	const bool bShotExact = IsExact(Shot->Result, Where);
	TestTrue(*(TEXT("RunShotBlocking result is compact, first offender ") + Where), bShotExact);
	CheckHash(*this, TEXT("RunShotBlocking deep hash == direct Run"), DeepResultHash(Shot->Result), DeepResultHash(*Work));
	AddInfo(FString::Printf(TEXT("break: reserved work result %.0f KB, compact heap %.1f KB, sizeof(FRbShot) %.1f KB, shot footprint %.1f KB "
								 "(%d events, %d record events, sim %.3f ms)"),
		ReservedBytes / 1024.0, RbShot::HeapBytes(Shot->Result) / 1024.0, sizeof(FRbShot) / 1024.0, Footprint / 1024.0, static_cast<int32>(Shot->Result.Events.size()),
		static_cast<int32>(Shot->Result.Record.Events.size()), Shot->SimMilliseconds));
	return true;
}

// --- ROB-10 (A5): the UE module's input and result hashes == the standalone build's ---------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationRob10, "RawBreak.Unit.Simulation.ROB10_Break9Hash", RB_UNIT_TEST_FLAGS)
bool FRbSimulationRob10::RunTest(const FString& Parameters)
{
	TMap<FString, FString> Reference;
	FString Path;
	if (!TestTrue(*(TEXT("read ") + Path), ReadHashFile(Reference, Path)))
	{
		return false;
	}
	const FString* InputText = Reference.Find(TEXT("inputHash"));
	const FString* ResultText = Reference.Find(TEXT("resultHash"));
	if (!TestTrue(TEXT("break9.hash has inputHash and resultHash"), InputText != nullptr && ResultText != nullptr))
	{
		return false;
	}
	const uint64 ExpectedInput = FCString::Strtoui64(**InputText, nullptr, 16);
	const uint64 ExpectedResult = FCString::Strtoui64(**ResultText, nullptr, 16);
	if (const FString* Version = Reference.Find(TEXT("coreVersion")))
	{
		TestEqual(TEXT("core version of the reference"), *Version, FString(UTF8_TO_TCHAR(rb::CoreVersion())));
	}

	FString Error;
	TUniquePtr<FRbShotRequest> Request = MakeUnique<FRbShotRequest>();
	if (!TestTrue(*(TEXT("break9 scenario: ") + Error), MakeBreak9(*Request, Error)))
	{
		return false;
	}
	// Input first: a mismatch here is scenario drift (the UE path builds another input), not a determinism failure.
	const bool bInput = CheckHash(*this, TEXT("ROB-10 InputHash (scenario rebuilt in the UE module vs rbsim --hash)"), RbShot::InputHash(Request->Input), ExpectedInput);

	const TSharedRef<FRbShot> Shot = URbSimulationSubsystem::RunShotBlocking(MoveTemp(*Request));
	TestTrue(TEXT("break9 simulates Ok"), Shot->Result.Status == rb::SimStatus::Ok);
	CheckHash(*this, TEXT("FRbShot::InputHash"), Shot->InputHash, ExpectedInput);
	const bool bResult = CheckHash(*this, TEXT("ROB-10 ResultHash (UE module vs standalone build)"), Shot->ResultHash, ExpectedResult);
	CheckHash(*this, TEXT("FRbShot::ResultHash == RbShot::ResultHash(Result)"), Shot->ResultHash, RbShot::ResultHash(Shot->Result));
	AddInfo(FString::Printf(TEXT("ROB-10 break9: input %s (%s), result %s (%s), %d events, stop %.4f s, sim %.3f ms"), *Hex(Shot->InputHash),
		bInput ? TEXT("match") : TEXT("MISMATCH"), *Hex(Shot->ResultHash), bResult ? TEXT("match") : TEXT("MISMATCH"), Shot->Result.Diagnostics.EventsProcessed,
		Shot->Result.StopTime, Shot->SimMilliseconds));
	return true;
}

// --- The worker service ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationSameFrame, "RawBreak.Unit.Simulation.Worker_SameFrameHandOff", RB_UNIT_TEST_FLAGS)
bool FRbSimulationSameFrame::RunTest(const FString& Parameters)
{
	FServiceWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	URbSimulationSubsystem* Service = World.Service;
	const double Budget = Service->CollectBudgetSeconds;

	using FMaker = bool (*)(FRbShotRequest&, FString&);
	const FMaker Makers[] = {&MakeBreak9, &MakeTwoBallShot, &MakeBreak9};
	const TCHAR* Names[] = {TEXT("break9"), TEXT("two-ball"), TEXT("break9 again (reused simulator)")};
	for (int32 k = 0; k < UE_ARRAY_COUNT(Makers); ++k)
	{
		FString Error;
		TUniquePtr<FRbShotRequest> Request = MakeUnique<FRbShotRequest>();
		TUniquePtr<FRbShotRequest> Copy = MakeUnique<FRbShotRequest>();
		if (!TestTrue(*(FString(Names[k]) + TEXT(" scenario: ") + Error), Makers[k](*Request, Error) && Makers[k](*Copy, Error)))
		{
			return false;
		}
		const int32 Before = World.Received.Num();
		const uint64 Frame = GFrameCounter;
		const uint32 Id = Service->SubmitShot(MoveTemp(*Request));
		TestTrue(*FString::Printf(TEXT("%s: submitted (id %u)"), Names[k], Id), Id > 0);
		TestTrue(TEXT("busy after submit"), Service->IsBusy());
		TestEqual(TEXT("nothing broadcast inside SubmitShot"), World.Received.Num(), Before);

		World.Tick(); // ONE world frame: the subsystem's Tick (after TG_PostPhysics) must hand the shot off
		const bool bSameTick = World.Received.Num() == Before + 1;
		if (!bSameTick && !World.TickUntil(Before + 1))
		{
			AddError(FString::Printf(TEXT("%s: no hand-off at all"), Names[k]));
			return false;
		}
		const TSharedRef<const FRbShot> Shot = World.Received.Last();
		TestEqual(TEXT("handed-off id"), Shot->Id, Id);
		TestFalse(TEXT("service free after the hand-off"), Service->IsBusy());
		TestTrue(TEXT("GetLastShot is the handed-off shot"), Service->GetLastShot() == Shot);
		TestEqual(TEXT("submit frame"), Shot->SubmitFrame, Frame);
		if (bSameTick)
		{
			TestEqual(TEXT("hand-off frame == submit frame"), Shot->HandOffFrame, Frame);
		}
		else if (Shot->SimMilliseconds >= 1000.0 * Budget)
		{
			// The simulation itself took longer than the budget (machine under load): late by design, not a defect.
			AddWarning(FString::Printf(TEXT("%s: simulation took %.3f ms > budget %.1f ms, hand-off %llu frame(s) late"), Names[k], Shot->SimMilliseconds,
				1000.0 * Budget, Shot->HandOffFrame - Shot->SubmitFrame));
		}
		else
		{
			AddError(FString::Printf(TEXT("%s: simulated in %.3f ms (budget %.1f ms) but not handed off in the submit frame"), Names[k], Shot->SimMilliseconds,
				1000.0 * Budget));
		}

		// Bitwise identical to the synchronous path (fresh Simulator + fresh result vs the pooled worker ones).
		const TSharedRef<FRbShot> Blocking = URbSimulationSubsystem::RunShotBlocking(MoveTemp(*Copy));
		TestTrue(*FString::Printf(TEXT("%s: status Ok"), Names[k]), Shot->Result.Status == rb::SimStatus::Ok);
		CheckHash(*this, TEXT("worker InputHash == RunShotBlocking"), Shot->InputHash, Blocking->InputHash);
		CheckHash(*this, TEXT("worker ResultHash == RunShotBlocking"), Shot->ResultHash, Blocking->ResultHash);
		CheckHash(*this, TEXT("worker deep result hash == RunShotBlocking"), DeepResultHash(Shot->Result), DeepResultHash(Blocking->Result));
		FString Where;
		const bool bExact = IsExact(Shot->Result, Where);
		TestTrue(*(TEXT("handed-off result is compact, first offender ") + Where), bExact);
		AddInfo(FString::Printf(TEXT("%s: sim %.3f ms %s, hand-off %s"), Names[k], Shot->SimMilliseconds, Shot->bSimulatedOnWorker ? TEXT("on a worker") : TEXT("in place"),
			bSameTick ? TEXT("in the submit frame") : TEXT("late")));
	}
	const FRbSimulationStats& Stats = Service->GetStats();
	TestEqual(TEXT("stats: submitted"), Stats.Submitted, 3);
	TestEqual(TEXT("stats: handed off"), Stats.HandedOff, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationOffGameThread, "RawBreak.Unit.Simulation.Worker_RunsOffGameThread", RB_UNIT_TEST_FLAGS)
bool FRbSimulationOffGameThread::RunTest(const FString& Parameters)
{
	FServiceWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	URbSimulationSubsystem* Service = World.Service;
	FString Error;
	TUniquePtr<FRbShotRequest> Request = MakeUnique<FRbShotRequest>();
	TUniquePtr<FRbShotRequest> Copy = MakeUnique<FRbShotRequest>();
	if (!TestTrue(*(TEXT("break9 scenario: ") + Error), MakeBreak9(*Request, Error) && MakeBreak9(*Copy, Error)))
	{
		return false;
	}
	const uint32 Id = Service->SubmitShot(MoveTemp(*Request));
	TestTrue(TEXT("submitted"), Id > 0);

	// The game thread never waits on the task here, so only a worker can run it.
	const double End = FPlatformTime::Seconds() + 10.0;
	while (!Service->IsResultReady() && FPlatformTime::Seconds() < End)
	{
		FPlatformProcess::Sleep(0.0005f);
	}
	if (!TestTrue(TEXT("the worker finished the shot"), Service->IsResultReady()))
	{
		Service->CancelInFlight();
		return false;
	}
	TestEqual(TEXT("finished but not broadcast before the game thread collects it"), World.Received.Num(), 0);
	TestTrue(TEXT("still busy until collected"), Service->IsBusy());

	World.Tick(); // collects the finished shot
	if (!TestEqual(TEXT("handed off by the next world tick"), World.Received.Num(), 1))
	{
		return false;
	}
	const TSharedRef<const FRbShot> Shot = World.Received[0];
	TestTrue(TEXT("Simulator::Run executed off the game thread"), Shot->bSimulatedOnWorker);
	TestEqual(TEXT("stats: ran on a worker"), Service->GetStats().RanOnWorker, 1);
	const TSharedRef<FRbShot> Blocking = URbSimulationSubsystem::RunShotBlocking(MoveTemp(*Copy));
	TestFalse(TEXT("RunShotBlocking runs on the calling thread"), Blocking->bSimulatedOnWorker);
	CheckHash(*this, TEXT("worker deep result hash == RunShotBlocking"), DeepResultHash(Shot->Result), DeepResultHash(Blocking->Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationRefuseBusy, "RawBreak.Unit.Simulation.Worker_RefusesWhileBusy", RB_UNIT_TEST_FLAGS)
bool FRbSimulationRefuseBusy::RunTest(const FString& Parameters)
{
	FServiceWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	URbSimulationSubsystem* Service = World.Service;
	AddExpectedMessagePlain(TEXT("SubmitShot refused: shot"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("SubmitShot refused: the request has no table context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("RunShotBlocking: the request has no table context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	FString Error;
	TUniquePtr<FRbShotRequest> A = MakeUnique<FRbShotRequest>();
	TUniquePtr<FRbShotRequest> B = MakeUnique<FRbShotRequest>();
	if (!TestTrue(*(TEXT("scenarios: ") + Error), MakeTwoBallShot(*A, Error) && MakeBreak9(*B, Error)))
	{
		return false;
	}
	const uint32 IdA = Service->SubmitShot(MoveTemp(*A));
	TestTrue(TEXT("first shot accepted"), IdA > 0);

	const TSharedPtr<const FRbTableContext> TableB = B->Table;
	TestEqual(TEXT("second shot refused while the first is in flight"), Service->SubmitShot(MoveTemp(*B)), 0u);
	TestTrue(TEXT("a refused request is left untouched (table)"), B->Table == TableB);
	TestEqual(TEXT("a refused request is left untouched (strikes)"), B->Input.Strikes.Size(), 1);
	TestEqual(TEXT("stats: refused"), Service->GetStats().Refused, 1);

	// A listener may submit the next shot from inside OnShotSimulated: the service is free before the broadcast.
	uint32 IdB = 0;
	Service->OnShotSimulated.AddLambda([&](const TSharedRef<const FRbShot>& Shot)
	{
		if (Shot->Id == IdA)
		{
			IdB = Service->SubmitShot(MoveTemp(*B));
		}
	});
	if (!TestTrue(TEXT("first shot handed off"), World.TickUntil(1)))
	{
		return false;
	}
	TestEqual(TEXT("next shot accepted from the hand-off handler"), IdB, IdA + 1);
	TestTrue(TEXT("second shot handed off"), World.TickUntil(2));
	if (World.Received.Num() == 2)
	{
		TestEqual(TEXT("hand-off order 1"), World.Received[0]->Id, IdA);
		TestEqual(TEXT("hand-off order 2"), World.Received[1]->Id, IdB);
	}

	FRbShotRequest NoTable;
	TestEqual(TEXT("a request without a table context is refused"), Service->SubmitShot(MoveTemp(NoTable)), 0u);
	TestFalse(TEXT("not busy after refusing"), Service->IsBusy());
	const TSharedRef<FRbShot> Invalid = URbSimulationSubsystem::RunShotBlocking(FRbShotRequest{});
	TestTrue(TEXT("RunShotBlocking without a table: InvalidInput, no crash"), Invalid->Result.Status == rb::SimStatus::InvalidInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationLateSubmit, "RawBreak.Unit.Simulation.Worker_LateSubmitNextTick", RB_UNIT_TEST_FLAGS)
bool FRbSimulationLateSubmit::RunTest(const FString& Parameters)
{
	// A shot submitted AFTER the subsystem's Tick of a frame (here from the OnShotSimulated handler, which runs inside that
	// Tick) gets its bounded wait at the first Tick after the submission, i.e. the next frame: it must not be left to
	// polling (a task no worker has started is only retracted by a wait). The budget is generous here so that the test
	// checks the policy, not the machine's load.
	FServiceWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	URbSimulationSubsystem* Service = World.Service;
	Service->CollectBudgetSeconds = 0.25;

	FString Error;
	TUniquePtr<FRbShotRequest> A = MakeUnique<FRbShotRequest>();
	TUniquePtr<FRbShotRequest> B = MakeUnique<FRbShotRequest>();
	if (!TestTrue(*(TEXT("scenarios: ") + Error), MakeTwoBallShot(*A, Error) && MakeBreak9(*B, Error)))
	{
		return false;
	}
	const uint32 IdA = Service->SubmitShot(MoveTemp(*A));
	TestTrue(TEXT("first shot accepted"), IdA > 0);
	uint32 IdB = 0;
	Service->OnShotSimulated.AddLambda([&](const TSharedRef<const FRbShot>& Shot)
	{
		if (Shot->Id == IdA)
		{
			IdB = Service->SubmitShot(MoveTemp(*B));
		}
	});

	World.Tick(); // frame N: A handed off (same frame); B submitted from the handler, after this frame's wait
	if (!TestEqual(TEXT("first shot handed off in its submit frame"), World.Received.Num(), 1))
	{
		Service->CancelInFlight();
		return false;
	}
	TestTrue(TEXT("second shot submitted from the handler"), IdB > 0);
	TestTrue(TEXT("second shot in flight after frame N"), Service->IsBusy());

	World.Tick(); // frame N + 1: the first Tick after B's submission waits for it
	if (!TestEqual(TEXT("late-submitted shot handed off by the first Tick after its submission"), World.Received.Num(), 2))
	{
		Service->CancelInFlight();
		return false;
	}
	const TSharedRef<const FRbShot> Shot = World.Received[1];
	TestEqual(TEXT("handed-off id"), Shot->Id, IdB);
	TestEqual(TEXT("hand-off one frame after the submit frame"), Shot->HandOffFrame, Shot->SubmitFrame + 1);
	TestFalse(TEXT("service free"), Service->IsBusy());
	const FRbSimulationStats& Stats = Service->GetStats();
	TestEqual(TEXT("stats: handed off"), Stats.HandedOff, 2);
	TestEqual(TEXT("stats: only the first shot counts as a same-frame hand-off"), Stats.SameFrameHandOffs, 1);
	AddInfo(FString::Printf(TEXT("late-submitted break: sim %.3f ms %s, hand-off +%llu frame"), Shot->SimMilliseconds,
		Shot->bSimulatedOnWorker ? TEXT("on a worker") : TEXT("in place (retracted by the wait)"), Shot->HandOffFrame - Shot->SubmitFrame));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationCancel, "RawBreak.Unit.Simulation.Worker_CancelInFlight", RB_UNIT_TEST_FLAGS)
bool FRbSimulationCancel::RunTest(const FString& Parameters)
{
	FServiceWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	URbSimulationSubsystem* Service = World.Service;
	FString Error;
	TUniquePtr<FRbShotRequest> A = MakeUnique<FRbShotRequest>();
	TUniquePtr<FRbShotRequest> B = MakeUnique<FRbShotRequest>();
	if (!TestTrue(*(TEXT("scenarios: ") + Error), MakeBreak9(*A, Error) && MakeTwoBallShot(*B, Error)))
	{
		return false;
	}
	const uint32 IdA = Service->SubmitShot(MoveTemp(*A));
	TestTrue(TEXT("submitted"), IdA > 0);
	Service->CancelInFlight(); // the worker may be running right now: waits, then drops
	TestFalse(TEXT("free after cancel"), Service->IsBusy());
	TestFalse(TEXT("nothing ready after cancel"), Service->IsResultReady());
	TestTrue(TEXT("no last shot"), !Service->GetLastShot().IsValid());
	TestEqual(TEXT("stats: discarded"), Service->GetStats().Discarded, 1);
	World.Tick();
	World.Tick();
	TestEqual(TEXT("a cancelled shot is never broadcast"), World.Received.Num(), 0);
	Service->CancelInFlight(); // nothing in flight: no-op
	TestEqual(TEXT("stats: discarded unchanged"), Service->GetStats().Discarded, 1);

	const uint32 IdB = Service->SubmitShot(MoveTemp(*B));
	TestEqual(TEXT("next id after a cancelled shot"), IdB, IdA + 1);
	TestTrue(TEXT("next shot handed off"), World.TickUntil(1));
	if (World.Received.Num() == 1)
	{
		TestEqual(TEXT("only the second shot arrives"), World.Received[0]->Id, IdB);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbSimulationDeinitialize, "RawBreak.Unit.Simulation.Worker_DeinitializeInFlight", RB_UNIT_TEST_FLAGS)
bool FRbSimulationDeinitialize::RunTest(const FString& Parameters)
{
	// World teardown with a shot in flight, at different moments of the worker (not started, running, finished but not
	// collected): Deinitialize waits for the task before the Simulator dies and never broadcasts.
	for (int32 Round = 0; Round < 12; ++Round)
	{
		int32 Broadcasts = 0;
		{
			FServiceWorld World;
			if (!World.Create(*this))
			{
				return false;
			}
			World.Service->OnShotSimulated.AddLambda([&Broadcasts](const TSharedRef<const FRbShot>&) { ++Broadcasts; });
			FString Error;
			TUniquePtr<FRbShotRequest> Request = MakeUnique<FRbShotRequest>();
			if (!TestTrue(*(TEXT("break9 scenario: ") + Error), MakeBreak9(*Request, Error)))
			{
				return false;
			}
			TestTrue(TEXT("submitted"), World.Service->SubmitShot(MoveTemp(*Request)) > 0);
			if (Round % 4 != 0)
			{
				FPlatformProcess::Sleep(0.0005f * static_cast<float>(Round % 4) * static_cast<float>(Round % 4)); // 0.5, 2, 4.5 ms
			}
			TestTrue(TEXT("still in flight at teardown"), World.Service->IsBusy());
			URbSimulationSubsystem* const Service = World.Service;
			World.Wrapper.DestroyTestWorld(false); // -> UWorld::CleanupWorld -> URbSimulationSubsystem::Deinitialize
			// No garbage collection ran (DestroyTestWorld(false)), so the deinitialized subsystem is still readable: the world
			// teardown itself (not a later BeginDestroy) must have waited for the task and dropped the shot.
			TestFalse(*FString::Printf(TEXT("round %d: Deinitialize waited for the in-flight shot"), Round), Service->IsBusy());
			TestEqual(*FString::Printf(TEXT("round %d: the in-flight shot was discarded by Deinitialize"), Round), Service->GetStats().Discarded, 1);
			TestEqual(*FString::Printf(TEXT("round %d: never handed off"), Round), Service->GetStats().HandedOff, 0);
		}
		TestEqual(*FString::Printf(TEXT("round %d: no broadcast from a torn-down world"), Round), Broadcasts, 0);
	}
	return true;
}

} // namespace RbSimulationTests

#endif // WITH_DEV_AUTOMATION_TESTS
