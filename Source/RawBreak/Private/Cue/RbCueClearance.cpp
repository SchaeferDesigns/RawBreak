#include "Cue/RbCueClearance.h"

#include "Balls/RbBallSet.h"
#include "Cue/RbCue.h"
#include "Table/RbTable.h"

#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "WorldCollision.h"

#include "rb/Physics/BallState.h"
#include "rb/Physics/CueStrike.h"

// Owner: UE-4. Minimum elevation (plan 5.5, T13): analytic balls + rail-top planes of rb::TableGeometry, then the
// environment capsule sweep. Everything analytic is core frame, metres, radians (RbCueClearance.h).
// The helpers live in RbCueClearance's own anonymous namespace, not the global one (unity builds share one translation unit
// between files with file-local helpers of the same names, e.g. Bisect).

namespace RbCueClearance
{
namespace
{
	constexpr double kCoarseStep = 0.25 * UE_DOUBLE_PI / 180.0;     // plan 5.5
	constexpr double kResolution = 0.01 * UE_DOUBLE_PI / 180.0;     // plan 5.5
	constexpr double kEnvironmentStep = 1.0 * UE_DOUBLE_PI / 180.0; // coarse step of the (physics-query) environment test
	constexpr double kCapsuleSegment = 0.30;                        // [m] capsule chain segment length
	constexpr double kShortCueLengths[3] = {1.3208, 1.2192, 0.9144}; // bar short cues 52 / 48 / 36 in (equipment 11.1)

	double MaxElevationOf(const FRbCueClearanceInput& Input)
	{
		return FMath::Clamp(FMath::IsFinite(Input.MaxElevation) ? Input.MaxElevation : 85.0 * UE_DOUBLE_PI / 180.0, 0.0,
			0.5 * UE_DOUBLE_PI - 1e-6);
	}

	double CueLengthOf(const FRbCueClearanceInput& Input)
	{
		return FMath::IsFinite(Input.CueLength) && Input.CueLength > 0.0 ? Input.CueLength : 1.4732;
	}

	double BackswingOf(const FRbCueClearanceInput& Input)
	{
		return FMath::IsFinite(Input.Backswing) ? FMath::Max(0.0, Input.Backswing) : 0.0;
	}

	double MarginOf(const FRbCueClearanceInput& Input)
	{
		return FMath::IsFinite(Input.Margin) ? Input.Margin : 0.0;
	}

	// A non-finite pose or body cannot be cleared meaningfully: no floor (the requested elevation) instead of a sweep to the limit.
	bool PoseFinite(const FRbCueClearanceInput& Input, const rb::human::CueBodyState& Body)
	{
		return FMath::IsFinite(Input.ContactPoint.x) && FMath::IsFinite(Input.ContactPoint.y) && FMath::IsFinite(Input.ContactPoint.z) &&
			FMath::IsFinite(Input.Azimuth) && FMath::IsFinite(Body.TipRadius) && FMath::IsFinite(Body.ButtRadius);
	}

	bool HasCueBall(const FRbCueClearanceInput& Input)
	{
		return Input.BallPositions && Input.CueBall >= 0 && Input.CueBall < Input.BallCount && (!Input.InPlay || Input.InPlay[Input.CueBall]);
	}

	// Cyrus-Beck clip of the plan line Origin + Dir s, s in [Lo, Hi], against a convex CCW polygon (inside = left of every edge).
	bool ClipToPolygon(const rb::RailTopPolygon& Poly, const rb::Vec2& Origin, const rb::Vec2& Dir, double& Lo, double& Hi)
	{
		for (int32 Index = 0; Index < Poly.VertexCount; ++Index)
		{
			const rb::Vec2& V0 = Poly.Vertices[Index];
			const rb::Vec2& V1 = Poly.Vertices[(Index + 1) % Poly.VertexCount];
			const rb::Vec2 Inward(-(V1.y - V0.y), V1.x - V0.x);
			const double Num = rb::Dot(Inward, Origin - V0);
			const double Den = rb::Dot(Inward, Dir);
			if (FMath::Abs(Den) < 1e-15)
			{
				if (Num < 0.0)
				{
					return false;
				}
				continue;
			}
			const double T = -Num / Den;
			if (Den > 0.0)
			{
				Lo = FMath::Max(Lo, T);
			}
			else
			{
				Hi = FMath::Min(Hi, T);
			}
			if (Lo > Hi)
			{
				return false;
			}
		}
		return Lo <= Hi;
	}

	bool InsidePolygon(const rb::RailTopPolygon& Poly, const rb::Vec2& Q)
	{
		for (int32 Index = 0; Index < Poly.VertexCount; ++Index)
		{
			const rb::Vec2& V0 = Poly.Vertices[Index];
			const rb::Vec2& V1 = Poly.Vertices[(Index + 1) % Poly.VertexCount];
			if (rb::Dot(rb::Vec2(-(V1.y - V0.y), V1.x - V0.x), Q - V0) < 0.0)
			{
				return false;
			}
		}
		return true;
	}

	bool InsideCut(const rb::RailTopPolygon& Poly, const rb::Vec2& Q)
	{
		const rb::Vec2 W = Q - Poly.CutCenter;
		return Poly.HasCut && rb::Dot(W, W) < Poly.CutRadius * Poly.CutRadius;
	}

	// Splits [Lo, Hi] of the plan line Q0 + D t into the parts outside the polygon's pocket cut disc (the hole through the rail
	// carries no surface). Returns the number of parts (0 .. 2).
	int32 OutsideCut(const rb::RailTopPolygon& Poly, const rb::Vec2& Q0, const rb::Vec2& D, double Lo, double Hi, double (&Out)[2][2])
	{
		Out[0][0] = Lo;
		Out[0][1] = Hi;
		if (!Poly.HasCut)
		{
			return 1;
		}
		const rb::Vec2 W = Q0 - Poly.CutCenter;
		const double A = rb::Dot(D, D);
		const double C = rb::Dot(W, W) - Poly.CutRadius * Poly.CutRadius;
		if (A < 1e-24)
		{
			return C < 0.0 ? 0 : 1;
		}
		const double Bh = rb::Dot(D, W);
		const double Disc = Bh * Bh - A * C;
		if (Disc <= 0.0)
		{
			return 1;
		}
		const double Root = FMath::Sqrt(Disc);
		const double T0 = (-Bh - Root) / A;
		const double T1 = (-Bh + Root) / A;
		int32 Count = 0;
		if (FMath::Min(Hi, T0) >= Lo)
		{
			Out[Count][0] = Lo;
			Out[Count][1] = FMath::Min(Hi, T0);
			++Count;
		}
		if (FMath::Max(Lo, T1) <= Hi)
		{
			Out[Count][0] = FMath::Max(Lo, T1);
			Out[Count][1] = Hi;
			++Count;
		}
		return Count;
	}

	// One trial pose in plan coordinates: q = Origin + Dir s + Right y, s along the body toward the butt (|Dir| = cos theta), y
	// lateral; swept radius r(s) = Rt + K min(s, L) over s in [0, SMax]. The underside over q lies at
	// z_axis(s) - sqrt(r(s)^2 - y^2) / cos(theta) (vertical section of the inclined body; plan 5.5's r(s) / cos(theta) at y = 0).
	struct FRailFrame
	{
		rb::Vec2 Origin;
		rb::Vec2 Dir;
		rb::Vec2 Right;
		double CosT = 1.0;
		double SinT = 0.0;
		double RimZ = 0.0;
		double L = 1.0;
		double SMax = 1.0;
		double Rt = 0.0;
		double K = 0.0;

		double Radius(double S) const { return Rt + K * FMath::Clamp(S, 0.0, L); }
		double SOf(const rb::Vec2& Q) const { return rb::Dot(Q - Origin, Dir) / (CosT * CosT); }
		double YOf(const rb::Vec2& Q) const { return rb::Dot(Q - Origin, Right); }
		rb::Vec2 At(double S, double Y) const { return Origin + Dir * S + Right * Y; }
	};

	struct FRailPlane
	{
		double Z0 = 0.0;
		double X0 = 0.0;
		double Y0 = 0.0;
		double GX = 0.0;
		double GY = 0.0;

		double At(const rb::Vec2& Q) const { return Z0 + GX * (Q.x - X0) + GY * (Q.y - Y0); }
	};

	// Smallest rail gap (no margin) of the swept body of Pose over every rail-top plane: the exact minimum of the underside
	// height above the plane over the part of the body's plan footprint (|y| <= r(s), 0 <= s <= SMax) that lies over the
	// polygon minus its pocket cut. The gap is convex over the footprint (a convex body's underside minus a plane), so the
	// minimum is either the unconstrained one (the worst point across the width, at s = 0, L or SMax) when that lies over the
	// surface, or on the region's boundary: the polygon edges (closed-form stationary point of the convex restriction), the
	// footprint's silhouettes y = +-r(s) (linear there: the ends of the parts over the surface), its end sections s = 0 / SMax
	// (closed form) and the rim of the pocket cut (64 chords). A test along the axis alone misses the side of the cue passing
	// over a cushion while the axis is still over the bed (a cue crossing a rail at a shallow angle: up to 2.4 deg too low).
	void EvaluateRails(const FRbTableContext& Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input, const FRbCuePose& Pose,
		double Elevation, FRbCueGap& InOut)
	{
		FRailFrame F;
		F.L = CueLengthOf(Input);
		F.SMax = F.L + BackswingOf(Input);
		F.CosT = FMath::Max(FMath::Cos(Elevation), 1e-6);
		F.SinT = FMath::Sin(Elevation);
		F.RimZ = Pose.Rim.z;
		F.Origin = rb::Vec2(Pose.Rim.x, Pose.Rim.y);
		F.Dir = rb::Vec2(-Pose.Direction.x, -Pose.Direction.y); // plan direction of increasing s (toward the butt)
		F.Right = rb::Vec2(FMath::Sin(Input.Azimuth), -FMath::Cos(Input.Azimuth));
		F.Rt = RbCueClearance::EnvelopeRadius(Body, F.L, 0.0);
		F.K = (RbCueClearance::EnvelopeRadius(Body, F.L, F.L) - F.Rt) / F.L;
		const double SPiece = FMath::Min(F.L, F.SMax);
		const double RMax = FMath::Max(F.Radius(0.0), F.Radius(F.SMax));

		FRailPlane Plane;
		auto Consider = [&](const rb::Vec2& Q)
		{
			const double S = F.SOf(Q);
			const double Y = F.YOf(Q);
			const double R = F.Radius(S);
			const double Gap = F.RimZ + F.SinT * S - Plane.At(Q) - FMath::Sqrt(FMath::Max(0.0, R * R - Y * Y)) / F.CosT;
			if (Gap < InOut.Gap)
			{
				InOut.Gap = Gap;
				InOut.By = rb::human::FloorSource::Rail;
				InOut.Ball = -1;
				InOut.S = FMath::Clamp(S, 0.0, F.SMax);
			}
		};

		// Minimum of the (convex) gap along the plan line Q0 + D t, t in [Lo, Hi], inside the footprint. Per piece of the swept
		// radius (taper s <= L, backswing cylinder s >= L) the footprint bounds are linear in t and the gap is
		// alpha + beta t - sqrt(Q(t)) / cos(theta), Q = (p0 + p1 t)^2 - (y0 + dy t)^2 = a t^2 + b t + c: its stationary points are
		// roots of a (a - m^2) t^2 + b (a - m^2) t + b^2 / 4 - m^2 c = 0 (m = beta cos(theta)). The ends and every root inside are
		// evaluated (a spurious root is still a point of the footprint, so the minimum stays exact).
		auto MinOnLine = [&](const rb::Vec2& Q0, const rb::Vec2& D, double Lo, double Hi)
		{
			const double S0 = F.SOf(Q0);
			const double DS = rb::Dot(D, F.Dir) / (F.CosT * F.CosT);
			const double Y0 = F.YOf(Q0);
			const double DY = rb::Dot(D, F.Right);
			const double Beta = F.SinT * DS - (Plane.GX * D.x + Plane.GY * D.y);
			const double M2 = Beta * F.CosT * Beta * F.CosT;
			// {s start, s end, r at s = 0 of the piece's linear law, dr / ds}
			const double Pieces[2][4] = {{0.0, SPiece, F.Rt, F.K}, {F.L, F.SMax, F.Radius(F.L), 0.0}};
			for (const auto& Piece : Pieces)
			{
				if (Piece[1] < Piece[0])
				{
					continue;
				}
				double A = Lo;
				double B = Hi;
				auto Constrain = [&A, &B](double C0, double C1) // C0 + C1 t <= 0
				{
					if (FMath::Abs(C1) < 1e-300)
					{
						if (C0 > 1e-15)
						{
							B = A - 1.0;
						}
						return;
					}
					const double T = -C0 / C1;
					if (C1 > 0.0)
					{
						B = FMath::Min(B, T);
					}
					else
					{
						A = FMath::Max(A, T);
					}
				};
				const double P0 = Piece[2] + Piece[3] * S0; // r at t = 0 (this piece's linear law)
				const double P1 = Piece[3] * DS;
				Constrain(Piece[0] - S0, -DS); // s >= start
				Constrain(S0 - Piece[1], DS);  // s <= end
				Constrain(Y0 - P0, DY - P1);   // y <= r(s)
				Constrain(-Y0 - P0, -DY - P1); // -y <= r(s)
				if (A > B)
				{
					continue;
				}
				Consider(Q0 + D * A);
				Consider(Q0 + D * B);
				const double Qa = P1 * P1 - DY * DY;
				const double Qb = 2.0 * (P0 * P1 - Y0 * DY);
				const double Qc = P0 * P0 - Y0 * Y0;
				const double Ea = Qa * (Qa - M2);
				const double Eb = Qb * (Qa - M2);
				const double Ec = 0.25 * Qb * Qb - M2 * Qc;
				const double Scale = FMath::Max3(FMath::Abs(Ea), FMath::Abs(Eb), FMath::Abs(Ec));
				if (!(Scale > 0.0))
				{
					continue;
				}
				double Roots[2];
				int32 RootCount = 0;
				if (FMath::Abs(Ea) > 1e-14 * Scale)
				{
					const double Disc = Eb * Eb - 4.0 * Ea * Ec;
					if (Disc >= 0.0)
					{
						const double Q = -0.5 * (Eb + (Eb >= 0.0 ? 1.0 : -1.0) * FMath::Sqrt(Disc));
						Roots[RootCount++] = Q / Ea;
						if (Q != 0.0)
						{
							Roots[RootCount++] = Ec / Q;
						}
					}
				}
				else if (FMath::Abs(Eb) > 1e-14 * Scale)
				{
					Roots[RootCount++] = -Ec / Eb;
				}
				for (int32 Index = 0; Index < RootCount; ++Index)
				{
					if (Roots[Index] > A && Roots[Index] < B)
					{
						Consider(Q0 + D * Roots[Index]);
					}
				}
			}
		};

		double Parts[2][2];
		for (const rb::RailTopPolygon& Poly : Table.Geometry.RailTops)
		{
			if (Poly.VertexCount < 3 || Poly.PlaneNormal.z <= 1e-9)
			{
				continue;
			}
			// Quick reject: the polygon lies entirely beside, ahead of or behind the footprint.
			{
				double SLo = TNumericLimits<double>::Max();
				double SHi = -TNumericLimits<double>::Max();
				double YLo = TNumericLimits<double>::Max();
				double YHi = -TNumericLimits<double>::Max();
				for (int32 Index = 0; Index < Poly.VertexCount; ++Index)
				{
					const double S = F.SOf(Poly.Vertices[Index]);
					const double Y = F.YOf(Poly.Vertices[Index]);
					SLo = FMath::Min(SLo, S);
					SHi = FMath::Max(SHi, S);
					YLo = FMath::Min(YLo, Y);
					YHi = FMath::Max(YHi, Y);
				}
				if (YLo > RMax || YHi < -RMax || SHi < 0.0 || SLo > F.SMax)
				{
					continue;
				}
			}
			const rb::Vec3& N = Poly.PlaneNormal;
			Plane.Z0 = Poly.PlanePoint.z;
			Plane.X0 = Poly.PlanePoint.x;
			Plane.Y0 = Poly.PlanePoint.y;
			Plane.GX = -N.x / N.z;
			Plane.GY = -N.y / N.z;

			// 1. Unconstrained minimum: the worst point across the width (y = u r, u = g cos / sqrt(1 + g^2 cos^2), g = the plane's
			//    slope across the cue) at s = 0, L or SMax (the gap of that point is piecewise linear in s).
			{
				const double G = Plane.GX * F.Right.x + Plane.GY * F.Right.y;
				const double U = G * F.CosT / FMath::Sqrt(1.0 + G * G * F.CosT * F.CosT);
				for (const double S : {0.0, SPiece, F.SMax})
				{
					const rb::Vec2 Q = F.At(S, U * F.Radius(S));
					if (InsidePolygon(Poly, Q) && !InsideCut(Poly, Q))
					{
						Consider(Q);
					}
				}
			}
			// 2. Polygon edges (outside the cut).
			for (int32 Index = 0; Index < Poly.VertexCount; ++Index)
			{
				const rb::Vec2& V0 = Poly.Vertices[Index];
				const rb::Vec2 D = Poly.Vertices[(Index + 1) % Poly.VertexCount] - V0;
				const int32 Count = OutsideCut(Poly, V0, D, 0.0, 1.0, Parts);
				for (int32 Part = 0; Part < Count; ++Part)
				{
					MinOnLine(V0, D, Parts[Part][0], Parts[Part][1]);
				}
			}
			// 3. Silhouettes y = +-r(s), parametrised by s (the gap is linear along them: the ends of the parts over the surface).
			for (const double Side : {-1.0, 1.0})
			{
				const rb::Vec2 Starts[2] = {F.Origin + F.Right * (Side * F.Rt), F.Origin + F.Right * (Side * F.Radius(F.L))};
				const rb::Vec2 Dirs[2] = {F.Dir + F.Right * (Side * F.K), F.Dir};
				const double Ranges[2][2] = {{0.0, SPiece}, {F.L, F.SMax}};
				for (int32 Piece = 0; Piece < 2; ++Piece)
				{
					double Lo = Ranges[Piece][0];
					double Hi = Ranges[Piece][1];
					if (Hi < Lo || !ClipToPolygon(Poly, Starts[Piece], Dirs[Piece], Lo, Hi))
					{
						continue;
					}
					const int32 Count = OutsideCut(Poly, Starts[Piece], Dirs[Piece], Lo, Hi, Parts);
					for (int32 Part = 0; Part < Count; ++Part)
					{
						Consider(Starts[Piece] + Dirs[Piece] * Parts[Part][0]);
						Consider(Starts[Piece] + Dirs[Piece] * Parts[Part][1]);
					}
				}
			}
			// 4. End sections s = 0 and s = SMax (across the width).
			for (const double S : {0.0, F.SMax})
			{
				const double R = F.Radius(S);
				const rb::Vec2 Q0 = F.At(S, -R);
				const rb::Vec2 D = F.Right * (2.0 * R);
				double Lo = 0.0;
				double Hi = 1.0;
				if (!ClipToPolygon(Poly, Q0, D, Lo, Hi))
				{
					continue;
				}
				const int32 Count = OutsideCut(Poly, Q0, D, Lo, Hi, Parts);
				for (int32 Part = 0; Part < Count; ++Part)
				{
					MinOnLine(Q0, D, Parts[Part][0], Parts[Part][1]);
				}
			}
			// 5. Rim of the pocket cut where it runs under the footprint (64 chords: sagitta 0.07 mm for a 6 cm hole).
			if (Poly.HasCut && Poly.CutRadius > 0.0)
			{
				const double YC = F.YOf(Poly.CutCenter);
				const double SC = F.SOf(Poly.CutCenter);
				const double Reach = Poly.CutRadius / F.CosT;
				if (FMath::Abs(YC) <= Poly.CutRadius + RMax && SC >= -Reach && SC <= F.SMax + Reach)
				{
					constexpr int32 Chords = 64;
					for (int32 Index = 0; Index < Chords; ++Index)
					{
						const double A0 = UE_DOUBLE_TWO_PI * static_cast<double>(Index) / Chords;
						const double A1 = UE_DOUBLE_TWO_PI * static_cast<double>(Index + 1) / Chords;
						const rb::Vec2 P0 = Poly.CutCenter + rb::Vec2(FMath::Cos(A0), FMath::Sin(A0)) * Poly.CutRadius;
						const rb::Vec2 D = Poly.CutCenter + rb::Vec2(FMath::Cos(A1), FMath::Sin(A1)) * Poly.CutRadius - P0;
						double Lo = 0.0;
						double Hi = 1.0;
						if (ClipToPolygon(Poly, P0, D, Lo, Hi))
						{
							MinOnLine(P0, D, Lo, Hi);
						}
					}
				}
			}
		}
	}

	// Bisection of [Lo, Hi] (Lo blocked, Hi clear) down to Resolution; returns the clear end, LoGap = the gap just below it.
	template <typename TestType>
	double Bisect(double Lo, double Hi, double Resolution, TestType&& Test, FRbCueGap& LoGap)
	{
		while (Hi - Lo > Resolution)
		{
			const double Mid = 0.5 * (Lo + Hi);
			FRbCueGap Gap;
			if (Test(Mid, Gap))
			{
				Hi = Mid;
			}
			else
			{
				Lo = Mid;
				LoGap = Gap;
			}
		}
		return Hi;
	}

	// Coarse sweep from Start upward (Step), then bisection to Resolution. Returns false when nothing up to MaxElevation is
	// clear (OutElevation = MaxElevation); OutGap = the blocking gap just below the result (none when Start is clear).
	template <typename TestType>
	bool SearchMinElevation(double Start, double MaxElevation, double Step, double Resolution, TestType&& Test, double& OutElevation,
		FRbCueGap& OutGap)
	{
		OutGap = FRbCueGap();
		FRbCueGap Gap;
		if (Test(Start, Gap))
		{
			OutElevation = Start;
			return true;
		}
		double Lo = Start;
		FRbCueGap LoGap = Gap;
		while (Lo < MaxElevation)
		{
			const double Next = FMath::Min(Lo + Step, MaxElevation);
			FRbCueGap NextGap;
			if (Test(Next, NextGap))
			{
				OutElevation = Bisect(Lo, Next, Resolution, Test, LoGap);
				OutGap = LoGap;
				return true;
			}
			Lo = Next;
			LoGap = NextGap;
		}
		OutElevation = MaxElevation;
		OutGap = LoGap;
		return false;
	}
}

	double EnvelopeRadius(const rb::human::CueBodyState& Body, double CueLength, double S)
	{
		const double L = CueLength > 0.0 ? CueLength : 1.4732;
		return Body.TipRadius + (Body.ButtRadius - Body.TipRadius) * FMath::Clamp(S, 0.0, L) / L;
	}

	FRbCuePose MakePose(const FRbCueClearanceInput& Input, double Elevation, double CueBallRadius)
	{
		FRbCuePose Pose;
		const double Reference = Input.ContactElevation >= 0.0 ? Input.ContactElevation : Input.Elevation;
		const rb::CueFrame RefFrame = rb::MakeCueFrame(Reference, Input.Azimuth);
		const double DefaultRadius = CueBallRadius > 0.0 ? CueBallRadius : rb::kDefaultBallRadius;
		rb::Vec3 Center;
		double Radius = DefaultRadius;
		if (HasCueBall(Input))
		{
			Center = Input.BallPositions[Input.CueBall];
			const double Measured = rb::Length(Input.ContactPoint - Center);
			if (Measured > 1e-6)
			{
				Radius = Measured;
			}
			else
			{
				Center = Input.ContactPoint + RefFrame.Axis * Radius; // contact point = centre given: treat as a centre-ball hit
			}
		}
		else
		{
			Center = Input.ContactPoint + RefFrame.Axis * Radius; // no cue ball: a centre-ball hit through ContactPoint
		}
		const rb::Vec3 QRef = (Input.ContactPoint - Center) / Radius;
		double A = rb::Dot(QRef, RefFrame.Right);
		double B = rb::Dot(QRef, RefFrame.Up);
		const double Rho = FMath::Sqrt(A * A + B * B);
		if (Rho > 0.99)
		{
			A *= 0.99 / Rho;
			B *= 0.99 / Rho;
		}
		const rb::CueFrame Frame = rb::MakeCueFrame(Elevation, Input.Azimuth);
		const double C = FMath::Sqrt(FMath::Max(0.0, 1.0 - A * A - B * B));
		const rb::Vec3 Q = Frame.Right * A + Frame.Up * B - Frame.Axis * C;
		const double Dome = FMath::IsFinite(Input.TipDomeRadius) ? FMath::Max(0.0, Input.TipDomeRadius) : 0.0106;
		const double HalfWidth = FMath::IsFinite(Input.TipWidth) ? 0.5 * FMath::Max(0.0, Input.TipWidth) : 0.006375;
		Pose.DomeCenter = Center + Q * (Radius + Dome);
		Pose.Rim = Pose.DomeCenter + Frame.Axis * FMath::Sqrt(FMath::Max(0.0, Dome * Dome - HalfWidth * HalfWidth));
		Pose.Direction = Frame.Axis;
		Pose.CueBallCenter = Center;
		Pose.CueBallRadius = Radius;
		Pose.OffsetA = A;
		Pose.OffsetB = B;
		return Pose;
	}

	double BallGap(const FRbCuePose& Pose, const rb::human::CueBodyState& Body, double CueLength, double Backswing, const rb::Vec3& Ball,
		double BallRadius, double* OutS)
	{
		const double L = CueLength > 0.0 ? CueLength : 1.4732;
		const double B = FMath::Max(0.0, Backswing);
		const rb::Vec3 W = Ball - Pose.Rim;
		const double S0 = -rb::Dot(W, Pose.Direction); // along the body (toward the butt)
		const double H2 = FMath::Max(0.0, rb::Dot(W, W) - S0 * S0);
		const double H = FMath::Sqrt(H2);
		auto Surface = [&](double S) { return FMath::Sqrt(H2 + (S - S0) * (S - S0)) - EnvelopeRadius(Body, L, S); };
		// Tapered part [0, L]: f(s) = |X(s) - Ball| - (r_t + k s) is convex; f' = 0 at s0 + k h / sqrt(1 - k^2).
		const double K = FMath::Clamp((Body.ButtRadius - Body.TipRadius) / L, -0.99, 0.99);
		double BestS = FMath::Clamp(S0 + K * H / FMath::Sqrt(1.0 - K * K), 0.0, L);
		double Best = Surface(BestS);
		if (B > 0.0)
		{
			// Backswing part [L, L + B]: constant radius r_b, nearest point of the line.
			const double S = FMath::Clamp(S0, L, L + B);
			const double Value = Surface(S);
			if (Value < Best)
			{
				Best = Value;
				BestS = S;
			}
		}
		if (OutS)
		{
			*OutS = BestS;
		}
		return Best - BallRadius;
	}

	FRbCueGap EvaluateGap(const FRbTableContext* Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input, double Elevation,
		bool bBalls, bool bRails)
	{
		FRbCueGap Out;
		const double CueBallRadius = Table ? Table->BallRadius(Input.CueBall) : rb::kDefaultBallRadius;
		const FRbCuePose Pose = MakePose(Input, Elevation, CueBallRadius);
		const double Margin = MarginOf(Input);
		const double L = CueLengthOf(Input);
		const double Backswing = BackswingOf(Input);
		if (bBalls && Input.BallPositions)
		{
			for (int32 Id = 0; Id < Input.BallCount; ++Id)
			{
				if (Id == Input.CueBall || (Input.InPlay && !Input.InPlay[Id]))
				{
					continue;
				}
				const double Radius = Table ? Table->BallRadius(Id) : rb::kDefaultBallRadius;
				double S = 0.0;
				const double Gap = BallGap(Pose, Body, L, Backswing, Input.BallPositions[Id], Radius, &S) - Margin;
				if (Gap < Out.Gap)
				{
					Out.Gap = Gap;
					Out.By = rb::human::FloorSource::Ball;
					Out.Ball = Id;
					Out.S = S;
				}
			}
		}
		if (bRails && Table)
		{
			FRbCueGap Rails;
			EvaluateRails(*Table, Body, Input, Pose, Elevation, Rails);
			Rails.Gap -= Margin;
			if (Rails.Gap < Out.Gap)
			{
				Out = Rails;
			}
		}
		return Out;
	}

	FRbCueClearanceResult ComputeMinElevation(const FRbTableContext& Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input)
	{
		FRbCueClearanceResult Result;
		const double MaxElevation = MaxElevationOf(Input);
		const double Start = FMath::Clamp(FMath::IsFinite(Input.Elevation) ? Input.Elevation : 0.0, 0.0, MaxElevation);
		Result.MinElevation = Start;
		if (!PoseFinite(Input, Body))
		{
			return Result;
		}
		auto Test = [&](double Elevation, FRbCueGap& OutGap)
		{
			OutGap = EvaluateGap(&Table, Body, Input, Elevation, true, true);
			return OutGap.Gap >= 0.0;
		};
		FRbCueGap Floor;
		double Elevation = Start;
		Result.bBlocked = !SearchMinElevation(Start, MaxElevation, kCoarseStep, kResolution, Test, Elevation, Floor);
		Result.MinElevation = Elevation;
		if (Elevation > Start || Result.bBlocked)
		{
			Result.FloorBy = Floor.By;
			Result.FloorBall = Floor.By == rb::human::FloorSource::Ball ? Floor.Ball : -1;
		}
		return Result;
	}

	double MinElevationForBall(const FRbCueClearanceInput& Input, const rb::Vec3& Obstacle, double ObstacleRadius, const rb::human::CueBodyState& Body)
	{
		const double MaxElevation = MaxElevationOf(Input);
		const double Start = FMath::Clamp(FMath::IsFinite(Input.Elevation) ? Input.Elevation : 0.0, 0.0, MaxElevation);
		if (!PoseFinite(Input, Body) || !FMath::IsFinite(Obstacle.x) || !FMath::IsFinite(Obstacle.y) || !FMath::IsFinite(Obstacle.z))
		{
			return Start;
		}
		const double Margin = MarginOf(Input);
		const double L = CueLengthOf(Input);
		const double Backswing = BackswingOf(Input);
		auto Test = [&](double Elevation, FRbCueGap& OutGap)
		{
			const FRbCuePose Pose = MakePose(Input, Elevation, rb::kDefaultBallRadius);
			OutGap.Gap = BallGap(Pose, Body, L, Backswing, Obstacle, ObstacleRadius, &OutGap.S) - Margin;
			OutGap.By = rb::human::FloorSource::Ball;
			return OutGap.Gap >= 0.0;
		};
		FRbCueGap Floor;
		double Elevation = Start;
		SearchMinElevation(Start, MaxElevation, kCoarseStep, 1e-9, Test, Elevation, Floor);
		return Elevation;
	}

	bool SweepEnvironment(const UWorld* World, const ARbTable& Table, const FRbCueClearanceInput& Input, double Elevation,
		const rb::human::CueBodyState& Body)
	{
		if (!World)
		{
			return true;
		}
		const double CueBallRadius = Table.HasContext() ? Table.GetContext().BallRadius(Input.CueBall) : rb::kDefaultBallRadius;
		const FRbCuePose Pose = MakePose(Input, Elevation, CueBallRadius);
		const double L = CueLengthOf(Input);
		const double SMax = L + BackswingOf(Input);
		const double Margin = FMath::Max(0.0, MarginOf(Input));
		const int32 Segments = FMath::Max(1, FMath::CeilToInt32(SMax / kCapsuleSegment));

		FCollisionQueryParams Params(SCENE_QUERY_STAT(RbCueEnvironmentSweep), false);
		Params.AddIgnoredActor(&Table);
		const FCollisionResponseParams Response;
		// Reused between calls: the stroke component sweeps every frame while aiming, and the player's pawn standing at the butt
		// overlaps the chain every time (no allocation per frame once the array has grown).
		static thread_local TArray<FOverlapResult> Overlaps;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const double S0 = SMax * static_cast<double>(Segment) / static_cast<double>(Segments);
			const double S1 = SMax * static_cast<double>(Segment + 1) / static_cast<double>(Segments);
			const FVector A = Table.CoreToWorld(Pose.Rim - Pose.Direction * S0);
			const FVector B = Table.CoreToWorld(Pose.Rim - Pose.Direction * S1);
			const FVector Axis = B - A;
			const double Length = Axis.Size();
			if (Length <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			// The taper grows toward the butt: the segment's radius is the one at its butt end (conservative).
			const double Radius = 100.0 * (EnvelopeRadius(Body, L, S1) + Margin);
			const FCollisionShape Shape = FCollisionShape::MakeCapsule(static_cast<float>(Radius), static_cast<float>(0.5 * Length + Radius));
			const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Axis / Length);
			Overlaps.Reset();
			World->OverlapMultiByChannel(Overlaps, 0.5 * (A + B), Rotation, ECC_WorldDynamic, Shape, Params, Response);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (!Overlap.bBlockingHit)
				{
					continue;
				}
				const AActor* Owner = Overlap.GetActor();
				if (Owner && (Owner == &Table || Owner->IsA<APawn>() || Owner->IsA<ARbCue>() || Owner->IsA<ARbBallSet>()))
				{
					continue; // the rails are analytic; no body in M1; balls and cues never collide
				}
				return false;
			}
		}
		return true;
	}

	FRbCueClearanceResult ComputeMinElevationWithEnvironment(const UWorld* World, const ARbTable& Table, const rb::human::CueBodyState& Body,
		const FRbCueClearanceInput& Input)
	{
		if (!Table.HasContext())
		{
			FRbCueClearanceResult Result;
			Result.MinElevation = FMath::Max(0.0, Input.Elevation);
			return Result;
		}
		const FRbTableContext& Context = Table.GetContext();
		FRbCueClearanceResult Result = ComputeMinElevation(Context, Body, Input);
		if (!World || Result.bBlocked || SweepEnvironment(World, Table, Input, Result.MinElevation, Body))
		{
			return Result;
		}
		const double MaxElevation = MaxElevationOf(Input);
		auto Test = [&](double Elevation, FRbCueGap& OutGap)
		{
			OutGap = EvaluateGap(&Context, Body, Input, Elevation, true, true);
			return OutGap.Gap >= 0.0 && SweepEnvironment(World, Table, Input, Elevation, Body);
		};
		FRbCueGap Floor;
		double Elevation = Result.MinElevation;
		const bool bClear = SearchMinElevation(Result.MinElevation, MaxElevation, kEnvironmentStep, kResolution, Test, Elevation, Floor);
		Result.MinElevation = Elevation;
		Result.bBlockedByEnvironment = !bClear;
		Result.bRaisedByEnvironment = true;
		Result.FloorBy = rb::human::FloorSource::None; // the core knows rails and balls only
		Result.FloorBall = -1;
		return Result;
	}

	double FindShortCueLength(const UWorld* World, const ARbTable& Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input,
		double MaxPlayableElevation)
	{
		auto Playable = [&](const FRbCueClearanceInput& Candidate)
		{
			const FRbCueClearanceResult Result = ComputeMinElevationWithEnvironment(World, Table, Body, Candidate);
			return !Result.bBlocked && !Result.bBlockedByEnvironment && Result.MinElevation <= MaxPlayableElevation;
		};
		if (Playable(Input))
		{
			return 0.0;
		}
		for (const double Length : kShortCueLengths)
		{
			if (Length >= CueLengthOf(Input) - 1e-6)
			{
				continue;
			}
			FRbCueClearanceInput Candidate = Input;
			Candidate.CueLength = Length;
			if (Playable(Candidate))
			{
				return Length;
			}
		}
		return 0.0;
	}
}
