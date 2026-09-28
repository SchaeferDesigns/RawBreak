// Owner: WP-5 (event detection). Whole-table checks of the dispatcher on every WP-2 preset (Integ_: they need
// BuildTableGeometry, ComputeCushionContact and FacingContactOffset, architecture 18):
//  * watertightness: a segment on the cloth, or in flight near a pocket or a rail, never passes into a table feature
//    before the event PredictTableEvent returns (a geometric oracle of every feature's documented surface, sampled
//    along the segment up to the event);
//  * culling: the swept-bound culling and the t_best pruning of PredictTableEvent return the exhaustive earliest event;
//  * the only leaks are over a pocket's hole (the r_p cylinder), through the ends of the facings and of the back wall:
//    the Level A geometry (collisions 5.3, TableGeometry.h) defines the facings up to the cushion-back line (and only up
//    to h, not up to the cushion top) and the back wall on the arc behind them, but not the cushion's end, back and
//    underside faces that bound the hole between them. Reported (mostly PocketFall, rarely in flight) and pinned to
//    exactly those classes.
// Regression of the review fixes: straight rail-top edges over a pocket cut are no edges (the cap's Facing edge of a
// corner surround lies entirely over the hole and was hit in mid-air), they end in a corner at the cut and (with the
// table) at the jaw rounding instead of the virtual jaw point; the cut rim of a sloped cushion top follows the surface
// (a horizontal circle at the cut center's height floated up to 8 mm above it or sank below it). The facing / jaw
// junction on the cloth (a ball rolled into the facing unchecked) is Integ_...Watertight's cloth half.

#include "rbtest.h"

#include "DetectTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/TableSpec.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;
using rb::TableFeatureKind;

namespace
{
#ifdef NDEBUG
	constexpr int kLeakTrialsPerMode = 600;
	constexpr int kCullTrialsPerMode = 4000;
	constexpr int kSamples = 2000;
#else
	constexpr int kLeakTrialsPerMode = 30;
	constexpr int kCullTrialsPerMode = 250;
	constexpr int kSamples = 1000;
#endif

	constexpr rb::TablePreset kPresets[] = {rb::TablePreset::NineFootPro, rb::TablePreset::NineFootTight, rb::TablePreset::EightFootPro,
		rb::TablePreset::EightFootHome, rb::TablePreset::SevenFootBar, rb::TablePreset::SevenFoot78, rb::TablePreset::SevenFootTrue};

	enum Mode : int
	{
		Cloth = 0,  // rolling, sliding or a curving tilt piece on the cloth near a pocket mouth or anywhere on the bed
		Flight = 1, // airborne near a pocket mouth or a rail (up to 0.1 m above the landing height)
		InPocket = 2, // PocketFall anywhere inside the hole, any direction
	};

	bool OnArc(const Vec2& D, double From, double Sweep)
	{
		if (Sweep >= rb::kTwoPi)
		{
			return true;
		}
		double Delta = std::atan2(D.y, D.x) - From;
		Delta -= rb::kTwoPi * std::floor(Delta / rb::kTwoPi);
		return Delta <= Sweep;
	}

	bool InPoly(const rb::RailTopPolygon& Poly, const Vec2& X)
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

	double PlaneZ(const rb::RailTopPolygon& Poly, const Vec2& P)
	{
		const Vec3& N = Poly.PlaneNormal;
		return Poly.PlanePoint.z - (N.x * (P.x - Poly.PlanePoint.x) + N.y * (P.y - Poly.PlanePoint.y)) / N.z;
	}

	bool IsPhysical(rb::RailEdgeKind K) { return K == rb::RailEdgeKind::CushionBack || K == rb::RailEdgeKind::OuterEdge || K == rb::RailEdgeKind::Facing; }

	// Every feature's documented surface (Detect.h, TableGeometry.h), written independently of the predictors: the deepest
	// penetration of a ball centered at X over the features whose validity region contains X; Class = the leak class.
	double Penetration(const Vec3& X, bool OnCloth, const rb::TableGeometry& T, const char*& Class, int* Which = nullptr)
	{
		double Worst = 0.0;
		Class = "";
		const auto Note = [&](double G, const char* Name, int Index)
		{
			if (G < Worst)
			{
				Worst = G;
				Class = Name;
				if (Which != nullptr)
				{
					*Which = Index;
				}
			}
		};
		for (int i = 0; i < T.Noses.Size(); ++i)
		{
			const rb::NoseSegment& N = T.Noses[i];
			if (!N.Present)
			{
				continue;
			}
			const Vec3 W = X - rb::ToVec3(N.Start, N.Height);
			const Vec3 D = rb::ToVec3(N.Direction);
			const double s = rb::Dot(D, W);
			if (s < 0.0 || s > N.Length)
			{
				continue;
			}
			if (OnCloth)
			{
				Note(rb::Dot(rb::ToVec3(N.InwardNormal), W) - rb::ComputeCushionContact(kR, N.Height, 0.0, false).HorizontalOffset, "nose", i);
			}
			else if (rb::Dot(rb::ToVec3(N.InwardNormal), W) >= 0.0)
			{
				Note(rb::Length(W - D * s) - kR, "nose", i);
			}
		}
		for (int i = 0; i < T.JawArcs.Size(); ++i)
		{
			const rb::JawArc& J = T.JawArcs[i];
			const Vec2 H = rb::XY(X) - J.Center;
			if (!OnArc(H, J.AngleFrom, J.AngleSweep))
			{
				continue;
			}
			if (OnCloth)
			{
				Note(rb::Length(H) - (J.Radius + rb::ComputeCushionContact(kR, J.Height, 0.0, false).HorizontalOffset), "jaw", i);
			}
			else
			{
				Note(std::sqrt(rb::Square(rb::Length(H) - J.Radius) + rb::Square(X.z - J.Height)) - kR, "jaw", i);
			}
		}
		for (int i = 0; i < T.Facings.Size(); ++i)
		{
			const rb::Facing& F = T.Facings[i];
			const Vec3 W = X - rb::ToVec3(F.Start, F.TopHeight);
			const Vec3 D = rb::ToVec3(F.Direction);
			const double s = rb::Dot(D, W);
			if (s < 0.0 || s > F.Length)
			{
				continue;
			}
			if (OnCloth)
			{
				Note(rb::Dot(rb::ToVec3(F.PocketNormal), W) - rb::FacingContactOffset(kR, F.TopHeight, F.Backdraft), "facing", i);
				continue;
			}
			const Vec3 N = rb::ToVec3(F.PocketNormal) * std::cos(F.Backdraft) - Vec3{0.0, 0.0, std::sin(F.Backdraft)};
			const double ContactZ = X.z - kR * N.z;
			if (ContactZ >= 0.0 && ContactZ <= F.TopHeight)
			{
				Note(rb::Dot(N, W) - kR, "facing", i);
			}
			if (rb::Dot(rb::ToVec3(F.PocketNormal), W) >= 0.0)
			{
				Note(rb::Length(W - D * s) - kR, "facingTop", i);
			}
		}
		if (OnCloth)
		{
			return Worst;
		}
		for (int p = 0; p < T.Pockets.Size(); ++p)
		{
			const rb::PocketGeometry& P = T.Pockets[p];
			const Vec2 H = rb::XY(X) - P.CaptureCenter;
			const double Rho = rb::Length(H);
			const bool Front = OnArc(H, P.FrontArcFrom, P.FrontArcSweep);
			if (Rho < P.CaptureRadius && (Front ? X.z < -P.DropRadius : X.z <= P.WallTopZ))
			{
				Note((P.CaptureRadius - kR) - Rho, Front ? "liner" : "backWall", p);
			}
			if (Front && Rho <= P.DropEdgeRadius && X.z >= -P.DropRadius)
			{
				Note(std::sqrt(rb::Square(Rho - P.DropEdgeRadius) + rb::Square(X.z + P.DropRadius)) - (kR + P.DropRadius), "rimTorus", p);
			}
		}
		for (int i = 0; i < T.RailTops.Size(); ++i)
		{
			const rb::RailTopPolygon& Poly = T.RailTops[i];
			const Vec3 Foot = X - Poly.PlaneNormal * kR;
			if (InPoly(Poly, rb::XY(Foot)) && (!Poly.HasCut || rb::Length(rb::XY(Foot) - Poly.CutCenter) >= Poly.CutRadius))
			{
				Note(rb::Dot(Poly.PlaneNormal, X - Poly.PlanePoint) - kR, "railTop", 100 * i);
			}
			for (int e = 0; e < Poly.VertexCount; ++e)
			{
				if (!IsPhysical(Poly.Edges[e]))
				{
					continue;
				}
				const Vec2 V0 = Poly.Vertices[e];
				const Vec2 V1 = Poly.Vertices[(e + 1) % Poly.VertexCount];
				const Vec3 E0 = rb::ToVec3(V0, PlaneZ(Poly, V0));
				const Vec3 E1 = rb::ToVec3(V1, PlaneZ(Poly, V1));
				const double L = rb::Length(E1 - E0);
				const Vec3 D = (E1 - E0) / L;
				const Vec3 W = X - E0;
				const double s = rb::Dot(D, W);
				// A Facing edge ends where its jaw's rounding starts (the polygon runs it to the virtual jaw point).
				double SLo = 0.0;
				double SHi = L;
				if (Poly.Edges[e] == rb::RailEdgeKind::Facing)
				{
					for (int j = 0; j < T.JawArcs.Size(); ++j)
					{
						const rb::JawArc& J = T.JawArcs[j];
						if (J.Pocket != Poly.Pocket || !(J.Radius > 0.0))
						{
							continue;
						}
						const double ST = rb::Dot(rb::ToVec3(J.TangentOnFacing - V0), D) / rb::Dot(rb::ToVec3(rb::XY(D)), D);
						if (rb::Length(J.VirtualPoint - V0) < 1e-9)
						{
							SLo = ST;
							Note(rb::Length(X - (E0 + D * ST)) - kR, "railTopEdge", 100 * i + e);
						}
						if (rb::Length(J.VirtualPoint - V1) < 1e-9)
						{
							SHi = ST;
							Note(rb::Length(X - (E0 + D * ST)) - kR, "railTopEdge", 100 * i + e);
						}
					}
				}
				if (s >= SLo && s <= SHi && rb::Dot(rb::Cross(D, Poly.PlaneNormal), W) >= 0.0 &&
					!(Poly.HasCut && rb::Length(rb::XY(E0 + D * s) - Poly.CutCenter) < Poly.CutRadius))
				{
					Note(rb::Length(W - D * s) - kR, "railTopEdge", 100 * i + e);
				}
				// Where the edge enters the cut disc it ends in a corner.
				if (Poly.HasCut && (rb::Length(V0 - Poly.CutCenter) < Poly.CutRadius) != (rb::Length(V1 - Poly.CutCenter) < Poly.CutRadius))
				{
					const Vec2 Dv = V1 - V0;
					const Vec2 Wv = V0 - Poly.CutCenter;
					const double a = rb::Dot(Dv, Dv);
					const double b = 2.0 * rb::Dot(Dv, Wv);
					const double c = rb::Dot(Wv, Wv) - Poly.CutRadius * Poly.CutRadius;
					const double Sq = std::sqrt(b * b - 4.0 * a * c);
					const double R0 = (-b - Sq) / (2.0 * a);
					const Vec2 Jp = V0 + Dv * (R0 >= 0.0 && R0 <= 1.0 ? R0 : (-b + Sq) / (2.0 * a));
					Note(rb::Length(X - rb::ToVec3(Jp, PlaneZ(Poly, Jp))) - kR, "railTopEdge", 100 * i + e);
				}
			}
			if (Poly.HasCut && Poly.PlaneNormal.x == 0.0 && Poly.PlaneNormal.y == 0.0)
			{
				// The rim of the flat cap: circle r_p about the cut center at the cap height.
				const double Zc = Poly.PlanePoint.z;
				const Vec2 H = rb::XY(X) - Poly.CutCenter;
				const double Rho = rb::Length(H);
				if (Rho <= Poly.CutRadius && X.z >= Zc && (Rho == 0.0 || InPoly(Poly, Poly.CutCenter + H * (Poly.CutRadius / Rho))))
				{
					Note(std::sqrt(rb::Square(Rho - Poly.CutRadius) + rb::Square(X.z - Zc)) - kR, "cutRim", 100 * i + 99);
				}
			}
			else if (Poly.HasCut && rb::Length(rb::XY(X) - Poly.CutCenter) < Poly.CutRadius + kR + 0.01)
			{
				// The true rim of a sloped cushion top: the plan circle lifted onto the plane, over the polygon; nearest point
				// by sampling the plan angle every 0.25 deg and a golden-section refinement. Where the center's foot on the
				// plane lies over the cut and the center is above the plane.
				const double Above = rb::Dot(Poly.PlaneNormal, X - Poly.PlanePoint);
				const Vec3 OnPlane = X - Poly.PlaneNormal * Above;
				if (Above >= 0.0 && rb::Length(rb::XY(OnPlane) - Poly.CutCenter) <= Poly.CutRadius)
				{
					const auto RimAt = [&](double Phi)
					{
						const Vec2 P = Poly.CutCenter + Vec2{std::cos(Phi), std::sin(Phi)} * Poly.CutRadius;
						return rb::ToVec3(P, PlaneZ(Poly, P));
					};
					double BestPhi = 0.0;
					double Best = 1e9;
					for (int k = 0; k < 1440; ++k)
					{
						const double Phi = rb::kTwoPi * k / 1440.0;
						if (InPoly(Poly, rb::XY(RimAt(Phi))) && rb::Length(X - RimAt(Phi)) < Best)
						{
							Best = rb::Length(X - RimAt(Phi));
							BestPhi = Phi;
						}
					}
					if (Best < 1e9)
					{
						double Lo = BestPhi - rb::kTwoPi / 1440.0;
						double Hi = BestPhi + rb::kTwoPi / 1440.0;
						for (int k = 0; k < 60; ++k)
						{
							const double M1 = Lo + (Hi - Lo) * 0.381966;
							const double M2 = Hi - (Hi - Lo) * 0.381966;
							const bool In1 = InPoly(Poly, rb::XY(RimAt(M1)));
							const bool In2 = InPoly(Poly, rb::XY(RimAt(M2)));
							const double D1 = In1 ? rb::Length(X - RimAt(M1)) : 1e9;
							const double D2 = In2 ? rb::Length(X - RimAt(M2)) : 1e9;
							(D1 < D2 ? Hi : Lo) = D1 < D2 ? M2 : M1;
							Best = rb::Min(Best, rb::Min(D1, D2));
						}
						Note(Best - kR, "cutRim", 100 * i + 99);
					}
				}
			}
		}
		return Worst;
	}

	MotionSegment RandomSegment(rb::Rng& Rng, int M, const rb::TableGeometry& T, const rb::PocketGeometry& P, rb::BallTableContext& Ctx)
	{
		const double Lx = T.HalfLength - kR - 0.001;
		const double Ly = T.HalfWidth - kR - 0.001;
		const double A = Rng.NextUniform(0.0, rb::kTwoPi);
		const Vec3 Dir{std::cos(A), std::sin(A), 0.0};
		if (M == Cloth)
		{
			const Vec3 Near = rb::ToVec3(P.MouthMid, kR) - rb::ToVec3(P.Axis) * Rng.NextUniform(0.03, 0.3) + Vec3{Rng.NextUniform(-0.1, 0.1), Rng.NextUniform(-0.1, 0.1), 0.0};
			const Vec3 Anywhere{Rng.NextUniform(-Lx, Lx), Rng.NextUniform(-Ly, Ly), kR};
			const Vec3 Pick = Rng.NextBelow(2) == 0 ? Near : Anywhere;
			const Vec3 Start{rb::Clamp(Pick.x, -Lx, Lx), rb::Clamp(Pick.y, -Ly, Ly), kR}; // on the bed (not behind a nose)
			const Vec3 V = Dir * Rng.NextUniform(0.05, 5.0);
			const std::uint32_t Kind = Rng.NextBelow(3);
			if (Kind == 1)
			{
				const Vec3 W{Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-150.0, 150.0), Rng.NextUniform(-50.0, 50.0)};
				if (rb::Length(rb::Planar(rb::SlipVelocity(V, W, kR))) > 1e-6)
				{
					return Sliding(Start, V, W);
				}
			}
			MotionSegment S = Rolling(Start, V);
			if (Kind == 2)
			{
				// A curving tilt piece (architecture 8.11): Accel2 not parallel to Vel0.
				const double B = Rng.NextUniform(0.0, rb::kTwoPi);
				S.Accel2 = S.Accel2 + Vec3{std::cos(B), std::sin(B), 0.0} * Rng.NextUniform(0.0, 0.05);
				S.TauEnd = rb::Min(S.TauEnd, 3.0);
				S.Tilt.Active = true;
			}
			return S;
		}
		if (M == Flight)
		{
			const Vec3 Start = Rng.NextBelow(3) != 0
				? rb::ToVec3(P.MouthMid, kR + Rng.NextUniform(0.0, 0.1)) - rb::ToVec3(P.Axis) * Rng.NextUniform(0.0, 0.4) +
					Vec3{Rng.NextUniform(-0.15, 0.15), Rng.NextUniform(-0.15, 0.15), 0.0}
				: Vec3{Rng.NextUniform(-Lx, Lx), (Rng.NextBelow(2) == 0 ? -1.0 : 1.0) * Rng.NextUniform(Ly - 0.2, Ly), kR + Rng.NextUniform(0.0, 0.1)};
			const Vec3 C{rb::Clamp(Start.x, -Lx, Lx), rb::Clamp(Start.y, -Ly, Ly), Start.z};
			return Airborne(C, Dir * Rng.NextUniform(0.1, 6.0) + Vec3{0.0, 0.0, Rng.NextUniform(0.0, 3.0)});
		}
		const Vec2 H = P.CaptureCenter + Vec2{Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-1.0, 1.0)} * 0.03;
		const Vec3 D{Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-1.0, 1.0), Rng.NextUniform(-0.3, 1.0)};
		MotionSegment S = PocketFall({H.x, H.y, Rng.NextUniform(-0.02, kR)}, D * Rng.NextUniform(0.0, 3.0));
		S.TauEnd = 0.5;
		Ctx.Pocket = P.Id;
		return S;
	}

	struct LeakStats
	{
		int Cases[3] = {0, 0, 0};
		int Leaks[3] = {0, 0, 0};
		int Hits[3] = {0, 0, 0};
		int HoleCorners[3] = {0, 0, 0}; // leaks into a facing, its top edge or the back wall with the center over a pocket's hole
	};

	// The Level A gap (file comment): inside the r_p cylinder the facings end at the cushion-back line and the back wall
	// runs behind them, but the cushion's end, back and underside faces between them are not defined (and above h the
	// facing's strip up to the cushion top is not part of the face, TableGeometry.h).
	bool IsHoleCorner(const char* Class, const Vec3& X, const rb::TableGeometry& T)
	{
		if (std::strcmp(Class, "facing") != 0 && std::strcmp(Class, "facingTop") != 0 && std::strcmp(Class, "backWall") != 0)
		{
			return false;
		}
		for (int p = 0; p < T.Pockets.Size(); ++p)
		{
			if (rb::Length(rb::XY(X) - T.Pockets[p].CaptureCenter) < T.Pockets[p].CaptureRadius)
			{
				return true;
			}
		}
		return false;
	}

	void RunLeaks(rb::TablePreset Preset, LeakStats& Stats)
	{
		rb::TableGeometry T;
		RB_REQUIRE(rb::BuildTableGeometry(rb::GetTableSpec(Preset), T) == rb::ErrorCode::Ok);
		rb::Rng Rng(0x5EA1ull + static_cast<std::uint64_t>(Preset));
		for (int M = 0; M < 3; ++M)
		{
			for (int Trial = 0; Trial < kLeakTrialsPerMode; ++Trial)
			{
				rb::BallTableContext Ctx;
				const rb::PocketGeometry& P = T.Pockets[static_cast<int>(Rng.NextBelow(static_cast<std::uint32_t>(T.Pockets.Size())))];
				const MotionSegment S = RandomSegment(Rng, M, T, P, Ctx);
				const char* Class = "";
				if (Penetration(S.Pos0, M == Cloth, T, Class) < -1e-9)
				{
					continue; // starts inside a feature: not a detection case
				}
				const rb::FeaturePrediction E =
					rb::PredictTableEvent(S, rb::BallSpec{}, Ctx, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
				++Stats.Cases[M];
				Stats.Hits[M] += E.Contact.Found ? 1 : 0;
				const double End = E.Contact.Found ? E.Contact.Time - S.T0 : rb::Min(S.TauEnd, 2.0);
				for (int i = 1; i <= kSamples; ++i)
				{
					const double Tau = End * static_cast<double>(i) / static_cast<double>(kSamples) - 1e-9;
					if (Penetration(rb::PositionAt(S, Tau), M == Cloth, T, Class) < -1e-6)
					{
						++Stats.Leaks[M];
						const Vec3 X = rb::PositionAt(S, Tau);
						const bool Known = M != Cloth && IsHoleCorner(Class, X, T);
						Stats.HoleCorners[M] += Known ? 1 : 0;
						if (!Known)
						{
							int Which = -1;
							Penetration(X, M == Cloth, T, Class, &Which);
							std::printf("  [%s] mode %d: into %s %d at (%.5f, %.5f, %.5f), event kind %d/%d/%d at tau %.6g; start (%.17g, %.17g, %.17g) v (%.17g, %.17g, "
										"%.17g) a (%.17g, %.17g, %.17g) window %.17g pocket %d\n",
								rb::GetTableSpec(Preset).Name, M, Class, Which, X.x, X.y, X.z, E.Contact.Found ? static_cast<int>(E.Feature.Kind) : -1, E.Feature.Index,
								E.Feature.SubIndex, End, S.Pos0.x, S.Pos0.y, S.Pos0.z, S.Vel0.x, S.Vel0.y, S.Vel0.z, S.Accel2.x, S.Accel2.y, S.Accel2.z, S.TauEnd,
								static_cast<int>(Ctx.Pocket));
						}
						break;
					}
				}
			}
		}
	}

	struct Best
	{
		ContactPrediction C;
		rb::TableFeatureRef F;
	};

	bool KeyLess(const rb::TableFeatureRef& A, const rb::TableFeatureRef& B)
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

	void Take(Best& B, const ContactPrediction& C, TableFeatureKind K, int I, int S)
	{
		if (!C.Found)
		{
			return;
		}
		const rb::TableFeatureRef R{K, static_cast<std::uint8_t>(I), static_cast<std::uint8_t>(S)};
		if (!B.C.Found || C.Time < B.C.Time || (C.Time == B.C.Time && KeyLess(R, B.F)))
		{
			B.C = C;
			B.F = R;
		}
	}

	// Every applicable feature of the dispatcher's state rules (Detect.h), without culling or pruning.
	Best Exhaustive(const MotionSegment& S, const rb::TableGeometry& T, const rb::BallTableContext& Ctx)
	{
		Best B;
		const rb::NumericsConfig N = Numerics();
		if (rb::IsOnSurface(S.State))
		{
			for (int i = 0; i < T.Noses.Size(); ++i)
			{
				Take(B, rb::PredictNoseOnCloth(S, kR, T.Noses[i], rb::ComputeCushionContact(kR, T.Noses[i].Height, 0.0, false).HorizontalOffset, rb::kInfinity, N),
					TableFeatureKind::NoseSegment, i, 0);
			}
			for (int i = 0; i < T.JawArcs.Size(); ++i)
			{
				Take(B, rb::PredictJawArcOnCloth(S, kR, T.JawArcs[i], rb::ComputeCushionContact(kR, T.JawArcs[i].Height, 0.0, false).HorizontalOffset, rb::kInfinity, N),
					TableFeatureKind::JawArc, i, 0);
			}
			for (int i = 0; i < T.Facings.Size(); ++i)
			{
				Take(B, rb::PredictFacingOnShelf(S, kR, T.Facings[i], rb::FacingContactOffset(kR, T.Facings[i].TopHeight, T.Facings[i].Backdraft), rb::kInfinity, N),
					TableFeatureKind::FacingFace, i, 0);
				// The facing's back-end edge (WP-10, VAL ROB-11).
				Take(B, rb::PredictFacingEndEdge(S, kR, T.Facings[i], rb::FacingEndEdgeTop(T, T.Facings[i]), rb::kInfinity, N), TableFeatureKind::FacingTopEdge, i,
					rb::kFacingEndEdge);
			}
			for (int p = 0; p < T.Pockets.Size(); ++p)
			{
				Take(B, rb::PredictDropEdge(S, kR, T.Pockets[p], rb::kInfinity, N), TableFeatureKind::DropEdge, p, 0);
			}
			return B;
		}
		const bool Air = S.State == rb::MotionState::Airborne;
		for (int i = 0; Air && i < T.Noses.Size(); ++i)
		{
			Take(B, rb::PredictNoseAirborne(S, kR, T.Noses[i], 0.0, rb::kInfinity, N), TableFeatureKind::NoseSegment, i, 0);
		}
		for (int i = 0; i < T.JawArcs.Size(); ++i)
		{
			if (Air || i / 2 == static_cast<int>(Ctx.Pocket))
			{
				Take(B, rb::PredictJawArcAirborne(S, kR, T.JawArcs[i], rb::kInfinity, N), TableFeatureKind::JawArc, i, 0);
			}
		}
		for (int i = 0; i < T.Facings.Size(); ++i)
		{
			if (Air || i / 2 == static_cast<int>(Ctx.Pocket))
			{
				Take(B, rb::PredictFacingAirborne(S, kR, T.Facings[i], rb::kInfinity, N), TableFeatureKind::FacingFace, i, 0);
				Take(B, rb::PredictFacingTopEdge(S, kR, T.Facings[i], rb::kInfinity, N), TableFeatureKind::FacingTopEdge, i, 0);
				Take(B, rb::PredictFacingEndEdge(S, kR, T.Facings[i], rb::FacingEndEdgeTop(T, T.Facings[i]), rb::kInfinity, N), TableFeatureKind::FacingTopEdge, i,
					rb::kFacingEndEdge);
				Take(B, rb::PredictFacingBottomEdge(S, kR, T.Facings[i], rb::kInfinity, N), TableFeatureKind::FacingTopEdge, i, rb::kFacingBottomEdge);
			}
		}
		for (int p = 0; p < T.Pockets.Size(); ++p)
		{
			if (Air || p == static_cast<int>(Ctx.Pocket))
			{
				Take(B, rb::PredictLinerWall(S, kR, T.Pockets[p], rb::kInfinity, N), TableFeatureKind::LinerWall, p, 0);
				Take(B, rb::PredictRimTorus(S, kR, T.Pockets[p], rb::kInfinity, N), TableFeatureKind::RimTorus, p, 0);
			}
		}
		if (!Air)
		{
			const int p = static_cast<int>(Ctx.Pocket);
			Take(B, rb::PredictCaptureDepth(S, kR, rb::kInfinity, N), TableFeatureKind::CaptureDepth, p, 0);
			Take(B, rb::PredictPocketExit(S, kR, T.Pockets[p], rb::kInfinity, N), TableFeatureKind::PocketExit, p, 0);
		}
		for (int i = 0; i < T.RailTops.Size(); ++i)
		{
			const rb::RailTopPolygon& Poly = T.RailTops[i];
			Take(B, rb::PredictRailTop(S, kR, Poly, rb::kInfinity, N), TableFeatureKind::RailTop, i, 0);
			for (int e = 0; e < Poly.VertexCount; ++e)
			{
				Take(B, rb::PredictRailTopEdge(S, kR, T, i, e, rb::kInfinity, N), TableFeatureKind::RailTopEdge, i, e);
			}
			Take(B, rb::PredictRailTopEdge(S, kR, Poly, rb::kCutRimEdge, rb::kInfinity, N), TableFeatureKind::RailTopEdge, i, rb::kCutRimEdge);
		}
		if (Air)
		{
			Take(B, rb::PredictOuterBoundary(S, T.OuterBoundary, rb::kInfinity), TableFeatureKind::OuterBoundary, 0, 0);
		}
		return B;
	}

	constexpr double kRailTopZ = 0.048;

	rb::RailTopPolygon Polygon(const Vec3& Point, const Vec3& Normal, std::initializer_list<Vec2> Vertices, std::initializer_list<rb::RailEdgeKind> Edges)
	{
		rb::RailTopPolygon P;
		P.PlanePoint = Point;
		P.PlaneNormal = rb::Normalized(Normal);
		for (const Vec2& V : Vertices)
		{
			P.Vertices[P.VertexCount++] = V;
		}
		int i = 0;
		for (rb::RailEdgeKind E : Edges)
		{
			P.Edges[i++] = E;
		}
		return P;
	}

	// The 9FT_PRO HEAD_RIGHT corner (P0, C_cap (-1.302615, -0.667615), r_p 0.062): the cap behind the head cushion down to
	// the corner, whose Facing edge (on the cushion-back line x = -1.3208, bordering the opening) lies entirely over the
	// hole and whose cushion-back ridge enters the cut at y = -0.60834 (the WP-2 polygon 20).
	rb::RailTopPolygon HeadCapAtCorner()
	{
		rb::RailTopPolygon P = Polygon({-1.3208, 0.0, kRailTopZ}, {0.0, 0.0, 1.0}, {{-1.3208, 0.0}, {-1.4478, 0.0}, {-1.4478, -0.6858}, {-1.3208, -0.6858}, {-1.3208, -0.6192}},
			{rb::RailEdgeKind::Seam, rb::RailEdgeKind::OuterEdge, rb::RailEdgeKind::Seam, rb::RailEdgeKind::Facing, rb::RailEdgeKind::CushionBack});
		P.Kind = rb::RailTopKind::RailCap;
		P.HasCut = true;
		P.CutCenter = {-1.302615, -0.667615};
		P.CutRadius = 0.062;
		P.Pocket = rb::PocketId::HeadRight;
		return P;
	}

	// The sloped head cushion top next to that corner (the WP-2 polygon 16): nose x = -1.27 at h, cushion back x = -1.3208 at
	// RailTopZ; the cut takes a sliver between its Facing edge and the cushion back.
	rb::RailTopPolygon HeadCushionTopAtCorner()
	{
		const double Slope = (kRailTopZ - kNoseH) / 0.0508;
		rb::RailTopPolygon P = Polygon({-1.27, 0.0, kNoseH}, {Slope, 0.0, 1.0}, {{-1.27, 0.0}, {-1.3208, 0.0}, {-1.3208, -0.6192}, {-1.27, -0.55418}},
			{rb::RailEdgeKind::Seam, rb::RailEdgeKind::CushionBack, rb::RailEdgeKind::Facing, rb::RailEdgeKind::Nose});
		P.HasCut = true;
		P.CutCenter = {-1.302615, -0.667615};
		P.CutRadius = 0.062;
		P.Pocket = rb::PocketId::HeadRight;
		return P;
	}
}

// -------------------------------------------------------------------------------------------------
// The facing / jaw junction (standalone, hand-built)
// -------------------------------------------------------------------------------------------------

RB_TEST(Detect_FacingJunctionWithTheJawIsWatertightOnTheCloth)
{
	// Facing along +x from its start (0, 0.7) (pocket side y < 0.7) and its jaw arc tangent there, r_j = 4 mm about
	// (0, 0.704), exposed from -x to -y. On the cloth the jaw is touched at r_j + R_c from its center and the facing at s_f
	// from its line: at the tangent point the facing's surface lies s_f - R_c = 0.06 mm beyond the jaw's.
	const double Rc = NoseContactOffset();
	const rb::Facing Face = MakeFacing({0.0, 0.7}, {0.1, 0.7}, {0.0, -1.0});
	const double Sf = (kR - (kNoseH - kR) * std::sin(Face.Backdraft)) / std::cos(Face.Backdraft);
	RB_REQUIRE(Sf - Rc > 5e-5 && Sf - Rc < 7e-5);
	const rb::JawArc Jaw = MakeJaw({0.0, 0.704}, 0.004, -rb::kPi, 0.5 * rb::kPi);
	// A ball converging on the facing at 10 mm/m passes the junction 0.03 mm inside the step: it clears the jaw, enters the
	// facing's range inside its contact offset (the crossing lay 2.9 mm before its start) and used to roll on into it.
	const double D0 = Rc + 3e-5; // plan distance from the facing line at x = 0
	MotionSegment S = Rolling({-0.05, 0.7 - D0 - 0.05 * 0.01, kR}, {1.0, 0.01, 0.0});
	S.Accel2 = {};
	S.TauEnd = 0.2;
	RB_CHECK(!rb::PredictJawArcOnCloth(S, kR, Jaw, Rc, rb::kInfinity, Numerics()).Found);
	const ContactPrediction C = rb::PredictFacingOnShelf(S, kR, Face, Sf, rb::kInfinity, Numerics());
	RB_REQUIRE(C.Found);
	RB_CHECK(C.Flags == 0);
	const Vec3 X = rb::PositionAt(S, C.Time);
	RB_CHECK_NEAR(X.x, 0.0, 1e-15);                 // at the junction
	RB_CHECK_NEAR(0.7 - X.y, D0, 1e-12);             // inside the facing's offset by the step's remainder
	RB_CHECK(0.7 - X.y < Sf && 0.7 - X.y > Rc);
	// The same entry but moving away from the facing: no contact (it leaves the step).
	MotionSegment Away = S;
	Away.Pos0.y = 0.7 - D0 + 0.05 * 0.01;
	Away.Vel0.y = -0.01;
	RB_CHECK(!rb::PredictFacingOnShelf(Away, kR, Face, Sf, rb::kInfinity, Numerics()).Found);
	// An entry deeper than the step is no junction crossing (the jaw is in the way; only a corrupt state gets there).
	MotionSegment Deep = S;
	Deep.Pos0.y = 0.7 - (Rc - 1e-3) - 0.05 * 0.01;
	RB_CHECK(!rb::PredictFacingOnShelf(Deep, kR, Face, Sf, rb::kInfinity, Numerics()).Found);
	// Airborne, just above the cloth: the same junction for the undercut face (contact point below h).
	MotionSegment Fly = S;
	Fly.State = rb::MotionState::Airborne;
	Fly.Pos0.z = kR + 1e-4;
	const ContactPrediction F = rb::PredictFacingAirborne(Fly, kR, Face, rb::kInfinity, Numerics());
	RB_REQUIRE(F.Found);
	RB_CHECK_NEAR(rb::PositionAt(Fly, F.Time).x, 0.0, 1e-15);
}

// -------------------------------------------------------------------------------------------------
// Rail-top edges and rims at the pocket cut (standalone, hand-built WP-2 polygons)
// -------------------------------------------------------------------------------------------------

RB_TEST(Detect_RailTopEdgeOverThePocketCutIsNoEdge)
{
	const rb::RailTopPolygon Cap = HeadCapAtCorner();
	// A ball flying over the hole toward -x, 10 mm above the cap height: it crosses the line of the cap's Facing edge
	// (x = -1.3208, y in [-0.6858, -0.6192]) where that edge would be 10 mm below its center, but the whole edge lies
	// over the cut: no contact. The first rail-top feature is the far rim of the cut.
	MotionSegment Over = PocketFall({-1.28, -0.65, kRailTopZ + 0.010}, {-1.0, 0.0, 0.0});
	Over.Accel2 = {};
	Over.TauEnd = 0.2;
	RB_CHECK(!rb::PredictRailTopEdge(Over, kR, Cap, 3, rb::kInfinity, Numerics()).Found);
	rb::TableGeometry T;
	T.RailTops.PushBack(Cap);
	RB_CHECK(!rb::PredictRailTopEdge(Over, kR, T, 0, 3, rb::kInfinity, Numerics()).Found);
	Vec3 From[2];
	Vec3 To[2];
	RB_CHECK(rb::RailTopEdgePieces(T, 0, 3, From, To) == 0);
	const ContactPrediction Rim = rb::PredictRailTopEdge(Over, kR, Cap, rb::kCutRimEdge, rb::kInfinity, Numerics());
	RB_REQUIRE(Rim.Found);
	const Vec3 AtRim = rb::PositionAt(Over, Rim.Time);
	RB_CHECK(AtRim.x < -1.3208 - 0.01); // beyond the phantom edge line, at the far side of the hole
	RB_CHECK_NEAR(std::sqrt(rb::Square(Cap.CutRadius - rb::Length(rb::XY(AtRim) - Cap.CutCenter)) + rb::Square(AtRim.z - kRailTopZ)), kR, 1e-12);

	// The cushion-back ridge (edge 4) exists only from its corner with the cut, y = -0.6676 + sqrt(r^2 - dx^2), upward.
	RB_REQUIRE(rb::RailTopEdgePieces(T, 0, 4, From, To) == 1);
	const double YJ = Cap.CutCenter.y + std::sqrt(Cap.CutRadius * Cap.CutRadius - rb::Square(-1.3208 - Cap.CutCenter.x));
	RB_CHECK_NEAR(From[0].x, -1.3208, 1e-15);
	RB_CHECK_NEAR(From[0].y, YJ, 1e-12);
	RB_CHECK_NEAR(To[0].y, 0.0, 1e-15);
	RB_CHECK(From[0].z == kRailTopZ && To[0].z == kRailTopZ);
	RB_REQUIRE(rb::RailTopEdgePieces(T, 0, 1, From, To) == 1); // the outer edge is far from the cut: whole
	RB_CHECK(From[0].x == -1.4478 && From[0].y == 0.0);
	RB_CHECK_NEAR(To[0].y, -0.6858, 1e-15);
	RB_CHECK(rb::RailTopEdgePieces(T, 0, 0, From, To) == 0); // a seam is no physical edge

	// Island feature queries skip the edge over the hole, keep the partly cut ridge.
	rb::TableFeatureRef Refs[16];
	bool Overflow = false;
	const int N = rb::QueryTableFeatures({{-1.36, -0.70, 0.0}, {-1.30, -0.60, 0.1}}, T, rb::DetectOptions{}, Refs, 16, Overflow);
	bool HasFacingEdge = false;
	bool HasRidge = false;
	bool HasRim = false;
	for (int i = 0; i < N; ++i)
	{
		HasFacingEdge |= Refs[i].Kind == TableFeatureKind::RailTopEdge && Refs[i].SubIndex == 3;
		HasRidge |= Refs[i].Kind == TableFeatureKind::RailTopEdge && Refs[i].SubIndex == 4;
		HasRim |= Refs[i].Kind == TableFeatureKind::RailTopEdge && Refs[i].SubIndex == rb::kCutRimEdge;
	}
	RB_CHECK(!Overflow);
	RB_CHECK(!HasFacingEdge);
	RB_CHECK(HasRidge);
	RB_CHECK(HasRim);
}

RB_TEST(Detect_RailTopEdgeEndsInACornerAtTheCut)
{
	const rb::RailTopPolygon Cap = HeadCapAtCorner();
	const double YJ = Cap.CutCenter.y + std::sqrt(Cap.CutRadius * Cap.CutRadius - rb::Square(-1.3208 - Cap.CutCenter.x));
	const Vec3 J{-1.3208, YJ, kRailTopZ};
	// From the opening side (x > -1.3208), below the corner along the ridge (its projection lies over the cut), outside the
	// cut circle: neither the ridge nor the rim is the nearest feature, the corner J is. Straight line (no gravity).
	const Vec3 Start = J + Vec3{0.06, -0.02, 0.03};
	MotionSegment S = PocketFall(Start, (J + Vec3{0.0, -0.004, 0.0} - Start) * 2.0);
	S.Accel2 = {};
	S.TauEnd = 1.0;
	const ContactPrediction C = rb::PredictRailTopEdge(S, kR, Cap, 4, rb::kInfinity, Numerics());
	RB_REQUIRE(C.Found);
	const Vec3 X = rb::PositionAt(S, C.Time);
	RB_CHECK_NEAR(rb::Length(X - J), kR, 1e-12);
	RB_CHECK(X.y < YJ); // projection over the cut
	// The rim point in the center's direction lies over the opening (x > -1.3208), not on this cap: the rim is no contact.
	RB_CHECK(!rb::PredictRailTopEdge(S, kR, Cap, rb::kCutRimEdge, C.Time, Numerics()).Found);
	// Contact frame from the corner (as detected), not from the ridge line.
	rb::TableGeometry T;
	T.RailTops.PushBack(Cap);
	rb::BallState B;
	B.Position = X;
	B.State = rb::MotionState::PocketFall;
	const rb::FixedContact F = rb::MakeFixedContact({TableFeatureKind::RailTopEdge, 0, 4}, T, B, rb::BallSpec{}, rb::DetectOptions{});
	const Vec3 Expected = (X - J) / rb::Length(X - J);
	RB_CHECK_NEAR(F.Normal.x, Expected.x, 1e-12);
	RB_CHECK_NEAR(F.Normal.y, Expected.y, 1e-12);
	RB_CHECK_NEAR(F.Normal.z, Expected.z, 1e-12);

	// Along the physical part of the ridge the edge line is the contact (unchanged).
	MotionSegment Down = PocketFall({-1.3208 + 0.01, -0.3, kRailTopZ + 0.05}, {});
	Down.TauEnd = 1.0;
	const ContactPrediction E = rb::PredictRailTopEdge(Down, kR, Cap, 4, rb::kInfinity, Numerics());
	RB_REQUIRE(E.Found);
	const Vec3 Y = rb::PositionAt(Down, E.Time);
	RB_CHECK_NEAR(std::sqrt(rb::Square(Y.x + 1.3208) + rb::Square(Y.z - kRailTopZ)), kR, 1e-12);
}

RB_TEST(Detect_CutRimOnASlopedCushionTopFollowsTheSurface)
{
	// The cut takes a sliver of the sloped head cushion top between its Facing edge and the cushion back (plan angles
	// 97.4-107.1 deg about C_cap), where the surface rises from 45.7 mm to RailTopZ. The rim there is the plan circle lifted
	// onto the plane (followed by chords 0.02 mm inside it, toward the hole): at 100 deg it is 1.7 mm below RailTopZ, while a
	// horizontal circle at the cut center's plane height (43.8 mm) lay below the surface. At the cushion back it ends in the
	// point J where the cap's rim circle continues and the cushion-back ridge ends in a corner.
	const rb::RailTopPolygon Top = HeadCushionTopAtCorner();
	const rb::RailTopPolygon Cap = HeadCapAtCorner();
	rb::TableGeometry T;
	T.RailTops.PushBack(Top);
	T.RailTops.PushBack(Cap);
	const auto Rim = [&](double Deg)
	{
		const Vec2 P = Top.CutCenter + Vec2{std::cos(Deg * rb::kDegToRad), std::sin(Deg * rb::kDegToRad)} * Top.CutRadius;
		return rb::ToVec3(P, PlaneZ(Top, P));
	};
	RB_CHECK(Rim(100.0).z < kRailTopZ - 0.0015);
	RB_CHECK(Rim(100.0).z > 0.0438 + 0.001);
	for (const double Deg : {100.0, 102.0, 104.0})
	{
		// From the hole, level with the rim point, moving radially out toward it (no gravity: a straight line).
		const Vec3 Q = Rim(Deg);
		RB_REQUIRE(InPoly(Top, rb::XY(Q)));
		const Vec3 Out = rb::Normalized(rb::Planar(Q) - rb::ToVec3(Top.CutCenter));
		MotionSegment S = PocketFall(Q - Out * 0.04, Out * 0.5);
		S.Accel2 = {};
		S.TauEnd = 1.0;
		const ContactPrediction C = rb::PredictRailTopEdge(S, kR, Top, rb::kCutRimEdge, rb::kInfinity, Numerics());
		RB_REQUIRE(C.Found);
		const Vec3 X = rb::PositionAt(S, C.Time);
		// Touches the chord at most 0.03 mm before the true rim point.
		RB_CHECK(rb::Length(X - Q) >= kR - 1e-9);
		RB_CHECK(rb::Length(X - Q) <= kR + 3e-5);
		RB_CHECK(!rb::PredictRailTopEdge(S, kR, Cap, rb::kCutRimEdge, rb::kInfinity, Numerics()).Found); // not the cap's part
		rb::BallState B;
		B.Position = X;
		B.State = rb::MotionState::PocketFall;
		const rb::FixedContact F = rb::MakeFixedContact({TableFeatureKind::RailTopEdge, 0, static_cast<std::uint8_t>(rb::kCutRimEdge)}, T, B, rb::BallSpec{}, rb::DetectOptions{});
		// From the nearest point of the (true) rim, to the chords' accuracy.
		Vec3 Near = Q;
		for (int k = -2000; k <= 2000; ++k)
		{
			const Vec3 R = Rim(Deg + 5.0 * k / 2000.0);
			if (rb::Length(X - R) < rb::Length(X - Near))
			{
				Near = R;
			}
		}
		RB_CHECK(rb::Dot(F.Normal, (X - Near) / rb::Length(X - Near)) > std::cos(3e-3));
	}
	// At the cushion back both the cap's rim circle and the ridge's corner end in J (one point, one contact time).
	const double YJ = Top.CutCenter.y + std::sqrt(Top.CutRadius * Top.CutRadius - rb::Square(-1.3208 - Top.CutCenter.x));
	const Vec3 J{-1.3208, YJ, kRailTopZ};
	const Vec3 Inward = rb::Normalized(rb::ToVec3(Top.CutCenter) - rb::Planar(J));
	MotionSegment AtJ = PocketFall(J + Inward * 0.010 + Vec3{0.0, 0.0, 0.06}, {0.0, 0.0, -0.5});
	AtJ.Accel2 = {};
	AtJ.TauEnd = 1.0;
	const ContactPrediction CapJ = rb::PredictRailTopEdge(AtJ, kR, Cap, rb::kCutRimEdge, rb::kInfinity, Numerics());
	const ContactPrediction RidgeJ = rb::PredictRailTopEdge(AtJ, kR, Top, 1, rb::kInfinity, Numerics());
	RB_REQUIRE(CapJ.Found && RidgeJ.Found);
	RB_CHECK_NEAR(CapJ.Time, RidgeJ.Time, 1e-12);
	RB_CHECK_NEAR(rb::Length(rb::PositionAt(AtJ, CapJ.Time) - J), kR, 1e-12);
	// Behind the cushion back the cap's rim (at RailTopZ), not the cushion top's.
	const Vec3 Q110 = rb::ToVec3(rb::XY(Rim(110.0)), kRailTopZ);
	RB_REQUIRE(InPoly(Cap, rb::XY(Q110)));
	MotionSegment Behind = PocketFall(Q110 - rb::Normalized(rb::Planar(Q110) - rb::ToVec3(Top.CutCenter)) * 0.04, rb::Normalized(rb::Planar(Q110) - rb::ToVec3(Top.CutCenter)) * 0.5);
	Behind.Accel2 = {};
	Behind.TauEnd = 1.0;
	RB_CHECK(rb::PredictRailTopEdge(Behind, kR, Cap, rb::kCutRimEdge, rb::kInfinity, Numerics()).Found);
	RB_CHECK(!rb::PredictRailTopEdge(Behind, kR, Top, rb::kCutRimEdge, rb::kInfinity, Numerics()).Found);
	// Over the pocket opening (the rim point in the ball's direction is on neither polygon) there is no rim.
	const Vec3 Open = rb::ToVec3(Top.CutCenter + Vec2{std::cos(60.0 * rb::kDegToRad), std::sin(60.0 * rb::kDegToRad)} * (Top.CutRadius - 0.01), 0.1);
	MotionSegment O = PocketFall(Open, {0.0, 0.0, -0.5});
	O.Accel2 = {};
	O.TauEnd = 1.0;
	RB_CHECK(!rb::PredictRailTopEdge(O, kR, Top, rb::kCutRimEdge, rb::kInfinity, Numerics()).Found);
	RB_CHECK(!rb::PredictRailTopEdge(O, kR, Cap, rb::kCutRimEdge, rb::kInfinity, Numerics()).Found);
}

// -------------------------------------------------------------------------------------------------
// Whole tables (WP-2's BuildTableGeometry)
// -------------------------------------------------------------------------------------------------

RB_TEST(Integ_Detect_TableIsWatertightOnTheClothAndInFlight)
{
	LeakStats All;
	for (rb::TablePreset Preset : kPresets)
	{
		LeakStats S;
		RunLeaks(Preset, S);
		for (int M = 0; M < 3; ++M)
		{
			All.Cases[M] += S.Cases[M];
			All.Leaks[M] += S.Leaks[M];
			All.Hits[M] += S.Hits[M];
			All.HoleCorners[M] += S.HoleCorners[M];
		}
	}
	std::printf("  [watertight] cloth %d cases (%d events) %d leaks; flight %d cases (%d events) %d leaks (%d at the undefined hole corners); in the pocket %d "
				"cases %d leaks (%d at the hole corners)\n",
		All.Cases[Cloth], All.Hits[Cloth], All.Leaks[Cloth], All.Cases[Flight], All.Hits[Flight], All.Leaks[Flight], All.HoleCorners[Flight], All.Cases[InPocket],
		All.Leaks[InPocket], All.HoleCorners[InPocket]);
	RB_CHECK(All.Cases[Cloth] >= kLeakTrialsPerMode * 5);
	RB_CHECK(All.Cases[Flight] >= kLeakTrialsPerMode * 5);
	RB_CHECK(All.Hits[Cloth] * 2 >= All.Cases[Cloth]);
	RB_CHECK(All.Hits[Flight] * 2 >= All.Cases[Flight]);
	RB_CHECK(All.Leaks[Cloth] == 0);
	// In flight and inside the hole: only the known Level A gap over a pocket's hole (see the file comment), and rarely.
	RB_CHECK(All.Leaks[Flight] == All.HoleCorners[Flight]);
	RB_CHECK(All.HoleCorners[Flight] * 500 <= All.Cases[Flight]);
	RB_CHECK(All.Leaks[InPocket] == All.HoleCorners[InPocket]);
	RB_CHECK(All.Leaks[InPocket] * 10 <= All.Cases[InPocket]);
}

RB_TEST(Integ_Detect_DispatcherCullingMatchesExhaustiveEvaluation)
{
	int Cases = 0;
	int Hits = 0;
	int Diffs = 0;
	int Ties = 0;
	for (rb::TablePreset Preset : kPresets)
	{
		rb::TableGeometry T;
		RB_REQUIRE(rb::BuildTableGeometry(rb::GetTableSpec(Preset), T) == rb::ErrorCode::Ok);
		rb::Rng Rng(0xC011ull + static_cast<std::uint64_t>(Preset));
		for (int M = 0; M < 3; ++M)
		{
			for (int Trial = 0; Trial < kCullTrialsPerMode / 7 + 1; ++Trial)
			{
				rb::BallTableContext Ctx;
				const rb::PocketGeometry& P = T.Pockets[static_cast<int>(Rng.NextBelow(static_cast<std::uint32_t>(T.Pockets.Size())))];
				const MotionSegment S = RandomSegment(Rng, M, T, P, Ctx);
				const rb::FeaturePrediction D =
					rb::PredictTableEvent(S, rb::BallSpec{}, Ctx, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
				const Best E = Exhaustive(S, T, Ctx);
				++Cases;
				Hits += E.C.Found ? 1 : 0;
				// The pruned window changes the time scaling of later candidates, so an equal event may differ in the last bits; a
				// ridge shared by two rail-top polygons (the cushion top's and the cap's cushion-back edge) is then hit "first" as
				// either of them (the same line, the same contact frame).
				const bool SameFeature = D.Feature.Kind == E.F.Kind && D.Feature.Index == E.F.Index && D.Feature.SubIndex == E.F.SubIndex;
				const bool Same = D.Contact.Found == E.C.Found &&
					(!E.C.Found ||
						(rb::Abs(D.Contact.Time - E.C.Time) <= 1e-11 * rb::Max(1.0, E.C.Time) && D.Contact.Flags == E.C.Flags &&
							(SameFeature || (D.Feature.Kind == TableFeatureKind::RailTopEdge && E.F.Kind == TableFeatureKind::RailTopEdge))));
				Ties += E.C.Found && !SameFeature ? 1 : 0;
				if (!Same)
				{
					++Diffs;
					std::printf("  [%s] mode %d: dispatcher %d t=%.15g kind %d/%d/%d, exhaustive %d t=%.15g kind %d/%d/%d\n", rb::GetTableSpec(Preset).Name, M, D.Contact.Found,
						D.Contact.Time, static_cast<int>(D.Feature.Kind), D.Feature.Index, D.Feature.SubIndex, E.C.Found, E.C.Time, static_cast<int>(E.F.Kind), E.F.Index,
						E.F.SubIndex);
				}
			}
		}
	}
	std::printf("  [culling] %d cases, %d events, %d shared-ridge ties, %d differences\n", Cases, Hits, Ties, Diffs);
	RB_CHECK(Diffs == 0);
	RB_CHECK(Ties * 100 <= Cases);
	RB_CHECK(Hits * 2 >= Cases);
}

RB_TEST(Integ_Detect_Slow_TableEventThroughput)
{
	// Performance probe of the dispatcher on the built 9FT_PRO table (Release numbers are the relevant ones).
	rb::TableGeometry T;
	RB_REQUIRE(rb::BuildTableGeometry(rb::kTableNineFootPro, T) == rb::ErrorCode::Ok);
	rb::Rng Rng(0xBE4Cull);
	constexpr int Count = 2048;
	static MotionSegment Segs[3][Count];
	static rb::BallTableContext Ctx[3][Count];
	for (int M = 0; M < 3; ++M)
	{
		for (int i = 0; i < Count; ++i)
		{
			const rb::PocketGeometry& P = T.Pockets[static_cast<int>(Rng.NextBelow(6))];
			Segs[M][i] = RandomSegment(Rng, M, T, P, Ctx[M][i]);
		}
	}
	const char* Names[3] = {"cloth (rolling / sliding / tilt piece)", "airborne near a pocket or rail", "PocketFall"};
	for (int M = 0; M < 3; ++M)
	{
		int Found = 0;
		constexpr int Rounds = 20;
		const auto Start = std::chrono::steady_clock::now();
		for (int Round = 0; Round < Rounds; ++Round)
		{
			for (int i = 0; i < Count; ++i)
			{
				const rb::FeaturePrediction E =
					rb::PredictTableEvent(Segs[M][i], rb::BallSpec{}, Ctx[M][i], T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
				Found += E.Contact.Found ? 1 : 0;
			}
		}
		const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
		std::printf("  PredictTableEvent, %s: %.0f ns per call, %d of %d with an event\n", Names[M], Seconds * 1e9 / (Count * Rounds), Found / Rounds, Count);
		RB_CHECK(Found > 0);
	}
}
