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
			// The meridian plane turns about the pocket's vertical axis as the axis point moves along the edge circle at v_t
			// (PocketDrop.h); no turn (bitwise the v1 frame) for v_t = 0 or a path without the circle.
			Vec3 AxisPoint = Path.AxisPoint;
			Vec3 EdgeNormal = Path.EdgeNormal;
			Vec3 EdgeTangent = Path.EdgeTangent;
			const double Theta = Path.AxisRadius > 0.0 ? -Path.TangentialSpeed * Tau / Path.AxisRadius : 0.0;
			if (Theta != 0.0)
			{
				const double C = Cos(Theta);
				const double Sn = Sin(Theta);
				const auto Turn = [C, Sn](const Vec3& V) { return Vec3{C * V.x - Sn * V.y, Sn * V.x + C * V.y, V.z}; };
				const Vec3 Offset = Path.AxisPoint - Path.AxisCenter;
				AxisPoint = Path.AxisCenter + Turn(Offset);
				EdgeNormal = Turn(Path.EdgeNormal);
				EdgeTangent = Turn(Path.EdgeTangent);
			}
			const Vec3 Radial = EdgeNormal * SinPsi + Vec3::UnitZ() * CosPsi; // e_r: axis -> center
			const Vec3 Along = EdgeNormal * CosPsi - Vec3::UnitZ() * SinPsi;  // direction of motion in the normal plane

			BallState S;
			S.Position = Theta != 0.0 ? AxisPoint + Radial * Path.Rho : Path.AxisPoint + Path.EdgeTangent * (Path.TangentialSpeed * Tau) + Radial * Path.Rho;
			S.Velocity = Along * Speed + EdgeTangent * Path.TangentialSpeed;
			// Rolling without slip on the edge in both directions, plus the spin about the contact normal.
			S.Omega = EdgeTangent * (Speed / Path.Radius) + (Vec3::UnitZ() * SinPsi - EdgeNormal * CosPsi) * (Path.TangentialSpeed / Path.Radius) +
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
		Path.AxisCenter = Vec3{Pocket.CaptureCenter.x, Pocket.CaptureCenter.y, Path.AxisPoint.z};
		Path.AxisRadius = Pocket.DropEdgeRadius; // the tangential motion follows the rounding-axis circle (PocketDrop.h)
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

	namespace
	{
		// Upper bound of |d^3 r / d tau^3| of the true pivot center path for psi in [0, Psi] (DERIVED). In the meridian plane
		// W(tau) = rho e_r(psi) with psi' = v / rho, v^2 = a + b sin^2(psi / 2), hence psi'' = b sin(psi) / (4 rho^2) and
		// psi''' = b cos(psi) psi' / (4 rho^2); psi', sin(psi) grow with psi on [0, psi_leave] (psi_leave < 58 deg for every
		// k in (0, 2/3]), so |W'| <= v, |W''| <= rho (psi'' + psi'^2), |W'''| <= rho (psi''' + psi'^3 + 3 psi' psi'') with the
		// values at Psi and cos <= 1. The motion along the edge turns the meridian plane about the pocket's vertical axis at
		// omega = |v_t| / a_d (PivotStateAtAngle), r = C + Rot(theta) W_h + W_z z_hat, where only the horizontal part W_h turns:
		// |r'''| <= |W'''| + 3 omega |W''| + 3 omega^2 |W'| + omega^3 |W_h|, and |W_h| = a_d - rho sin(psi) <= a_d (the center
		// moves toward the hole). (A path without the circle drifts linearly: no third derivative.)
		double PivotJerkBound(const PivotPath& Path, double Psi)
		{
			const PivotIntegral I = MakeIntegral(Path.PivotSpeed0, Path.Rho, Path.InertiaK, Path.Gravity);
			const double Rho = Path.Rho;
			const double HalfSin = Sin(0.5 * Psi);
			const double Speed = Sqrt(I.A + I.B * HalfSin * HalfSin);
			const double W1 = Speed / Rho;
			const double W2 = I.B * Sin(Psi) / (4.0 * Rho * Rho);
			const double W3 = I.B * W1 / (4.0 * Rho * Rho);
			const double M1 = Speed;
			const double M2 = Rho * (W2 + W1 * W1);
			double M3 = Rho * (W3 + W1 * W1 * W1 + 3.0 * W1 * W2);
			if (Path.AxisRadius > 0.0 && Path.TangentialSpeed != 0.0)
			{
				const double Omega = Abs(Path.TangentialSpeed) / Path.AxisRadius;
				M3 += 3.0 * Omega * M2 + 3.0 * Omega * Omega * M1 + Omega * Omega * Omega * Path.AxisRadius;
			}
			return M3;
		}

		// max over s in [0, 1] of |s (s - Mu) (s - 1)| (the Lagrange node polynomial of the nodes 0, Mu, 1).
		double NodePolynomialMax(double Mu)
		{
			const double M = Clamp(Mu, 0.0, 1.0);
			const double Disc = Sqrt(Max(0.0, 1.0 - M + M * M));
			double Worst = 0.0;
			for (int k = 0; k < 2; ++k)
			{
				const double S = ((1.0 + M) + (k == 0 ? -Disc : Disc)) / 3.0;
				Worst = Max(Worst, Abs(S * (S - M) * (S - 1.0)));
			}
			return Worst;
		}

		// psi and local time tau of the substitution value U (the forward direction of PivotAngleAt: no inversion).
		void PivotAtU(const PivotIntegral& I, int Panels, double U, double& Psi, double& Tau)
		{
			Tau = U > 0.0 ? I.Scale * SimpsonIntegral(U, I.Q, Panels) : 0.0;
			Psi = U > 0.0 ? 2.0 * Asin(Min(1.0, Sqrt(I.Q) * Sinh(U))) : 0.0;
		}
	}

	PivotProxyPiece MakePivotProxyPiece(const PivotPath& Path, double FromU, double T0, double Tolerance)
	{
		PivotProxyPiece Out;
		Out.Seg.State = MotionState::PocketPivot;
		Out.Seg.T0 = T0;
		Out.Seg.Radius = Path.Radius;
		Out.Seg.SupportZ = 0.0;
		const double EndTime = Path.T0 + (Path.Result.Immediate ? 0.0 : Path.Result.Duration);
		const double UpperLimit = Path.UpperLimit;
		const PivotIntegral I = MakeIntegral(Path.PivotSpeed0, Path.Rho, Path.InertiaK, Path.Gravity);
		const int Panels = EvenPanels(Path.SimpsonPanels);
		double PsiA = 0.0;
		double TauA = 0.0;
		const double Ua = Clamp(FromU, 0.0, UpperLimit);
		PivotAtU(I, Panels, Ua, PsiA, TauA);
		const BallState A = Path.Result.Immediate ? EvaluatePivot(Path, 0.0) : PivotStateAtAngle(Path, PsiA, TauA);
		Out.Seg.Pos0 = A.Position;
		Out.Seg.Vel0 = A.Velocity;
		Out.Seg.Omega0 = A.Omega;
		if (Path.Result.Immediate || !(Ua < UpperLimit) || !(Tolerance > 0.0))
		{
			// Nothing left (or no tolerance asked: the whole remainder as one piece through the leave point).
			Out.UpperU = UpperLimit;
			Out.Last = true;
			Out.Seg.TauEnd = Max(0.0, EndTime - T0);
			if (Out.Seg.TauEnd > 0.0)
			{
				const Vec3 End = EvaluatePivot(Path, Path.Result.Duration).Position;
				Out.Seg.Accel2 = (End - Out.Seg.Pos0 - Out.Seg.Vel0 * Out.Seg.TauEnd) / (Out.Seg.TauEnd * Out.Seg.TauEnd);
			}
			return Out;
		}

		// Largest span whose bound meets the tolerance: first guess from the jerk bound at the start and dtau/dU there, then
		// shrink with the bound at the span's end (the bound grows with psi, so it is conservative for the whole span). The
		// remainder of quadratic interpolation of a vector function is e(tau) = w(tau) r[t0, tm, tb, tau] with the node
		// polynomial w and the divided difference r[...] = integral of B-spline x r''' (a non-negative kernel of mass 1/6), so
		// |e| <= |w| max|r'''| / 6 in the Euclidean norm. kSafety covers the Simpson time law, whose derivatives differ from the
		// ODE's (collisions 5.4: T_p itself within 1e-4) - measured margin ~3 (A-VAL-1).
		constexpr double kSafety = 2.0;
		const double Coefficient = kSafety / 6.0;
		const double JerkStart = Max(PivotJerkBound(Path, PsiA), 1e-30);
		const double SlopeStart = I.Scale * Integrand(Ua, I.Q); // dtau/dU at Ua (grows with U)
		double DeltaU = Cbrt(Tolerance / (Coefficient * 0.0481125224324688 * JerkStart)) / SlopeStart;
		double Ub = UpperLimit;
		double PsiB = 0.0;
		double TauB = 0.0;
		double PsiM = 0.0;
		double TauM = 0.0;
		double Bound = 0.0;
		for (int Iteration = 0; Iteration < 60; ++Iteration)
		{
			Ub = Ua + DeltaU < UpperLimit ? Ua + DeltaU : UpperLimit;
			PivotAtU(I, Panels, Ub, PsiB, TauB);
			PivotAtU(I, Panels, 0.5 * (Ua + Ub), PsiM, TauM);
			const double Span = TauB - TauA;
			const double Mu = Span > 0.0 ? (TauM - TauA) / Span : 0.5;
			Bound = Coefficient * NodePolynomialMax(Mu) * PivotJerkBound(Path, PsiB) * Span * Span * Span;
			if (Bound <= Tolerance || !(Span > 0.0))
			{
				break;
			}
			// tau is close to linear in U over a piece: scale the step to the target span (cube root of the ratio) with a margin.
			DeltaU = (Ub - Ua) * Min(0.9 * Cbrt(Tolerance / Bound), 0.9);
		}
		Out.Last = !(Ub < UpperLimit);
		Out.UpperU = Out.Last ? UpperLimit : Ub;
		Out.Deviation = Bound;
		const Vec3 Pm = PivotStateAtAngle(Path, PsiM, TauM).Position;
		const Vec3 Pb = Out.Last ? EvaluatePivot(Path, Path.Result.Duration).Position : PivotStateAtAngle(Path, PsiB, TauB).Position;
		const double Sm = TauM - TauA;
		const double Sb = Out.Last ? EndTime - T0 : TauB - TauA;
		Out.Seg.TauEnd = Max(0.0, Sb);
		if (Sm > 0.0 && Sb > Sm)
		{
			// Quadratic through (0, Pa), (Sm, Pm), (Sb, Pb).
			const Vec3 SlopeM = (Pm - A.Position) / Sm;
			const Vec3 SlopeB = (Pb - A.Position) / Sb;
			Out.Seg.Accel2 = (SlopeB - SlopeM) / (Sb - Sm);
			Out.Seg.Vel0 = SlopeM - Out.Seg.Accel2 * Sm;
		}
		return Out;
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
