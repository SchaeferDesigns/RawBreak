// Owner: WP-10 (validation & benchmarks). Human-factors tests assigned to WP-10 (architecture 17.12):
//  * HF-B10: with Slope = 0 and ChalkCling off every MOT / COL / VAL / RUL test passes unchanged - that is the default test run
//    itself. This test pins its premise: the parameters every other test starts from (PhysicsParams{}, the VAL / COL sets,
//    MakePhysicsParams of every preset) are the level table with clean balls.
//  * HF-B02 (release blocker): the routine-shot budgets of human-factors 3.10 with the FULL core (squirt, swerve, throw, cushions
//    and the real pocket instead of the budget model), 10^5 shots per profile and cue class (house cue m/m_e 15, standard own
//    cue 20; the LD shaft, m/m_e 40, reported against its disclosed values).
//  * HF-B12: a simulated proxy of the playtest KPI (the counterfactual share of misses caused by the human layer).
//  * HF-B09 (rating round-robins) needs an AI planner outside the core (O-16) and is not implemented here.
//
// Routine shot (3.10): cue ball <= 1 m from the object ball, object ball <= 1 m from the pocket, cut <= 30 deg, medium speed
// (1.5 m/s as HF-S04), tip at the centre (within 0.1 R), comfortable closed bridge. The budget model evaluates the WORST routine
// shot (d_CO = 1 m, cut 30 deg, a bar corner pocket 1 m away); HF-B02 does the same with the full core (INTERPRETATION): cue
// ball exactly 1 m behind the ghost ball, cut exactly 30 deg, object ball exactly 1 m from the mouth midpoint of a corner pocket
// of the 7-ft bar table (its own cloth, cushions and pockets, MakePhysicsParams), the approach direction (angle to the pocket
// axis within +-40 deg, which corner, which side the cut) random; layouts that do not fit on the table are skipped. Perfect input
// (no steering, exact aim): the intended azimuth is the middle of the azimuth interval that pots the object ball with the human
// layer off (the stroke is then the intended one bit for bit, HF-T08), so throw, squirt and swerve are compensated as a perfect
// player would. Keys, contact times and Settle as HF-S04 / HF-S06. A miss is any shot whose object ball does not drop in the
// aimed pocket.

#include "Validation/ValidationUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/Cue.h"
#include "rb/Human/AiProfiles.h"
#include "rb/Human/CueState.h"
#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"
#include "rb/Human/TipState.h"

using namespace rb;
using namespace rb::human;
using simtest::kR;

RB_TEST(HF_B10_DefaultParametersAreLevelAndClean)
{
	const PhysicsParams Default{};
	RB_CHECK(IsLevel(Default.Tilt) && !Default.ChalkCling && Default.BallBall.ClingFactor == 1.0);
	const TablePreset Presets[] = {TablePreset::NineFootPro, TablePreset::NineFootTight, TablePreset::EightFootPro, TablePreset::EightFootHome,
		TablePreset::SevenFootBar, TablePreset::SevenFoot78, TablePreset::SevenFootTrue};
	for (TablePreset Preset : Presets)
	{
		const PhysicsParams P = MakePhysicsParams(GetTableSpec(Preset));
		RB_CHECK(IsLevel(P.Tilt) && !P.ChalkCling && P.BallBall.ClingFactor == 1.0);
		const PhysicsParams C = MakePhysicsParams(GetTableSpec(Preset), TableCondition{});
		RB_CHECK(IsLevel(C.Tilt) && !C.ChalkCling && C.BallBall.ClingFactor == 1.0);
	}
	// The parameter sets of the MOT / COL / VAL test trees derive from these defaults.
	RB_CHECK(IsLevel(simtest::ValParams().Tilt) && !simtest::ValParams().ChalkCling && simtest::ValParams().BallBall.ClingFactor == 1.0);
	RB_CHECK(IsLevel(simtest::ColParams().Tilt) && !simtest::ColParams().ChalkCling && simtest::ColParams().BallBall.ClingFactor == 1.0);
	RB_CHECK(IsLevel(val::TableParams(kTableNineFootPro).Tilt) && !val::TableParams(kTableNineFootPro).ChalkCling);
}

namespace
{
	const int kCorners[4] = {static_cast<int>(PocketId::HeadRight), static_cast<int>(PocketId::FootRight), static_cast<int>(PocketId::FootLeft),
		static_cast<int>(PocketId::HeadLeft)};

	struct Layout
	{
		Vec2 Cue;
		Vec2 Object;
		int Pocket = 0;
		double Speed = 1.5;   // intended tip speed [m/s]
		double Azimuth = 0.0; // perfect intended azimuth [rad]
		double Window = 0.0;  // width of the potting azimuth interval [rad]
	};

	struct LayoutRanges
	{
		double CueMin = 1.0; // cue ball behind the ghost ball [m]
		double CueMax = 1.0;
		double ObjectMin = 1.0; // object ball to the mouth midpoint [m]
		double ObjectMax = 1.0;
		double CutMin = 30.0; // |cut| [deg]
		double CutMax = 30.0;
		double Speed = 1.5; // intended tip speed [m/s] ("medium speed": V 1.5 m/s of HF-S04)
	};

	struct Shooter
	{
		ShooterAttributes Attributes;
		StrokeSituation Situation;
		TipState Tip;
		CueBodyState CueBody;
		CueSpec Cue = kCuePlaying19oz;
		HumanParams Params;
	};

	const TableGeometry& Bar() { return simtest::Table(kTableSevenFootBar); }

	const PhysicsParams& BarParams()
	{
		static const PhysicsParams P = MakePhysicsParams(kTableSevenFootBar);
		return P;
	}

	IntendedStroke Intended(double Azimuth, double TimeDown, bool Settle, double Speed)
	{
		IntendedStroke S;
		S.Azimuth = Azimuth;
		S.Elevation = 3.0 * kDegToRad;
		S.Speed = Speed;
		S.TimeDown = TimeDown;
		S.ForwardStart = TimeDown - 0.2;
		S.SettleStart = Settle ? TimeDown - 2.0 : -1.0;
		return S;
	}

	// Simulates the strike from the layout; true if the object ball (id 1) drops in the layout's pocket.
	bool Pots(const Layout& L, const CueStrikeInput& Strike)
	{
		SimInput& In = simtest::NewInput(Bar(), BarParams(), 3);
		In.Record.Trajectories = false;
		In.Record.EventStates = false;
		simtest::Place(In, 0, ToVec3(L.Cue, kR));
		simtest::Place(In, 1, ToVec3(L.Object, kR));
		StrikeRequest S;
		S.Ball = 0;
		S.Input = Strike;
		In.Strikes.PushBack(S);
		ShotResult& R = simtest::ResultSlot(3);
		Simulator Sim;
		if (Sim.Run(In, R) != SimStatus::Ok)
		{
			return false;
		}
		return R.Finals[1].Status == BallFinalStatus::Pocketed && static_cast<int>(R.Finals[1].Pocket) == L.Pocket;
	}

	ExecutedStroke Execute(const Layout& L, const Shooter& S, const IntendedStroke& I, const NoiseKey& Key, const NoiseHistory& History)
	{
		BallObstacle Others[1];
		Others[0].Id = 1;
		Others[0].Position = ToVec3(L.Object, kR);
		Others[0].Radius = kR;
		return ExecuteStroke(I, S.Attributes, S.Situation, S.Tip, S.CueBody, S.Cue, MakeBallSpec(kR, kDefaultBallMass), ToVec3(L.Cue, kR), Others, 1, Key,
			History, S.Params);
	}

	// Noise-free pot test at azimuth Phi: the human layer off (NoiseScale 0, straight cue), so the executed stroke is the intended one.
	bool PotsAt(const Layout& L, const CueSpec& Cue, double Phi)
	{
		Shooter Clean;
		Clean.Cue = Cue;
		Clean.Params.NoiseScale = 0.0;
		const ExecutedStroke X = Execute(L, Clean, Intended(Phi, 2.5, false, L.Speed), NoiseKey{}, NoiseHistory{});
		return X.Error == ErrorCode::Ok && Pots(L, X.Strike);
	}

	Vec2 Rotate(const Vec2& V, double Angle) { return {V.x * Cos(Angle) - V.y * Sin(Angle), V.x * Sin(Angle) + V.y * Cos(Angle)}; }

	// A random routine layout and its perfect azimuth for Cue (the middle of the potting interval); false if it does not fit on
	// the table or nothing pots.
	bool MakeLayout(Rng& Random, const CueSpec& Cue, const LayoutRanges& Ranges, Layout& L)
	{
		const TableGeometry& T = Bar();
		L.Speed = Ranges.Speed;
		L.Pocket = kCorners[Random.NextBelow(4u)];
		const PocketGeometry& G = T.Pockets[L.Pocket];
		const Vec2 U = Rotate(G.Axis, Random.NextUniform(-40.0, 40.0) * kDegToRad); // object ball travel direction
		L.Object = G.MouthMid - U * Random.NextUniform(Ranges.ObjectMin, Ranges.ObjectMax);
		const double Cut = (Random.NextBelow(2u) == 0u ? -1.0 : 1.0) * Random.NextUniform(Ranges.CutMin, Ranges.CutMax) * kDegToRad;
		const Vec2 Ghost = L.Object - U * (2.0 * kR);
		L.Cue = Ghost - Rotate(U, Cut) * Random.NextUniform(Ranges.CueMin, Ranges.CueMax);
		const double Margin = kR + 0.03;
		for (const Vec2& P : {L.Object, L.Cue})
		{
			if (Abs(P.x) > T.HalfLength - Margin || Abs(P.y) > T.HalfWidth - Margin)
			{
				return false;
			}
		}
		// Potting interval: scan outward from the ghost-ball azimuth for a potting azimuth, then bisect both edges.
		const Vec2 Aim = Ghost - L.Cue;
		const double Phi0 = Atan2(Aim.y, Aim.x);
		bool Found = false;
		double Inside = Phi0;
		for (int k = 0; k <= 40 && !Found; ++k)
		{
			const double Probe = Phi0 + (k % 2 == 0 ? 1.0 : -1.0) * static_cast<double>(k / 2) * 0.1 * kDegToRad;
			if (PotsAt(L, Cue, Probe))
			{
				Found = true;
				Inside = Probe;
			}
		}
		if (!Found)
		{
			return false;
		}
		double Edge[2] = {};
		for (int Side = 0; Side < 2; ++Side)
		{
			const double Direction = Side == 0 ? -1.0 : 1.0;
			double In = Inside;
			double Out = Inside + Direction * 3.0 * kDegToRad;
			if (PotsAt(L, Cue, Out))
			{
				return false; // an interval wider than 3 deg to one side is not a pot geometry of this kind (never seen)
			}
			for (int k = 0; k < 30; ++k)
			{
				const double Mid = 0.5 * (In + Out);
				(PotsAt(L, Cue, Mid) ? In : Out) = Mid;
			}
			Edge[Side] = In;
		}
		L.Azimuth = 0.5 * (Edge[0] + Edge[1]);
		L.Window = Edge[1] - Edge[0];
		return PotsAt(L, Cue, L.Azimuth);
	}

#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kLayouts = 20; // Debug: reduced counts (architecture 18)
	constexpr int kPerLayout = 10;
#else
	constexpr int kLayouts = 1000;
	constexpr int kPerLayout = 100; // 10^5 shots per profile and cue class
#endif

	std::vector<Layout> MakeLayouts(const CueSpec& Cue, const LayoutRanges& Ranges, std::uint64_t Seed, int Count)
	{
		std::vector<Layout> Out;
		Rng Random(Seed);
		for (int Attempt = 0; Attempt < 100 * Count && static_cast<int>(Out.size()) < Count; ++Attempt)
		{
			Layout L;
			if (MakeLayout(Random, Cue, Ranges, L))
			{
				Out.push_back(L);
			}
		}
		return Out;
	}

	NoiseKey KeyOf(std::uint64_t Seed, std::uint32_t Shooter, std::uint32_t Index)
	{
		NoiseKey Key;
		Key.MatchSeed = Seed;
		Key.RackIndex = Index / 20u;
		Key.ShotIndex = Index;
		Key.ShooterId = Shooter;
		Key.ShooterShotIndex = Index;
		return Key;
	}

	void PrintWindows(const char* Name, const std::vector<Layout>& Set)
	{
		std::vector<double> Half;
		for (const Layout& L : Set)
		{
			Half.push_back(0.5 * L.Window * kRadToDeg);
		}
		std::sort(Half.begin(), Half.end());
		if (!Half.empty())
		{
			std::printf("  %s: %zu layouts, potting half-window (CB azimuth) min %.3f / median %.3f / max %.3f deg\n", Name, Half.size(), Half.front(),
				Half[Half.size() / 2], Half.back());
		}
	}
}

namespace
{
	struct Budget
	{
		const char* Name;
		double Attribute;
		double Pressure;
		bool Settle;
		double Limit;
	};

	// Misses of one budget row over a layout set (keys, contact times and Settle as HF-S04 / HF-S06).
	int CountMisses(const std::vector<Layout>& Set, const Budget& B, const CueSpec& Cue, int& Shots)
	{
		Shooter S;
		S.Attributes = UniformAttributes(B.Attribute);
		S.Situation.Pressure = B.Pressure;
		S.Cue = Cue;
		NoiseHistory History = RebuildNoiseHistory(0xB02u, 1u, 0u);
		int Misses = 0;
		Shots = 0;
		std::uint32_t Index = 0;
		for (const Layout& L : Set)
		{
			for (int k = 0; k < kPerLayout; ++k, ++Index)
			{
				const double TimeDown = 1.5 + 2.5 * U01(HashKeys(99u, Index));
				const ExecutedStroke X = Execute(L, S, Intended(L.Azimuth, TimeDown, B.Settle, L.Speed), KeyOf(0xB02u, 1u, Index), History);
				AdvanceNoiseHistory(History);
				++Shots;
				Misses += X.Error == ErrorCode::Ok && Pots(L, X.Strike) ? 0 : 1;
			}
		}
		return Misses;
	}
}

// HF-B02: the 3.10 budgets with the full core (perfect input): B1 (P 0, no Settle, attributes 25) <= 3 %, B1 attributes 100
// <= 0.3 %, B2 (P 1, Settle held at contact, 25) <= 3 %, B3 (P 1, no Settle, 25) <= 10 %, for the house cue (m/m_e 15) and the
// standard own cue (20); the LD shaft (m/m_e 40, end mass 0.170 / 40 kg) is reported against its disclosed 3.71 / 0.00 / 4.97 /
// 21.7 %. Gated at the spec's medium speed (V 1.5 m/s of HF-S04). On the bar table's cloth (mu_r 0.014) that speed brings the
// object ball of the worst routine pot to the pocket with little to spare (a stroke about 30 % short leaves it in the mouth), so
// the full core also counts the misses of the speed channels (speed scatter and flinch, x g^(1/2) under pressure), which the
// direction-only budget model cannot see; the same rows at V 2.0 m/s (the S0 stroke of human-factors 6) are printed for
// information.
RB_TEST(Integ_HF_B02_Slow_RoutineBudgetsFullCore)
{
	const Budget Rows[4] = {{"B1 P0 attr 25", 25.0, 0.0, false, 0.03}, {"B1 P0 attr 100", 100.0, 0.0, false, 0.003}, {"B2 P1 Settle attr 25", 25.0, 1.0, true, 0.03},
		{"B3 P1 no Settle 25", 25.0, 1.0, false, 0.10}};
	struct CueClass
	{
		const char* Name;
		CueSpec Cue;
		bool Gated;
		double Model[4]; // the budget model's values (3.10 table)
	};
	CueSpec Ld = kCuePlaying19oz;
	Ld.EndMass = kDefaultBallMass / 40.0;
	const CueClass Cues[3] = {{"house m/m_e 15", kCueHouse19oz, true, {0.0107, 0.0, 0.0199, 0.0662}},
		{"standard m/m_e 20", kCuePlaying19oz, true, {0.0115, 0.0, 0.0220, 0.0900}}, {"LD m/m_e 40", Ld, false, {0.0371, 0.0, 0.0497, 0.217}}};
	const LayoutRanges Worst{};
	LayoutRanges Faster = Worst;
	Faster.Speed = 2.0;
	for (const CueClass& C : Cues)
	{
		const std::vector<Layout> Set = MakeLayouts(C.Cue, Worst, 0xB02u, kLayouts);
		PrintWindows(C.Name, Set);
		RB_CHECK(static_cast<int>(Set.size()) == kLayouts);
		for (int r = 0; r < 4; ++r)
		{
			const Budget& B = Rows[r];
			int Shots = 0;
			const int Misses = CountMisses(Set, B, C.Cue, Shots);
			const double Share = Shots > 0 ? static_cast<double>(Misses) / Shots : 1.0;
			const bool Pass = Share <= B.Limit;
			std::printf("  HF-B02 %-17s %-21s V 1.5: misses %6d / %6d = %6.3f %% (budget %4.1f %%, budget model %5.2f %%) %s\n", C.Name, B.Name, Misses, Shots,
				100.0 * Share, 100.0 * B.Limit, 100.0 * C.Model[r], C.Gated ? val::Verdict(Pass) : "reported");
			if (C.Gated)
			{
				RB_CHECK(Pass);
			}
		}
		if (C.Gated)
		{
			const std::vector<Layout> Fast = MakeLayouts(C.Cue, Faster, 0xB02u, kLayouts);
			for (int r : {0, 2, 3})
			{
				int Shots = 0;
				const int Misses = CountMisses(Fast, Rows[r], C.Cue, Shots);
				std::printf("  HF-B02 %-17s %-21s V 2.0: misses %6d / %6d = %6.3f %% (information)\n", C.Name, Rows[r].Name, Misses, Shots,
					Shots > 0 ? 100.0 * Misses / Shots : 0.0);
			}
		}
	}
}

// HF-B12 (simulated proxy of the playtest KPI): the share of misses the counterfactual diagnosis (3.9) attributes to the human
// layer (steps 3-5: hand drift / nerves, tip placement / speed, human layer; steps 1-2 cannot apply on fresh equipment and a
// level, clean table) is <= 35 % at the start profile (attributes 25) and <= 10 % at attributes 85. INTERPRETATION: the player's
// own input error is modelled by the synthetic hand (3.8) of the AI profile nearest in skill - the Bar regular's for the start
// profile, the Road player's at 85 - on game-like pots (d_CO 0.3 - 1.5 m, d_OP 0.3 - 1.2 m, cut <= 60 deg, V 2.0 m/s, P 0,
// standard cue, 7-ft bar table); the other hands are printed for information.
RB_TEST(Integ_HF_B12_Slow_HumanLayerShareOfMisses)
{
	LayoutRanges Game;
	Game.CueMin = 0.3;
	Game.CueMax = 1.5;
	Game.ObjectMin = 0.3;
	Game.ObjectMax = 1.2;
	Game.CutMin = 0.0;
	Game.CutMax = 60.0;
	Game.Speed = 2.0;
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	const std::vector<Layout> Set = MakeLayouts(kCuePlaying19oz, Game, 0xB12u, 20);
	constexpr int kPer = 5;
#else
	const std::vector<Layout> Set = MakeLayouts(kCuePlaying19oz, Game, 0xB12u, 1000);
	constexpr int kPer = 10; // 10^4 shots per case
#endif
	RB_REQUIRE(!Set.empty());
	PrintWindows("HF-B12 game-like pots", Set);
	struct Case
	{
		double Attribute;
		AiProfileId Hand;
		const char* HandName;
		double Limit; // < 0: information only
	};
	const Case Cases[6] = {{25.0, AiProfileId::BarRegular, "Bar regular", 0.35}, {25.0, AiProfileId::Tourist, "Tourist", -1.0},
		{25.0, AiProfileId::LeaguePlayer, "League player", -1.0}, {85.0, AiProfileId::RoadPlayer, "Road player", 0.10},
		{85.0, AiProfileId::LocalHustler, "Local hustler", -1.0}, {85.0, AiProfileId::TouringPro, "Touring pro", -1.0}};
	for (const Case& C : Cases)
	{
		AiCharacter Player;
		Player.Profile = GetAiProfile(C.Hand);
		Player.CharacterSeed = 0x12345u;
		Shooter S;
		S.Attributes = UniformAttributes(C.Attribute);
		NoiseHistory History = RebuildNoiseHistory(0xB12u, 2u, 0u);
		int Misses = 0;
		int HumanCaused = 0;
		int Shots = 0;
		std::uint32_t Index = 0;
		for (const Layout& L : Set)
		{
			for (int k = 0; k < kPer; ++k, ++Index)
			{
				const NoiseKey Key = KeyOf(0xB12u, 2u, Index);
				PlannedStroke Plan;
				Plan.Azimuth = L.Azimuth;
				Plan.Elevation = 3.0 * kDegToRad;
				Plan.Speed = L.Speed;
				const IntendedStroke I = SyntheticHand(Plan, Player, S.Situation, kR, Key, History, S.Params);
				const ExecutedStroke X = Execute(L, S, I, Key, History);
				++Shots;
				if (!(X.Error == ErrorCode::Ok && Pots(L, X.Strike)))
				{
					++Misses;
					// Diagnosis steps 3-5 (index 2-4), each one change against the real shot; the first make names the cause.
					for (int Step = 2; Step < kDiagnosisStepCount; ++Step)
					{
						Shooter Re = S;
						ApplyDiagnosisStep(DiagnosisStepAt(Step), Re.Params, Re.Tip, Re.CueBody);
						const ExecutedStroke Y = Execute(L, Re, I, Key, History);
						if (Y.Error == ErrorCode::Ok && Pots(L, Y.Strike))
						{
							++HumanCaused;
							break;
						}
					}
				}
				AdvanceNoiseHistory(History);
			}
		}
		const double Share = Misses > 0 ? static_cast<double>(HumanCaused) / Misses : 0.0;
		const bool Gated = C.Limit >= 0.0;
		std::printf("  HF-B12 attributes %3.0f, input hand %-13s: %5d misses in %d shots (%5.1f %%), human layer %5d = %5.1f %% of the misses %s\n", C.Attribute,
			C.HandName, Misses, Shots, 100.0 * Misses / (Shots > 0 ? Shots : 1), HumanCaused, 100.0 * Share,
			Gated ? val::Verdict(Share <= C.Limit) : "(info)");
		if (Gated)
		{
			RB_CHECK(Share <= C.Limit);
		}
	}
}
