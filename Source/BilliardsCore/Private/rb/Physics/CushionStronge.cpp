#include "rb/Core/FpGuard.h"
// Owner: WP-4 (cushion, facing & pocket-edge resolution). Spec: prior-art 2.8 / XREF-02 (pooltool's default cushion model).
//
// Stronge's compliant frictional impact with tangential compliance (W. J. Stronge, Impact Mechanics, 2nd ed. 2018,
// "tangential compliance in planar impact of rough bodies"), as used by pooltool 0.6.0 for its default cushion
// (stronge_compliant, omega_ratio). This is an independent implementation of the published model from its
// equations (derivation below); pooltool served only as a numerical oracle (tests in Tests/Core/Cushion).
//
// Contact element: a linear normal spring (loading frequency w_n, unloading w_n / e, so the separation speed is
// e times the approach speed U) in series with a tangential spring (frequency w_t = OmegaRatio w_n) and a
// Coulomb slider. Sphere against a half space: normal inverse mass 1/m, tangential 1/m + R^2/I = (1 + k)/(k m)
// (beta' = (1 + k)/k = 3.5 for a solid ball), stiffness ratio eta^2 = beta' / OmegaRatio^2. Non-dimensional
// phase theta = w_n t: compression theta in [0, pi/2], restitution phase phi = theta/e + (pi/2)(1 - 1/e),
// end at theta_f = (1 + e) pi/2. With slip speed s along the initial slip direction, u the tangential spring
// deflection scaled by w_n/U, S(theta) the normal force shape (sin theta, then sin phi) and p(theta) the normal
// impulse per unit m U (1 - cos theta, then 1 - e cos phi):
//   slipping (initially):  s/U = r - mu beta' p(theta),   u = mu eta^2 S(theta)      (r = s0 / U)
//   sticking:              d(s/U)/dtheta = -OmegaRatio^2 u,   du/dtheta = s/U  (harmonic)
//   slip -> stick when the slider velocity vanishes: s/U = mu eta^2 dS/dtheta;
//   stick -> slip when |u| reaches the friction limit mu eta^2 S(theta).
// Regimes: initial stick (r < mu eta^2), gross slip (r > mu max(beta', (1 + e) beta' - eta^2 / e): the slider
// velocity stays positive over the whole impact; the beta' term matters only for e < 1 / OmegaRatio, where pooltool
// 0.6.0 misclassifies and gains energy - see StrongeFinalSlipRatio), otherwise slip - stick - slip. Checked against a
// direct time integration of the spring-slider contact (Tests/Core/Cushion/TestCushionAdversarial.cpp).
// The final slip after a stick phase acts along the spring force: this
// implementation takes its sign from the deflection at the release (pooltool 0.6.0 always uses the reversed
// direction; the two agreed to 7e-15 on a 300-case random oracle sweep with e in [0.3, 1], mu in [0.05, 0.4]
// and OmegaRatio in [1.2, 1.95]; after the review's regime fix, 3000 such cases agree to 1e-15 wherever
// e >= 1 / OmegaRatio and differ only below it, where the direct integration sides with this implementation).
// The normal impulse is (1 + e) m U. The velocities follow from the impulses
// (pitfall 1: normal k_hat from the contact to the center, r_I = -R k_hat).
#include "rb/Physics/Cushion.h"

#include "rb/Core/Assert.h"
#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		struct StrongeParams
		{
			double Mu = 0.0;
			double E = 1.0;          // restitution e (> 0)
			double BetaRatio = 3.5;  // beta' = beta_t / beta_n
			double EtaSquared = 1.0; // eta^2 = beta' / OmegaRatio^2
			double Rho = 1.8;        // OmegaRatio = w_t / w_n
		};

		constexpr double kHalfPi = 0.5 * kPi;

		// Restitution phase angle phi(theta) (theta >= pi/2).
		inline double RestitutionPhase(double Theta, double E) { return Theta / E + kHalfPi * (1.0 - 1.0 / E); }

		// Normal force shape S(theta) and normal impulse p(theta) per unit m U.
		inline double ForceShape(double Theta, double E) { return Theta <= kHalfPi ? Sin(Theta) : Sin(RestitutionPhase(Theta, E)); }
		inline double NormalImpulse(double Theta, double E)
		{
			return Theta <= kHalfPi ? 1.0 - Cos(Theta) : 1.0 - E * Cos(RestitutionPhase(Theta, E));
		}

		struct StickSolution
		{
			double Theta0 = 0.0; // start of the stick phase
			double S0 = 0.0;     // s/U at the start
			double U0 = 0.0;     // spring deflection at the start
		};

		inline double StickSlip(const StickSolution& St, double Rho, double Theta)
		{
			const double X = Rho * (Theta - St.Theta0);
			return St.S0 * Cos(X) - Rho * St.U0 * Sin(X);
		}

		inline double StickDeflection(const StickSolution& St, double Rho, double Theta)
		{
			const double X = Rho * (Theta - St.Theta0);
			return St.U0 * Cos(X) + (St.S0 / Rho) * Sin(X);
		}

		// |u| - mu eta^2 S: negative while the stick holds.
		inline double StickExcess(const StickSolution& St, const StrongeParams& P, double Theta)
		{
			return Abs(StickDeflection(St, P.Rho, Theta)) - P.Mu * P.EtaSquared * ForceShape(Theta, P.E);
		}

		// First release of the stick in (From, To]: sampled on 64 sub-intervals, refined by bisection. Returns To if
		// the stick holds until the end of the impact.
		double StickReleaseTheta(const StickSolution& St, const StrongeParams& P, double From, double To)
		{
			constexpr int kSamples = 64;
			if (!(To > From))
			{
				return To;
			}
			double Previous = From;
			for (int i = 1; i <= kSamples; ++i)
			{
				const double Theta = From + (To - From) * (static_cast<double>(i) / kSamples);
				if (StickExcess(St, P, Theta) > 0.0)
				{
					double Lo = Previous;
					double Hi = Theta;
					for (int k = 0; k < 80; ++k)
					{
						const double Mid = 0.5 * (Lo + Hi);
						if (StickExcess(St, P, Mid) > 0.0)
						{
							Hi = Mid;
						}
						else
						{
							Lo = Mid;
						}
					}
					return Hi;
				}
				Previous = Theta;
			}
			return To;
		}

		// Final slip after a stick phase: from the release at ThetaRelease to the end ThetaEnd, the friction acts
		// along the spring force (sign of the deflection) with magnitude mu F_n.
		double FinishAfterStick(const StickSolution& St, const StrongeParams& P, double ThetaRelease, double ThetaEnd)
		{
			const double SRelease = StickSlip(St, P.Rho, ThetaRelease);
			if (!(ThetaRelease < ThetaEnd))
			{
				return SRelease;
			}
			const double Deflection = StickDeflection(St, P.Rho, ThetaRelease);
			const double Direction = Deflection < 0.0 ? 1.0 : -1.0; // force on the ball along +slip if u < 0
			return SRelease + Direction * P.Mu * P.BetaRatio * (NormalImpulse(ThetaEnd, P.E) - NormalImpulse(ThetaRelease, P.E));
		}

		// Final slip speed (along the initial slip direction) per unit approach speed, for r = s0 / U >= 0.
		double StrongeFinalSlipRatio(double R, const StrongeParams& P, bool& StickOut)
		{
			const double ThetaEnd = (1.0 + P.E) * kHalfPi;
			const double Mu = P.Mu;
			const double Beta = P.BetaRatio;
			const double Eta2 = P.EtaSquared;
			StickOut = true;

			// Regimes by the slider velocity g(theta) = r - mu beta' p(theta) - mu eta^2 S'(theta) (header). g(0) = r - mu eta^2:
			// initial stick below. dg/dtheta = mu S (eta^2 - beta') < 0 in compression and mu S (eta^2 / e^2 - beta') in
			// restitution, so min g is g(theta_end) = r - mu ((1 + e) beta' - eta^2 / e) for e >= 1 / OmegaRatio and
			// g(pi/2) = r - mu beta' for e < 1 / OmegaRatio (g rises again during restitution). Gross slip needs min g > 0.
			// REVIEW FIX (WP-4 review): pooltool 0.6.0 tests only g(theta_end), and before the initial-stick test. For
			// e < 1 / OmegaRatio that threshold drops below mu beta' (and below mu eta^2 for e < 1 / OmegaRatio^2), so it
			// took slip-stick-slip and initial-stick impacts for gross slip, applied friction against a reversed slider and
			// fed energy in (up to 5e-2 J at OmegaRatio 1.07, e = 0.6). For e >= 1 / OmegaRatio (pooltool's defaults e_c =
			// 0.85, omega_ratio 1.8: XREF-02) both give bitwise the same result.
			if (R < Mu * Eta2)
			{
				// Initial stick (the spring starts unloaded); it holds through compression (sin(Rho x) <= Rho sin(x)).
				StickSolution St;
				St.Theta0 = 0.0;
				St.S0 = R;
				St.U0 = 0.0;
				const double Release = StickReleaseTheta(St, P, kHalfPi, ThetaEnd);
				return FinishAfterStick(St, P, Release, ThetaEnd);
			}
			if (R > Mu * Max(Beta, (1.0 + P.E) * Beta - Eta2 / P.E))
			{
				// Gross slip for the whole impact.
				StickOut = false;
				return R - Mu * Beta * (1.0 + P.E);
			}

			// Slip, then stick where the slider velocity vanishes, then slip again.
			double ThetaStick = 0.0;
			const double X = R / Mu - Beta;
			if (R <= Mu * Beta)
			{
				// During compression: cos(theta) = (r/mu - beta') / (eta^2 - beta') (always here for e < 1 / OmegaRatio).
				const double C = Clamp(X / (Eta2 - Beta), -1.0, 1.0);
				ThetaStick = Acos(C);
			}
			else
			{
				// During restitution: cos(phi) = (r/mu - beta') / (eta^2 / e - e beta').
				const double C = Clamp(X / (Eta2 / P.E - P.E * Beta), -1.0, 1.0);
				const double Phi = Acos(C);
				ThetaStick = P.E * (Phi - kHalfPi * (1.0 - 1.0 / P.E));
			}
			ThetaStick = Clamp(ThetaStick, 0.0, ThetaEnd);
			StickSolution St;
			St.Theta0 = ThetaStick;
			St.S0 = R - Mu * Beta * NormalImpulse(ThetaStick, P.E);
			St.U0 = Mu * Eta2 * ForceShape(ThetaStick, P.E);
			const double From = Max(ThetaStick, kHalfPi);
			const double Release = StickReleaseTheta(St, P, From, ThetaEnd);
			return FinishAfterStick(St, P, Release, ThetaEnd);
		}
	}

	CushionImpactResult ResolveStronge(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& Spec, double Elevation, double Restitution,
		double Friction, double OmegaRatio)
	{
		// The compliant model needs e > 0 (the unloading spring is w_n / e); an (almost) inelastic contact, e.g. the
		// resting rule of the dispatcher, falls back to the rigid impulse (Han / GRI) with the same e and mu.
		if (Restitution < 1e-3)
		{
			return ResolveHan(VelocityLocal, OmegaLocal, Spec, Elevation, Restitution, Friction);
		}
		// The model needs 1 < omega_t / omega_n < 2 (pooltool asserts it); a parameter outside (e.g. a console override of
		// cushion.stronge_omega_ratio) is clamped into the range instead of stopping a debug build.
		const double Ratio = Clamp(OmegaRatio, 1.0 + 1e-6, 2.0 - 1e-6);

		CushionImpactResult Result;
		Result.Velocity = VelocityLocal;
		Result.Omega = OmegaLocal;
		Result.Restitution = Restitution;

		const Vec3 Normal{0.0, -Cos(Elevation), -Sin(Elevation)}; // k_hat: contact point -> center (local frame)
		const Vec3 ContactArm = Normal * (-Spec.Radius);
		const Vec3 ContactVelocity = VelocityLocal + Cross(OmegaLocal, ContactArm);
		const double Approach = -Dot(ContactVelocity, Normal); // U > 0 when approaching
		if (!(Approach > 0.0))
		{
			Result.Restitution = 0.0;
			Result.Resting = true;
			return Result;
		}

		const double K = InertiaFactor(Spec);
		StrongeParams P;
		P.Mu = Friction;
		P.E = Restitution;
		P.BetaRatio = (1.0 + K) / K;
		P.Rho = Ratio;
		P.EtaSquared = P.BetaRatio / (Ratio * Ratio);

		const Vec3 Tangential = ContactVelocity + Normal * Approach; // component of the contact velocity normal to k_hat
		const double SlipSpeed = Length(Tangential);
		double NewSlipSpeed = SlipSpeed;
		if (SlipSpeed > 0.0 && Friction > 0.0)
		{
			bool Stick = false;
			NewSlipSpeed = Approach * StrongeFinalSlipRatio(SlipSpeed / Approach, P, Stick);
			Result.Stick = Stick;
		}

		// Impulses: normal (1 + e) m U along k_hat; tangential m (s_f - s_0) / beta' along the initial slip direction.
		const double Mass = Spec.Mass;
		const double NormalImpulseMagnitude = (1.0 + Restitution) * Mass * Approach;
		Vec3 Impulse = Normal * NormalImpulseMagnitude;
		if (SlipSpeed > 0.0)
		{
			Impulse += Tangential * (Mass * (NewSlipSpeed - SlipSpeed) / (P.BetaRatio * SlipSpeed));
		}
		Result.Velocity = VelocityLocal + Impulse / Mass;
		Result.Omega = OmegaLocal + Cross(ContactArm, Impulse) / Spec.Inertia;
		Result.NormalSpeed = Approach;
		Result.NormalImpulse = NormalImpulseMagnitude;
		return Result;
	}
}
