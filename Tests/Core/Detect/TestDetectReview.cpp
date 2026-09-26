// Owner: WP-5 (event detection). Adversarial review tests (no spec IDs): every predictor against brute-force sampling of
// its exact geometric gap, swap / mirror symmetry, non-finite inputs, bitwise determinism and no heap allocation in the
// hot paths. Regression tests of the review fixes:
//  * Detect_Review_PocketFallAboveTheWallMeetsTheCutRim: a ball bouncing up inside a pocket above WallTopZ passed through
//    the rim of the rail cut (PocketFall predicted no rail-top feature);
//  * Detect_Review_LineCrossingTiesAreOrderedByTheReturnedTime: equal returned times were ordered by the local time, not
//    by (Time, Line) as the contract says;
//  * Detect_Review_NonFiniteInputsNeverYieldNonFiniteEvents: a non-finite segment time base (T0 = NaN / +-inf) produced
//    Found events with a NaN / infinite Time (nose, facing, pocket, rail-top, landing predictors), which would corrupt
//    the ordering of the event heap;
//  * Detect_JawArcAirborneIsTheExactEdgeCircle / Detect_AirborneNoseToJawJunctionIsWatertight (TestDetectCushion.cpp):
//    the airborne jaw's center-sphere approximation left a gap at the nose junction.

#include "rbtest.h"

#include "DetectTestUtil.h"

#include "rb/Core/Random.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
	#include <crtdbg.h>
	#define RB_DETECT_ALLOC_HOOK 1
#else
	#define RB_DETECT_ALLOC_HOOK 0
#endif

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;

namespace
{
#ifdef NDEBUG
	constexpr int kCasesPerPredictor = 2000;
#else
	constexpr int kCasesPerPredictor = 250;
#endif

	bool SameBits(double A, double B) { return std::memcmp(&A, &B, sizeof(double)) == 0; }

	Vec3 RandomDirection(rb::Rng& Rng)
	{
		for (;;)
		{
			const Vec3 V{Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-1.0, 1.0)};
			const double L = rb::Length(V);
			if (L > 0.1 && L <= 1.0)
			{
				return V / L;
			}
		}
	}

	Vec3 RandomPlanDirection(rb::Rng& Rng)
	{
		const double A = Rng.NextUniform(0.0, rb::kTwoPi);
		return {std::cos(A), std::sin(A), 0.0};
	}

	Vec3 Toward(const Vec3& From, const Vec3& To)
	{
		const Vec3 D = To - From;
		return D / rb::Length(D);
	}

	bool OnArc(const Vec2& Dir, double From, double Sweep)
	{
		double Delta = std::atan2(Dir.y, Dir.x) - From;
		Delta -= rb::kTwoPi * std::floor(Delta / rb::kTwoPi);
		return Delta <= Sweep;
	}

	// A surface segment on the cloth: rolling, sliding with random spin, or a curving tilt piece (Accel2 not parallel to Vel0).
	MotionSegment ClothSegment(rb::Rng& Rng, const Vec3& P, const Vec3& Dir)
	{
		const std::uint32_t Kind = Rng.NextBelow(3);
		const double Speed = Rng.NextUniform(0.01, 3.0);
		if (Kind == 1)
		{
			const Vec3 W{Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-150.0, 150.0), 0.0};
			if (rb::Length(rb::Planar(rb::SlipVelocity(Dir * Speed, W, kR))) > 1e-6)
			{
				return Sliding(P, Dir * Speed, W);
			}
		}
		MotionSegment S = Rolling(P, Dir * Speed);
		if (Kind == 2)
		{
			S.Accel2 = S.Accel2 + RandomPlanDirection(Rng) * Rng.NextUniform(0.0, 0.05);
			S.TauEnd = rb::Min(S.TauEnd, 3.0);
			S.Tilt.Active = true;
		}
		return S;
	}

	// A ballistic segment aimed along Aim with a random extra vertical speed; PocketFall (no landing) with a fixed window.
	MotionSegment FlightSegment(rb::Rng& Rng, const Vec3& P, const Vec3& Aim)
	{
		MotionSegment S = PocketFall(P, Aim * Rng.NextUniform(0.1, 4.0) + Vec3{0.0, 0.0, Rng.NextUniform(-0.5, 2.0)});
		S.TauEnd = 0.4;
		return S;
	}

	// First VALID downward crossing (Gap > 0 -> Gap <= 0) of the exact gap on [0, TauMax]: N uniform samples, bisection
	// of each sign change down to adjacent doubles, the first crossing whose position is valid (as the predictors do).
	template <class GapFn, class ValidFn>
	double SampledFirstCrossing(const GapFn& Gap, const ValidFn& Valid, double TauMax, int N)
	{
		double Prev = Gap(0.0);
		double PrevTau = 0.0;
		for (int i = 1; i <= N; ++i)
		{
			const double Tau = TauMax * static_cast<double>(i) / static_cast<double>(N);
			const double G = Gap(Tau);
			if (Prev > 0.0 && G <= 0.0)
			{
				double Lo = PrevTau;
				double Hi = Tau;
				for (int k = 0; k < 200; ++k)
				{
					const double Mid = 0.5 * (Lo + Hi);
					if (Mid <= Lo || Mid >= Hi)
					{
						break;
					}
					(Gap(Mid) <= 0.0 ? Hi : Lo) = Mid;
				}
				if (Valid(Hi))
				{
					return Hi;
				}
			}
			Prev = G;
			PrevTau = Tau;
		}
		return rb::kInfinity;
	}

	struct Agreement
	{
		const char* Name = "";
		int Cases = 0;
		int Hits = 0;
		int Grazes = 0;     // hit / miss decided by a dip of at most 0.1 um (tangency band of the predictors: 1 nm)
		int Mismatches = 0;
		double MaxError = 0.0; // [s]
	};

	// One case: the predictor against the sampled reference (times to 1e-9 s). Starts within 1 um of the boundary are
	// skipped (the start rules have their own tests). A disagreement is re-sampled 100x denser first (a short dip between
	// samples); what remains must be a graze, else it is a mismatch.
	template <class GapFn, class ValidFn>
	void CompareWithSampling(Agreement& A, const ContactPrediction& P, double T0, const GapFn& Gap, const ValidFn& Valid, double TauMax)
	{
		if (!(Gap(0.0) >= 1e-6))
		{
			return;
		}
		++A.Cases;
		double Ref = SampledFirstCrossing(Gap, Valid, TauMax, 4000);
		const double Local = P.Found ? P.Time - T0 : rb::kInfinity;
		const auto Agrees = [&](double R) { return (R < rb::kInfinity) == P.Found && (!P.Found || rb::Abs(R - Local) <= 1e-9); };
		if (!Agrees(Ref))
		{
			Ref = SampledFirstCrossing(Gap, Valid, TauMax, 400000);
		}
		if (Agrees(Ref))
		{
			if (P.Found)
			{
				++A.Hits;
				A.MaxError = rb::Max(A.MaxError, rb::Abs(Ref - Local));
			}
			return;
		}
		// Graze: the gap's minimum around the disputed time stays within 0.1 um of zero.
		const double At = P.Found && Local < Ref ? Local : Ref;
		double Minimum = rb::kInfinity;
		for (int i = -2000; i <= 2000; ++i)
		{
			const double Tau = At + 2e-3 * static_cast<double>(i) / 2000.0;
			if (Tau >= 0.0 && Tau <= TauMax)
			{
				Minimum = rb::Min(Minimum, Gap(Tau));
			}
		}
		if (rb::Abs(Minimum) <= 1e-7)
		{
			++A.Grazes;
			return;
		}
		++A.Mismatches;
		std::printf("  [%s] predictor %d at %.12g, sampled %.12g (minimum gap nearby %.3g)\n", A.Name, P.Found ? 1 : 0, Local, Ref, Minimum);
	}

	void CheckAgreement(const Agreement& A)
	{
		RB_CHECK(A.Mismatches == 0);
		RB_CHECK(A.Cases >= kCasesPerPredictor / 2);
		RB_CHECK(A.Hits >= A.Cases / 20);
		RB_CHECK(A.Grazes * 100 <= A.Cases);
		RB_CHECK(A.MaxError <= 1e-9);
		if (A.Mismatches != 0)
		{
			std::printf("  %s: %d cases, %d hits, %d grazes, %d mismatches\n", A.Name, A.Cases, A.Hits, A.Grazes, A.Mismatches);
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Hand-built features shared by the tests below
	// ---------------------------------------------------------------------------------------------
	constexpr double kRailTopZ = 0.048;

	// RAIL_LEFT nose piece, y = +0.635 for x in [-0.3, 0.3], inward normal -y.
	rb::NoseSegment NosePiece() { return MakeNose({0.3, 0.635}, {-0.3, 0.635}, {0.0, -1.0}); }

	rb::RailTopPolygon Quad(const Vec3& Point, const Vec3& Normal, const Vec2 (&V)[4], const rb::RailEdgeKind (&E)[4])
	{
		rb::RailTopPolygon P;
		P.PlanePoint = Point;
		P.PlaneNormal = rb::Normalized(Normal);
		for (int i = 0; i < 4; ++i)
		{
			P.Vertices[P.VertexCount++] = V[i];
			P.Edges[i] = E[i];
		}
		return P;
	}

	// Sloped cushion top behind NosePiece (h at the nose line up to RailTopZ at the cushion back 50.8 mm behind).
	rb::RailTopPolygon SlopedTop()
	{
		const Vec2 V[4] = {{-0.3, 0.635}, {0.3, 0.635}, {0.3, 0.6858}, {-0.3, 0.6858}};
		const rb::RailEdgeKind E[4] = {rb::RailEdgeKind::Nose, rb::RailEdgeKind::Seam, rb::RailEdgeKind::CushionBack, rb::RailEdgeKind::Seam};
		return Quad({0.0, 0.635, kNoseH}, {0.0, -(kRailTopZ - kNoseH) / 0.0508, 1.0}, V, E);
	}

	// Flat cap behind it with a fictional cut disc.
	rb::RailTopPolygon CapWithCut()
	{
		const Vec2 V[4] = {{-0.3, 0.6858}, {0.3, 0.6858}, {0.3, 0.8128}, {-0.3, 0.8128}};
		const rb::RailEdgeKind E[4] = {rb::RailEdgeKind::CushionBack, rb::RailEdgeKind::Seam, rb::RailEdgeKind::OuterEdge, rb::RailEdgeKind::Seam};
		rb::RailTopPolygon P = Quad({0.0, 0.0, kRailTopZ}, {0.0, 0.0, 1.0}, V, E);
		P.HasCut = true;
		P.CutCenter = {0.1, 0.75};
		P.CutRadius = 0.05;
		return P;
	}

	// Cap square around the 9FT_PRO FOOT_LEFT corner pocket, minus its cut (the pocket surround).
	rb::RailTopPolygon CornerSurround()
	{
		const Vec2 V[4] = {{1.25, 0.62}, {1.40, 0.62}, {1.40, 0.77}, {1.25, 0.77}};
		const rb::RailEdgeKind E[4] = {rb::RailEdgeKind::Seam, rb::RailEdgeKind::OuterEdge, rb::RailEdgeKind::OuterEdge, rb::RailEdgeKind::Seam};
		rb::RailTopPolygon P = Quad({0.0, 0.0, kRailTopZ}, {0.0, 0.0, 1.0}, V, E);
		P.HasCut = true;
		P.CutCenter = CornerPocket().CaptureCenter;
		P.CutRadius = CornerPocket().CaptureRadius;
		P.Pocket = rb::PocketId::FootLeft;
		return P;
	}

	// The pockets (FOOT_LEFT real, the others far away), the FOOT_LEFT surround and a large outer boundary.
	rb::TableGeometry PocketWithSurround()
	{
		rb::TableGeometry T;
		for (int i = 0; i < rb::kPocketCount; ++i)
		{
			rb::PocketGeometry P = CornerPocket();
			P.Id = static_cast<rb::PocketId>(i);
			if (P.Id != rb::PocketId::FootLeft)
			{
				P.CaptureCenter = {-10.0 - i, -10.0};
			}
			T.Pockets.PushBack(P);
		}
		T.RailTops.PushBack(CornerSurround());
		T.OuterBoundary = {{-5.0, -5.0}, {5.0, 5.0}};
		return T;
	}

	bool InsideConvex(const rb::RailTopPolygon& Poly, const Vec2& X)
	{
		for (int i = 0; i < Poly.VertexCount; ++i)
		{
			const Vec2 A = Poly.Vertices[i];
			const Vec2 B = Poly.Vertices[(i + 1) % Poly.VertexCount];
			if (rb::Cross(B - A, X - A) < -1e-9 * rb::Length(B - A))
			{
				return false;
			}
		}
		return true;
	}
}

// -------------------------------------------------------------------------------------------------
// Regression tests of the review fixes
// -------------------------------------------------------------------------------------------------

RB_TEST(Detect_Review_PocketFallAboveTheWallMeetsTheCutRim)
{
	// A ball bounced up inside the FOOT_LEFT hole (e.g. off the rim torus) flies toward the back of the pocket above
	// WallTopZ: the back wall ends there, so the rim of the rail cut is the next contact. Before the fix PocketFall
	// predicted no rail-top feature: the ball passed through the rim and the cap until PocketExit at a_d.
	const rb::TableGeometry T = PocketWithSurround();
	const rb::PocketGeometry& P = T.Pockets[static_cast<int>(rb::PocketId::FootLeft)];
	const Vec3 Back{std::sqrt(0.5), std::sqrt(0.5), 0.0}; // pocket axis, away from the table
	const MotionSegment Up = PocketFall(rb::ToVec3(P.CaptureCenter, 0.03), Back * 1.0 + Vec3{0.0, 0.0, 0.8});
	rb::BallTableContext InPocket;
	InPocket.Pocket = rb::PocketId::FootLeft;
	const rb::FeaturePrediction D = rb::PredictTableEvent(Up, rb::BallSpec{}, InPocket, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
	RB_REQUIRE(D.Contact.Found);
	RB_CHECK(D.Feature.Kind == rb::TableFeatureKind::RailTopEdge);
	RB_CHECK(D.Feature.Index == 0);
	RB_CHECK(D.Feature.SubIndex == rb::kCutRimEdge);
	const ContactPrediction Rim = rb::PredictRailTopEdge(Up, kR, T.RailTops[0], rb::kCutRimEdge, rb::kInfinity, Numerics());
	RB_REQUIRE(Rim.Found);
	RB_CHECK(D.Contact.Time == Rim.Time);
	const Vec3 C = rb::PositionAt(Up, Rim.Time);
	RB_CHECK(C.z > P.WallTopZ); // above the back wall: the liner cannot hold it
	RB_CHECK_NEAR(std::sqrt(rb::Square(P.CaptureRadius - rb::Length(rb::XY(C) - P.CaptureCenter)) + rb::Square(C.z - kRailTopZ)), kR, 1e-12);
	const ContactPrediction Exit = rb::PredictPocketExit(Up, kR, P, rb::kInfinity, Numerics());
	RB_CHECK(Exit.Found && Exit.Time > Rim.Time);
	RB_CHECK(!rb::PredictLinerWall(Up, kR, P, rb::kInfinity, Numerics()).Found);

	// Below the wall top the back wall still comes first (unchanged).
	const MotionSegment Low = PocketFall(rb::ToVec3(P.CaptureCenter, 0.0), Back * 1.0);
	const rb::FeaturePrediction L = rb::PredictTableEvent(Low, rb::BallSpec{}, InPocket, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
	RB_REQUIRE(L.Contact.Found);
	RB_CHECK(L.Feature.Kind == rb::TableFeatureKind::LinerWall);

	// Rail-top polygons away from the ball's pocket are never candidates in PocketFall (the ball leaves a_d first).
	rb::TableGeometry Far = T;
	rb::RailTopPolygon Other = CornerSurround();
	for (int i = 0; i < Other.VertexCount; ++i)
	{
		Other.Vertices[i] = Other.Vertices[i] + Vec2{0.5, 0.0};
	}
	Other.CutCenter = Other.CutCenter + Vec2{0.5, 0.0};
	Far.RailTops[0] = Other;
	const rb::FeaturePrediction F = rb::PredictTableEvent(Up, rb::BallSpec{}, InPocket, Far, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
	RB_REQUIRE(F.Contact.Found);
	RB_CHECK(F.Feature.Kind == rb::TableFeatureKind::PocketExit);
}

RB_TEST(Detect_Review_LineCrossingTiesAreOrderedByTheReturnedTime)
{
	// Late in a shot (T0 = 100 s, ulp 1.4e-14 s) the baulk line lies 2e-15 m before the head string: the local crossing
	// times differ, the returned absolute times are equal, so the order is by Line (HeadString before Baulk).
	MotionSegment S = Rolling({0.0, 0.3, kR}, {1.0, 0.0, 0.0}, kMuR, kG, 100.0);
	S.Accel2 = {};
	S.TauEnd = 2.0;
	rb::TableLandmarks L;
	L.HeadStringX = 0.25;
	L.BaulkX = 0.25 - 2e-15;
	L.FootStringX = 5.0;
	L.CenterStringX = 6.0;
	L.LongStringY = 5.0;
	rb::LineCrossing Out[8];
	const int N = rb::PredictLineCrossings(S, L, S.T0, rb::kInfinity, 1e-6, false, Out, 8);
	RB_REQUIRE(N == 2);
	RB_REQUIRE(SameBits(Out[0].Time, Out[1].Time));
	RB_CHECK(Out[0].Line == rb::TableLine::HeadString);
	RB_CHECK(Out[1].Line == rb::TableLine::Baulk);
	// Capacity 1 keeps the first by (Time, Line).
	RB_REQUIRE(rb::PredictLineCrossings(S, L, S.T0, rb::kInfinity, 1e-6, false, Out, 1) == 1);
	RB_CHECK(Out[0].Line == rb::TableLine::HeadString);
}

RB_TEST(Detect_Review_TorusGrazeToleranceDoesNotGrowWithTheStartDistance)
{
	// A ball falling into the FOOT_LEFT hole skims the rounded rim (degree 8) 20 nm deep, 20x the tangency band eps_f
	// (1 nm): a contact. The segment starts 0.5 m away and 0.25 m up. Before the fix the degree-8 band was scaled by the
	// START value of the second factor (d_far^2 - (R + r_d)^2 = 0.37 m^2 there instead of 4 a_d rho = 0.012 m^2 at the
	// contact): grazes up to ~32 nm deep were dropped as tangencies.
	const rb::PocketGeometry P = CornerPocket();
	const double Reach = kR + P.DropRadius;
	const double Phi = 0.25 * rb::kPi; // tangent point on the valid quarter, 45 deg up from the inner horizontal
	const Vec2 E{-std::sqrt(0.5), -std::sqrt(0.5)}; // front-arc direction from C_cap (toward the table)
	const double RhoT = P.DropEdgeRadius - Reach * std::cos(Phi);
	const double ZT = -P.DropRadius + Reach * std::sin(Phi);
	const double Depth = 20e-9;
	// Path through the tangent point shifted Depth toward the core circle, moving inward-down along the tube at 3 m/s
	// (|v|^2 > Reach g sin(phi): the parabola curves away from the tube, so its distance to the core has a single minimum).
	const double Rho1 = RhoT + Depth * std::cos(Phi);
	const double Z1 = ZT - Depth * std::sin(Phi);
	const double VRho = -3.0 * std::sin(Phi);
	const double VZ = -3.0 * std::cos(Phi);
	const double Back = 0.216; // [s] before the tangent instant: 0.46 m out and 0.23 m up
	const Vec3 At1 = rb::ToVec3(P.CaptureCenter + E * Rho1, Z1);
	const Vec3 V1 = rb::ToVec3(E * VRho, VZ);
	const Vec3 A{0.0, 0.0, -0.5 * kG};
	MotionSegment S = PocketFall(At1 - V1 * Back + A * (Back * Back), V1 - A * (2.0 * Back));
	S.TauEnd = 0.3;
	RB_REQUIRE(rb::Length(rb::XY(S.Pos0) - P.CaptureCenter) > 0.4);
	const auto Gap = [&](double Tau)
	{
		const Vec3 X = rb::PositionAt(S, Tau);
		return std::sqrt(rb::Square(rb::Length(rb::XY(X) - P.CaptureCenter) - P.DropEdgeRadius) + rb::Square(X.z + P.DropRadius)) - Reach;
	};
	RB_CHECK_NEAR(Gap(Back), -Depth, 1e-12);
	const ContactPrediction C = rb::PredictRimTorus(S, kR, P, rb::kInfinity, Numerics());
	RB_REQUIRE(C.Found);
	RB_CHECK(C.Flags == 0);
	RB_CHECK(C.Time < Back && C.Time > Back - 1e-4);
	RB_CHECK_NEAR(Gap(C.Time), 0.0, 1e-12);
	// The same path 0.5 nm off (inside the band) is a tangency: no contact.
	MotionSegment Touch = S;
	Touch.Pos0 = Touch.Pos0 + rb::ToVec3(E * (-(Depth - 0.5e-9) * std::cos(Phi)), (Depth - 0.5e-9) * std::sin(Phi));
	RB_CHECK_NEAR(rb::PositionAt(Touch, Back).z - ZT, 0.5e-9 * -std::sin(Phi), 1e-15);
	RB_CHECK(!rb::PredictRimTorus(Touch, kR, P, rb::kInfinity, Numerics()).Found);
}

// -------------------------------------------------------------------------------------------------
// Every predictor against brute-force sampling of its exact geometric gap
// -------------------------------------------------------------------------------------------------

RB_TEST(Detect_Review_ClothPredictorsAgreeWithBruteForceSampling)
{
	rb::Rng Rng(0x5A11ull);
	const double Rc = NoseContactOffset();
	const rb::NoseSegment Nose = NosePiece();
	Agreement AN{"NoseOnCloth"}, AJ{"JawArcOnCloth"}, AD{"DropEdge"}, AF{"FacingOnShelf"};
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec3 P{Rng.NextUniform(-0.6, 0.6), Rng.NextUniform(0.2, 0.635 - Rc - 1e-5), kR};
		const MotionSegment S = ClothSegment(Rng, P, rb::Planar(Toward(P, {Rng.NextUniform(-0.4, 0.4), 0.635, kR})));
		const auto Gap = [&](double Tau) { return (0.635 - rb::PositionAt(S, Tau).y) - Rc; };
		const auto Valid = [&](double Tau) { return rb::Abs(rb::PositionAt(S, Tau).x) <= 0.3 + 1e-9; };
		CompareWithSampling(AN, rb::PredictNoseOnCloth(S, kR, Nose, Rc, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const double From = Rng.NextUniform(-rb::kPi, rb::kPi);
		const double Sweep = Rng.NextUniform(0.2, 3.5);
		const rb::JawArc Jaw = MakeJaw({0.0, 0.6}, Rng.NextBelow(5) == 0 ? 0.0 : 0.004, From, Sweep);
		const Vec3 O = rb::ToVec3(Jaw.Center, kR);
		const Vec3 P = O + RandomPlanDirection(Rng) * Rng.NextUniform(0.04, 0.3);
		const MotionSegment S = ClothSegment(Rng, P, rb::Planar(Toward(P, O + RandomPlanDirection(Rng) * Rng.NextUniform(0.0, 0.04))));
		const auto Gap = [&](double Tau) { return rb::Length(rb::XY(rb::PositionAt(S, Tau)) - Jaw.Center) - (Jaw.Radius + Rc); };
		const auto Valid = [&](double Tau) { return OnArc(rb::XY(rb::PositionAt(S, Tau)) - Jaw.Center, From - 1e-7, Sweep + 2e-7); };
		CompareWithSampling(AJ, rb::PredictJawArcOnCloth(S, kR, Jaw, Rc, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	const rb::PocketGeometry Pocket = CornerPocket();
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec3 C = rb::ToVec3(Pocket.CaptureCenter, kR);
		const Vec3 P = C + RandomPlanDirection(Rng) * Rng.NextUniform(Pocket.DropEdgeRadius + 1e-4, 0.4);
		const MotionSegment S = ClothSegment(Rng, P, rb::Planar(Toward(P, C + RandomPlanDirection(Rng) * Rng.NextUniform(0.0, 0.1))));
		const auto Gap = [&](double Tau) { return rb::Length(rb::XY(rb::PositionAt(S, Tau)) - Pocket.CaptureCenter) - Pocket.DropEdgeRadius; };
		const auto Valid = [&](double Tau)
		{ return OnArc(rb::XY(rb::PositionAt(S, Tau)) - Pocket.CaptureCenter, Pocket.FrontArcFrom - 1e-9, Pocket.FrontArcSweep + 2e-9); };
		CompareWithSampling(AD, rb::PredictDropEdge(S, kR, Pocket, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	const rb::Facing Face = MakeFacing({0.0, 0.7}, {0.1, 0.7}, {0.0, -1.0});
	const double Sf = (kR - (kNoseH - kR) * std::sin(Face.Backdraft)) / std::cos(Face.Backdraft);
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec3 P{Rng.NextUniform(-0.3, 0.4), Rng.NextUniform(0.4, 0.7 - Sf - 1e-5), kR};
		const MotionSegment S = ClothSegment(Rng, P, rb::Planar(Toward(P, {Rng.NextUniform(-0.05, 0.15), 0.7, kR})));
		const auto Gap = [&](double Tau) { return (0.7 - rb::PositionAt(S, Tau).y) - Sf; };
		const auto Valid = [&](double Tau)
		{
			const double X = rb::PositionAt(S, Tau).x;
			return X >= -1e-9 && X <= 0.1 + 1e-9;
		};
		CompareWithSampling(AF, rb::PredictFacingOnShelf(S, kR, Face, Sf, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	CheckAgreement(AN);
	CheckAgreement(AJ);
	CheckAgreement(AD);
	CheckAgreement(AF);
}

RB_TEST(Detect_Review_FlightPredictorsAgreeWithBruteForceSampling)
{
	rb::Rng Rng(0xF11Eull);
	Agreement AN{"NoseAirborne"}, AJ{"JawArcAirborne"}, AF{"FacingAirborne"}, AE{"FacingTopEdge"};
	const rb::NoseSegment Nose = NosePiece();
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec3 P{Rng.NextUniform(-0.5, 0.5), Rng.NextUniform(0.3, 0.66), Rng.NextUniform(kR, kR + 0.15)};
		const MotionSegment S = FlightSegment(Rng, P, Toward(P, {Rng.NextUniform(-0.4, 0.4), 0.635, kNoseH}));
		const auto Gap = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return std::sqrt(rb::Square(X.y - 0.635) + rb::Square(X.z - kNoseH)) - kR;
		};
		const auto Valid = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return rb::Abs(X.x) <= 0.3 + 1e-9 && X.y <= 0.635; // table side of the nose line
		};
		CompareWithSampling(AN, rb::PredictNoseAirborne(S, kR, Nose, 0.0, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		// Exact edge circle (radius r_j at h, incl. sharp and large jaws): distance from the center to the circle = R.
		const double From = Rng.NextUniform(-rb::kPi, rb::kPi);
		const double Sweep = Rng.NextUniform(0.2, 3.5);
		const double Rj = Rng.NextBelow(5) == 0 ? 0.0 : Rng.NextUniform(0.001, 0.02);
		const rb::JawArc Jaw = MakeJaw({0.0, 0.6}, Rj, From, Sweep);
		const Vec3 O = rb::ToVec3(Jaw.Center, kNoseH);
		const Vec3 P = O + RandomDirection(Rng) * Rng.NextUniform(0.04, 0.2);
		const MotionSegment S = FlightSegment(Rng, P, Toward(P, O + RandomDirection(Rng) * Rng.NextUniform(0.0, 0.03)));
		const auto Gap = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return std::sqrt(rb::Square(rb::Length(rb::XY(X) - Jaw.Center) - Rj) + rb::Square(X.z - kNoseH)) - kR;
		};
		const auto Valid = [&](double Tau) { return OnArc(rb::XY(rb::PositionAt(S, Tau)) - Jaw.Center, From - 1e-6, Sweep + 2e-6); };
		CompareWithSampling(AJ, rb::PredictJawArcAirborne(S, kR, Jaw, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	const rb::Facing Face = MakeFacing({0.0, 0.7}, {0.1, 0.7}, {0.0, -1.0});
	const Vec3 N{0.0, -std::cos(Face.Backdraft), -std::sin(Face.Backdraft)};
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec3 P{Rng.NextUniform(-0.2, 0.3), Rng.NextUniform(0.5, 0.69), Rng.NextUniform(0.0, 0.12)};
		const MotionSegment S = FlightSegment(Rng, P, Toward(P, {Rng.NextUniform(-0.02, 0.12), 0.7, Rng.NextUniform(0.0, kNoseH + 0.02)}));
		const auto Gap = [&](double Tau) { return rb::Dot(N, rb::PositionAt(S, Tau) - Vec3{0.0, 0.7, kNoseH}) - kR; };
		const auto Valid = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			const double ContactZ = X.z - kR * N.z;
			return X.x >= -1e-9 && X.x <= 0.1 + 1e-9 && ContactZ >= -1e-9 && ContactZ <= kNoseH + 1e-9;
		};
		CompareWithSampling(AF, rb::PredictFacingAirborne(S, kR, Face, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
		const auto EdgeGap = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return std::sqrt(rb::Square(X.y - 0.7) + rb::Square(X.z - kNoseH)) - kR;
		};
		const auto EdgeValid = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return X.x >= -1e-9 && X.x <= 0.1 + 1e-9 && X.y <= 0.7;
		};
		CompareWithSampling(AE, rb::PredictFacingTopEdge(S, kR, Face, rb::kInfinity, Numerics()), S.T0, EdgeGap, EdgeValid, S.TauEnd);
	}
	CheckAgreement(AN);
	CheckAgreement(AJ);
	CheckAgreement(AF);
	CheckAgreement(AE);
}

RB_TEST(Detect_Review_PocketPredictorsAgreeWithBruteForceSampling)
{
	rb::Rng Rng(0xB0C4ull);
	const rb::PocketGeometry P = CornerPocket();
	const Vec2 C = P.CaptureCenter;
	Agreement AL{"LinerWall"}, AT{"RimTorus"}, AC{"CaptureDepth"}, AX{"PocketExit"};
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec2 H = C + rb::XY(RandomPlanDirection(Rng)) * Rng.NextUniform(0.0, P.CaptureRadius - kR - 1e-4);
		const MotionSegment S = FlightSegment(Rng, {H.x, H.y, Rng.NextUniform(-0.08, 0.06)}, RandomDirection(Rng));
		const auto Gap = [&](double Tau) { return (P.CaptureRadius - kR) - rb::Length(rb::XY(rb::PositionAt(S, Tau)) - C); };
		const auto Valid = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return OnArc(rb::XY(X) - C, P.FrontArcFrom - 1e-9, P.FrontArcSweep + 2e-9) ? X.z < -P.DropRadius : X.z <= P.WallTopZ;
		};
		CompareWithSampling(AL, rb::PredictLinerWall(S, kR, P, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
		CompareWithSampling(AC, rb::PredictCaptureDepth(S, kR, rb::kInfinity, Numerics()), S.T0, [&](double Tau) { return rb::PositionAt(S, Tau).z + kR; },
			[](double) { return true; }, S.TauEnd);
	}
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		// Rim torus (degree 8): the existing quarter only (front arc, center at rho <= a_d and z >= -r_d).
		const double Angle = P.FrontArcFrom + Rng.NextUniform(-0.2, P.FrontArcSweep + 0.2);
		const Vec2 H = C + Vec2{std::cos(Angle), std::sin(Angle)} * Rng.NextUniform(0.0, P.DropEdgeRadius + 0.01);
		const MotionSegment S = FlightSegment(Rng, {H.x, H.y, Rng.NextUniform(-0.06, 0.06)}, RandomDirection(Rng));
		const auto Gap = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return std::sqrt(rb::Square(rb::Length(rb::XY(X) - C) - P.DropEdgeRadius) + rb::Square(X.z + P.DropRadius)) - (kR + P.DropRadius);
		};
		const auto Valid = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return OnArc(rb::XY(X) - C, P.FrontArcFrom - 1e-8, P.FrontArcSweep + 2e-8) && rb::Length(rb::XY(X) - C) <= P.DropEdgeRadius + 1e-9 &&
				X.z >= -P.DropRadius - 1e-9;
		};
		CompareWithSampling(AT, rb::PredictRimTorus(S, kR, P, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec2 H = C + rb::XY(RandomPlanDirection(Rng)) * Rng.NextUniform(0.0, P.DropEdgeRadius - 1e-4);
		const MotionSegment S = FlightSegment(Rng, {H.x, H.y, Rng.NextUniform(-0.05, 0.1)}, RandomDirection(Rng));
		const auto Gap = [&](double Tau) { return P.DropEdgeRadius - rb::Length(rb::XY(rb::PositionAt(S, Tau)) - C); };
		const auto Valid = [&](double Tau) { return rb::PositionAt(S, Tau).z > kR; };
		CompareWithSampling(AX, rb::PredictPocketExit(S, kR, P, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	CheckAgreement(AL);
	CheckAgreement(AT);
	CheckAgreement(AC);
	CheckAgreement(AX);
}

RB_TEST(Detect_Review_RailTopAndObserverPredictorsAgreeWithBruteForceSampling)
{
	rb::Rng Rng(0x7A11ull);
	const rb::RailTopPolygon Top = SlopedTop();
	const rb::RailTopPolygon Cap = CapWithCut();
	Agreement AS{"SlopedTopPlane"}, AP{"CapPlane"}, AB{"CushionBackEdge"}, AO{"OuterEdge"}, AR{"CutRim"}, AI{"PlanEnter"}, AL{"PlanLeave"};
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		const Vec3 P{Rng.NextUniform(-0.4, 0.4), Rng.NextUniform(0.55, 0.9), Rng.NextUniform(kR, 0.2)};
		const MotionSegment S = FlightSegment(Rng, P, Toward(P, {Rng.NextUniform(-0.35, 0.35), Rng.NextUniform(0.62, 0.83), Rng.NextUniform(kNoseH, kRailTopZ)}));
		CompareWithSampling(
			AS, rb::PredictRailTop(S, kR, Top, rb::kInfinity, Numerics()), S.T0, [&](double Tau) { return rb::Dot(Top.PlaneNormal, rb::PositionAt(S, Tau) - Top.PlanePoint) - kR; },
			[&](double Tau) { return InsideConvex(Top, rb::XY(rb::PositionAt(S, Tau) - Top.PlaneNormal * kR)); }, S.TauEnd);
		CompareWithSampling(
			AP, rb::PredictRailTop(S, kR, Cap, rb::kInfinity, Numerics()), S.T0, [&](double Tau) { return rb::PositionAt(S, Tau).z - kRailTopZ - kR; },
			[&](double Tau)
			{
				const Vec2 X = rb::XY(rb::PositionAt(S, Tau));
				return InsideConvex(Cap, X) && rb::Length(X - Cap.CutCenter) >= Cap.CutRadius - 1e-9;
			},
			S.TauEnd);
		// Cushion back edge of the sloped top (edge 2), met from beyond it in the plane.
		const Vec3 E0{0.3, 0.6858, kRailTopZ};
		const Vec3 D{-1.0, 0.0, 0.0};
		const Vec3 Out = rb::Cross(D, Top.PlaneNormal);
		CompareWithSampling(
			AB, rb::PredictRailTopEdge(S, kR, Top, 2, rb::kInfinity, Numerics()), S.T0,
			[&](double Tau)
			{
				const Vec3 W = rb::PositionAt(S, Tau) - E0;
				return rb::Length(W - D * rb::Dot(D, W)) - kR;
			},
			[&](double Tau)
			{
				const Vec3 W = rb::PositionAt(S, Tau) - E0;
				return rb::Dot(D, W) >= -1e-9 && rb::Dot(D, W) <= 0.6 + 1e-9 && rb::Dot(Out, W) >= 0.0;
			},
			S.TauEnd);
		CompareWithSampling(
			AO, rb::PredictRailTopEdge(S, kR, Cap, 2, rb::kInfinity, Numerics()), S.T0,
			[&](double Tau)
			{
				const Vec3 X = rb::PositionAt(S, Tau);
				return std::sqrt(rb::Square(X.y - 0.8128) + rb::Square(X.z - kRailTopZ)) - kR;
			},
			[&](double Tau)
			{
				const Vec3 X = rb::PositionAt(S, Tau);
				return rb::Abs(X.x) <= 0.3 + 1e-9 && X.y >= 0.8128;
			},
			S.TauEnd);
	}
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		// The cut rim (degree 8): circle r_p at the cap height, for centers over the hole whose rim point is on the cap.
		const Vec3 C0 = rb::ToVec3(Cap.CutCenter, kRailTopZ);
		const Vec3 P = C0 + RandomDirection(Rng) * Rng.NextUniform(0.03, 0.15);
		const MotionSegment S = FlightSegment(Rng, P, Toward(P, C0 + RandomDirection(Rng) * Rng.NextUniform(0.0, 0.06)));
		const auto Gap = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			return std::sqrt(rb::Square(rb::Length(rb::XY(X) - Cap.CutCenter) - Cap.CutRadius) + rb::Square(X.z - kRailTopZ)) - kR;
		};
		const auto Valid = [&](double Tau)
		{
			const Vec3 X = rb::PositionAt(S, Tau);
			const Vec2 H = rb::XY(X) - Cap.CutCenter;
			const double Rho = rb::Length(H);
			return Rho <= Cap.CutRadius + 1e-9 && X.z >= kRailTopZ - 1e-9 && (Rho == 0.0 || InsideConvex(Cap, Cap.CutCenter + H * (Cap.CutRadius / Rho)));
		};
		CompareWithSampling(AR, rb::PredictRailTopEdge(S, kR, Cap, rb::kCutRimEdge, rb::kInfinity, Numerics()), S.T0, Gap, Valid, S.TauEnd);
	}
	for (int i = 0; i < kCasesPerPredictor; ++i)
	{
		// Jump-over observer: plan distance of a flying ball and a resting / rolling one.
		MotionSegment A = Airborne({Rng.NextUniform(-0.3, 0.3), Rng.NextUniform(-0.3, 0.3), kR}, RandomPlanDirection(Rng) * Rng.NextUniform(0.1, 3.0) + Vec3{0.0, 0.0, Rng.NextUniform(0.1, 2.0)});
		const Vec3 Q = rb::PositionAt(A, Rng.NextUniform(0.0, A.TauEnd)) + RandomPlanDirection(Rng) * Rng.NextUniform(0.0, 0.08);
		const MotionSegment B = Rng.NextBelow(2) == 0 ? Stationary({Q.x, Q.y, kR}) : ClothSegment(Rng, {Q.x, Q.y, kR}, RandomPlanDirection(Rng));
		const double TauMax = rb::Min(A.TauEnd, B.TauEnd);
		const auto Plan = [&](double Tau) { return rb::Length(rb::XY(rb::PositionAt(B, Tau) - rb::PositionAt(A, Tau))) - 2.0 * kR; };
		CompareWithSampling(AI, rb::PredictPlanDistanceCrossing(A, kR, B, kR, true, rb::kInfinity, Numerics()), A.T0, Plan, [](double) { return true; }, TauMax);
		CompareWithSampling(AL, rb::PredictPlanDistanceCrossing(A, kR, B, kR, false, rb::kInfinity, Numerics()), A.T0, [&](double Tau) { return -Plan(Tau); },
			[](double) { return true; }, TauMax);
	}
	CheckAgreement(AS);
	CheckAgreement(AP);
	CheckAgreement(AB);
	CheckAgreement(AO);
	CheckAgreement(AR);
	CheckAgreement(AI);
	RB_CHECK(AL.Mismatches == 0 && AL.Cases > 0); // starts inside the plan overlap are rarer
}

// -------------------------------------------------------------------------------------------------
// Symmetry, non-finite inputs, determinism, allocation
// -------------------------------------------------------------------------------------------------

RB_TEST(Detect_Review_SwapAndMirrorSymmetryIsBitwise)
{
	// Swapping the pair and mirroring y -> -y (features mirrored with it) must give bit-identical predictions.
	rb::Rng Rng(0x5E77ull);
	const auto Mirror = [](MotionSegment S)
	{
		S.Pos0.y = -S.Pos0.y;
		S.Vel0.y = -S.Vel0.y;
		S.Accel2.y = -S.Accel2.y;
		return S;
	};
	const auto Same = [](const ContactPrediction& A, const ContactPrediction& B) { return A.Found == B.Found && A.Flags == B.Flags && SameBits(A.Time, B.Time); };
	int Broken = 0;
	int Hits = 0;
	const double Rc = NoseContactOffset();
	for (int i = 0; i < 4 * kCasesPerPredictor; ++i)
	{
		const Vec3 PA{Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-0.5, 0.5), kR};
		const MotionSegment A = ClothSegment(Rng, PA, RandomPlanDirection(Rng));
		const Vec3 PB = rb::PositionAt(A, Rng.NextUniform(0.0, rb::Min(A.TauEnd, 2.0))) + RandomPlanDirection(Rng) * Rng.NextUniform(2.0 * kR + 1e-6, 0.1);
		const MotionSegment B = Rng.NextBelow(2) == 0 ? Stationary(PB) : ClothSegment(Rng, PB, RandomPlanDirection(Rng));
		const ContactPrediction P = rb::PredictBallBall(A, kR, B, kR, rb::kInfinity, Numerics());
		Hits += P.Found ? 1 : 0;
		Broken += Same(P, rb::PredictBallBall(B, kR, A, kR, rb::kInfinity, Numerics())) ? 0 : 1;
		Broken += Same(P, rb::PredictBallBall(Mirror(A), kR, Mirror(B), kR, rb::kInfinity, Numerics())) ? 0 : 1;

		// Nose, jaw and drop edge mirrored about y = 0.
		const rb::NoseSegment N = MakeNose({1.0, 0.635}, {-1.0, 0.635}, {0.0, -1.0});
		const rb::NoseSegment NM = MakeNose({1.0, -0.635}, {-1.0, -0.635}, {0.0, 1.0});
		Broken += Same(rb::PredictNoseOnCloth(A, kR, N, Rc, rb::kInfinity, Numerics()), rb::PredictNoseOnCloth(Mirror(A), kR, NM, Rc, rb::kInfinity, Numerics())) ? 0 : 1;
		const rb::JawArc J = MakeJaw({PB.x, PB.y}, 0.004, -2.5, 2.0);
		const rb::JawArc JM = MakeJaw({PB.x, -PB.y}, 0.004, 0.5, 2.0);
		Broken += Same(rb::PredictJawArcOnCloth(A, kR, J, Rc, rb::kInfinity, Numerics()), rb::PredictJawArcOnCloth(Mirror(A), kR, JM, Rc, rb::kInfinity, Numerics())) ? 0 : 1;
		rb::PocketGeometry Pk = CornerPocket();
		Pk.CaptureCenter = {PB.x, PB.y};
		rb::PocketGeometry PkM = Pk;
		PkM.CaptureCenter.y = -PkM.CaptureCenter.y;
		PkM.FrontArcFrom = -(Pk.FrontArcFrom + Pk.FrontArcSweep);
		Broken += Same(rb::PredictDropEdge(A, kR, Pk, rb::kInfinity, Numerics()), rb::PredictDropEdge(Mirror(A), kR, PkM, rb::kInfinity, Numerics())) ? 0 : 1;
	}
	RB_CHECK(Broken == 0);
	RB_CHECK(Hits > kCasesPerPredictor / 4);
}

RB_TEST(Detect_Review_NonFiniteInputsNeverYieldNonFiniteEvents)
{
	// NaN / Inf / huge values in any segment field or limit: no crash, and a reported event always has a finite time.
	const double Bad[] = {std::nan(""), rb::kInfinity, -rb::kInfinity, 1e300, -1e300, 1e-300};
	const rb::PocketGeometry P = CornerPocket();
	const rb::NoseSegment Nose = NosePiece();
	const rb::JawArc Jaw = MakeJaw({0.0, 0.6}, 0.004, -rb::kPi, rb::kPi);
	const rb::Facing Face = MakeFacing({0.0, 0.7}, {0.1, 0.7}, {0.0, -1.0});
	const rb::RailTopPolygon Cap = CapWithCut();
	const rb::RailTopPolygon Top = SlopedTop();
	const rb::TableGeometry T = PocketWithSurround();
	rb::TableLandmarks L;
	L.HeadStringX = -0.635;
	L.FootStringX = 0.635;
	int Weird = 0;
	int Calls = 0;
	const auto Check = [&](const ContactPrediction& C)
	{
		++Calls;
		Weird += C.Found && !rb::IsFinite(C.Time) ? 1 : 0;
	};
	for (const double X : Bad)
	{
		for (int Field = 0; Field < 11; ++Field)
		{
			MotionSegment S = Airborne({0.1, 0.6, 0.05}, {1.0, 0.5, 0.3});
			double* Fields[10] = {&S.Pos0.x, &S.Pos0.y, &S.Pos0.z, &S.Vel0.x, &S.Vel0.y, &S.Vel0.z, &S.Accel2.x, &S.Accel2.z, &S.TauEnd, &S.T0};
			double Limit = rb::kInfinity;
			if (Field < 10)
			{
				*Fields[Field] = X;
			}
			else
			{
				Limit = X;
			}
			MotionSegment OnCloth = S;
			OnCloth.State = rb::MotionState::Rolling;
			const MotionSegment B = Stationary({0.2, 0.6, kR});
			Check(rb::PredictBallBall(S, kR, B, kR, Limit, Numerics()));
			Check(rb::PredictBallBall(B, kR, S, kR, Limit, Numerics()));
			Check(rb::PredictNoseOnCloth(OnCloth, kR, Nose, NoseContactOffset(), Limit, Numerics()));
			Check(rb::PredictNoseAirborne(S, kR, Nose, 0.0, Limit, Numerics()));
			Check(rb::PredictJawArcOnCloth(OnCloth, kR, Jaw, NoseContactOffset(), Limit, Numerics()));
			Check(rb::PredictJawArcAirborne(S, kR, Jaw, Limit, Numerics()));
			Check(rb::PredictFacingOnShelf(OnCloth, kR, Face, 0.0275, Limit, Numerics()));
			Check(rb::PredictFacingAirborne(S, kR, Face, Limit, Numerics()));
			Check(rb::PredictFacingTopEdge(S, kR, Face, Limit, Numerics()));
			Check(rb::PredictDropEdge(OnCloth, kR, P, Limit, Numerics()));
			Check(rb::PredictLinerWall(S, kR, P, Limit, Numerics()));
			Check(rb::PredictRimTorus(S, kR, P, Limit, Numerics()));
			Check(rb::PredictCaptureCircle(OnCloth, P, Limit, Numerics()));
			Check(rb::PredictCaptureDepth(S, kR, Limit, Numerics()));
			Check(rb::PredictPocketExit(S, kR, P, Limit, Numerics()));
			Check(rb::PredictSlateLanding(S, kR, Limit));
			Check(rb::PredictRailTop(S, kR, Top, Limit, Numerics()));
			Check(rb::PredictRailTopEdge(S, kR, Cap, 2, Limit, Numerics()));
			Check(rb::PredictRailTopEdge(S, kR, Cap, rb::kCutRimEdge, Limit, Numerics()));
			int Edge = -1;
			Check(rb::PredictSupportExit(OnCloth, Cap, Limit, Edge));
			Check(rb::PredictOuterBoundary(S, {{-1.5, -0.8}, {1.5, 0.8}}, Limit));
			rb::EnvironmentSpec Lamp;
			Lamp.LampUndersideZ = 0.1;
			Check(rb::PredictLampApex(S, kR, Lamp, kG, Limit));
			Check(rb::PredictPlanDistanceCrossing(S, kR, B, kR, true, Limit, Numerics()));
			Check(rb::PredictPlanDistanceCrossing(S, kR, B, kR, false, Limit, Numerics()));
			for (const rb::MotionState State : {rb::MotionState::Airborne, rb::MotionState::Rolling, rb::MotionState::PocketFall})
			{
				MotionSegment D = S;
				D.State = State;
				rb::BallTableContext Context;
				Context.Pocket = rb::PocketId::FootLeft;
				Check(rb::PredictTableEvent(D, rb::BallSpec{}, Context, T, Lamp, rb::DetectOptions{}, kG, Limit, Numerics()).Contact);
			}
			rb::LineCrossing Out[16];
			const int N = rb::PredictLineCrossings(OnCloth, L, 0.0, Limit, 1e-6, true, Out, 16);
			for (int k = 0; k < N; ++k)
			{
				Weird += rb::IsFinite(Out[k].Time) ? 0 : 1;
			}
			const rb::Aabb3 Box = rb::SweptBounds(S, 0.0, Limit);
			Weird += Box.Lo.x > Box.Hi.x || Box.Lo.y > Box.Hi.y || Box.Lo.z > Box.Hi.z ? 1 : 0; // never inverted (NaN compares false)
		}
	}
	RB_CHECK(Weird == 0);
	RB_CHECK(Calls > 1000);

	// Degenerate polynomials: zero, constant, NaN and infinite coefficients never produce roots outside the interval.
	double Roots[rb::Polynomial::kMaxDegree];
	rb::Polynomial Z;
	Z.Degree = 4;
	RB_CHECK(rb::SolveInInterval(Z, 0.0, 1.0, Roots) == 0);
	for (const double X : Bad)
	{
		rb::Polynomial Q;
		Q.Degree = 4;
		Q.c[0] = -1.0;
		Q.c[2] = X;
		Q.c[4] = 1.0;
		const int Count = rb::SolveInInterval(Q, 0.0, 2.0, Roots);
		for (int k = 0; k < Count; ++k)
		{
			RB_CHECK(Roots[k] >= 0.0 && Roots[k] <= 2.0);
		}
	}
}

namespace
{
#if RB_DETECT_ALLOC_HOOK
	long& AllocationCount()
	{
		static long Count = 0;
		return Count;
	}

	int CountAllocations(int AllocType, void*, size_t, int, long, const unsigned char*, int)
	{
		if (AllocType == _HOOK_ALLOC || AllocType == _HOOK_REALLOC)
		{
			++AllocationCount();
		}
		return 1;
	}
#endif

	// Every public entry point of Detect.h and Polynomial.h on a mixed set of segments; returns a checksum of the results.
	double DetectionWorkload()
	{
		rb::Rng Rng(0xC0FFEEull);
		const rb::TableGeometry T = PocketWithSurround();
		const rb::PocketGeometry& Pocket = T.Pockets[static_cast<int>(rb::PocketId::FootLeft)];
		const rb::NoseSegment Nose = NosePiece();
		const rb::JawArc Jaw = MakeJaw({0.0, 0.6}, 0.004, -rb::kPi, rb::kPi);
		const rb::Facing Face = MakeFacing({0.0, 0.7}, {0.1, 0.7}, {0.0, -1.0});
		const rb::RailTopPolygon Cap = CapWithCut();
		rb::TableLandmarks L;
		L.HeadStringX = -0.635;
		L.FootStringX = 0.635;
		rb::EnvironmentSpec Lamp;
		Lamp.LampUndersideZ = 0.3;
		double Sum = 0.0;
		const auto Add = [&Sum](const ContactPrediction& C) { Sum += C.Found ? C.Time + C.Flags : -1.0; };
		for (int i = 0; i < 64; ++i)
		{
			const Vec3 P{Rng.NextUniform(-0.3, 0.3), Rng.NextUniform(0.3, 0.7), kR};
			const MotionSegment A = ClothSegment(Rng, P, RandomPlanDirection(Rng));
			const MotionSegment F = Airborne(P, RandomPlanDirection(Rng) * 2.0 + Vec3{0.0, 0.0, 1.0});
			const MotionSegment In = FlightSegment(Rng, rb::ToVec3(Pocket.CaptureCenter, 0.0), RandomDirection(Rng));
			const MotionSegment B = Stationary(rb::PositionAt(A, 0.3) + Vec3{0.03, 0.03, 0.0});
			Add(rb::PredictBallBall(A, kR, B, kR, rb::kInfinity, Numerics()));
			Add(rb::PredictBallBall(F, kR, B, kR, rb::kInfinity, Numerics()));
			Add(rb::PredictNoseOnCloth(A, kR, Nose, NoseContactOffset(), rb::kInfinity, Numerics()));
			Add(rb::PredictNoseAirborne(F, kR, Nose, 0.0, rb::kInfinity, Numerics()));
			Add(rb::PredictJawArcOnCloth(A, kR, Jaw, NoseContactOffset(), rb::kInfinity, Numerics()));
			Add(rb::PredictJawArcAirborne(F, kR, Jaw, rb::kInfinity, Numerics()));
			Add(rb::PredictFacingOnShelf(A, kR, Face, 0.0275, rb::kInfinity, Numerics()));
			Add(rb::PredictFacingAirborne(F, kR, Face, rb::kInfinity, Numerics()));
			Add(rb::PredictFacingTopEdge(F, kR, Face, rb::kInfinity, Numerics()));
			Add(rb::PredictDropEdge(A, kR, Pocket, rb::kInfinity, Numerics()));
			Add(rb::PredictLinerWall(In, kR, Pocket, rb::kInfinity, Numerics()));
			Add(rb::PredictRimTorus(In, kR, Pocket, rb::kInfinity, Numerics()));
			Add(rb::PredictCaptureCircle(A, Pocket, rb::kInfinity, Numerics()));
			Add(rb::PredictCaptureDepth(In, kR, rb::kInfinity, Numerics()));
			Add(rb::PredictPocketExit(In, kR, Pocket, rb::kInfinity, Numerics()));
			Add(rb::PredictSlateLanding(F, kR, rb::kInfinity));
			Add(rb::PredictRailTop(F, kR, Cap, rb::kInfinity, Numerics()));
			Add(rb::PredictRailTopEdge(F, kR, Cap, 2, rb::kInfinity, Numerics()));
			Add(rb::PredictRailTopEdge(F, kR, Cap, rb::kCutRimEdge, rb::kInfinity, Numerics()));
			int Edge = -1;
			Add(rb::PredictSupportExit(A, Cap, rb::kInfinity, Edge));
			Sum += Edge;
			Add(rb::PredictOuterBoundary(F, T.OuterBoundary, rb::kInfinity));
			Add(rb::PredictLampApex(F, kR, Lamp, kG, rb::kInfinity));
			Add(rb::PredictPlanDistanceCrossing(F, kR, B, kR, true, rb::kInfinity, Numerics()));
			rb::LineCrossing Out[16];
			const int N = rb::PredictLineCrossings(A, L, 0.0, rb::kInfinity, 1e-6, true, Out, 16);
			for (int k = 0; k < N; ++k)
			{
				Sum += Out[k].Time + static_cast<double>(Out[k].Line) + Out[k].Direction;
			}
			rb::BallTableContext InPocket;
			InPocket.Pocket = rb::PocketId::FootLeft;
			for (const MotionSegment* S : {&A, &F, &In})
			{
				const rb::FeaturePrediction E = rb::PredictTableEvent(*S, rb::BallSpec{}, InPocket, T, Lamp, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
				Add(E.Contact);
				Sum += static_cast<double>(E.Feature.Kind) + E.Feature.Index + E.Feature.SubIndex;
				rb::BallState Ball;
				Ball.Position = rb::PositionAt(*S, 0.01);
				Ball.State = S->State;
				const rb::FixedContact K = rb::MakeFixedContact(E.Feature, T, Ball, rb::BallSpec{}, rb::DetectOptions{});
				Sum += K.Normal.x + K.Normal.y + K.Normal.z + K.Elevation;
			}
			const rb::Aabb3 Box = rb::SweptBounds(A, 0.0, 1.0);
			Sum += Box.Lo.x + Box.Hi.y;
			rb::TableFeatureRef Refs[32];
			bool Overflow = false;
			Sum += rb::QueryTableFeatures(Box.Inflated(0.1), T, rb::DetectOptions{}, Refs, 32, Overflow);
			const rb::Polynomial G = rb::BallBallGapPolynomial(A, kR, B, kR, 0.0);
			double Roots[rb::Polynomial::kMaxDegree];
			const int Count = rb::SolveInInterval(G, 0.0, 10.0, Roots);
			for (int k = 0; k < Count; ++k)
			{
				Sum += Roots[k];
			}
		}
		return Sum;
	}
}

RB_TEST(Detect_Review_NoHeapAllocationAndBitwiseDeterminism)
{
	const double Warm = DetectionWorkload(); // outside the counted window (lazy CRT initialisation, if any)
#if RB_DETECT_ALLOC_HOOK
	AllocationCount() = 0;
	const _CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(&CountAllocations);
	{
		const std::vector<double> Control(8, 1.0); // positive control: the hook sees heap allocations
		RB_CHECK(AllocationCount() >= 1 && Control.size() == 8u);
	}
	AllocationCount() = 0;
	const double Sum = DetectionWorkload();
	const long Allocations = AllocationCount();
	_CrtSetAllocHook(Previous);
	RB_CHECK(Allocations == 0);
	RB_CHECK(SameBits(Sum, Warm));
#else
	RB_CHECK(SameBits(DetectionWorkload(), Warm));
#endif
	RB_CHECK(rb::IsFinite(Warm));
}
