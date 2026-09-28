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
			if (Lo >= Hi)
			{
				return false;
			}
		}
		return Lo < Hi;
	}

	// Smallest rail gap (no margin) of the swept body of Pose over every rail-top plane.
	void EvaluateRails(const FRbTableContext& Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input, const FRbCuePose& Pose,
		double Elevation, FRbCueGap& InOut)
	{
		const double L = CueLengthOf(Input);
		const double SMax = L + BackswingOf(Input);
		const double CosT = FMath::Cos(Elevation);
		const double SinT = FMath::Sin(Elevation);
		const rb::Vec2 Origin(Pose.Rim.x, Pose.Rim.y);
		const rb::Vec2 Dir(-Pose.Direction.x, -Pose.Direction.y); // plan direction of increasing s (toward the butt)
		const rb::Vec2 Right(FMath::Sin(Input.Azimuth), -FMath::Cos(Input.Azimuth));
		const double DirLen2 = rb::Dot(Dir, Dir);

		for (const rb::RailTopPolygon& Poly : Table.Geometry.RailTops)
		{
			if (Poly.VertexCount < 3 || Poly.PlaneNormal.z <= 1e-9)
			{
				continue;
			}
			double Lo = 0.0;
			double Hi = SMax;
			if (!ClipToPolygon(Poly, Origin, Dir, Lo, Hi))
			{
				continue;
			}
			// Parts of [Lo, Hi] outside the pocket cut disc (the hole through the rail carries no surface).
			double Parts[2][2] = {{Lo, Hi}, {0.0, -1.0}};
			if (Poly.HasCut)
			{
				const rb::Vec2 W = Origin - Poly.CutCenter;
				const double C = rb::Dot(W, W) - Poly.CutRadius * Poly.CutRadius;
				if (DirLen2 < 1e-18)
				{
					if (C < 0.0)
					{
						continue; // a vertical cue standing in the hole
					}
				}
				else
				{
					const double Bh = rb::Dot(Dir, W);
					const double Disc = Bh * Bh - DirLen2 * C;
					if (Disc > 0.0)
					{
						const double Root = FMath::Sqrt(Disc);
						const double S0 = (-Bh - Root) / DirLen2;
						const double S1 = (-Bh + Root) / DirLen2;
						Parts[0][0] = Lo;
						Parts[0][1] = FMath::Min(Hi, S0);
						Parts[1][0] = FMath::Max(Lo, S1);
						Parts[1][1] = Hi;
					}
				}
			}
			const rb::Vec3& N = Poly.PlaneNormal;
			const double GradX = -N.x / N.z;
			const double GradY = -N.y / N.z;
			const double Across = GradX * Right.x + GradY * Right.y;
			const double Thickness = FMath::Sqrt(1.0 / FMath::Max(CosT * CosT, 1e-12) + Across * Across);
			auto GapAt = [&](double S)
			{
				const double Px = Origin.x + Dir.x * S;
				const double Py = Origin.y + Dir.y * S;
				const double Plane = Poly.PlanePoint.z + GradX * (Px - Poly.PlanePoint.x) + GradY * (Py - Poly.PlanePoint.y);
				const double Axis = Pose.Rim.z + SinT * S;
				return Axis - Plane - RbCueClearance::EnvelopeRadius(Body, L, S) * Thickness;
			};
			for (const auto& Part : Parts)
			{
				if (Part[1] < Part[0])
				{
					continue;
				}
				// Piecewise linear in s: the minimum lies at an end or at the taper's end s = L.
				double Candidates[3] = {Part[0], Part[1], FMath::Clamp(L, Part[0], Part[1])};
				for (const double S : Candidates)
				{
					const double Gap = GapAt(S);
					if (Gap < InOut.Gap)
					{
						InOut.Gap = Gap;
						InOut.By = rb::human::FloorSource::Rail;
						InOut.Ball = -1;
						InOut.S = S;
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
		TArray<FOverlapResult> Overlaps;
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
