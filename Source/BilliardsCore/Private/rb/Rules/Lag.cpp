#include "rb/Core/FpGuard.h"
// Owner: WP-9 (rules table procedures & match). Spec: rules.md 4.1.
#include "rb/Rules/Lag.h"

#include "rb/Math/Scalar.h"

namespace rb::rules
{
	namespace
	{
		constexpr bool IsSideCushion(std::uint8_t Feature)
		{
			return Feature == static_cast<std::uint8_t>(CushionId::RightHead) || Feature == static_cast<std::uint8_t>(CushionId::RightFoot) ||
				Feature == static_cast<std::uint8_t>(CushionId::LeftFoot) || Feature == static_cast<std::uint8_t>(CushionId::LeftHead);
		}

		constexpr bool IsSidePocket(std::uint8_t Feature)
		{
			return Feature == static_cast<std::uint8_t>(PocketId::SideRight) || Feature == static_cast<std::uint8_t>(PocketId::SideLeft);
		}

		// Ball struck by strike Strike (kNoBall if the record carries no stroke info for it).
		int StruckBall(const StrokeRecord& Stroke, int Strike)
		{
			return (Strike >= 0 && Strike < Stroke.Strokes.Size()) ? Stroke.Strokes[Strike].Ball : kNoBall;
		}
	}

	LagBallFacts DeriveLagBallFacts(const ShotRecord& Record, int Ball, const RulesTable& Table, const RulesTolerances& Tolerances)
	{
		LagBallFacts F;
		if (Ball < 0 || Ball >= kMaxBalls)
		{
			F.Bad = true;
			F.OtherFoul = true;
			return F;
		}
		double Radius = Record.Start.Radius[Ball];
		if (!(Radius > 0.0))
		{
			Radius = Ball < kRulesBallCount ? Table.BallRadius[Ball] : Table.NominalBallRadius;
		}

		for (const RecordEvent& E : Record.Events)
		{
			if (E.A != Ball)
			{
				continue;
			}
			switch (E.Type)
			{
			case RecordEventType::BallLineCross: // (a) center passed the long string by more than eps_line
				if (E.Feature == static_cast<std::uint8_t>(TableLine::LongString))
				{
					F.CrossedLongString = true;
				}
				break;
			case RecordEventType::BallCushion:
				if (E.Feature == static_cast<std::uint8_t>(CushionId::Foot))
				{
					++F.FootCushionContacts; // (b)
				}
				else if (IsSideCushion(E.Feature))
				{
					F.SideCushionContact = true; // (d)
				}
				break;
			case RecordEventType::BallJaw: // (d) side-pocket jaws count as side cushion (INTERPRETATION)
				if (IsSidePocket(E.Feature))
				{
					F.SideCushionContact = true;
				}
				break;
			case RecordEventType::BallPocketed:
			case RecordEventType::BallOffTable:
			case RecordEventType::SupportedOverPocket:
				F.PocketedOrOffTable = true; // (c)
				break;
			default:
				break;
			}
		}

		const BallEnd& End = Record.End.Balls[Ball];
		if (End.Status == BallEndStatus::Pocketed || End.Status == BallEndStatus::OffTable)
		{
			F.PocketedOrOffTable = true;
		}
		for (const SupportedBall& S : Record.End.Supported)
		{
			if (S.Ball == Ball)
			{
				F.PocketedOrOffTable = true;
			}
		}

		// Distance to the head cushion nose from the ball's nearest point, (e) resting past the nose line.
		if (!F.PocketedOrOffTable)
		{
			const double HeadNoseX = -0.5 * Table.Length;
			F.Distance = (End.Position.x - Radius) - HeadNoseX;
			F.PastHeadCushionNose = End.Position.x - Radius < HeadNoseX;
		}

		// Stroke fouls (bad lag (h)): double hit / push by this ball's own cue, its cue touching the other
		// lag ball, game-layer non-tip contacts on this ball. CueTip non-tip contacts are attributed to the
		// striker through the TipContacts list (strike index).
		int OwnTipContacts = 0;
		for (const TipContact& T : Record.Stroke.TipContacts)
		{
			const int Struck = StruckBall(Record.Stroke, T.Strike);
			const bool ByOwnCue = Struck == Ball || (Struck == kNoBall && T.Ball == Ball);
			if (T.Ball == Ball && ByOwnCue)
			{
				++OwnTipContacts;
				if (T.End - T.Start > Tolerances.PushDuration)
				{
					F.OtherFoul = true; // push
				}
			}
			else if (Struck == Ball && T.Ball != Ball)
			{
				F.OtherFoul = true; // this ball's cue touched another ball
			}
		}
		if (OwnTipContacts >= 2)
		{
			F.OtherFoul = true; // double hit
		}
		for (const NonTipContact& N : Record.Stroke.NonTipContacts)
		{
			if (N.Ball == Ball && N.Source != NonTipSource::CueTip)
			{
				F.OtherFoul = true;
			}
		}

		F.Bad = F.CrossedLongString || F.FootCushionContacts != 1 || F.PocketedOrOffTable || F.SideCushionContact || F.PastHeadCushionNose ||
			F.OtherFoul;
		return F;
	}

	LagResult EvaluateLag(const LagBallFacts& First, const LagBallFacts& Second, const RulesTolerances& Tolerances)
	{
		LagResult Result;
		Result.First = First;
		Result.Second = Second;
		if (First.Bad && Second.Bad)
		{
			Result.Outcome = LagOutcome::Relag;
		}
		else if (First.Bad)
		{
			Result.Outcome = LagOutcome::SecondWins;
		}
		else if (Second.Bad)
		{
			Result.Outcome = LagOutcome::FirstWins;
		}
		else if (Abs(First.Distance - Second.Distance) <= Tolerances.LagTie)
		{
			Result.Outcome = LagOutcome::Relag; // referee cannot determine the winner (R 1.2(g))
		}
		else
		{
			Result.Outcome = First.Distance < Second.Distance ? LagOutcome::FirstWins : LagOutcome::SecondWins;
		}
		return Result;
	}

	void LagStartPositions(const RulesTable& Table, Vec2& First, Vec2& Second)
	{
		const double X = Table.HeadStringX - Table.NominalBallRadius - 0.01;
		First = {X, Table.FootSpot.y - 0.25 * Table.Width};
		Second = {X, Table.FootSpot.y + 0.25 * Table.Width};
	}
}
