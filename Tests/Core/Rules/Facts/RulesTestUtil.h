#pragma once

// Owner: WP-8 (rules facts & evaluation). Shared builders for the rules tests: hand-built ShotRecords
// (rules.md 3.1-3.4) fed through DeriveShotFacts -> EvaluateShot, and start states (pitfall 1).
// Conventions of rules.md 17: 9-ft table, R = 0.028575 m, x_HS = -0.635, x_FS = +0.635, times in s.

#include "rbtest.h"

#include "rb/Core/Constants.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Vec2.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Shot/ShotRecord.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace rbrules
{
	inline constexpr double kR = 0.028575;
	inline constexpr int A = 0; // shooter in most tests
	inline constexpr int B = 1; // opponent

	inline rb::rules::RulesTable Table9Ft() { return rb::rules::MakeRulesTable(2.54, 1.27, kR); }

	inline constexpr std::uint32_t Bit(int Ball) { return 1u << static_cast<unsigned>(Ball); }

	// Resting spots for object balls that the test does not place explicitly: all between the head string and the
	// foot rail, well apart from each other and from the rails.
	inline rb::Vec2 DefaultPosition(int Ball)
	{
		if (Ball == 0)
		{
			return {-0.30, 0.0};
		}
		return {0.10 + 0.08 * (Ball % 5), -0.40 + 0.16 * (Ball / 5)};
	}

	// 15-ball triangle (rules.md 5.1), apex on the foot spot, ball b on site b - 1 (row-major, apex first).
	inline rb::Vec2 TriangleSite(int Site)
	{
		int Row = 0;
		int First = 0;
		while (Site >= First + Row + 1)
		{
			First += Row + 1;
			++Row;
		}
		const int K = Site - First;
		return {0.635 + Row * 1.7320508075688772 * kR, (2 * K - Row) * kR};
	}

	class Shot
	{
	public:
		rb::ShotRecord Record;

		Shot()
		{
			for (int b = 0; b < rb::kMaxBalls; ++b)
			{
				Record.Start.Radius[b] = kR;
			}
			rb::StrokeInfo Stroke;
			Stroke.Ball = 0;
			Record.Stroke.Strokes.PushBack(Stroke);
			Tips({{0.0, 0.0012}}); // a normal 1.2 ms tip contact
			On(0, DefaultPosition(0));
		}

		// Ball on the table at shot start, and at rest there at the end unless an event says otherwise.
		Shot& On(int Ball, rb::Vec2 P)
		{
			Record.Start.Presence[Ball] = rb::BallPresence::OnTable;
			Record.Start.Position[Ball] = P;
			Record.End.Balls[Ball].Status = rb::BallEndStatus::OnTable;
			Record.End.Balls[Ball].Position = P;
			return *this;
		}

		Shot& On(std::initializer_list<int> Balls)
		{
			for (const int Ball : Balls)
			{
				On(Ball, DefaultPosition(Ball));
			}
			return *this;
		}

		Shot& OnRange(int First, int Last)
		{
			for (int b = First; b <= Last; ++b)
			{
				On(b, DefaultPosition(b));
			}
			return *this;
		}

		Shot& Rack15() // object balls 1..15 in a tight triangle
		{
			for (int b = 1; b <= 15; ++b)
			{
				On(b, TriangleSite(b - 1));
			}
			return *this;
		}

		Shot& Presence(int Ball, rb::BallPresence P)
		{
			Record.Start.Presence[Ball] = P;
			Record.End.Balls[Ball].Status = rb::BallEndStatus::NotUsed;
			return *this;
		}

		Shot& InHand(rb::CueBallInHand Region, rb::Vec2 P)
		{
			Record.Start.InHand = Region;
			Record.Start.PlacedPosition = P;
			return On(0, P);
		}

		Shot& EndAt(int Ball, rb::Vec2 P)
		{
			Record.End.Balls[Ball].Status = rb::BallEndStatus::OnTable;
			Record.End.Balls[Ball].Position = P;
			return *this;
		}

		rb::RecordEvent& Add(rb::RecordEventType Type, double T, int BallA, int BallB = -1)
		{
			rb::RecordEvent E;
			E.Type = Type;
			E.Time = T;
			E.Sequence = NextSequence++;
			E.A = static_cast<rb::BallId>(BallA);
			E.B = static_cast<rb::BallId>(BallB);
			Record.Events.push_back(E);
			return Record.Events.back();
		}

		// Ball-ball contact (stored with A < B); PositionOther = plan center of the higher-id ball (the object ball
		// when one of them is the cue ball); Cut = cut angle [rad].
		Shot& Hit(double T, int Ball1, int Ball2, rb::Vec2 PositionOther = {0.3, 0.0}, double Cut = 0.0)
		{
			const int Lo = Ball1 < Ball2 ? Ball1 : Ball2;
			const int Hi = Ball1 < Ball2 ? Ball2 : Ball1;
			rb::RecordEvent& E = Add(rb::RecordEventType::BallBall, T, Lo, Hi);
			E.PositionB = PositionOther;
			E.CutAngle = Cut;
			return *this;
		}

		Shot& Rail(double T, int Ball, rb::CushionId C = rb::CushionId::Foot, bool ContinuesInitialFreeze = false)
		{
			rb::RecordEvent& E = Add(rb::RecordEventType::BallCushion, T, Ball);
			E.Feature = static_cast<std::uint8_t>(C);
			E.ContinuesInitialFreeze = ContinuesInitialFreeze;
			return *this;
		}

		Shot& Jaw(double T, int Ball, rb::PocketId P, rb::JawSide Side = rb::JawSide::Incoming)
		{
			rb::RecordEvent& E = Add(rb::RecordEventType::BallJaw, T, Ball);
			E.Feature = static_cast<std::uint8_t>(P);
			E.Side = static_cast<std::int8_t>(Side);
			return *this;
		}

		Shot& RailTop(double T, int Ball, rb::CushionId C = rb::CushionId::Foot)
		{
			rb::RecordEvent& E = Add(rb::RecordEventType::BallRailTop, T, Ball);
			E.Feature = static_cast<std::uint8_t>(C);
			return *this;
		}

		Shot& Pot(double T, int Ball, rb::PocketId P)
		{
			Add(rb::RecordEventType::BallPocketEnter, T, Ball).Feature = static_cast<std::uint8_t>(P);
			Add(rb::RecordEventType::BallPocketed, T, Ball).Feature = static_cast<std::uint8_t>(P);
			Record.End.Balls[Ball].Status = rb::BallEndStatus::Pocketed;
			Record.End.Balls[Ball].Pocket = P;
			return *this;
		}

		Shot& Off(double T, int Ball, rb::OffTableReason Reason = rb::OffTableReason::Floor)
		{
			Add(rb::RecordEventType::BallOffTable, T, Ball).Feature = static_cast<std::uint8_t>(Reason);
			Record.End.Balls[Ball].Status = rb::BallEndStatus::OffTable;
			return *this;
		}

		// Direction +1: toward +x (+y for the long string), -1 the other way.
		Shot& Cross(double T, int Ball, rb::TableLine Line, int Direction)
		{
			rb::RecordEvent& E = Add(rb::RecordEventType::BallLineCross, T, Ball);
			E.Feature = static_cast<std::uint8_t>(Line);
			E.Side = static_cast<std::int8_t>(Direction);
			return *this;
		}

		Shot& Airborne(double T, int Ball, double ZMax = 0.04)
		{
			Add(rb::RecordEventType::BallAirborne, T, Ball).ZMax = ZMax;
			return *this;
		}

		Shot& Tips(std::initializer_list<std::pair<double, double>> Intervals)
		{
			Record.Stroke.TipContacts.Clear();
			for (const auto& I : Intervals)
			{
				rb::TipContact C;
				C.Ball = 0;
				C.Strike = 0;
				C.Start = I.first;
				C.End = I.second;
				Record.Stroke.TipContacts.PushBack(C);
			}
			return *this;
		}

		Shot& StopAt(double T)
		{
			Record.End.StopTime = T;
			return *this;
		}

		// Sorts the log by (Time, Sequence) and sets tStop after the last event unless given.
		const rb::ShotRecord& Finish()
		{
			std::stable_sort(Record.Events.begin(), Record.Events.end(), [](const rb::RecordEvent& L, const rb::RecordEvent& R) {
				return L.Time < R.Time || (L.Time == R.Time && L.Sequence < R.Sequence);
			});
			if (Record.End.StopTime <= 0.0)
			{
				double Last = 0.0;
				for (const rb::RecordEvent& E : Record.Events)
				{
					Last = E.Time > Last ? E.Time : Last;
				}
				Record.End.StopTime = Last + 0.5;
			}
			return Record;
		}

		rb::rules::ShotFacts Facts(const rb::rules::RulesTable& Table = Table9Ft(), double ShotClockLimit = rb::kInfinity)
		{
			Finish();
			rb::rules::ShotFacts F;
			rb::rules::DeriveShotFacts(Record, Table, rb::RulesTolerances{}, ShotClockLimit, F);
			return F;
		}

	private:
		std::uint32_t NextSequence = 0;
	};

	// Start state from the record's start snapshot: normal shot, cue ball in position, open table.
	inline rb::rules::GameState StateFor(rb::rules::Discipline Game, const Shot& S, int Shooter = A)
	{
		rb::rules::GameState G;
		G.Game = Game;
		G.Shooter = Shooter;
		G.RackBreaker = Shooter;
		G.CueBall = rb::rules::CueBallNext::InPosition;
		G.IsBreakShot = false;
		G.PushOutAvailable = false;
		G.TableOpen = true;
		for (int b = 0; b < rb::rules::kRulesBallCount; ++b)
		{
			switch (S.Record.Start.Presence[b])
			{
			case rb::BallPresence::OnTable: G.Balls[b].Kind = rb::rules::BallStatusKind::OnTable; break;
			case rb::BallPresence::Pocketed: G.Balls[b].Kind = rb::rules::BallStatusKind::Pocketed; break;
			case rb::BallPresence::OutOfPlay: G.Balls[b].Kind = rb::rules::BallStatusKind::OutOfPlay; break;
			case rb::BallPresence::NotUsed: G.Balls[b].Kind = rb::rules::BallStatusKind::NotUsed; break;
			}
			G.Balls[b].Position = S.Record.Start.Position[b];
		}
		return G;
	}

	// Break state: first shot of the rack, cue ball in hand above the head string (in baulk for Blackball).
	inline rb::rules::GameState BreakStateFor(rb::rules::Discipline Game, const Shot& S, int Shooter = A)
	{
		rb::rules::GameState G = StateFor(Game, S, Shooter);
		G.IsBreakShot = true;
		G.CueBall = Game == rb::rules::Discipline::Blackball ? rb::rules::CueBallNext::InHandBaulk : rb::rules::CueBallNext::InHandAboveHeadString;
		return G;
	}

	inline rb::rules::ShotDeclaration Declare(int Ball, rb::PocketId Pocket, rb::rules::ShotKind Kind = rb::rules::ShotKind::Normal)
	{
		rb::rules::ShotDeclaration D;
		D.Kind = Kind;
		D.Called.Ball = static_cast<rb::BallId>(Ball);
		D.Called.Pocket = Pocket;
		return D;
	}

	inline rb::rules::ShotDeclaration Kind(rb::rules::ShotKind K)
	{
		rb::rules::ShotDeclaration D;
		D.Kind = K;
		return D;
	}

	inline rb::rules::ShotOutcome Evaluate(const rb::rules::RulesConfig& C, const rb::rules::GameState& G, const rb::rules::ShotDeclaration& D, Shot& S,
		const rb::rules::RulesTable& Table = Table9Ft())
	{
		const rb::rules::ShotFacts F = S.Facts(Table);
		return rb::rules::EvaluateShot(C, Table, G, D, F);
	}

	inline bool OptionsAre(const rb::rules::ShotOutcome& O, std::initializer_list<rb::rules::Option> Expected)
	{
		if (O.Options.Size() != static_cast<int>(Expected.size()))
		{
			return false;
		}
		int i = 0;
		for (const rb::rules::Option X : Expected)
		{
			if (O.Options[i++] != X)
			{
				return false;
			}
		}
		return true;
	}

	inline bool SpotsAre(const rb::rules::ShotOutcome& O, std::initializer_list<int> Expected)
	{
		if (O.BallsToSpot.Size() != static_cast<int>(Expected.size()))
		{
			return false;
		}
		int i = 0;
		for (const int X : Expected)
		{
			if (O.BallsToSpot[i++] != X)
			{
				return false;
			}
		}
		return true;
	}

	inline bool IsPass(const rb::rules::ShotOutcome& O, int Next, rb::rules::CueBallNext CueBall)
	{
		return O.Next == rb::rules::NextAction::Pass && O.NextShooter == Next && O.NextCueBall == CueBall;
	}

	inline bool IsContinue(const rb::rules::ShotOutcome& O, int Shooter)
	{
		return O.Next == rb::rules::NextAction::Continue && O.NextShooter == Shooter;
	}

	inline bool IsDecide(const rb::rules::ShotOutcome& O, int Decider)
	{
		return O.Next == rb::rules::NextAction::AwaitDecision && O.NextShooter == Decider;
	}

	inline bool IsRackWon(const rb::rules::ShotOutcome& O, int Winner)
	{
		return O.Next == rb::rules::NextAction::RackWon && O.Winner == Winner;
	}
}
