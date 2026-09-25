// Owner: WP-9 (rules table procedures & match). rules.md 4.1 lag (both lag balls in ONE record);
// tests L01-L07.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Rules/Lag.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

namespace
{
	constexpr int kBallA = 0; // player A's lag ball (strike 0)
	constexpr int kBallB = 1; // player B's lag ball (strike 1)
	constexpr double kHeadNose = -1.27;

	struct LagRecord
	{
		ShotRecord Record;
		std::uint32_t Sequence = 0;

		LagRecord()
		{
			Vec2 A;
			Vec2 B;
			LagStartPositions(NineFootTable(), A, B);
			AddBall(kBallA, A);
			AddBall(kBallB, B);
		}

		void AddBall(int Ball, const Vec2& Start)
		{
			Record.Start.Presence[Ball] = BallPresence::OnTable;
			Record.Start.Position[Ball] = Start;
			Record.Start.Radius[Ball] = kR;
			StrokeInfo Stroke;
			Stroke.Ball = static_cast<BallId>(Ball);
			Record.Stroke.Strokes.PushBack(Stroke);
			TipContact Tip;
			Tip.Ball = static_cast<BallId>(Ball);
			Tip.Strike = Ball;
			Tip.Start = 0.0;
			Tip.End = 0.0012;
			Record.Stroke.TipContacts.PushBack(Tip);
		}

		void Event(RecordEventType Type, int Ball, double Time, std::uint8_t Feature = 0xFF)
		{
			RecordEvent E;
			E.Type = Type;
			E.A = static_cast<BallId>(Ball);
			E.Time = Time;
			E.Sequence = Sequence++;
			E.Feature = Feature;
			Record.Events.push_back(E);
		}

		void Cushion(int Ball, CushionId C, double Time) { Event(RecordEventType::BallCushion, Ball, Time, static_cast<std::uint8_t>(C)); }

		// A good lag: one foot-cushion contact, comes to rest with d = Distance (y keeps its side).
		void GoodLag(int Ball, double Distance, double Time = 1.0)
		{
			Cushion(Ball, CushionId::Foot, Time);
			RestAt(Ball, kHeadNose + kR + Distance, Ball == kBallA ? -0.3 : 0.3);
		}

		void RestAt(int Ball, double X, double Y)
		{
			Record.End.Balls[Ball].Status = BallEndStatus::OnTable;
			Record.End.Balls[Ball].Position = {X, Y};
		}

		LagResult Evaluate() const
		{
			const RulesTable T = NineFootTable();
			const RulesTolerances Tol;
			return EvaluateLag(DeriveLagBallFacts(Record, kBallA, T, Tol), DeriveLagBallFacts(Record, kBallB, T, Tol), Tol);
		}
	};
}

RB_TEST(Rules_Match_LagStartPositions)
{
	Vec2 A;
	Vec2 B;
	LagStartPositions(NineFootTable(), A, B);
	RB_CHECK_NEAR(A.x, -0.635 - kR - 0.01, 1e-12);
	RB_CHECK_NEAR(B.x, -0.635 - kR - 0.01, 1e-12);
	RB_CHECK_NEAR(A.y, -0.3175, 1e-12);
	RB_CHECK_NEAR(B.y, 0.3175, 1e-12);
}

RB_TEST(Rules_L01_CloserGoodLagWinsAndChoosesBreaker)
{
	LagRecord R;
	R.GoodLag(kBallA, 0.050);
	R.GoodLag(kBallB, 0.080);
	const LagResult Result = R.Evaluate();
	RB_CHECK(!Result.First.Bad);
	RB_CHECK(!Result.Second.Bad);
	RB_CHECK_NEAR(Result.First.Distance, 0.050, 1e-12);
	RB_CHECK_NEAR(Result.Second.Distance, 0.080, 1e-12);
	RB_CHECK(Result.Outcome == LagOutcome::FirstWins);

	const MatchConfig Config = MakeMatchConfig(Discipline::NineBall);
	MatchState State;
	StartMatch(Config, State);
	RB_CHECK(State.Phase == MatchPhase::Lag);
	RB_REQUIRE(ApplyLagResult(Config, State, Result) == ErrorCode::Ok);
	RB_CHECK(State.Phase == MatchPhase::LagWinnerChooses);
	RB_CHECK(State.LagWinner == 0);
	RB_CHECK(State.Decider == 0);
	RB_REQUIRE(ChooseBreaker(Config, State, 1) == ErrorCode::Ok); // A lets B break
	RB_CHECK(State.Phase == MatchPhase::RackSetup);
	RB_CHECK(State.Game.RackBreaker == 1);
	RB_CHECK(State.FirstBreaker == 1);
}

RB_TEST(Rules_L02_SideCushionIsBadLag)
{
	LagRecord R;
	R.Cushion(kBallA, CushionId::LeftHead, 0.4); // C4
	R.GoodLag(kBallA, 0.05);
	R.GoodLag(kBallB, 0.30);
	const LagResult Result = R.Evaluate();
	RB_CHECK(Result.First.Bad);
	RB_CHECK(Result.First.SideCushionContact);
	RB_CHECK(!Result.Second.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::SecondWins);
}

RB_TEST(Rules_Match_LagSidePocketJawIsSideCushion)
{
	// INTERPRETATION (4.1 d): jaws of the side pockets are side-cushion contacts; corner jaws are not.
	LagRecord R;
	R.Event(RecordEventType::BallJaw, kBallA, 0.5, static_cast<std::uint8_t>(PocketId::SideRight));
	R.GoodLag(kBallA, 0.05);
	R.Event(RecordEventType::BallJaw, kBallB, 0.9, static_cast<std::uint8_t>(PocketId::FootLeft));
	R.GoodLag(kBallB, 0.30);
	const LagResult Result = R.Evaluate();
	RB_CHECK(Result.First.Bad);
	RB_CHECK(!Result.Second.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::SecondWins);
}

RB_TEST(Rules_L03_TwoFootCushionContactsBad)
{
	LagRecord R;
	R.Cushion(kBallA, CushionId::Foot, 0.9);
	R.GoodLag(kBallA, 0.05, 1.4);
	R.GoodLag(kBallB, 0.30);
	const LagResult Result = R.Evaluate();
	RB_CHECK(Result.First.FootCushionContacts == 2);
	RB_CHECK(Result.First.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::SecondWins);

	// never reaching the foot cushion is bad as well
	LagRecord Short;
	Short.RestAt(kBallA, -0.2, -0.3);
	Short.GoodLag(kBallB, 0.30);
	const LagResult ShortResult = Short.Evaluate();
	RB_CHECK(ShortResult.First.FootCushionContacts == 0);
	RB_CHECK(ShortResult.First.Bad);
}

RB_TEST(Rules_L04_BothBadRelag)
{
	LagRecord R;
	R.Cushion(kBallA, CushionId::Foot, 1.0);
	R.Event(RecordEventType::BallPocketed, kBallA, 2.5, static_cast<std::uint8_t>(PocketId::HeadRight));
	R.Record.End.Balls[kBallA].Status = BallEndStatus::Pocketed;
	R.Record.End.Balls[kBallA].Pocket = PocketId::HeadRight;
	R.Cushion(kBallB, CushionId::Foot, 1.0);
	R.Cushion(kBallB, CushionId::Foot, 1.5);
	R.RestAt(kBallB, -1.0, 0.3);
	const LagResult Result = R.Evaluate();
	RB_CHECK(Result.First.Bad && Result.First.PocketedOrOffTable);
	RB_CHECK(Result.Second.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::Relag);

	const MatchConfig Config = MakeMatchConfig(Discipline::NineBall);
	MatchState State;
	StartMatch(Config, State);
	RB_REQUIRE(ApplyLagResult(Config, State, Result) == ErrorCode::Ok);
	RB_CHECK(State.Phase == MatchPhase::Lag); // lag again
	RB_CHECK(State.LagWinner == -1);
}

RB_TEST(Rules_L05_TieWithinLagToleranceRelag)
{
	LagRecord R;
	R.GoodLag(kBallA, 0.0500);
	R.GoodLag(kBallB, 0.0503);
	const LagResult Result = R.Evaluate();
	RB_CHECK(!Result.First.Bad && !Result.Second.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::Relag); // 0.3 mm <= eps_lag = 0.5 mm

	LagRecord Clear;
	Clear.GoodLag(kBallA, 0.0500);
	Clear.GoodLag(kBallB, 0.0510);
	RB_CHECK(Clear.Evaluate().Outcome == LagOutcome::FirstWins);
}

RB_TEST(Rules_L06_CrossingLongStringBad)
{
	LagRecord R;
	R.Event(RecordEventType::BallLineCross, kBallA, 0.8, static_cast<std::uint8_t>(TableLine::LongString));
	R.GoodLag(kBallA, 0.02);
	R.GoodLag(kBallB, 0.40);
	const LagResult Result = R.Evaluate();
	RB_CHECK(Result.First.CrossedLongString);
	RB_CHECK(Result.First.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::SecondWins);
	// crossing other lines is harmless
	LagRecord Other;
	Other.Event(RecordEventType::BallLineCross, kBallA, 0.2, static_cast<std::uint8_t>(TableLine::HeadString));
	Other.Event(RecordEventType::BallLineCross, kBallA, 0.5, static_cast<std::uint8_t>(TableLine::CenterString));
	Other.GoodLag(kBallA, 0.02);
	Other.GoodLag(kBallB, 0.40);
	RB_CHECK(Other.Evaluate().Outcome == LagOutcome::FirstWins);
}

RB_TEST(Rules_L07_RestingPastHeadCushionNoseBad)
{
	LagRecord R;
	R.Cushion(kBallA, CushionId::Foot, 1.0);
	R.RestAt(kBallA, -1.2705 + kR, 0.62); // in the jaws of P5, x_rest - R = -1.2705
	R.GoodLag(kBallB, 0.30);
	const LagResult Result = R.Evaluate();
	RB_CHECK(Result.First.PastHeadCushionNose);
	RB_CHECK(Result.First.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::SecondWins);
}

RB_TEST(Rules_Match_LagStrokeFoulsAreBad)
{
	// (h) double hit, push or touched ball on a lag ball, attributed per strike.
	LagRecord Double;
	TipContact Again;
	Again.Ball = kBallA;
	Again.Strike = 0;
	Again.Start = 0.015;
	Again.End = 0.016;
	Double.Record.Stroke.TipContacts.PushBack(Again);
	Double.GoodLag(kBallA, 0.02);
	Double.GoodLag(kBallB, 0.40);
	RB_CHECK(Double.Evaluate().Outcome == LagOutcome::SecondWins);

	LagRecord Push;
	Push.Record.Stroke.TipContacts[1].End = 0.0062; // B's tip contact lasts 6.2 ms
	Push.GoodLag(kBallA, 0.40);
	Push.GoodLag(kBallB, 0.02);
	const LagResult PushResult = Push.Evaluate();
	RB_CHECK(PushResult.Second.OtherFoul);
	RB_CHECK(PushResult.Outcome == LagOutcome::FirstWins);

	LagRecord Touched;
	NonTipContact Hand;
	Hand.Ball = kBallB;
	Hand.Source = NonTipSource::BridgeHand;
	Touched.Record.Stroke.NonTipContacts.PushBack(Hand);
	Touched.GoodLag(kBallA, 0.40);
	Touched.GoodLag(kBallB, 0.02);
	RB_CHECK(Touched.Evaluate().Outcome == LagOutcome::FirstWins);
}
