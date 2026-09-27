#include "rb/Core/FpGuard.h"
// Owner: WP-4 (cushion, facing & pocket-edge resolution). Spec: physics-collisions 4.2, 4.3, 4.5 (Mathavan et al. 2010).
//
// Ball on the cloth against a cushion edge (or a facing face, theta = beta_v), cushion-local frame (X along the
// cushion, Y into it, Z up), v_Z = 0 throughout (rigid slate). The independent variable is the accumulated cushion
// normal impulse P (Stronge's approach: no stiffness). Per unit dP the impulses are: the cushion normal (1, along
// k_hat), the cushion friction F = (F_X, F_S) in the tangent plane at I (X_hat, S_hat = -sin Y + cos Z), the table
// normal N_C = sin - F_S cos (vertical balance) and the cloth friction G = (G_X, G_Y) at C. With a = 1/m and
// b = R^2/I = 1/(k m) (general inertia, architecture 7.2; b = 5/(2m) for a solid ball):
//   dv_X = a (F_X + G_X)                  R dw_X = b (F_S + G_Y)
//   dv_Y = a (-cos - F_S sin + G_Y)       R dw_Y = b (F_X sin - G_X)
//   R dw_Z = -b F_X cos                   dW = v_Y cos   (normal work at I; energetic restitution)
// Sliding contacts: F = -mu_w s_I_hat, G = -mu_s N_C s_C_hat; this is the spec's equation set (4.5) term by term.
// The slips (4.2) are linear in the state, and their rates are linear in the friction impulses (Delassus form):
//   s_I' = (0, a sin cos) + diag(a + b, a sin^2 + b) F + diag(a - b sin, b - a sin) G
//   s_C' = (0, -a cos)    + diag(a - b sin, b - a sin) F + (a + b) G
// (X and S/Y decouple; the contacts couple only through the Coulomb directions and N_C.)
//
// Two integrators:
//  * SplitAtSlipReversal = false: the spec's algorithm verbatim (fixed RK4 steps dP = (1 + e) m v_Y / N, friction
//    off below s_eps, trapezoid work, bisection on v_Y = 0 and on the work target). First order where a slip passes
//    through zero (the right-hand side is discontinuous there): used by M-6 and as the accuracy-gate reference.
//  * SplitAtSlipReversal = true (default): the same equations with the Coulomb (Filippov) contact law made exact at
//    slip zeros, which restores 4th-order convergence (DERIVED, the spec's "optional accuracy upgrade" of 4.5):
//      - each contact is Slip (friction against its slip direction, continued smoothly through a zero crossing inside
//        a step), Stick (the friction that keeps its slip rate at zero, in closed form from the Delassus form), or
//        Young (a slip that has just left zero: its direction follows the quasi-static solution ds/dP = rho d,
//        rho >= 0, until |s| is large enough for the explicit direction dynamics; closed forms except one 2x2
//        secular equation);
//      - RK4 steps of nominal length (1 + e) m v_Y / N, shortened where a slipping contact's direction turns fast or
//        its direction dynamics become stiff (relaxation rate kappa / |s|, kappa = mu N times the contact's inverse
//        mass): the turn per step is bounded and h <= kStabilityFactor |s| / kappa; a slip that runs straight into
//        zero within the step is integrated with its direction frozen;
//      - events located inside a step on the cubic Hermite interpolant of the step and landed on with one RK4 step
//        (polished on the RK4 map when needed): end of compression (v_Y = 0), end of restitution (W = (1 - e^2) W_c,
//        W integrated as a 6th state), a slip reaching zero, a stick losing feasibility (|friction| = mu N);
//      - at a slip zero the contact sticks if the required friction is feasible, otherwise it leaves as Young;
//        both contacts at zero are decided jointly (stick-stick, stick-leave, leave-stick, leave-leave).
//    The step count N is CushionParams::MathavanSteps = 8, set by the accuracy gate A-CUSH-1
//    (Tests/Core/Cushion/TestMathavanGate.cpp): M-2..M-4 within 1.0e-4 of the N = 20 000 reference (tolerance 2e-4),
//    every larger N passes too; 4th-order convergence above N = 12. The slip-slip evaluation (the common mode) has a
//    fast path with the normalisations folded into its products (EvaluateSlipSlip); ~1.9 us per typical hit (A-CUSH-2).
//    If the loop budget runs out (rare: a slip creeping just above s_eps, see IntegrateSplit), the plain integrator
//    (N >= 200) redoes the impact.
#include "rb/Physics/Cushion.h"

#include "rb/Core/Assert.h"
#include "rb/Core/Constants.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec2.h"

#include <cstdint>

namespace rb
{
	namespace
	{
		// Upper bound on MathavanSettings::Steps (keeps the loop budgets 64 N in int range).
		constexpr int kMaxMathavanSteps = 1 << 22;

		// State layout: velocities and spins in the cushion-local frame, plus the normal work W at I.
		enum StateIndex : int
		{
			kVX = 0,
			kVY = 1,
			kWX = 2,
			kWY = 3,
			kWZ = 4,
			kWork = 5,
			kStateSize = 6,
		};

		struct State
		{
			double Y[kStateSize] = {};
		};

		// Contact impulses per unit dP.
		struct Friction
		{
			double FX = 0.0; // cushion friction along X_hat
			double FS = 0.0; // cushion friction along S_hat
			double GX = 0.0; // cloth friction along X_hat
			double GY = 0.0; // cloth friction along Y_hat
			double NC = 0.0; // table normal impulse
		};

		struct Model
		{
			double R = 0.0;
			double Sin = 0.0;
			double Cos = 0.0;
			double MuW = 0.0;
			double MuS = 0.0;
			double SlipEps = 0.0;
			double A = 0.0;  // 1/m
			double B = 0.0;  // R^2/I
			double BR = 0.0; // R/I
			// Delassus blocks (header comment): s_I' = (0, CIs) + diag(DxII, DsII) F + diag(DxIC, DsIC) G,
			// s_C' = (0, CCy) + diag(DxIC, DsIC) F + DCC G.
			double DxII = 0.0;
			double DsII = 0.0;
			double DxIC = 0.0;
			double DsIC = 0.0;
			double DCC = 0.0;
			double CIs = 0.0;
			double CCy = 0.0;
			// Both contacts sticking (constant impulses): F = (0, SSFS), G = (0, SSGY), N_C = SSNC.
			double SSFS = 0.0;
			double SSGY = 0.0;
			double SSNC = 0.0;
			// Cushion slip against a sticking cloth: s_I' = (0, CtS) + diag(KIx, KIs) F (Schur complement).
			double KIx = 0.0;
			double KIs = 0.0;
			double CtS = 0.0;
			// Friction response of a slip's direction (stiffness bound kappa; the direction relaxes at kappa / |s|).
			double KappaI = 0.0;     // cushion: mu_w (a + b)(1 + mu_s)
			double KappaCPerN = 0.0; // cloth: mu_s (a + b) per unit N_C
			// Products used by the slip-slip fast path (EvaluateSlipSlip).
			double RSin = 0.0;     // R sin
			double RCos = 0.0;     // R cos
			double MuWCos = 0.0;   // mu_w cos
			double AMuS = 0.0;     // a mu_s
			double BRMuS = 0.0;    // (R/I) mu_s
			double ACos = 0.0;     // a cos
			double ASin = 0.0;     // a sin
			double BRSin = 0.0;    // (R/I) sin
			double BRCos = 0.0;    // (R/I) cos
			bool TableImpulseNonNegative = true; // mu_w cos <= sin: N_C = sin + mu_w cos s_hat_S >= 0 needs no clamp
		};

		Model MakeModel(const BallSpec& Spec, const MathavanSettings& Settings)
		{
			Model M;
			M.R = Spec.Radius;
			M.Sin = Sin(Settings.Elevation);
			M.Cos = Cos(Settings.Elevation);
			M.MuW = Max(0.0, Settings.CushionFriction);
			M.MuS = Max(0.0, Settings.ClothFriction);
			M.SlipEps = Settings.SlipEps;
			M.A = 1.0 / Spec.Mass;
			M.B = Spec.Radius * Spec.Radius / Spec.Inertia;
			M.BR = Spec.Radius / Spec.Inertia;
			M.DxII = M.A + M.B;
			M.DsII = M.A * M.Sin * M.Sin + M.B;
			M.DxIC = M.A - M.B * M.Sin;
			M.DsIC = M.B - M.A * M.Sin;
			M.DCC = M.A + M.B;
			M.CIs = M.A * M.Sin * M.Cos;
			M.CCy = -M.A * M.Cos;
			// [DsII DsIC; DsIC DCC] [F_S; G_Y] = -[CIs; CCy]
			const double Det = M.DsII * M.DCC - M.DsIC * M.DsIC;
			M.SSFS = (-M.CIs * M.DCC + M.DsIC * M.CCy) / Det;
			M.SSGY = (-M.DsII * M.CCy + M.DsIC * M.CIs) / Det;
			M.SSNC = M.Sin - M.Cos * M.SSFS;
			M.KIx = M.DxII - M.DxIC * M.DxIC / M.DCC;
			M.KIs = M.DsII - M.DsIC * M.DsIC / M.DCC;
			M.CtS = M.CIs - M.DsIC * M.CCy / M.DCC;
			M.KappaI = M.MuW * (M.A + M.B) * (1.0 + M.MuS);
			M.KappaCPerN = M.MuS * (M.A + M.B);
			M.RSin = M.R * M.Sin;
			M.RCos = M.R * M.Cos;
			M.MuWCos = M.MuW * M.Cos;
			M.AMuS = M.A * M.MuS;
			M.BRMuS = M.BR * M.MuS;
			M.ACos = M.A * M.Cos;
			M.ASin = M.A * M.Sin;
			M.BRSin = M.BR * M.Sin;
			M.BRCos = M.BR * M.Cos;
			M.TableImpulseNonNegative = M.MuWCos <= M.Sin;
			return M;
		}

		// Slip velocities (4.2): at I (s_X, s_S) and at C (s_XC, s_YC); v_Z = 0. The same linear maps give the slip
		// rates from a state derivative.
		inline Vec2 CushionSlip(const Model& M, const double* Y)
		{
			return {Y[kVX] + M.RSin * Y[kWY] - M.RCos * Y[kWZ], -M.Sin * Y[kVY] + M.R * Y[kWX]};
		}

		inline Vec2 ClothSlip(const Model& M, const double* Y)
		{
			return {Y[kVX] - M.R * Y[kWY], Y[kVY] + M.R * Y[kWX]};
		}

		inline Vec2 ContactSlip(const Model& M, const double* Y, int Contact)
		{
			return Contact == 0 ? CushionSlip(M, Y) : ClothSlip(M, Y);
		}

		inline void Derivative(const Model& M, const double* Y, const Friction& F, double* DY)
		{
			DY[kVX] = M.A * (F.FX + F.GX);
			DY[kVY] = M.A * (-M.Cos - F.FS * M.Sin + F.GY);
			DY[kWX] = M.BR * (F.FS + F.GY);
			DY[kWY] = M.BR * (F.FX * M.Sin - F.GX);
			DY[kWZ] = -M.BR * F.FX * M.Cos;
			DY[kWork] = Y[kVY] * M.Cos;
		}

		// ---------------------------------------------------------------------------------------------------------
		// Plain integrator: the spec's algorithm (fixed steps, friction off below s_eps, trapezoid work, bisection).
		// ---------------------------------------------------------------------------------------------------------
		Friction PlainFriction(const Model& M, const double* Y)
		{
			Friction F;
			const Vec2 SI = CushionSlip(M, Y);
			const double NI = Length(SI);
			if (NI > M.SlipEps)
			{
				F.FX = -M.MuW * SI.x / NI;
				F.FS = -M.MuW * SI.y / NI;
			}
			F.NC = Max(0.0, M.Sin - F.FS * M.Cos);
			const Vec2 SC = ClothSlip(M, Y);
			const double NCs = Length(SC);
			if (NCs > M.SlipEps)
			{
				F.GX = -M.MuS * F.NC * SC.x / NCs;
				F.GY = -M.MuS * F.NC * SC.y / NCs;
			}
			return F;
		}

		void PlainStep(const Model& M, const State& Y0, double H, State& Out)
		{
			double K1[kStateSize];
			double K2[kStateSize];
			double K3[kStateSize];
			double K4[kStateSize];
			double T[kStateSize];
			Derivative(M, Y0.Y, PlainFriction(M, Y0.Y), K1);
			for (int i = 0; i < kStateSize; ++i)
			{
				T[i] = Y0.Y[i] + 0.5 * H * K1[i];
			}
			Derivative(M, T, PlainFriction(M, T), K2);
			for (int i = 0; i < kStateSize; ++i)
			{
				T[i] = Y0.Y[i] + 0.5 * H * K2[i];
			}
			Derivative(M, T, PlainFriction(M, T), K3);
			for (int i = 0; i < kStateSize; ++i)
			{
				T[i] = Y0.Y[i] + H * K3[i];
			}
			Derivative(M, T, PlainFriction(M, T), K4);
			const double Sixth = H / 6.0;
			for (int i = 0; i < kStateSize; ++i)
			{
				Out.Y[i] = Y0.Y[i] + Sixth * (K1[i] + 2.0 * K2[i] + 2.0 * K3[i] + K4[i]);
			}
		}

		void IntegratePlain(const Model& M, State& Y, double Restitution, int Steps, int MaxBisections, double& ImpulseOut)
		{
			const double InitialVY = Y.Y[kVY];
			const double Dp = (1.0 + Restitution) * InitialVY / (M.A * static_cast<double>(Steps));
			// v_Y decreases at >= 0.8 a per unit P (friction cannot reverse the normal push for mu_w < tan(theta)), so both
			// phases end well inside this bound; it only guards against non-physical inputs.
			const int MaxLoop = 64 * Steps + 64;
			double Impulse = 0.0;
			double WorkCompression = 0.0;
			State Next;
			State Mid;

			// Compression: until v_Y = 0 (bisection of the last step, landing on the v_Y <= 0 side).
			for (int Loop = 0; Loop < MaxLoop; ++Loop)
			{
				PlainStep(M, Y, Dp, Next);
				if (Next.Y[kVY] <= 0.0)
				{
					double Lo = 0.0;
					double Hi = Dp;
					for (int k = 0; k < MaxBisections; ++k)
					{
						const double Half = 0.5 * (Lo + Hi);
						if (!(Half > Lo && Half < Hi))
						{
							break; // the bracket cannot shrink any more (every further halving would be a no-op)
						}
						PlainStep(M, Y, Half, Mid);
						if (Mid.Y[kVY] > 0.0)
						{
							Lo = Half;
						}
						else
						{
							Hi = Half;
						}
					}
					PlainStep(M, Y, Hi, Mid);
					WorkCompression += 0.5 * Hi * (Y.Y[kVY] + Mid.Y[kVY]) * M.Cos;
					Impulse += Hi;
					Y = Mid;
					Y.Y[kVY] = 0.0;
					break;
				}
				WorkCompression += 0.5 * Dp * (Y.Y[kVY] + Next.Y[kVY]) * M.Cos;
				Impulse += Dp;
				Y = Next;
			}
			if (!(Restitution > 0.0))
			{
				ImpulseOut = Impulse;
				return;
			}

			// Restitution: until the restitution work reaches e^2 W_c (bisection of the last step on the target).
			const double Target = Restitution * Restitution * WorkCompression;
			double WorkRestitution = 0.0;
			for (int Loop = 0; Loop < MaxLoop; ++Loop)
			{
				PlainStep(M, Y, Dp, Next);
				const double Dw = 0.5 * Dp * (Abs(Y.Y[kVY]) + Abs(Next.Y[kVY])) * M.Cos;
				if (WorkRestitution + Dw >= Target)
				{
					double Lo = 0.0;
					double Hi = Dp;
					for (int k = 0; k < MaxBisections; ++k)
					{
						const double Half = 0.5 * (Lo + Hi);
						if (!(Half > Lo && Half < Hi))
						{
							break;
						}
						PlainStep(M, Y, Half, Mid);
						const double DwMid = 0.5 * Half * (Abs(Y.Y[kVY]) + Abs(Mid.Y[kVY])) * M.Cos;
						if (WorkRestitution + DwMid < Target)
						{
							Lo = Half;
						}
						else
						{
							Hi = Half;
						}
					}
					PlainStep(M, Y, Hi, Mid);
					Impulse += Hi;
					Y = Mid;
					break;
				}
				WorkRestitution += Dw;
				Impulse += Dp;
				Y = Next;
			}
			ImpulseOut = Impulse;
		}

		// ---------------------------------------------------------------------------------------------------------
		// Split integrator: Filippov contacts, events located inside steps.
		// ---------------------------------------------------------------------------------------------------------

		// Step control (DERIVED/TUNING, chosen with the accuracy gate A-CUSH-1 and the random-impact sweep of
		// Tests/Core/Cushion/TestMathavanGate.cpp).
		constexpr double kTanMaxTurnPerStep = 0.2553419212210362; // tan(0.25 rad): largest turn of a slip direction per step
		constexpr double kStabilityFactor = 2.0;   // h <= this |s| / kappa (RK4 is stable to 2.78 on the negative real axis)
		constexpr double kRadialTolerance = 1e-3;  // |ds/dP x s_hat| <= this |ds/dP . s_hat|: the slip runs straight at zero
		constexpr double kYoungGrowFactor = 0.1;   // a Young slip becomes a Slip at |s| >= this kappa h
		constexpr double kZeroFraction = 1e-6;     // a slip below max(s_eps, this |ds/dP| h) at a step start is zero
		constexpr double kLandedZeroFraction = 1e-3; // a located slip zero with |s| below max(s_eps, this |ds/dP| h) is zero

		enum class Mode : std::uint8_t
		{
			Slip,  // friction -mu N s_hat (continued through a zero crossing inside a step)
			Stick, // friction keeps the slip rate at zero; event when it reaches the cone
			Young, // a slip that has just left zero: quasi-static direction (or the stick friction while feasible)
		};

		// Contact index: 0 = cushion contact I (slip components X, S), 1 = cloth contact C (slip components X, Y).
		struct Contacts
		{
			Mode M[2] = {Mode::Slip, Mode::Slip};
			Vec2 Ref[2] = {{1.0, 0.0}, {1.0, 0.0}}; // Slip: direction at the last accepted point; fixed Young directions
			bool Frozen[2] = {false, false};         // Slip: direction held at Ref for this step (straight run into zero)
		};

		inline Vec2 SlipDirection(const Model& M, const double* Y, const Contacts& C, int Contact)
		{
			if (C.Frozen[Contact])
			{
				return C.Ref[Contact];
			}
			const Vec2 S = ContactSlip(M, Y, Contact);
			const double N2 = LengthSquared(S);
			if (!(N2 > M.SlipEps * M.SlipEps))
			{
				return C.Ref[Contact];
			}
			double Inv = 1.0 / Sqrt(N2);
			if (Dot(S, C.Ref[Contact]) < 0.0)
			{
				Inv = -Inv; // past a zero crossing inside the step: the direction is continued smoothly
			}
			return S * Inv;
		}

		// The same direction as SlipDirection, as the pair (S, Q) with direction S / Q: S is the slip (negated past a zero
		// crossing inside the step) and Q = |S|, or S = Ref and Q = 1 for a held direction. Lets the fast path fold the
		// normalisation into its products.
		inline double DirectionDivisor(const Model& M, const Contacts& C, int Contact, Vec2& S)
		{
			if (C.Frozen[Contact])
			{
				S = C.Ref[Contact];
				return 1.0;
			}
			const double N2 = LengthSquared(S);
			if (!(N2 > M.SlipEps * M.SlipEps))
			{
				S = C.Ref[Contact];
				return 1.0;
			}
			if (Dot(S, C.Ref[Contact]) < 0.0)
			{
				S = -S;
			}
			return Sqrt(N2);
		}

		// Cushion slipping along D.
		inline void SetCushionSlip(const Model& M, const Vec2& D, Friction& F)
		{
			F.FX = -M.MuW * D.x;
			F.FS = -M.MuW * D.y;
			F.NC = Max(0.0, M.Sin - M.Cos * F.FS);
		}

		// Cloth slipping along D (F.NC already set).
		inline void SetClothSlip(const Model& M, const Vec2& D, Friction& F)
		{
			F.GX = -M.MuS * F.NC * D.x;
			F.GY = -M.MuS * F.NC * D.y;
		}

		// Cloth sticking for a known cushion friction: s_C' = 0.
		inline void SetClothStick(const Model& M, Friction& F)
		{
			F.GX = -M.DxIC * F.FX / M.DCC;
			F.GY = -(M.CCy + M.DsIC * F.FS) / M.DCC;
		}

		// Young cloth for a known cushion friction. The cloth response is isotropic, s_C' = c + (a + b) G with
		// c = (0, CCy) + D_IC F: it sticks if |c| <= mu_s N_C (a + b), otherwise it slips along c.
		inline void SetClothYoung(const Model& M, Friction& F)
		{
			const double CX = M.DxIC * F.FX;
			const double CY = M.CCy + M.DsIC * F.FS;
			const double Norm = Sqrt(CX * CX + CY * CY);
			if (Norm > M.MuS * F.NC * M.DCC)
			{
				const double Scale = -M.MuS * F.NC / Norm;
				F.GX = Scale * CX;
				F.GY = Scale * CY;
			}
			else
			{
				F.GX = -CX / M.DCC;
				F.GY = -CY / M.DCC;
			}
		}

		// Cushion sticking while the cloth slips along D: s_I' = 0 with G = -mu_s (sin - cos F_S) D (linear in F_S).
		inline void SetCushionStickClothSlip(const Model& M, const Vec2& D, Friction& F)
		{
			const double Denominator = M.DsII + M.DsIC * M.MuS * M.Cos * D.y;
			F.FS = (-M.CIs + M.DsIC * M.MuS * M.Sin * D.y) / Denominator;
			F.NC = M.Sin - M.Cos * F.FS;
			if (F.NC < 0.0)
			{
				// The ball would lift off the slate (only for mu_w > tan(theta)): no table impulse.
				F.NC = 0.0;
				F.FS = -M.CIs / M.DsII;
			}
			F.GX = -M.MuS * F.NC * D.x;
			F.GY = -M.MuS * F.NC * D.y;
			F.FX = -M.DxIC * F.GX / M.DxII;
		}

		// Young cushion while the cloth slips along D. s_I'(d) = c + B d with F = -mu_w d is affine and upper triangular:
		//   c = (0, CIs) - mu_s sin u,  B = -mu_w [[DxII, mu_s cos u_X], [0, DsII + mu_s cos u_Y]],  u = D_IC D.
		// Stick if the solution of c + B d = 0 has |d| <= 1; otherwise the leaving direction d(lambda) = (lambda I - B)^-1 c
		// with |d| = 1, lambda >= 0 (|d(lambda)| decreases from |d(0)| > 1). Safeguarded Newton on 1/|d| - 1, which is
		// linear in lambda for a diagonal B (the secular-equation form of trust-region solvers): 2-4 iterations.
		void SetCushionYoungClothSlip(const Model& M, const Vec2& D, Friction& F)
		{
			const double UX = M.DxIC * D.x;
			const double UY = M.DsIC * D.y;
			const double CX = -M.MuS * M.Sin * UX;
			const double CY = M.CIs - M.MuS * M.Sin * UY;
			const double B11 = -M.MuW * M.DxII;
			const double B12 = -M.MuW * M.MuS * M.Cos * UX;
			const double B22 = -M.MuW * (M.DsII + M.MuS * M.Cos * UY);
			Vec2 Dir;
			if (M.MuW > 0.0)
			{
				const double StickS = -CY / B22;
				const double StickX = -(CX + B12 * StickS) / B11;
				if (StickX * StickX + StickS * StickS <= 1.0)
				{
					Dir = {StickX, StickS};
				}
				else
				{
					double Lo = 0.0;
					double Hi = Abs(CX) + Abs(CY) + Abs(B12) + 1e-300;
					double Lambda = 0.0;
					for (int Iteration = 0; Iteration < 64; ++Iteration)
					{
						const double InvS = 1.0 / (Lambda - B22);
						const double InvX = 1.0 / (Lambda - B11);
						const double DS = CY * InvS;
						const double DX = (CX + B12 * DS) * InvX;
						const double N2 = DX * DX + DS * DS;
						if (N2 > 1.0)
						{
							Lo = Lambda;
						}
						else
						{
							Hi = Lambda;
						}
						// phi = 1/|d| - 1, phi' = -(d . d') / |d|^3: Newton step n^2 (1 - n) / (d . d').
						const double DSd = -DS * InvS;
						const double DXd = (B12 * DSd - DX) * InvX;
						const double Slope = DX * DXd + DS * DSd;
						const double N = Sqrt(N2);
						double Next = Slope < 0.0 ? Lambda + N2 * (1.0 - N) / Slope : 0.5 * (Lo + Hi);
						if (!(Next > Lo && Next < Hi))
						{
							Next = 0.5 * (Lo + Hi);
						}
						const double Change = Abs(Next - Lambda);
						Lambda = Next;
						if (Change <= 1e-14 * (Lambda - B11))
						{
							break;
						}
					}
					const double DS = CY / (Lambda - B22);
					const double DX = (CX + B12 * DS) / (Lambda - B11);
					const double N = Sqrt(DX * DX + DS * DS);
					Dir = N > 0.0 ? Vec2{DX / N, DS / N} : Vec2{0.0, 1.0};
				}
			}
			SetCushionSlip(M, Dir, F);
			SetClothSlip(M, D, F);
		}

		// Young cushion against a sticking cloth: s_I' = (0, CtS) + diag(KIx, KIs) F leaves along +-S_hat (the X
		// response has no forcing), or sticks (both contacts sticking).
		inline void SetCushionYoungClothStick(const Model& M, Friction& F)
		{
			if (Abs(M.CtS) > M.MuW * M.KIs)
			{
				SetCushionSlip(M, {0.0, SignNonZero(M.CtS)}, F);
			}
			else
			{
				F.FX = 0.0;
				F.FS = M.SSFS;
				F.NC = M.SSNC;
			}
			SetClothStick(M, F);
		}

		// The leaving direction of a Young cloth against a sticking cushion is +-Y_hat (the X response has no forcing):
		// the sign whose slip rate points along it. Returns 0 if neither leaves (the cloth can stick).
		double ClothLeaveSignCushionStick(const Model& M)
		{
			for (int k = 0; k < 2; ++k)
			{
				const double Sigma = k == 0 ? -SignNonZero(M.SSGY) : SignNonZero(M.SSGY);
				Friction F;
				SetCushionStickClothSlip(M, {0.0, Sigma}, F);
				const double RateY = M.CCy + M.DsIC * F.FS + M.DCC * F.GY;
				if (Sigma * RateY > 0.0)
				{
					return Sigma;
				}
			}
			return 0.0;
		}

		// Contact impulses for the given modes at the state Y.
		Friction ComputeFriction(const Model& M, const double* Y, const Contacts& C)
		{
			Friction F;
			const Mode MI = C.M[0];
			const Mode MC = C.M[1];
			if (MI == Mode::Slip)
			{
				SetCushionSlip(M, SlipDirection(M, Y, C, 0), F);
				if (MC == Mode::Slip)
				{
					SetClothSlip(M, SlipDirection(M, Y, C, 1), F);
				}
				else if (MC == Mode::Stick)
				{
					SetClothStick(M, F);
				}
				else
				{
					SetClothYoung(M, F);
				}
			}
			else if (MI == Mode::Stick)
			{
				if (MC == Mode::Stick)
				{
					F.FS = M.SSFS;
					F.GY = M.SSGY;
					F.NC = M.SSNC;
				}
				else
				{
					// Slip, or Young with its fixed leaving direction +-Y_hat (Ref).
					SetCushionStickClothSlip(M, MC == Mode::Slip ? SlipDirection(M, Y, C, 1) : C.Ref[1], F);
				}
			}
			else
			{
				if (MC == Mode::Slip)
				{
					SetCushionYoungClothSlip(M, SlipDirection(M, Y, C, 1), F);
				}
				else if (MC == Mode::Stick)
				{
					SetCushionYoungClothStick(M, F);
				}
				else
				{
					// Both leaving zero together: the joint quasi-static directions fixed at the decision.
					SetCushionSlip(M, C.Ref[0], F);
					SetClothSlip(M, C.Ref[1], F);
				}
			}
			return F;
		}

		// Both contacts slipping (the common mode): the same impulses as SetCushionSlip + SetClothSlip + Derivative, with the
		// normalisations folded into the products so that the chain slip -> |s_I| -> N_C -> derivative is short (this
		// evaluation is the inner loop of the integrator).
		inline void EvaluateSlipSlip(const Model& M, const double* Y, const Contacts& C, double* DY, Friction& F)
		{
			Vec2 SI = CushionSlip(M, Y);
			Vec2 SC = ClothSlip(M, Y);
			const double QI = DirectionDivisor(M, C, 0, SI);
			const double QC = DirectionDivisor(M, C, 1, SC);
			// Independent divisions (they pipeline) rather than a reciprocal and products: shorter dependency chains.
			const double CX = SC.x / QC;
			const double CY = SC.y / QC;
			const double FX = (-M.MuW * SI.x) / QI;
			const double FS = (-M.MuW * SI.y) / QI;
			const double NCVariable = (M.MuWCos * SI.y) / QI; // N_C = sin - cos F_S = sin + this
			const double KX = M.AMuS * CX;
			const double KY = M.AMuS * CY;
			const double LX = M.BRMuS * CX;
			const double LY = M.BRMuS * CY;
			double NC;
			if (M.TableImpulseNonNegative)
			{
				// N_C >= sin - mu_w cos > 0: the cloth terms split into a part known early and one after the division.
				NC = M.Sin + NCVariable;
				DY[kVX] = (M.A * FX - KX * M.Sin) - KX * NCVariable;
				DY[kVY] = ((-M.ACos - M.ASin * FS) - KY * M.Sin) - KY * NCVariable;
				DY[kWX] = (M.BR * FS - LY * M.Sin) - LY * NCVariable;
				DY[kWY] = (M.BRSin * FX + LX * M.Sin) + LX * NCVariable;
			}
			else
			{
				NC = Max(0.0, M.Sin + NCVariable);
				DY[kVX] = M.A * FX - KX * NC;
				DY[kVY] = (-M.ACos - M.ASin * FS) - KY * NC;
				DY[kWX] = M.BR * FS - LY * NC;
				DY[kWY] = M.BRSin * FX + LX * NC;
			}
			DY[kWZ] = -M.BRCos * FX;
			DY[kWork] = Y[kVY] * M.Cos;
			F.FX = FX;
			F.FS = FS;
			F.NC = NC;
			F.GX = -M.MuS * NC * CX;
			F.GY = -M.MuS * NC * CY;
		}

		inline void Evaluate(const Model& M, const double* Y, const Contacts& C, double* DY, Friction& F)
		{
			if (C.M[0] == Mode::Slip && C.M[1] == Mode::Slip)
			{
				EvaluateSlipSlip(M, Y, C, DY, F);
				return;
			}
			F = ComputeFriction(M, Y, C);
			Derivative(M, Y, F, DY);
		}

		inline void Evaluate(const Model& M, const double* Y, const Contacts& C, double* DY)
		{
			Friction F;
			Evaluate(M, Y, C, DY, F);
		}

		// Classic RK4 step of length H from Y0; K1 = f(Y0) is passed in (shared by every trial step length). K4Out (optional)
		// receives the last stage derivative f(Y0 + H K3), an O(H) approximation of f at the step end.
		void Rk4(const Model& M, const State& Y0, const double* K1, double H, const Contacts& C, State& Out, double* K4Out = nullptr)
		{
			double K2[kStateSize];
			double K3[kStateSize];
			double K4Local[kStateSize];
			double* K4 = K4Out != nullptr ? K4Out : K4Local;
			double T[kStateSize];
			const double Half = 0.5 * H;
			for (int i = 0; i < kStateSize; ++i)
			{
				T[i] = Y0.Y[i] + Half * K1[i];
			}
			Evaluate(M, T, C, K2);
			for (int i = 0; i < kStateSize; ++i)
			{
				T[i] = Y0.Y[i] + Half * K2[i];
			}
			Evaluate(M, T, C, K3);
			for (int i = 0; i < kStateSize; ++i)
			{
				T[i] = Y0.Y[i] + H * K3[i];
			}
			Evaluate(M, T, C, K4);
			const double Sixth = H / 6.0;
			for (int i = 0; i < kStateSize; ++i)
			{
				Out.Y[i] = Y0.Y[i] + Sixth * (K1[i] + 2.0 * K2[i] + 2.0 * K3[i] + K4[i]);
			}
		}

		// Stick margin of a contact (mu N - |friction| per unit dP; >= 0 while sticking is feasible).
		inline double StickMargin(const Model& M, const Friction& F, int Contact)
		{
			if (Contact == 0)
			{
				return M.MuW - Sqrt(F.FX * F.FX + F.FS * F.FS);
			}
			return M.MuS * F.NC - Sqrt(F.GX * F.GX + F.GY * F.GY);
		}

		// ---- Mode decisions ---------------------------------------------------------------------------------------

		// Joint decision when both contacts are at zero slip (a zero slip meeting a sticking or just-released contact):
		// stick-stick if feasible; otherwise the combination that lets the infeasible contact(s) slip.
		void DecideJoint(const Model& M, Contacts& C)
		{
			constexpr double kTolerance = 1e-12;
			const bool CushionHolds = Abs(M.SSFS) <= M.MuW * (1.0 + kTolerance);
			const bool ClothHolds = Abs(M.SSGY) <= M.MuS * M.SSNC * (1.0 + kTolerance);
			if (CushionHolds && ClothHolds)
			{
				C.M[0] = Mode::Stick;
				C.M[1] = Mode::Stick;
				return;
			}

			// Cushion sticks, cloth leaves along +-Y_hat.
			auto TryCushionStick = [&]() -> bool
			{
				const double Sigma = ClothLeaveSignCushionStick(M);
				if (Sigma == 0.0)
				{
					return false;
				}
				Friction F;
				SetCushionStickClothSlip(M, {0.0, Sigma}, F);
				if (StickMargin(M, F, 0) < -kTolerance * M.MuW)
				{
					return false;
				}
				C.M[0] = Mode::Stick;
				C.M[1] = Mode::Young;
				C.Ref[1] = {0.0, Sigma};
				return true;
			};
			// Cloth sticks, cushion leaves along +-S_hat.
			auto TryClothStick = [&]() -> bool
			{
				if (!(Abs(M.CtS) > M.MuW * M.KIs))
				{
					return false;
				}
				Friction F;
				SetCushionYoungClothStick(M, F);
				if (StickMargin(M, F, 1) < -kTolerance * M.MuS * F.NC)
				{
					return false;
				}
				C.M[0] = Mode::Young;
				C.M[1] = Mode::Stick;
				return true;
			};

			if (!CushionHolds && ClothHolds)
			{
				if (TryClothStick() || TryCushionStick())
				{
					return;
				}
			}
			else if (CushionHolds && !ClothHolds)
			{
				if (TryCushionStick() || TryClothStick())
				{
					return;
				}
			}

			// Both leave zero: joint quasi-static directions by fixed-point iteration (cloth along its forcing, cushion
			// from the secular equation), started from the cushion's leaving direction against a sticking cloth.
			Vec2 DI = {0.0, SignNonZero(M.CtS)};
			Vec2 DC = {0.0, -SignNonZero(M.SSGY)};
			for (int Iteration = 0; Iteration < 32; ++Iteration)
			{
				Friction F;
				SetCushionSlip(M, DI, F);
				const Vec2 Forcing = {M.DxIC * F.FX, M.CCy + M.DsIC * F.FS};
				const double NormForcing = Length(Forcing);
				if (NormForcing > 0.0)
				{
					DC = Forcing / NormForcing;
				}
				Friction G;
				SetCushionYoungClothSlip(M, DC, G);
				const Vec2 NextDI = M.MuW > 0.0 ? Vec2{-G.FX / M.MuW, -G.FS / M.MuW} : DI;
				const double Change = LengthSquared(NextDI - DI);
				DI = NextDI;
				if (Change <= 1e-28)
				{
					break;
				}
			}
			const double NI = Length(DI);
			C.M[0] = Mode::Young;
			C.M[1] = Mode::Young;
			C.Ref[0] = NI > 0.0 ? DI / NI : Vec2{0.0, 1.0};
			C.Ref[1] = DC;
		}

		// Decision for a contact whose slip is zero while the other contact slips: stick if the friction that keeps it
		// at zero is feasible, otherwise it leaves zero (Young). If the other contact is not slipping (Stick, Young) it is
		// at zero too: joint decision.
		void DecideAtZero(const Model& M, const double* Y, int Contact, Contacts& C)
		{
			const int Other = 1 - Contact;
			if (C.M[Other] != Mode::Slip)
			{
				DecideJoint(M, C);
				return;
			}
			Contacts Trial = C;
			Trial.M[Contact] = Mode::Stick;
			Trial.Frozen[Other] = false;
			const Friction F = ComputeFriction(M, Y, Trial);
			const double Scale = Contact == 0 ? M.MuW : M.MuS * F.NC;
			C.M[Contact] = StickMargin(M, F, Contact) >= -1e-12 * Scale ? Mode::Stick : Mode::Young;
		}

		// ---- Events -----------------------------------------------------------------------------------------------

		enum class EventKind : std::uint8_t
		{
			None,
			CompressionEnd,
			RestitutionEnd,
			SlipZero,   // a Slip contact's slip crosses the normal line of its reference direction (a zero, or a turn past 90 deg)
			StickBreak, // a Stick contact's required friction reaches mu N
		};

		struct EventContext
		{
			double WorkTarget = 0.0;
		};

		inline double EventValue(const Model& M, EventKind Kind, int Contact, const double* Y, const Contacts& C, const EventContext& Context)
		{
			switch (Kind)
			{
			case EventKind::CompressionEnd:
				return Y[kVY];
			case EventKind::RestitutionEnd:
				return Y[kWork] - Context.WorkTarget;
			case EventKind::SlipZero:
				return Dot(ContactSlip(M, Y, Contact), C.Ref[Contact]);
			case EventKind::StickBreak:
				return StickMargin(M, ComputeFriction(M, Y, C), Contact);
			case EventKind::None:
			default:
				break;
			}
			return 1.0;
		}

		// Cubic Hermite interpolant of the step (Y0, K0) -> (Y1, K1) of length H at Theta in [0, 1].
		inline void HermiteState(const State& Y0, const double* K0, const State& Y1, const double* K1, double H, double Theta, State& Out)
		{
			const double T2 = Theta * Theta;
			const double T3 = T2 * Theta;
			const double H00 = 2.0 * T3 - 3.0 * T2 + 1.0;
			const double H10 = (T3 - 2.0 * T2 + Theta) * H;
			const double H01 = -2.0 * T3 + 3.0 * T2;
			const double H11 = (T3 - T2) * H;
			for (int i = 0; i < kStateSize; ++i)
			{
				Out.Y[i] = H00 * Y0.Y[i] + H10 * K0[i] + H01 * Y1.Y[i] + H11 * K1[i];
			}
		}

		// Illinois (modified regula falsi) iteration for a root of G in (Lo, Hi) with G(Lo) > 0 >= G(Hi); Eval(Theta)
		// returns G. Returns a Theta with |G| <= Tolerance, or the upper end of the final bracket.
		template <typename EvalFn>
		double Illinois(double Lo, double Hi, double GLo, double GHi, int MaxIterations, double Tolerance, double MinWidth, EvalFn&& Eval)
		{
			int Side = 0;
			for (int Iteration = 0; Iteration < MaxIterations; ++Iteration)
			{
				double Theta = Hi - GHi * (Hi - Lo) / (GHi - GLo);
				if (!(Theta > Lo && Theta < Hi))
				{
					Theta = 0.5 * (Lo + Hi);
				}
				const double G = Eval(Theta);
				if (Abs(G) <= Tolerance)
				{
					return Theta;
				}
				if (G > 0.0)
				{
					Lo = Theta;
					GLo = G;
					if (Side == 1)
					{
						GHi *= 0.5;
					}
					Side = 1;
				}
				else
				{
					Hi = Theta;
					GHi = G;
					if (Side == -1)
					{
						GLo *= 0.5;
					}
					Side = -1;
				}
				if (Hi - Lo <= MinWidth)
				{
					break;
				}
			}
			return Hi;
		}

		// Rate dg/dP of a linear event function along the flow (DY = f(Y)); 0 for the non-linear stick margin.
		inline double EventSlope(const Model& M, EventKind Kind, int Contact, const double* DY, const Contacts& C)
		{
			switch (Kind)
			{
			case EventKind::CompressionEnd:
				return DY[kVY];
			case EventKind::RestitutionEnd:
				return DY[kWork];
			case EventKind::SlipZero:
				return Dot(ContactSlip(M, DY, Contact), C.Ref[Contact]);
			case EventKind::StickBreak:
			case EventKind::None:
			default:
				break;
			}
			return 0.0;
		}

		// Root of an event function in Theta in (ThetaLo, 1] on the Hermite interpolant of the step (GLo = g(ThetaLo) > 0 >=
		// G1 = g(1)). A linear event function is a cubic in Theta there (values and slopes at both ends): safeguarded
		// Newton on the cubic. The stick margin is evaluated on interpolated states (Illinois).
		double LocateOnHermite(const Model& M, const State& Y0, const double* K0, const State& Y1, const double* K1, double H, const Contacts& C,
			EventKind Kind, int Contact, const EventContext& Context, double ThetaLo, double GLo, double G1)
		{
			if (Kind == EventKind::StickBreak)
			{
				State Mid;
				return Illinois(ThetaLo, 1.0, GLo, G1, 48, 1e-15 * (Abs(GLo) + Abs(G1)), 1e-14,
					[&](double Theta)
					{
						HermiteState(Y0, K0, Y1, K1, H, Theta, Mid);
						return EventValue(M, Kind, Contact, Mid.Y, C, Context);
					});
			}
			// g(Theta) = ((P3 Theta + P2) Theta + P1) Theta + P0 with g(0) = G0, g(1) = G1, g'(0) = H g0', g'(1) = H g1'.
			const double G0 = EventValue(M, Kind, Contact, Y0.Y, C, Context);
			const double S0 = H * EventSlope(M, Kind, Contact, K0, C);
			const double S1 = H * EventSlope(M, Kind, Contact, K1, C);
			const double P3 = 2.0 * G0 + S0 - 2.0 * G1 + S1;
			const double P2 = -3.0 * G0 - 2.0 * S0 + 3.0 * G1 - S1;
			const double P1 = S0;
			const double P0 = G0;
			double Lo = ThetaLo;
			double Hi = 1.0;
			double Theta = Lo + (Hi - Lo) * GLo / (GLo - G1);
			const double Tolerance = 1e-15 * (Abs(GLo) + Abs(G1));
			for (int Iteration = 0; Iteration < 32; ++Iteration)
			{
				const double G = ((P3 * Theta + P2) * Theta + P1) * Theta + P0;
				if (Abs(G) <= Tolerance)
				{
					break;
				}
				if (G > 0.0)
				{
					Lo = Theta;
				}
				else
				{
					Hi = Theta;
				}
				const double Slope = (3.0 * P3 * Theta + 2.0 * P2) * Theta + P1;
				double Next = Slope < 0.0 ? Theta - G / Slope : 0.5 * (Lo + Hi);
				if (!(Next > Lo && Next < Hi))
				{
					Next = 0.5 * (Lo + Hi);
				}
				if (Abs(Next - Theta) <= 1e-15 || Hi - Lo <= 1e-15)
				{
					Theta = Next;
					break;
				}
				Theta = Next;
			}
			return Theta;
		}

		// Lands on an event: one RK4 step of length Theta0 H. The residual of a linear event function is removed by a
		// first-order move along the flow (the last RK4 stage derivative) when it is tiny (the Hermite estimate is
		// 4th-order accurate); otherwise the step length is polished on the RK4 map (Illinois) while |g| > Tolerance.
		// Returns the step length; Out is the state there (on or just past the event). The root is bracketed by
		// (ThetaLo, GLo0 > 0) and (1, G1 <= 0).
		double LandOnEvent(const Model& M, const State& Y0, const double* K0, double H, const Contacts& C, EventKind Kind, int Contact,
			const EventContext& Context, double ThetaLo, double GLo0, double G1, double Theta0, double Tolerance, int MaxIterations, State& Out)
		{
			double K4[kStateSize];
			Rk4(M, Y0, K0, Theta0 * H, C, Out, K4);
			const double G = EventValue(M, Kind, Contact, Out.Y, C, Context);
			if (Abs(G) <= Tolerance)
			{
				return Theta0 * H;
			}
			if (Kind != EventKind::StickBreak)
			{
				const double Slope = EventSlope(M, Kind, Contact, K4, C);
				const double Delta = Slope != 0.0 ? -G / Slope : kInfinity;
				if (Abs(Delta) <= 1e-4 * H && Theta0 * H + Delta >= ThetaLo * H)
				{
					for (int i = 0; i < kStateSize; ++i)
					{
						Out.Y[i] += Delta * K4[i];
					}
					return Theta0 * H + Delta;
				}
			}
			const double Lo = G > 0.0 ? Theta0 : ThetaLo;
			const double Hi = G > 0.0 ? 1.0 : Theta0;
			const double GLo = G > 0.0 ? G : GLo0;
			const double GHi = G > 0.0 ? G1 : G;
			State Mid;
			const double Theta = Illinois(Lo, Hi, GLo, GHi, MaxIterations, Tolerance, 1e-15,
				[&](double T)
				{
					Rk4(M, Y0, K0, T * H, C, Mid);
					return EventValue(M, Kind, Contact, Mid.Y, C, Context);
				});
			Rk4(M, Y0, K0, Theta * H, C, Out);
			return Theta * H;
		}

		// ---- Step control -----------------------------------------------------------------------------------------

		// Friction response of a contact's direction (kappa; the direction relaxes at kappa / |s|).
		inline double DirectionKappa(const Model& M, const Friction& F, int Contact)
		{
			return Contact == 0 ? M.KappaI : M.KappaCPerN * F.NC;
		}

		// Slips at an accepted point and their rates (from K1 = f(Y)).
		struct SlipInfo
		{
			Vec2 S[2];
			double R[2] = {0.0, 0.0};
			Vec2 Rate[2];
		};

		inline void Analyze(const Model& M, const State& Y, const double* K1, SlipInfo& Info)
		{
			for (int Contact = 0; Contact < 2; ++Contact)
			{
				Info.S[Contact] = ContactSlip(M, Y.Y, Contact);
				Info.R[Contact] = Length(Info.S[Contact]);
				Info.Rate[Contact] = ContactSlip(M, K1, Contact);
			}
		}

		// At an accepted point: Slip references follow the physical slip direction; a Young slip grown to
		// |s| >= kYoungGrowFactor kappa h becomes a Slip. Returns true if the vector field changed (K1 must be re-evaluated).
		bool UpdateContacts(const Model& M, const SlipInfo& Info, const Friction& F, double NominalStep, Contacts& C)
		{
			bool Changed = false;
			for (int Contact = 0; Contact < 2; ++Contact)
			{
				const double R = Info.R[Contact];
				if (C.M[Contact] == Mode::Stick || !(R > M.SlipEps))
				{
					continue;
				}
				if (C.M[Contact] == Mode::Young)
				{
					if (R < kYoungGrowFactor * DirectionKappa(M, F, Contact) * NominalStep)
					{
						continue;
					}
					C.M[Contact] = Mode::Slip;
					Changed = true;
				}
				C.Ref[Contact] = Info.S[Contact] / R;
			}
			return Changed;
		}

		// A Slip contact whose slip is (numerically) zero at the step start is decided by the contact law. Returns true if
		// a mode changed.
		bool SettleSlipsAtZero(const Model& M, const State& Y, const SlipInfo& Info, double NominalStep, Contacts& C)
		{
			bool Changed = false;
			for (int Contact = 0; Contact < 2; ++Contact)
			{
				if (C.M[Contact] != Mode::Slip)
				{
					continue;
				}
				// |s| <= max(s_eps, kZeroFraction |s'| h), compared in squares (no square root in the per-step bookkeeping).
				const double R = Info.R[Contact];
				const double Scale = kZeroFraction * NominalStep;
				if (!(R > M.SlipEps) || !(R * R > Scale * Scale * LengthSquared(Info.Rate[Contact])))
				{
					DecideAtZero(M, Y.Y, Contact, C);
					Changed = true;
				}
			}
			return Changed;
		}

		// Largest step for the Slip contacts (F = friction at the step start). A slip heading straight at zero that reaches
		// it within the step runs with its direction frozen at Ref (= its current direction): the crossing is then an event
		// on a smooth path.
		double LimitStep(const Model& M, const SlipInfo& Info, const Friction& F, double H, Contacts& C)
		{
			double Limited = H;
			for (int Contact = 0; Contact < 2; ++Contact)
			{
				C.Frozen[Contact] = false;
				const double R = Info.R[Contact];
				if (C.M[Contact] != Mode::Slip || !(R > M.SlipEps))
				{
					continue;
				}
				// Radial rate rho = (s . s') / |s| and perpendicular rate omega = |s x s'| / |s|, used multiplied by |s|.
				const Vec2 S = Info.S[Contact];
				const Vec2 Rate = Info.Rate[Contact];
				const double RhoR = Dot(S, Rate);
				const double OmegaR = Abs(Cross(S, Rate));
				const double R2 = R * R;
				if (RhoR < 0.0 && OmegaR <= kRadialTolerance * -RhoR && R2 <= -RhoR * H)
				{
					C.Frozen[Contact] = true;
					continue;
				}
				// Turn per step: h (omega - tan rho) <= tan |s| (the turn after h is at most atan(h omega / (|s| + h rho))).
				const double TurnDenominatorR = OmegaR - kTanMaxTurnPerStep * RhoR;
				if (TurnDenominatorR * Limited > kTanMaxTurnPerStep * R2)
				{
					Limited = kTanMaxTurnPerStep * R2 / TurnDenominatorR;
				}
				const double Kappa = DirectionKappa(M, F, Contact);
				if (Kappa * Limited > kStabilityFactor * R)
				{
					Limited = kStabilityFactor * R / Kappa;
				}
			}
			return Limited;
		}

		// Returns false if the loop budget ran out before the end of restitution; the caller then falls back to the plain
		// integrator. Measured in the WP-4 review: a slip that passes CLOSE TO (not through) zero and then creeps just above
		// s_eps (stick marginally infeasible) stays in Slip mode with steps held at kStabilityFactor |s| / kappa, so the
		// budget can run out: ~1 in 1e5 rail hits at the default mu_w, ~3 in 1e4 at mu_w 0.1-0.3 (~0.2 ms each instead
		// of ~2 us; accuracy then that of the plain N = 200 scheme, COL_Mathavan_CreepingSlipNearZeroStaysAccurate).
		bool IntegrateSplit(const Model& M, State& Y, double Restitution, int Steps, int MaxIterations, double& ImpulseOut)
		{
			const double VY0 = Y.Y[kVY];
			const double NominalStep = (1.0 + Restitution) * VY0 / (M.A * static_cast<double>(Steps));
			const int MaxLoop = 32 * Steps + 1024;
			bool Finished = false;

			// Initial modes: slipping contacts take their slip direction; zero slips (a rolling ball at C) are decided by
			// the contact law (jointly if both are zero).
			Contacts C;
			bool AtZero[2] = {false, false};
			for (int Contact = 0; Contact < 2; ++Contact)
			{
				const Vec2 S = ContactSlip(M, Y.Y, Contact);
				const double R = Length(S);
				if (R > M.SlipEps)
				{
					C.Ref[Contact] = S / R;
				}
				else
				{
					AtZero[Contact] = true;
				}
			}
			if (AtZero[0] && AtZero[1])
			{
				DecideJoint(M, C);
			}
			else if (AtZero[0] || AtZero[1])
			{
				DecideAtZero(M, Y.Y, AtZero[0] ? 0 : 1, C);
			}

			bool Compression = true;
			EventContext Context;
			double Impulse = 0.0;
			int ZeroLengthEvents = 0;
			double K1[kStateSize];
			double K1Next[kStateSize];
			Friction F;
			Friction FNext;
			SlipInfo Info;
			State Next;
			State AtEvent;
			Evaluate(M, Y.Y, C, K1, F);
			for (int Loop = 0; Loop < MaxLoop; ++Loop)
			{
				// Contact bookkeeping at the accepted point Y: references, Young slips that have grown, zero slips.
				Analyze(M, Y, K1, Info);
				if (UpdateContacts(M, Info, F, NominalStep, C))
				{
					Evaluate(M, Y.Y, C, K1, F);
					Analyze(M, Y, K1, Info);
				}
				if (SettleSlipsAtZero(M, Y, Info, NominalStep, C))
				{
					Evaluate(M, Y.Y, C, K1, F);
					Analyze(M, Y, K1, Info);
				}
				const double Step = LimitStep(M, Info, F, NominalStep, C);
				Rk4(M, Y, K1, Step, C, Next);
				Evaluate(M, Next.Y, C, K1Next, FNext);

				// Earliest event inside the step (Theta on the Hermite interpolant).
				EventKind BestKind = EventKind::None;
				int BestContact = 0;
				double BestTheta = 2.0;
				double BestThetaLo = 0.0;
				double BestGLo = 0.0;
				double BestG1 = 0.0;
				auto Consider = [&](EventKind Kind, int Contact, double ThetaLo, double GLo)
				{
					// The stick margin at the step end comes from the friction already evaluated there.
					const double G1 = Kind == EventKind::StickBreak ? StickMargin(M, FNext, Contact) : EventValue(M, Kind, Contact, Next.Y, C, Context);
					if (!(G1 <= 0.0))
					{
						return;
					}
					const double Theta = GLo > 0.0 ? LocateOnHermite(M, Y, K1, Next, K1Next, Step, C, Kind, Contact, Context, ThetaLo, GLo, G1) : ThetaLo;
					if (Theta < BestTheta)
					{
						BestKind = Kind;
						BestContact = Contact;
						BestTheta = Theta;
						BestThetaLo = ThetaLo;
						BestGLo = GLo;
						BestG1 = G1;
					}
				};
				auto ConsiderFromStart = [&](EventKind Kind, int Contact)
				{
					Consider(Kind, Contact, 0.0, EventValue(M, Kind, Contact, Y.Y, C, Context));
				};
				if (ZeroLengthEvents < 8)
				{
					for (int Contact = 0; Contact < 2; ++Contact)
					{
						if (C.M[Contact] == Mode::Slip)
						{
							if (Info.R[Contact] > M.SlipEps)
							{
								ConsiderFromStart(EventKind::SlipZero, Contact);
							}
						}
						else if (C.M[Contact] == Mode::Stick)
						{
							Consider(EventKind::StickBreak, Contact, 0.0, StickMargin(M, F, Contact));
						}
					}
				}
				// Phase ends. The vector field does not change at the end of compression, so the step is not cut there:
				// W_c is the maximum of W (dW/dP = v_Y cos = 0), read off the Hermite interpolant (insensitive to the located
				// Theta to first order), and the restitution target (1 - e^2) W_c applies from there on, inside the same step.
				if (!Compression)
				{
					ConsiderFromStart(EventKind::RestitutionEnd, 0);
				}
				else if (!(Restitution > 0.0))
				{
					ConsiderFromStart(EventKind::CompressionEnd, 0);
				}
				else if (Next.Y[kVY] <= 0.0)
				{
					const double VYStart = Y.Y[kVY];
					const double ThetaC =
						VYStart > 0.0 ? LocateOnHermite(M, Y, K1, Next, K1Next, Step, C, EventKind::CompressionEnd, 0, Context, 0.0, VYStart, Next.Y[kVY]) : 0.0;
					if (ThetaC < BestTheta)
					{
						State AtCompressionEnd;
						HermiteState(Y, K1, Next, K1Next, Step, ThetaC, AtCompressionEnd);
						Compression = false;
						Context.WorkTarget = AtCompressionEnd.Y[kWork] * (1.0 - Restitution * Restitution);
						Consider(EventKind::RestitutionEnd, 0, ThetaC, AtCompressionEnd.Y[kWork] - Context.WorkTarget);
					}
				}

				if (BestKind == EventKind::None)
				{
					Impulse += Step;
					Y = Next;
					for (int i = 0; i < kStateSize; ++i)
					{
						K1[i] = K1Next[i];
					}
					F = FNext;
					if (C.Frozen[0] || C.Frozen[1])
					{
						C.Frozen[0] = false;
						C.Frozen[1] = false;
						Evaluate(M, Y.Y, C, K1, F);
					}
					ZeroLengthEvents = 0;
					continue;
				}

				// Land on the event (RK4 step of the located length; the residual of a linear event is removed along the
				// flow). A stick break needs no polish: the friction is continuous there (only its slope changes), and a
				// Young contact keeps sticking while that is still feasible.
				double Tolerance = kInfinity;
				if (BestKind == EventKind::CompressionEnd)
				{
					Tolerance = 1e-14 * VY0;
				}
				else if (BestKind == EventKind::RestitutionEnd)
				{
					Tolerance = 1e-14 * Abs(Context.WorkTarget) + 1e-300;
				}
				else if (BestKind == EventKind::SlipZero)
				{
					Tolerance = Max(M.SlipEps, kZeroFraction * Length(Info.Rate[BestContact]) * NominalStep);
				}
				double Landed = 0.0;
				if (BestGLo > 0.0)
				{
					Landed = LandOnEvent(M, Y, K1, Step, C, BestKind, BestContact, Context, BestThetaLo, BestGLo, BestG1, BestTheta, Tolerance, MaxIterations,
						AtEvent);
				}
				else
				{
					AtEvent = Y;
				}
				Impulse += Landed;
				Y = AtEvent;
				ZeroLengthEvents = Landed <= 1e-12 * NominalStep ? ZeroLengthEvents + 1 : 0;

				if (BestKind == EventKind::CompressionEnd)
				{
					Y.Y[kVY] = 0.0;
					Context.WorkTarget = Y.Y[kWork] * (1.0 - Restitution * Restitution);
					Compression = false;
					if (!(Restitution > 0.0))
					{
						Finished = true;
						break;
					}
				}
				else if (BestKind == EventKind::RestitutionEnd)
				{
					Finished = true;
					break;
				}
				else if (BestKind == EventKind::SlipZero)
				{
					const double Threshold = Max(M.SlipEps, kLandedZeroFraction * Length(Info.Rate[BestContact]) * NominalStep);
					if (!(Length(ContactSlip(M, Y.Y, BestContact)) > Threshold))
					{
						C.Frozen[BestContact] = false;
						DecideAtZero(M, Y.Y, BestContact, C);
					}
					// else: the slip direction turned past 90 degrees without reaching zero: the step ends there and the
					// reference is re-anchored at the next step start.
				}
				else
				{
					// Stick break: the contact leaves zero along the quasi-static direction (continuous with the stick
					// friction on the cone).
					C.M[BestContact] = Mode::Young;
				}
				C.Frozen[0] = false;
				C.Frozen[1] = false;
				Evaluate(M, Y.Y, C, K1, F);
			}
			ImpulseOut = Impulse;
			return Finished;
		}
	}

	CushionImpactResult ResolveMathavan(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& Spec, const MathavanSettings& Settings)
	{
		CushionImpactResult Result;
		Result.Velocity = {VelocityLocal.x, VelocityLocal.y, 0.0};
		Result.Omega = OmegaLocal;
		const double NormalSpeed = VelocityLocal.y;
		const bool FiniteInput = IsFinite(VelocityLocal.x) && IsFinite(VelocityLocal.y) && IsFinite(OmegaLocal.x) && IsFinite(OmegaLocal.y) &&
			IsFinite(OmegaLocal.z);
		if (!(NormalSpeed > 0.0) || !FiniteInput)
		{
			// Precondition violated (separating, or a corrupt state): no impulse.
			Result.Resting = true;
			return Result;
		}
		Result.NormalSpeed = NormalSpeed;
		if (NormalSpeed < Settings.RestSpeed)
		{
			// Resting / pressing contact (4.5, 7.3): v_Y := 0, nothing else changes (the normal impulse that stops
			// v_Y, friction neglected: P cos(theta) = m v_Y).
			Result.Velocity.y = 0.0;
			Result.NormalImpulse = Spec.Mass * NormalSpeed / Cos(Settings.Elevation);
			Result.Resting = true;
			return Result;
		}

		const Model M = MakeModel(Spec, Settings);
		const double Restitution = Clamp(Settings.Restitution, 0.0, 1.0);
		const int Steps = Settings.Steps < 1 ? 1 : (Settings.Steps > kMaxMathavanSteps ? kMaxMathavanSteps : Settings.Steps);
		const int MaxBisections = Settings.MaxBisections < 1 ? 1 : Settings.MaxBisections;

		State Y;
		Y.Y[kVX] = VelocityLocal.x;
		Y.Y[kVY] = VelocityLocal.y;
		Y.Y[kWX] = OmegaLocal.x;
		Y.Y[kWY] = OmegaLocal.y;
		Y.Y[kWZ] = OmegaLocal.z;
		Y.Y[kWork] = 0.0;

		double Impulse = 0.0;
		bool Done = false;
		if (Settings.SplitAtSlipReversal)
		{
			State Split = Y;
			Done = IntegrateSplit(M, Split, Restitution, Steps, MaxBisections, Impulse);
			if (Done)
			{
				Y = Split;
			}
		}
		if (!Done)
		{
			// The spec's algorithm (also the safety net of the split integrator: at least the spec's N = 200).
			IntegratePlain(M, Y, Restitution, Settings.SplitAtSlipReversal ? (Steps > 200 ? Steps : 200) : Steps, MaxBisections, Impulse);
		}

		Result.Velocity = {Y.Y[kVX], Y.Y[kVY], 0.0};
		Result.Omega = {Y.Y[kWX], Y.Y[kWY], Y.Y[kWZ]};
		Result.NormalImpulse = Impulse;
		Result.Restitution = Restitution;
		return Result;
	}
}
