#include "rb/Core/FpGuard.h"
// Owner: WP-4 (cushion, facing & pocket-edge resolution). Spec: physics-collisions 5.4 (pivot macro-step), pitfall 19.
#include "rb/Physics/PocketDrop.h"

#include "rb/Core/Assert.h"
#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		// Pivot constants of the substitution x = sin(psi/2) = sqrt(a/b) sinh(u): a = v0^2, b = 4 g rho / (1 + k).
		struct PivotIntegral
		{
			double A = 0.0;     // v0^2 [m^2/s^2]
			double B = 0.0;     // 4 g rho / (1 + k) [m^2/s^2]
			double Q = 0.0;     // a / b [1]
			double Scale = 0.0; // 2 rho / sqrt(b) [s]
		};

		PivotIntegral MakeIntegral(double V0, double Rho, double InertiaK, double Gravity)
		{
			PivotIntegral I;
			I.A = V0 * V0;
			I.B = 4.0 * Gravity * Rho / (1.0 + InertiaK);
			I.Q = I.A / I.B;
			I.Scale = 2.0 * Rho / Sqrt(I.B);
			return I;
		}

		inline double Integrand(double U, double Q)
		{
			const double S = Sinh(U);
			return 1.0 / Sqrt(1.0 - Q * S * S);
		}

		// Composite Simpson rule of the smooth integrand on [0, Upper] (collisions 5.4: <= 1e-4 relative with 16 panels
		// for v0 >= 1e-4 m/s).
		double SimpsonIntegral(double Upper, double Q, int Panels)
		{
			const double H = Upper / static_cast<double>(Panels);
			double Sum = Integrand(0.0, Q) + Integrand(Upper, Q);
			for (int i = 1; i < Panels; ++i)
			{
				Sum += ((i & 1) != 0 ? 4.0 : 2.0) * Integrand(H * static_cast<double>(i), Q);
			}
			return Sum * H / 3.0;
		}

		int EvenPanels(int Panels)
		{
			const int P = Panels < 2 ? 2 : Panels;
			return (P & 1) != 0 ? P + 1 : P;
		}

		struct PivotSolution
		{
			PivotResult Result;
			double Speed0 = 0.0;     // v0 used by the integral (floored)
			double UpperLimit = 0.0; // U at psi_leave
		};

		PivotSolution SolvePivot(double V0, double Rho, double InertiaK, double Gravity, const NumericsConfig& Numerics)
		{
			PivotSolution S;
			const double Speed = Max(0.0, V0);
			if (Speed * Speed >= Gravity * Rho)
			{
				// Leaves the edge at once (N < 0 already at psi = 0): ballistic from the DropEdge state.
				S.Result.Immediate = true;
				S.Result.LeaveAngle = 0.0;
				S.Result.Duration = 0.0;
				S.Result.LeaveSpeed = Speed;
				S.Speed0 = Speed;
				return S;
			}
			// T_p ~ ln(1/v0) diverges for v0 -> 0: the whole pivot uses v0 >= PivotMinSpeed (5.4); a non-positive
			// configured floor still keeps v0 > 0 (the substitution divides by v0).
			const double V = Max(Max(Speed, Numerics.PivotMinSpeed), 1e-12);
			const double K = InertiaK;
			const double CosLeave = Min(1.0, (2.0 + (1.0 + K) * V * V / (Gravity * Rho)) / (3.0 + K));
			const double Psi = Acos(CosLeave);
			const PivotIntegral I = MakeIntegral(V, Rho, K, Gravity);
			const double X = Sin(0.5 * Psi);
			const double U = Asinh(Sqrt(I.B / I.A) * X);
			S.Result.Immediate = false;
			S.Result.LeaveAngle = Psi;
			S.Result.LeaveSpeed = Sqrt(I.A + I.B * X * X);
			S.Result.Duration = I.Scale * SimpsonIntegral(U, I.Q, EvenPanels(Numerics.PivotSimpsonPanels));
			S.Speed0 = V;
			S.UpperLimit = U;
			return S;
		}

		// psi(tau): inverts T(U) = Scale * Simpson(U) = tau on [0, UpperLimit] (monotone; safeguarded Newton with the
		// integrand as slope, bisection fallback), then psi = 2 asin(sqrt(a/b) sinh U).
		double PivotAngleAt(const PivotPath& Path, double Tau)
		{
			if (Path.Result.Immediate || !(Tau > 0.0))
			{
				return 0.0;
			}
			if (!(Tau < Path.Result.Duration))
			{
				return Path.Result.LeaveAngle;
			}
			const PivotIntegral I = MakeIntegral(Path.PivotSpeed0, Path.Rho, Path.InertiaK, Path.Gravity);
			const int Panels = EvenPanels(Path.SimpsonPanels);
			const double Target = Tau / I.Scale;
			double Lo = 0.0;
			double Hi = Path.UpperLimit;
			double U = Hi * (Tau / Path.Result.Duration);
			for (int Iteration = 0; Iteration < 100; ++Iteration)
			{
				const double G = SimpsonIntegral(U, I.Q, Panels) - Target;
				if (G > 0.0)
				{
					Hi = U;
				}
				else
				{
					Lo = U;
				}
				double Next = U - G / Integrand(U, I.Q);
				if (!(Next > Lo && Next < Hi))
				{
					Next = 0.5 * (Lo + Hi);
				}
				const double Delta = Abs(Next - U);
				U = Next;
				if (Delta <= 1e-15 * Path.UpperLimit || Hi - Lo <= 1e-15 * Path.UpperLimit)
				{
					break;
				}
			}
			const double Sine = Min(1.0, Sqrt(I.Q) * Sinh(U));
			return 2.0 * Asin(Sine);
		}

		BallState PivotStateAtAngle(const PivotPath& Path, double Psi, double Tau)
		{
			const PivotIntegral I = MakeIntegral(Path.PivotSpeed0, Path.Rho, Path.InertiaK, Path.Gravity);
			const double SinPsi = Sin(Psi);
			const double CosPsi = Cos(Psi);
			const double HalfSin = Sin(0.5 * Psi);
			const double Speed = Sqrt(I.A + I.B * HalfSin * HalfSin);
			const Vec3 Radial = Path.EdgeNormal * SinPsi + Vec3::UnitZ() * CosPsi; // e_r: axis -> center
			const Vec3 Along = Path.EdgeNormal * CosPsi - Vec3::UnitZ() * SinPsi;  // direction of motion in the normal plane

			BallState S;
			S.Position = Path.AxisPoint + Path.EdgeTangent * (Path.TangentialSpeed * Tau) + Radial * Path.Rho;
			S.Velocity = Along * Speed + Path.EdgeTangent * Path.TangentialSpeed;
			// Rolling without slip on the edge in both directions, plus the spin about the contact normal.
			S.Omega = Path.EdgeTangent * (Speed / Path.Radius) + (Vec3::UnitZ() * SinPsi - Path.EdgeNormal * CosPsi) * (Path.TangentialSpeed / Path.Radius) +
				Radial * Path.Omega0.z;
			S.State = MotionState::PocketPivot;
			return S;
		}
	}

	PivotResult ComputePivot(double V0, double Rho, double InertiaK, double Gravity, const NumericsConfig& Numerics)
	{
		return SolvePivot(V0, Rho, InertiaK, Gravity, Numerics).Result;
	}

	PivotPath MakePivotPath(const BallState& AtDropEdge, double T0, const PocketGeometry& Pocket, const BallSpec& Spec, double Gravity,
		const NumericsConfig& Numerics)
	{
		PivotPath Path;
		Path.Pocket = Pocket.Id;
		Path.T0 = T0;
		Path.Radius = Spec.Radius;
		Path.Rho = Spec.Radius + Pocket.DropRadius;
		Path.Gravity = Gravity;
		Path.InertiaK = InertiaFactor(Spec);
		Path.SimpsonPanels = EvenPanels(Numerics.PivotSimpsonPanels);
		Path.Velocity0 = AtDropEdge.Velocity;
		Path.Omega0 = AtDropEdge.Omega;

		// n_e: horizontal unit normal of the drop edge at the crossing point, toward the hole (the capture center).
		const Vec2 ToCenter = Pocket.CaptureCenter - XY(AtDropEdge.Position);
		const double Distance = Length(ToCenter);
		RB_ASSERT(Distance > 0.0);
		const Vec2 Normal = Distance > 0.0 ? ToCenter / Distance : Normalized(Pocket.Axis);
		Path.EdgeNormal = ToVec3(Normal);
		Path.EdgeTangent = ToVec3(PerpCcw(Normal)); // z_hat x n_e
		// The rounding axis lies directly below the center (psi = 0 is exactly the DropEdge position: no jump).
		Path.AxisPoint = AtDropEdge.Position - Vec3::UnitZ() * Path.Rho;
		Path.NormalSpeed0 = Dot(AtDropEdge.Velocity, Path.EdgeNormal);
		Path.TangentialSpeed = Dot(AtDropEdge.Velocity, Path.EdgeTangent);

		const PivotSolution S = SolvePivot(Path.NormalSpeed0, Path.Rho, Path.InertiaK, Gravity, Numerics);
		Path.Result = S.Result;
		Path.PivotSpeed0 = S.Speed0;
		Path.UpperLimit = S.UpperLimit;
		return Path;
	}

	BallState EvaluatePivot(const PivotPath& Path, double Tau)
	{
		if (Path.Result.Immediate)
		{
			BallState S;
			S.Position = Path.AxisPoint + Vec3::UnitZ() * Path.Rho;
			S.Velocity = Path.Velocity0;
			S.Omega = Path.Omega0;
			S.State = MotionState::PocketPivot;
			return S;
		}
		const double T = Clamp(Tau, 0.0, Path.Result.Duration);
		return PivotStateAtAngle(Path, PivotAngleAt(Path, T), T);
	}

	BallState PivotLeaveState(const PivotPath& Path)
	{
		BallState S = EvaluatePivot(Path, Path.Result.Duration);
		S.State = MotionState::PocketFall;
		return S;
	}

	MotionSegment PivotDetectionProxy(const PivotPath& Path)
	{
		// Quadratic through the start position and velocity and the leave position (the true path is a circle arc).
		const BallState Start = EvaluatePivot(Path, 0.0);
		MotionSegment Seg;
		Seg.State = MotionState::PocketPivot;
		Seg.T0 = Path.T0;
		Seg.Radius = Path.Radius;
		Seg.SupportZ = 0.0;
		Seg.Pos0 = Start.Position;
		Seg.Vel0 = Start.Velocity;
		Seg.Omega0 = Start.Omega;
		const double Duration = Path.Result.Immediate ? 0.0 : Path.Result.Duration;
		Seg.TauEnd = Duration;
		if (Duration > 0.0)
		{
			const BallState End = EvaluatePivot(Path, Duration);
			Seg.Accel2 = (End.Position - Seg.Pos0 - Seg.Vel0 * Duration) / (Duration * Duration);
		}
		return Seg;
	}
}
