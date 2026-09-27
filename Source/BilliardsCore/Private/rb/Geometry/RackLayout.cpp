#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 9; physics-collisions 3.9.5; architecture section 15 row 23.
#include "rb/Geometry/RackLayout.h"

#include "rb/Core/Constants.h"
#include "rb/Core/Random.h"
#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		constexpr double kSqrt3 = 1.7320508075688772935;
		constexpr double kTwoOverSqrt3 = 1.1547005383792515290;

		// Row spacing grid: 2^-44 m (5.7e-14 m). See RackRowSpacing.
		constexpr double kRowSpacingGrid = 1.0 / 17592186044416.0;

		constexpr int kMaxRows = 5;
		constexpr int kMaxRackContacts = 3 * kMaxRackSites; // a packing of n equal discs has < 3 n contacts (30 for the triangle)

		constexpr int kRelaxationSweeps = 32;
		constexpr int kMaxProjectionSweeps = 256;
		// Overlapping pairs are separated to D (1 + margin): 5.7e-11 m for pool balls, far below the physics
		// touching tolerance (ContactTol 1e-9 m), so a projected contact still counts as touching; the margin
		// makes the projection sweeps converge in a few sweeps instead of approaching D asymptotically.
		constexpr double kProjectionMargin = 1e-9;
		// Nominal contact in the input lattice: |p_i - p_j| <= D (1 + 1e-7).
		constexpr double kContactRel = 1e-7;
		// Two input balls belong to the same row when their x differ by less than D / 4 (rows are 0.87 D apart).
		constexpr double kRowTolRel = 0.25;

		struct ShapeRows
		{
			int Count = 0;
			int Balls[kMaxRows] = {};
		};

		constexpr ShapeRows RowsOf(RackShape Shape)
		{
			switch (Shape)
			{
			case RackShape::Triangle15: return {5, {1, 2, 3, 4, 5}};
			case RackShape::Triangle10: return {4, {1, 2, 3, 4, 0}};
			case RackShape::Diamond9: return {5, {1, 2, 3, 2, 1}};
			}
			return {};
		}

		// Row spacing (sqrt 3 / 2) D rounded UP to a multiple of 2^-44 m. The rounding (< 6e-14 m, far below any
		// physical scale) makes ApexX = FootSpotX - 2 dx and ApexX + 2 dx exact for every table-sized FootSpotX
		// (|x| < 256 m: dx lies on the binary grid of FootSpotX, and both results are representable), so the
		// anchor ball of a CenterOnFootSpot rack sits bitwise on the foot spot; rounding up keeps every lattice
		// distance >= D (no overlap).
		double RackRowSpacing(double BallDiameter)
		{
			const double Exact = 0.5 * kSqrt3 * BallDiameter;
			return -Floor(-Exact / kRowSpacingGrid) * kRowSpacingGrid;
		}

		struct Contact
		{
			int A = 0;
			int B = 0;
			double Target = 0.0; // D + g [m]
		};

		// Moves A and B along their center line so that |p_B - p_A| = Target: each ball takes half of the
		// correction; a fixed ball (the anchor) does not move and the other takes all of it.
		void ProjectPair(Vec2* P, int A, int B, int Anchor, double Target)
		{
			const Vec2 Delta = P[B] - P[A];
			const double Dist = Length(Delta);
			if (!(Dist > 0.0))
			{
				return; // coincident centres: no direction (cannot occur for a lattice input)
			}
			const Vec2 Correction = Delta * ((Target - Dist) / Dist);
			if (A == Anchor)
			{
				P[B] += Correction;
			}
			else if (B == Anchor)
			{
				P[A] -= Correction;
			}
			else
			{
				P[A] -= Correction * 0.5;
				P[B] += Correction * 0.5;
			}
		}

		// Target gap of one nominal contact (collisions 3.9.5; prior-art 5.6 outliers). Two draws per contact,
		// always: u_o decides the outlier branch, the second draw is the value, so the stream position of a
		// contact does not depend on earlier outcomes.
		double DrawGap(Rng& Stream, const RackGapParams& Gaps)
		{
			const double OutlierDraw = Stream.NextDouble01();
			const double ValueDraw = Stream.NextDouble01();
			const double Gap = OutlierDraw < Gaps.OutlierProbability ? Gaps.OutlierMin + (Gaps.OutlierMax - Gaps.OutlierMin) * ValueDraw
			                                                         : Gaps.Mean + Gaps.Jitter * (2.0 * ValueDraw - 1.0);
			return Max(0.0, Gap); // also maps NaN parameters to 0
		}
	}

	double RackApexX(RackShape Shape, RackAnchor Anchor, double FootSpotX, double BallDiameter)
	{
		if (Anchor == RackAnchor::ApexOnFootSpot || RowsOf(Shape).Count < 3)
		{
			return FootSpotX;
		}
		// The anchor (row 2) is two rows behind the apex: x_FS - sqrt(3) D (exact, see RackRowSpacing).
		return FootSpotX - 2.0 * RackRowSpacing(BallDiameter);
	}

	int RackAnchorSiteIndex(RackShape /*Shape*/, RackAnchor Anchor)
	{
		// Row 2, index 1 is site 1 + 2 + 1 = 4 in every shape (rows 1, 2, 3, ...).
		return Anchor == RackAnchor::ApexOnFootSpot ? 0 : 4;
	}

	int BuildRackLattice(RackShape Shape, double ApexX, double BallDiameter, RackSite* Out)
	{
		const ShapeRows Rows = RowsOf(Shape);
		if (Out == nullptr || Rows.Count == 0 || !IsFinite(ApexX) || !IsFinite(BallDiameter) || !(BallDiameter > 0.0))
		{
			return 0;
		}
		const double Dx = RackRowSpacing(BallDiameter);
		int Site = 0;
		for (int Row = 0; Row < Rows.Count; ++Row)
		{
			const int N = Rows.Balls[Row];
			const double X = ApexX + static_cast<double>(Row) * Dx;
			for (int j = 0; j < N; ++j)
			{
				// y = (j - (n - 1)/2) D: half-integer multiples of D, exactly antisymmetric about the long string.
				const double Offset = static_cast<double>(2 * j - (N - 1)) * 0.5;
				RackSite& S = Out[Site];
				S.Row = Row;
				S.Index = j;
				S.Position = {X, Offset * BallDiameter};
				++Site;
			}
		}
		return Site;
	}

	void ApplyRackGaps(Vec2* Positions, int Count, int AnchorIndex, double BallDiameter, const RackGapParams& Gaps, std::uint64_t Seed)
	{
		if (Positions == nullptr || Count < 2 || Count > kMaxRackSites || AnchorIndex < 0 || AnchorIndex >= Count || !IsFinite(BallDiameter) ||
			!(BallDiameter > 0.0))
		{
			return;
		}
		const double D = BallDiameter;

		// 1. Nominal contacts of the input lattice in canonical order (i < j, by i then j), one target gap each.
		Contact Contacts[kMaxRackContacts];
		int ContactCount = 0;
		const double ContactDist2 = Square(D * (1.0 + kContactRel));
		Rng Stream(Seed);
		bool AnyGap = false;
		for (int i = 0; i < Count; ++i)
		{
			for (int j = i + 1; j < Count; ++j)
			{
				if (LengthSquared(Positions[j] - Positions[i]) <= ContactDist2 && ContactCount < kMaxRackContacts)
				{
					const double Gap = DrawGap(Stream, Gaps);
					AnyGap = AnyGap || Gap > 0.0;
					Contacts[ContactCount] = {i, j, D + Gap};
					++ContactCount;
				}
			}
		}
		if (!AnyGap)
		{
			return; // every target gap is 0: the frozen lattice is already the exact solution (kept bitwise)
		}

		// Rows: balls grouped by x (ascending), each row sorted by y (ascending); ties keep the input order.
		int Order[kMaxRackSites];
		for (int i = 0; i < Count; ++i)
		{
			Order[i] = i;
		}
		for (int i = 1; i < Count; ++i) // insertion sort by (x, y): stable and deterministic
		{
			const int Key = Order[i];
			int j = i - 1;
			while (j >= 0 && (Positions[Order[j]].x > Positions[Key].x ||
								 (Positions[Order[j]].x == Positions[Key].x && Positions[Order[j]].y > Positions[Key].y)))
			{
				Order[j + 1] = Order[j];
				--j;
			}
			Order[j + 1] = Key;
		}
		int RowOf[kMaxRackSites] = {};
		int RowStart[kMaxRackSites + 1] = {};
		int RowCount = 0;
		for (int k = 0; k < Count; ++k)
		{
			if (k == 0 || Positions[Order[k]].x - Positions[Order[k - 1]].x > kRowTolRel * D)
			{
				RowStart[RowCount] = k;
				++RowCount;
			}
			RowOf[Order[k]] = RowCount - 1;
		}
		RowStart[RowCount] = Count;
		// Within a row the (x, y) sort already orders by y only if the row's x are equal; re-sort each row by y.
		for (int r = 0; r < RowCount; ++r)
		{
			for (int i = RowStart[r] + 1; i < RowStart[r + 1]; ++i)
			{
				const int Key = Order[i];
				int j = i - 1;
				while (j >= RowStart[r] && Positions[Order[j]].y > Positions[Key].y)
				{
					Order[j + 1] = Order[j];
					--j;
				}
				Order[j + 1] = Key;
			}
		}

		const auto GapOf = [&Contacts, ContactCount](int A, int B) {
			const int Lo = A < B ? A : B;
			const int Hi = A < B ? B : A;
			for (int c = 0; c < ContactCount; ++c)
			{
				if (Contacts[c].A == Lo && Contacts[c].B == Hi)
				{
					return Contacts[c].Target;
				}
			}
			return -1.0; // not a nominal contact
		};

		// 2. Row expansion. Rows move away from the anchor row along x by (2 / sqrt 3) x the mean target gap of
		// the contacts between consecutive rows (a 30 deg contact opens by dx sqrt(3) / 2).
		double RowShift[kMaxRackSites] = {};
		const int AnchorRow = RowOf[AnchorIndex];
		const auto MeanGapBetweenRows = [&](int RowA, int RowB) {
			double Sum = 0.0;
			int N = 0;
			for (int c = 0; c < ContactCount; ++c)
			{
				const int Ra = RowOf[Contacts[c].A];
				const int Rb = RowOf[Contacts[c].B];
				if ((Ra == RowA && Rb == RowB) || (Ra == RowB && Rb == RowA))
				{
					Sum += Contacts[c].Target - D;
					++N;
				}
			}
			return N > 0 ? Sum / static_cast<double>(N) : 0.0;
		};
		for (int r = AnchorRow + 1; r < RowCount; ++r)
		{
			RowShift[r] = RowShift[r - 1] + kTwoOverSqrt3 * MeanGapBetweenRows(r - 1, r);
		}
		for (int r = AnchorRow - 1; r >= 0; --r)
		{
			RowShift[r] = RowShift[r + 1] - kTwoOverSqrt3 * MeanGapBetweenRows(r, r + 1);
		}
		// In-row spreading: cumulative in-row target gaps along y, the anchor row centred on the anchor ball, every
		// other row on the midpoint of its extremes.
		for (int r = 0; r < RowCount; ++r)
		{
			double Cumulative[kMaxRackSites] = {};
			for (int k = RowStart[r] + 1; k < RowStart[r + 1]; ++k)
			{
				const double Target = GapOf(Order[k - 1], Order[k]);
				Cumulative[k] = Cumulative[k - 1] + (Target > 0.0 ? Target - D : 0.0);
			}
			double Center = 0.5 * (Cumulative[RowStart[r]] + Cumulative[RowStart[r + 1] - 1]);
			if (r == AnchorRow)
			{
				for (int k = RowStart[r]; k < RowStart[r + 1]; ++k)
				{
					if (Order[k] == AnchorIndex)
					{
						Center = Cumulative[k];
					}
				}
			}
			for (int k = RowStart[r]; k < RowStart[r + 1]; ++k)
			{
				const int Ball = Order[k];
				if (Ball != AnchorIndex)
				{
					Positions[Ball] += Vec2{RowShift[r], Cumulative[k] - Center};
				}
			}
		}

		// 3. Relaxation: Gauss-Seidel sweeps over the contacts in canonical order toward |p_i - p_j| = D + g_c.
		for (int Sweep = 0; Sweep < kRelaxationSweeps; ++Sweep)
		{
			for (int c = 0; c < ContactCount; ++c)
			{
				ProjectPair(Positions, Contacts[c].A, Contacts[c].B, AnchorIndex, Contacts[c].Target);
			}
		}

		// 4. Projection: sweeps over all pairs in canonical order separating any pair closer than D, until none is
		// left (the 30 contacts are inconsistent for isolated large gaps, so the relaxation's least-squares
		// compromise can compress a neighbouring contact below D).
		// The test is on the distance itself (not its square), so "no pair closer than D" holds for |p_i - p_j|
		// exactly as computed.
		const double Separated = D * (1.0 + kProjectionMargin);
		bool Overlap = true;
		for (int Sweep = 0; Sweep < kMaxProjectionSweeps && Overlap; ++Sweep)
		{
			Overlap = false;
			for (int i = 0; i < Count; ++i)
			{
				for (int j = i + 1; j < Count; ++j)
				{
					if (Length(Positions[j] - Positions[i]) < D)
					{
						ProjectPair(Positions, i, j, AnchorIndex, Separated);
						Overlap = true;
					}
				}
			}
		}
		if (Overlap)
		{
			// Guaranteed fallback (never observed): scale the rack about the anchor until the closest pair is D.
			double MinDist = kInfinity;
			for (int i = 0; i < Count; ++i)
			{
				for (int j = i + 1; j < Count; ++j)
				{
					MinDist = Min(MinDist, Length(Positions[j] - Positions[i]));
				}
			}
			if (MinDist > 0.0 && MinDist < D)
			{
				const double Scale = Separated / MinDist;
				const Vec2 Anchor = Positions[AnchorIndex];
				for (int i = 0; i < Count; ++i)
				{
					if (i != AnchorIndex)
					{
						Positions[i] = Anchor + (Positions[i] - Anchor) * Scale;
					}
				}
			}
		}
	}

	int CountTouchingPairs(const Vec2* Positions, int Count, double BallDiameter, double Tolerance)
	{
		if (Positions == nullptr || Count < 2)
		{
			return 0;
		}
		const double Reach2 = Square(BallDiameter + Tolerance);
		int Touching = 0;
		for (int i = 0; i < Count; ++i)
		{
			for (int j = i + 1; j < Count; ++j)
			{
				if (LengthSquared(Positions[j] - Positions[i]) <= Reach2)
				{
					++Touching;
				}
			}
		}
		return Touching;
	}

	double RackInnerSide(RackShape Shape, double BallDiameter)
	{
		// Offsetting each side of the ball-centre polygon by R lengthens it by R cot(alpha / 2) at each end
		// (equipment 9.1): 60 deg corners add sqrt(3) D per side, the 120 deg corners of the diamond 1/sqrt(3) D.
		switch (Shape)
		{
		case RackShape::Triangle15: return (4.0 + kSqrt3) * BallDiameter;
		case RackShape::Triangle10: return (3.0 + kSqrt3) * BallDiameter;
		case RackShape::Diamond9: return (2.0 + kTwoOverSqrt3) * BallDiameter;
		}
		return 0.0;
	}
}
