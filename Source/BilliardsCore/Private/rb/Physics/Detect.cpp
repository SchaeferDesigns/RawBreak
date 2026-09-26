#include "rb/Core/FpGuard.h"
// Owner: WP-5 (event detection). Spec: physics-collisions 3.1-3.6, 4.10, 5.3, 6.1-6.3, 7.4; prior-art 5.5, 5.7, 5.10;
// architecture 8.2, 8.8, 8.11 (a tilt chain piece is a GENERAL quadratic: Accel2 is never assumed parallel to Vel0,
// neither by a predictor nor by a bound). Allocation-free, deterministic.
#include "rb/Physics/Detect.h"

#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		// Tolerances of the predictors whose signatures carry no NumericsConfig (landing, support exit, boundary, apex).
		constexpr NumericsConfig kDefaultNumerics{};

		// =========================================================================================
		// Vector quadratics p(tau) = C + B tau + A tau^2 (collisions 3.1)
		// =========================================================================================
		struct VecQuad
		{
			Vec3 C; // position at tau = 0 [m]
			Vec3 B; // velocity at tau = 0 [m/s]
			Vec3 A; // half the acceleration [m/s^2]
		};

		// Re-expansion of a segment to the origin RefTime, always from the STORED segment origin (collisions 3.1).
		VecQuad Expand(const MotionSegment& Seg, double RefTime)
		{
			const double Dt = RefTime - Seg.T0;
			if (Dt == 0.0)
			{
				return {Seg.Pos0, Seg.Vel0, Seg.Accel2};
			}
			return {PositionAt(Seg, Dt), VelocityAt(Seg, Dt), Seg.Accel2};
		}

		// The segment relative to the point P (local time).
		VecQuad Relative(const MotionSegment& Seg, const Vec3& P) { return {Seg.Pos0 - P, Seg.Vel0, Seg.Accel2}; }

		VecQuad Difference(const VecQuad& P, const VecQuad& Q) { return {P.C - Q.C, P.B - Q.B, P.A - Q.A}; }

		VecQuad PlanOf(const VecQuad& Q) { return {Planar(Q.C), Planar(Q.B), Planar(Q.A)}; }

		// Component perpendicular to the unit direction D (Q = I - D D^T, collisions 4.10).
		VecQuad RejectFrom(const VecQuad& Q, const Vec3& D)
		{
			return {Q.C - D * Dot(D, Q.C), Q.B - D * Dot(D, Q.B), Q.A - D * Dot(D, Q.A)};
		}

		Vec3 At(const VecQuad& Q, double Tau) { return Q.C + Q.B * Tau + Q.A * (Tau * Tau); }
		Vec3 VelocityOf(const VecQuad& Q, double Tau) { return Q.B + Q.A * (2.0 * Tau); }

		// |Q(tau)|^2 as a quartic (the a4..a0 of collisions 3.2).
		Polynomial SquaredNorm(const VecQuad& Q)
		{
			Polynomial P;
			P.Degree = 4;
			P.c[0] = Dot(Q.C, Q.C);
			P.c[1] = 2.0 * Dot(Q.B, Q.C);
			P.c[2] = Dot(Q.B, Q.B) + 2.0 * Dot(Q.A, Q.C);
			P.c[3] = 2.0 * Dot(Q.A, Q.B);
			P.c[4] = Dot(Q.A, Q.A);
			return P;
		}

		// N . Q(tau) as a quadratic.
		Polynomial Along(const VecQuad& Q, const Vec3& N)
		{
			Polynomial P;
			P.Degree = 2;
			P.c[0] = Dot(N, Q.C);
			P.c[1] = Dot(N, Q.B);
			P.c[2] = Dot(N, Q.A);
			return P;
		}

		Polynomial Negated(const Polynomial& P)
		{
			Polynomial Q = P;
			for (int i = 0; i <= Q.Degree; ++i)
			{
				Q.c[i] = -Q.c[i];
			}
			return Q;
		}

		// Product of two polynomials (Degree(X) + Degree(Y) <= kMaxDegree).
		Polynomial Multiply(const Polynomial& X, const Polynomial& Y)
		{
			Polynomial P;
			P.Degree = X.Degree + Y.Degree;
			for (int i = 0; i <= X.Degree; ++i)
			{
				for (int j = 0; j <= Y.Degree; ++j)
				{
					P.c[i + j] += X.c[i] * Y.c[j];
				}
			}
			return P;
		}

		// Degree lowered to the highest EXACTLY non-zero coefficient (exposed gap polynomials, D-6).
		void DropExactZeros(Polynomial& P)
		{
			while (P.Degree > 0 && P.c[P.Degree] == 0.0)
			{
				--P.Degree;
			}
		}

		// Sum of absolute components: an upper bound of the Euclidean length without a square root.
		double NormL1(const Vec3& V) { return Abs(V.x) + Abs(V.y) + Abs(V.z); }

		// Local search window [0, TauMax] of one segment: its validity window cut at TimeLimit (absolute).
		double LocalWindow(const MotionSegment& Seg, double TimeLimit) { return Min(Seg.TauEnd, TimeLimit - Seg.T0); }

		// Plan angle Dir lies on the CCW arc [From, From + Sweep], widened by Slack [rad] on both sides.
		bool InAngularRange(const Vec2& Dir, double From, double Sweep, double Slack)
		{
			if (Sweep + 2.0 * Slack >= kTwoPi)
			{
				return true;
			}
			double Delta = Atan2(Dir.y, Dir.x) - (From - Slack);
			Delta -= kTwoPi * Floor(Delta / kTwoPi);
			return Delta <= Sweep + 2.0 * Slack;
		}

		// Convex CCW polygon contains P (plan), every edge widened by Slack [m] (watertight joints, prior-art 5.7).
		bool InsidePolygon(const RailTopPolygon& Poly, const Vec2& P, double Slack)
		{
			const int N = Poly.VertexCount;
			if (N < 3)
			{
				return false;
			}
			for (int i = 0; i < N; ++i)
			{
				const Vec2& V0 = Poly.Vertices[i];
				const Vec2& V1 = Poly.Vertices[(i + 1) % N];
				const Vec2 E = V1 - V0;
				if (Cross(E, P - V0) < -Slack * Length(E))
				{
					return false;
				}
			}
			return true;
		}

		// Height of the polygon's plane above the plan point P.
		double PlaneHeight(const RailTopPolygon& Poly, const Vec2& P)
		{
			const Vec3& N = Poly.PlaneNormal;
			if (Abs(N.z) < 1e-12)
			{
				return Poly.PlanePoint.z;
			}
			return Poly.PlanePoint.z - (N.x * (P.x - Poly.PlanePoint.x) + N.y * (P.y - Poly.PlanePoint.y)) / N.z;
		}

		// =========================================================================================
		// Earliest valid crossing of a contact / region function (collisions 3.3, 3.4, 3.6, 4.10)
		// =========================================================================================
		struct CrossingSpec
		{
			double Scale = 1.0;       // df/dgap near the crossing [f units per m] (2 D for squared distances, 1 for linear ones)
			bool Contact = true;      // contact feature (pressing and overlap semantics) or region boundary (drop edge, exit, ...)
			bool SolidBeyond = true;  // F < 0 is material / already crossed. False where it is free space (outside the hole wall,
			                          //   outside a plan overlap): only the touching band |F(0)| <= Scale ContactTol then counts.
		};

		// An unbounded window (no segment end, no TimeLimit, e.g. PocketFall against a resting ball) is searched up to the
		// polynomial's root bound, capped far beyond any shot (TimeHorizon is 600 s) so the time scaling stays finite.
		constexpr double kUnboundedWindow = 1e6; // [s]

		struct AlwaysValid
		{
			bool operator()(double) const { return true; }
		};

		struct NoPolish
		{
			double operator()(double Tau, double, double) const { return Tau; }
		};

		// F(tau) > 0 = separated / outside, = 0 at the contact (the boundary), < 0 = penetrating / beyond, in the local
		// time tau of the window [0, TauMax] that starts at the absolute time StartTime. Returns the earliest
		//  * start contact (tau = 0): F(0) <= Scale ContactTol (touching, incl. rounding overlap) and IsValid(0), and
		//    either approaching (F'(0) < -Scale ApproachSpeedTol) -> AtStart, or zero normal speed with F''(0) < 0
		//    -> AtStart | Pressing (contact features; a region crossing that is about to happen is AtStart only);
		//    touching and separating, or zero speed without F'' < 0, yields no start event (3.6);
		//  * otherwise the first DOWNWARD crossing in (0, TauMax] (approach test: F' < 0) whose position IsValid; a
		//    decreasing run that turns back up at an interior minimum above -Scale TangencyTolPerLength is a graze and a
		//    miss (3.3). Roots are isolated on the time-scaled polynomial (tau = T s, T = min(TauMax, root bound), 3.4),
		//    refined by safeguarded Newton-bisection, then optionally polished by Polish(tau, BracketLo, BracketHi).
		// Overlap beyond OverlapGuard at the start (contact features) sets ContactFlags::Overlap even without an event.
		template <class ValidFn, class PolishFn>
		ContactPrediction FirstCrossing(const Polynomial& F, double TauMax, double StartTime, const CrossingSpec& Spec, const NumericsConfig& N,
			const ValidFn& IsValid, const PolishFn& Polish)
		{
			ContactPrediction Out;
			if (!(TauMax >= 0.0) || !IsFinite(StartTime))
			{
				return Out; // empty window (or NaN); a corrupt time base never yields a non-finite event time (heap order)
			}

			// ---- start rules (3.6) ----
			const double F0 = F.c[0];
			const double F1 = F.Degree >= 1 ? F.c[1] : 0.0;
			const double F2 = F.Degree >= 2 ? 2.0 * F.c[2] : 0.0;
			if (F0 <= Spec.Scale * N.ContactTol && (Spec.SolidBeyond || F0 >= -Spec.Scale * N.ContactTol) && IsValid(0.0))
			{
				if (Spec.Contact && F0 < -Spec.Scale * N.OverlapGuard)
				{
					Out.Flags = ContactFlags::Overlap;
				}
				const double SpeedTol = Spec.Scale * N.ApproachSpeedTol;
				const bool IsPressing = Abs(F1) <= SpeedTol && F2 < 0.0;
				if (IsPressing || F1 < -SpeedTol)
				{
					const std::uint8_t Start = IsPressing && Spec.Contact ? static_cast<std::uint8_t>(ContactFlags::AtStart | ContactFlags::Pressing)
																		: ContactFlags::AtStart;
					Out.Found = true;
					Out.Time = StartTime;
					Out.Flags = static_cast<std::uint8_t>(Out.Flags | Start);
					return Out;
				}
			}
			if (!(TauMax > 0.0))
			{
				return Out;
			}

			// ---- time scaling and degree reduction (3.3, 3.4) ----
			double T = Min(TauMax, kUnboundedWindow);
			const double Bound = F.RootBound();
			if (Bound < T)
			{
				T = Bound;
			}
			if (!(T > 0.0))
			{
				return Out; // empty window, or a non-zero constant (no root)
			}
			Polynomial S = F.ScaledArgument(T);
			S.Trim(N.RootTrimRel);
			if (S.Degree == 0)
			{
				return Out;
			}
			const Polynomial DS = S.Derivative();

			// ---- monotone pieces between the critical points ----
			double Knots[Polynomial::kMaxDegree + 1];
			double Values[Polynomial::kMaxDegree + 1];
			int NumKnots = 0;
			Knots[NumKnots++] = 0.0;
			if (S.Degree >= 2)
			{
				double Critical[Polynomial::kMaxDegree];
				const int NumCritical = SolveInInterval(DS, 0.0, 1.0, Critical, N.RootTimeTol);
				for (int i = 0; i < NumCritical; ++i)
				{
					if (Critical[i] > Knots[NumKnots - 1] && Critical[i] < 1.0)
					{
						Knots[NumKnots++] = Critical[i];
					}
				}
			}
			Knots[NumKnots++] = 1.0;
			for (int k = 0; k < NumKnots; ++k)
			{
				Values[k] = S.Eval(Knots[k]);
			}

			// ---- decreasing runs: the first one that goes from > 0 to <= 0 and is not a graze ----
			const double GrazeTol = Spec.Scale * N.TangencyTolPerLength;
			int i = 0;
			while (i + 1 < NumKnots)
			{
				if (!(Values[i + 1] < Values[i]))
				{
					++i;
					continue;
				}
				int j = i + 1;
				while (j + 1 < NumKnots && Values[j + 1] <= Values[j])
				{
					++j;
				}
				if (Values[i] > 0.0 && Values[j] <= 0.0)
				{
					const bool InteriorMinimum = j + 1 < NumKnots;
					if (!(InteriorMinimum && Values[j] > -GrazeTol))
					{
						int p = i;
						while (!(Values[p + 1] <= 0.0))
						{
							++p;
						}
						const double s = Values[p + 1] == 0.0 ? Knots[p + 1]
															   : RefineBracketedRoot(S, DS, Knots[p], Knots[p + 1], N.RootTimeTol, N.RootMaxIterations);
						double Tau = Polish(s * T, Knots[p] * T, Knots[p + 1] * T);
						if (Tau > TauMax)
						{
							Tau = TauMax;
						}
						if (IsValid(Tau))
						{
							Out.Found = true;
							Out.Time = StartTime + Tau;
							return Out;
						}
					}
				}
				i = j;
			}
			return Out;
		}

		template <class ValidFn>
		ContactPrediction FirstCrossing(const Polynomial& F, double TauMax, double StartTime, const CrossingSpec& Spec, const NumericsConfig& N,
			const ValidFn& IsValid)
		{
			return FirstCrossing(F, TauMax, StartTime, Spec, N, IsValid, NoPolish{});
		}

		// Earlier of two predictions (ties keep A).
		bool Earlier(const ContactPrediction& A, const ContactPrediction& B) { return A.Found && (!B.Found || A.Time < B.Time); }

		// =========================================================================================
		// Shared geometry of the pocket and edge predictors
		// =========================================================================================

		// Contact of a ball (radius R) with a straight edge line from P0 along the unit D (length L): |Q(p - P0)|^2 = R^2 (4.10),
		// valid along [0, L] and on the side Side (Side . (p - P0) >= 0).
		ContactPrediction EdgeLineContact(const MotionSegment& Seg, double Radius, const Vec3& P0, const Vec3& D, double L, const Vec3& Side,
			double TimeLimit, const NumericsConfig& N)
		{
			const VecQuad Q0 = Relative(Seg, P0);
			Polynomial F = SquaredNorm(RejectFrom(Q0, D));
			F.c[0] -= Radius * Radius;
			const double Slack = N.SegmentParamSlack;
			const auto Valid = [&](double Tau)
			{
				const Vec3 W = At(Q0, Tau);
				const double s = Dot(D, W);
				return s >= -Slack && s <= L + Slack && Dot(Side, W) >= 0.0;
			};
			return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {2.0 * Radius, true}, N, Valid);
		}

		// Degree-8 contact of a ball with a horizontal circle (center C, radius Major, height Zc) thickened by the tube radius
		// Minor: Q^2 - 4 Major^2 rho^2 = 0 with Q = rho^2 + Major^2 + (z - Zc)^2 - (R + Minor)^2 (collisions 5.3), = (distance to
		// the circle^2 - (R + Minor)^2) * (distance to the antipodal circle point^2 - (R + Minor)^2). The second factor is > 0
		// whenever Major > R + Minor (rim torus, cut rim); for a small circle (jaw edge, Major = r_j < R) it can vanish deep
		// inside, so such callers also require Q > 0 (the root of the NEAR factor, collisions 5.3). Valid(tau) decides which
		// part of the tube exists.
		template <class ValidFn>
		ContactPrediction CircleTubeContact(const MotionSegment& Seg, double Radius, const Vec2& C, double Major, double Minor, double Zc, double TimeLimit,
			const NumericsConfig& N, const ValidFn& Valid)
		{
			const VecQuad Qh = PlanOf(Relative(Seg, ToVec3(C, 0.0)));
			const Polynomial Rho2 = SquaredNorm(Qh);
			Polynomial Zq;
			Zq.Degree = 2;
			Zq.c[0] = Seg.Pos0.z - Zc;
			Zq.c[1] = Seg.Vel0.z;
			Zq.c[2] = Seg.Accel2.z;
			const double Reach = Radius + Minor;
			Polynomial Q = Multiply(Zq, Zq);
			for (int i = 0; i <= 4; ++i)
			{
				Q.c[i] += Rho2.c[i];
			}
			Q.Degree = 4;
			Q.c[0] += Major * Major - Reach * Reach;
			Polynomial F = Multiply(Q, Q);
			const double FourMajor2 = 4.0 * Major * Major;
			for (int i = 0; i <= 4; ++i)
			{
				F.c[i] -= FourMajor2 * Rho2.c[i];
			}
			// df/dgap = 2 (R + Minor) * (second factor); that factor is 4 Major rho at a contact, rho (the center's plan distance)
			// in [Max(Major - Reach, 0), Major + Reach]. Clamped to that range (floor Major^2 near the axis): the start value sets
			// the touching band and the graze tolerance, which must stay those of a contact also for a start far away.
			const double Rho0 = Sqrt(Rho2.c[0]);
			const double Second0 = Q.c[0] + 2.0 * Major * Rho0;
			const double SecondMin = 4.0 * Major * Max(Major - Reach, 0.25 * Major);
			const double SecondMax = 4.0 * Major * (Major + Reach);
			const CrossingSpec Spec{2.0 * Reach * Clamp(Second0, SecondMin, SecondMax), true};
			return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, Spec, N, Valid);
		}

		// =========================================================================================
		// Broad-phase bounds of table elements (culling only; always conservative)
		// =========================================================================================
		Aabb3 SegmentBox(const Vec2& P0, const Vec2& P1, double ZLo, double ZHi, double Pad)
		{
			return {{Min(P0.x, P1.x) - Pad, Min(P0.y, P1.y) - Pad, ZLo - Pad}, {Max(P0.x, P1.x) + Pad, Max(P0.y, P1.y) + Pad, ZHi + Pad}};
		}

		Aabb3 NoseBox(const NoseSegment& Nose, double Pad) { return SegmentBox(Nose.Start, Nose.End, Nose.Height, Nose.Height, Pad); }

		Aabb3 JawBox(const JawArc& Arc, double Pad)
		{
			const double Rj = Arc.Radius + Pad;
			return {{Arc.Center.x - Rj, Arc.Center.y - Rj, Arc.Height - Pad}, {Arc.Center.x + Rj, Arc.Center.y + Rj, Arc.Height + Pad}};
		}

		// The undercut face is recessed up to h tan(beta_v) behind its plan line at the cloth; bounded by h (beta_v < 45 deg).
		Aabb3 FacingBox(const Facing& Face, double Pad) { return SegmentBox(Face.Start, Face.End, 0.0, Face.TopHeight, Pad + Face.TopHeight); }

		Aabb3 PocketBox(const PocketGeometry& Pocket, double Pad)
		{
			const double Rr = Max(Pocket.DropEdgeRadius, Pocket.CaptureRadius) + Pad;
			return {{Pocket.CaptureCenter.x - Rr, Pocket.CaptureCenter.y - Rr, -kInfinity},
				{Pocket.CaptureCenter.x + Rr, Pocket.CaptureCenter.y + Rr, Pocket.WallTopZ + Pad}};
		}

		Aabb3 PolygonBox(const RailTopPolygon& Poly, double Pad)
		{
			Aabb3 Box{{kInfinity, kInfinity, kInfinity}, {-kInfinity, -kInfinity, -kInfinity}};
			for (int i = 0; i < Poly.VertexCount; ++i)
			{
				const Vec2& V = Poly.Vertices[i];
				const double Z = PlaneHeight(Poly, V);
				Box.Lo = {Min(Box.Lo.x, V.x), Min(Box.Lo.y, V.y), Min(Box.Lo.z, Z)};
				Box.Hi = {Max(Box.Hi.x, V.x), Max(Box.Hi.y, V.y), Max(Box.Hi.z, Z)};
			}
			return Box.Inflated(Pad);
		}

		bool IsPhysicalRailTopEdge(RailEdgeKind Kind) { return Kind == RailEdgeKind::CushionBack || Kind == RailEdgeKind::OuterEdge || Kind == RailEdgeKind::Facing; }

		Aabb3 RailTopEdgeBox(const RailTopPolygon& Poly, int Edge, double Pad)
		{
			if (Edge == kCutRimEdge)
			{
				const double Z = PlaneHeight(Poly, Poly.CutCenter);
				const double Rr = Poly.CutRadius + Pad;
				return {{Poly.CutCenter.x - Rr, Poly.CutCenter.y - Rr, Z - Pad}, {Poly.CutCenter.x + Rr, Poly.CutCenter.y + Rr, Z + Pad}};
			}
			const Vec2& V0 = Poly.Vertices[Edge];
			const Vec2& V1 = Poly.Vertices[(Edge + 1) % Poly.VertexCount];
			const double Z0 = PlaneHeight(Poly, V0);
			const double Z1 = PlaneHeight(Poly, V1);
			return SegmentBox(V0, V1, Min(Z0, Z1), Max(Z0, Z1), Pad);
		}

		// Feature ordering key (Kind, Index, SubIndex) for deterministic ties.
		bool KeyLess(const TableFeatureRef& A, const TableFeatureRef& B)
		{
			if (A.Kind != B.Kind)
			{
				return A.Kind < B.Kind;
			}
			if (A.Index != B.Index)
			{
				return A.Index < B.Index;
			}
			return A.SubIndex < B.SubIndex;
		}

		// Nose / facing / jaw normal of an on-cloth edge contact: k = cos(theta) n - sin(theta) z (collisions 4.1).
		Vec3 TiltedDown(const Vec3& HorizontalOut, double Theta) { return HorizontalOut * Cos(Theta) - Vec3::UnitZ() * Sin(Theta); }

		double ElevationOf(const Vec3& Normal) { return Asin(Clamp(-Normal.z, -1.0, 1.0)); }
	}

	// =============================================================================================
	// Ball-ball (3.1-3.6, 7.4)
	// =============================================================================================
	Polynomial BallBallGapPolynomial(const MotionSegment& A, double RadiusA, const MotionSegment& B, double RadiusB, double RefTime)
	{
		const VecQuad D = Difference(Expand(B, RefTime), Expand(A, RefTime));
		Polynomial P = SquaredNorm(D);
		const double Sum = RadiusA + RadiusB;
		P.c[0] -= Sum * Sum;
		DropExactZeros(P);
		return P;
	}

	ContactPrediction PredictBallBall(const MotionSegment& A, double RadiusA, const MotionSegment& B, double RadiusB, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		const double RefTime = Max(A.T0, B.T0);
		const double End = Min(Min(A.T0 + A.TauEnd, B.T0 + B.TauEnd), TimeLimit);
		const double TauMax = End - RefTime;
		if (!(TauMax >= 0.0))
		{
			return {};
		}
		const VecQuad QA = Expand(A, RefTime);
		const VecQuad QB = Expand(B, RefTime);
		const VecQuad D = Difference(QB, QA);
		const double Sum = RadiusA + RadiusB;

		// Broad phase (3.5): the gap can close by at most reach_A + reach_B over the window (L1 norms bound the lengths).
		if (TauMax < kInfinity)
		{
			const double Reach = (NormL1(QA.B) + NormL1(QB.B)) * TauMax + (NormL1(QA.A) + NormL1(QB.A)) * (TauMax * TauMax);
			const double Limit = Sum + Reach + Numerics.ContactTol;
			if (LengthSquared(D.C) > Limit * Limit)
			{
				return {};
			}
		}

		Polynomial F = SquaredNorm(D);
		F.c[0] -= Sum * Sum;

		// Polish on the vector form |dC + dB tau + dA tau^2|^2 - D^2: no cancellation between the monomial terms, so
		// slow approaches far from t_ref keep full precision (ROOT-01). Newton steps stay inside the monotone bracket.
		const auto Polish = [&](double Tau, double Lo, double Hi)
		{
			double X = Tau;
			for (int k = 0; k < 2; ++k)
			{
				const Vec3 P = At(D, X);
				const double Fx = Dot(P, P) - Sum * Sum;
				const double Dx = 2.0 * Dot(P, VelocityOf(D, X));
				if (!(Dx < 0.0))
				{
					break;
				}
				const double Next = X - Fx / Dx;
				if (!(Next >= Lo && Next <= Hi) || Next == X)
				{
					break;
				}
				X = Next;
			}
			return X;
		};
		return FirstCrossing(F, TauMax, RefTime, {2.0 * Sum, true}, Numerics, AlwaysValid{}, Polish);
	}

	Aabb3 SweptBounds(const MotionSegment& Seg, double TauFrom, double TauTo)
	{
		const double Lo = Max(TauFrom, 0.0);
		double Hi = Min(TauTo, Seg.TauEnd);
		if (!(Hi >= Lo))
		{
			Hi = Lo;
		}
		const double P[3] = {Seg.Pos0.x, Seg.Pos0.y, Seg.Pos0.z};
		const double V[3] = {Seg.Vel0.x, Seg.Vel0.y, Seg.Vel0.z};
		const double A[3] = {Seg.Accel2.x, Seg.Accel2.y, Seg.Accel2.z};
		double BoxLo[3];
		double BoxHi[3];
		for (int k = 0; k < 3; ++k)
		{
			const double XLo = P[k] + V[k] * Lo + A[k] * (Lo * Lo);
			double Min3 = XLo;
			double Max3 = XLo;
			if (Hi < kInfinity)
			{
				const double XHi = P[k] + V[k] * Hi + A[k] * (Hi * Hi);
				Min3 = Min(Min3, XHi);
				Max3 = Max(Max3, XHi);
			}
			else if (A[k] > 0.0 || (A[k] == 0.0 && V[k] > 0.0))
			{
				Max3 = kInfinity;
			}
			else if (A[k] < 0.0 || (A[k] == 0.0 && V[k] < 0.0))
			{
				Min3 = -kInfinity;
			}
			if (A[k] != 0.0)
			{
				const double TauStar = -V[k] / (2.0 * A[k]);
				if (TauStar > Lo && TauStar < Hi)
				{
					const double XStar = P[k] + V[k] * TauStar + A[k] * (TauStar * TauStar);
					Min3 = Min(Min3, XStar);
					Max3 = Max(Max3, XStar);
				}
			}
			// 1 pm pad: the bound must contain every evaluation of the piece despite rounding.
			BoxLo[k] = Min3 - 1e-12;
			BoxHi[k] = Max3 + 1e-12;
		}
		return {{BoxLo[0], BoxLo[1], BoxLo[2]}, {BoxHi[0], BoxHi[1], BoxHi[2]}};
	}

	// =============================================================================================
	// Cushion noses and jaws (4.10)
	// =============================================================================================
	ContactPrediction PredictNoseOnCloth(const MotionSegment& Seg, double /*Radius*/, const NoseSegment& Nose, double ContactOffset, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		if (!Nose.Present)
		{
			return {};
		}
		const Vec3 Normal = ToVec3(Nose.InwardNormal);
		const Vec3 Dir = ToVec3(Nose.Direction);
		const VecQuad Q = Relative(Seg, ToVec3(Nose.Start, Seg.Pos0.z));
		Polynomial F = Along(Q, Normal); // sigma(tau): signed distance of the center from the nose line (table side > 0)
		F.c[0] -= ContactOffset;
		const double Slack = Numerics.SegmentParamSlack;
		const double L = Nose.Length;
		const auto Valid = [&](double Tau)
		{
			const double s = Dot(Dir, At(Q, Tau));
			return s >= -Slack && s <= L + Slack;
		};
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {1.0, true}, Numerics, Valid);
	}

	ContactPrediction PredictNoseAirborne(const MotionSegment& Seg, double Radius, const NoseSegment& Nose, double NoseProfileRadius, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		if (!Nose.Present)
		{
			return {};
		}
		// Table side n_c . q >= 0; behind the nose line the rail-top planes are the contact (6.2).
		return EdgeLineContact(Seg, Radius + NoseProfileRadius, ToVec3(Nose.Start, Nose.Height), ToVec3(Nose.Direction), Nose.Length,
			ToVec3(Nose.InwardNormal), TimeLimit, Numerics);
	}

	ContactPrediction PredictJawArcOnCloth(const MotionSegment& Seg, double /*Radius*/, const JawArc& Arc, double ContactOffset, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		const VecQuad Qh = PlanOf(Relative(Seg, ToVec3(Arc.Center, 0.0)));
		Polynomial F = SquaredNorm(Qh);
		const double Reach = Arc.Radius + ContactOffset;
		F.c[0] -= Reach * Reach;
		const double AngleSlack = Numerics.SegmentParamSlack / Max(Arc.Radius, 1e-6);
		const auto Valid = [&](double Tau) { return InAngularRange(XY(At(Qh, Tau)), Arc.AngleFrom, Arc.AngleSweep, AngleSlack); };
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {2.0 * Reach, true}, Numerics, Valid);
	}

	ContactPrediction PredictJawArcAirborne(const MotionSegment& Seg, double Radius, const JawArc& Arc, double TimeLimit, const NumericsConfig& Numerics)
	{
		// EXACT edge contact (4.10 "alternative", degree 8 by the same isolation): the distance from the center to the arc's
		// edge circle (radius r_j at height h about Center) equals R, where the nearest circle point lies on the exposed arc
		// (plan direction Center -> center within [AngleFrom, AngleFrom + AngleSweep]). At each tangent point this surface
		// has the cross-section of the airborne nose / facing top-edge cylinder (radius R about the edge line), so the
		// nose -> jaw -> facing chain is watertight for balls arriving from above. The quartic center-sphere approximation
		// |p - O| = R + r_j is not: at the nose junction it bulges up to r_j beyond the nose cylinder, and a ball crossing
		// the junction plane inside that shell (e.g. falling onto the jaw while drifting along the rail) was hit by neither
		// predictor and flew through the jaw into the cushion.
		const VecQuad Q = Relative(Seg, ToVec3(Arc.Center, Arc.Height));
		const double AngleSlack = Numerics.SegmentParamSlack / Max(Arc.Radius, 1e-6);
		if (!(Arc.Radius > 0.0))
		{
			// Sharp jaw (r_j = 0): the circle is the point O itself, |p - O| = R (quartic, exact).
			Polynomial F = SquaredNorm(Q);
			F.c[0] -= Radius * Radius;
			const auto ValidPoint = [&](double Tau)
			{
				const Vec2 Dir = XY(At(Q, Tau));
				return LengthSquared(Dir) > 0.0 && InAngularRange(Dir, Arc.AngleFrom, Arc.AngleSweep, AngleSlack);
			};
			return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {2.0 * Radius, true}, Numerics, ValidPoint);
		}
		const double Rj2 = Arc.Radius * Arc.Radius;
		const double R2 = Radius * Radius;
		const auto Valid = [&](double Tau)
		{
			const Vec3 W = At(Q, Tau);
			const Vec2 Dir = XY(W);
			const double Rho2 = LengthSquared(Dir);
			// Q > 0: the root of the near factor (distance to the nearest circle point = R), never of the antipodal one.
			return Rho2 > 0.0 && Rho2 + Rj2 + W.z * W.z - R2 > 0.0 && InAngularRange(Dir, Arc.AngleFrom, Arc.AngleSweep, AngleSlack);
		};
		return CircleTubeContact(Seg, Radius, Arc.Center, Arc.Radius, 0.0, Arc.Height, TimeLimit, Numerics, Valid);
	}

	// =============================================================================================
	// Pocket elements (5.3)
	// =============================================================================================
	ContactPrediction PredictFacingOnShelf(const MotionSegment& Seg, double /*Radius*/, const Facing& Face, double ContactOffset, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		const Vec3 Normal = ToVec3(Face.PocketNormal);
		const Vec3 Dir = ToVec3(Face.Direction);
		const VecQuad Q = Relative(Seg, ToVec3(Face.Start, Seg.Pos0.z));
		Polynomial F = Along(Q, Normal); // plan distance of the center from the facing line, pocket side > 0
		F.c[0] -= ContactOffset;
		const double Slack = Numerics.SegmentParamSlack;
		const double L = Face.Length;
		const auto Valid = [&](double Tau)
		{
			const double s = Dot(Dir, At(Q, Tau));
			return s >= -Slack && s <= L + Slack;
		};
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {1.0, true}, Numerics, Valid);
	}

	ContactPrediction PredictFacingAirborne(const MotionSegment& Seg, double Radius, const Facing& Face, double TimeLimit, const NumericsConfig& Numerics)
	{
		// Undercut plane through the plan line at height h, normal = PocketNormal tilted down by beta_v; distance = R.
		const Vec3 Normal = TiltedDown(ToVec3(Face.PocketNormal), Face.Backdraft);
		const Vec3 Dir = ToVec3(Face.Direction);
		const Vec3 P0 = ToVec3(Face.Start, Face.TopHeight);
		const VecQuad Q = Relative(Seg, P0);
		Polynomial F = Along(Q, Normal);
		F.c[0] -= Radius;
		const double Slack = Numerics.SegmentParamSlack;
		const double L = Face.Length;
		const double H = Face.TopHeight;
		const auto Valid = [&](double Tau)
		{
			const Vec3 W = At(Q, Tau);
			const double s = Dot(Dir, W);
			const double ContactZ = W.z + H - Radius * Normal.z; // contact point = center - R n
			return s >= -Slack && s <= L + Slack && ContactZ >= -Slack && ContactZ <= H + Slack;
		};
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {1.0, true}, Numerics, Valid);
	}

	ContactPrediction PredictFacingTopEdge(const MotionSegment& Seg, double Radius, const Facing& Face, double TimeLimit, const NumericsConfig& Numerics)
	{
		// The face's upper edge (plan line at height h), hit from the pocket side like the airborne nose (5.3).
		return EdgeLineContact(Seg, Radius, ToVec3(Face.Start, Face.TopHeight), ToVec3(Face.Direction), Face.Length, ToVec3(Face.PocketNormal),
			TimeLimit, Numerics);
	}

	ContactPrediction PredictDropEdge(const MotionSegment& Seg, double /*Radius*/, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		// Horizontal distance of the center from C_cap reaches a_d = r_p + r_d, entering, on the front arc (pitfall 19).
		const VecQuad Qh = PlanOf(Relative(Seg, ToVec3(Pocket.CaptureCenter, 0.0)));
		Polynomial F = SquaredNorm(Qh);
		const double Ad = Pocket.DropEdgeRadius;
		F.c[0] -= Ad * Ad;
		const double AngleSlack = Numerics.SegmentParamSlack / Max(Ad, 1e-6);
		const auto Valid = [&](double Tau) { return InAngularRange(XY(At(Qh, Tau)), Pocket.FrontArcFrom, Pocket.FrontArcSweep, AngleSlack); };
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {2.0 * Ad, false}, Numerics, Valid);
	}

	ContactPrediction PredictLinerWall(const MotionSegment& Seg, double Radius, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		// Vertical cylinder r_p from inside: (r_p - R)^2 - rho^2 crosses 0 downward (moving outward). The wall exists on the
		// front arc only below the rim (contact z < -r_d) and elsewhere up to WallTopZ (5.3, architecture 8.9).
		const Vec3 C = ToVec3(Pocket.CaptureCenter, 0.0);
		const VecQuad Q = Relative(Seg, C);
		const VecQuad Qh = PlanOf(Q);
		const double Inner = Pocket.CaptureRadius - Radius;
		Polynomial F = Negated(SquaredNorm(Qh));
		F.c[0] += Inner * Inner;
		const double AngleSlack = Numerics.SegmentParamSlack / Max(Pocket.CaptureRadius, 1e-6);
		const double Rd = Pocket.DropRadius;
		const double WallTop = Pocket.WallTopZ;
		const auto Valid = [&](double Tau)
		{
			const Vec3 W = At(Q, Tau);
			const bool Front = InAngularRange(XY(W), Pocket.FrontArcFrom, Pocket.FrontArcSweep, AngleSlack);
			return Front ? W.z < -Rd : W.z <= WallTop;
		};
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {2.0 * Max(Inner, 1e-6), true, false}, Numerics, Valid);
	}

	ContactPrediction PredictRimTorus(const MotionSegment& Seg, double Radius, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		// Torus: major a_d, minor r_d, core at z = -r_d (5.3). The rounding exists as the quarter between the flat shelf
		// (up) and the vertical slate cut (toward the hole), on the front arc: direction core -> center has rho <= a_d
		// and z >= -r_d.
		const double Ad = Pocket.DropEdgeRadius;
		const double Rd = Pocket.DropRadius;
		const Vec3 C = ToVec3(Pocket.CaptureCenter, 0.0);
		const VecQuad Q = Relative(Seg, C);
		const double Slack = Numerics.SegmentParamSlack;
		const double AngleSlack = Slack / Max(Ad, 1e-6);
		const auto Valid = [&](double Tau)
		{
			const Vec3 W = At(Q, Tau);
			const Vec2 H = XY(W);
			return InAngularRange(H, Pocket.FrontArcFrom, Pocket.FrontArcSweep, AngleSlack) && LengthSquared(H) <= Square(Ad + Slack) && W.z >= -Rd - Slack;
		};
		return CircleTubeContact(Seg, Radius, Pocket.CaptureCenter, Ad, Rd, -Rd, TimeLimit, Numerics, Valid);
	}

	ContactPrediction PredictCaptureCircle(const MotionSegment& Seg, const PocketGeometry& Pocket, double TimeLimit, const NumericsConfig& Numerics)
	{
		const VecQuad Qh = PlanOf(Relative(Seg, ToVec3(Pocket.CaptureCenter, 0.0)));
		Polynomial F = SquaredNorm(Qh);
		const double Rp = Pocket.CaptureRadius;
		F.c[0] -= Rp * Rp;
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {2.0 * Rp, false}, Numerics, AlwaysValid{});
	}

	ContactPrediction PredictCaptureDepth(const MotionSegment& Seg, double Radius, double TimeLimit, const NumericsConfig& Numerics)
	{
		Polynomial F; // z(tau) + R
		F.Degree = 2;
		F.c[0] = Seg.Pos0.z + Radius;
		F.c[1] = Seg.Vel0.z;
		F.c[2] = Seg.Accel2.z;
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {1.0, false}, Numerics, AlwaysValid{});
	}

	ContactPrediction PredictPocketExit(const MotionSegment& Seg, double Radius, const PocketGeometry& Pocket, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		// Center back outside the drop-edge circle a_d while above z = R (5.4 "rattled out"): a_d^2 - rho^2 crosses 0 downward.
		const VecQuad Q = Relative(Seg, ToVec3(Pocket.CaptureCenter, 0.0));
		const double Ad = Pocket.DropEdgeRadius;
		Polynomial F = Negated(SquaredNorm(PlanOf(Q)));
		F.c[0] += Ad * Ad;
		const auto Valid = [&](double Tau) { return At(Q, Tau).z > Radius; };
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {2.0 * Ad, false}, Numerics, Valid);
	}

	// =============================================================================================
	// Slate, rail top, leaving the table (C.2, 6.1-6.3)
	// =============================================================================================
	ContactPrediction PredictSlateLanding(const MotionSegment& Seg, double Radius, double TimeLimit)
	{
		if (Seg.State != MotionState::Airborne || !IsFinite(Seg.T0))
		{
			return {};
		}
		// z(tau) = SupportZ + R, the LATER root (C.2) of a tau^2 + b tau + c = 0.
		const double a = Seg.Accel2.z;
		const double b = Seg.Vel0.z;
		const double c = Seg.Pos0.z - (Seg.SupportZ + Radius);
		double Tau = -1.0;
		if (a == 0.0)
		{
			if (b < 0.0)
			{
				Tau = -c / b;
			}
		}
		else
		{
			const double Disc = b * b - 4.0 * a * c;
			if (Disc >= 0.0)
			{
				const double q = -0.5 * (b + SignNonZero(b) * Sqrt(Disc));
				const double R0 = q / a;
				const double R1 = q != 0.0 ? c / q : R0;
				Tau = Max(R0, R1);
			}
		}
		if (!(Tau >= 0.0) || !IsFinite(Tau))
		{
			return {};
		}
		// The end slot owns the landing: TauEnd is the landing of an Airborne segment (rounding may put the root 1 ulp after).
		const double Slack = 1e-12 * Max(1.0, Seg.TauEnd);
		if (Tau > Seg.TauEnd + Slack)
		{
			return {};
		}
		Tau = Min(Tau, Seg.TauEnd);
		if (Seg.T0 + Tau > TimeLimit)
		{
			return {};
		}
		ContactPrediction Out;
		Out.Found = true;
		Out.Time = Seg.T0 + Tau;
		return Out;
	}

	ContactPrediction PredictRailTop(const MotionSegment& Seg, double Radius, const RailTopPolygon& Polygon, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		const Vec3 Normal = Polygon.PlaneNormal;
		const VecQuad Q = Relative(Seg, Polygon.PlanePoint);
		Polynomial F = Along(Q, Normal); // n . (p - x0) - R
		F.c[0] -= Radius;
		const double Slack = Numerics.SegmentParamSlack;
		const auto Valid = [&](double Tau)
		{
			const Vec2 Contact = XY(At(Q, Tau) + Polygon.PlanePoint - Normal * Radius);
			if (!InsidePolygon(Polygon, Contact, Slack))
			{
				return false;
			}
			return !Polygon.HasCut || LengthSquared(Contact - Polygon.CutCenter) >= Square(Max(Polygon.CutRadius - Slack, 0.0));
		};
		return FirstCrossing(F, LocalWindow(Seg, TimeLimit), Seg.T0, {1.0, true}, Numerics, Valid);
	}

	ContactPrediction PredictRailTopEdge(const MotionSegment& Seg, double Radius, const RailTopPolygon& Polygon, int Edge, double TimeLimit,
		const NumericsConfig& Numerics)
	{
		const double Slack = Numerics.SegmentParamSlack;
		if (Edge == kCutRimEdge)
		{
			if (!Polygon.HasCut)
			{
				return {};
			}
			// Circle r_p at the plane height of the cut (a tube of radius 0); the rim is the convex edge between the cap (up)
			// and the hole wall (toward the axis): valid for centers inside the circle in plan and above the rim, with the
			// circle point below the center belonging to this polygon.
			const Vec2 C = Polygon.CutCenter;
			const double Rp = Polygon.CutRadius;
			const double Zc = PlaneHeight(Polygon, C);
			const auto Valid = [&](double Tau)
			{
				const Vec3 P = PositionAt(Seg, Tau);
				const Vec2 H = XY(P) - C;
				const double Rho2 = LengthSquared(H);
				if (Rho2 > Square(Rp + Slack) || P.z < Zc - Slack)
				{
					return false;
				}
				if (Rho2 == 0.0)
				{
					return true; // centered over the hole: the whole rim ring touches
				}
				return InsidePolygon(Polygon, C + H * (Rp / Sqrt(Rho2)), Slack);
			};
			return CircleTubeContact(Seg, Radius, C, Rp, 0.0, Zc, TimeLimit, Numerics, Valid);
		}
		if (Edge < 0 || Edge >= Polygon.VertexCount || !IsPhysicalRailTopEdge(Polygon.Edges[Edge]))
		{
			return {}; // Seam: no physical edge; Nose: the cushion nose line (PredictNoseAirborne)
		}
		const Vec2& V0 = Polygon.Vertices[Edge];
		const Vec2& V1 = Polygon.Vertices[(Edge + 1) % Polygon.VertexCount];
		const Vec3 E0 = ToVec3(V0, PlaneHeight(Polygon, V0));
		const Vec3 E1 = ToVec3(V1, PlaneHeight(Polygon, V1));
		const double L = Length(E1 - E0);
		if (!(L > 0.0))
		{
			return {};
		}
		const Vec3 D = (E1 - E0) / L;
		// Outward in-plane normal of the edge (the polygon is CCW seen from above, its normal points up): beyond this
		// edge's line the edge is this polygon's closest feature.
		const Vec3 Outward = Cross(D, Polygon.PlaneNormal);
		return EdgeLineContact(Seg, Radius, E0, D, L, Outward, TimeLimit, Numerics);
	}

	ContactPrediction PredictSupportExit(const MotionSegment& Seg, const RailTopPolygon& Polygon, double TimeLimit, int& EdgeOut)
	{
		EdgeOut = -1;
		const NumericsConfig& N = kDefaultNumerics;
		const double TauMax = LocalWindow(Seg, TimeLimit);
		ContactPrediction Best;
		const int Count = Polygon.VertexCount;
		for (int i = 0; i < Count; ++i)
		{
			const Vec2& V0 = Polygon.Vertices[i];
			const Vec2 E = Polygon.Vertices[(i + 1) % Count] - V0;
			const double L = Length(E);
			if (!(L > 0.0))
			{
				continue;
			}
			const Vec3 Inward = ToVec3(PerpCcw(E / L)); // CCW polygon: the interior is on the left
			const ContactPrediction P = FirstCrossing(Along(Relative(Seg, ToVec3(V0, Seg.Pos0.z)), Inward), TauMax, Seg.T0, {1.0, false}, N,
				AlwaysValid{});
			if (Earlier(P, Best))
			{
				Best = P;
				EdgeOut = i;
			}
		}
		if (Polygon.HasCut)
		{
			Polynomial F = SquaredNorm(PlanOf(Relative(Seg, ToVec3(Polygon.CutCenter, 0.0))));
			F.c[0] -= Polygon.CutRadius * Polygon.CutRadius;
			const ContactPrediction P = FirstCrossing(F, TauMax, Seg.T0, {2.0 * Polygon.CutRadius, false}, N, AlwaysValid{});
			if (Earlier(P, Best))
			{
				Best = P;
				EdgeOut = kCutRimEdge;
			}
		}
		return Best;
	}

	ContactPrediction PredictOuterBoundary(const MotionSegment& Seg, const Aabb2& Outer, double TimeLimit)
	{
		const NumericsConfig& N = kDefaultNumerics;
		const double TauMax = LocalWindow(Seg, TimeLimit);
		if (!(TauMax >= 0.0) || !IsFinite(Seg.T0))
		{
			return {};
		}
		// Signed distances inside the boundary: Hi.x - x, x - Lo.x, Hi.y - y, y - Lo.y.
		const Vec3 Normals[4] = {{-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}};
		const double Offsets[4] = {Outer.Hi.x, -Outer.Lo.x, Outer.Hi.y, -Outer.Lo.y};
		ContactPrediction Best;
		for (int k = 0; k < 4; ++k)
		{
			Polynomial F = Along({Seg.Pos0, Seg.Vel0, Seg.Accel2}, Normals[k]);
			F.c[0] += Offsets[k];
			if (F.c[0] < -N.ContactTol)
			{
				// Already beyond the boundary: the center has crossed (6.3).
				ContactPrediction Now;
				Now.Found = true;
				Now.Time = Seg.T0;
				Now.Flags = ContactFlags::AtStart;
				return Now;
			}
			const ContactPrediction P = FirstCrossing(F, TauMax, Seg.T0, {1.0, false}, N, AlwaysValid{});
			if (Earlier(P, Best))
			{
				Best = P;
			}
		}
		return Best;
	}

	ContactPrediction PredictLampApex(const MotionSegment& Seg, double Radius, const EnvironmentSpec& Environment, double Gravity, double TimeLimit)
	{
		if (Seg.State != MotionState::Airborne || !(Gravity > 0.0) || !(Seg.Vel0.z > 0.0) || !IsFinite(Seg.T0))
		{
			return {};
		}
		const double TauApex = Seg.Vel0.z / Gravity;
		if (!(TauApex <= LocalWindow(Seg, TimeLimit)) || !IsFinite(TauApex))
		{
			return {};
		}
		const double ZMax = Seg.Pos0.z + Seg.Vel0.z * Seg.Vel0.z / (2.0 * Gravity);
		if (!(ZMax + Radius >= Environment.LampUndersideZ) || !Environment.LampFootprint.Contains(XY(PositionAt(Seg, TauApex))))
		{
			return {};
		}
		ContactPrediction Out;
		Out.Found = true;
		Out.Time = Seg.T0 + TauApex;
		return Out;
	}

	// =============================================================================================
	// Rules observers
	// =============================================================================================
	int PredictLineCrossings(const MotionSegment& Seg, const TableLandmarks& Landmarks, double TimeFrom, double TimeLimit, double Eps, bool IncludeFrom,
		LineCrossing* Out, int Capacity)
	{
		if (!(Seg.T0 + Seg.TauEnd >= TimeFrom) || !(TimeLimit >= TimeFrom) || !(TimeLimit >= Seg.T0))
		{
			return 0;
		}
		struct Candidate
		{
			double Time; // absolute: the order and the window are judged on what the caller receives
			TableLine Line;
			std::int8_t Direction;
		};
		Candidate Found[4 * kTableLineCount];
		int NumFound = 0;

		// Threshold crossings of x(tau) (or y(tau)): +1 when rising through Line + Eps, -1 when falling through Line - Eps.
		const auto AddRoots = [&](double P, double V, double A, double Threshold, TableLine Line, std::int8_t Direction)
		{
			const double c = P - Threshold;
			double Roots[2];
			int NumRoots = 0;
			if (A == 0.0)
			{
				if (V != 0.0)
				{
					Roots[NumRoots++] = -c / V;
				}
			}
			else
			{
				const double Disc = V * V - 4.0 * A * c;
				if (Disc < 0.0)
				{
					return;
				}
				const double q = -0.5 * (V + SignNonZero(V) * Sqrt(Disc));
				if (q == 0.0)
				{
					Roots[NumRoots++] = 0.0; // V = c = 0: at the threshold with zero velocity
				}
				else
				{
					Roots[NumRoots++] = q / A;
					Roots[NumRoots++] = c / q;
				}
			}
			for (int r = 0; r < NumRoots; ++r)
			{
				// The window is judged on the reported absolute time, so (TimeFrom, TimeLimit] is exact for the caller.
				const double Tau = Roots[r];
				const double Time = Seg.T0 + Tau;
				if (!(Tau >= 0.0) || !(Tau <= Seg.TauEnd) || !(Time <= TimeLimit) || !(Time > TimeFrom || (IncludeFrom && Time == TimeFrom)))
				{
					continue;
				}
				const double Slope = V + 2.0 * A * Tau;
				// A zero slope is a tangency (not a crossing), except at tau = 0 where the ball starts ON the threshold and
				// accelerates beyond it.
				const double Beyond = Slope != 0.0 ? Slope : (Tau == 0.0 ? A : 0.0);
				if ((Direction > 0 && Beyond > 0.0) || (Direction < 0 && Beyond < 0.0))
				{
					Found[NumFound++] = {Time, Line, Direction};
				}
			}
		};

		struct LineDef
		{
			TableLine Line;
			bool AlongY;
			double Value;
		};
		const LineDef Lines[kTableLineCount] = {{TableLine::HeadString, false, Landmarks.HeadStringX}, {TableLine::FootString, false, Landmarks.FootStringX},
			{TableLine::CenterString, false, Landmarks.CenterStringX}, {TableLine::LongString, true, Landmarks.LongStringY},
			{TableLine::Baulk, false, Landmarks.BaulkX}};
		for (const LineDef& Def : Lines)
		{
			const double P = Def.AlongY ? Seg.Pos0.y : Seg.Pos0.x;
			const double V = Def.AlongY ? Seg.Vel0.y : Seg.Vel0.x;
			const double A = Def.AlongY ? Seg.Accel2.y : Seg.Accel2.x;
			AddRoots(P, V, A, Def.Value + Eps, Def.Line, 1);
			AddRoots(P, V, A, Def.Value - Eps, Def.Line, -1);
		}

		// Sort by the ABSOLUTE (Time, Line) (insertion sort, stable and deterministic): two crossings whose local times differ
		// by less than half an ulp of T0 + tau report the same Time and are then ordered by Line.
		for (int i = 1; i < NumFound; ++i)
		{
			const Candidate Key = Found[i];
			int j = i - 1;
			while (j >= 0 && (Found[j].Time > Key.Time || (Found[j].Time == Key.Time && Found[j].Line > Key.Line)))
			{
				Found[j + 1] = Found[j];
				--j;
			}
			Found[j + 1] = Key;
		}
		const int Count = Out != nullptr ? (NumFound < Capacity ? NumFound : Capacity) : 0;
		for (int i = 0; i < Count; ++i)
		{
			Out[i].Time = Found[i].Time;
			Out[i].Line = Found[i].Line;
			Out[i].Direction = Found[i].Direction;
		}
		return Count;
	}

	ContactPrediction PredictPlanDistanceCrossing(const MotionSegment& A, double RadiusA, const MotionSegment& B, double RadiusB, bool Entering,
		double TimeLimit, const NumericsConfig& Numerics)
	{
		const double RefTime = Max(A.T0, B.T0);
		const double End = Min(Min(A.T0 + A.TauEnd, B.T0 + B.TauEnd), TimeLimit);
		const double TauMax = End - RefTime;
		if (!(TauMax >= 0.0))
		{
			return {};
		}
		const VecQuad D = PlanOf(Difference(Expand(B, RefTime), Expand(A, RefTime)));
		Polynomial F = SquaredNorm(D);
		const double Sum = RadiusA + RadiusB;
		F.c[0] -= Sum * Sum;
		if (!Entering)
		{
			F = Negated(F);
		}
		return FirstCrossing(F, TauMax, RefTime, {2.0 * Sum, false, false}, Numerics, AlwaysValid{});
	}

	// =============================================================================================
	// Table dispatcher (architecture 8.2)
	// =============================================================================================
	FeaturePrediction PredictTableEvent(const MotionSegment& Seg, const BallSpec& Spec, const BallTableContext& Context, const TableGeometry& Table,
		const EnvironmentSpec& Environment, const DetectOptions& Options, double Gravity, double TimeLimit, const NumericsConfig& Numerics)
	{
		FeaturePrediction Best;
		const MotionState State = Seg.State;
		if (State == MotionState::Stationary || State == MotionState::Spinning || IsTerminal(State))
		{
			return Best; // no translation: nothing can be reached
		}
		const double TauMax = LocalWindow(Seg, TimeLimit);
		if (!(TauMax >= 0.0))
		{
			return Best;
		}
		const double R = Spec.Radius;
		double Limit = TimeLimit; // t_best pruning (3.2); ties are broken by the full (Kind, Index, SubIndex) key below

		const auto Consider = [&](const ContactPrediction& P, TableFeatureKind Kind, int Index, int SubIndex)
		{
			if (!P.Found || P.Time > Limit)
			{
				return;
			}
			const TableFeatureRef Ref{Kind, static_cast<std::uint8_t>(Index), static_cast<std::uint8_t>(SubIndex)};
			if (Best.Contact.Found && (P.Time > Best.Contact.Time || (P.Time == Best.Contact.Time && !KeyLess(Ref, Best.Feature))))
			{
				return;
			}
			Best.Contact = P;
			Best.Feature = Ref;
			Limit = P.Time;
		};

		// Swept bounds of the ball over its window, inflated by its radius (every candidate is culled against them).
		const double Pad = R + Options.NoseProfileRadius + Numerics.ContactTol + Numerics.SegmentParamSlack;
		const Aabb3 Reach = SweptBounds(Seg, 0.0, TauMax).Inflated(Pad);

		const int NumPockets = Table.Pockets.Size();
		const auto FacingsOfPocket = [&](int Pocket, bool Edges)
		{
			for (int Side = 0; Side < 2; ++Side)
			{
				const int Index = 2 * Pocket + Side;
				if (Index >= Table.Facings.Size() || !Reach.Overlaps(FacingBox(Table.Facings[Index], 0.0)))
				{
					continue;
				}
				Consider(PredictFacingAirborne(Seg, R, Table.Facings[Index], Limit, Numerics), TableFeatureKind::FacingFace, Index, 0);
				if (Edges)
				{
					Consider(PredictFacingTopEdge(Seg, R, Table.Facings[Index], Limit, Numerics), TableFeatureKind::FacingTopEdge, Index, 0);
				}
			}
		};
		const auto JawsOfPocket = [&](int Pocket)
		{
			for (int Side = 0; Side < 2; ++Side)
			{
				const int Index = 2 * Pocket + Side;
				if (Index < Table.JawArcs.Size() && Reach.Overlaps(JawBox(Table.JawArcs[Index], 0.0)))
				{
					Consider(PredictJawArcAirborne(Seg, R, Table.JawArcs[Index], Limit, Numerics), TableFeatureKind::JawArc, Index, 0);
				}
			}
		};
		// Rail-top planes, their physical edges and pocket-cut rims whose bounds overlap Reach and Within (6.2).
		const auto RailTopsIn = [&](const Aabb3& Within)
		{
			for (int i = 0; i < Table.RailTops.Size(); ++i)
			{
				const RailTopPolygon& Poly = Table.RailTops[i];
				const Aabb3 Box = PolygonBox(Poly, 0.0);
				if (!Reach.Overlaps(Box) || !Within.Overlaps(Box))
				{
					continue;
				}
				Consider(PredictRailTop(Seg, R, Poly, Limit, Numerics), TableFeatureKind::RailTop, i, 0);
				for (int e = 0; e < Poly.VertexCount; ++e)
				{
					if (IsPhysicalRailTopEdge(Poly.Edges[e]) && Reach.Overlaps(RailTopEdgeBox(Poly, e, 0.0)))
					{
						Consider(PredictRailTopEdge(Seg, R, Poly, e, Limit, Numerics), TableFeatureKind::RailTopEdge, i, e);
					}
				}
				if (Poly.HasCut && Reach.Overlaps(RailTopEdgeBox(Poly, kCutRimEdge, 0.0)))
				{
					Consider(PredictRailTopEdge(Seg, R, Poly, kCutRimEdge, Limit, Numerics), TableFeatureKind::RailTopEdge, i, kCutRimEdge);
				}
			}
		};

		if (IsOnSurface(State))
		{
			if (Context.Support == SupportKind::RailCap)
			{
				// Ball on the flat rail cap (6.2): leaves its polygon, or its center crosses the outer boundary.
				if (Context.SupportPolygon < Table.RailTops.Size())
				{
					int Edge = -1;
					const ContactPrediction P = PredictSupportExit(Seg, Table.RailTops[Context.SupportPolygon], Limit, Edge);
					if (P.Found && Edge >= 0)
					{
						Consider(P, TableFeatureKind::SupportExit, Context.SupportPolygon, Edge);
					}
				}
				Consider(PredictOuterBoundary(Seg, Table.OuterBoundary, Limit), TableFeatureKind::OuterBoundary, 0, 0);
				return Best;
			}

			// Ball on the cloth / shelf: noses (edge at h, R_c), jaw arcs, facing faces (plane, s_f), drop edges or capture circles.
			double LastHeight = -1.0;
			double Offset = 0.0;
			for (int i = 0; i < Table.Noses.Size(); ++i)
			{
				const NoseSegment& Nose = Table.Noses[i];
				if (!Nose.Present || !Reach.Overlaps(NoseBox(Nose, 0.0)))
				{
					continue;
				}
				if (Nose.Height != LastHeight)
				{
					LastHeight = Nose.Height;
					Offset = ComputeCushionContact(R, Nose.Height, Options.NoseProfileRadius, Options.PooltoolCompat).HorizontalOffset;
				}
				Consider(PredictNoseOnCloth(Seg, R, Nose, Offset, Limit, Numerics), TableFeatureKind::NoseSegment, i, 0);
			}
			LastHeight = -1.0;
			for (int i = 0; i < Table.JawArcs.Size(); ++i)
			{
				const JawArc& Arc = Table.JawArcs[i];
				if (!Reach.Overlaps(JawBox(Arc, 0.0)))
				{
					continue;
				}
				if (Arc.Height != LastHeight)
				{
					LastHeight = Arc.Height;
					Offset = ComputeCushionContact(R, Arc.Height, Options.NoseProfileRadius, Options.PooltoolCompat).HorizontalOffset;
				}
				Consider(PredictJawArcOnCloth(Seg, R, Arc, Offset, Limit, Numerics), TableFeatureKind::JawArc, i, 0);
			}
			for (int i = 0; i < Table.Facings.Size(); ++i)
			{
				const Facing& Face = Table.Facings[i];
				if (!Reach.Overlaps(FacingBox(Face, 0.0)))
				{
					continue;
				}
				const double Sf = FacingContactOffset(R, Face.TopHeight, Face.Backdraft);
				Consider(PredictFacingOnShelf(Seg, R, Face, Sf, Limit, Numerics), TableFeatureKind::FacingFace, i, 0);
			}
			for (int p = 0; p < NumPockets; ++p)
			{
				const PocketGeometry& Pocket = Table.Pockets[p];
				if (!Reach.Overlaps(PocketBox(Pocket, 0.0)))
				{
					continue;
				}
				if (Options.Pockets == PocketModel::CaptureCircle)
				{
					Consider(PredictCaptureCircle(Seg, Pocket, Limit, Numerics), TableFeatureKind::CaptureCircle, p, 0);
				}
				else
				{
					Consider(PredictDropEdge(Seg, R, Pocket, Limit, Numerics), TableFeatureKind::DropEdge, p, 0);
				}
			}
			return Best;
		}

		if (State == MotionState::Airborne)
		{
			for (int i = 0; i < Table.Noses.Size(); ++i)
			{
				const NoseSegment& Nose = Table.Noses[i];
				if (Nose.Present && Reach.Overlaps(NoseBox(Nose, 0.0)))
				{
					Consider(PredictNoseAirborne(Seg, R, Nose, Options.NoseProfileRadius, Limit, Numerics), TableFeatureKind::NoseSegment, i, 0);
				}
			}
			for (int i = 0; i < Table.JawArcs.Size(); ++i)
			{
				if (Reach.Overlaps(JawBox(Table.JawArcs[i], 0.0)))
				{
					Consider(PredictJawArcAirborne(Seg, R, Table.JawArcs[i], Limit, Numerics), TableFeatureKind::JawArc, i, 0);
				}
			}
			for (int i = 0; i < Table.Facings.Size(); ++i)
			{
				const Facing& Face = Table.Facings[i];
				if (Reach.Overlaps(FacingBox(Face, 0.0)))
				{
					Consider(PredictFacingAirborne(Seg, R, Face, Limit, Numerics), TableFeatureKind::FacingFace, i, 0);
					Consider(PredictFacingTopEdge(Seg, R, Face, Limit, Numerics), TableFeatureKind::FacingTopEdge, i, 0);
				}
			}
			if (Options.Pockets == PocketModel::GeometricLevelA)
			{
				// Every pocket whose a_d cylinder the swept bounds reach: liner / back wall and rim torus (architecture 8.2).
				for (int p = 0; p < NumPockets; ++p)
				{
					const PocketGeometry& Pocket = Table.Pockets[p];
					if (Reach.Overlaps(PocketBox(Pocket, 0.0)))
					{
						Consider(PredictLinerWall(Seg, R, Pocket, Limit, Numerics), TableFeatureKind::LinerWall, p, 0);
						Consider(PredictRimTorus(Seg, R, Pocket, Limit, Numerics), TableFeatureKind::RimTorus, p, 0);
					}
				}
			}
			RailTopsIn(Reach);
			Consider(PredictOuterBoundary(Seg, Table.OuterBoundary, Limit), TableFeatureKind::OuterBoundary, 0, 0);
			Consider(PredictLampApex(Seg, R, Environment, Gravity, Limit), TableFeatureKind::LampApex, 0, 0);
			return Best;
		}

		const int Pocket = static_cast<int>(Context.Pocket);
		if (Context.Pocket == PocketId::None || Pocket >= NumPockets)
		{
			return Best;
		}
		if (State == MotionState::PocketPivot)
		{
			// On the pivot's detection proxy: facings (face + top edge) and jaw arcs of its pocket (5.4); balls are pair slots.
			JawsOfPocket(Pocket);
			FacingsOfPocket(Pocket, true);
			return Best;
		}
		if (State == MotionState::PocketFall)
		{
			const PocketGeometry& P = Table.Pockets[Pocket];
			JawsOfPocket(Pocket);
			FacingsOfPocket(Pocket, true);
			Consider(PredictLinerWall(Seg, R, P, Limit, Numerics), TableFeatureKind::LinerWall, Pocket, 0);
			Consider(PredictRimTorus(Seg, R, P, Limit, Numerics), TableFeatureKind::RimTorus, Pocket, 0);
			Consider(PredictCaptureDepth(Seg, R, Limit, Numerics), TableFeatureKind::CaptureDepth, Pocket, 0);
			Consider(PredictPocketExit(Seg, R, P, Limit, Numerics), TableFeatureKind::PocketExit, Pocket, 0);
			// A ball bouncing up inside the hole (rim torus, jaw) above WallTopZ is no longer stopped by the back wall: it meets
			// the rim of the rail cut and the cap around it. Until PocketExit (center beyond a_d, z > R) hands it to the airborne
			// dispatcher, only rail-top features within the a_d cylinder (plus the ball's reach) can be touched.
			RailTopsIn(PocketBox(P, Pad));
		}
		return Best;
	}

	int QueryTableFeatures(const Aabb3& Region, const TableGeometry& Table, const DetectOptions& Options, TableFeatureRef* Out, int Capacity,
		bool& Overflow)
	{
		Overflow = false;
		int Count = 0;
		const auto Emit = [&](TableFeatureKind Kind, int Index, int SubIndex)
		{
			if (Count < Capacity && Out != nullptr)
			{
				Out[Count++] = {Kind, static_cast<std::uint8_t>(Index), static_cast<std::uint8_t>(SubIndex)};
			}
			else
			{
				Overflow = true;
			}
		};
		const double Pad = Options.NoseProfileRadius;
		for (int i = 0; i < Table.Noses.Size(); ++i)
		{
			if (Table.Noses[i].Present && Region.Overlaps(NoseBox(Table.Noses[i], Pad)))
			{
				Emit(TableFeatureKind::NoseSegment, i, 0);
			}
		}
		for (int i = 0; i < Table.JawArcs.Size(); ++i)
		{
			if (Region.Overlaps(JawBox(Table.JawArcs[i], 0.0)))
			{
				Emit(TableFeatureKind::JawArc, i, 0);
			}
		}
		for (int i = 0; i < Table.Facings.Size(); ++i)
		{
			if (Region.Overlaps(FacingBox(Table.Facings[i], 0.0)))
			{
				Emit(TableFeatureKind::FacingFace, i, 0);
			}
		}
		for (int i = 0; i < Table.Facings.Size(); ++i)
		{
			const Facing& Face = Table.Facings[i];
			if (Region.Overlaps(SegmentBox(Face.Start, Face.End, Face.TopHeight, Face.TopHeight, 0.0)))
			{
				Emit(TableFeatureKind::FacingTopEdge, i, 0);
			}
		}
		if (Options.Pockets == PocketModel::GeometricLevelA)
		{
			for (int p = 0; p < Table.Pockets.Size(); ++p)
			{
				const PocketGeometry& Pocket = Table.Pockets[p];
				const double Ad = Pocket.DropEdgeRadius;
				const Aabb3 Box{{Pocket.CaptureCenter.x - Ad, Pocket.CaptureCenter.y - Ad, 0.0}, {Pocket.CaptureCenter.x + Ad, Pocket.CaptureCenter.y + Ad, 0.0}};
				if (Region.Overlaps(Box))
				{
					Emit(TableFeatureKind::DropEdge, p, 0);
				}
			}
		}
		for (int i = 0; i < Table.RailTops.Size(); ++i)
		{
			if (Region.Overlaps(PolygonBox(Table.RailTops[i], 0.0)))
			{
				Emit(TableFeatureKind::RailTop, i, 0);
			}
		}
		for (int i = 0; i < Table.RailTops.Size(); ++i)
		{
			const RailTopPolygon& Poly = Table.RailTops[i];
			for (int e = 0; e < Poly.VertexCount; ++e)
			{
				if (IsPhysicalRailTopEdge(Poly.Edges[e]) && Region.Overlaps(RailTopEdgeBox(Poly, e, 0.0)))
				{
					Emit(TableFeatureKind::RailTopEdge, i, e);
				}
			}
			if (Poly.HasCut && Region.Overlaps(RailTopEdgeBox(Poly, kCutRimEdge, 0.0)))
			{
				Emit(TableFeatureKind::RailTopEdge, i, kCutRimEdge);
			}
		}
		if (Options.Pockets == PocketModel::CaptureCircle)
		{
			for (int p = 0; p < Table.Pockets.Size(); ++p)
			{
				const PocketGeometry& Pocket = Table.Pockets[p];
				const double Rp = Pocket.CaptureRadius;
				const Aabb3 Box{{Pocket.CaptureCenter.x - Rp, Pocket.CaptureCenter.y - Rp, 0.0}, {Pocket.CaptureCenter.x + Rp, Pocket.CaptureCenter.y + Rp, 0.0}};
				if (Region.Overlaps(Box))
				{
					Emit(TableFeatureKind::CaptureCircle, p, 0);
				}
			}
		}
		return Count;
	}

	FixedContact MakeFixedContact(const TableFeatureRef& Feature, const TableGeometry& Table, const BallState& Ball, const BallSpec& Spec,
		const DetectOptions& Options)
	{
		FixedContact Out;
		Out.Normal = Vec3::UnitZ();
		const double R = Spec.Radius;
		const Vec3& P = Ball.Position;
		const bool OnSurface = IsOnSurface(Ball.State);
		const int Index = Feature.Index;

		// Horizontal unit of -Normal (the into-feature direction of a GRI contact); zero for a vertical normal.
		const auto IntoFrom = [](const Vec3& Normal) { return Normalized(Planar(-Normal)); };

		switch (Feature.Kind)
		{
		case TableFeatureKind::NoseSegment:
		{
			if (Index >= Table.Noses.Size())
			{
				break;
			}
			const NoseSegment& Nose = Table.Noses[Index];
			const Vec3 Inward = ToVec3(Nose.InwardNormal);
			Out.Kind = FixedContactKind::NoseEdge;
			Out.RailFeature = static_cast<std::uint8_t>(RailFeatureOfCushion(static_cast<CushionId>(Index)));
			Out.IntoFeature = -Inward;
			if (OnSurface)
			{
				const CushionContactGeometry G = ComputeCushionContact(R, Nose.Height, Options.NoseProfileRadius, Options.PooltoolCompat);
				Out.BallOnCloth = true;
				Out.Elevation = G.Theta;
				Out.Normal = TiltedDown(Inward, G.Theta);
			}
			else
			{
				const Vec3 D = ToVec3(Nose.Direction);
				const Vec3 W = P - ToVec3(Nose.Start, Nose.Height);
				Out.BallOnCloth = false;
				Out.Normal = Normalized(W - D * Dot(D, W));
				Out.Elevation = ElevationOf(Out.Normal);
			}
			break;
		}
		case TableFeatureKind::JawArc:
		{
			if (Index >= Table.JawArcs.Size())
			{
				break;
			}
			const JawArc& Arc = Table.JawArcs[Index];
			Out.Kind = FixedContactKind::JawArcEdge;
			Out.RailFeature = static_cast<std::uint8_t>(RailFeatureOfJaw(Arc.Pocket, Arc.Side));
			Out.Pocket = Arc.Pocket;
			Out.IntoFeature = Normalized(ToVec3(Arc.Center) - Planar(P)); // from the ball center toward the arc center (4.1)
			if (OnSurface)
			{
				const CushionContactGeometry G = ComputeCushionContact(R, Arc.Height, Options.NoseProfileRadius, Options.PooltoolCompat);
				Out.BallOnCloth = true;
				Out.Elevation = G.Theta;
				Out.Normal = TiltedDown(-Out.IntoFeature, G.Theta);
			}
			else
			{
				// From the nearest point of the edge circle (r_j at h), as detected; the center itself for a sharp jaw or a
				// center straight above the circle's center.
				Out.BallOnCloth = false;
				const Vec2 H = XY(P) - Arc.Center;
				const double Rho = Length(H);
				const Vec2 Rim = Rho > 0.0 ? Arc.Center + H * (Arc.Radius / Rho) : Arc.Center;
				Out.Normal = Normalized(P - ToVec3(Rim, Arc.Height));
				Out.Elevation = ElevationOf(Out.Normal);
			}
			break;
		}
		case TableFeatureKind::FacingFace:
		{
			if (Index >= Table.Facings.Size())
			{
				break;
			}
			const Facing& Face = Table.Facings[Index];
			Out.Kind = FixedContactKind::FacingFace;
			Out.RailFeature = static_cast<std::uint8_t>(RailFeatureOfJaw(Face.Pocket, Face.Side));
			Out.Pocket = Face.Pocket;
			Out.BallOnCloth = OnSurface; // on the shelf: Mathavan with theta = beta_v (5.3); airborne: GRI on the plane
			Out.IntoFeature = -ToVec3(Face.PocketNormal);
			Out.Elevation = Face.Backdraft;
			Out.Normal = TiltedDown(ToVec3(Face.PocketNormal), Face.Backdraft);
			break;
		}
		case TableFeatureKind::FacingTopEdge:
		{
			if (Index >= Table.Facings.Size())
			{
				break;
			}
			const Facing& Face = Table.Facings[Index];
			const Vec3 D = ToVec3(Face.Direction);
			const Vec3 W = P - ToVec3(Face.Start, Face.TopHeight);
			Out.Kind = FixedContactKind::FacingTopEdge;
			Out.RailFeature = static_cast<std::uint8_t>(RailFeatureOfJaw(Face.Pocket, Face.Side));
			Out.Pocket = Face.Pocket;
			Out.BallOnCloth = false;
			Out.IntoFeature = -ToVec3(Face.PocketNormal);
			Out.Normal = Normalized(W - D * Dot(D, W));
			Out.Elevation = ElevationOf(Out.Normal);
			break;
		}
		case TableFeatureKind::LinerWall:
		{
			if (Index >= Table.Pockets.Size())
			{
				break;
			}
			const PocketGeometry& Pocket = Table.Pockets[Index];
			const Vec3 Outward = Normalized(Planar(P) - ToVec3(Pocket.CaptureCenter));
			Out.Kind = FixedContactKind::Liner;
			Out.Pocket = Pocket.Id;
			Out.BallOnCloth = false;
			Out.IntoFeature = Outward;
			Out.Elevation = Pocket.LinerUndercut;
			Out.Normal = TiltedDown(-Outward, Pocket.LinerUndercut); // undercut wall deflects the ball down (5.3, WPA 10)
			break;
		}
		case TableFeatureKind::RimTorus:
		{
			if (Index >= Table.Pockets.Size())
			{
				break;
			}
			const PocketGeometry& Pocket = Table.Pockets[Index];
			const Vec3 Outward = Normalized(Planar(P) - ToVec3(Pocket.CaptureCenter));
			const Vec3 Core = ToVec3(Pocket.CaptureCenter, -Pocket.DropRadius) + Outward * Pocket.DropEdgeRadius;
			Out.Kind = FixedContactKind::RimTorus;
			Out.Pocket = Pocket.Id;
			Out.BallOnCloth = false;
			Out.Normal = Normalized(P - Core);
			Out.IntoFeature = IntoFrom(Out.Normal);
			Out.Elevation = ElevationOf(Out.Normal);
			break;
		}
		case TableFeatureKind::RailTop:
		{
			if (Index >= Table.RailTops.Size())
			{
				break;
			}
			const RailTopPolygon& Poly = Table.RailTops[Index];
			Out.Kind = FixedContactKind::RailTop;
			Out.Pocket = Poly.Pocket;
			Out.BallOnCloth = false;
			Out.Normal = Poly.PlaneNormal;
			Out.IntoFeature = IntoFrom(Out.Normal);
			Out.Elevation = ElevationOf(Out.Normal);
			break;
		}
		case TableFeatureKind::RailTopEdge:
		{
			if (Index >= Table.RailTops.Size())
			{
				break;
			}
			const RailTopPolygon& Poly = Table.RailTops[Index];
			Out.Kind = FixedContactKind::RailTopEdge;
			Out.Pocket = Poly.Pocket;
			Out.BallOnCloth = false;
			if (Feature.SubIndex == kCutRimEdge)
			{
				const Vec2 H = XY(P) - Poly.CutCenter;
				const double Rho = Length(H);
				const Vec2 Dir = Rho > 0.0 ? H / Rho : Vec2{};
				const Vec3 Rim = ToVec3(Poly.CutCenter + Dir * Poly.CutRadius, PlaneHeight(Poly, Poly.CutCenter));
				Out.Normal = Normalized(P - Rim);
			}
			else if (Feature.SubIndex < Poly.VertexCount)
			{
				const Vec2& V0 = Poly.Vertices[Feature.SubIndex];
				const Vec2& V1 = Poly.Vertices[(Feature.SubIndex + 1) % Poly.VertexCount];
				const Vec3 E0 = ToVec3(V0, PlaneHeight(Poly, V0));
				const Vec3 D = Normalized(ToVec3(V1, PlaneHeight(Poly, V1)) - E0);
				const Vec3 W = P - E0;
				Out.Normal = Normalized(W - D * Dot(D, W));
			}
			Out.IntoFeature = IntoFrom(Out.Normal);
			Out.Elevation = ElevationOf(Out.Normal);
			break;
		}
		case TableFeatureKind::SlateLanding:
			Out.Kind = FixedContactKind::Slate; // k_hat = z_hat (G-3 parity with motion C.3)
			Out.BallOnCloth = false;
			break;
		default:
			// DropEdge, CaptureDepth, PocketExit, CaptureCircle, SupportExit, OuterBoundary, LampApex: region events routed by
			// the simulator (pocket state machine, rail-top routing, off table), not contacts. Normal = z_hat.
			Out.BallOnCloth = OnSurface;
			if ((Feature.Kind == TableFeatureKind::DropEdge || Feature.Kind == TableFeatureKind::CaptureDepth || Feature.Kind == TableFeatureKind::PocketExit ||
					Feature.Kind == TableFeatureKind::CaptureCircle) &&
				Index < Table.Pockets.Size())
			{
				Out.Pocket = Table.Pockets[Index].Id;
			}
			break;
		}
		return Out;
	}
}
