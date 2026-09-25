#include "rb/Core/FpGuard.h"
// Owner: WP-9 (rules table procedures & match). Spec: rules.md 4.3, 4.4, 5, 9.5.
#include "rb/Rules/TableRules.h"

#include "rb/Core/Random.h"
#include "rb/Math/Scalar.h"

namespace rb::rules
{
	namespace
	{
		constexpr double kSqrt3 = 1.7320508075688772935;

		// Site indices in BuildRackLattice order (row, then index increasing with y), rules.md 5.1.
		constexpr int kApexSite = 0;           // (row 0)
		constexpr int kCenterSite = 4;         // (row 2, index 1) for every shape
		constexpr int kBackCornerLowSite = 10; // Triangle15 (row 4, index 0)
		constexpr int kBackCornerHighSite = 14;// Triangle15 (row 4, index 4)

		constexpr std::uint32_t BallBit(int Ball) { return 1u << static_cast<unsigned>(Ball); }

		constexpr std::uint32_t BallRangeMask(int First, int Last)
		{
			std::uint32_t Mask = 0u;
			for (int b = First; b <= Last; ++b)
			{
				Mask |= BallBit(b);
			}
			return Mask;
		}

		constexpr int SiteCountOf(RackShape Shape)
		{
			switch (Shape)
			{
			case RackShape::Triangle15: return 15;
			case RackShape::Triangle10: return 10;
			case RackShape::Diamond9: return 9;
			}
			return 0;
		}

		double RadiusOf(const RulesTable& Table, int Ball)
		{
			return (Ball >= 0 && Ball < kRulesBallCount) ? Table.BallRadius[Ball] : Table.NominalBallRadius;
		}

		void BeginRack(RackShape Shape, RackAnchor Anchor, RackAssignment& Out)
		{
			Out = RackAssignment{};
			Out.Shape = Shape;
			Out.Anchor = Anchor;
			Out.SiteCount = SiteCountOf(Shape);
		}

		void AssignSite(RackAssignment& Out, int Site, int Ball)
		{
			Out.BallAtSite[Site] = static_cast<BallId>(Ball);
			Out.Racked[Ball] = true;
		}

		// Shuffles Free[0..Count) (seeded) and fills the still empty sites in site order (the apex is
		// skipped when SkipApex); balls left over stay unracked.
		void FillRandom(RackAssignment& Out, Rng& Stream, BallId* Free, int Count, bool SkipApex)
		{
			Stream.Shuffle(Free, Count);
			int Next = 0;
			for (int Site = 0; Site < Out.SiteCount && Next < Count; ++Site)
			{
				if (Out.BallAtSite[Site] != kNoBall || (SkipApex && Site == kApexSite))
				{
					continue;
				}
				AssignSite(Out, Site, Free[Next]);
				++Next;
			}
		}

		// Collects the balls of Mask (ascending ids) that are not racked yet.
		int CollectFree(const RackAssignment& Out, std::uint32_t Mask, BallId* Free)
		{
			int Count = 0;
			for (int b = 1; b < kRulesBallCount; ++b)
			{
				if ((Mask & BallBit(b)) != 0u && !Out.Racked[b])
				{
					Free[Count] = static_cast<BallId>(b);
					++Count;
				}
			}
			return Count;
		}

		// 8-ball / Blackball fill (5.1): 8 at (r2, k1), one solid and one stripe in the back corners (which
		// corner is random), everything else random (the apex can therefore never hold the 8).
		void FillEightBall(RackAssignment& Out, Rng& Stream)
		{
			AssignSite(Out, kCenterSite, 8);
			const int Solid = 1 + static_cast<int>(Stream.NextBelow(7u));
			const int Stripe = 9 + static_cast<int>(Stream.NextBelow(7u));
			const bool SolidLow = Stream.NextBelow(2u) == 0u;
			AssignSite(Out, kBackCornerLowSite, SolidLow ? Solid : Stripe);
			AssignSite(Out, kBackCornerHighSite, SolidLow ? Stripe : Solid);
			BallId Free[kRulesBallCount];
			const int Count = CollectFree(Out, BallRangeMask(1, 15), Free);
			FillRandom(Out, Stream, Free, Count, false);
		}

		// Apex ball + center ball fixed, the rest of the balls 1..LastBall random (9-ball, 10-ball).
		void FillApexAndCenter(RackAssignment& Out, Rng& Stream, int ApexBall, int CenterBall, int LastBall)
		{
			AssignSite(Out, kApexSite, ApexBall);
			AssignSite(Out, kCenterSite, CenterBall);
			BallId Free[kRulesBallCount];
			const int Count = CollectFree(Out, BallRangeMask(1, LastBall), Free);
			FillRandom(Out, Stream, Free, Count, false);
		}

		// Lattice (WP-2) with the diameter of the largest racked ball, micro-gaps with the next value of the
		// seed stream, anchor site fixed on the foot spot.
		ErrorCode PlaceRack(const RulesTable& Table, Rng& Stream, const RackGapParams& Gaps, RackAssignment& Out)
		{
			double Diameter = 0.0;
			for (int Site = 0; Site < Out.SiteCount; ++Site)
			{
				if (Out.BallAtSite[Site] != kNoBall)
				{
					Diameter = Max(Diameter, 2.0 * RadiusOf(Table, Out.BallAtSite[Site]));
				}
			}
			if (!(Diameter > 0.0))
			{
				Diameter = 2.0 * Table.NominalBallRadius;
			}

			const double ApexX = RackApexX(Out.Shape, Out.Anchor, Table.FootSpot.x, Diameter);
			RackSite Sites[kMaxRackSites];
			const int Count = BuildRackLattice(Out.Shape, ApexX, Diameter, Sites);
			if (Count != Out.SiteCount)
			{
				return ErrorCode::InvalidState; // lattice unavailable / inconsistent
			}

			Vec2 Positions[kMaxRackSites];
			for (int Site = 0; Site < Count; ++Site)
			{
				Positions[Site] = Sites[Site].Position + Vec2{0.0, Table.FootSpot.y};
			}
			const std::uint64_t GapSeed = Stream.NextU64();
			ApplyRackGaps(Positions, Count, RackAnchorSiteIndex(Out.Shape, Out.Anchor), Diameter, Gaps, GapSeed);

			for (int Site = 0; Site < Count; ++Site)
			{
				const BallId Ball = Out.BallAtSite[Site];
				if (Ball != kNoBall)
				{
					Out.Position[Ball] = Positions[Site];
				}
			}
			return ErrorCode::Ok;
		}

		// Distance from P to the segment [A, B].
		double SegmentDistance(const Vec2& P, const Vec2& A, const Vec2& B)
		{
			const Vec2 Ab = B - A;
			const double Len2 = LengthSquared(Ab);
			const double T = Len2 > 0.0 ? Clamp(Dot(P - A, Ab) / Len2, 0.0, 1.0) : 0.0;
			return Sqrt(LengthSquared(P - (A + Ab * T)));
		}
	}

	ErrorCode GenerateRack(Discipline Game, const RulesConfig& Config, const RulesTable& Table, std::uint64_t Seed, bool ApexEmpty,
		const RackGapParams& Gaps, RackAssignment& Out)
	{
		if (Game == Discipline::StraightPool)
		{
			return GenerateStraightPoolRack(Table, Seed, ApexEmpty, BallRangeMask(1, 15), Gaps, Out);
		}
		Out = RackAssignment{};
		if (ApexEmpty)
		{
			return ErrorCode::InvalidArgument;
		}

		Rng Stream(Seed);
		switch (Game)
		{
		case Discipline::EightBall:
			BeginRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, Out);
			FillEightBall(Out, Stream);
			break;
		case Discipline::Blackball:
			BeginRack(RackShape::Triangle15, RackAnchor::CenterOnFootSpot, Out);
			FillEightBall(Out, Stream);
			break;
		case Discipline::NineBall:
			BeginRack(RackShape::Diamond9,
				Config.NineBallRack == NineBallRackRule::NineOnSpot ? RackAnchor::CenterOnFootSpot : RackAnchor::ApexOnFootSpot, Out);
			FillApexAndCenter(Out, Stream, 1, 9, 9);
			break;
		case Discipline::TenBall:
			BeginRack(RackShape::Triangle10, RackAnchor::ApexOnFootSpot, Out);
			FillApexAndCenter(Out, Stream, 1, 10, 10);
			break;
		case Discipline::StraightPool:
			break; // handled above
		}

		const ErrorCode Placed = PlaceRack(Table, Stream, Gaps, Out);
		if (!Succeeded(Placed))
		{
			Out = RackAssignment{};
		}
		return Placed;
	}

	ErrorCode GenerateStraightPoolRack(const RulesTable& Table, std::uint64_t Seed, bool ApexEmpty, std::uint32_t BallMask,
		const RackGapParams& Gaps, RackAssignment& Out)
	{
		BeginRack(RackShape::Triangle15, RackAnchor::ApexOnFootSpot, Out);
		Rng Stream(Seed);
		BallId Free[kRulesBallCount];
		const int Count = CollectFree(Out, BallMask & BallRangeMask(1, 15), Free);
		FillRandom(Out, Stream, Free, Count, ApexEmpty);

		const ErrorCode Placed = PlaceRack(Table, Stream, Gaps, Out);
		if (!Succeeded(Placed))
		{
			Out = RackAssignment{};
		}
		return Placed;
	}

	Vec2 SpotBall(const GameState& State, int Ball, const RulesTable& Table, const RulesTolerances& Tolerances)
	{
		const double Rb = RadiusOf(Table, Ball);
		const double Eps = Tolerances.Line;
		const double FootX = Table.FootSpot.x;
		const double LineY = Table.FootSpot.y; // the long string
		const double MinX = -0.5 * Table.Length + Rb;
		const double MaxX = 0.5 * Table.Length - Rb;

		// Step 1: blocked open intervals (x_j - h_j, x_j + h_j) on the long string, per-ball D_j.
		double Lo[kRulesBallCount];
		double Hi[kRulesBallCount];
		int Blockers = 0;
		for (int j = 0; j < kRulesBallCount; ++j)
		{
			if (j == Ball || State.Balls[j].Kind != BallStatusKind::OnTable)
			{
				continue;
			}
			const double D = Rb + Table.BallRadius[j] + (j == kCueBallId ? Tolerances.SpotCueBallGap : 0.0);
			const Vec2& P = State.Balls[j].Position;
			const double Dy = P.y - LineY;
			if (Abs(Dy) < D)
			{
				const double H = Sqrt(D * D - Dy * Dy);
				Lo[Blockers] = P.x - H;
				Hi[Blockers] = P.x + H;
				++Blockers;
			}
		}

		// Pitfall 26: exact tangency must not block (tolerance eps_line on both ends).
		const auto IsBlocked = [&](double X) {
			for (int k = 0; k < Blockers; ++k)
			{
				if (Lo[k] + Eps < X && X < Hi[k] - Eps)
				{
					return true;
				}
			}
			return false;
		};
		const auto InRange = [&](double X) { return X >= MinX && X <= MaxX; };

		// Step 3: smallest free x >= x_FS; candidates x_FS and the right ends >= x_FS.
		if (InRange(FootX) && !IsBlocked(FootX))
		{
			return {FootX, LineY};
		}
		double Best = kInfinity;
		for (int k = 0; k < Blockers; ++k)
		{
			const double X = Hi[k];
			if (X >= FootX && X < Best && InRange(X) && !IsBlocked(X))
			{
				Best = X;
			}
		}
		if (Best < kInfinity)
		{
			return {Best, LineY};
		}

		// Step 4: toward the head, largest free x < x_FS; candidates the left ends.
		double BestLow = -kInfinity;
		for (int k = 0; k < Blockers; ++k)
		{
			const double X = Lo[k];
			if (X < FootX && X > BestLow && InRange(X) && !IsBlocked(X))
			{
				BestLow = X;
			}
		}
		if (BestLow > -kInfinity)
		{
			return {BestLow, LineY};
		}
		return Table.FootSpot; // unreachable with at most 16 balls on a regulation table
	}

	void SpotBalls(GameState& State, const BallId* Balls, int Count, const RulesTable& Table, const RulesTolerances& Tolerances)
	{
		for (int i = 0; i < Count; ++i)
		{
			const int Ball = Balls[i];
			if (Ball < 0 || Ball >= kRulesBallCount)
			{
				continue;
			}
			const Vec2 P = SpotBall(State, Ball, Table, Tolerances);
			State.Balls[Ball].Kind = BallStatusKind::OnTable;
			State.Balls[Ball].Position = P;
		}
	}

	int SpotRequestCandidate(const GameState& State, std::uint32_t LegalMask, const RulesTable& Table, const RulesTolerances& Tolerances)
	{
		if (State.CueBall != CueBallNext::InHandAboveHeadString)
		{
			return -1;
		}
		int Best = -1;
		double BestX = -kInfinity;
		for (int b = 1; b < kRulesBallCount; ++b)
		{
			if ((LegalMask & BallBit(b)) == 0u || State.Balls[b].Kind != BallStatusKind::OnTable)
			{
				continue;
			}
			const Vec2& P = State.Balls[b].Position;
			if (!AboveHeadString(P, Table, Tolerances.Line))
			{
				return -1; // a legal ball on or below the head string is playable: no request
			}
			if (P.x > BestX) // nearest the head string; ties keep the lowest id
			{
				BestX = P.x;
				Best = b;
			}
		}
		return Best;
	}

	RackOutline StraightPoolRackOutline(const RulesTable& Table)
	{
		const double R = Table.NominalBallRadius;
		const double X = Table.FootSpot.x;
		const double Y = Table.FootSpot.y;
		const double BackX = X + 4.0 * kSqrt3 * R + R;
		const double HalfBack = 4.0 * R + kSqrt3 * R;
		RackOutline O;
		O.Apex = {X - 2.0 * R, Y};
		O.BackLeft = {BackX, Y + HalfBack};
		O.BackRight = {BackX, Y - HalfBack};
		return O;
	}

	bool InterferesWithRack(const Vec2& P, double Radius, const RulesTable& Table)
	{
		const RackOutline O = StraightPoolRackOutline(Table);
		// Counter-clockwise: apex -> back right (-y) -> back left (+y).
		const Vec2 A = O.Apex;
		const Vec2 B = O.BackRight;
		const Vec2 C = O.BackLeft;
		const bool Inside = Cross(B - A, P - A) >= 0.0 && Cross(C - B, P - B) >= 0.0 && Cross(A - C, P - C) >= 0.0;
		if (Inside)
		{
			return Radius > 0.0;
		}
		const double Dist = Min(SegmentDistance(P, A, B), Min(SegmentDistance(P, B, C), SegmentDistance(P, C, A)));
		return Dist < Radius;
	}

	bool BlocksSpot(const Vec2& Spot, double SpotRadius, const Vec2& Other, double OtherRadius)
	{
		const double Reach = SpotRadius + OtherRadius;
		return LengthSquared(Other - Spot) < Reach * Reach;
	}

	RackCommand PlanRerack14(const Vec2& CueBall, int FifteenthBall, const Vec2& FifteenthBallPosition, const RulesTable& Table,
		const RulesTolerances& Tolerances)
	{
		const double CueRadius = RadiusOf(Table, kCueBallId);
		const double FifteenthRadius = RadiusOf(Table, FifteenthBall);
		RackCommand Command;
		Command.Kind = RackCommandKind::Rerack14;
		Command.FifteenthBall = static_cast<BallId>(FifteenthBall);

		const bool CueInRack = InterferesWithRack(CueBall, CueRadius, Table);
		const bool FifteenthInRack = InterferesWithRack(FifteenthBallPosition, FifteenthRadius, Table);
		if (!CueInRack && !FifteenthInRack)
		{
			return Command; // both stay
		}
		if (CueInRack && FifteenthInRack) // R 7.8(b)
		{
			Command.Kind = RackCommandKind::Rerack15;
			Command.FifteenthBallPlacement = PlacementCommand::IntoRack;
			Command.CueBallPlacement = PlacementCommand::InHandAboveHeadString;
			return Command;
		}
		if (FifteenthInRack) // R 7.8(c)
		{
			Command.FifteenthBallPlacement =
				BlocksSpot(Table.HeadSpot, FifteenthRadius, CueBall, CueRadius) ? PlacementCommand::ToCenterSpot : PlacementCommand::ToHeadSpot;
			return Command;
		}
		if (!AboveHeadString(FifteenthBallPosition, Table, Tolerances.Line)) // R 7.8(d): on or below the head string
		{
			Command.CueBallPlacement = PlacementCommand::InHandAboveHeadString;
			return Command;
		}
		Command.CueBallPlacement =
			BlocksSpot(Table.HeadSpot, CueRadius, FifteenthBallPosition, FifteenthRadius) ? PlacementCommand::ToCenterSpot : PlacementCommand::ToHeadSpot;
		return Command;
	}

	RackCommand PlanRerack15AfterFifteenthPocketed(const Vec2& CueBall, const RulesTable& Table)
	{
		RackCommand Command;
		Command.Kind = RackCommandKind::Rerack15;
		Command.FifteenthBall = kNoBall;
		Command.FifteenthBallPlacement = PlacementCommand::IntoRack;
		Command.CueBallPlacement =
			InterferesWithRack(CueBall, RadiusOf(Table, kCueBallId), Table) ? PlacementCommand::InHandAboveHeadString : PlacementCommand::Keep;
		return Command;
	}

	bool OverPocketOpening(const Vec2& P, const RulesTable& Table)
	{
		const int Count = Table.PocketCount < kPocketCount ? Table.PocketCount : kPocketCount;
		for (int k = 0; k < Count; ++k)
		{
			const PocketOpening& Pocket = Table.Pockets[k];
			// Inside the drop-edge circle.
			if (Pocket.DropEdgeRadius > 0.0 && LengthSquared(P - Pocket.CaptureCenter) < Pocket.DropEdgeRadius * Pocket.DropEdgeRadius)
			{
				return true;
			}
			// Beyond the mouth line, between the two virtual jaw points.
			const Vec2 J0 = Pocket.JawPoint[0];
			const Vec2 Mouth = Pocket.JawPoint[1] - J0;
			const double MouthLen2 = LengthSquared(Mouth);
			if (MouthLen2 > 0.0 && Dot(P - J0, Pocket.Axis) > 0.0)
			{
				const double T = Dot(P - J0, Mouth) / MouthLen2;
				if (T >= 0.0 && T <= 1.0)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool CueBallPlacementLegal(const GameState& State, const Vec2& P, CueBallNext Region, const RulesTable& Table,
		const RulesTolerances& Tolerances)
	{
		const double Rc = RadiusOf(Table, kCueBallId);
		if (!(Abs(P.x) <= 0.5 * Table.Length - Rc) || !(Abs(P.y) <= 0.5 * Table.Width - Rc))
		{
			return false; // also rejects NaN
		}
		if (OverPocketOpening(P, Table))
		{
			return false;
		}
		for (int j = 1; j < kRulesBallCount; ++j)
		{
			if (State.Balls[j].Kind != BallStatusKind::OnTable)
			{
				continue;
			}
			const double MinDist = Rc + Table.BallRadius[j] - Tolerances.PlacementOverlap;
			if (LengthSquared(P - State.Balls[j].Position) < MinDist * MinDist)
			{
				return false;
			}
		}
		switch (Region)
		{
		case CueBallNext::InHandAboveHeadString: return AboveHeadString(P, Table, Tolerances.Line);
		case CueBallNext::InHandBaulk: return InBaulk(P, Table, Tolerances.Line);
		case CueBallNext::InHandAnywhere:
		case CueBallNext::InPosition: return true;
		}
		return true;
	}
}
