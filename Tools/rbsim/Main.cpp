// rbsim - run one shot through BilliardsCore and write JSON (event log + sampled trajectories) for
// debugging, visualisation, calibration (motion spec implementation note 14) and replays.
// Owner: WP-7 (output, playback & tools). Interface and JSON schema: Docs/architecture.md, section "rbsim".

#include "JsonWriter.h"

#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ParamTable.h"
#include "rb/Physics/Playback.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Rules/TableRules.h"
#include "rb/Shot/ShotRecordBuilder.h"
#include "rb/Version.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	struct PlacedBall
	{
		int Id = 0;
		double Values[9] = {}; // x, y [, z, vx, vy, vz, wx, wy, wz]
		int Count = 0;
	};

	// Additional strike (--strike): ball, V, aim, elevation and optional contact offsets; same cue preset.
	struct ExtraStrike
	{
		int Ball = 0;
		double Speed = 0.0;
		double AimDeg = 0.0;
		double ElevationDeg = 0.0;
		double OffsetA = 0.0;
		double OffsetB = 0.0;
	};

	struct Options
	{
		rb::TablePreset Table = rb::TablePreset::NineFootPro;
		int Cloth = -1; // -1 = the table's cloth, otherwise rb::ClothPreset
		rb::BallSetPreset Balls = rb::BallSetPreset::StandardPool;
		int Rack = -1;  // -1 none, otherwise rb::rules::Discipline
		rb::RackGapParams RackGap = rb::kRackGapWoodenRack;
		std::uint64_t Seed = 1;
		std::vector<PlacedBall> Placed;
		rb::CuePreset Cue = rb::CuePreset::Playing19oz;
		int StrikeBall = rb::kCueBallId;
		double Speed = 0.0;
		double AimDeg = 0.0;
		bool AimGiven = false;
		double ElevationDeg = 0.0;
		double OffsetA = 0.0;
		double OffsetB = 0.0;
		bool AxisOffsets = false;
		bool Squirt = true;
		std::vector<ExtraStrike> ExtraStrikes;
		std::vector<std::string> ParamOverrides; // "key=value"
		bool ListParams = false;
		const char* InPath = nullptr;
		const char* DumpInputPath = nullptr;
		const char* OutPath = nullptr;
		double SampleDt = 0.01;
		bool Trajectories = true;
		bool EventStates = true;
		bool Record = false;
		bool Facts = false;
		int Bench = 0;
		bool Compact = false;
		bool Geometry = false;
	};

	void PrintUsage()
	{
		std::fprintf(stderr,
			"rbsim %s - BilliardsCore shot runner\n"
			"Scenario:\n"
			"  --table 9ft-pro|9ft-tight|8ft-pro|8ft-home|7ft-bar|7ft-78|7ft-true   (default 9ft-pro)\n"
			"  --cloth table|default|fast|bar           cloth preset (default: the table's)\n"
			"  --balls standard|divebar|oldbar|snooker|blackball  ball set (default standard)\n"
			"  --rack none|8ball|9ball|10ball|14.1      rack the object balls (default none)\n"
			"  --rack-gap none|tight|wooden|sloppy|mixture  rack micro-gaps (default wooden)\n"
			"  --seed N                                 rack / ball-set seed (default 1)\n"
			"  --ball ID:X,Y                            ball at rest at (X, Y) [m] (repeatable; default CB on the head spot)\n"
			"  --state ID:X,Y,Z,VX,VY,VZ,WX,WY,WZ       explicit initial state (repeatable)\n"
			"Strikes (t = 0):\n"
			"  --cue playing|break|jump|house           cue preset (default playing)\n"
			"  --speed V                                tip speed [m/s] of the primary strike (default 0 = no strike)\n"
			"  --strike-ball ID                         ball struck by the primary strike (default 0)\n"
			"  --aim DEG  --elevation DEG               azimuth (default 0 = +x) and elevation\n"
			"  --offset A,B                             contact-point offsets / R\n"
			"  --axis-offset A,B                        cue-axis offsets / R (converted with the tip radius)\n"
			"  --no-squirt\n"
			"  --strike ID:V,AIM,ELEV[,A,B]             additional strike, same cue (the lag: two strikes at t = 0)\n"
			"Parameters:\n"
			"  --param KEY=VALUE                        override one PhysicsParams field (repeatable, see --list-params)\n"
			"  --list-params                            print every key, its value for the chosen table and its unit\n"
			"Replay:\n"
			"  --dump-input FILE                        write the complete SimInput as JSON (rbsimInput schema)\n"
			"  --in FILE                                simulate a dumped SimInput (scenario options ignored) [TODO(WP-7)]\n"
			"Output:\n"
			"  --out FILE        JSON file (default stdout)     --dt S   sample interval (default 0.01, 0 = none)\n"
			"  --no-trajectories --no-states --record --facts --compact --bench N --geometry\n",
			rb::CoreVersion());
	}

	bool ParseNumbers(const char* Text, double* Out, int MaxCount, int& Count)
	{
		Count = 0;
		const char* p = Text;
		while (*p != '\0' && Count < MaxCount)
		{
			char* End = nullptr;
			Out[Count] = std::strtod(p, &End);
			if (End == p)
			{
				return false;
			}
			++Count;
			p = End;
			if (*p == ',')
			{
				++p;
			}
			else if (*p != '\0')
			{
				return false;
			}
		}
		return *p == '\0';
	}

	bool ParseBall(const char* Text, PlacedBall& Out, int Expected)
	{
		const char* Colon = std::strchr(Text, ':');
		if (Colon == nullptr)
		{
			return false;
		}
		Out.Id = std::atoi(Text);
		return ParseNumbers(Colon + 1, Out.Values, 9, Out.Count) && Out.Count == Expected && Out.Id >= 0 && Out.Id < rb::kMaxBalls;
	}

	bool ParseStrike(const char* Text, ExtraStrike& Out)
	{
		const char* Colon = std::strchr(Text, ':');
		if (Colon == nullptr)
		{
			return false;
		}
		Out.Ball = std::atoi(Text);
		double V[5] = {};
		int N = 0;
		if (!ParseNumbers(Colon + 1, V, 5, N) || (N != 3 && N != 5) || Out.Ball < 0 || Out.Ball >= rb::kMaxBalls)
		{
			return false;
		}
		Out.Speed = V[0];
		Out.AimDeg = V[1];
		Out.ElevationDeg = V[2];
		Out.OffsetA = N == 5 ? V[3] : 0.0;
		Out.OffsetB = N == 5 ? V[4] : 0.0;
		return true;
	}

	bool Is(const char* A, const char* B) { return std::strcmp(A, B) == 0; }

	bool ParseArgs(int Argc, char** Argv, Options& O)
	{
		for (int i = 1; i < Argc; ++i)
		{
			const char* Arg = Argv[i];
			const char* Val = (i + 1 < Argc) ? Argv[i + 1] : "";
			auto Next = [&]() { ++i; return Val; };
			if (Is(Arg, "--help") || Is(Arg, "-h")) { return false; }
			else if (Is(Arg, "--table"))
			{
				const char* V = Next();
				if (Is(V, "9ft-pro")) O.Table = rb::TablePreset::NineFootPro;
				else if (Is(V, "9ft-tight")) O.Table = rb::TablePreset::NineFootTight;
				else if (Is(V, "8ft-pro")) O.Table = rb::TablePreset::EightFootPro;
				else if (Is(V, "8ft-home")) O.Table = rb::TablePreset::EightFootHome;
				else if (Is(V, "7ft-bar")) O.Table = rb::TablePreset::SevenFootBar;
				else if (Is(V, "7ft-78")) O.Table = rb::TablePreset::SevenFoot78;
				else if (Is(V, "7ft-true")) O.Table = rb::TablePreset::SevenFootTrue;
				else return false;
			}
			else if (Is(Arg, "--cloth"))
			{
				const char* V = Next();
				if (Is(V, "table")) O.Cloth = -1;
				else if (Is(V, "default")) O.Cloth = static_cast<int>(rb::ClothPreset::Default);
				else if (Is(V, "fast")) O.Cloth = static_cast<int>(rb::ClothPreset::WorstedFast);
				else if (Is(V, "bar")) O.Cloth = static_cast<int>(rb::ClothPreset::NappedBar);
				else return false;
			}
			else if (Is(Arg, "--balls"))
			{
				const char* V = Next();
				if (Is(V, "standard")) O.Balls = rb::BallSetPreset::StandardPool;
				else if (Is(V, "divebar")) O.Balls = rb::BallSetPreset::DiveBar;
				else if (Is(V, "oldbar")) O.Balls = rb::BallSetPreset::OldBarOversizedCue;
				else if (Is(V, "snooker")) O.Balls = rb::BallSetPreset::Snooker;
				else if (Is(V, "blackball")) O.Balls = rb::BallSetPreset::Blackball;
				else return false;
			}
			else if (Is(Arg, "--rack"))
			{
				const char* V = Next();
				if (Is(V, "none")) O.Rack = -1;
				else if (Is(V, "8ball")) O.Rack = static_cast<int>(rb::rules::Discipline::EightBall);
				else if (Is(V, "9ball")) O.Rack = static_cast<int>(rb::rules::Discipline::NineBall);
				else if (Is(V, "10ball")) O.Rack = static_cast<int>(rb::rules::Discipline::TenBall);
				else if (Is(V, "14.1")) O.Rack = static_cast<int>(rb::rules::Discipline::StraightPool);
				else return false;
			}
			else if (Is(Arg, "--rack-gap"))
			{
				const char* V = Next();
				if (Is(V, "none")) O.RackGap = rb::kRackGapNone;
				else if (Is(V, "tight")) O.RackGap = rb::kRackGapTightTemplate;
				else if (Is(V, "wooden")) O.RackGap = rb::kRackGapWoodenRack;
				else if (Is(V, "sloppy")) O.RackGap = rb::kRackGapSloppyBar;
				else if (Is(V, "mixture")) O.RackGap = rb::kRackGapMixture;
				else return false;
			}
			else if (Is(Arg, "--seed")) { O.Seed = std::strtoull(Next(), nullptr, 10); }
			else if (Is(Arg, "--ball") || Is(Arg, "--state"))
			{
				PlacedBall B;
				if (!ParseBall(Next(), B, Is(Arg, "--ball") ? 2 : 9)) return false;
				O.Placed.push_back(B);
			}
			else if (Is(Arg, "--cue"))
			{
				const char* V = Next();
				if (Is(V, "playing")) O.Cue = rb::CuePreset::Playing19oz;
				else if (Is(V, "break")) O.Cue = rb::CuePreset::Break21oz;
				else if (Is(V, "jump")) O.Cue = rb::CuePreset::Jump9oz;
				else if (Is(V, "house")) O.Cue = rb::CuePreset::House19oz;
				else return false;
			}
			else if (Is(Arg, "--speed")) { O.Speed = std::strtod(Next(), nullptr); }
			else if (Is(Arg, "--strike-ball"))
			{
				O.StrikeBall = std::atoi(Next());
				if (O.StrikeBall < 0 || O.StrikeBall >= rb::kMaxBalls) return false;
			}
			else if (Is(Arg, "--aim")) { O.AimDeg = std::strtod(Next(), nullptr); O.AimGiven = true; }
			else if (Is(Arg, "--elevation")) { O.ElevationDeg = std::strtod(Next(), nullptr); }
			else if (Is(Arg, "--offset") || Is(Arg, "--axis-offset"))
			{
				double V[2] = {};
				int N = 0;
				if (!ParseNumbers(Next(), V, 2, N) || N != 2) return false;
				O.OffsetA = V[0];
				O.OffsetB = V[1];
				O.AxisOffsets = Is(Arg, "--axis-offset");
			}
			else if (Is(Arg, "--no-squirt")) { O.Squirt = false; }
			else if (Is(Arg, "--strike"))
			{
				ExtraStrike S;
				if (!ParseStrike(Next(), S)) return false;
				O.ExtraStrikes.push_back(S);
			}
			else if (Is(Arg, "--param")) { O.ParamOverrides.emplace_back(Next()); }
			else if (Is(Arg, "--list-params")) { O.ListParams = true; }
			else if (Is(Arg, "--in")) { O.InPath = Next(); }
			else if (Is(Arg, "--dump-input")) { O.DumpInputPath = Next(); }
			else if (Is(Arg, "--out")) { O.OutPath = Next(); }
			else if (Is(Arg, "--dt")) { O.SampleDt = std::strtod(Next(), nullptr); }
			else if (Is(Arg, "--no-trajectories")) { O.Trajectories = false; }
			else if (Is(Arg, "--no-states")) { O.EventStates = false; }
			else if (Is(Arg, "--record")) { O.Record = true; }
			else if (Is(Arg, "--facts")) { O.Facts = true; O.Record = true; }
			else if (Is(Arg, "--compact")) { O.Compact = true; }
			else if (Is(Arg, "--bench")) { O.Bench = std::atoi(Next()); }
			else if (Is(Arg, "--geometry")) { O.Geometry = true; }
			else
			{
				std::fprintf(stderr, "rbsim: unknown option %s\n", Arg);
				return false;
			}
		}
		return true;
	}

	const char* ToString(rb::MotionState S)
	{
		switch (S)
		{
		case rb::MotionState::Stationary: return "Stationary";
		case rb::MotionState::Spinning: return "Spinning";
		case rb::MotionState::Sliding: return "Sliding";
		case rb::MotionState::Rolling: return "Rolling";
		case rb::MotionState::Airborne: return "Airborne";
		case rb::MotionState::PocketPivot: return "PocketPivot";
		case rb::MotionState::PocketFall: return "PocketFall";
		case rb::MotionState::Pocketed: return "Pocketed";
		case rb::MotionState::OffTable: return "OffTable";
		}
		return "?";
	}

	const char* ToString(rb::ShotEventType T)
	{
		switch (T)
		{
		case rb::ShotEventType::CueStrike: return "CueStrike";
		case rb::ShotEventType::TipRecontact: return "TipRecontact";
		case rb::ShotEventType::TipContactBegin: return "TipContactBegin";
		case rb::ShotEventType::TipContactEnd: return "TipContactEnd";
		case rb::ShotEventType::BallBall: return "BallBall";
		case rb::ShotEventType::BallCushion: return "BallCushion";
		case rb::ShotEventType::BallJaw: return "BallJaw";
		case rb::ShotEventType::BallRailTop: return "BallRailTop";
		case rb::ShotEventType::BallSlate: return "BallSlate";
		case rb::ShotEventType::BallAirborne: return "BallAirborne";
		case rb::ShotEventType::BallLand: return "BallLand";
		case rb::ShotEventType::BallPocketEnter: return "BallPocketEnter";
		case rb::ShotEventType::BallPocketRim: return "BallPocketRim";
		case rb::ShotEventType::BallLiner: return "BallLiner";
		case rb::ShotEventType::BallPocketExit: return "BallPocketExit";
		case rb::ShotEventType::BallPocketed: return "BallPocketed";
		case rb::ShotEventType::BallOffTable: return "BallOffTable";
		case rb::ShotEventType::BallExternalContact: return "BallExternalContact";
		case rb::ShotEventType::MotionTransition: return "MotionTransition";
		case rb::ShotEventType::BallLineCross: return "BallLineCross";
		case rb::ShotEventType::BallJumpedOver: return "BallJumpedOver";
		case rb::ShotEventType::IslandBegin: return "IslandBegin";
		case rb::ShotEventType::IslandRigid: return "IslandRigid";
		case rb::ShotEventType::IslandEnd: return "IslandEnd";
		case rb::ShotEventType::ZenoGuard: return "ZenoGuard";
		case rb::ShotEventType::Diagnostic: return "Diagnostic";
		case rb::ShotEventType::TiltRefresh: return "TiltRefresh";
		}
		return "?";
	}

	const char* ToString(rb::SimStatus S)
	{
		switch (S)
		{
		case rb::SimStatus::Ok: return "Ok";
		case rb::SimStatus::InvalidInput: return "InvalidInput";
		case rb::SimStatus::Aborted: return "Aborted";
		case rb::SimStatus::HorizonReached: return "HorizonReached";
		case rb::SimStatus::NotImplemented: return "NotImplemented";
		}
		return "?";
	}

	const char* ToString(rb::BallFinalStatus S)
	{
		switch (S)
		{
		case rb::BallFinalStatus::NotInPlay: return "NotInPlay";
		case rb::BallFinalStatus::OnTable: return "OnTable";
		case rb::BallFinalStatus::Pocketed: return "Pocketed";
		case rb::BallFinalStatus::OffTable: return "OffTable";
		}
		return "?";
	}

	const char* ToString(rb::RailTopKind K) { return K == rb::RailTopKind::CushionTop ? "cushionTop" : "railCap"; }

	const char* ToString(rb::RailEdgeKind K)
	{
		switch (K)
		{
		case rb::RailEdgeKind::Seam: return "seam";
		case rb::RailEdgeKind::Nose: return "nose";
		case rb::RailEdgeKind::CushionBack: return "cushionBack";
		case rb::RailEdgeKind::OuterEdge: return "outerEdge";
		case rb::RailEdgeKind::Facing: return "facing";
		}
		return "?";
	}

	void WriteVec(rbsim::JsonWriter& J, const char* Key, const rb::Vec3& V)
	{
		const double Values[3] = {V.x, V.y, V.z};
		J.Key(Key);
		J.NumberArray(Values, 3);
	}

	void WriteVec2(rbsim::JsonWriter& J, const char* Key, const rb::Vec2& V)
	{
		const double Values[2] = {V.x, V.y};
		J.Key(Key);
		J.NumberArray(Values, 2);
	}

	void WriteState(rbsim::JsonWriter& J, const rb::BallState& S)
	{
		J.BeginObject();
		WriteVec(J, "r", S.Position);
		WriteVec(J, "v", S.Velocity);
		WriteVec(J, "w", S.Omega);
		J.Field("state", ToString(S.State));
		J.EndObject();
	}

	bool ApplyParamOverrides(const Options& O, rb::PhysicsParams& Params)
	{
		for (const std::string& Override : O.ParamOverrides)
		{
			const std::size_t Eq = Override.find('=');
			if (Eq == std::string::npos)
			{
				std::fprintf(stderr, "rbsim: --param expects KEY=VALUE, got '%s'\n", Override.c_str());
				return false;
			}
			const std::string Key = Override.substr(0, Eq);
			char* End = nullptr;
			const char* ValueText = Override.c_str() + Eq + 1;
			const double Value = std::strtod(ValueText, &End);
			if (End == ValueText || *End != '\0' || !rb::SetPhysicsParam(Params, Key.c_str(), Value))
			{
				std::fprintf(stderr, "rbsim: cannot set parameter '%s' (unknown key or invalid value; see --list-params)\n", Override.c_str());
				return false;
			}
		}
		return true;
	}

	rb::CueStrikeInput MakeStrikeInput(const Options& O, double Speed, double AimDeg, double ElevationDeg, double A, double B, bool AxisOffsets, double BallRadius)
	{
		rb::CueStrikeInput S;
		S.Cue = rb::GetCueSpec(O.Cue);
		S.Speed = Speed;
		S.Elevation = ElevationDeg * rb::kDegToRad;
		S.Azimuth = AimDeg * rb::kDegToRad;
		S.OffsetA = A;
		S.OffsetB = B;
		if (AxisOffsets)
		{
			const rb::Vec2 Contact = rb::AimToContactOffset({A, B}, BallRadius, S.Cue.TipDomeRadius);
			S.OffsetA = Contact.x;
			S.OffsetB = Contact.y;
		}
		S.SquirtEnabled = O.Squirt;
		return S;
	}

	// Builds the simulator input from the options. Returns false on a fatal setup error.
	bool BuildInput(const Options& O, rb::TableGeometry& Geometry, rb::SimInput& In)
	{
		const rb::TableSpec Spec = rb::GetTableSpec(O.Table);
		if (rb::BuildTableGeometry(Spec, Geometry) != rb::ErrorCode::Ok)
		{
			std::fprintf(stderr, "rbsim: warning: BuildTableGeometry not available (%s); continuing with a partial table\n", Spec.Name);
			Geometry.Spec = Spec;
		}
		In.Table = &Geometry;
		In.Params = rb::MakePhysicsParams(Spec); // the single source of table-dependent physics
		if (O.Cloth >= 0)
		{
			In.Params.Cloth = rb::ClothParamsFor(static_cast<rb::ClothPreset>(O.Cloth));
		}
		if (!ApplyParamOverrides(O, In.Params))
		{
			return false;
		}

		rb::BallSet Set;
		if (rb::BuildBallSet(O.Balls, O.Seed, Set) != rb::ErrorCode::Ok)
		{
			Set.Count = rb::kPoolBallCount;
			for (int i = 0; i < Set.Count; ++i)
			{
				Set.Balls[i] = rb::kStandardPoolBall;
			}
		}
		const rb::rules::RulesTable Landmarks = rb::rules::MakeRulesTable(Spec.Length, Spec.Width, Set.Balls[1].Radius);

		// Cue ball on the head spot unless placed explicitly.
		rb::SimBall& Cue = In.Balls[rb::kCueBallId];
		Cue.InPlay = true;
		Cue.Spec = Set.Balls[rb::kCueBallId];
		Cue.State.Position = {Landmarks.HeadSpot.x, Landmarks.HeadSpot.y, Cue.Spec.Radius};

		if (O.Rack >= 0)
		{
			rb::rules::RackAssignment Rack;
			const rb::rules::RulesConfig Config;
			if (rb::rules::GenerateRack(static_cast<rb::rules::Discipline>(O.Rack), Config, Landmarks, O.Seed, false, O.RackGap, Rack) != rb::ErrorCode::Ok)
			{
				std::fprintf(stderr, "rbsim: warning: GenerateRack not available; no rack placed\n");
			}
			for (int Id = 1; Id < rb::kPoolBallCount; ++Id)
			{
				if (Rack.Racked[Id])
				{
					rb::SimBall& B = In.Balls[Id];
					B.InPlay = true;
					B.Spec = Set.Balls[Id];
					B.State.Position = {Rack.Position[Id].x, Rack.Position[Id].y, B.Spec.Radius};
				}
			}
		}

		for (const PlacedBall& P : O.Placed)
		{
			rb::SimBall& B = In.Balls[P.Id];
			B.InPlay = true;
			B.Spec = P.Id < Set.Count ? Set.Balls[P.Id] : rb::kStandardPoolBall;
			B.State = rb::BallState{};
			B.State.Position = {P.Values[0], P.Values[1], P.Count > 2 ? P.Values[2] : B.Spec.Radius};
			if (P.Count == 9)
			{
				B.State.Velocity = {P.Values[3], P.Values[4], P.Values[5]};
				B.State.Omega = {P.Values[6], P.Values[7], P.Values[8]};
			}
			rb::ClassifyState(B.State, B.Spec.Radius, 0.0, In.Params.Numerics);
		}

		if (O.Speed > 0.0)
		{
			if (!In.Balls[O.StrikeBall].InPlay)
			{
				std::fprintf(stderr, "rbsim: --strike-ball %d is not on the table (place it with --ball)\n", O.StrikeBall);
				return false;
			}
			rb::StrikeRequest Request;
			Request.Ball = static_cast<rb::BallId>(O.StrikeBall);
			Request.Input = MakeStrikeInput(O, O.Speed, O.AimGiven ? O.AimDeg : 0.0, O.ElevationDeg, O.OffsetA, O.OffsetB, O.AxisOffsets,
				In.Balls[O.StrikeBall].Spec.Radius);
			In.Strikes.PushBack(Request);
		}
		for (const ExtraStrike& E : O.ExtraStrikes)
		{
			if (!In.Balls[E.Ball].InPlay)
			{
				std::fprintf(stderr, "rbsim: --strike ball %d is not on the table (place it with --ball)\n", E.Ball);
				return false;
			}
			rb::StrikeRequest Request;
			Request.Ball = static_cast<rb::BallId>(E.Ball);
			Request.Input = MakeStrikeInput(O, E.Speed, E.AimDeg, E.ElevationDeg, E.OffsetA, E.OffsetB, false, In.Balls[E.Ball].Spec.Radius);
			if (!In.Strikes.PushBack(Request))
			{
				std::fprintf(stderr, "rbsim: at most %d strikes per simulation\n", rb::kMaxStrikes);
				return false;
			}
		}

		In.Record.Trajectories = O.Trajectories;
		In.Record.EventStates = O.EventStates;
		In.Record.ShotRecord = O.Record;
		return true;
	}

	void WriteTableSpec(rbsim::JsonWriter& J, const rb::TableSpec& S)
	{
		J.BeginObject();
		J.Field("name", S.Name);
		J.FieldInt("preset", static_cast<int>(S.Preset));
		J.Field("length", S.Length);
		J.Field("width", S.Width);
		J.Field("bedHeight", S.BedHeight);
		J.Field("cushionNoseHeight", S.CushionNoseHeight);
		J.Field("cushionWidth", S.CushionWidth);
		J.Field("cushionNoseProfileRadius", S.CushionNoseProfileRadius);
		J.Field("railWidthTotal", S.RailWidthTotal);
		J.Field("railTopZ", S.RailTopZ);
		J.Field("slateThickness", S.SlateThickness);
		J.Field("sightInset", S.SightInset);
		J.Field("sightDiameter", S.SightDiameter);
		const rb::PocketSpec* Pockets[2] = {&S.Corner, &S.Side};
		const char* PocketKeys[2] = {"corner", "side"};
		for (int k = 0; k < 2; ++k)
		{
			J.Key(PocketKeys[k]);
			J.BeginObject();
			J.Field("mouth", Pockets[k]->Mouth);
			J.Field("cutAngle", Pockets[k]->CutAngle);
			J.Field("shelf", Pockets[k]->Shelf);
			J.Field("jawRadius", Pockets[k]->JawRadius);
			J.Field("captureRadius", Pockets[k]->CaptureRadius);
			J.EndObject();
		}
		J.Field("backdraft", S.Backdraft);
		J.Field("dropPointRadius", S.DropPointRadius);
		J.Field("facingThickness", S.FacingThickness);
		J.Field("linerUndercut", S.LinerUndercut);
		J.FieldBool("hasPockets", S.HasPockets);
		J.FieldInt("cloth", static_cast<int>(S.Cloth));
		J.Field("facingRestitutionScale", S.FacingRestitutionScale);
		J.Field("linerRestitution", S.LinerRestitution);
		J.Field("linerFriction", S.LinerFriction);
		J.EndObject();
	}

	// rbsimInput schema v1: everything needed to reproduce Simulator::Run bit for bit (architecture 5.3).
	void WriteSimInput(const rb::SimInput& In, rbsim::JsonWriter& J)
	{
		J.BeginObject();
		J.FieldInt("rbsimInput", 1);
		J.Field("coreVersion", rb::CoreVersion());
		J.Key("tableSpec");
		WriteTableSpec(J, In.Table->Spec);
		J.Key("environment");
		J.BeginObject();
		J.Field("lampUndersideZ", In.Environment.LampUndersideZ);
		const double Footprint[4] = {In.Environment.LampFootprint.Lo.x, In.Environment.LampFootprint.Lo.y, In.Environment.LampFootprint.Hi.x,
			In.Environment.LampFootprint.Hi.y};
		J.Key("lampFootprint");
		J.NumberArray(Footprint, 4);
		J.EndObject();
		J.Key("params");
		J.BeginObject();
		for (int i = 0; i < rb::PhysicsParamCount(); ++i)
		{
			const rb::PhysicsParamInfo Info = rb::PhysicsParamAt(i);
			double Value = 0.0;
			rb::GetPhysicsParam(In.Params, Info.Key, Value);
			J.Field(Info.Key, Value);
		}
		J.EndObject();
		J.Key("balls");
		J.BeginArray();
		for (int Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::SimBall& B = In.Balls[Id];
			if (!B.InPlay)
			{
				continue;
			}
			J.BeginObject();
			J.FieldInt("id", Id);
			J.Field("radius", B.Spec.Radius);
			J.Field("mass", B.Spec.Mass);
			J.Field("inertia", B.Spec.Inertia);
			J.Key("state");
			WriteState(J, B.State);
			const double Q[4] = {B.Orientation.w, B.Orientation.x, B.Orientation.y, B.Orientation.z};
			J.Key("q");
			J.NumberArray(Q, 4);
			J.EndObject();
		}
		J.EndArray();
		J.Key("strikes");
		J.BeginArray();
		for (const rb::StrikeRequest& S : In.Strikes)
		{
			J.BeginObject();
			J.FieldInt("ball", S.Ball);
			J.Field("V", S.Input.Speed);
			J.Field("theta", S.Input.Elevation);
			J.Field("phi", S.Input.Azimuth);
			J.Field("a", S.Input.OffsetA);
			J.Field("b", S.Input.OffsetB);
			J.Field("lambdaOverride", S.Input.LambdaOverride);
			J.FieldBool("squirt", S.Input.SquirtEnabled);
			J.FieldBool("tipTouchesCloth", S.Input.TipTouchesCloth);
			J.Key("cue");
			J.BeginObject();
			J.Field("mass", S.Input.Cue.Mass);
			J.Field("endMass", S.Input.Cue.EndMass);
			J.Field("tipRestitution", S.Input.Cue.TipRestitution);
			J.Field("tipFriction", S.Input.Cue.TipFriction);
			J.Field("tipFrictionKinetic", S.Input.Cue.TipFrictionKinetic);
			J.Field("tipDomeRadius", S.Input.Cue.TipDomeRadius);
			J.Field("tipDiameter", S.Input.Cue.TipDiameter);
			J.Field("length", S.Input.Cue.Length);
			J.Field("contactTime", S.Input.Cue.ContactTime);
			J.Field("followThroughDistance", S.Input.Cue.FollowThroughDistance);
			J.FieldBool("jumpCue", S.Input.Cue.JumpCue);
			J.EndObject();
			J.EndObject();
		}
		J.EndArray();
		J.Key("context");
		J.BeginObject();
		J.FieldInt("inHand", static_cast<int>(In.Context.InHand));
		WriteVec2(J, "placed", In.Context.PlacedPosition);
		J.FieldBool("templatePresent", In.Context.TemplatePresent);
		J.Field("shotClockElapsed", In.Context.ShotClockElapsed);
		J.FieldBool("footOnFloor", In.Context.FootOnFloor);
		J.Field("frozenTolerance", In.Context.FrozenTolerance);
		J.Key("nonTipContacts");
		J.BeginArray();
		for (const rb::NonTipContact& C : In.Context.NonTipContacts)
		{
			J.BeginObject();
			J.FieldInt("ball", C.Ball);
			J.FieldInt("source", static_cast<int>(C.Source));
			J.Field("t", C.Time);
			J.EndObject();
		}
		J.EndArray();
		J.EndObject();
		J.Key("record");
		J.BeginObject();
		J.FieldBool("trajectories", In.Record.Trajectories);
		J.FieldBool("eventStates", In.Record.EventStates);
		J.FieldBool("logTransitions", In.Record.LogTransitions);
		J.FieldBool("logObservers", In.Record.LogObservers);
		J.FieldBool("shotRecord", In.Record.ShotRecord);
		J.EndObject();
		J.EndObject();
	}

	// Single-source-of-truth export for the render mesh generator (ue5-realism-plan 6.7).
	void WriteGeometry(rbsim::JsonWriter& J, const rb::TableGeometry& G)
	{
		J.Key("geometry");
		J.BeginObject();
		J.Field("name", G.Spec.Name);
		J.Field("length", G.Spec.Length);
		J.Field("width", G.Spec.Width);
		J.Field("noseHeight", G.Spec.CushionNoseHeight);
		J.Field("railTopZ", G.Spec.RailTopZ);
		J.Field("railWidthTotal", G.Spec.RailWidthTotal);
		J.Key("noses");
		J.BeginArray();
		for (const rb::NoseSegment& N : G.Noses)
		{
			J.BeginObject();
			J.FieldInt("cushion", static_cast<int>(N.Cushion));
			J.FieldBool("present", N.Present);
			WriteVec2(J, "start", N.Start);
			WriteVec2(J, "end", N.End);
			WriteVec2(J, "inwardNormal", N.InwardNormal);
			J.EndObject();
		}
		J.EndArray();
		J.Key("jawArcs");
		J.BeginArray();
		for (const rb::JawArc& A : G.JawArcs)
		{
			J.BeginObject();
			J.FieldInt("pocket", static_cast<int>(A.Pocket));
			J.FieldInt("side", static_cast<int>(A.Side));
			WriteVec2(J, "center", A.Center);
			J.Field("radius", A.Radius);
			J.Field("angleFrom", A.AngleFrom);
			J.Field("angleSweep", A.AngleSweep);
			J.EndObject();
		}
		J.EndArray();
		J.Key("facings");
		J.BeginArray();
		for (const rb::Facing& F : G.Facings)
		{
			J.BeginObject();
			J.FieldInt("pocket", static_cast<int>(F.Pocket));
			J.FieldInt("side", static_cast<int>(F.Side));
			WriteVec2(J, "start", F.Start);
			WriteVec2(J, "end", F.End);
			J.Field("backdraft", F.Backdraft);
			J.Field("thickness", F.Thickness);
			J.EndObject();
		}
		J.EndArray();
		J.Key("pockets");
		J.BeginArray();
		for (const rb::PocketGeometry& P : G.Pockets)
		{
			J.BeginObject();
			J.FieldInt("id", static_cast<int>(P.Id));
			J.Field("kind", P.Kind == rb::PocketKind::Corner ? "corner" : "side");
			WriteVec2(J, "jawIncoming", P.JawPoint[0]);
			WriteVec2(J, "jawOutgoing", P.JawPoint[1]);
			WriteVec2(J, "captureCenter", P.CaptureCenter);
			J.Field("captureRadius", P.CaptureRadius);
			J.Field("dropRadius", P.DropRadius);
			J.Field("dropEdgeRadius", P.DropEdgeRadius);
			J.Field("shelf", P.Shelf);
			J.Field("wallTopZ", P.WallTopZ);
			J.EndObject();
		}
		J.EndArray();
		J.Key("railTops");
		J.BeginArray();
		for (const rb::RailTopPolygon& R : G.RailTops)
		{
			J.BeginObject();
			J.Field("kind", ToString(R.Kind));
			J.FieldInt("cushion", static_cast<int>(R.Cushion));
			J.FieldInt("pocket", static_cast<int>(R.Pocket));
			WriteVec(J, "planePoint", R.PlanePoint);
			WriteVec(J, "planeNormal", R.PlaneNormal);
			J.Key("vertices");
			J.BeginArray();
			for (int v = 0; v < R.VertexCount; ++v)
			{
				const double Values[2] = {R.Vertices[v].x, R.Vertices[v].y};
				J.NumberArray(Values, 2);
			}
			J.EndArray();
			J.Key("edges");
			J.BeginArray();
			for (int v = 0; v < R.VertexCount; ++v)
			{
				J.String(ToString(R.Edges[v]));
			}
			J.EndArray();
			if (R.HasCut)
			{
				WriteVec2(J, "cutCenter", R.CutCenter);
				J.Field("cutRadius", R.CutRadius);
			}
			J.EndObject();
		}
		J.EndArray();
		J.Key("sights");
		J.BeginArray();
		for (const rb::Sight& S : G.Sights)
		{
			const double Values[3] = {S.Position.x, S.Position.y, S.Position.z};
			J.NumberArray(Values, 3);
		}
		J.EndArray();
		J.Key("profile");
		J.BeginArray();
		for (const rb::Vec2& P : G.Profile.Points)
		{
			const double Values[2] = {P.x, P.y};
			J.NumberArray(Values, 2);
		}
		J.EndArray();
		std::vector<rb::Vec2> Outline(4096);
		const int N = rb::BuildNoseOutline(G, 16, Outline.data(), static_cast<int>(Outline.size()));
		J.Key("noseOutline");
		J.BeginArray();
		for (int i = 0; i < N; ++i)
		{
			const double Values[2] = {Outline[static_cast<std::size_t>(i)].x, Outline[static_cast<std::size_t>(i)].y};
			J.NumberArray(Values, 2);
		}
		J.EndArray();
		J.EndObject();
	}

	void WriteJson(const Options& O, const rb::SimInput& In, const rb::ShotResult& R, rbsim::JsonWriter& J)
	{
		J.BeginObject();
		J.FieldInt("rbsim", 2);
		J.Field("coreVersion", rb::CoreVersion());

		J.Key("input");
		J.BeginObject();
		J.Field("table", In.Table->Spec.Name);
		J.Field("gravity", In.Params.Gravity);
		J.Key("cloth");
		J.BeginObject();
		J.Field("mu_s", In.Params.Cloth.SlidingFriction);
		J.Field("mu_r", In.Params.Cloth.RollingResistance);
		J.Field("alpha_sp", In.Params.Cloth.SpinDeceleration);
		J.EndObject();
		J.Key("strikes");
		J.BeginArray();
		for (const rb::StrikeRequest& S : In.Strikes)
		{
			J.BeginObject();
			J.FieldInt("ball", S.Ball);
			J.Field("V", S.Input.Speed);
			J.Field("theta", S.Input.Elevation);
			J.Field("phi", S.Input.Azimuth);
			J.Field("a", S.Input.OffsetA);
			J.Field("b", S.Input.OffsetB);
			J.Field("cueMass", S.Input.Cue.Mass);
			J.EndObject();
		}
		J.EndArray();
		J.Key("paramOverrides");
		J.BeginArray();
		for (const std::string& Override : O.ParamOverrides)
		{
			J.String(Override.c_str());
		}
		J.EndArray();
		J.FieldInt("seed", static_cast<std::int64_t>(O.Seed));
		J.EndObject();

		J.Field("status", ToString(R.Status));
		J.Field("stopTime", R.StopTime);

		J.Key("diagnostics");
		J.BeginObject();
		J.Field("inputError", rb::ToString(R.Diagnostics.InputError));
		J.FieldInt("eventsProcessed", R.Diagnostics.EventsProcessed);
		J.FieldInt("staleEventsSkipped", R.Diagnostics.StaleEventsSkipped);
		J.FieldInt("predictions", R.Diagnostics.Predictions);
		J.FieldInt("islands", R.Diagnostics.Islands);
		J.FieldInt("islandSteps", R.Diagnostics.IslandSteps);
		J.FieldInt("islandRigidSwitches", R.Diagnostics.IslandRigidSwitches);
		J.FieldBool("islandBudgetExceeded", R.Diagnostics.IslandBudgetExceeded);
		J.FieldInt("zenoTriggers", R.Diagnostics.ZenoTriggers);
		J.FieldInt("overlapWarnings", R.Diagnostics.OverlapWarnings);
		J.FieldInt("missedEvents", R.Diagnostics.MissedEvents);
		J.EndObject();

		J.Key("strikeResults");
		J.BeginArray();
		for (const rb::StrikeOutcome& S : R.Strikes)
		{
			J.BeginObject();
			J.FieldInt("ball", S.Ball);
			J.Field("error", rb::ToString(S.Result.Error));
			J.Field("impulse", S.Result.Impulse);
			J.Field("squirt", S.Result.SquirtAngle);
			J.FieldBool("miscue", S.Result.Miscue);
			J.Field("cueSpeedAfter", S.Result.CueSpeedAfter);
			J.Key("state");
			WriteState(J, S.Result.State);
			J.EndObject();
		}
		J.EndArray();

		J.Key("events");
		J.BeginArray();
		for (const rb::ShotEvent& E : R.Events)
		{
			J.BeginObject();
			J.Field("t", E.Time);
			J.Field("type", ToString(E.Type));
			J.FieldInt("a", E.A);
			J.FieldInt("b", E.B);
			J.FieldInt("feature", E.Feature);
			J.FieldInt("sub", E.SubFeature);
			J.FieldInt("flags", E.Flags);
			WriteVec(J, "normal", E.Normal);
			J.Field("vn", E.NormalSpeed);
			J.Field("jn", E.NormalImpulse);
			J.Field("jt", E.TangentImpulse);
			J.Field("cut", E.CutAngle);
			J.Field("value", E.Value);
			if (E.Type == rb::ShotEventType::MotionTransition)
			{
				J.Field("from", ToString(E.From));
				J.Field("to", ToString(E.To));
			}
			if (O.EventStates)
			{
				J.Key("pre");
				J.BeginArray();
				WriteState(J, E.Pre[0]);
				WriteState(J, E.Pre[1]);
				J.EndArray();
				J.Key("post");
				J.BeginArray();
				WriteState(J, E.Post[0]);
				WriteState(J, E.Post[1]);
				J.EndArray();
			}
			J.EndObject();
		}
		J.EndArray();

		J.Key("balls");
		J.BeginArray();
		std::vector<rb::TrajectorySample> Samples(static_cast<std::size_t>(1) << 16);
		for (int Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			if (!In.Balls[Id].InPlay)
			{
				continue;
			}
			const rb::BallFinal& F = R.Finals[Id];
			J.BeginObject();
			J.FieldInt("id", Id);
			J.Field("radius", In.Balls[Id].Spec.Radius);
			J.Field("mass", In.Balls[Id].Spec.Mass);
			J.Key("initial");
			WriteState(J, In.Balls[Id].State);
			J.Field("final", ToString(F.Status));
			J.Key("finalState");
			WriteState(J, F.State);
			J.FieldInt("pocket", static_cast<int>(F.Pocket));
			J.FieldInt("segments", static_cast<std::int64_t>(R.Tracks[Id].Segments.size()));
			if (O.Trajectories && O.SampleDt > 0.0)
			{
				const int N = rb::SampleTrajectory(R, Id, O.SampleDt, Samples.data(), static_cast<int>(Samples.size()));
				J.Key("samples"); // [t, x, y, z, qw, qx, qy, qz, stateIndex]
				J.BeginArray();
				for (int k = 0; k < N; ++k)
				{
					const rb::TrajectorySample& S = Samples[static_cast<std::size_t>(k)];
					const double Row[9] = {S.Time, S.Position.x, S.Position.y, S.Position.z, S.Orientation.w, S.Orientation.x, S.Orientation.y, S.Orientation.z,
						static_cast<double>(static_cast<int>(S.State))};
					J.NumberArray(Row, 9);
				}
				J.EndArray();
			}
			J.EndObject();
		}
		J.EndArray();

		if (O.Trajectories)
		{
			J.Key("cueTips"); // [strike, t0, t1, x0, y0, z0, dx, dy, dz, speed0, decel, sampled]
			J.BeginArray();
			for (const rb::CueTipSegment& C : R.CueTips)
			{
				const double Row[12] = {static_cast<double>(C.Strike), C.Path.StartTime, C.T1, C.Path.Start.x, C.Path.Start.y, C.Path.Start.z, C.Path.Direction.x,
					C.Path.Direction.y, C.Path.Direction.z, C.Path.Speed0, C.Path.Deceleration, C.Kind == rb::SegmentKind::Sampled ? 1.0 : 0.0};
				J.NumberArray(Row, 12);
			}
			J.EndArray();
		}

		if (O.Record)
		{
			J.Key("record");
			J.BeginObject();
			J.FieldInt("events", static_cast<std::int64_t>(R.Record.Events.size()));
			J.FieldInt("tipContacts", R.Record.Stroke.TipContacts.Size());
			J.FieldBool("truncated", R.Record.Truncated);
			J.Field("stopTime", R.Record.End.StopTime);
			if (O.Facts)
			{
				double Radii[rb::kMaxBalls] = {};
				for (int Id = 0; Id < rb::kMaxBalls; ++Id)
				{
					Radii[Id] = In.Balls[Id].Spec.Radius;
				}
				const double Nominal = In.Balls[1].InPlay ? In.Balls[1].Spec.Radius : In.Balls[0].Spec.Radius;
				const rb::rules::RulesTable Table = rb::BuildRulesTable(*In.Table, Nominal, Radii, rb::kMaxBalls);
				rb::rules::ShotFacts Facts;
				rb::rules::DeriveShotFacts(R.Record, Table, rb::RulesTolerances{}, rb::kInfinity, Facts);
				J.Key("facts");
				J.BeginObject();
				J.FieldInt("earliestContact", Facts.EarliestContact);
				J.Field("firstContactTime", Facts.FirstContactTime);
				J.FieldInt("objectBallsToRail", Facts.NumObjectBallsDrivenToRail);
				J.FieldBool("cueBallPocketed", Facts.CueBallPocketed);
				J.FieldBool("doubleHit", Facts.DoubleHit);
				J.FieldBool("pushShot", Facts.PushShot);
				J.FieldInt("pocketedCount", Facts.Pocketed.Size());
				J.EndObject();
			}
			J.EndObject();
		}
		if (O.Geometry)
		{
			WriteGeometry(J, *In.Table);
		}
		J.EndObject();
	}

	bool WriteTextFile(const char* Path, const std::string& Text)
	{
		std::FILE* File = Path != nullptr ? std::fopen(Path, "wb") : stdout;
		if (File == nullptr)
		{
			std::fprintf(stderr, "rbsim: cannot open %s\n", Path);
			return false;
		}
		std::fwrite(Text.data(), 1, Text.size(), File);
		std::fputc('\n', File);
		if (File != stdout)
		{
			std::fclose(File);
		}
		return true;
	}
}

int main(int Argc, char** Argv)
{
	Options O;
	if (!ParseArgs(Argc, Argv, O))
	{
		PrintUsage();
		return 2;
	}
	if (O.InPath != nullptr)
	{
		// TODO(WP-7): parse the rbsimInput schema written by --dump-input (tableSpec, environment, params by key, balls,
		// strikes, context, record) with a small JSON reader and simulate it instead of the scenario options.
		std::fprintf(stderr, "rbsim: --in is not implemented yet (TODO(WP-7))\n");
		return 2;
	}

	rb::TableGeometry Geometry;
	rb::SimInput Input;
	if (!BuildInput(O, Geometry, Input))
	{
		return 2;
	}

	if (O.ListParams)
	{
		for (int i = 0; i < rb::PhysicsParamCount(); ++i)
		{
			const rb::PhysicsParamInfo Info = rb::PhysicsParamAt(i);
			double Value = 0.0;
			rb::GetPhysicsParam(Input.Params, Info.Key, Value);
			std::printf("%-36s %.17g %s\n", Info.Key, Value, Info.Unit);
		}
		return 0;
	}

	if (O.DumpInputPath != nullptr)
	{
		rbsim::JsonWriter D(!O.Compact);
		WriteSimInput(Input, D);
		if (!WriteTextFile(O.DumpInputPath, D.Text()))
		{
			return 1;
		}
	}

	rb::Simulator Sim;
	rb::ShotResult Result;
	Sim.Run(Input, Result);

	if (O.Bench > 0)
	{
		std::vector<double> Micros;
		Micros.reserve(static_cast<std::size_t>(O.Bench));
		for (int i = 0; i < O.Bench; ++i)
		{
			const auto T0 = std::chrono::steady_clock::now();
			Sim.Run(Input, Result);
			const auto T1 = std::chrono::steady_clock::now();
			Micros.push_back(std::chrono::duration<double, std::micro>(T1 - T0).count());
		}
		std::sort(Micros.begin(), Micros.end());
		const std::size_t N = Micros.size();
		std::fprintf(stderr, "rbsim bench: %d runs, median %.2f us, p99 %.2f us, max %.2f us, events %d\n", O.Bench, Micros[N / 2],
			Micros[std::min(N - 1, (N * 99) / 100)], Micros[N - 1], Result.Diagnostics.EventsProcessed);
	}

	rbsim::JsonWriter J(!O.Compact);
	WriteJson(O, Input, Result, J);
	if (!WriteTextFile(O.OutPath, J.Text()))
	{
		return 1;
	}
	return Result.Status == rb::SimStatus::Ok ? 0 : 3;
}
