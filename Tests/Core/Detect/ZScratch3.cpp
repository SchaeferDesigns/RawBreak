// TEMPORARY (not committed): dispatcher culling vs exhaustive evaluation on the built 9FT_PRO table (needs wp/2).
#include "rbtest.h"

#include "DetectTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/TableSpec.h"

#include <cstdio>

using namespace detecttest;
using rb::ContactPrediction;
using rb::MotionSegment;
using rb::TableFeatureKind;

namespace
{
	struct Best
	{
		ContactPrediction C;
		rb::TableFeatureRef F;
	};

	bool KeyLess(const rb::TableFeatureRef& A, const rb::TableFeatureRef& B)
	{
		if (A.Kind != B.Kind)
			return A.Kind < B.Kind;
		if (A.Index != B.Index)
			return A.Index < B.Index;
		return A.SubIndex < B.SubIndex;
	}

	void Take(Best& B, const ContactPrediction& C, TableFeatureKind K, int I, int S)
	{
		if (!C.Found)
			return;
		const rb::TableFeatureRef R{K, static_cast<std::uint8_t>(I), static_cast<std::uint8_t>(S)};
		if (!B.C.Found || C.Time < B.C.Time || (C.Time == B.C.Time && KeyLess(R, B.F)))
		{
			B.C = C;
			B.F = R;
		}
	}

	Best Exhaustive(const MotionSegment& S, const rb::TableGeometry& T, const rb::BallTableContext& Ctx)
	{
		Best B;
		const double Rc = rb::ComputeCushionContact(kR, kNoseH, 0.0, false).HorizontalOffset;
		const rb::NumericsConfig N = Numerics();
		if (S.State == rb::MotionState::Rolling || S.State == rb::MotionState::Sliding)
		{
			for (int i = 0; i < T.Noses.Size(); ++i)
				Take(B, rb::PredictNoseOnCloth(S, kR, T.Noses[i], rb::ComputeCushionContact(kR, T.Noses[i].Height, 0.0, false).HorizontalOffset, rb::kInfinity, N),
					TableFeatureKind::NoseSegment, i, 0);
			for (int i = 0; i < T.JawArcs.Size(); ++i)
				Take(B, rb::PredictJawArcOnCloth(S, kR, T.JawArcs[i], rb::ComputeCushionContact(kR, T.JawArcs[i].Height, 0.0, false).HorizontalOffset, rb::kInfinity, N),
					TableFeatureKind::JawArc, i, 0);
			for (int i = 0; i < T.Facings.Size(); ++i)
				Take(B, rb::PredictFacingOnShelf(S, kR, T.Facings[i], rb::FacingContactOffset(kR, T.Facings[i].TopHeight, T.Facings[i].Backdraft), rb::kInfinity, N),
					TableFeatureKind::FacingFace, i, 0);
			for (int p = 0; p < T.Pockets.Size(); ++p)
				Take(B, rb::PredictDropEdge(S, kR, T.Pockets[p], rb::kInfinity, N), TableFeatureKind::DropEdge, p, 0);
			(void)Rc;
			return B;
		}
		const bool Air = S.State == rb::MotionState::Airborne;
		for (int i = 0; Air && i < T.Noses.Size(); ++i)
			if (T.Noses[i].Present)
				Take(B, rb::PredictNoseAirborne(S, kR, T.Noses[i], 0.0, rb::kInfinity, N), TableFeatureKind::NoseSegment, i, 0);
		for (int i = 0; i < T.JawArcs.Size(); ++i)
			if (Air || i / 2 == static_cast<int>(Ctx.Pocket))
				Take(B, rb::PredictJawArcAirborne(S, kR, T.JawArcs[i], rb::kInfinity, N), TableFeatureKind::JawArc, i, 0);
		for (int i = 0; i < T.Facings.Size(); ++i)
			if (Air || i / 2 == static_cast<int>(Ctx.Pocket))
			{
				Take(B, rb::PredictFacingAirborne(S, kR, T.Facings[i], rb::kInfinity, N), TableFeatureKind::FacingFace, i, 0);
				Take(B, rb::PredictFacingTopEdge(S, kR, T.Facings[i], rb::kInfinity, N), TableFeatureKind::FacingTopEdge, i, 0);
			}
		for (int p = 0; p < T.Pockets.Size(); ++p)
			if (Air || p == static_cast<int>(Ctx.Pocket))
			{
				Take(B, rb::PredictLinerWall(S, kR, T.Pockets[p], rb::kInfinity, N), TableFeatureKind::LinerWall, p, 0);
				Take(B, rb::PredictRimTorus(S, kR, T.Pockets[p], rb::kInfinity, N), TableFeatureKind::RimTorus, p, 0);
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
				Take(B, rb::PredictRailTopEdge(S, kR, Poly, e, rb::kInfinity, N), TableFeatureKind::RailTopEdge, i, e);
			if (Poly.HasCut)
				Take(B, rb::PredictRailTopEdge(S, kR, Poly, rb::kCutRimEdge, rb::kInfinity, N), TableFeatureKind::RailTopEdge, i, rb::kCutRimEdge);
		}
		if (Air)
			Take(B, rb::PredictOuterBoundary(S, T.OuterBoundary, rb::kInfinity), TableFeatureKind::OuterBoundary, 0, 0);
		return B;
	}
}

namespace
{
	bool Arc(const Vec2& D, double From, double Sweep, double Slack)
	{
		if (Sweep + 2 * Slack >= rb::kTwoPi)
			return true;
		double Delta = std::atan2(D.y, D.x) - (From - Slack);
		Delta -= rb::kTwoPi * std::floor(Delta / rb::kTwoPi);
		return Delta <= Sweep + 2 * Slack;
	}

	bool InPoly(const rb::RailTopPolygon& Poly, const Vec2& X)
	{
		for (int i = 0; i < Poly.VertexCount; ++i)
		{
			const Vec2 A = Poly.Vertices[i], B = Poly.Vertices[(i + 1) % Poly.VertexCount];
			if (rb::Cross(B - A, X - A) < -1e-9 * rb::Length(B - A))
				return false;
		}
		return true;
	}

	double PlaneZ(const rb::RailTopPolygon& Poly, const Vec2& P)
	{
		const Vec3& N = Poly.PlaneNormal;
		return Poly.PlanePoint.z - (N.x * (P.x - Poly.PlanePoint.x) + N.y * (P.y - Poly.PlanePoint.y)) / N.z;
	}

	// Deepest valid penetration over all features at position X (state class: 0 cloth, 1 flight / pocket).
	double Penetration(const Vec3& X, int Cls, const rb::TableGeometry& T, char* What)
	{
		double Worst = 0.0;
		const auto Note = [&](double G, const char* Name, int I)
		{
			if (G < Worst)
			{
				Worst = G;
				std::snprintf(What, 64, "%s %d", Name, I);
			}
		};
		const double Rc = rb::ComputeCushionContact(kR, kNoseH, 0.0, false).HorizontalOffset;
		for (int i = 0; i < T.Noses.Size(); ++i)
		{
			const rb::NoseSegment& N = T.Noses[i];
			if (!N.Present)
				continue;
			const Vec3 W = X - rb::ToVec3(N.Start, N.Height);
			const Vec3 D = rb::ToVec3(N.Direction);
			const double s = rb::Dot(D, W);
			if (s < 0 || s > N.Length)
				continue;
			if (Cls == 0)
				Note(rb::Dot(rb::ToVec3(N.InwardNormal), W) - Rc, "nose", i);
			else if (rb::Dot(rb::ToVec3(N.InwardNormal), W) >= 0)
				Note(rb::Length(W - D * s) - kR, "noseA", i);
		}
		for (int i = 0; i < T.JawArcs.Size(); ++i)
		{
			const rb::JawArc& J = T.JawArcs[i];
			const Vec2 H = rb::XY(X) - J.Center;
			if (!Arc(H, J.AngleFrom, J.AngleSweep, 0.0))
				continue;
			if (Cls == 0)
				Note(rb::Length(H) - (J.Radius + Rc), "jaw", i);
			else
				Note(std::sqrt(rb::Square(rb::Length(H) - J.Radius) + rb::Square(X.z - J.Height)) - kR, "jawA", i);
		}
		for (int i = 0; i < T.Facings.Size(); ++i)
		{
			const rb::Facing& F = T.Facings[i];
			const Vec3 W = X - rb::ToVec3(F.Start, F.TopHeight);
			const Vec3 D = rb::ToVec3(F.Direction);
			const double s = rb::Dot(D, W);
			if (s < 0 || s > F.Length)
				continue;
			if (Cls == 0)
				Note(rb::Dot(rb::ToVec3(F.PocketNormal), W) - rb::FacingContactOffset(kR, F.TopHeight, F.Backdraft), "facing", i);
			else
			{
				const Vec3 N = rb::ToVec3(F.PocketNormal) * std::cos(F.Backdraft) - Vec3{0, 0, std::sin(F.Backdraft)};
				const double Cz = X.z - kR * N.z;
				if (Cz >= 0 && Cz <= F.TopHeight)
					Note(rb::Dot(N, W) - kR, "faceA", i);
				if (rb::Dot(rb::ToVec3(F.PocketNormal), W) >= 0)
					Note(rb::Length(W - D * s) - kR, "faceTop", i);
			}
		}
		if (Cls != 0)
		{
			for (int p = 0; p < T.Pockets.Size(); ++p)
			{
				const rb::PocketGeometry& P = T.Pockets[p];
				const Vec2 H = rb::XY(X) - P.CaptureCenter;
				const double Rho = rb::Length(H);
				const bool Front = Arc(H, P.FrontArcFrom, P.FrontArcSweep, 0.0);
				if (Rho < P.CaptureRadius + 0.02 && (Front ? X.z < -P.DropRadius : X.z <= P.WallTopZ))
					Note((P.CaptureRadius - kR) - Rho, "liner", p);
				if (Front && Rho <= P.DropEdgeRadius && X.z >= -P.DropRadius)
					Note(std::sqrt(rb::Square(Rho - P.DropEdgeRadius) + rb::Square(X.z + P.DropRadius)) - (kR + P.DropRadius), "torus", p);
			}
			for (int i = 0; i < T.RailTops.Size(); ++i)
			{
				const rb::RailTopPolygon& Poly = T.RailTops[i];
				const Vec3 Foot = X - Poly.PlaneNormal * kR;
				if (InPoly(Poly, rb::XY(Foot)) && (!Poly.HasCut || rb::Length(rb::XY(Foot) - Poly.CutCenter) >= Poly.CutRadius))
					Note(rb::Dot(Poly.PlaneNormal, X - Poly.PlanePoint) - kR, "plane", i);
				for (int e = 0; e < Poly.VertexCount; ++e)
				{
					if (!(Poly.Edges[e] == rb::RailEdgeKind::CushionBack || Poly.Edges[e] == rb::RailEdgeKind::OuterEdge || Poly.Edges[e] == rb::RailEdgeKind::Facing))
						continue;
					const Vec2 V0 = Poly.Vertices[e], V1 = Poly.Vertices[(e + 1) % Poly.VertexCount];
					const Vec3 E0 = rb::ToVec3(V0, PlaneZ(Poly, V0)), E1 = rb::ToVec3(V1, PlaneZ(Poly, V1));
					const double L = rb::Length(E1 - E0);
					const Vec3 D = (E1 - E0) / L;
					const Vec3 W = X - E0;
					const double s = rb::Dot(D, W);
					if (s < 0 || s > L || rb::Dot(rb::Cross(D, Poly.PlaneNormal), W) < 0)
						continue;
					Note(rb::Length(W - D * s) - kR, "rtEdge", i * 100 + e);
				}
				if (Poly.HasCut)
				{
					const Vec2 H = rb::XY(X) - Poly.CutCenter;
					const double Rho = rb::Length(H);
					const double Zc = PlaneZ(Poly, Poly.CutCenter);
					if (Rho <= Poly.CutRadius && X.z >= Zc && (Rho == 0 || InPoly(Poly, Poly.CutCenter + H * (Poly.CutRadius / Rho))))
						Note(std::sqrt(rb::Square(Rho - Poly.CutRadius) + rb::Square(X.z - Zc)) - kR, "cutRim", i);
				}
			}
		}
		return Worst;
	}
}

RB_TEST(Integ_Scratch3_LeakTest)
{
	rb::TableGeometry T;
	RB_REQUIRE(rb::BuildTableGeometry(rb::kTableNineFootPro, T) == rb::ErrorCode::Ok);
	rb::Rng Rng(4242);
	int Cases[3] = {0, 0, 0}, Leaks[3] = {0, 0, 0};
	for (int Trial = 0; Trial < 40000; ++Trial)
	{
		const int Mode = static_cast<int>(Rng.NextBelow(3));
		MotionSegment S;
		rb::BallTableContext Ctx;
		const rb::PocketGeometry& P = T.Pockets[Rng.NextBelow(6)];
		if (Mode == 0)
		{
			const Vec3 Start = rb::ToVec3(P.MouthMid, kR) - rb::ToVec3(P.Axis) * Rng.NextUniform(0.03, 0.3) + Vec3{Rng.NextUniform(-0.1, 0.1), Rng.NextUniform(-0.1, 0.1), 0.0};
			if (rb::Abs(Start.x) > 1.27 - kR - 0.001 || rb::Abs(Start.y) > 0.635 - kR - 0.001)
				continue;
			const double A = Rng.NextUniform(0.0, rb::kTwoPi);
			S = Rolling(Start, Vec3{std::cos(A), std::sin(A), 0.0} * Rng.NextUniform(0.05, 5.0));
		}
		else if (Mode == 1)
		{
			const Vec3 Start = rb::ToVec3(P.MouthMid, kR + Rng.NextUniform(0.0, 0.1)) - rb::ToVec3(P.Axis) * Rng.NextUniform(0.0, 0.4) +
				Vec3{Rng.NextUniform(-0.15, 0.15), Rng.NextUniform(-0.15, 0.15), 0.0};
			if (rb::Abs(Start.x) > 1.27 - kR - 0.001 || rb::Abs(Start.y) > 0.635 - kR - 0.001)
				continue;
			const double A = Rng.NextUniform(0.0, rb::kTwoPi);
			S = Airborne(Start, Vec3{std::cos(A), std::sin(A), 0.0} * Rng.NextUniform(0.1, 6.0) + Vec3{0.0, 0.0, Rng.NextUniform(0.0, 3.0)});
		}
		else
		{
			const Vec2 H = P.CaptureCenter + Vec2{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)} * 0.03;
			const Vec3 D{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-0.3, 1)};
			S = PocketFall({H.x, H.y, Rng.NextUniform(-0.02, kR)}, D * Rng.NextUniform(0.0, 3.0));
			S.TauEnd = 0.5;
			Ctx.Pocket = P.Id;
		}
		char Start[64] = "";
		if (Penetration(S.Pos0, Mode == 0 ? 0 : 1, T, Start) < -1e-9)
			continue; // starts inside something
		const rb::FeaturePrediction E = rb::PredictTableEvent(S, rb::BallSpec{}, Ctx, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
		const double End = E.Contact.Found ? E.Contact.Time - S.T0 : rb::Min(S.TauEnd, 2.0);
		++Cases[Mode];
		for (int i = 1; i <= 2000; ++i)
		{
			const double Tau = End * i / 2000.0 - 1e-9;
			char What[64] = "";
			const double Pen = Penetration(rb::PositionAt(S, Tau), Mode == 0 ? 0 : 1, T, What);
			if (Pen < -1e-6)
			{
				++Leaks[Mode];
				if (Trial == 8 || Trial == 33918 || Trial == 289 || Trial == 523)
				{
					int PolyI = 0, EdgeI = 0;
					if (sscanf_s(What, "rtEdge %d", &PolyI) == 1)
					{
						EdgeI = PolyI % 100;
						PolyI /= 100;
						const rb::RailTopPolygon& Poly = T.RailTops[PolyI];
						const ContactPrediction C = rb::PredictRailTopEdge(S, kR, Poly, EdgeI, rb::kInfinity, Numerics());
						std::printf("    poly %d kind %d edgekind %d hascut %d cut (%.5f %.5f) r %.4f pocket %d | pred found %d t %.6g flags %d\n", PolyI, (int)Poly.Kind, (int)Poly.Edges[EdgeI], Poly.HasCut,
							Poly.CutCenter.x, Poly.CutCenter.y, Poly.CutRadius, (int)Poly.Pocket, C.Found, C.Time, C.Flags);
						for (int v = 0; v < Poly.VertexCount; ++v)
							std::printf("      v%d (%.5f %.5f) z %.5f edge %d\n", v, Poly.Vertices[v].x, Poly.Vertices[v].y, PlaneZ(Poly, Poly.Vertices[v]), (int)Poly.Edges[v]);
					}
					std::printf("    start (%.5f %.5f %.5f) vel (%.4f %.4f %.4f) capcenter (%.5f %.5f) rp %.4f wall %.4f\n", S.Pos0.x, S.Pos0.y, S.Pos0.z, S.Vel0.x, S.Vel0.y, S.Vel0.z,
						P.CaptureCenter.x, P.CaptureCenter.y, P.CaptureRadius, P.WallTopZ);
				}
				if (Leaks[Mode] <= 12)
				{
					const Vec3 X = rb::PositionAt(S, Tau);
					std::printf("  LEAK mode %d trial %d: tau %.6g / event %.6g (kind %d idx %d) into %s by %.3g at (%.5f %.5f %.5f) pocket %d\n", Mode, Trial, Tau, End,
						E.Contact.Found ? (int)E.Feature.Kind : -1, E.Feature.Index, What, -Pen, X.x, X.y, X.z, (int)P.Id);
				}
				break;
			}
		}
	}
	for (int m = 0; m < 3; ++m)
		std::printf("  mode %d: %d cases %d leaks\n", m, Cases[m], Leaks[m]);
}

RB_TEST(Integ_Scratch3_DispatcherCullingMatchesExhaustive)
{
	rb::TableGeometry T;
	RB_REQUIRE(rb::BuildTableGeometry(rb::kTableNineFootPro, T) == rb::ErrorCode::Ok);
	std::printf("  noses %d jaws %d facings %d pockets %d railtops %d\n", T.Noses.Size(), T.JawArcs.Size(), T.Facings.Size(), T.Pockets.Size(), T.RailTops.Size());
	rb::Rng Rng(99);
	int Cases[3] = {0, 0, 0}, Diff[3] = {0, 0, 0}, Hits[3] = {0, 0, 0};
	for (int Trial = 0; Trial < 60000; ++Trial)
	{
		const int Mode = static_cast<int>(Rng.NextBelow(3)); // 0 cloth, 1 airborne, 2 pocket fall
		MotionSegment S;
		rb::BallTableContext Ctx;
		const int Pk = static_cast<int>(Rng.NextBelow(6));
		const rb::PocketGeometry& P = T.Pockets[Pk];
		if (Mode == 0)
		{
			// near a pocket mouth or a rail
			const Vec3 Start = Rng.NextBelow(2) ? rb::ToVec3(P.MouthMid, kR) - rb::ToVec3(P.Axis) * Rng.NextUniform(0.03, 0.4) + Vec3{Rng.NextUniform(-0.1, 0.1), Rng.NextUniform(-0.1, 0.1), 0.0}
												: Vec3{Rng.NextUniform(-1.2, 1.2), Rng.NextUniform(-0.6, 0.6), kR};
			if (rb::Abs(Start.x) > 1.27 - kR - 0.001 || rb::Abs(Start.y) > 0.635 - kR - 0.001)
				continue;
			const double A = Rng.NextUniform(0.0, rb::kTwoPi);
			const Vec3 V = Vec3{std::cos(A), std::sin(A), 0.0} * Rng.NextUniform(0.05, 5.0);
			if (Rng.NextBelow(2))
				S = Rolling(Start, V);
			else
			{
				const Vec3 W{Rng.NextUniform(-150, 150), Rng.NextUniform(-150, 150), Rng.NextUniform(-50, 50)};
				if (rb::Length(rb::Planar(rb::SlipVelocity(V, W, kR))) < 1e-6)
					continue;
				S = Sliding(Start, V, W);
			}
		}
		else if (Mode == 1)
		{
			const Vec3 Start = rb::ToVec3(P.MouthMid, kR + Rng.NextUniform(0.0, 0.1)) - rb::ToVec3(P.Axis) * Rng.NextUniform(0.0, 0.5) +
				Vec3{Rng.NextUniform(-0.15, 0.15), Rng.NextUniform(-0.15, 0.15), 0.0};
			if (rb::Abs(Start.x) > 1.27 - kR - 0.001 || rb::Abs(Start.y) > 0.635 - kR - 0.001)
				continue;
			const double A = Rng.NextUniform(0.0, rb::kTwoPi);
			S = Airborne(Start, Vec3{std::cos(A), std::sin(A), 0.0} * Rng.NextUniform(0.1, 6.0) + Vec3{0.0, 0.0, Rng.NextUniform(0.0, 3.0)});
		}
		else
		{
			const Vec2 H = P.CaptureCenter + Vec2{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)} * 0.03;
			const Vec3 D{Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1), Rng.NextUniform(-1, 1)};
			S = PocketFall({H.x, H.y, Rng.NextUniform(-0.02, kR)}, D * Rng.NextUniform(0.0, 3.0));
			Ctx.Pocket = P.Id;
		}
		const rb::FeaturePrediction D = rb::PredictTableEvent(S, rb::BallSpec{}, Ctx, T, rb::EnvironmentSpec{}, rb::DetectOptions{}, kG, rb::kInfinity, Numerics());
		const Best E = Exhaustive(S, T, Ctx);
		++Cases[Mode];
		Hits[Mode] += E.C.Found;
		const bool Same = D.Contact.Found == E.C.Found && (!E.C.Found || (D.Contact.Time == E.C.Time && D.Feature.Kind == E.F.Kind && D.Feature.Index == E.F.Index && D.Feature.SubIndex == E.F.SubIndex));
		if (!Same)
		{
			// equal times with t_best pruning can differ in the last bits: accept 1e-12 and same kind
			const bool Close = D.Contact.Found && E.C.Found && rb::Abs(D.Contact.Time - E.C.Time) < 1e-12;
			if (!Close)
			{
				++Diff[Mode];
				if (Diff[Mode] <= 5)
					std::printf("  mode %d trial %d: disp %d t=%.12g kind %d idx %d sub %d | exh %d t=%.12g kind %d idx %d sub %d\n", Mode, Trial, D.Contact.Found, D.Contact.Time,
						(int)D.Feature.Kind, D.Feature.Index, D.Feature.SubIndex, E.C.Found, E.C.Time, (int)E.F.Kind, E.F.Index, E.F.SubIndex);
			}
		}
	}
	for (int m = 0; m < 3; ++m)
		std::printf("  mode %d: %d cases %d hits %d diffs\n", m, Cases[m], Hits[m], Diff[m]);
}
