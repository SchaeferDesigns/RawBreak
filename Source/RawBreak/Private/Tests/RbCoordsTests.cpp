// Coordinate adapter tests: ue5-realism-plan T14-T16 plus round trips and the rolling-sign case (T24 in UE axes).
// Owner: UE-0 (the adapter is a frozen contract).

#include "Core/RbCoords.h"
#include "Tests/RbTestFlags.h"

#include "rb/Physics/BallState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCoordsT14Position, "RawBreak.Unit.Coords.T14_Position", RB_UNIT_TEST_FLAGS)
bool FRbCoordsT14Position::RunTest(const FString& Parameters)
{
	const FVector P = FRbCoords::PositionToUE(rb::Vec3(1.0, 0.5, rb::kDefaultBallRadius));
	TestNearlyEqual(TEXT("X"), P.X, 100.0, 1e-9);
	TestNearlyEqual(TEXT("Y"), P.Y, -50.0, 1e-9);
	TestNearlyEqual(TEXT("Z"), P.Z, 2.8575, 1e-9);
	const rb::Vec3 Back = FRbCoords::PositionToCore(P);
	TestNearlyEqual(TEXT("round trip x"), Back.x, 1.0, 1e-12);
	TestNearlyEqual(TEXT("round trip y"), Back.y, 0.5, 1e-12);
	TestNearlyEqual(TEXT("round trip z"), Back.z, rb::kDefaultBallRadius, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCoordsT15Quaternion, "RawBreak.Unit.Coords.T15_Quaternion", RB_UNIT_TEST_FLAGS)
bool FRbCoordsT15Quaternion::RunTest(const FString& Parameters)
{
	const double H = FMath::Sqrt(0.5);
	const rb::Quat Q(H, 0.0, 0.0, H); // +90 deg about core z: x_hat -> y_hat
	const FQuat QUE = FRbCoords::OrientationToUE(Q);
	TestNearlyEqual(TEXT("W"), QUE.W, H, 1e-12);
	TestNearlyEqual(TEXT("X"), QUE.X, 0.0, 1e-12);
	TestNearlyEqual(TEXT("Y"), QUE.Y, 0.0, 1e-12);
	TestNearlyEqual(TEXT("Z"), QUE.Z, -H, 1e-12);
	const FVector Rotated = QUE.RotateVector(FVector(1.0, 0.0, 0.0));
	TestNearlyEqual(TEXT("rotated X"), Rotated.X, 0.0, 1e-12);
	TestNearlyEqual(TEXT("rotated Y"), Rotated.Y, -1.0, 1e-12); // UE image of core y_hat
	TestNearlyEqual(TEXT("rotated Z"), Rotated.Z, 0.0, 1e-12);

	// Commutes with the mirror for an arbitrary rotation: M (q v) == q_UE (M v).
	const rb::Quat R = rb::Normalized(rb::Quat(0.3, -0.5, 0.7, 0.2));
	const rb::Vec3 V(0.2, -1.3, 0.8);
	const FVector Expected = FRbCoords::DirectionToUE(rb::Rotate(R, V));
	const FVector Actual = FRbCoords::OrientationToUE(R).RotateVector(FRbCoords::DirectionToUE(V));
	TestTrue(TEXT("mirror commutes with rotation"), Expected.Equals(Actual, 1e-12));
	const rb::Quat Back = FRbCoords::OrientationToCore(FRbCoords::OrientationToUE(R));
	TestTrue(TEXT("quaternion round trip"), Back == R);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCoordsT16Omega, "RawBreak.Unit.Coords.T16_AngularVelocity", RB_UNIT_TEST_FLAGS)
bool FRbCoordsT16Omega::RunTest(const FString& Parameters)
{
	const FVector W = FRbCoords::AngularVelocityToUE(rb::Vec3(1.0, 2.0, 3.0));
	TestEqual(TEXT("wx"), W.X, -1.0);
	TestEqual(TEXT("wy"), W.Y, 2.0);
	TestEqual(TEXT("wz"), W.Z, -3.0);

	// Pseudovector check: a ball rolling toward core +x spins about core +y (T24); in UE it moves toward +X and its
	// top point (0, 0, R) must move toward +X under a small rotation about the mapped omega.
	const double R = rb::kDefaultBallRadius;
	const rb::Vec3 OmegaCore(0.0, 1.0 / R, 0.0);
	const FVector OmegaUE = FRbCoords::AngularVelocityToUE(OmegaCore);
	const FQuat Step(OmegaUE.GetSafeNormal(), OmegaUE.Size() * 1e-3);
	const FVector Top = Step.RotateVector(FVector(0.0, 0.0, 100.0 * R));
	TestTrue(TEXT("top point moves toward +X"), Top.X > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCoordsAzimuth, "RawBreak.Unit.Coords.Azimuth", RB_UNIT_TEST_FLAGS)
bool FRbCoordsAzimuth::RunTest(const FString& Parameters)
{
	// Core phi = +90 deg points to +y (left); in UE that is -Y.
	TestNearlyEqual(TEXT("phi of -Y"), FRbCoords::AzimuthFromUEDirection(FVector(0.0, -1.0, 0.0)), 0.5 * UE_DOUBLE_PI, 1e-12);
	const double Phi = 0.7;
	const double Theta = 0.1;
	const FVector D = FRbCoords::CueDirectionToUE(Phi, Theta);
	TestNearlyEqual(TEXT("azimuth round trip"), FRbCoords::AzimuthFromUEDirection(D), Phi, 1e-12);
	TestNearlyEqual(TEXT("butt raised -> tip points down"), D.Z, -FMath::Sin(Theta), 1e-12);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
