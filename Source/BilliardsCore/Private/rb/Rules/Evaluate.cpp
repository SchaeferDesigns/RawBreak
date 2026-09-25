#include "rb/Core/FpGuard.h"
// Owner: WP-8 (rules facts & evaluation). Spec: rules.md 4.5, 4.9, 6-10, 12, 16.
#include "rb/Rules/Evaluate.h"

#include "rb/Rules/TableRules.h"

#include <cstddef>
#include <initializer_list>

namespace rb::rules
{
	namespace
	{
		constexpr int kEightBall = 8;
		constexpr int kNineBall = 9;
		constexpr int kTenBall = 10;

		constexpr bool IsObjectBallId(int Ball) { return Ball >= 1 && Ball < kRulesBallCount; }
		constexpr std::uint32_t BallBit(int Ball) { return 1u << static_cast<unsigned>(Ball); }

		// Severity for ShotOutcome::Enforced (rules.md 4.9 "several fouls on one shot"): 0. the 14.1 breaking foul,
		// 1. rack-level (third consecutive foul), 2. standard fouls that send the cue ball to the kitchen in 14.1
		// (3.1, 3.11 para 2), 3. all other standard fouls (fixed order for a deterministic HUD).
		constexpr Foul kFoulSeverity[] = {
			Foul::BreakingFoul141,
			Foul::ThreeConsecutiveFouls,
			Foul::CueBallScratch,
			Foul::CueBallOffTable,
			Foul::BadPlayAboveHeadStringP2,
			Foul::BadPlayAboveHeadStringP1,
			Foul::ObjectBallOffTable,
			Foul::WrongBallFirst,
			Foul::NoRailAfterContact,
			Foul::BreakTooFewRails,
			Foul::BlackballBreakFoul,
			Foul::PottedOpponentBallOnly,
			Foul::JumpedOverBall,
			Foul::DoubleHit,
			Foul::PushShot,
			Foul::TouchedBall,
			Foul::BadCueBallPlacement,
			Foul::BallsStillMoving,
			Foul::NoFootOnFloor,
			Foul::SlowPlay,
			Foul::TemplateFoul,
			Foul::IllegalScoop,
		};
		static_assert(sizeof(kFoulSeverity) / sizeof(kFoulSeverity[0]) == static_cast<std::size_t>(Foul::Count), "every foul has a severity rank");

		Foul MostSevere(const FoulSet& X)
		{
			for (const Foul F : kFoulSeverity)
			{
				if (X.Has(F))
				{
					return F;
				}
			}
			return Foul::Count;
		}

		bool OnTableAtStart(const GameState& S, int Ball) { return S.Balls[Ball].Kind == BallStatusKind::OnTable; }

		std::uint32_t OnTableObjectBalls(const GameState& S)
		{
			std::uint32_t Mask = 0;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if (OnTableAtStart(S, b))
				{
					Mask |= BallBit(b);
				}
			}
			return Mask;
		}

		std::uint32_t GroupMask(BallGroup Group)
		{
			std::uint32_t Mask = 0;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if (Group != BallGroup::None && GroupOf(b) == Group)
				{
					Mask |= BallBit(b);
				}
			}
			return Mask;
		}

		std::uint32_t PocketedObjectBalls(const ShotFacts& F)
		{
			std::uint32_t Mask = 0;
			for (const PocketedBall& P : F.Pocketed)
			{
				if (IsObjectBallId(P.Ball))
				{
					Mask |= BallBit(P.Ball);
				}
			}
			return Mask;
		}

		// Object balls on the table after the shot: on the table at start, neither pocketed nor off the table.
		std::uint32_t ObjectBallsLeft(const GameState& S, const ShotFacts& F)
		{
			return OnTableObjectBalls(S) & ~PocketedObjectBalls(F) & ~F.ObjectBallsOffTable;
		}

		int CountBits(std::uint32_t Mask)
		{
			int N = 0;
			for (; Mask != 0u; Mask &= Mask - 1u)
			{
				++N;
			}
			return N;
		}

		void PushSpots(ShotOutcome& O, std::uint32_t Mask)
		{
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if ((Mask & BallBit(b)) != 0u)
				{
					bool Listed = false;
					for (const BallId Id : O.BallsToSpot)
					{
						Listed = Listed || Id == b;
					}
					if (!Listed)
					{
						O.BallsToSpot.PushBack(static_cast<BallId>(b));
					}
				}
			}
		}

		// ------------------------------------------------------------------------------------------
		// Outcome constructors (rules.md 10.2: Lose / Win / Pass / Continue / Decide).
		// ------------------------------------------------------------------------------------------
		ShotOutcome MakeOutcome(const GameState& S)
		{
			ShotOutcome O;
			O.FoulsAfter[0] = S.Players[0].ConsecutiveFouls;
			O.FoulsAfter[1] = S.Players[1].ConsecutiveFouls;
			return O;
		}

		ShotOutcome Finish(ShotOutcome& O, const FoulSet& X, const char* RuleRef)
		{
			O.Detected = X;
			O.AnyFoul = !X.IsEmpty();
			O.Enforced = MostSevere(X);
			O.RuleRef = RuleRef;
			return O;
		}

		void SetPass(ShotOutcome& O, int Next, CueBallNext CueBall)
		{
			O.Next = NextAction::Pass;
			O.NextShooter = Next;
			O.NextCueBall = CueBall;
		}

		void SetContinue(ShotOutcome& O, int Shooter)
		{
			O.Next = NextAction::Continue;
			O.NextShooter = Shooter;
			O.NextCueBall = CueBallNext::InPosition;
		}

		void SetDecide(ShotOutcome& O, int Decider, std::initializer_list<Option> Options)
		{
			O.Next = NextAction::AwaitDecision;
			O.NextShooter = Decider;
			O.NextCueBall = CueBallNext::InPosition;
			O.Options.Clear();
			for (const Option Choice : Options)
			{
				O.Options.PushBack(Choice);
			}
		}

		void SetRackWon(ShotOutcome& O, int Winner)
		{
			O.Next = NextAction::RackWon;
			O.Winner = Winner;
			O.NextShooter = -1;
		}

		void SetRerackAndBreak(ShotOutcome& O, int Breaker, CueBallNext CueBall)
		{
			O.Next = NextAction::RerackAndBreak;
			O.NextShooter = Breaker;
			O.NextCueBall = CueBall;
			O.Rack.Kind = RackCommandKind::Rerack15;
		}

		// A foul-free shot that ends the visit: the turn passes, unless the shooter still has extra visits
		// (FoulCueBallMode TwoVisits / FreeShotPlusVisit), which he then plays from position.
		void SetPassOrVisit(ShotOutcome& O, const GameState& S, int Opp)
		{
			if (S.VisitsRemaining > 0)
			{
				SetContinue(O, S.Shooter);
				O.NextVisits = S.VisitsRemaining - 1;
				return;
			}
			SetPass(O, Opp, CueBallNext::InPosition);
		}

		// ------------------------------------------------------------------------------------------
		// Common fouls and predicates (rules.md 10.2)
		// ------------------------------------------------------------------------------------------
		FoulSet DetectCommonFouls(const RulesConfig& C, const ShotFacts& F)
		{
			FoulSet X;
			if (F.BallsMovingAtStart) X.Add(Foul::BallsStillMoving);                       // R 3.9
			if (!F.FootOnFloor) X.Add(Foul::NoFootOnFloor);                                // R 3.4
			if (!F.CueBallPlacementLegal) X.Add(Foul::BadCueBallPlacement);                // R 3.10
			const bool Touched = C.Fouls == FoulScope::AllBall ? F.NonTipBallContact : F.NonTipCueBallContact;
			if (Touched) X.Add(Foul::TouchedBall);                                         // R 3.6 (all-ball fouls)
			if (F.DoubleHit) X.Add(Foul::DoubleHit);                                       // R 3.7
			if (F.PushShot) X.Add(Foul::PushShot);                                         // R 3.8
			if (F.CueBallPocketed) X.Add(Foul::CueBallScratch);                            // R 3.1
			if (F.CueBallOffTable) X.Add(Foul::CueBallOffTable);                           // R 3.1
			if (F.ObjectBallsOffTable != 0u) X.Add(Foul::ObjectBallOffTable);              // R 3.5
			if (F.ShotClockExpired) X.Add(Foul::SlowPlay);                                 // R 3.14
			if (C.UseRackTemplate && F.TemplateTouched) X.Add(Foul::TemplateFoul);         // R 3.15
			if (C.Scoop == ScoopPolicy::Foul && F.Scoop) X.Add(Foul::IllegalScoop);        // variants (F9)
			if (C.JumpShots == JumpShotRule::Illegal && F.JumpedOver != 0u) X.Add(Foul::JumpedOverBall); // Blackball R 8.13.3
			return X;
		}

		// R 3.2 + tie rule: a foul only if no ball of the tie set is legal; no contact at all is 3.3's business.
		bool WrongBallFirst(const ShotFacts& F, std::uint32_t LegalMask)
		{
			if (F.FirstContactTieSet.IsEmpty())
			{
				return false;
			}
			for (const BallId B : F.FirstContactTieSet)
			{
				if ((LegalMask & BallBit(B)) != 0u)
				{
					return false;
				}
			}
			return true;
		}

		// R 3.3 + tie rule.
		bool NoRailAfterContact(const ShotFacts& F, bool FirstContactWasLegal)
		{
			if (F.AnyObjectBallPocketed)
			{
				return false;
			}
			if (F.FirstContactTieSet.IsEmpty())
			{
				return true; // the cue ball touched no object ball
			}
			return !F.AnyBallDrivenToRailAfterFirstContact(FirstContactWasLegal);
		}

		// R 3.11 (only when the shot starts with the cue ball in hand above the head string): 0, 1 or 2 (paragraph).
		int BadPlayFromAboveHeadString(const GameState& S, const ShotFacts& F, int FirstContact)
		{
			if (S.CueBall != CueBallNext::InHandAboveHeadString)
			{
				return 0;
			}
			if (!F.CueBallCrossedHeadString && !F.CueBallContactedBallOnOrBelowHeadString)
			{
				return 2;
			}
			if (IsObjectBallId(FirstContact) && (F.AboveHeadStringAtFirstCueBallContact & BallBit(FirstContact)) != 0u
				&& !F.CueBallCrossedHeadStringBeforeFirstContact)
			{
				return 1;
			}
			return 0;
		}

		void AddBadPlayAboveHeadString(FoulSet& X, int Paragraph)
		{
			if (Paragraph == 1) X.Add(Foul::BadPlayAboveHeadStringP1);
			if (Paragraph == 2) X.Add(Foul::BadPlayAboveHeadStringP2);
		}

		bool CalledShotMade(const ShotDeclaration& D, const ShotFacts& F, const Call& Called)
		{
			return D.Kind != ShotKind::Safety && IsObjectBallId(Called.Ball) && Called.Pocket != PocketId::None && F.PocketOf(Called.Ball) == Called.Pocket;
		}

		// Next cue-ball state after a standard 8-ball foul (FoulCueBallMode, rules.md 12.1; WPA: in hand anywhere).
		void ApplyFoulCueBall(const RulesConfig& C, const ShotFacts& F, ShotOutcome& O)
		{
			const bool CueBallGone = F.CueBallPocketed || F.CueBallOffTable;
			switch (C.FoulCueBall)
			{
			case FoulCueBallMode::InHandAnywhere: O.NextCueBall = CueBallNext::InHandAnywhere; break;
			case FoulCueBallMode::InHandBehindHeadString: O.NextCueBall = CueBallNext::InHandAboveHeadString; break;
			case FoulCueBallMode::InHandBaulk: O.NextCueBall = CueBallNext::InHandBaulk; break;
			case FoulCueBallMode::FreeShotPlusVisit:
				O.NextFreeShot = true;
				O.NextVisits = 1;
				O.NextCueBall = CueBallGone ? CueBallNext::InHandBaulk : CueBallNext::InPosition;
				break;
			case FoulCueBallMode::TwoVisits:
				O.NextVisits = 1;
				O.NextCueBall = CueBallGone ? CueBallNext::InHandBaulk : CueBallNext::InPosition;
				break;
			case FoulCueBallMode::InPosition: O.NextCueBall = CueBallGone ? CueBallNext::InHandAnywhere : CueBallNext::InPosition; break;
			}
		}

		// ------------------------------------------------------------------------------------------
		// Legal first contacts (bit masks over the balls on the table at shot START, pitfall 1)
		// ------------------------------------------------------------------------------------------

		// APA break (BreakFirstContact = HeadBallOrSecondRow): the apex ball or a ball of the second row, taken from
		// the start positions (rows grow toward +x; a row is the set of balls within 5 mm of its x).
		std::uint32_t HeadBallOrSecondRowMask(const GameState& S)
		{
			constexpr double RowTolerance = 0.005;
			const std::uint32_t OnTable = OnTableObjectBalls(S);
			double MinX = kInfinity;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if ((OnTable & BallBit(b)) != 0u && S.Balls[b].Position.x < MinX)
				{
					MinX = S.Balls[b].Position.x;
				}
			}
			double SecondX = kInfinity;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				const double X = S.Balls[b].Position.x;
				if ((OnTable & BallBit(b)) != 0u && X > MinX + RowTolerance && X < SecondX)
				{
					SecondX = X;
				}
			}
			std::uint32_t Mask = 0;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if ((OnTable & BallBit(b)) != 0u && S.Balls[b].Position.x <= SecondX + RowTolerance)
				{
					Mask |= BallBit(b);
				}
			}
			return Mask;
		}

		// 8-ball claim (R 4.4): the declared cleared group, or - when the 8 is called on an open table - the group
		// that is completely off the table (the validator auto-sets it; done here too for inferred calls). Solids
		// first if both qualify (no effect beyond this shot).
		BallGroup EffectiveClaim(const GameState& S, const ShotDeclaration& D, int CalledBall)
		{
			if (D.ClaimedClearedGroup != BallGroup::None || CalledBall != kEightBall)
			{
				return D.ClaimedClearedGroup;
			}
			if (GroupCleared(S, BallGroup::Solids)) return BallGroup::Solids;
			if (GroupCleared(S, BallGroup::Stripes)) return BallGroup::Stripes;
			return BallGroup::None;
		}

		struct EightBallView
		{
			BallGroup Mine = BallGroup::None;
			bool Open = true;
			bool OnEight = false;
			bool AnyGroupGone = false;
		};

		EightBallView ViewEightBall(const GameState& S, BallGroup Claim)
		{
			EightBallView V;
			V.Mine = S.Players[S.Shooter].Group;
			V.Open = S.TableOpen || V.Mine == BallGroup::None;
			const bool ClaimOk = V.Open && Claim != BallGroup::None && GroupCleared(S, Claim);
			V.OnEight = (!V.Open && GroupCleared(S, V.Mine)) || ClaimOk; // at shot START
			V.AnyGroupGone = V.Open && (GroupCleared(S, BallGroup::Solids) || GroupCleared(S, BallGroup::Stripes));
			return V;
		}

		std::uint32_t EightBallLegalMask(const RulesConfig& C, const GameState& S, BallGroup Claim)
		{
			const std::uint32_t OnTable = OnTableObjectBalls(S);
			if (S.IsBreakShot)
			{
				return C.BreakFirstContact == BreakFirstContactRule::HeadBallOrSecondRow ? HeadBallOrSecondRowMask(S) : OnTable; // R 4.3: any ball
			}
			if (S.FreeShot)
			{
				return OnTable; // FoulCueBall FreeShotPlusVisit (12.1 / 12.5): 3.2 is suspended on the free shot
			}
			const EightBallView V = ViewEightBall(S, Claim);
			if (V.OnEight)
			{
				return OnTable & BallBit(kEightBall);
			}
			if (V.Open)
			{
				std::uint32_t Mask = OnTable & ~BallBit(kEightBall);
				// R 4.4 (2025): the 8 first is no foul once a group is completely off the table.
				if (!C.OpenTableEightFirstFoul || (C.OpenTableEightGroupGoneException && V.AnyGroupGone))
				{
					Mask |= OnTable & BallBit(kEightBall);
				}
				return Mask;
			}
			return OnTable & GroupMask(V.Mine);
		}

		std::uint32_t BlackballLegalMask(const GameState& S)
		{
			const std::uint32_t OnTable = OnTableObjectBalls(S);
			if (S.IsBreakShot || S.FreeShot)
			{
				return OnTable; // free shot: 3.2 suspended (rules.md 12.4)
			}
			const BallGroup Mine = S.Players[S.Shooter].Group;
			const bool Open = S.TableOpen || Mine == BallGroup::None;
			if (Open)
			{
				return OnTable & ~BallBit(kEightBall);
			}
			if (GroupCleared(S, Mine))
			{
				return OnTable & BallBit(kEightBall);
			}
			return OnTable & GroupMask(Mine);
		}

		// ------------------------------------------------------------------------------------------
		// 8-ball (rules.md 6, 10.3; variants 12.3, 12.6)
		// ------------------------------------------------------------------------------------------
		ShotOutcome EvaluateEightBallBreak(const RulesConfig& C, const GameState& S, const ShotFacts& F, FoulSet X, ShotOutcome O)
		{
			const int Me = S.Shooter;
			const int Opp = 1 - Me;
			O.FoulsAfter[Me] = X.IsEmpty() ? 0 : S.Players[Me].ConsecutiveFouls + 1;

			if (F.IsPocketed(kEightBall))
			{
				if (X.IsEmpty())
				{
					switch (C.EightOnBreak)
					{
					case EightOnBreakRule::Win: SetRackWon(O, Me); return Finish(O, X, "variant: 8 on the break wins");
					case EightOnBreakRule::Rerack: SetRerackAndBreak(O, Me, CueBallNext::InHandAboveHeadString); return Finish(O, X, "variant: 8 on the break, re-rack");
					case EightOnBreakRule::SpotOrRebreakOption: break;
					}
					SetDecide(O, Me, {Option::Spot8ContinueFromPosition, Option::RerackDeciderBreaks});
					return Finish(O, X, "R 4.3(e)");
				}
				switch (C.EightOnBreakWithFoul)
				{
				case EightOnBreakWithFoulRule::Lose: SetRackWon(O, Opp); return Finish(O, X, "variant: 8 on the break with a foul loses");
				case EightOnBreakWithFoulRule::Rerack: SetRerackAndBreak(O, Me, CueBallNext::InHandAboveHeadString); return Finish(O, X, "variant: 8 on the break with a foul, re-rack");
				case EightOnBreakWithFoulRule::OpponentOption: break;
				}
				SetDecide(O, Opp, {Option::Spot8BallInHandAboveHeadString, Option::RerackDeciderBreaks}); // incoming player re-breaks (INTERPRETATION)
				return Finish(O, X, "R 4.3(f)");
			}

			// R 4.3(g), 4.7: only the 8 is spotted (variants: every ball driven off the table).
			PushSpots(O, C.SpotJumpedObjectBalls ? F.ObjectBallsOffTable : (F.ObjectBallsOffTable & BallBit(kEightBall)));

			bool CountOk = F.AnyObjectBallPocketed || F.NumObjectBallsDrivenToRail >= 4;
			if (C.BreakFirstContact == BreakFirstContactRule::HeadBallOrSecondRow)
			{
				// APA: the head ball or a second-row ball first, and no cue-ball rail before the rack.
				const std::uint32_t Legal = HeadBallOrSecondRowMask(S);
				bool RailBefore = false;
				for (const RailContactEntry& E : F.Balls[kCueBallId].RailContacts)
				{
					RailBefore = RailBefore || E.Time < F.FirstContactTime;
				}
				CountOk = CountOk && !WrongBallFirst(F, Legal) && !F.FirstContactTieSet.IsEmpty() && !RailBefore;
			}
			if (!CountOk)
			{
				// R 4.3(d) illegal break; with a foul as well, accepting the table gives BIH above HS (INTERPRETATION).
				O.CueBallIfAccepted = X.IsEmpty() ? CueBallNext::InPosition : CueBallNext::InHandAboveHeadString;
				SetDecide(O, Opp, {Option::AcceptTable, Option::RerackDeciderBreaks, Option::RerackOffenderBreaks});
				return Finish(O, X, "R 4.3(d)");
			}
			if (!X.IsEmpty())
			{
				if (F.CueBallPocketed || F.CueBallOffTable)
				{
					SetPass(O, Opp, CueBallNext::InHandAboveHeadString);
					return Finish(O, X, "R 4.3(h)");
				}
				O.CueBallIfAccepted = CueBallNext::InPosition;
				SetDecide(O, Opp, {Option::AcceptTable, Option::BallInHandAboveHeadString});
				return Finish(O, X, F.ObjectBallsOffTable != 0u ? "R 4.3(g)" : "R 4.3(h)");
			}
			if (C.BreakAssignsGroup == BreakAssignsGroupRule::IfOnlyOneGroupPocketed && F.AnyObjectBallPocketed)
			{
				const std::uint32_t Potted = PocketedObjectBalls(F);
				const bool Solids = (Potted & GroupMask(BallGroup::Solids)) != 0u;
				const bool Stripes = (Potted & GroupMask(BallGroup::Stripes)) != 0u;
				if (Solids != Stripes)
				{
					O.AssignShooterGroup = Solids ? BallGroup::Solids : BallGroup::Stripes; // 12.3 / 12.6 variant
				}
			}
			if (F.AnyObjectBallPocketed)
			{
				SetContinue(O, Me); // R 4.3(c): table stays open
			}
			else
			{
				SetPass(O, Opp, CueBallNext::InPosition);
			}
			return Finish(O, X, "R 4.3(c)");
		}

		// Legally made ball of the shot for continuing / group assignment: the called ball in the called pocket
		// (Explicit / ObviousAssist), or - CallMode EightOnly / None ("slop counts") - the first pocketed legal ball.
		bool EightBallMade(const RulesConfig& C, const ShotDeclaration& D, const ShotFacts& F, const Call& Called, const EightBallView& V,
			BallGroup& MadeGroup)
		{
			if (C.Calls == CallMode::EightOnly || C.Calls == CallMode::None)
			{
				if (D.Kind == ShotKind::Safety)
				{
					return false;
				}
				for (const PocketedBall& P : F.Pocketed)
				{
					if (IsObjectBallId(P.Ball) && P.Ball != kEightBall && (V.Open || GroupOf(P.Ball) == V.Mine))
					{
						MadeGroup = GroupOf(P.Ball);
						return true;
					}
				}
				return false;
			}
			if (Called.Ball == kEightBall || !CalledShotMade(D, F, Called) || (!V.Open && GroupOf(Called.Ball) != V.Mine))
			{
				return false;
			}
			MadeGroup = GroupOf(Called.Ball);
			return true;
		}

		ShotOutcome EvaluateEightBall(const RulesConfig& C, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
		{
			const int Me = S.Shooter;
			const int Opp = 1 - Me;
			ShotOutcome O = MakeOutcome(S);
			FoulSet X = DetectCommonFouls(C, F);

			if (S.IsBreakShot)
			{
				// R 3.11 applies to the break too: it starts in hand above the head string (v1.1, pitfall 27, E33).
				const int Fc = ResolveFirstContact(F, EightBallLegalMask(C, S, BallGroup::None));
				AddBadPlayAboveHeadString(X, BadPlayFromAboveHeadString(S, F, Fc));
				return EvaluateEightBallBreak(C, S, F, X, O);
			}

			const Call Called = ResolveCall(C, S, D, F); // explicit or inferred (4.5); used for the 8 too (v1.1 C6)
			const BallGroup Claim = EffectiveClaim(S, D, Called.Ball);
			const EightBallView V = ViewEightBall(S, Claim);
			const std::uint32_t Legal = EightBallLegalMask(C, S, Claim);
			const int Fc = ResolveFirstContact(F, Legal);
			AddBadPlayAboveHeadString(X, BadPlayFromAboveHeadString(S, F, Fc));
			const bool Wbf = WrongBallFirst(F, Legal);
			if (Wbf) X.Add(Foul::WrongBallFirst);
			if (C.RailAfterContactRequired && NoRailAfterContact(F, !Wbf)) X.Add(Foul::NoRailAfterContact);
			O.FoulsAfter[Me] = X.IsEmpty() ? 0 : S.Players[Me].ConsecutiveFouls + 1;

			// Loss of the rack (R 4.8, 4.10; never on the break).
			if (F.IsOffTable(kEightBall))
			{
				SetRackWon(O, Opp);
				return Finish(O, X, "R 4.8(d)");
			}
			if (F.IsPocketed(kEightBall))
			{
				if (!X.IsEmpty())
				{
					SetRackWon(O, Opp);
					return Finish(O, X, "R 4.8(a)");
				}
				if (!V.OnEight)
				{
					SetRackWon(O, Opp); // incl. an unclaimed 8 on an open table (E34)
					return Finish(O, X, "R 4.8(b)");
				}
				const bool CalledEight = C.Calls == CallMode::None || (Called.Ball == kEightBall && F.PocketOf(kEightBall) == Called.Pocket);
				if (D.Kind == ShotKind::Safety || !CalledEight)
				{
					SetRackWon(O, Opp);
					return Finish(O, X, "R 4.8(c)");
				}
				if (C.LastPocketRule)
				{
					const BallGroup Group = V.Open ? Claim : V.Mine;
					const PocketId Last = Group == BallGroup::None ? PocketId::None : S.LastGroupBallPocket[static_cast<int>(Group)];
					if (Last != PocketId::None && Last != F.PocketOf(kEightBall))
					{
						SetRackWon(O, Opp);
						return Finish(O, X, "variant: last-pocket rule");
					}
				}
				SetRackWon(O, Me);
				return Finish(O, X, "R 4.5");
			}

			if (!X.IsEmpty())
			{
				if (C.ScratchWhileShootingEightLoses && V.OnEight && (F.CueBallPocketed || F.CueBallOffTable))
				{
					SetRackWon(O, Opp);
					return Finish(O, X, "variant: scratch while shooting the 8");
				}
				if (C.SpotJumpedObjectBalls)
				{
					PushSpots(O, F.ObjectBallsOffTable);
				}
				if (C.ThreeFoulRule && O.FoulsAfter[Me] >= 3)
				{
					O.FoulsAfter[Me] = 0;
					X.Add(Foul::ThreeConsecutiveFouls);
					SetRackWon(O, Opp);
					return Finish(O, X, "R 3.13");
				}
				SetPass(O, Opp, CueBallNext::InHandAnywhere);
				ApplyFoulCueBall(C, F, O); // balls stay down, table unchanged
				return Finish(O, X, "R 4.9");
			}

			if (D.Kind == ShotKind::Safety)
			{
				SetPassOrVisit(O, S, Opp); // anything pocketed stays down
				return Finish(O, X, "R 4.6");
			}
			BallGroup MadeGroup = BallGroup::None;
			if (!EightBallMade(C, D, F, Called, V, MadeGroup))
			{
				SetPassOrVisit(O, S, Opp);
				return Finish(O, X, "R 4.5");
			}
			SetContinue(O, Me);
			O.NextVisits = S.VisitsRemaining;
			if (V.Open)
			{
				O.AssignShooterGroup = MadeGroup; // R 4.4: the legally made called ball decides the group
			}
			return Finish(O, X, "R 4.5");
		}

		// ------------------------------------------------------------------------------------------
		// 9-ball (rules.md 7, 10.4)
		// ------------------------------------------------------------------------------------------
		ShotOutcome EvaluateNineBall(const RulesConfig& C, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
		{
			const int Me = S.Shooter;
			const int Opp = 1 - Me;
			const int Lowest = LowestObjectBallAtStart(S);
			const bool Push = D.Kind == ShotKind::PushOut; // validator: only when S.PushOutAvailable
			ShotOutcome O = MakeOutcome(S);
			FoulSet X = DetectCommonFouls(C, F);
			if (!Push) // R 5.4: 3.2 and 3.3 are suspended on a push-out
			{
				const bool Wbf = WrongBallFirst(F, Lowest > 0 ? BallBit(Lowest) : 0u);
				if (Wbf) X.Add(Foul::WrongBallFirst);
				if (S.IsBreakShot)
				{
					if (!F.AnyObjectBallPocketed && F.NumObjectBallsDrivenToRail < 4) X.Add(Foul::BreakTooFewRails); // R 5.3(b)
				}
				else if (C.RailAfterContactRequired && NoRailAfterContact(F, !Wbf))
				{
					X.Add(Foul::NoRailAfterContact);
				}
			}
			const bool NineIn = F.IsPocketed(kNineBall);
			const bool NineOff = F.IsOffTable(kNineBall);

			if (!X.IsEmpty()) // R 5.7
			{
				if (NineIn || NineOff) PushSpots(O, BallBit(kNineBall)); // R 5.6
				const int N = S.Players[Me].ConsecutiveFouls + 1;
				if (C.ThreeFoulRule && N >= 3)
				{
					O.FoulsAfter[Me] = 0;
					X.Add(Foul::ThreeConsecutiveFouls);
					SetRackWon(O, Opp);
					return Finish(O, X, "R 5.8");
				}
				O.FoulsAfter[Me] = N;
				SetPass(O, Opp, CueBallNext::InHandAnywhere);
				return Finish(O, X, "R 5.7");
			}
			O.FoulsAfter[Me] = 0;

			if (S.IsBreakShot)
			{
				const bool ThreeBallApplies = C.ThreeBallRule && (C.ThreeBallScope == ThreeBallRuleScope::Reg16Combined || !F.AnyObjectBallPocketed);
				const int Count = C.ThreeBallRuleReach ? F.CountPocketedOrReachedHeadString : F.CountPocketedOrCrossedHeadString;
				if (ThreeBallApplies && Count < 3) // Reg 16: illegal break, not a foul
				{
					if (NineIn) PushSpots(O, BallBit(kNineBall)); // Reg 16(5)
					SetDecide(O, Opp, {Option::AcceptTableNoPushOut, Option::HandBackPushOutAllowed});
					return Finish(O, X, "Reg 16");
				}
				if (NineIn)
				{
					SetRackWon(O, Me);
					return Finish(O, X, "R 5.5");
				}
				O.NextPushOutAvailable = true; // R 5.4
				if (F.AnyObjectBallPocketed) SetContinue(O, Me);
				else SetPass(O, Opp, CueBallNext::InPosition);
				return Finish(O, X, "R 5.3");
			}
			if (Push)
			{
				if (NineIn) PushSpots(O, BallBit(kNineBall)); // R 5.6
				SetDecide(O, Opp, {Option::ShootFromPosition, Option::PassBack});
				return Finish(O, X, "R 5.4");
			}
			if (NineIn)
			{
				SetRackWon(O, Me);
				return Finish(O, X, "R 5.5");
			}
			if (F.AnyObjectBallPocketed) SetContinue(O, Me);
			else SetPass(O, Opp, CueBallNext::InPosition);
			return Finish(O, X, "R 5.5");
		}

		// ------------------------------------------------------------------------------------------
		// 10-ball (rules.md 8, 10.5)
		// ------------------------------------------------------------------------------------------
		bool TenIsLastBall(const RulesConfig& C, const GameState& S, const ShotFacts& F)
		{
			switch (C.TenOnlyBall)
			{
			case TenOnlyBallMoment::ShotStart: return OnTableAtStart(S, kTenBall) && CountObjectBallsOnTable(S) == 1;
			case TenOnlyBallMoment::AtPocketing:
			{
				// Every other object ball that was on the table had dropped before the 10 did.
				const double TenTime = F.Balls[kTenBall].PocketedTime;
				for (int b = 1; b < kRulesBallCount; ++b)
				{
					if (b == kTenBall || !OnTableAtStart(S, b))
					{
						continue;
					}
					if (!F.Balls[b].Pocketed || F.Balls[b].PocketedTime >= TenTime)
					{
						return false;
					}
				}
				return true;
			}
			case TenOnlyBallMoment::EarlyTenWins: return true; // non-WPA variant
			}
			return false;
		}

		ShotOutcome EvaluateTenBall(const RulesConfig& C, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
		{
			const int Me = S.Shooter;
			const int Opp = 1 - Me;
			const int Lowest = LowestObjectBallAtStart(S);
			const bool Push = D.Kind == ShotKind::PushOut;
			ShotOutcome O = MakeOutcome(S);
			FoulSet X = DetectCommonFouls(C, F);
			if (!Push)
			{
				const bool Wbf = WrongBallFirst(F, Lowest > 0 ? BallBit(Lowest) : 0u);
				if (Wbf) X.Add(Foul::WrongBallFirst);
				if (S.IsBreakShot)
				{
					if (!F.AnyObjectBallPocketed && F.NumObjectBallsDrivenToRail < 4) X.Add(Foul::BreakTooFewRails); // R 6.3(b)
				}
				else if (C.RailAfterContactRequired && NoRailAfterContact(F, !Wbf))
				{
					X.Add(Foul::NoRailAfterContact);
				}
			}
			const bool TenIn = F.IsPocketed(kTenBall);
			const bool TenOff = F.IsOffTable(kTenBall);

			if (!X.IsEmpty()) // R 6.9
			{
				if (TenIn || TenOff) PushSpots(O, BallBit(kTenBall)); // R 6.8
				const int N = S.Players[Me].ConsecutiveFouls + 1;
				if (C.ThreeFoulRule && N >= 3)
				{
					O.FoulsAfter[Me] = 0;
					X.Add(Foul::ThreeConsecutiveFouls);
					SetRackWon(O, Opp);
					return Finish(O, X, "R 6.10");
				}
				O.FoulsAfter[Me] = N;
				SetPass(O, Opp, CueBallNext::InHandAnywhere);
				return Finish(O, X, "R 6.9");
			}
			O.FoulsAfter[Me] = 0;

			if (S.IsBreakShot) // no call on the break (R 6.5)
			{
				if (TenIn) PushSpots(O, BallBit(kTenBall)); // R 6.8
				O.NextPushOutAvailable = true;               // R 6.4
				if (F.AnyObjectBallPocketed) SetContinue(O, Me); // INTERPRETATION (8.2)
				else SetPass(O, Opp, CueBallNext::InPosition);
				return Finish(O, X, "R 6.3");
			}
			if (Push)
			{
				if (TenIn) PushSpots(O, BallBit(kTenBall));
				SetDecide(O, Opp, {Option::ShootFromPosition, Option::PassBack});
				return Finish(O, X, "R 6.4");
			}
			const Call Called = ResolveCall(C, S, D, F); // never Safety in 10-ball (validator)
			const bool NoCalls = C.Calls == CallMode::None;
			const bool Made = NoCalls ? (D.Kind != ShotKind::Safety && F.AnyObjectBallPocketed) : CalledShotMade(D, F, Called);
			if (Made)
			{
				const bool TenMade = NoCalls ? TenIn : Called.Ball == kTenBall;
				if (TenMade && TenIsLastBall(C, S, F))
				{
					SetRackWon(O, Me);
					return Finish(O, X, "R 6.7");
				}
				if (TenIn) PushSpots(O, BallBit(kTenBall)); // R 6.8
				SetContinue(O, Me);
				return Finish(O, X, "R 6.7");
			}
			if (F.AnyObjectBallPocketed) // R 6.6 wrongfully pocketed balls: not a foul
			{
				if (TenIn) PushSpots(O, BallBit(kTenBall));
				SetDecide(O, Opp, {Option::ShootFromPosition, Option::PassBack});
				return Finish(O, X, "R 6.6");
			}
			SetPass(O, Opp, CueBallNext::InPosition);
			return Finish(O, X, "R 6.5");
		}

		// ------------------------------------------------------------------------------------------
		// 14.1 continuous (rules.md 9, 10.6)
		// ------------------------------------------------------------------------------------------
		ShotOutcome EvaluateStraightPool(const RulesConfig& C, const RulesTable& T, const GameState& S, const ShotDeclaration& D, const ShotFacts& F)
		{
			const int Me = S.Shooter;
			const int Opp = 1 - Me;
			ShotOutcome O = MakeOutcome(S);
			FoulSet X = DetectCommonFouls(C, F); // no 3.2 in 14.1
			const std::uint32_t OnTable = OnTableObjectBalls(S);
			const int Fc = ResolveFirstContact(F, OnTable); // any ball may be hit first
			const int P311 = BadPlayFromAboveHeadString(S, F, Fc);
			AddBadPlayAboveHeadString(X, P311);
			const Call Called = ResolveCall(C, S, D, F);
			const bool Made = C.Calls == CallMode::None ? (D.Kind != ShotKind::Safety && F.AnyObjectBallPocketed) : CalledShotMade(D, F, Called);
			const std::uint32_t Spots = PocketedObjectBalls(F) | F.ObjectBallsOffTable;

			if (S.IsBreakShot) // opening break, R 7.3
			{
				// The called ball pocketed (not "legally", v1.1 C3), or after the first rack contact the cue ball and
				// at least two object balls each driven to a rail (pocketed counts, R 2.7).
				const bool Requirement = Made
					|| (F.CueBallContactedRack && F.CueBallDrivenToRailAfterRackContact && F.NumObjectBallsDrivenToRailAfterRackContact >= 2);
				if (!Requirement) // breaking foul, R 7.10: absorbs every standard foul, never counted (R 7.11)
				{
					O.ScoreDelta[Me] = -2;
					O.FoulsAfter[Me] = S.Players[Me].ConsecutiveFouls;
					PushSpots(O, Spots);
					O.CueBallIfAccepted = (F.CueBallPocketed || F.CueBallOffTable) ? CueBallNext::InHandAboveHeadString : CueBallNext::InPosition; // INTERPRETATION
					X.Add(Foul::BreakingFoul141);
					SetDecide(O, Opp, {Option::AcceptTable, Option::RequireRebreak});
					return Finish(O, X, "R 7.3(b)");
				}
			}
			else if (C.RailAfterContactRequired && NoRailAfterContact(F, true))
			{
				X.Add(Foul::NoRailAfterContact);
			}

			if (!X.IsEmpty()) // standard foul, R 7.9
			{
				O.ScoreDelta[Me] = -1;
				PushSpots(O, Spots); // R 7.6: pocketed and off-table balls are spotted (ascending, INTERPRETATION)
				const int N = S.Players[Me].ConsecutiveFouls + 1;
				if (C.ThreeFoulRule && N >= 3) // R 7.11
				{
					O.ScoreDelta[Me] -= 15;
					O.FoulsAfter[Me] = 0;
					O.BallsToSpot.Clear(); // all 15 balls are re-racked
					X.Add(Foul::ThreeConsecutiveFouls);
					SetRerackAndBreak(O, Me, CueBallNext::InHandAboveHeadString); // the offender takes an opening break
					return Finish(O, X, "R 7.11");
				}
				O.FoulsAfter[Me] = N;
				const bool Kitchen = F.CueBallPocketed || F.CueBallOffTable || P311 == 2;
				SetPass(O, Opp, Kitchen ? CueBallNext::InHandAboveHeadString : CueBallNext::InPosition);
				return Finish(O, X, "R 7.9");
			}
			O.FoulsAfter[Me] = 0;

			if (!Made) // miss or safety: every ball pocketed on the shot is spotted, R 7.5, 7.6
			{
				PushSpots(O, PocketedObjectBalls(F));
				SetPass(O, Opp, CueBallNext::InPosition);
				return Finish(O, X, D.Kind == ShotKind::Safety ? "R 7.5" : "R 7.6");
			}
			const int Points = CountBits(PocketedObjectBalls(F)); // R 7.7: every ball pocketed on a scoring shot
			O.ScoreDelta[Me] = Points;
			if (S.Players[Me].Score + Points >= C.TargetPoints)
			{
				O.Next = NextAction::MatchWon;
				O.Winner = Me;
				O.NextShooter = -1;
				return Finish(O, X, "R 7.4");
			}
			const std::uint32_t Left = ObjectBallsLeft(S, F);
			const int LeftCount = CountBits(Left);
			if (LeftCount == 1) // R 7.8 / Table 1 (9.5)
			{
				int Fifteenth = 1;
				while (Fifteenth < kRulesBallCount && (Left & BallBit(Fifteenth)) == 0u)
				{
					++Fifteenth;
				}
				O.Rack = PlanRerack14(F.Balls[kCueBallId].FinalPosition, Fifteenth, F.Balls[Fifteenth].FinalPosition, T, C.Tolerances);
			}
			else if (LeftCount == 0) // R 7.8(a): the 15th went down with the 14th
			{
				O.Rack = PlanRerack15AfterFifteenthPocketed(F.Balls[kCueBallId].FinalPosition, T);
			}
			SetContinue(O, Me);
			return Finish(O, X, "R 7.4");
		}

		// ------------------------------------------------------------------------------------------
		// WPA Blackball (rules.md 12.4)
		// ------------------------------------------------------------------------------------------

		// Standard Blackball foul: the incoming player gets a free shot with the cue ball in position or in hand in
		// baulk; balls driven off the table are spotted (black, next shooter's group - reds first if open - then the rest).
		ShotOutcome BlackballFoul(const RulesConfig& C, const GameState& S, const ShotFacts& F, FoulSet X, ShotOutcome O, const char* RuleRef)
		{
			const int Me = S.Shooter;
			const int Opp = 1 - Me;
			const std::uint32_t Off = F.ObjectBallsOffTable;
			BallGroup NextGroup = S.TableOpen ? BallGroup::None : S.Players[Opp].Group;
			if (NextGroup == BallGroup::None) NextGroup = BallGroup::Solids;
			PushSpots(O, Off & BallBit(kEightBall));
			PushSpots(O, Off & GroupMask(NextGroup));
			PushSpots(O, Off);

			const int N = S.Players[Me].ConsecutiveFouls + 1;
			if (C.ThreeFoulRule && N >= 3)
			{
				O.FoulsAfter[Me] = 0;
				X.Add(Foul::ThreeConsecutiveFouls);
				SetRackWon(O, Opp);
				return Finish(O, X, "R 3.13");
			}
			O.FoulsAfter[Me] = N;
			const bool CueBallGone = F.CueBallPocketed || F.CueBallOffTable;
			SetPass(O, Opp, CueBallGone ? CueBallNext::InHandBaulk : CueBallNext::InPosition);
			O.NextFreeShot = true;
			O.NextVisits = (C.FoulCueBall == FoulCueBallMode::TwoVisits || C.FoulCueBall == FoulCueBallMode::FreeShotPlusVisit) ? 1 : 0;
			return Finish(O, X, RuleRef);
		}

		ShotOutcome EvaluateBlackball(const RulesConfig& C, const GameState& S, const ShotFacts& F)
		{
			const int Me = S.Shooter;
			const int Opp = 1 - Me;
			ShotOutcome O = MakeOutcome(S);
			FoulSet X = DetectCommonFouls(C, F);

			if (S.IsBreakShot) // R 8.5
			{
				if (F.IsPocketed(kEightBall))
				{
					// Black on the break: re-rack, the same player breaks again; a scratch or a ball off the table on
					// that break is ignored.
					SetRerackAndBreak(O, Me, CueBallNext::InHandBaulk);
					return Finish(O, FoulSet{}, "R 8.5");
				}
				if (!F.AnyObjectBallPocketed && F.NumObjectBallsCrossedCenterString < 2) X.Add(Foul::BlackballBreakFoul);
				if (!X.IsEmpty())
				{
					return BlackballFoul(C, S, F, X, O, "R 8.5(b)");
				}
				O.FoulsAfter[Me] = 0;
				if (F.AnyObjectBallPocketed) SetContinue(O, Me); // the table stays open after the break
				else SetPass(O, Opp, CueBallNext::InPosition);
				return Finish(O, X, "R 8.5");
			}

			const BallGroup Mine = S.Players[Me].Group;
			const bool Open = S.TableOpen || Mine == BallGroup::None;
			const std::uint32_t Legal = BlackballLegalMask(S);
			const bool Wbf = WrongBallFirst(F, Legal);
			if (Wbf) X.Add(Foul::WrongBallFirst);
			if (C.RailAfterContactRequired && NoRailAfterContact(F, !Wbf)) X.Add(Foul::NoRailAfterContact);
			const std::uint32_t Potted = PocketedObjectBalls(F) & ~BallBit(kEightBall);
			if (!Open && !S.FreeShot)
			{
				const BallGroup Theirs = Mine == BallGroup::Solids ? BallGroup::Stripes : BallGroup::Solids;
				if ((Potted & GroupMask(Theirs)) != 0u && (Potted & GroupMask(Mine)) == 0u) X.Add(Foul::PottedOpponentBallOnly);
			}

			// Only a POTTED black decides the rack. A black driven off the table is a standard foul: it is spotted
			// first (rules.md 12.4 spotting order "black, then ...") and the opponent gets a free shot.
			if (F.IsPocketed(kEightBall))
			{
				O.FoulsAfter[Me] = X.IsEmpty() ? 0 : S.Players[Me].ConsecutiveFouls + 1;
				const bool GroupLeft = !Open && (ObjectBallsLeft(S, F) & GroupMask(Mine)) != 0u;
				if (!X.IsEmpty() || Open || GroupLeft)
				{
					SetRackWon(O, Opp); // black on an illegal shot, or while own balls remain after the shot
					return Finish(O, X, "R 8 (loss of rack)");
				}
				SetRackWon(O, Me);
				return Finish(O, X, "R 8");
			}
			if (!X.IsEmpty())
			{
				return BlackballFoul(C, S, F, X, O, "R 8 (foul: free shot)");
			}
			O.FoulsAfter[Me] = 0;

			if (Open && !S.FreeShot)
			{
				const bool Solids = (Potted & GroupMask(BallGroup::Solids)) != 0u;
				const bool Stripes = (Potted & GroupMask(BallGroup::Stripes)) != 0u;
				if (Solids != Stripes)
				{
					O.AssignShooterGroup = Solids ? BallGroup::Solids : BallGroup::Stripes; // only one group potted
				}
			}
			const bool Counts = (Open || S.FreeShot) ? Potted != 0u : (Potted & GroupMask(Mine)) != 0u;
			if (Counts)
			{
				SetContinue(O, Me);
				O.NextVisits = S.VisitsRemaining;
			}
			else
			{
				SetPassOrVisit(O, S, Opp);
			}
			return Finish(O, X, "R 8");
		}
	}

	ShotOutcome EvaluateShot(const RulesConfig& Config, const RulesTable& Table, const GameState& State, const ShotDeclaration& Declaration,
		const ShotFacts& Facts)
	{
		switch (State.Game)
		{
		case Discipline::EightBall: return EvaluateEightBall(Config, State, Declaration, Facts);
		case Discipline::NineBall: return EvaluateNineBall(Config, State, Declaration, Facts);
		case Discipline::TenBall: return EvaluateTenBall(Config, State, Declaration, Facts);
		case Discipline::StraightPool: return EvaluateStraightPool(Config, Table, State, Declaration, Facts);
		case Discipline::Blackball: return EvaluateBlackball(Config, State, Facts);
		}
		return MakeOutcome(State);
	}

	Call ResolveCall(const RulesConfig& Config, const GameState& State, const ShotDeclaration& Declaration, const ShotFacts& Facts)
	{
		if (Config.Calls != CallMode::ObviousAssist || Declaration.Called.Ball != kNoBall || Declaration.Kind == ShotKind::Safety
			|| Declaration.Kind == ShotKind::PushOut)
		{
			return Declaration.Called;
		}
		// 4.5 ObviousAssist: Obvious(b, p) <=> b = first contact; before b drops in p it touches no other ball and no
		// rail other than p's jaws (or p's interior); the cue ball touched no rail before hitting b.
		const int B = ResolveFirstContact(Facts, LegalFirstContactMask(Config, State, Declaration));
		if (!IsObjectBallId(B))
		{
			return {};
		}
		const BallShotSummary& Ball = Facts.Balls[B];
		if (!Ball.Pocketed || Ball.Pocket == PocketId::None || Ball.BallContactsOverflow || Ball.RailContactsOverflow)
		{
			return {};
		}
		const PocketId P = Ball.Pocket;
		double HitTime = kInfinity;
		for (const BallContactEntry& E : Ball.BallContacts)
		{
			if (E.Partner == kCueBallId)
			{
				HitTime = HitTime < E.Time ? HitTime : E.Time;
			}
			else if (E.Time <= Ball.PocketedTime)
			{
				return {}; // kissed or combined
			}
		}
		for (const RailContactEntry& E : Ball.RailContacts)
		{
			const bool OwnPocket = (E.Kind == RailContactKind::Jaw || E.Kind == RailContactKind::Liner) && E.Id == static_cast<std::uint8_t>(P);
			if (E.Time <= Ball.PocketedTime && !OwnPocket)
			{
				return {}; // banked, or touched another pocket's jaw
			}
		}
		const BallShotSummary& Cue = Facts.Balls[kCueBallId];
		if (Cue.RailContactsOverflow)
		{
			return {};
		}
		for (const RailContactEntry& E : Cue.RailContacts)
		{
			if (E.Time < HitTime)
			{
				return {}; // kick shot
			}
		}
		Call Inferred;
		Inferred.Ball = static_cast<BallId>(B);
		Inferred.Pocket = P;
		return Inferred;
	}

	int LowestObjectBallAtStart(const GameState& State)
	{
		for (int b = 1; b < kRulesBallCount; ++b)
		{
			if (OnTableAtStart(State, b))
			{
				return b;
			}
		}
		return -1;
	}

	bool GroupCleared(const GameState& State, BallGroup Group)
	{
		if (Group == BallGroup::None)
		{
			return false;
		}
		return (OnTableObjectBalls(State) & GroupMask(Group)) == 0u;
	}

	int CountObjectBallsOnTable(const GameState& State)
	{
		return CountBits(OnTableObjectBalls(State));
	}

	std::uint32_t LegalFirstContactMask(const RulesConfig& Config, const GameState& State, const ShotDeclaration& Declaration)
	{
		switch (State.Game)
		{
		case Discipline::EightBall: return EightBallLegalMask(Config, State, EffectiveClaim(State, Declaration, Declaration.Called.Ball));
		case Discipline::NineBall:
		case Discipline::TenBall:
		{
			if (Declaration.Kind == ShotKind::PushOut && !State.IsBreakShot)
			{
				return OnTableObjectBalls(State); // 3.2 suspended on a push-out
			}
			const int Lowest = LowestObjectBallAtStart(State);
			return Lowest > 0 ? BallBit(Lowest) : 0u;
		}
		case Discipline::StraightPool: return OnTableObjectBalls(State);
		case Discipline::Blackball: return BlackballLegalMask(State);
		}
		return 0u;
	}
}
