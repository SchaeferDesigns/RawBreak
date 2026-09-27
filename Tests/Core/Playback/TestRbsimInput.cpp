// A-TOOL-1: rbsim --dump-input -> --in round trip (rbsimInput schema v1, Docs/architecture.md 5.3 and 14), incl. tilt keys
// and chalk marks; the JSON reader behind --in (WP-7).
//
// The test target globs Tests/Core only, so the rbsim I/O sources (developer-tool code outside BilliardsCore) are
// compiled into this translation unit.

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "../../../Tools/rbsim/JsonReader.cpp"
#include "../../../Tools/rbsim/JsonWriter.cpp"
#include "../../../Tools/rbsim/SimInputJson.cpp"

#include "rb/Core/Random.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Physics/ParamTable.h"
#include "rb/Physics/Simulator.h"

#include <cstring>
#include <memory>
#include <string>

using namespace playtest;

namespace
{
	struct Scenario
	{
		rb::TableGeometry Geometry;
		rb::SimInput Input;
	};

	// A shot that exercises every field of the schema: a tilted, clingy venue table, marked balls, the lag's two strikes
	// with non-default cue data, a game-layer context and mixed recording switches.
	std::unique_ptr<Scenario> MakeScenario(bool FiniteLamp)
	{
		std::unique_ptr<Scenario> S = std::make_unique<Scenario>();
		rb::BuildTableGeometry(rb::kTableSevenFootBar, S->Geometry);
		rb::SimInput& In = S->Input;
		In.Table = &S->Geometry;
		rb::TableCondition Condition;
		Condition.Slope = {1.3e-3, -0.7e-3};
		Condition.BallCling = 1.3;
		Condition.ChalkCling = true;
		In.Params = rb::MakePhysicsParams(S->Geometry.Spec, Condition);
		rb::SetPhysicsParam(In.Params, "tilt.tolerance", 2.5e-5);
		rb::SetPhysicsParam(In.Params, "cushion.mathavan_steps", 40.0);
		if (FiniteLamp)
		{
			In.Environment.LampUndersideZ = 0.84;
			In.Environment.LampFootprint = {{-0.61, -0.3}, {0.62, 0.31}};
		}

		const rb::BallSpec Specs[3] = {rb::kOversizedCueBall, rb::kStandardPoolBall, rb::MakeBallSpec(0.0286, 0.1655)};
		const int Ids[3] = {0, 1, 13};
		rb::Rng Rng(4242);
		for (int i = 0; i < 3; ++i)
		{
			rb::SimBall& B = In.Balls[Ids[i]];
			B.InPlay = true;
			B.Spec = Specs[i];
			B.State.Position = {Rng.NextUniform(-0.9, 0.9), Rng.NextUniform(-0.4, 0.4), B.Spec.Radius};
			B.Orientation = rb::Normalized(rb::Quat{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)});
			for (int m = 0; m < i + 1; ++m)
			{
				rb::ChalkMark Mark;
				Mark.BodyDir = rb::Normalized(rb::Vec3{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)});
				Mark.Strength = Rng.NextUniform(0.3, 1.0);
				Mark.Radius = m == 0 ? 0.0025 : 0.004;
				B.ChalkMarks.PushBack(Mark);
			}
		}
		In.Balls[13].State.Velocity = {0.1234567890123, -1.0 / 3.0, 0.0}; // a moving ball (tests, replays of odd states)
		In.Balls[13].State.State = rb::MotionState::Sliding;

		rb::StrikeRequest First;
		First.Ball = 0;
		First.Input.Speed = 4.5;
		First.Input.Elevation = 0.123456789;
		First.Input.Azimuth = -2.9;
		First.Input.OffsetA = 0.31;
		First.Input.OffsetB = -0.27;
		First.Input.Cue = rb::GetCueSpec(rb::CuePreset::House19oz);
		First.Input.LambdaOverride = 0.35;
		First.Input.SquirtEnabled = false;
		First.Input.TipTouchesCloth = true;
		In.Strikes.PushBack(First);
		rb::StrikeRequest Second;
		Second.Ball = 1;
		Second.Input.Speed = 1.75;
		Second.Input.Cue = rb::GetCueSpec(rb::CuePreset::Jump9oz);
		Second.Input.Cue.FollowThroughDistance = 0.0;
		In.Strikes.PushBack(Second);

		rb::ShotContext& C = In.Context;
		C.InHand = rb::CueBallInHand::AboveHeadString;
		C.PlacedPosition = {-0.7, 0.05};
		C.TemplatePresent = true;
		C.ShotClockElapsed = 7.25;
		C.FootOnFloor = false;
		C.FrozenTolerance = 2.0e-4;
		rb::NonTipContact Touch;
		Touch.Ball = 13;
		Touch.Source = rb::NonTipSource::Hair;
		Touch.Time = -0.125;
		C.NonTipContacts.PushBack(Touch);
		Touch.Ball = 0;
		Touch.Source = rb::NonTipSource::PlacingHand;
		Touch.Time = -3.0;
		C.NonTipContacts.PushBack(Touch);

		In.Record.Trajectories = false;
		In.Record.EventStates = true;
		In.Record.LogTransitions = false;
		In.Record.LogObservers = true;
		In.Record.ShotRecord = true;
		return S;
	}

	bool SameCue(const rb::CueSpec& A, const rb::CueSpec& B)
	{
		return SameBits(A.Mass, B.Mass) && SameBits(A.EndMass, B.EndMass) && SameBits(A.TipRestitution, B.TipRestitution) &&
			SameBits(A.TipFriction, B.TipFriction) && SameBits(A.TipFrictionKinetic, B.TipFrictionKinetic) && SameBits(A.TipDomeRadius, B.TipDomeRadius) &&
			SameBits(A.TipDiameter, B.TipDiameter) && SameBits(A.Length, B.Length) && SameBits(A.ContactTime, B.ContactTime) &&
			SameBits(A.FollowThroughDistance, B.FollowThroughDistance) && A.JumpCue == B.JumpCue;
	}

	bool SamePocketSpec(const rb::PocketSpec& A, const rb::PocketSpec& B)
	{
		return SameBits(A.Mouth, B.Mouth) && SameBits(A.CutAngle, B.CutAngle) && SameBits(A.Shelf, B.Shelf) && SameBits(A.JawRadius, B.JawRadius) &&
			SameBits(A.CaptureRadius, B.CaptureRadius);
	}

	bool SameTableSpec(const rb::TableSpec& A, const rb::TableSpec& B)
	{
		return A.Preset == B.Preset && std::strcmp(A.Name, B.Name) == 0 && SameBits(A.Length, B.Length) && SameBits(A.Width, B.Width) &&
			SameBits(A.BedHeight, B.BedHeight) && SameBits(A.CushionNoseHeight, B.CushionNoseHeight) && SameBits(A.CushionWidth, B.CushionWidth) &&
			SameBits(A.CushionNoseProfileRadius, B.CushionNoseProfileRadius) && SameBits(A.RailWidthTotal, B.RailWidthTotal) &&
			SameBits(A.RailTopZ, B.RailTopZ) && SameBits(A.SlateThickness, B.SlateThickness) && SameBits(A.SightInset, B.SightInset) &&
			SameBits(A.SightDiameter, B.SightDiameter) && SamePocketSpec(A.Corner, B.Corner) && SamePocketSpec(A.Side, B.Side) &&
			SameBits(A.Backdraft, B.Backdraft) && SameBits(A.DropPointRadius, B.DropPointRadius) && SameBits(A.FacingThickness, B.FacingThickness) &&
			SameBits(A.LinerUndercut, B.LinerUndercut) && A.HasPockets == B.HasPockets && A.Cloth == B.Cloth &&
			SameBits(A.FacingRestitutionScale, B.FacingRestitutionScale) && SameBits(A.LinerRestitution, B.LinerRestitution) &&
			SameBits(A.LinerFriction, B.LinerFriction);
	}

	// Every field Simulator::Run reads, bitwise.
	bool SameInput(const rb::SimInput& A, const rb::SimInput& B)
	{
		bool Same = A.Table != nullptr && B.Table != nullptr && SameTableSpec(A.Table->Spec, B.Table->Spec);
		Same = Same && SameBits(A.Environment.LampUndersideZ, B.Environment.LampUndersideZ) &&
			SameBits(A.Environment.LampFootprint.Lo.x, B.Environment.LampFootprint.Lo.x) &&
			SameBits(A.Environment.LampFootprint.Lo.y, B.Environment.LampFootprint.Lo.y) &&
			SameBits(A.Environment.LampFootprint.Hi.x, B.Environment.LampFootprint.Hi.x) &&
			SameBits(A.Environment.LampFootprint.Hi.y, B.Environment.LampFootprint.Hi.y);
		for (int i = 0; i < rb::PhysicsParamCount(); ++i)
		{
			const char* Key = rb::PhysicsParamAt(i).Key;
			double Va = 0.0;
			double Vb = 0.0;
			Same = Same && rb::GetPhysicsParam(A.Params, Key, Va) && rb::GetPhysicsParam(B.Params, Key, Vb) && SameBits(Va, Vb);
		}
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			const rb::SimBall& X = A.Balls[b];
			const rb::SimBall& Y = B.Balls[b];
			Same = Same && X.InPlay == Y.InPlay;
			if (!X.InPlay || !Y.InPlay)
			{
				continue;
			}
			Same = Same && SameBits(X.Spec.Radius, Y.Spec.Radius) && SameBits(X.Spec.Mass, Y.Spec.Mass) && SameBits(X.Spec.Inertia, Y.Spec.Inertia) &&
				SameBits(X.State, Y.State) && SameBits(X.Orientation, Y.Orientation) && X.ChalkMarks.Size() == Y.ChalkMarks.Size();
			for (int m = 0; Same && m < X.ChalkMarks.Size(); ++m)
			{
				Same = SameBits(X.ChalkMarks[m].BodyDir, Y.ChalkMarks[m].BodyDir) && SameBits(X.ChalkMarks[m].Strength, Y.ChalkMarks[m].Strength) &&
					SameBits(X.ChalkMarks[m].Radius, Y.ChalkMarks[m].Radius);
			}
		}
		Same = Same && A.Strikes.Size() == B.Strikes.Size();
		for (int i = 0; Same && i < A.Strikes.Size(); ++i)
		{
			const rb::StrikeRequest& X = A.Strikes[i];
			const rb::StrikeRequest& Y = B.Strikes[i];
			Same = X.Ball == Y.Ball && SameBits(X.Input.Speed, Y.Input.Speed) && SameBits(X.Input.Elevation, Y.Input.Elevation) &&
				SameBits(X.Input.Azimuth, Y.Input.Azimuth) && SameBits(X.Input.OffsetA, Y.Input.OffsetA) && SameBits(X.Input.OffsetB, Y.Input.OffsetB) &&
				SameCue(X.Input.Cue, Y.Input.Cue) && SameBits(X.Input.LambdaOverride, Y.Input.LambdaOverride) &&
				X.Input.SquirtEnabled == Y.Input.SquirtEnabled && X.Input.TipTouchesCloth == Y.Input.TipTouchesCloth;
		}
		const rb::ShotContext& Ca = A.Context;
		const rb::ShotContext& Cb = B.Context;
		Same = Same && Ca.InHand == Cb.InHand && SameBits(Ca.PlacedPosition.x, Cb.PlacedPosition.x) && SameBits(Ca.PlacedPosition.y, Cb.PlacedPosition.y) &&
			Ca.TemplatePresent == Cb.TemplatePresent && SameBits(Ca.ShotClockElapsed, Cb.ShotClockElapsed) && Ca.FootOnFloor == Cb.FootOnFloor &&
			SameBits(Ca.FrozenTolerance, Cb.FrozenTolerance) && Ca.NonTipContacts.Size() == Cb.NonTipContacts.Size();
		for (int i = 0; Same && i < Ca.NonTipContacts.Size(); ++i)
		{
			Same = Ca.NonTipContacts[i].Ball == Cb.NonTipContacts[i].Ball && Ca.NonTipContacts[i].Source == Cb.NonTipContacts[i].Source &&
				SameBits(Ca.NonTipContacts[i].Time, Cb.NonTipContacts[i].Time);
		}
		return Same && A.Record.Trajectories == B.Record.Trajectories && A.Record.EventStates == B.Record.EventStates &&
			A.Record.LogTransitions == B.Record.LogTransitions && A.Record.LogObservers == B.Record.LogObservers &&
			A.Record.ShotRecord == B.Record.ShotRecord;
	}

	bool SameShotEvent(const rb::ShotEvent& A, const rb::ShotEvent& B)
	{
		bool Same = SameBits(A.Time, B.Time) && A.Type == B.Type && A.Flags == B.Flags && A.A == B.A && A.B == B.B && A.Feature == B.Feature &&
			A.SubFeature == B.SubFeature && A.From == B.From && A.To == B.To && SameBits(A.Normal, B.Normal) && SameBits(A.NormalSpeed, B.NormalSpeed) &&
			SameBits(A.NormalImpulse, B.NormalImpulse) && SameBits(A.TangentImpulse, B.TangentImpulse) && SameBits(A.CutAngle, B.CutAngle) &&
			SameBits(A.Value, B.Value);
		for (int i = 0; i < 2; ++i)
		{
			Same = Same && SameBits(A.Pre[i], B.Pre[i]) && SameBits(A.Post[i], B.Post[i]);
		}
		return Same;
	}

	std::string Dump(const rb::SimInput& In, bool Pretty)
	{
		rbsim::JsonWriter J(Pretty);
		rbsim::WriteSimInput(In, J);
		return J.Text();
	}
}

// A-TOOL-1 (input half): --dump-input -> --in restores every field bitwise, incl. the tilt keys, the chalk marks and
// non-finite values; dumping the parsed input again gives the same text; the geometry is rebuilt from the spec.
RB_TEST(ARCH_TOOL1_DumpInputRoundTripIsBitwise)
{
	for (const bool FiniteLamp : {false, true})
	{
		for (const bool Pretty : {true, false})
		{
			const std::unique_ptr<Scenario> S = MakeScenario(FiniteLamp);
			const std::string Text = Dump(S->Input, Pretty);
			const std::unique_ptr<rbsim::LoadedSimInput> L = std::make_unique<rbsim::LoadedSimInput>();
			std::string Error;
			RB_REQUIRE(rbsim::ParseSimInput(Text, *L, Error));
			RB_CHECK(Error.empty());
			RB_CHECK(L->MissingParams == 0);
			RB_CHECK(L->Input.Table == &L->Geometry);
			RB_CHECK(SameInput(S->Input, L->Input));
			RB_CHECK(Dump(L->Input, Pretty) == Text);

			double Slope = 0.0;
			RB_CHECK(rb::GetPhysicsParam(L->Input.Params, "tilt.slope_x", Slope) && Slope == 1.3e-3);
			RB_CHECK(L->Input.Params.ChalkCling && L->Input.Params.BallBall.ClingFactor == 1.3);
			RB_CHECK(L->Input.Balls[13].ChalkMarks.Size() == 3);
			RB_CHECK(FiniteLamp || L->Input.Environment.LampUndersideZ == rb::kInfinity);

			// The rebuilt geometry is the original one (BuildTableGeometry is a pure function of the spec).
			const rb::TableGeometry& A = S->Geometry;
			const rb::TableGeometry& B = L->Geometry;
			RB_REQUIRE(A.Pockets.Size() == B.Pockets.Size() && A.RailTops.Size() == B.RailTops.Size());
			for (int k = 0; k < A.Pockets.Size(); ++k)
			{
				RB_CHECK(SameBits(A.Pockets[k].CaptureCenter.x, B.Pockets[k].CaptureCenter.x) &&
					SameBits(A.Pockets[k].DropEdgeRadius, B.Pockets[k].DropEdgeRadius));
			}
			for (int k = 0; k < A.Noses.Size(); ++k)
			{
				RB_CHECK(SameBits(A.Noses[k].Start.x, B.Noses[k].Start.x) && SameBits(A.Noses[k].End.y, B.Noses[k].End.y));
			}
		}
	}
}

// The reader behind --in is strict and exact.
RB_TEST(Rbsim_JsonReaderIsStrictAndExact)
{
	rbsim::JsonValue V;
	std::string Error;
	RB_REQUIRE(rbsim::ParseJson(" {\"a\": [1, -2.5e-3, true, null, \"x\\\"\\u00e9\\ud83d\\ude00\"], \"b\": {}} ", V, Error));
	RB_REQUIRE(V.IsObject() && V.Find("a") != nullptr && V.Find("a")->Items.size() == 5u);
	RB_CHECK(V.Find("a")->Items[1].NumberValue == -2.5e-3);
	RB_CHECK(V.Find("a")->Items[2].BoolValue && V.Find("a")->Items[3].IsNull());
	RB_CHECK(V.Find("a")->Items[4].StringValue == "x\"\xC3\xA9\xF0\x9F\x98\x80");
	RB_CHECK(V.Find("b")->IsObject() && V.Find("c") == nullptr);

	const char* Bad[] = {"", "{", "[1,]", "{\"a\" 1}", "01", "1.", ".5", "+1", "nan", "Infinity", "\"\\x\"", "\"\\ud800\"", "[1] 2", "tru", "{\"a\":1,}",
		"\"a\nb\""};
	for (const char* Text : Bad)
	{
		RB_CHECK(!rbsim::ParseJson(Text, V, Error));
		RB_CHECK(!Error.empty());
	}
	std::string Deep(200, '[');
	Deep += std::string(200, ']');
	RB_CHECK(!rbsim::ParseJson(Deep, V, Error));

	// 17 significant digits through the writer and back: every double bitwise (random bit patterns incl. subnormals).
	rb::Rng Rng(99);
	for (int i = 0; i < 20000; ++i)
	{
		double X = std::bit_cast<double>(Rng.NextU64());
		if (!std::isfinite(X))
		{
			continue;
		}
		rbsim::JsonWriter J(false);
		J.BeginArray();
		J.Number(X);
		J.Real(-X);
		J.EndArray();
		RB_REQUIRE(rbsim::ParseJson(J.Text(), V, Error));
		RB_CHECK(SameBits(V.Items[0].NumberValue, X) && SameBits(V.Items[1].NumberValue, -X));
	}
	rbsim::JsonWriter J(false);
	J.BeginArray();
	J.Real(rb::kInfinity);
	J.Real(-rb::kInfinity);
	J.Number(rb::kInfinity);
	J.EndArray();
	RB_CHECK(J.Text() == "[\"Infinity\",\"-Infinity\",null]");
}

// Schema errors are reported with the field path; a dump without some parameter keys still loads (counted).
RB_TEST(Rbsim_SimInputReaderReportsSchemaErrors)
{
	const std::unique_ptr<Scenario> S = MakeScenario(true);
	const std::string Text = Dump(S->Input, true);
	const std::unique_ptr<rbsim::LoadedSimInput> L = std::make_unique<rbsim::LoadedSimInput>();
	std::string Error;

	auto Replace = [](std::string T, const char* From, const char* To) {
		const std::size_t At = T.find(From);
		if (At != std::string::npos)
		{
			T.replace(At, std::strlen(From), To);
		}
		return T;
	};
	RB_CHECK(!rbsim::ParseSimInput(Replace(Text, "\"rbsimInput\": 1", "\"rbsimInput\": 2"), *L, Error));
	RB_CHECK(Error.find("version") != std::string::npos);
	RB_CHECK(!rbsim::ParseSimInput(Replace(Text, "\"strength\"", "\"strenght\""), *L, Error));
	RB_CHECK(Error.find("balls[0].chalkMarks[0].strength") != std::string::npos);
	RB_CHECK(!rbsim::ParseSimInput(Replace(Text, "\"cloth.mu_s\"", "\"cloth.mu_x\""), *L, Error));
	RB_CHECK(Error.find("params.cloth.mu_x") != std::string::npos);
	RB_CHECK(!rbsim::ParseSimInput(Replace(Text, "\"state\": \"Stationary\"", "\"state\": \"Hovering\""), *L, Error));
	RB_CHECK(!rbsim::ParseSimInput(Replace(Text, "\"inHand\": 2", "\"inHand\": 9"), *L, Error));
	RB_CHECK(!rbsim::ParseSimInput(Replace(Text, "\"cutAngle\": ", "\"cutAngle\": 0, \"ignored\": "), *L, Error)); // the geometry builder rejects it
	RB_CHECK(Error.find("BuildTableGeometry") != std::string::npos);
	RB_CHECK(!rbsim::ParseSimInput("[1, 2]", *L, Error));

	// A missing parameter key keeps MakePhysicsParams' value and is counted.
	std::string NoKey = Text;
	const std::size_t At = NoKey.find("\"cushion.mathavan_steps\": 40,");
	RB_REQUIRE(At != std::string::npos);
	NoKey.erase(At, std::strlen("\"cushion.mathavan_steps\": 40,"));
	RB_REQUIRE(rbsim::ParseSimInput(NoKey, *L, Error));
	RB_CHECK(L->MissingParams == 1);
	double Steps = 0.0;
	double Default = 0.0;
	RB_CHECK(rb::GetPhysicsParam(L->Input.Params, "cushion.mathavan_steps", Steps));
	RB_CHECK(rb::GetPhysicsParam(rb::MakePhysicsParams(L->Geometry.Spec), "cushion.mathavan_steps", Default));
	RB_CHECK(Steps == Default);
}

// A-TOOL-1 (event-log half; needs the simulator, WP-6a/6b): replaying the parsed dump reproduces the event log bitwise.
RB_TEST(Integ_ARCH_TOOL1_ReplayReproducesEventLogBitwise)
{
	const std::unique_ptr<Scenario> S = MakeScenario(true);
	rb::SimInput& In = S->Input;
	In.Context = rb::ShotContext{};
	In.Balls[13].State = rb::BallState{};
	In.Balls[13].State.Position = {0.4, 0.1, In.Balls[13].Spec.Radius};
	In.Balls[0].State.Position = {-0.5, -0.25, In.Balls[0].Spec.Radius};
	In.Balls[1].State.Position = {-0.5, 0.25, In.Balls[1].Spec.Radius};
	In.Strikes[0].Input.Azimuth = 0.2;
	In.Strikes[0].Input.Elevation = 0.1;
	In.Strikes[0].Input.OffsetA = 0.2;
	In.Strikes[0].Input.OffsetB = 0.1;
	In.Strikes[1].Input.Azimuth = 0.0;
	In.Record = rb::RecordOptions{};
	const std::unique_ptr<rbsim::LoadedSimInput> L = std::make_unique<rbsim::LoadedSimInput>();
	std::string Error;
	RB_REQUIRE(rbsim::ParseSimInput(Dump(In, false), *L, Error));

	rb::Simulator Sim;
	const std::unique_ptr<rb::ShotResult> A = std::make_unique<rb::ShotResult>();
	const std::unique_ptr<rb::ShotResult> B = std::make_unique<rb::ShotResult>();
	RB_REQUIRE(Sim.Run(In, *A) == rb::SimStatus::Ok);
	RB_REQUIRE(Sim.Run(L->Input, *B) == rb::SimStatus::Ok);
	RB_CHECK(A->Events.size() > 4u);
	RB_REQUIRE(A->Events.size() == B->Events.size());
	for (std::size_t i = 0; i < A->Events.size(); ++i)
	{
		RB_CHECK(SameShotEvent(A->Events[i], B->Events[i]));
	}
	RB_CHECK(SameBits(A->StopTime, B->StopTime));
}
