// Cue tests (UE-4, Docs/ue-architecture.md 13): T13 minimum elevation (20.787 deg); ball floor == the core's executed-pose
// shaft-contact test (what you see is what hits); rail floor vs an independent brute force over the cushion profile; mesh
// length / tip + butt radii = CueSpec / CueBodyState, closed outward lathe with section attributes, baked asset == builder;
// SetPoseCore puts the tip dome centre exactly at the given point in a translated + yawed table (no dead band, no
// teleport), visibility per drive; environment sweep blocked by a wall placed in a test world (+ short cue); the cue follows
// the stroke component's pose and floor. Owner: UE-4.

#include "Balls/RbBallTestSupport.h"
#include "Core/RbCoords.h"
#include "Cue/RbCue.h"
#include "Cue/RbCueClearance.h"
#include "Cue/RbCueMeshBuilder.h"
#include "Player/RbStrokeComponent.h"
#include "Table/RbTable.h"
#include "Tests/RbTestFlags.h"

#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/DefaultPawn.h"
#include "Math/RandomStream.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"

#include "rb/Equipment/BallSets.h"
#include "rb/Human/HumanModel.h"
#include "rb/Physics/CueStrike.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Everything in a named namespace: the unity build compiles the test files in one translation unit, and their file-local
// helpers (kR, ...) share names.
namespace RbCueTests
{
using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;

namespace
{
	constexpr double kR = 0.028575;
	constexpr double kDeg = UE_DOUBLE_PI / 180.0;

	// The T13 cue (plan 13): r_t = 6.5 mm, r_b = 15.9 mm, L = 1.47 m.
	rb::human::CueBodyState T13Body()
	{
		rb::human::CueBodyState Body;
		Body.TipRadius = 0.0065;
		Body.ButtRadius = 0.0159;
		return Body;
	}

	// Clearance input for a cue ball at Center, contact offsets (a, b) in the frame of Elevation, one table of balls.
	struct FClearanceScene
	{
		rb::Vec3 Positions[rb::kMaxBalls];
		bool InPlay[rb::kMaxBalls] = {};
		FRbCueClearanceInput Input;

		FClearanceScene(const rb::Vec3& Center, double Azimuth, double Elevation, double A = 0.0, double B = 0.0, double Radius = kR)
		{
			Positions[0] = Center;
			InPlay[0] = true;
			const rb::CueFrame Frame = rb::MakeCueFrame(Elevation, Azimuth);
			Input.ContactPoint = Center + rb::CueContactPoint(Frame, A, B, Radius);
			Input.Azimuth = Azimuth;
			Input.Elevation = Elevation;
			Input.CueBall = 0;
			Input.BallPositions = Positions;
			Input.InPlay = InPlay;
			Input.BallCount = rb::kMaxBalls;
		}

		void AddBall(int32 Id, const rb::Vec3& Position)
		{
			Positions[Id] = Position;
			InPlay[Id] = true;
		}
	};

	// The plan's T13 closed form by fixed-point iteration: D sin(theta) = R + r(s*), s* = D cos(theta) - R.
	double T13FixedPoint(double D, double R, const rb::human::CueBodyState& Body, double L, double& OutS, double& OutR)
	{
		double Theta = 0.0;
		for (int32 Iteration = 0; Iteration < 200; ++Iteration)
		{
			OutS = D * FMath::Cos(Theta) - R;
			OutR = Body.TipRadius + (Body.ButtRadius - Body.TipRadius) * OutS / L;
			Theta = FMath::Asin((R + OutR) / D);
		}
		return Theta;
	}

	// A transient table context (the 9-ft pro table, standard balls).
	TSharedPtr<const FRbTableContext> MakeContext(FAutomationTestBase& Test)
	{
		FString Error;
		TSharedPtr<const FRbTableContext> Context = FRbTableContext::Create(FRbTableSetup{}, Error);
		Test.TestTrue(FString::Printf(TEXT("table context (%s)"), *Error), Context.IsValid());
		return Context;
	}

	// Upper envelope of the cushion + rail cross-section at d behind the nose line (-inf off the rail).
	double ProfileHeight(const rb::CushionProfile& Profile, double D)
	{
		double Height = -TNumericLimits<double>::Max();
		for (int32 Index = 0; Index + 1 < Profile.Points.Size(); ++Index)
		{
			const rb::Vec2& A = Profile.Points[Index];
			const rb::Vec2& B = Profile.Points[Index + 1];
			const double Lo = FMath::Min(A.x, B.x);
			const double Hi = FMath::Max(A.x, B.x);
			if (D < Lo || D > Hi)
			{
				continue;
			}
			const double Z = Hi - Lo < 1e-12 ? FMath::Max(A.y, B.y) : A.y + (B.y - A.y) * (D - A.x) / (B.x - A.x);
			Height = FMath::Max(Height, Z);
		}
		return Height;
	}

	// Independent rail test for a cue over the HEAD rail (x < -L/2, away from the corners): the body sampled across its width
	// on the vertical ellipse of the inclined cylinder (41 lateral lines), each line every 0.25 mm of s plus exactly where it
	// crosses a profile break (nose, cushion back, outer edge) and at the taper end, against the cushion profile (d behind
	// the nose). Per lateral line the underside and the profile are piecewise linear in s, so the line's minimum is sampled
	// exactly; only the width is discretised.
	bool BruteForceRailClear(const FRbTableContext& Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input, double Theta)
	{
		const FRbCuePose Pose = RbCueClearance::MakePose(Input, Theta, Table.BallRadius(0));
		const double L = Input.CueLength;
		const double SMax = L + Input.Backswing;
		const double CosT = FMath::Cos(Theta);
		const double SinT = FMath::Sin(Theta);
		const double HalfLength = Table.Geometry.HalfLength;
		const double HalfWidth = Table.Geometry.HalfWidth;
		const rb::CushionProfile& Profile = Table.Geometry.Profile;
		const double RailWidth = Profile.Points[Profile.Points.Size() - 1].x;
		const rb::Vec2 Right(FMath::Sin(Input.Azimuth), -FMath::Cos(Input.Azimuth));
		const double DirX = -Pose.Direction.x;
		const double DirY = -Pose.Direction.y;
		const int32 Lateral = 40;
		TArray<double> Samples;
		for (int32 Step = 0; Step <= Lateral; ++Step)
		{
			const double U = -1.0 + 2.0 * static_cast<double>(Step) / static_cast<double>(Lateral);
			Samples.Reset();
			for (double S = 0.0; S <= SMax; S += 0.00025)
			{
				const double AxisD = -HalfLength - (Pose.Rim.x + DirX * S);
				if (AxisD > -0.03 && AxisD < RailWidth + 0.03) // the width is < 3.2 cm: nothing else can be over the head rail
				{
					Samples.Add(S);
				}
			}
			Samples.Add(L);
			// The lateral line at offset y = U r(s) crosses d = d_k where Px(s) + Right.x U r(s) = -HalfLength - d_k (r linear in s:
			// solved by a few fixed-point steps).
			for (const rb::Vec2& Break : Profile.Points)
			{
				if (FMath::Abs(DirX) < 1e-12)
				{
					break;
				}
				double S = 0.0;
				for (int32 Iteration = 0; Iteration < 20; ++Iteration)
				{
					const double Y = U * RbCueClearance::EnvelopeRadius(Body, L, S);
					S = (-HalfLength - Break.x - Pose.Rim.x - Right.x * Y) / DirX;
				}
				if (S >= 0.0 && S <= SMax)
				{
					Samples.Add(S);
				}
			}
			for (const double S : Samples)
			{
				const double Radius = RbCueClearance::EnvelopeRadius(Body, L, S);
				const double Y = U * Radius;
				const double Qx = Pose.Rim.x + DirX * S + Right.x * Y;
				const double Qy = Pose.Rim.y + DirY * S + Right.y * Y;
				const double D = -HalfLength - Qx;
				if (D < -1e-12 || D > RailWidth + 1e-12 || FMath::Abs(Qy) > HalfWidth)
				{
					continue;
				}
				const double Under = Pose.Rim.z + SinT * S - Radius / CosT * FMath::Sqrt(FMath::Max(0.0, 1.0 - U * U));
				if (Under - Input.Margin < ProfileHeight(Profile, FMath::Clamp(D, 0.0, RailWidth)))
				{
					return false;
				}
			}
		}
		return true;
	}

	double BruteForceRailFloor(const FRbTableContext& Table, const rb::human::CueBodyState& Body, const FRbCueClearanceInput& Input)
	{
		double Lo = 0.0;
		if (BruteForceRailClear(Table, Body, Input, Lo))
		{
			return 0.0;
		}
		double Hi = Lo;
		while (Hi < 80.0 * kDeg)
		{
			Hi += 0.1 * kDeg;
			if (BruteForceRailClear(Table, Body, Input, Hi))
			{
				break;
			}
			Lo = Hi;
		}
		while (Hi - Lo > 1e-7)
		{
			const double Mid = 0.5 * (Lo + Hi);
			(BruteForceRailClear(Table, Body, Input, Mid) ? Hi : Lo) = Mid;
		}
		return Hi;
	}

	double RadiusYZ(const FVector3d& P) { return FMath::Sqrt(P.Y * P.Y + P.Z * P.Z); }

	const ERbCuePreset kPresets[] = {ERbCuePreset::Playing19oz, ERbCuePreset::Break21oz, ERbCuePreset::Jump9oz, ERbCuePreset::House19oz};

	// A blocking box (BlockAll) with the given world transform and half extent [cm].
	AActor* SpawnWall(UWorld* World, const FTransform& Transform, const FVector& Extent)
	{
		AActor* Wall = World->SpawnActor<AActor>(AActor::StaticClass(), Transform);
		if (!Wall)
		{
			return nullptr;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("WallBox"));
		Box->SetBoxExtent(Extent, false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Static);
		Wall->SetRootComponent(Box);
		Box->SetWorldTransform(Transform);
		Box->RegisterComponent();
		return Wall;
	}
}

// ------------------------------------------------------------------------------------------------------------------------
// Clearance
// ------------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueT13, "RawBreak.Unit.Cue.T13_MinElevation", RB_UNIT_TEST_FLAGS)
bool FRbCueT13::RunTest(const FString& Parameters)
{
	// Plan 13 T13: obstacle ball 0.10 m straight behind the cue ball, centre-ball hit, r_t = 6.5 mm, r_b = 15.9 mm, L = 1.47 m,
	// margin 0 -> theta_min = 20.787 deg (s* = 64.92 mm, r = 6.915 mm).
	const rb::human::CueBodyState Body = T13Body();
	double SStar = 0.0;
	double RStar = 0.0;
	const double FixedPoint = T13FixedPoint(0.10, kR, Body, 1.47, SStar, RStar);
	TestNearlyEqual(TEXT("T13 closed form [deg]"), FixedPoint / kDeg, 20.787, 0.01);
	TestNearlyEqual(TEXT("T13 s* [mm]"), 1000.0 * SStar, 64.92, 0.01);
	TestNearlyEqual(TEXT("T13 r(s*) [mm]"), 1000.0 * RStar, 6.915, 0.001);

	// The plan's geometry: the axis through the contact point (a flat tip: dome radius 0), no backswing, no margin.
	for (const double AzimuthDeg : {0.0, 37.0, -128.0})
	{
		const double Azimuth = AzimuthDeg * kDeg;
		const rb::Vec3 Center(0.2, -0.1, kR);
		FClearanceScene Scene(Center, Azimuth, 0.0);
		Scene.Input.TipDomeRadius = 0.0;
		Scene.Input.TipWidth = 0.0;
		Scene.Input.Margin = 0.0;
		Scene.Input.CueLength = 1.47;
		Scene.Input.Backswing = 0.0;
		const rb::Vec3 Behind(Center.x - 0.10 * FMath::Cos(Azimuth), Center.y - 0.10 * FMath::Sin(Azimuth), kR);
		const double Theta = RbCueClearance::MinElevationForBall(Scene.Input, Behind, kR, Body);
		TestNearlyEqual(*FString::Printf(TEXT("MinElevationForBall at azimuth %.0f deg [deg]"), AzimuthDeg), Theta / kDeg, 20.787, 0.01);
		// The exact taper minimum differs from the closest-axis-point form by < 0.001 deg.
		TestNearlyEqual(TEXT("MinElevationForBall == fixed point"), Theta / kDeg, FixedPoint / kDeg, 0.001);

		// The same through ComputeMinElevation on the table (0.25 deg sweep + bisection to 0.01 deg): the clear end of the bracket.
		const TSharedPtr<const FRbTableContext> Context = MakeContext(*this);
		if (!Context.IsValid())
		{
			return false;
		}
		Scene.AddBall(5, Behind);
		const FRbCueClearanceResult Result = RbCueClearance::ComputeMinElevation(*Context, Body, Scene.Input);
		TestTrue(TEXT("ComputeMinElevation within [theta, theta + 0.01 deg]"),
			Result.MinElevation >= Theta - 1e-9 && Result.MinElevation <= Theta + 0.0101 * kDeg);
		TestTrue(TEXT("FloorBy Ball, FloorBall 5"), Result.FloorBy == rb::human::FloorSource::Ball && Result.FloorBall == 5);
		TestFalse(TEXT("not blocked"), Result.bBlocked);
	}

	// The game's cue (nickel dome, 1 mm margin, 25 cm backswing): a little higher, and clear at the result.
	{
		const rb::Vec3 Center(0.0, 0.0, kR);
		FClearanceScene Scene(Center, 0.0, 0.0);
		const rb::Vec3 Behind(-0.10, 0.0, kR);
		Scene.Input.CueLength = 1.47;
		const double Theta = RbCueClearance::MinElevationForBall(Scene.Input, Behind, kR, Body);
		AddInfo(FString::Printf(TEXT("game cue (dome 10.6 mm, width 12.75 mm, margin 1 mm): %.3f deg"), Theta / kDeg));
		TestTrue(TEXT("margin + dome raise the floor by < 1 deg"), Theta > FixedPoint && Theta < FixedPoint + 1.0 * kDeg);
		const FRbCuePose Pose = RbCueClearance::MakePose(Scene.Input, Theta, kR);
		TestTrue(TEXT("clear by the margin at the floor"),
			RbCueClearance::BallGap(Pose, Body, 1.47, 0.25, Behind, kR) >= Scene.Input.Margin - 1e-12);
		// The requested elevation above the floor is returned unchanged (the contact point stays the centre hit of 0 deg).
		Scene.Input.ContactElevation = 0.0;
		Scene.Input.Elevation = 30.0 * kDeg;
		TestEqual(TEXT("clear request kept"), RbCueClearance::MinElevationForBall(Scene.Input, Behind, kR, Body), 30.0 * kDeg);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueBallFloorCore, "RawBreak.Unit.Cue.BallFloorMatchesCore", RB_UNIT_TEST_FLAGS)
bool FRbCueBallFloorCore::RunTest(const FString& Parameters)
{
	// The clearance pose IS the core's executed pose (dome centre, rim, taper, human-factors 3.6): at the floor (margin 0, no
	// backswing) rb::human::ExecuteStroke finds no shaft contact, 0.02 deg below it it does - for off-centre hits, other
	// azimuths and a ball beside the line. And the floor feeds ExecuteStroke's ElevationFloor.
	const TSharedPtr<const FRbTableContext> Context = MakeContext(*this);
	if (!Context.IsValid())
	{
		return false;
	}
	const rb::human::CueBodyState Body;
	const rb::CueSpec Cue = rb::kCuePlaying19oz;
	const rb::human::TipState Tip;
	rb::human::HumanParams Params;
	Params.NoiseScale = 0.0;
	struct FCase
	{
		double AzimuthDeg, AxisA, AxisB;
		rb::Vec3 Obstacle; // relative to the cue ball, plan
	};
	const FCase Cases[] = {
		{0.0, 0.0, 0.0, rb::Vec3(-0.10, 0.0, 0.0)},
		{25.0, 0.3, -0.4, rb::Vec3(-0.09, -0.03, 0.0)},
		{-140.0, -0.5, 0.2, rb::Vec3(0.06, 0.05, 0.0)},
		{90.0, 0.0, 0.5, rb::Vec3(0.02, -0.25, 0.0)},
	};
	const rb::Vec3 Center(-0.3, 0.12, kR);
	for (const FCase& Case : Cases)
	{
		const double Azimuth = Case.AzimuthDeg * kDeg;
		const rb::Vec2 Contact = rb::AimToContactOffset(rb::Vec2(Case.AxisA, Case.AxisB), kR, Tip.DomeRadius);
		FClearanceScene Scene(Center, Azimuth, 0.0, Contact.x, Contact.y);
		Scene.Input.Margin = 0.0;
		Scene.Input.Backswing = 0.0;
		Scene.Input.CueLength = Cue.Length;
		const rb::Vec3 Obstacle(Center.x + Case.Obstacle.x, Center.y + Case.Obstacle.y, kR);
		Scene.AddBall(3, Obstacle);
		const FRbCueClearanceResult Result = RbCueClearance::ComputeMinElevation(*Context, Body, Scene.Input);
		const FString Name = FString::Printf(TEXT("azimuth %.0f, axis (%.1f, %.1f)"), Case.AzimuthDeg, Case.AxisA, Case.AxisB);
		if (!TestTrue(*(Name + TEXT(": the ball sets a floor")), Result.MinElevation > 0.0 && Result.FloorBy == rb::human::FloorSource::Ball &&
			Result.FloorBall == 3))
		{
			continue;
		}
		rb::human::BallObstacle Other;
		Other.Id = 3;
		Other.Position = Obstacle;
		auto Candidates = [&](double Elevation, double Floor)
		{
			rb::human::IntendedStroke Intended;
			Intended.Azimuth = Azimuth;
			Intended.Elevation = Elevation;
			Intended.AxisOffsetA = Case.AxisA;
			Intended.AxisOffsetB = Case.AxisB;
			Intended.Speed = 2.0;
			Intended.TimeDown = 2.0;
			Intended.ForwardStart = 1.5;
			rb::human::StrokeSituation Situation;
			Situation.ElevationFloor = Floor;
			Situation.FloorBy = Floor > 0.0 ? rb::human::FloorSource::Ball : rb::human::FloorSource::None;
			Situation.FloorBall = Floor > 0.0 ? 3 : rb::kNoBall;
			const rb::human::ExecutedStroke Executed = rb::human::ExecuteStroke(Intended, rb::human::UniformAttributes(50.0), Situation, Tip,
				Body, Cue, rb::kStandardPoolBall, Center, &Other, 1, rb::human::NoiseKey{}, rb::human::NoiseHistory{}, Params);
			TestTrue(*(Name + TEXT(": ExecuteStroke ok")), Executed.Error == rb::ErrorCode::Ok);
			return Executed;
		};
		TestEqual(*(Name + TEXT(": no shaft contact at the floor")), Candidates(Result.MinElevation, 0.0).ShaftContactCandidates.Size(), 0);
		TestEqual(*(Name + TEXT(": shaft contact 0.02 deg below")), Candidates(Result.MinElevation - 0.02 * kDeg, 0.0).ShaftContactCandidates.Size(), 1);
		// Fed as the floor, a lower request is executed at the floor.
		const rb::human::ExecutedStroke Floored = Candidates(0.0, Result.MinElevation);
		TestTrue(*(Name + TEXT(": floor applied by ExecuteStroke")), Floored.ElevationClamped && Floored.Strike.Elevation == Result.MinElevation);
	}

	// Balls ahead of the tip, beside the line far away or pocketed (not in play) never set a floor; the cue ball is never an obstacle.
	// (Without backswing and with the butt short of the head rail: on a 9-ft table a level cue with its backswing always
	// passes over some rail.)
	{
		const rb::Vec3 Free(0.25, 0.12, kR);
		FClearanceScene Scene(Free, 0.0, 0.0);
		Scene.Input.Backswing = 0.0;
		Scene.AddBall(1, rb::Vec3(Free.x + 0.2, Free.y, kR));
		Scene.AddBall(2, rb::Vec3(Free.x - 0.3, Free.y + 0.2, kR));
		Scene.AddBall(4, rb::Vec3(Free.x - 0.1, Free.y, kR));
		Scene.InPlay[4] = false;
		const FRbCueClearanceResult Result = RbCueClearance::ComputeMinElevation(*Context, Body, Scene.Input);
		TestEqual(TEXT("free line: floor 0"), Result.MinElevation, 0.0);
		TestTrue(TEXT("free line: FloorBy None"), Result.FloorBy == rb::human::FloorSource::None && Result.FloorBall == -1);
	}

	// Non-finite input: no floor (the requested elevation), no sweep to the limit.
	{
		FClearanceScene Scene(Center, 0.0, 0.1);
		Scene.Input.ContactPoint.x = std::numeric_limits<double>::quiet_NaN();
		Scene.AddBall(4, rb::Vec3(Center.x - 0.1, Center.y, kR));
		const FRbCueClearanceResult Result = RbCueClearance::ComputeMinElevation(*Context, Body, Scene.Input);
		TestTrue(TEXT("NaN contact point: requested elevation, no floor"), Result.MinElevation == 0.1 && Result.FloorBy == rb::human::FloorSource::None &&
			!Result.bBlocked);
	}

	// Backswing: a ball 1.6 m behind the cue ball is only reached by the butt on the backswing.
	{
		FClearanceScene Scene(rb::Vec3(0.9, 0.0, kR), 0.0, 0.0);
		Scene.AddBall(6, rb::Vec3(0.9 - 1.6, 0.0, kR));
		Scene.Input.Backswing = 0.0;
		const double NoSwing = RbCueClearance::MinElevationForBall(Scene.Input, Scene.Positions[6], kR, Body);
		Scene.Input.Backswing = 0.25;
		const double Swing = RbCueClearance::MinElevationForBall(Scene.Input, Scene.Positions[6], kR, Body);
		TestEqual(TEXT("out of reach without backswing"), NoSwing, 0.0);
		TestTrue(TEXT("the backswing reaches it"), Swing > 0.5 * kDeg);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueRailFloor, "RawBreak.Unit.Cue.RailFloor", RB_UNIT_TEST_FLAGS)
bool FRbCueRailFloor::RunTest(const FString& Parameters)
{
	// The rail floor (rail-top planes, sloped cushion tops, cue width across the slope) vs an independent brute force over the
	// cushion profile; cue ball near the head rail and in the middle (the butt reaches the rail), square and oblique.
	const TSharedPtr<const FRbTableContext> Context = MakeContext(*this);
	if (!Context.IsValid())
	{
		return false;
	}
	const FRbTableContext& Table = *Context;
	const rb::human::CueBodyState Body;
	const double HalfLength = Table.Geometry.HalfLength;
	struct FCase
	{
		double DistanceFromNose, Y, AzimuthDeg, AxisB, Backswing;
	};
	const FCase Cases[] = {
		{0.06, 0.10, 0.0, 0.0, 0.25},
		{0.03, -0.20, 0.0, -0.4, 0.25},
		{0.10, 0.00, 20.0, 0.0, 0.25},
		{0.08, 0.15, -35.0, 0.3, 0.0},
		{HalfLength, 0.00, 0.0, 0.0, 0.25}, // the table centre: the butt still passes over the head rail
	};
	for (const FCase& Case : Cases)
	{
		const rb::Vec3 Center(-HalfLength + Case.DistanceFromNose, Case.Y, kR);
		const rb::Vec2 Contact = rb::AimToContactOffset(rb::Vec2(0.0, Case.AxisB), kR, 0.0106);
		FClearanceScene Scene(Center, Case.AzimuthDeg * kDeg, 0.0, Contact.x, Contact.y);
		Scene.Input.Backswing = Case.Backswing;
		const FRbCueClearanceResult Result = RbCueClearance::ComputeMinElevation(Table, Body, Scene.Input);
		const double Brute = BruteForceRailFloor(Table, Body, Scene.Input);
		const FString Name = FString::Printf(TEXT("%.2f m from the head nose, y %.2f, azimuth %.0f, b %.1f"), Case.DistanceFromNose, Case.Y,
			Case.AzimuthDeg, Case.AxisB);
		AddInfo(FString::Printf(TEXT("%s: floor %.3f deg (brute force %.4f deg)"), *Name, Result.MinElevation / kDeg, Brute / kDeg));
		TestTrue(*(Name + TEXT(": the rail sets a floor")), Result.MinElevation > 0.1 * kDeg && Result.FloorBy == rb::human::FloorSource::Rail &&
			Result.FloorBall == -1);
		// Exact for a square crossing (0.01 deg bracket); across a sloped cushion top the analytic test takes the worst of the
		// cue's width at every point of each plane (the brute force splits the width between planes): conservative by < 0.03 deg.
		TestTrue(*(Name + TEXT(": == brute force (0.01 deg bracket, conservative < 0.03 deg)")),
			Result.MinElevation >= Brute - 0.002 * kDeg && Result.MinElevation <= Brute + (Case.AzimuthDeg == 0.0 ? 0.012 : 0.03) * kDeg);
		TestTrue(*(Name + TEXT(": clear at the floor")), RbCueClearance::EvaluateGap(&Table, Body, Scene.Input, Result.MinElevation).Gap >= 0.0);
	}

	// Aiming at the head rail from the head end: the whole cue lies over the bed, no floor.
	{
		FClearanceScene Scene(rb::Vec3(-HalfLength + 0.3, 0.0, kR), 180.0 * kDeg, 0.0);
		const FRbCueClearanceResult Result = RbCueClearance::ComputeMinElevation(Table, Body, Scene.Input);
		TestEqual(TEXT("over the bed only: floor 0"), Result.MinElevation, 0.0);
	}
	// Frozen to the head cushion, shooting away: a steep floor, still found.
	{
		FClearanceScene Scene(rb::Vec3(-HalfLength + kR, 0.1, kR), 0.0, 0.0);
		const FRbCueClearanceResult Result = RbCueClearance::ComputeMinElevation(Table, Body, Scene.Input);
		AddInfo(FString::Printf(TEXT("frozen to the cushion: %.2f deg"), Result.MinElevation / kDeg));
		TestTrue(TEXT("frozen to the cushion: rail floor"), Result.FloorBy == rb::human::FloorSource::Rail && Result.MinElevation > 5.0 * kDeg &&
			!Result.bBlocked);
	}
	return true;
}

// ------------------------------------------------------------------------------------------------------------------------
// Mesh
// ------------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueMeshDimensions, "RawBreak.Unit.Cue.Mesh_Dimensions", RB_UNIT_TEST_FLAGS)
bool FRbCueMeshDimensions::RunTest(const FString& Parameters)
{
	// Length / tip + butt radii = CueSpec / CueBodyState: apex at X = r_dome, rim radius w_tip / 2 at X = RimDepth, butt end at
	// X = RimDepth - L; every ferrule .. bumper-side vertex ON the taper r(s) (r(0) = TipRadius, r(L) = ButtRadius), nothing
	// outside it; the dome on the sphere of radius r_dome about the origin (= the tip dome centre of SetPoseCore).
	rb::human::CueBodyState Custom;
	Custom.TipRadius = 0.0062;
	Custom.ButtRadius = 0.0152;
	Custom.ShaftLength = 0.70;
	for (const ERbCuePreset Preset : kPresets)
	{
		for (const rb::human::CueBodyState& Body : {rb::human::CueBodyState{}, Custom})
		{
			const rb::CueSpec Spec = rb::GetCueSpec(RbTypes::ToCore(Preset));
			const FRbCueMeshOptions Options = RbCueMeshBuilder::Resolve(Spec, Body, FRbCueMeshOptions());
			FDynamicMesh3 Mesh;
			RbCueMeshBuilder::BuildCue(Spec, Body, FRbCueMeshOptions(), Mesh);
			const FString Name = FString::Printf(TEXT("%s%s"), *StaticEnum<ERbCuePreset>()->GetNameStringByValue(static_cast<int64>(Preset)),
				Body.TipRadius == Custom.TipRadius ? TEXT(" (custom body)") : TEXT(""));
			const double L = 100.0 * Spec.Length;
			const double Dome = 100.0 * Spec.TipDomeRadius;
			const double Rim = RbCueMeshBuilder::RimDepthCm(Spec);
			TestNearlyEqual(*(Name + TEXT(": rim depth")), Rim, FMath::Sqrt(Dome * Dome - 0.25 * 1e4 * Spec.TipDiameter * Spec.TipDiameter), 1e-12);

			const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh.GetBounds();
			TestNearlyEqual(*(Name + TEXT(": apex X = r_dome")), Bounds.Max.X, Dome, 1e-9);
			TestNearlyEqual(*(Name + TEXT(": butt X = RimDepth - L")), Bounds.Min.X, Rim - L, 1e-9);
			TestNearlyEqual(*(Name + TEXT(": mesh length = cap + CueSpec::Length")), Bounds.Max.X - Bounds.Min.X, Dome - Rim + L, 1e-9);
			TestNearlyEqual(*(Name + TEXT(": taper r(0) = TipRadius")), RbCueMeshBuilder::TaperRadiusCm(Spec, Body, 0.0), 100.0 * Body.TipRadius, 1e-12);
			TestNearlyEqual(*(Name + TEXT(": taper r(L) = ButtRadius")), RbCueMeshBuilder::TaperRadiusCm(Spec, Body, L), 100.0 * Body.ButtRadius, 1e-12);

			double WorstTaper = 0.0;
			double WorstDome = 0.0;
			double Outside = 0.0;
			double MaxRadius = 0.0;
			double RimRadius = -1.0;
			int32 OnTaper = 0;
			for (const int32 V : Mesh.VertexIndicesItr())
			{
				const FVector3d P = Mesh.GetVertex(V);
				const double S = Rim - P.X;
				const double R = RadiusYZ(P);
				MaxRadius = FMath::Max(MaxRadius, R);
				if (S < -1e-9)
				{
					WorstDome = FMath::Max(WorstDome, FMath::Abs(P.Length() - Dome));
				}
				else if (FMath::Abs(S) < 1e-9)
				{
					RimRadius = FMath::Max(RimRadius, R);
				}
				if (S >= Options.TipHeightCm - 1e-9 && S <= L - Options.BumperFilletCm + 1e-9)
				{
					WorstTaper = FMath::Max(WorstTaper, FMath::Abs(R - RbCueMeshBuilder::TaperRadiusCm(Spec, Body, S)));
					++OnTaper;
				}
				if (S >= Options.TipHeightCm - 1e-9)
				{
					Outside = FMath::Max(Outside, R - RbCueMeshBuilder::TaperRadiusCm(Spec, Body, S));
				}
			}
			TestTrue(*(Name + TEXT(": taper vertices")), OnTaper > 0);
			TestTrue(*(Name + TEXT(": every body vertex on r(s) (1e-9 cm)")), WorstTaper < 1e-9);
			TestTrue(*(Name + TEXT(": nothing outside the taper envelope")), Outside < 1e-9);
			TestTrue(*(Name + TEXT(": dome on the r_dome sphere about the origin")), WorstDome < 1e-9);
			TestNearlyEqual(*(Name + TEXT(": rim radius = TipDiameter / 2")), RimRadius, 50.0 * Spec.TipDiameter, 1e-9);
			TestNearlyEqual(*(Name + TEXT(": largest radius = r(L - fillet)")), MaxRadius,
				RbCueMeshBuilder::TaperRadiusCm(Spec, Body, L - Options.BumperFilletCm), 1e-9);

			// Sections: tip .. bumper in order, contiguous, covering the cue; the joint at CueBodyState::ShaftLength.
			const TArray<FRbCueSectionRange> Sections = RbCueMeshBuilder::SectionRanges(Spec, Body, FRbCueMeshOptions());
			TestTrue(*(Name + TEXT(": sections from the apex to the butt")), Sections.Num() >= 5 && Sections[0].Section == ERbCueSection::Tip &&
				FMath::IsNearlyEqual(Sections[0].StartS, Rim - Dome, 1e-9) && Sections.Last().Section == ERbCueSection::Bumper &&
				FMath::IsNearlyEqual(Sections.Last().EndS, L, 1e-9));
			for (int32 Index = 1; Index < Sections.Num(); ++Index)
			{
				TestTrue(*(Name + TEXT(": contiguous, ordered sections")), Sections[Index].StartS == Sections[Index - 1].EndS &&
					static_cast<int32>(Sections[Index].Section) > static_cast<int32>(Sections[Index - 1].Section));
			}
			const FRbCueSectionRange* Joint = Sections.FindByPredicate([](const FRbCueSectionRange& R) { return R.Section == ERbCueSection::Joint; });
			const FRbCueSectionRange* Ferrule = Sections.FindByPredicate([](const FRbCueSectionRange& R) { return R.Section == ERbCueSection::Ferrule; });
			TestTrue(*(Name + TEXT(": ferrule ends at CueBodyState::FerruleLength")), Ferrule && FMath::IsNearlyEqual(Ferrule->EndS, 100.0 * Body.FerruleLength, 1e-9));
			TestTrue(*(Name + TEXT(": joint at CueBodyState::ShaftLength")), Joint && FMath::IsNearlyEqual(Joint->StartS, 100.0 * Body.ShaftLength, 1e-9));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueMeshTopology, "RawBreak.Unit.Cue.Mesh_ClosedOutward", RB_UNIT_TEST_FLAGS)
bool FRbCueMeshTopology::RunTest(const FString& Parameters)
{
	// Closed genus-0 lathe, outward in UE space (volume > 0 and = the lathe volume), overlay normals agree with the faces,
	// section attributes: triangle group = UV1.x = section, UV0.x = s [m], vertex colour = SectionColor; radial sagitta.
	for (const ERbCuePreset Preset : kPresets)
	{
		const rb::CueSpec Spec = rb::GetCueSpec(RbTypes::ToCore(Preset));
		const rb::human::CueBodyState Body;
		const FRbCueMeshOptions Options = RbCueMeshBuilder::Resolve(Spec, Body, FRbCueMeshOptions());
		FDynamicMesh3 Mesh;
		RbCueMeshBuilder::BuildCue(Spec, Body, FRbCueMeshOptions(), Mesh);
		const FString Name = StaticEnum<ERbCuePreset>()->GetNameStringByValue(static_cast<int64>(Preset));
		TestTrue(*(Name + TEXT(": closed")), Mesh.IsClosed());
		TestEqual(*(Name + TEXT(": Euler characteristic 2")), Mesh.VertexCount() - Mesh.EdgeCount() + Mesh.TriangleCount(), 2);
		TestEqual(*(Name + TEXT(": radial segments (multiple of 4)")), Options.RadialSegments % 4, 0);

		// Volume with UE's winding (VectorUtil::Normal = (C - A) x (B - A)): positive = outward.
		double Volume = 0.0;
		int32 BadNormals = 0;
		int32 Degenerate = 0;
		const UE::Geometry::FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
		const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Attributes->PrimaryNormals();
		const UE::Geometry::FDynamicMeshUVOverlay* UV0 = Attributes->GetUVLayer(0);
		const UE::Geometry::FDynamicMeshUVOverlay* UV1 = Attributes->GetUVLayer(1);
		const UE::Geometry::FDynamicMeshColorOverlay* Colors = Attributes->PrimaryColors();
		int32 BadAttributes = 0;
		TSet<int32> Groups;
		const double Rim = RbCueMeshBuilder::RimDepthCm(Spec);
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tri, A, B, C);
			Volume += A.Dot((C - A).Cross(B - A)) / 6.0;
			if (Mesh.GetTriArea(Tri) < 1e-12)
			{
				++Degenerate;
				continue;
			}
			const FVector3d Face = Mesh.GetTriNormal(Tri);
			const FIndex3i NormalTri = Normals->GetTriangle(Tri);
			const FIndex3i Verts = Mesh.GetTriangle(Tri);
			const int32 Group = Mesh.GetTriangleGroup(Tri);
			Groups.Add(Group);
			const FVector4f Expected = RbCueMeshBuilder::SectionColor(static_cast<ERbCueSection>(Group));
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FVector3f N = Normals->GetElement(NormalTri[Corner]);
				BadNormals += FVector3d(N).Dot(Face) > 0.0 && FMath::IsNearlyEqual(N.Length(), 1.0f, 1e-5f) ? 0 : 1;
				const FVector2f U0 = UV0->GetElement(UV0->GetTriangle(Tri)[Corner]);
				const FVector2f U1 = UV1->GetElement(UV1->GetTriangle(Tri)[Corner]);
				const FVector4f Color = Colors->GetElement(Colors->GetTriangle(Tri)[Corner]);
				const double S = Rim - Mesh.GetVertex(Verts[Corner]).X;
				BadAttributes += FMath::IsNearlyEqual(static_cast<double>(U0.X), 0.01 * S, 1e-6) ? 0 : 1;
				BadAttributes += U0.Y >= 0.0f && U0.Y <= 1.0f ? 0 : 1;
				BadAttributes += U1.X == static_cast<float>(Group) && U1.Y >= 0.0f && U1.Y <= 1.0f ? 0 : 1;
				BadAttributes += Color == Expected ? 0 : 1;
			}
		}
		TestEqual(*(Name + TEXT(": no degenerate triangles")), Degenerate, 0);
		TestEqual(*(Name + TEXT(": overlay normals unit and outward (agree with the faces)")), BadNormals, 0);
		TestEqual(*(Name + TEXT(": UV0 = (s [m], azimuth), UV1 = (section, t), colour = section")), BadAttributes, 0);

		// Lathe volume of the profile (frusta of the rings + the dome cap), the polygonal cross-section scaled by N sin(2 pi / N) / (2 pi).
		double Exact = 0.0;
		{
			const double Dome = 100.0 * Spec.TipDomeRadius;
			const double Cap = Dome - Rim;
			Exact += UE_DOUBLE_PI * Cap * Cap * (3.0 * Dome - Cap) / 3.0;
			const double L = 100.0 * Spec.Length;
			const double W2 = 50.0 * Spec.TipDiameter;
			const double H = Options.TipHeightCm;
			const double Rh = RbCueMeshBuilder::TaperRadiusCm(Spec, Body, H);
			Exact += UE_DOUBLE_PI * H * (W2 * W2 + W2 * Rh + Rh * Rh) / 3.0;
			const double F = Options.BumperFilletCm;
			const double R0 = Rh;
			const double R1 = RbCueMeshBuilder::TaperRadiusCm(Spec, Body, L - F);
			Exact += UE_DOUBLE_PI * (L - F - H) * (R0 * R0 + R0 * R1 + R1 * R1) / 3.0;
			// Fillet end: cylinder of radius R1 - F plus the quarter torus (Pappus) over length F.
			const double Rc = R1 - F;
			Exact += UE_DOUBLE_PI * Rc * Rc * F + 2.0 * UE_DOUBLE_PI * Rc * UE_DOUBLE_PI * F * F / 4.0 +
				2.0 * UE_DOUBLE_PI * (4.0 * F / (3.0 * UE_DOUBLE_PI)) * UE_DOUBLE_PI * F * F / 4.0;
		}
		const double N = Options.RadialSegments;
		const double PolygonFactor = N * FMath::Sin(UE_DOUBLE_TWO_PI / N) / UE_DOUBLE_TWO_PI;
		AddInfo(FString::Printf(TEXT("%s: %d triangles, volume %.3f cm^3 (lathe %.3f)"), *Name, Mesh.TriangleCount(), Volume, Exact * PolygonFactor));
		TestTrue(*(Name + TEXT(": outward (volume > 0)")), Volume > 0.0);
		TestTrue(*(Name + TEXT(": volume = lathe volume (0.5 %)")), FMath::Abs(Volume / (Exact * PolygonFactor) - 1.0) < 0.005);

		// Every section present as a triangle group.
		for (const FRbCueSectionRange& Range : RbCueMeshBuilder::SectionRanges(Spec, Body, FRbCueMeshOptions()))
		{
			TestTrue(*FString::Printf(TEXT("%s: section %s present"), *Name, RbCueMeshBuilder::SectionName(Range.Section)),
				Groups.Contains(static_cast<int32>(Range.Section)));
		}
		// Silhouette: 48 segments -> sagitta r (1 - cos(pi / 48)) < 0.035 mm at the butt.
		const double Sagitta = 10.0 * 100.0 * Body.ButtRadius * (1.0 - FMath::Cos(UE_DOUBLE_PI / N));
		TestTrue(*(Name + TEXT(": butt sagitta < 0.035 mm")), Sagitta < 0.035);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueMeshBaked, "RawBreak.Unit.Cue.Mesh_Baked", RB_UNIT_TEST_FLAGS)
bool FRbCueMeshBaked::RunTest(const FString& Parameters)
{
	// The baked SM_Cue_<Preset> (rb_bake_cue.py) is exactly the builder's mesh: triangles, bounds, no Nanite, no collision.
	int32 Found = 0;
	for (const ERbCuePreset Preset : kPresets)
	{
		const UStaticMesh* Baked = ARbCue::LoadBakedMesh(Preset);
		if (!Baked)
		{
			continue;
		}
		++Found;
		const rb::CueSpec Spec = rb::GetCueSpec(RbTypes::ToCore(Preset));
		FDynamicMesh3 Mesh;
		RbCueMeshBuilder::BuildCue(Spec, rb::human::CueBodyState{}, FRbCueMeshOptions(), Mesh);
		const FString Name = ARbCue::BakedMeshPackage(Preset);
		TestEqual(*(Name + TEXT(": triangles == builder")), Baked->GetNumTriangles(0), Mesh.TriangleCount());
		const FBox Box = Baked->GetBoundingBox();
		const UE::Geometry::FAxisAlignedBox3d Expected = Mesh.GetBounds();
		TestTrue(*(Name + TEXT(": bounds == builder (1e-3 cm)")), Box.Min.Equals(FVector(Expected.Min), 1e-3) && Box.Max.Equals(FVector(Expected.Max), 1e-3));
		TestFalse(*(Name + TEXT(": no Nanite")), Baked->HasValidNaniteData());
		const FStaticMeshRenderData* RenderData = Baked->GetRenderData();
		TestTrue(*(Name + TEXT(": two UV channels")), RenderData && RenderData->LODResources.Num() > 0 &&
			RenderData->LODResources[0].GetNumTexCoords() == 2);
	}
	if (Found == 0)
	{
		AddWarning(TEXT("no baked cue meshes (run Tools/unreal/editor/rb_bake_cue.py)"));
	}
	return true;
}

// ------------------------------------------------------------------------------------------------------------------------
// Actor
// ------------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueSetPoseTest, "RawBreak.Unit.Cue.SetPoseCore_YawedTable", RB_UNIT_TEST_FLAGS)
bool FRbCueSetPoseTest::RunTest(const FString& Parameters)
{
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World); // translated + yawed 30 deg
	ARbCue* Cue = TestWorld.World->SpawnActor<ARbCue>();
	if (!TestTrue(TEXT("table + cue"), Scene.Table && Scene.Table->HasContext() && Cue))
	{
		return false;
	}
	Cue->InitForTable(Scene.Table, rb::kCuePlaying19oz, rb::human::CueBodyState{});
	TestTrue(TEXT("attached to the cloth origin"), Cue->GetRootComponent()->GetAttachParent() == Scene.Table->GetClothOrigin());
	TestTrue(TEXT("a mesh component"), Cue->GetMeshComponent() != nullptr);
	TestTrue(TEXT("a material (M_RbCue or the vertex-colour fallback)"), Cue->GetAppliedMaterial() != nullptr);
	TestTrue(TEXT("Hidden after init"), Cue->GetDrive() == ERbCueDrive::Hidden && Cue->IsHidden());

	int32 Updates = 0;
	int32 Teleports = 0;
	const FDelegateHandle Handle = Cue->GetRootComponent()->TransformUpdated.AddLambda(
		[&Updates, &Teleports](USceneComponent*, EUpdateTransformFlags, ETeleportType Teleport)
		{
			++Updates;
			Teleports += Teleport != ETeleportType::None ? 1 : 0;
		});

	FRandomStream Random(413);
	double WorstLocation = 0.0;
	double WorstAxis = 0.0;
	double WorstApex = 0.0;
	const double Dome = 100.0 * rb::kCuePlaying19oz.TipDomeRadius;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const rb::Vec3 Tip(Random.FRandRange(-1.3, 1.3), Random.FRandRange(-0.7, 0.7), Random.FRandRange(0.0, 0.3));
		const double Elevation = (Index == 0 ? 85.0 : Random.FRandRange(0.0, 85.0)) * kDeg; // incl. the steepest masse
		const rb::CueFrame Frame = rb::MakeCueFrame(Elevation, Random.FRandRange(-180.0, 180.0) * kDeg);
		Cue->SetPoseCore(Tip, Frame.Axis);
		WorstLocation = FMath::Max(WorstLocation, (Cue->GetActorLocation() - Scene.Table->CoreToWorld(Tip)).Size());
		WorstAxis = FMath::Max(WorstAxis, (Cue->GetActorForwardVector() - Scene.Table->CoreDirectionToWorld(Frame.Axis)).Size());
		// The mesh's apex = dome centre + r_dome along the axis.
		const FVector Apex = Cue->GetMeshComponent()->GetComponentTransform().TransformPosition(FVector(Dome, 0.0, 0.0));
		WorstApex = FMath::Max(WorstApex, (Apex - Scene.Table->CoreToWorld(Tip + Frame.Axis * (0.01 * Dome))).Size());
	}
	AddInfo(FString::Printf(TEXT("worst tip %.3g cm, axis %.3g, apex %.3g cm"), WorstLocation, WorstAxis, WorstApex));
	TestTrue(TEXT("tip dome centre at the given point (1e-4 cm)"), WorstLocation < 1e-4);
	TestTrue(TEXT("axis = the given direction (1e-9)"), WorstAxis < 1e-9);
	TestTrue(TEXT("mesh apex at dome centre + r_dome (1e-4 cm)"), WorstApex < 1e-4);
	
	// No dead band: a 1 um move (below the engine setters' 1e-4 cm tolerance) arrives exactly.
	{
		const rb::Vec3 Tip(0.1, 0.2, 0.05);
		const rb::Vec3 Axis = rb::MakeCueFrame(5.0 * kDeg, 30.0 * kDeg).Axis;
		Cue->SetPoseCore(Tip, Axis);
		const FVector Before = Cue->GetRootComponent()->GetRelativeLocation();
		Cue->SetPoseCore(Tip + rb::Vec3(1e-8, 0.0, 0.0), Axis);
		const FVector After = Cue->GetRootComponent()->GetRelativeLocation();
		TestNearlyEqual(TEXT("1e-6 cm move applied"), After.X - Before.X, 1e-6, 1e-12);
		const rb::Vec3 Turned = rb::MakeCueFrame(5.0 * kDeg + 1e-8, 30.0 * kDeg).Axis;
		Cue->SetPoseCore(Tip, Turned);
		TestTrue(TEXT("1e-8 rad turn applied"), Cue->GetActorForwardVector().Equals(Scene.Table->CoreDirectionToWorld(Turned), 1e-12));
	}
	Cue->GetRootComponent()->TransformUpdated.Remove(Handle);
	TestTrue(TEXT("pose updates"), Updates >= 200);
	TestEqual(TEXT("no teleports (motion vectors)"), Teleports, 0);

	// Visibility per drive.
	Cue->SetDrive(ERbCueDrive::Input);
	TestTrue(TEXT("Input: shown"), !Cue->IsHidden());
	Cue->SetDrive(ERbCueDrive::Playback);
	TestTrue(TEXT("Playback: shown"), !Cue->IsHidden());
	Cue->SetDrive(ERbCueDrive::Hidden);
	TestTrue(TEXT("Hidden: hidden"), Cue->IsHidden());
	TestTrue(TEXT("casts shadows"), Cue->GetMeshComponent()->CastShadow);
	TestTrue(TEXT("no collision on the mesh"), Cue->GetMeshComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

	// Mesh choice: the bake for the default body of a preset spec, the runtime builder otherwise (same geometry).
	const bool bBaked = ARbCue::LoadBakedMesh(ERbCuePreset::Playing19oz) != nullptr;
	TestTrue(TEXT("baked mesh used when it exists"), Cue->IsUsingBakedMesh() == bBaked);
	rb::human::CueBodyState Custom;
	Custom.ButtRadius = 0.0150;
	Cue->InitForTable(Scene.Table, rb::kCuePlaying19oz, Custom);
	TestFalse(TEXT("custom body: runtime mesh"), Cue->IsUsingBakedMesh());
	if (const UDynamicMeshComponent* Runtime = Cast<UDynamicMeshComponent>(Cue->GetMeshComponent()))
	{
		FDynamicMesh3 Expected;
		RbCueMeshBuilder::BuildCue(rb::kCuePlaying19oz, Custom, FRbCueMeshOptions(), Expected);
		TestEqual(TEXT("runtime mesh == builder"), Runtime->GetMesh()->TriangleCount(), Expected.TriangleCount());
	}
	else
	{
		AddError(TEXT("runtime mesh component expected"));
	}
	ERbCuePreset Preset = ERbCuePreset::Playing19oz;
	TestTrue(TEXT("preset of the house cue spec"), ARbCue::FindPresetForSpec(rb::kCueHouse19oz, Preset) && Preset == ERbCuePreset::House19oz);
	rb::CueSpec Odd = rb::kCuePlaying19oz;
	Odd.Length = 1.3;
	TestFalse(TEXT("no preset for a 1.3 m cue"), ARbCue::FindPresetForSpec(Odd, Preset));
	TestEqual(TEXT("baked package name"), ARbCue::BakedMeshPackage(ERbCuePreset::Break21oz), FString(TEXT("/Game/Generated/Cues/SM_Cue_Break21oz")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueEnvironment, "RawBreak.Unit.Cue.EnvironmentSweep_Wall", RB_UNIT_TEST_FLAGS)
bool FRbCueEnvironment::RunTest(const FString& Parameters)
{
	// A wall 1.45 m behind the cue ball (beyond the head end of a translated + yawed table): the 58 in cue with its backswing
	// hits it until the butt is raised; the table itself (rails analytic) and a pawn in the way never count; the 48 in short
	// cue is the longest one playable below 25 deg.
	RbBallTest::FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(World);
	if (!TestTrue(TEXT("table"), Scene.Table && Scene.Table->HasContext()) || !TestNotNull(TEXT("physics scene"), World->GetPhysicsScene()))
	{
		return false;
	}
	ARbTable& Table = *Scene.Table;
	const FRbTableContext& Context = Table.GetContext();
	const rb::human::CueBodyState Body;
	const double HalfLength = Context.Geometry.HalfLength;
	const rb::Vec3 Center(-HalfLength + 0.30, 0.0, kR);
	FClearanceScene Clearance(Center, 0.0, 0.0);
	FRbCueClearanceInput& Input = Clearance.Input;

	TestTrue(TEXT("no wall: clear at 0 deg (the table is ignored)"), RbCueClearance::SweepEnvironment(World, Table, Input, 0.0, Body));
	const FRbCueClearanceResult Free = RbCueClearance::ComputeMinElevationWithEnvironment(World, Table, Body, Input);
	TestFalse(TEXT("no wall: not raised by the environment"), Free.bRaisedByEnvironment);

	// Wall: 10 cm thick, 4 m wide, from the floor to 2.2 m, its face 1.45 m behind the cue ball (table-local -X).
	const double WallFace = Center.x - 1.45; // core x of the face
	const FTransform TableToWorld = Table.GetTableToWorld();
	const FVector WallLocal(100.0 * WallFace - 5.0, 0.0, 70.0);
	const FTransform WallTransform(TableToWorld.GetRotation(), TableToWorld.TransformPosition(WallLocal));
	AActor* Wall = SpawnWall(World, WallTransform, FVector(5.0, 200.0, 150.0));
	if (!TestNotNull(TEXT("wall"), Wall))
	{
		return false;
	}
	TestFalse(TEXT("wall: the level cue is blocked"), RbCueClearance::SweepEnvironment(World, Table, Input, 0.0, Body));
	TestTrue(TEXT("wall: a steep cue clears it"), RbCueClearance::SweepEnvironment(World, Table, Input, 70.0 * kDeg, Body));

	const FRbCueClearanceResult Raised = RbCueClearance::ComputeMinElevationWithEnvironment(World, Table, Body, Input);
	AddInfo(FString::Printf(TEXT("58 in cue + 25 cm backswing: environment floor %.2f deg (analytic floor %.2f deg)"), Raised.MinElevation / kDeg,
		Free.MinElevation / kDeg));
	TestTrue(TEXT("raised by the environment"), Raised.bRaisedByEnvironment && !Raised.bBlockedByEnvironment &&
		Raised.FloorBy == rb::human::FloorSource::None);
	TestTrue(TEXT("clear at the floor"), RbCueClearance::SweepEnvironment(World, Table, Input, Raised.MinElevation, Body));
	TestFalse(TEXT("blocked 0.02 deg below (0.01 deg bisection)"), RbCueClearance::SweepEnvironment(World, Table, Input, Raised.MinElevation - 0.02 * kDeg, Body));
	// Geometry: the butt end of the swept body (L + backswing, radius r_b + margin) just reaches the wall face.
	const double Reach = Input.CueLength + Input.Backswing;
	const double Expected = FMath::Acos(FMath::Min(1.0, (Center.x - WallFace - 0.035) / Reach));
	TestTrue(TEXT("environment floor near the butt-reaches-the-wall angle (+-3 deg)"), FMath::Abs(Raised.MinElevation - Expected) < 3.0 * kDeg);

	// A pawn standing where the butt passes is not an obstacle (no body in M1).
	ADefaultPawn* Pawn = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(),
		FTransform(TableToWorld.TransformPosition(FVector(100.0 * (Center.x - 1.0), 0.0, 10.0))));
	TestNotNull(TEXT("pawn"), Pawn);
	TestTrue(TEXT("pawn ignored"), RbCueClearance::SweepEnvironment(World, Table, Input, Raised.MinElevation, Body));

	// Short cue (plan 5.5, equipment 8.3): the longest bar short cue playable below 25 deg.
	const double Short = RbCueClearance::FindShortCueLength(World, Table, Body, Input, 25.0 * kDeg);
	TestNearlyEqual(TEXT("48 in short cue"), Short, 1.2192, 1e-9);
	FRbCueClearanceInput ShortInput = Input;
	ShortInput.CueLength = Short;
	const FRbCueClearanceResult WithShort = RbCueClearance::ComputeMinElevationWithEnvironment(World, Table, Body, ShortInput);
	AddInfo(FString::Printf(TEXT("48 in cue: floor %.2f deg"), WithShort.MinElevation / kDeg));
	TestTrue(TEXT("the short cue plays below 25 deg"), WithShort.MinElevation <= 25.0 * kDeg && !WithShort.bBlockedByEnvironment);
	TestEqual(TEXT("no short cue needed without the wall"), [&] {
		Wall->Destroy();
		return RbCueClearance::FindShortCueLength(World, Table, Body, Input, 25.0 * kDeg);
	}(), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCueFollowsStroke, "RawBreak.Unit.Cue.FollowsStrokePose", RB_UNIT_TEST_FLAGS)
bool FRbCueFollowsStroke::RunTest(const FString& Parameters)
{
	// The stroke component drives the cue: shown while down, its pose = the component's rendered pose (bitwise, core terms)
	// and in the world; the butt rises automatically over a ball behind the cue ball (floor from RbCueClearance) and the
	// rendered cue clears it; standing up hides the cue.
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	ARbCue* Cue = TestWorld.World->SpawnActor<ARbCue>();
	if (!TestTrue(TEXT("table + cue"), Scene.Table && Scene.Table->HasContext() && Cue))
	{
		return false;
	}
	Cue->InitForTable(Scene.Table, rb::kCuePlaying19oz, rb::human::CueBodyState{});

	const rb::Vec3 CueBall(0.2, -0.1, kR);
	FRbStrokeContext Context;
	Context.Params.NoiseScale = 0.0; // the rendered elevation is exactly max(aim, floor)
	rb::human::BallObstacle Behind;
	Behind.Id = 7;
	Behind.Position = rb::Vec3(CueBall.x - 0.10, CueBall.y, kR);
	Context.OtherBalls.Add(Behind);

	double Clock = 1000.0;
	URbStrokeComponent* Stroke = NewObject<URbStrokeComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Stroke->AddToRoot();
	Stroke->ClockOverride = [&Clock]() { return Clock; };
	Stroke->SetTable(Scene.Table);
	Stroke->SetCue(Cue);
	Stroke->SetStrokeContext(Context);
	Stroke->BeginAddress(CueBall, kR);
	Stroke->SetAim(0.0, 0.0, 0.0, 0.0);
	Stroke->RequestGetDownToggle();
	TestTrue(TEXT("getting down shows the cue"), Cue->GetDrive() == ERbCueDrive::Input && !Cue->IsHidden());
	Clock = Stroke->GetDownSince() + 0.5;
	Stroke->TickStroke(Clock);
	TestTrue(TEXT("down"), Stroke->GetPhase() == ERbStrokePhase::Down);

	rb::Vec3 Tip;
	rb::Vec3 Direction;
	Stroke->GetCuePoseCore(Tip, Direction);
	TestTrue(TEXT("cue pose == the component's rendered pose (bitwise)"), Cue->GetTipDomeCenterCore() == Tip && Cue->GetDirectionCore() == Direction);
	TestTrue(TEXT("world pose"), Cue->GetActorLocation().Equals(Scene.Table->CoreToWorld(Tip), 1e-6) &&
		Cue->GetActorForwardVector().Equals(Scene.Table->CoreDirectionToWorld(Direction), 1e-9));

	// The floor: RbCueClearance of this aim; the rendered cue sits at it and clears the ball behind by the margin.
	FClearanceScene Clearance(CueBall, 0.0, 0.0);
	Clearance.AddBall(7, Behind.Position);
	Clearance.Input.Backswing = Stroke->MaxBackswing;
	const FRbCueClearanceResult Floor = RbCueClearance::ComputeMinElevation(Scene.Table->GetContext(), Context.CueBody, Clearance.Input);
	AddInfo(FString::Printf(TEXT("floor %.3f deg"), Floor.MinElevation / kDeg));
	TestNearlyEqual(TEXT("aim floor == RbCueClearance"), Stroke->GetAim().ElevationFloor, Floor.MinElevation, 1e-12);
	TestTrue(TEXT("context floor by ball 7"), Stroke->GetContext().Situation.FloorBy == rb::human::FloorSource::Ball &&
		Stroke->GetContext().Situation.FloorBall == 7);
	const double RenderedElevation = FMath::Asin(-Direction.z);
	TestNearlyEqual(TEXT("the butt rose to the floor"), RenderedElevation, Floor.MinElevation, 1e-9);
	// The rendered cue (address: tip AddressDistance behind the ball) clears the obstacle.
	FRbCuePose Pose;
	Pose.Direction = Direction;
	Pose.DomeCenter = Tip;
	Pose.Rim = Tip + Direction * FMath::Sqrt(0.0106 * 0.0106 - 0.006375 * 0.006375);
	TestTrue(TEXT("the rendered cue clears the ball behind"), RbCueClearance::BallGap(Pose, Context.CueBody, rb::kCuePlaying19oz.Length, 0.0,
		Behind.Position, kR) > 0.0);

	// Standing up hides it.
	Stroke->RequestGetDownToggle();
	Clock += 0.1;
	Stroke->TickStroke(Clock);
	TestTrue(TEXT("walking: cue hidden"), Cue->GetDrive() == ERbCueDrive::Hidden && Cue->IsHidden());

	Stroke->ClockOverride = nullptr;
	Stroke->RemoveFromRoot();
	Stroke->MarkAsGarbage();
	return true;
}
} // namespace RbCueTests

#endif // WITH_DEV_AUTOMATION_TESTS
