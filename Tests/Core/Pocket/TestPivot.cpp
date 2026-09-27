// Owner: WP-4. Rolling over the rounded drop edge (collisions 5.4): P-2 leave angles and pivot times, A-CUSH-4 (general
// inertia factor), and the pivot path used by the pocket state machine (MakePivotPath, EvaluatePivot, PivotLeaveState,
// PivotDetectionProxy).

#include "Cushion/CushionTestUtil.h"

#include "rb/Core/Tolerances.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/PocketDrop.h"

using namespace rbcushiontest;

namespace
{
	constexpr double kDropRadius = 0.0047625; // r_d, 9FT_PRO (collisions 9.5)
	constexpr double kRho = kR + kDropRadius;

	// T(psi) = integral_0^psi rho / v(psi') dpsi' with v^2 = v0^2 + (2 g rho / (1 + k)) (1 - cos psi'), by the spec's
	// substitution with many panels (reference for the 16-panel result).
	double ReferencePivotTime(double V0, double Rho, double K, double Psi)
	{
		const double A = V0 * V0;
		const double B = 4.0 * kG * Rho / (1.0 + K);
		const double Q = A / B;
		const double U = rb::Asinh(rb::Sqrt(B / A) * rb::Sin(0.5 * Psi));
		const int Panels = 20000;
		const double H = U / Panels;
		double Sum = 0.0;
		for (int i = 0; i <= Panels; ++i)
		{
			const double S = rb::Sinh(H * i);
			const double F = 1.0 / rb::Sqrt(1.0 - Q * S * S);
			Sum += (i == 0 || i == Panels) ? F : ((i & 1) != 0 ? 4.0 * F : 2.0 * F);
		}
		return 2.0 * Rho / rb::Sqrt(B) * Sum * H / 3.0;
	}

	// A corner-like pocket: capture center on the +x axis, drop edge circle a_d = r_p + r_d.
	rb::PocketGeometry TestPocket()
	{
		rb::PocketGeometry P;
		P.Id = rb::PocketId::FootLeft;
		P.CaptureCenter = {1.0, 0.2};
		P.CaptureRadius = 0.062;
		P.DropRadius = kDropRadius;
		P.DropEdgeRadius = P.CaptureRadius + P.DropRadius;
		P.Axis = {1.0, 0.0};
		return P;
	}

	// A ball on the cloth whose center is on the drop-edge circle, approaching from direction Angle (plan, about the
	// capture center), rolling with speed components along the inward normal (Vn) and the edge tangent (Vt), plus w_z.
	rb::BallState AtDropEdge(const rb::PocketGeometry& P, double Angle, double Vn, double Vt, double Wz)
	{
		const rb::Vec2 Out{rb::Cos(Angle), rb::Sin(Angle)};
		const rb::Vec2 C = P.CaptureCenter + Out * P.DropEdgeRadius;
		const rb::Vec3 Ne{-Out.x, -Out.y, 0.0};                    // toward the hole
		const rb::Vec3 Te = rb::Cross(rb::Vec3::UnitZ(), Ne);       // z x n_e
		rb::BallState S;
		S.Position = {C.x, C.y, kR};
		S.Velocity = Ne * Vn + Te * Vt;
		S.Omega = rb::Cross(rb::Vec3::UnitZ(), S.Velocity) / kR + rb::Vec3{0.0, 0.0, Wz};
		S.State = rb::MotionState::Rolling;
		return S;
	}
}

RB_TEST(COL_P2_PivotLeaveAngleAndTime)
{
	// rho = R + r_d: 53.968, 53.071, 45.445, 25.430 deg at v0 = 0, 0.1, 0.3, 0.5 m/s (1e-3 deg); immediate leave at
	// v0 >= sqrt(g rho) = 0.5717772 m/s; T_p (asinh substitution, 16 panels) within 1e-3 relative.
	const rb::NumericsConfig Numerics;
	const double K = rb::kSolidSphereInertiaFactor;
	const double Speeds[] = {0.0, 0.1, 0.3, 0.5};
	const double Angles[] = {53.968, 53.071, 45.445, 25.430};
	for (int i = 0; i < 4; ++i)
	{
		const rb::PivotResult P = rb::ComputePivot(Speeds[i], kRho, K, kG, Numerics);
		RB_CHECK(!P.Immediate);
		RB_CHECK_NEAR(Degrees(P.LeaveAngle), Angles[i], 1e-3);
	}
	RB_CHECK_NEAR(rb::Sqrt(kG * kRho), 0.5717772, 1e-7);
	RB_CHECK(rb::ComputePivot(0.5717773, kRho, K, kG, Numerics).Immediate);
	RB_CHECK(!rb::ComputePivot(0.5717771, kRho, K, kG, Numerics).Immediate);
	const rb::PivotResult Now = rb::ComputePivot(0.8, kRho, K, kG, Numerics);
	RB_CHECK(Now.Immediate && Now.Duration == 0.0 && Now.LeaveAngle == 0.0 && Now.LeaveSpeed == 0.8);

	const double TimeSpeeds[] = {1e-4, 1e-3, 0.01, 0.05, 0.1, 0.3, 0.5};
	const double Times[] = {0.63021, 0.47136, 0.31250, 0.20129, 0.15305, 0.07371, 0.02876};
	for (int i = 0; i < 7; ++i)
	{
		const rb::PivotResult P = rb::ComputePivot(TimeSpeeds[i], kRho, K, kG, Numerics);
		RB_CHECK_NEAR(P.Duration / Times[i], 1.0, 1e-3);
		// The 16-panel substitution vs the converged integral: collisions 5.4 states <= 1e-4; measured up to 1.03e-4 (k = 0.4),
		// 1.2e-4 for other k (at the smallest speeds): checked at 1.5e-4, far inside the 1e-3 of P-2.
		RB_CHECK_NEAR(P.Duration / ReferencePivotTime(TimeSpeeds[i], kRho, K, P.LeaveAngle), 1.0, 1.5e-4);
	}
	// v0 below the 1e-4 m/s floor uses the floor (T_p ~ ln(1/v0) would diverge).
	RB_CHECK(rb::ComputePivot(0.0, kRho, K, kG, Numerics).Duration == rb::ComputePivot(1e-4, kRho, K, kG, Numerics).Duration);
}

RB_TEST(ARCH_CUSH4_PivotGeneralInertia)
{
	// k = 0.4 is the spec's (10 + 7 v0^2 / (g rho)) / 17; for other k: energy (1 + k)/2 m (v^2 - v0^2) = m g rho (1 - cos psi)
	// and N = 0 at the leave angle (g cos psi = v^2 / rho); T_p equals the converged integral.
	const rb::NumericsConfig Numerics;
	const double Speeds[] = {0.0, 0.1, 0.3, 0.5};
	for (double V0 : Speeds)
	{
		const rb::PivotResult P = rb::ComputePivot(V0, kRho, 0.4, kG, Numerics);
		const double V = rb::Max(V0, Numerics.PivotMinSpeed);
		RB_CHECK_NEAR(rb::Cos(P.LeaveAngle), (10.0 + 7.0 * V * V / (kG * kRho)) / 17.0, 1e-14);
	}
	const double Ks[] = {0.3, 0.5, 2.0 / 3.0};
	for (double K : Ks)
	{
		const double SqrtGRho = rb::Sqrt(kG * kRho);
		for (double V0 : Speeds)
		{
			const rb::PivotResult P = rb::ComputePivot(V0, kRho, K, kG, Numerics);
			const double V = rb::Max(V0, Numerics.PivotMinSpeed);
			const double C = rb::Cos(P.LeaveAngle);
			RB_CHECK_NEAR(0.5 * (1.0 + K) * (P.LeaveSpeed * P.LeaveSpeed - V * V), kG * kRho * (1.0 - C), 1e-12);
			RB_CHECK_NEAR(kG * C, P.LeaveSpeed * P.LeaveSpeed / kRho, 1e-12);
			RB_CHECK_NEAR(P.Duration / ReferencePivotTime(V, kRho, K, P.LeaveAngle), 1.0, 1.5e-4);
		}
		RB_CHECK(rb::ComputePivot(SqrtGRho * (1.0 + 1e-9), kRho, K, kG, Numerics).Immediate);
		// At v0 -> 0 the leave angle is acos(2 / (3 + k)): 53.97 deg for k = 0.4, independent of rho.
		RB_CHECK_NEAR(rb::ComputePivot(0.0, kRho, K, kG, Numerics).LeaveAngle, rb::Acos(2.0 / (3.0 + K)), 1e-6);
	}
	// A heavier inertia factor pivots longer (less translational energy from the same drop).
	RB_CHECK(rb::ComputePivot(0.1, kRho, 0.5, kG, Numerics).Duration > rb::ComputePivot(0.1, kRho, 0.4, kG, Numerics).Duration);
}

RB_TEST(COL_PivotPathContinuityAndGeometry)
{
	const rb::NumericsConfig Numerics;
	const rb::BallSpec Ball = PoolBall();
	const rb::PocketGeometry Pocket = TestPocket();
	const double Angle = rb::kPi + 0.3; // approaching from the table side, off the axis
	const rb::BallState In = AtDropEdge(Pocket, Angle, 0.25, 0.1, 0.0);
	const rb::PivotPath Path = rb::MakePivotPath(In, 1.5, Pocket, Ball, kG, Numerics);
	RB_CHECK(Path.Pocket == rb::PocketId::FootLeft);
	RB_CHECK(Path.T0 == 1.5);
	RB_CHECK_NEAR(Path.Rho, kR + kDropRadius, 1e-15);
	RB_CHECK_NEAR(Path.NormalSpeed0, 0.25, 1e-14);
	RB_CHECK_NEAR(Path.TangentialSpeed, 0.1, 1e-14);
	RB_CHECK(!Path.Result.Immediate);
	RB_CHECK(Path.Result.Duration > 0.0);
	// The rounding axis lies on the a_d circle at z = -r_d, directly below the crossing point.
	RB_CHECK_NEAR(rb::Length(rb::XY(Path.AxisPoint) - Pocket.CaptureCenter), Pocket.DropEdgeRadius, 1e-14);
	RB_CHECK_NEAR(Path.AxisPoint.z, -kDropRadius, 1e-15);

	// psi = 0: exactly the DropEdge state (no position, velocity or spin jump for a ball rolling onto the edge).
	const rb::BallState S0 = rb::EvaluatePivot(Path, 0.0);
	RB_CHECK(MaxAbsDiff(S0.Position, In.Position) <= 1e-15);
	RB_CHECK(MaxAbsDiff(S0.Velocity, In.Velocity) <= 1e-15);
	RB_CHECK(MaxAbsDiff(S0.Omega, In.Omega) * kR <= 1e-15);
	RB_CHECK(S0.State == rb::MotionState::PocketPivot);

	// Along the path: the center stays on the circle rho about the axis (plus the straight tangential drift), rolling
	// without slip on the edge, and the energy (1 + k)/2 m v^2 + m g z is conserved.
	const double K = rb::InertiaFactor(Ball);
	const double Energy0 = 0.5 * (1.0 + K) * rb::LengthSquared(S0.Velocity) + kG * S0.Position.z;
	double LastPsiZ = S0.Position.z;
	for (int i = 1; i <= 20; ++i)
	{
		const double Tau = Path.Result.Duration * i / 20.0;
		const rb::BallState S = rb::EvaluatePivot(Path, Tau);
		const rb::Vec3 Arm = S.Position - Path.AxisPoint - Path.EdgeTangent * (Path.TangentialSpeed * Tau);
		RB_CHECK_NEAR(rb::Length(Arm), kRho, 1e-12);
		RB_CHECK_NEAR(rb::Dot(Arm, Path.EdgeTangent), 0.0, 1e-12);
		const rb::Vec3 ContactPointVelocity = S.Velocity + rb::Cross(S.Omega, rb::Normalized(Arm) * (-kR));
		RB_CHECK(rb::Length(ContactPointVelocity) <= 1e-12);
		RB_CHECK_NEAR(0.5 * (1.0 + K) * rb::LengthSquared(S.Velocity) + kG * S.Position.z, Energy0, 1e-9);
		RB_CHECK(S.Position.z < LastPsiZ); // monotone drop
		LastPsiZ = S.Position.z;
	}

	// The leave state: psi_leave, v_leave (cos psi n_e - sin psi z) + v_t t_e, State = PocketFall.
	const rb::BallState Leave = rb::PivotLeaveState(Path);
	RB_CHECK(Leave.State == rb::MotionState::PocketFall);
	const double Psi = Path.Result.LeaveAngle;
	const rb::Vec3 ExpectedV =
		(Path.EdgeNormal * rb::Cos(Psi) - rb::Vec3::UnitZ() * rb::Sin(Psi)) * Path.Result.LeaveSpeed + Path.EdgeTangent * Path.TangentialSpeed;
	RB_CHECK(MaxAbsDiff(Leave.Velocity, ExpectedV) <= 1e-12);
	RB_CHECK(MaxAbsDiff(Leave.Position, rb::EvaluatePivot(Path, Path.Result.Duration).Position) <= 1e-15);
	// N = 0 there: g cos(psi) = v_leave^2 / rho.
	RB_CHECK_NEAR(kG * rb::Cos(Psi), Path.Result.LeaveSpeed * Path.Result.LeaveSpeed / kRho, 1e-12);
	// Times beyond the ends are clamped.
	RB_CHECK(rb::EvaluatePivot(Path, -1.0).Position == S0.Position);
	RB_CHECK(MaxAbsDiff(rb::EvaluatePivot(Path, 10.0).Position, Leave.Position) <= 1e-15);

	// psi(tau) inverts T(psi): the time to reach the angle at tau, integrated independently, is tau.
	const double TauMid = 0.37 * Path.Result.Duration;
	const rb::BallState Mid = rb::EvaluatePivot(Path, TauMid);
	const rb::Vec3 ArmMid = Mid.Position - Path.AxisPoint - Path.EdgeTangent * (Path.TangentialSpeed * TauMid);
	const double PsiMid = rb::Atan2(rb::Dot(ArmMid, Path.EdgeNormal), ArmMid.z);
	RB_CHECK_NEAR(ReferencePivotTime(Path.PivotSpeed0, kRho, K, PsiMid) / TauMid, 1.0, 2e-4);
}

RB_TEST(COL_PivotImmediateLeaveAndSpinAboutTheNormal)
{
	const rb::NumericsConfig Numerics;
	const rb::BallSpec Ball = PoolBall();
	const rb::PocketGeometry Pocket = TestPocket();
	// Fast ball (v_perp > sqrt(g rho)): leaves at once, ballistic from the DropEdge state (velocity and spin unchanged).
	const rb::BallState Fast = AtDropEdge(Pocket, rb::kPi, 1.0, 0.0, 5.0);
	const rb::PivotPath P = rb::MakePivotPath(Fast, 0.0, Pocket, Ball, kG, Numerics);
	RB_CHECK(P.Result.Immediate);
	const rb::BallState L = rb::PivotLeaveState(P);
	RB_CHECK(L.State == rb::MotionState::PocketFall);
	RB_CHECK(MaxAbsDiff(L.Position, Fast.Position) <= 1e-15);
	RB_CHECK(L.Velocity == Fast.Velocity && L.Omega == Fast.Omega);
	const rb::MotionSegment Proxy = rb::PivotDetectionProxy(P);
	RB_CHECK(Proxy.TauEnd == 0.0);

	// Slow ball with side spin: w_z is kept as spin about the contact normal (e_r) throughout the pivot.
	const rb::BallState Slow = AtDropEdge(Pocket, rb::kPi - 0.2, 0.2, 0.0, 7.0);
	const rb::PivotPath Q = rb::MakePivotPath(Slow, 0.0, Pocket, Ball, kG, Numerics);
	RB_CHECK(!Q.Result.Immediate);
	for (int i = 0; i <= 4; ++i)
	{
		const double Tau = Q.Result.Duration * i / 4.0;
		const rb::BallState S = rb::EvaluatePivot(Q, Tau);
		const rb::Vec3 Er = rb::Normalized(S.Position - Q.AxisPoint);
		RB_CHECK_NEAR(rb::Dot(S.Omega, Er), 7.0, 1e-12);
	}
}

RB_TEST(COL_PivotDetectionProxy)
{
	// Quadratic through the start position / velocity and the leave position at Duration.
	const rb::NumericsConfig Numerics;
	const rb::PocketGeometry Pocket = TestPocket();
	const rb::PivotPath Path = rb::MakePivotPath(AtDropEdge(Pocket, rb::kPi + 0.1, 0.3, -0.05, 0.0), 2.0, Pocket, PoolBall(), kG, Numerics);
	const rb::MotionSegment Seg = rb::PivotDetectionProxy(Path);
	RB_CHECK(Seg.State == rb::MotionState::PocketPivot);
	RB_CHECK(Seg.T0 == 2.0);
	RB_CHECK_NEAR(Seg.TauEnd, Path.Result.Duration, 0.0);
	const rb::BallState Start = rb::EvaluatePivot(Path, 0.0);
	RB_CHECK(Seg.Pos0 == Start.Position && Seg.Vel0 == Start.Velocity);
	RB_CHECK(MaxAbsDiff(rb::PositionAt(Seg, Seg.TauEnd), rb::PivotLeaveState(Path).Position) <= 1e-12);
	// The proxy stays close to the true arc (it is only used to find contacts during the pivot).
	for (int i = 1; i < 10; ++i)
	{
		const double Tau = Seg.TauEnd * i / 10.0;
		RB_CHECK(rb::Length(rb::PositionAt(Seg, Tau) - rb::EvaluatePivot(Path, Tau).Position) < 0.25 * kRho);
	}
}

RB_TEST(Integ_COL_P2_PivotOnTheNineFootProCornerPocket)
{
	// With WP-2's geometry: the corner pocket of 9FT_PRO has r_d = 4.7625 mm, so rho = R + r_d and a ball rolling
	// along the pocket axis at 0.3 m/s over the drop edge leaves at 45.445 deg.
	rb::TableGeometry Table;
	RB_REQUIRE(rb::BuildTableGeometry(rb::kTableNineFootPro, Table) == rb::ErrorCode::Ok);
	const rb::PocketGeometry& Pocket = Table.Pockets[0];
	RB_CHECK_NEAR(Pocket.DropRadius, kDropRadius, 1e-12);
	RB_CHECK_NEAR(Pocket.DropEdgeRadius, Pocket.CaptureRadius + Pocket.DropRadius, 1e-12);
	const rb::Vec2 Out = -Pocket.Axis;
	const rb::Vec2 C = Pocket.CaptureCenter + Out * Pocket.DropEdgeRadius;
	rb::BallState S;
	S.Position = {C.x, C.y, kR};
	S.Velocity = rb::ToVec3(Pocket.Axis) * 0.3;
	S.Omega = rb::Cross(rb::Vec3::UnitZ(), S.Velocity) / kR;
	S.State = rb::MotionState::Rolling;
	const rb::PivotPath Path = rb::MakePivotPath(S, 0.0, Pocket, PoolBall(), kG, rb::NumericsConfig{});
	RB_CHECK_NEAR(Degrees(Path.Result.LeaveAngle), 45.445, 1e-3);
	RB_CHECK_NEAR(Path.Result.Duration, 0.07371, 1e-3 * 0.07371);
}
