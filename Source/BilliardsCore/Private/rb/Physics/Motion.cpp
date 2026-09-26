#include "rb/Core/FpGuard.h"
// Owner: WP-1 (motion, slate & cue strike). Spec: physics-motion-and-cue Part A, C.1; human-factors 4.5 (tilted table).
#include "rb/Physics/Motion.h"

#include "rb/Physics/Slate.h"

namespace rb
{
	namespace
	{
		// ---------------------------------------------------------------------------------------------
		// Level closed forms (A.3-A.7, C.1)
		// ---------------------------------------------------------------------------------------------

		// Linear w_z decay on a support (A.7): rate -sgn(w_z0) alpha_sp until w_z reaches 0 at OmegaZStopTau.
		// w_z0 = 0 -> stop at tau = 0 (w_z stays 0); alpha_sp <= 0 (no spin friction) -> w_z constant.
		void SetSpinDecay(MotionSegment& Seg, double OmegaZ, double AlphaSp)
		{
			if (OmegaZ == 0.0)
			{
				Seg.OmegaZRate = 0.0;
				Seg.OmegaZStopTau = 0.0;
			}
			else if (AlphaSp > 0.0)
			{
				Seg.OmegaZRate = OmegaZ > 0.0 ? -AlphaSp : AlphaSp;
				Seg.OmegaZStopTau = Abs(OmegaZ) / AlphaSp;
			}
			else
			{
				Seg.OmegaZRate = 0.0;
				Seg.OmegaZStopTau = kInfinity;
			}
		}

		// Horizontal slip of the support contact point, u_h = v_h + R (z_hat x w)_h (0.3).
		Vec2 HorizontalSlip(const Vec3& Velocity, const Vec3& Omega, double Radius)
		{
			return {Velocity.x - Radius * Omega.y, Velocity.y + Radius * Omega.x};
		}

		// (1/R) z_hat x h for a horizontal vector h (rolling spin of the velocity h).
		Vec3 ZCrossOverR(const Vec2& H, double Radius) { return {-H.y / Radius, H.x / Radius, 0.0}; }

		// ---------------------------------------------------------------------------------------------
		// Pursuit problem dx/dt = G - K x_hat, K > |G| (human-factors 4.5.2)
		// ---------------------------------------------------------------------------------------------

		// Decomposition of x0 against G: x0 = |x0| (cos(beta0) G_hat + S0 EPerp), A = (1 + cos beta0) / 2,
		// B = (1 - cos beta0) / 2. A and B are formed without cancellation (A = s^2 / (2 (1 - c)) for c < 0,
		// B = s^2 / (2 (1 + c)) for c >= 0), so the nearly anti-parallel (uphill) and parallel cases keep full
		// relative precision; S0 == 0 exactly gives (A, B) = (1, 0) or (0, 1) exactly.
		struct PursuitSetup
		{
			double Speed = 0.0; // |x0| [m/s]
			double GNorm = 0.0; // |G| [m/s^2]
			Vec2 XHat;          // x0 / |x0|
			Vec2 GHat;
			Vec2 EPerp;         // unit vector perpendicular to G_hat on the side of x0
			double S0 = 0.0;    // sin(beta0) >= 0
			double A = 1.0;
			double B = 0.0;
		};

		// Length and direction of a 2-D vector without underflow / overflow of the squares: an exact power-of-two rescaling
		// (2^-500 .. 2^500) is applied first when needed, so for ordinary magnitudes this is exactly Length(V) and
		// V / Length(V). Returns 0 (and Unit = 0) for a zero or non-finite vector.
		double RobustLength(const Vec2& V, Vec2& Unit)
		{
			constexpr double kUp = 0x1p+500;
			constexpr double kDown = 0x1p-500;
			Unit = Vec2{};
			double MaxAbs = Max(Abs(V.x), Abs(V.y));
			if (!(MaxAbs > 0.0) || !(MaxAbs < kInfinity))
			{
				return 0.0;
			}
			Vec2 S = V;
			double Back = 1.0; // exact power of two: |V| = |S| * Back
			while (MaxAbs < kDown)
			{
				S = S * kUp;
				MaxAbs *= kUp;
				Back *= kDown;
			}
			while (MaxAbs > kUp)
			{
				S = S * kDown;
				MaxAbs *= kDown;
				Back *= kUp;
			}
			const double Len = Length(S);
			Unit = S / Len;
			return Len * Back;
		}

		PursuitSetup MakePursuitSetup(const Vec2& X0, const Vec2& G)
		{
			PursuitSetup P;
			P.Speed = RobustLength(X0, P.XHat);
			P.GNorm = RobustLength(G, P.GHat);
			if (P.Speed > 0.0 && P.GNorm > 0.0)
			{
				// Angle between x0 and G from the unit vectors (no underflow of |x0| |G| for tiny drives, review fix).
				const double DotXG = Dot(P.XHat, P.GHat);
				const double CrossXG = Cross(P.XHat, P.GHat);
				const double Norm = Sqrt(DotXG * DotXG + CrossXG * CrossXG);
				const double C0 = DotXG / Norm;
				P.S0 = Abs(CrossXG) / Norm;
				if (C0 >= 0.0)
				{
					P.B = P.S0 * P.S0 / (2.0 * (1.0 + C0));
					P.A = 1.0 - P.B;
				}
				else
				{
					P.A = P.S0 * P.S0 / (2.0 * (1.0 - C0));
					P.B = 1.0 - P.A;
				}
				const Vec2 Perp = PerpCcw(P.GHat);
				P.EPerp = CrossXG <= 0.0 ? Perp : -Perp; // Cross(G, x0) >= 0: x0 lies counter-clockwise of G
			}
			return P;
		}

		// A drive below 2^-60 K changes no result by more than 1e-18 relative (|dx| <= |G| T_stop, T_stop ~ |x0| / K), but its
		// p = K / |G| overflows the exponents of 4.5.2 for subnormal |G|: such a drive (and G = 0, or NaN) is the level law.
		bool IsNegligibleDrive(const PursuitSetup& P, double K) { return !(P.GNorm > K * 0x1p-60); }

		// Level (G = 0 or negligible) or exactly collinear (S0 = 0) problems move with a constant acceleration until the stop.
		bool IsQuadraticPursuit(const PursuitSetup& P, double K) { return IsNegligibleDrive(P, K) || P.S0 == 0.0; }

		// Deceleration along x_hat0 of a quadratic pursuit: K (level), K - |G| (downhill), K + |G| (uphill).
		double QuadraticDeceleration(const PursuitSetup& P, double K)
		{
			if (IsNegligibleDrive(P, K))
			{
				return K;
			}
			return P.A == 1.0 ? K - P.GNorm : K + P.GNorm;
		}

		// T_stop, shared by PursuitStopTime and EvaluatePursuit so that a chain piece ending at T_stop is evaluated
		// exactly at the stop.
		double StopTimeOf(const PursuitSetup& P, double K)
		{
			if (!(P.Speed > 0.0))
			{
				return 0.0;
			}
			if (IsQuadraticPursuit(P, K))
			{
				const double Decel = QuadraticDeceleration(P, K);
				return Decel > 0.0 ? P.Speed / Decel : kInfinity;
			}
			if (!(K > P.GNorm))
			{
				return kInfinity;
			}
			return P.Speed * P.A / (K - P.GNorm) + P.Speed * P.B / (K + P.GNorm);
		}

		// Solves t(lam) = Tau for 0 < Tau < T_stop (general case, 0 < S0). With T - t(lam) = a e^{-m lam} + b e^{-n lam}
		// (m = p - 1, n = p + 1, a + b = T_stop) the equation is
		//     F(lam) = m lam - ln(alpha + beta e^{-2 lam}) - Lam = 0,   alpha = a / T, beta = b / T, Lam = -log1p(-Tau / T),
		// with F increasing and concave: Newton from lam0 = Lam / F'(0) (left of the root) converges monotonically; the
		// bracket [Lam / n, Lam / m] safeguards it. Stop: |dlam| <= 1e-15 max(1, lam) (human-factors 4.5.2).
		double SolvePursuitLambda(double Alpha, double Beta, double M, double N, double BigLam)
		{
			double Lo = BigLam / N;
			double Hi = BigLam / M;
			double Lam = BigLam / (M + 2.0 * Beta);
			for (int Iteration = 0; Iteration < 100; ++Iteration)
			{
				// e^{-2 lam} and E_2 = 1 - e^{-2 lam}, each without cancellation.
				double Q = 0.0;
				double E2 = 0.0;
				if (Lam < 0.35)
				{
					const double Em1 = Expm1(-2.0 * Lam);
					E2 = -Em1;
					Q = 1.0 + Em1;
				}
				else
				{
					Q = Exp(-2.0 * Lam);
					E2 = 1.0 - Q;
				}
				const double BetaE2 = Beta * E2;
				const double Den = Alpha + Beta * Q; // 1 - beta E_2 as a sum of non-negative terms
				const double LogDen = BetaE2 < 0.5 ? Log1p(-BetaE2) : Log(Den);
				const double F = M * Lam - LogDen - BigLam;
				if (F == 0.0)
				{
					break;
				}
				if (F < 0.0)
				{
					Lo = Lam;
				}
				else
				{
					Hi = Lam;
				}
				const double Fp = M + 2.0 * Beta * Q / Den;
				const double Step = F / Fp;
				const double Next = Lam - Step;
				if (Abs(Step) <= 1e-15 * Max(1.0, Lam))
				{
					Lam = Next;
					break;
				}
				Lam = (Next > Lo && Next < Hi) ? Next : 0.5 * (Lo + Hi);
			}
			return Lam;
		}

		// ---------------------------------------------------------------------------------------------
		// Tilted-table chain pieces (human-factors 4.5.3)
		// ---------------------------------------------------------------------------------------------

		// Pursuit data of a Sliding / Rolling state (4.5.2 table, per-ball k): Sliding x = u, G = g_t,
		// K = mu_s g (1 + k)/k, c_s = k/(1 + k); Rolling x = v_h, G = g_t/(1 + k) (+ g NapPseudoSlope on the cloth),
		// K = mu_r g (1 - eta_n v_hat0 . n_nap), c_s = 1.
		struct PursuitData
		{
			Vec2 X0;
			Vec2 G;
			double K = 0.0;
			double Cs = 1.0;
		};

		PursuitData MakePursuitData(const BallState& S, double InertiaK, const ClothParams& Surface, double SupportZ, double Gravity,
			const TiltParams& Tilt, double Radius)
		{
			PursuitData D;
			const Vec2 Gt = InPlaneGravity(Tilt, Gravity);
			if (S.State == MotionState::Sliding)
			{
				D.X0 = HorizontalSlip(S.Velocity, S.Omega, Radius);
				D.G = Gt;
				D.K = Surface.SlidingFriction * Gravity * (1.0 + InertiaK) / InertiaK;
				D.Cs = InertiaK / (1.0 + InertiaK);
				return D;
			}
			D.X0 = XY(S.Velocity);
			D.G = Gt / (1.0 + InertiaK);
			D.K = Surface.RollingResistance * Gravity;
			D.Cs = 1.0;
			const bool NapActive = SupportZ == 0.0 && (Tilt.NapPseudoSlope.x != 0.0 || Tilt.NapPseudoSlope.y != 0.0);
			if (NapActive)
			{
				D.G += Tilt.NapPseudoSlope * Gravity;
				if (Tilt.NapResistance != 0.0)
				{
					const Vec2 NapDir = Normalized(Tilt.NapPseudoSlope, 0.0);
					const Vec2 VHat = Normalized(D.X0, 0.0);
					D.K *= 1.0 - Tilt.NapResistance * Dot(VHat, NapDir); // frozen per piece (4.5.6)
				}
			}
			return D;
		}

		// Largest turn of the rolling direction per chain piece while nap resistance is frozen per piece (human-factors 4.5.6:
		// freezing error <= eta_n K |dbeta| Delta^2 / 2) [rad].
		constexpr double kNapMaxTurn = 0.05;

		// Time until the direction of x has turned by Turn [rad] toward G (4.5.2: tan(beta / 2) = tan(beta0 / 2) e^{-lam},
		// then t(lam)); +inf if x never turns that far (beta0 <= Turn, collinear or level motion).
		double TurnDuration(const PursuitSetup& P, double K, double Turn)
		{
			if (IsQuadraticPursuit(P, K) || !(K > P.GNorm) || !(P.A > 0.0))
			{
				return kInfinity;
			}
			const double W0 = Sqrt(P.B / P.A); // tan(beta0 / 2): A = cos^2(beta0 / 2), B = sin^2(beta0 / 2)
			const double Beta0 = 2.0 * Atan(W0);
			if (!(Beta0 > Turn))
			{
				return kInfinity;
			}
			const double Lam = Log(W0 / Tan(0.5 * (Beta0 - Turn)));
			const double Km = K - P.GNorm;
			const double Kp = K + P.GNorm;
			return P.Speed * P.A * -Expm1(-(Km / P.GNorm) * Lam) / Km + P.Speed * P.B * -Expm1(-(Kp / P.GNorm) * Lam) / Kp;
		}

		// True when the 4.5.3 refresh rule runs this piece to the exact stop by construction (collinear or tail piece, or
		// invalid settings): TiltPieceDuration then returns Remaining without the cubic root.
		bool PieceRunsToStop(double SpeedX, double K, double GNorm, double Cs, double SinBound, double Tolerance, double MaxInterval)
		{
			if (!(Tolerance > 0.0) || !(MaxInterval > 0.0))
			{
				return true;
			}
			return SinBound <= 1e-12 || SpeedX <= Sqrt(Tolerance * (K - GNorm) / (4.0 * Cs));
		}

		// One chain piece of a Sliding / Rolling phase on a tilted table (4.5.3 MakeTiltSegment). Returns false (and
		// leaves Seg untouched) when the level closed form applies: G = 0 for this state (e.g. sliding on a nap-only
		// table), the ball has no pursuit speed, the drive is negligible (|G| <= 2^-60 K) or the tilt violates K > |G|
		// (rejected by ValidatePhysicsParams).
		bool MakeTiltPiece(MotionSegment& Seg, const BallState& S, double InertiaK, const ClothParams& Surface, double SupportZ, double Gravity,
			const TiltParams& Tilt)
		{
			const PursuitData D = MakePursuitData(S, InertiaK, Surface, SupportZ, Gravity, Tilt, Seg.Radius);
			if (D.G.x == 0.0 && D.G.y == 0.0)
			{
				return false;
			}
			const PursuitSetup P = MakePursuitSetup(D.X0, D.G);
			if (!(P.Speed > 0.0) || !(D.K > P.GNorm) || IsNegligibleDrive(P, D.K))
			{
				return false;
			}

			const double Remaining = StopTimeOf(P, D.K);
			// sigma_i: bound on sin(beta) over the piece (beta decreases toward 0; beta > 90 deg can pass through 90 deg).
			// cos(beta0) >= 0 <=> A >= B. Exactly collinear motion (S0 = 0, downhill or uphill) never turns: its phase is
			// one exact quadratic (4.5.3 "collinear: one exact segment"), so the uphill case does not take the bound 1.
			const double SinBound = (P.A >= P.B || P.S0 == 0.0) ? P.S0 : 1.0;
			double Delta = TiltPieceDuration(P.Speed, D.K, P.GNorm, D.Cs, SinBound, Tilt.Tolerance, Tilt.RefreshMaxInterval, Remaining);
			// Nap resistance (4.5.6, rolling on the cloth only): K is frozen with v_hat_i, so the refresh rule also keeps the turn
			// of the direction per piece within kNapMaxTurn (the tail and collinear pieces run to the stop as before).
			const bool NapTurnRule = S.State == MotionState::Rolling && SupportZ == 0.0 && Tilt.NapResistance != 0.0 &&
				(Tilt.NapPseudoSlope.x != 0.0 || Tilt.NapPseudoSlope.y != 0.0);
			if (NapTurnRule &&
				!PieceRunsToStop(P.Speed, D.K, P.GNorm, D.Cs, SinBound, Tilt.Tolerance, Tilt.RefreshMaxInterval))
			{
				Delta = Min(Delta, TurnDuration(P, D.K, kNapMaxTurn));
			}
			const bool Refresh = Delta < Remaining;
			const PursuitState Node = EvaluatePursuit(D.X0, D.G, D.K, Delta);

			// Deviation of the node from the linear path, r_node - r_i - v_i Delta, formed without the O(Delta)
			// cancellation of whole displacements: Rolling X(Delta) - x_i Delta; Sliding (with v = L_c + c_s x and
			// L_c(tau) = L_c0 + (1 - c_s) G tau) (1 - c_s) G Delta^2 / 2 + c_s (X(Delta) - x_i Delta).
			Vec2 A2;
			if (Delta > 0.0)
			{
				const Vec2 Curve = Node.Integral - D.X0 * Delta;
				const double InvDelta2 = 1.0 / (Delta * Delta);
				A2 = S.State == MotionState::Sliding ? D.G * (0.5 * (1.0 - D.Cs)) + Curve * (D.Cs * InvDelta2) : Curve * InvDelta2;
			}

			Seg.TauEnd = Delta;
			Seg.Accel2 = ToVec3(A2);
			if (S.State == MotionState::Sliding)
			{
				// w_h(tau) = (1/R) z_hat x (v(tau) - u(tau)), u = (v - L_c) / c_s: linear, dw_h/dtau = ((1 - c_s)/(c_s R)) z_hat x (G - 2 A2).
				const double Factor = (1.0 - D.Cs) / (D.Cs * Seg.Radius);
				const Vec2 H = D.G - A2 * 2.0;
				Seg.OmegaDotH = {-Factor * H.y, Factor * H.x, 0.0};
			}
			Seg.Tilt.Active = true;
			Seg.Tilt.EndsInRefresh = Refresh;
			Seg.Tilt.X0 = D.X0;
			Seg.Tilt.G = D.G;
			Seg.Tilt.K = D.K;
			Seg.Tilt.Cs = D.Cs;
			Seg.Tilt.XEnd = Refresh ? Node.X : Vec2{};
			return true;
		}

		// Velocity and spin of a tilt chain piece for the pursuit variable X at local time Tau (Sliding: x = u).
		void ApplyPursuitVelocity(BallState& S, const MotionSegment& Seg, const Vec2& X, double Tau)
		{
			const TiltChain& C = Seg.Tilt;
			Vec2 V = X;
			if (Seg.State == MotionState::Sliding)
			{
				const Vec2 Lc = XY(Seg.Vel0) - C.X0 * C.Cs + C.G * ((1.0 - C.Cs) * Tau);
				V = Lc + X * C.Cs;
				const Vec3 W = ZCrossOverR(V - X, Seg.Radius);
				S.Omega = {W.x, W.y, OmegaZAt(Seg, Tau)};
			}
			else
			{
				const Vec3 W = ZCrossOverR(V, Seg.Radius);
				S.Omega = {W.x, W.y, OmegaZAt(Seg, Tau)};
			}
			S.Velocity = {V.x, V.y, 0.0};
		}

		double ClampTau(const MotionSegment& Seg, double Tau)
		{
			if (!(Tau > 0.0))
			{
				return 0.0;
			}
			return Tau < Seg.TauEnd ? Tau : Seg.TauEnd;
		}
	}

	MotionState ClassifyState(BallState& S, double Radius, double SupportZ, const NumericsConfig& Numerics)
	{
		switch (S.State)
		{
		case MotionState::PocketPivot:
		case MotionState::PocketFall:
		case MotionState::Pocketed:
		case MotionState::OffTable:
			return S.State;
		case MotionState::Stationary:
		case MotionState::Spinning:
		case MotionState::Sliding:
		case MotionState::Rolling:
		case MotionState::Airborne:
			break;
		}

		if ((S.Position.z - SupportZ) - Radius > Numerics.EpsZ || S.Velocity.z > Numerics.EpsV)
		{
			S.State = MotionState::Airborne;
			return S.State;
		}

		S.Position.z = SupportZ + Radius;
		S.Velocity.z = 0.0;
		const double EpsV2 = Numerics.EpsV * Numerics.EpsV;
		const Vec2 U = HorizontalSlip(S.Velocity, S.Omega, Radius);
		if (LengthSquared(U) > EpsV2)
		{
			S.State = MotionState::Sliding;
		}
		else if (S.Velocity.x * S.Velocity.x + S.Velocity.y * S.Velocity.y > EpsV2)
		{
			S.State = MotionState::Rolling;
			S.Omega.x = -S.Velocity.y / Radius;
			S.Omega.y = S.Velocity.x / Radius;
		}
		else
		{
			S.Velocity = Vec3::Zero();
			S.Omega.x = 0.0;
			S.Omega.y = 0.0;
			if (Abs(S.Omega.z) > Numerics.EpsWTimesRadius / Radius)
			{
				S.State = MotionState::Spinning;
			}
			else
			{
				S.Omega.z = 0.0;
				S.State = MotionState::Stationary;
			}
		}
		return S.State;
	}

	MotionSegment MakeSegment(const BallState& S, double T0, const BallSpec& Spec, const ClothParams& Surface, double SupportZ, double Gravity)
	{
		MotionSegment Seg;
		Seg.State = S.State;
		Seg.T0 = T0;
		Seg.Radius = Spec.Radius;
		Seg.SupportZ = SupportZ;
		Seg.Pos0 = S.Position;
		Seg.Vel0 = S.Velocity;
		Seg.Omega0 = S.Omega;

		switch (S.State)
		{
		case MotionState::Sliding:
		{
			const double InertiaK = InertiaFactor(Spec);
			const Vec2 U = HorizontalSlip(S.Velocity, S.Omega, Spec.Radius);
			const double SlipSpeed = Length(U);
			if (SlipSpeed > 0.0)
			{
				const Vec2 UHat = U / SlipSpeed;
				const double Friction = Surface.SlidingFriction * Gravity; // mu_s g
				Seg.Accel2 = {-0.5 * Friction * UHat.x, -0.5 * Friction * UHat.y, 0.0};
				const double SpinRate = Friction / (InertiaK * Spec.Radius); // 5 mu_s g / (2R) for k = 2/5
				Seg.OmegaDotH = {-SpinRate * UHat.y, SpinRate * UHat.x, 0.0};
				Seg.TauEnd = SlideDuration(SlipSpeed, Surface.SlidingFriction, Gravity, InertiaK);
			}
			else
			{
				Seg.TauEnd = 0.0;
			}
			SetSpinDecay(Seg, S.Omega.z, Surface.SpinDeceleration);
			break;
		}
		case MotionState::Rolling:
		{
			const Vec2 V = XY(S.Velocity);
			const double Speed = Length(V);
			if (Speed > 0.0)
			{
				const Vec2 VHat = V / Speed;
				const double Decel = Surface.RollingResistance * Gravity; // mu_r g
				Seg.Accel2 = {-0.5 * Decel * VHat.x, -0.5 * Decel * VHat.y, 0.0};
				Seg.TauEnd = RollDuration(Speed, Surface.RollingResistance, Gravity);
			}
			else
			{
				Seg.TauEnd = 0.0;
			}
			SetSpinDecay(Seg, S.Omega.z, Surface.SpinDeceleration);
			break;
		}
		case MotionState::Spinning:
			SetSpinDecay(Seg, S.Omega.z, Surface.SpinDeceleration);
			Seg.TauEnd = Seg.OmegaZStopTau;
			break;
		case MotionState::Airborne:
			Seg.Accel2 = {0.0, 0.0, -0.5 * Gravity};
			Seg.TauEnd = LandingTau(S.Position.z - SupportZ, S.Velocity.z, Spec.Radius, Gravity);
			break;
		case MotionState::PocketFall:
		case MotionState::PocketPivot: // real pivots come from rb/Physics/PocketDrop.h; here: ballistic, no end
			Seg.Accel2 = {0.0, 0.0, -0.5 * Gravity};
			break;
		case MotionState::Pocketed:
		case MotionState::OffTable: // terminal: frozen where it is
			Seg.Vel0 = Vec3::Zero();
			Seg.Omega0 = Vec3::Zero();
			break;
		case MotionState::Stationary:
			break;
		}
		return Seg;
	}

	BallState EvaluateSegment(const MotionSegment& Seg, double Tau)
	{
		const double T = ClampTau(Seg, Tau);
		BallState S;
		S.State = Seg.State;
		S.Position = PositionAt(Seg, T);
		S.Velocity = VelocityAt(Seg, T);
		switch (Seg.State)
		{
		case MotionState::Sliding:
			S.Omega = {Seg.Omega0.x + Seg.OmegaDotH.x * T, Seg.Omega0.y + Seg.OmegaDotH.y * T, OmegaZAt(Seg, T)};
			break;
		case MotionState::Rolling:
			S.Omega = {-S.Velocity.y / Seg.Radius, S.Velocity.x / Seg.Radius, OmegaZAt(Seg, T)};
			break;
		case MotionState::Spinning:
			S.Omega = {0.0, 0.0, OmegaZAt(Seg, T)};
			break;
		case MotionState::Stationary:
		case MotionState::Airborne:
		case MotionState::PocketPivot:
		case MotionState::PocketFall:
		case MotionState::Pocketed:
		case MotionState::OffTable:
			S.Omega = Seg.Omega0;
			break;
		}
		return S;
	}

	BallState SegmentEndState(const MotionSegment& Seg, const NumericsConfig& Numerics)
	{
		if (!(Seg.TauEnd < kInfinity))
		{
			// No internal end (Stationary, PocketFall, terminal states): the state is the segment's start state.
			return EvaluateSegment(Seg, 0.0);
		}

		const double T = Seg.TauEnd > 0.0 ? Seg.TauEnd : 0.0;
		BallState S;
		S.State = Seg.State;
		S.Position = PositionAt(Seg, T);
		S.Velocity = Vec3::Zero();
		S.Omega = Vec3::Zero();

		switch (Seg.State)
		{
		case MotionState::Sliding:
		{
			Vec2 V;
			if (Seg.Tilt.Active)
			{
				if (Seg.Tilt.EndsInRefresh)
				{
					// Exact node: velocity and spin from the pursuit solution, state unchanged, no snap.
					ApplyPursuitVelocity(S, Seg, Seg.Tilt.XEnd, T);
					return S;
				}
				// End of the tilted slide: u := 0, v := L_c(T) exact.
				V = XY(Seg.Vel0) - Seg.Tilt.X0 * Seg.Tilt.Cs + Seg.Tilt.G * ((1.0 - Seg.Tilt.Cs) * T);
			}
			else
			{
				V = XY(VelocityAt(Seg, T)); // = v0 - (k/(1+k)) u0: the rolling velocity (Coriolis invariant, A.5)
			}
			const Vec3 W = ZCrossOverR(V, Seg.Radius); // snap w_h := z_hat x v / R
			S.Velocity = {V.x, V.y, 0.0};
			S.Omega = {W.x, W.y, OmegaZAt(Seg, T)};
			break;
		}
		case MotionState::Rolling:
			if (Seg.Tilt.Active && Seg.Tilt.EndsInRefresh)
			{
				ApplyPursuitVelocity(S, Seg, Seg.Tilt.XEnd, T);
				return S;
			}
			S.Omega.z = OmegaZAt(Seg, T); // snap v := 0, w_h := 0
			break;
		case MotionState::Spinning:
			break; // snap w_z := 0
		case MotionState::Airborne:
		{
			// Landing on the plane z = SupportZ + R (C.2): snap the height and take the exact impact speed
			// v_z = -sqrt(v_z0^2 + 2 g (z0 - z_land)) (energy); horizontal motion and spin from the segment.
			const BallState E = EvaluateSegment(Seg, T);
			S.Velocity = E.Velocity;
			S.Omega = E.Omega;
			if (Seg.Accel2.z < 0.0)
			{
				const double Height = (Seg.Pos0.z - Seg.SupportZ) - Seg.Radius;
				const double Disc = Seg.Vel0.z * Seg.Vel0.z - 4.0 * Seg.Accel2.z * Height;
				S.Position.z = Seg.SupportZ + Seg.Radius;
				S.Velocity.z = -Sqrt(Disc > 0.0 ? Disc : 0.0);
			}
			return S; // landings are routed by the simulator (collisions 6.1), not classified here
		}
		case MotionState::Stationary:
		case MotionState::PocketPivot:
		case MotionState::PocketFall:
		case MotionState::Pocketed:
		case MotionState::OffTable:
			return EvaluateSegment(Seg, T);
		}

		ClassifyState(S, Seg.Radius, Seg.SupportZ, Numerics);
		return S;
	}

	double PursuitStopTime(const Vec2& X0, const Vec2& G, double K)
	{
		return StopTimeOf(MakePursuitSetup(X0, G), K);
	}

	PursuitState EvaluatePursuit(const Vec2& X0, const Vec2& G, double K, double Tau)
	{
		PursuitState Out;
		const PursuitSetup P = MakePursuitSetup(X0, G);
		if (!(P.Speed > 0.0))
		{
			Out.Lambda = kInfinity; // at rest: x = 0, X = 0
			return Out;
		}
		if (!(Tau > 0.0))
		{
			Out.X = X0;
			return Out;
		}

		if (IsQuadraticPursuit(P, K))
		{
			// Level (G = 0 or negligible) or collinear (downhill c0 = 1: K - |G|, uphill c0 = -1: K + |G|): exact quadratic
			// until the stop.
			const Vec2 XHat = P.XHat;
			const double Decel = QuadraticDeceleration(P, K);
			const double TStop = StopTimeOf(P, K);
			const double T = Tau < TStop ? Tau : TStop;
			const Vec2 Acc = XHat * (-Decel);
			Out.Integral = X0 * T + Acc * (0.5 * T * T);
			if (Tau < TStop)
			{
				Out.X = X0 + Acc * T;
				const double Rate = IsNegligibleDrive(P, K) ? 0.0 : Decel / P.GNorm; // p -+ 1
				Out.Lambda = Rate > 0.0 ? -Log1p(-T / TStop) / Rate : 0.0;
			}
			else
			{
				Out.Lambda = kInfinity;
			}
			return Out;
		}

		const double GNorm = P.GNorm;
		if (!(K > GNorm))
		{
			// Invalid (the ball would never stop; ValidatePhysicsParams rejects it): no deceleration.
			Out.X = X0;
			Out.Integral = X0 * Tau;
			return Out;
		}

		const double Km = K - GNorm;
		const double Kp = K + GNorm;
		const double Speed2 = P.Speed * P.Speed;
		const double TStop = StopTimeOf(P, K);

		double Lam = kInfinity;
		if (Tau < TStop)
		{
			const double TermA = P.Speed * P.A / Km; // a: T - t(lam) = a e^{-m lam} + b e^{-n lam}
			const double TermB = P.Speed * P.B / Kp; // b
			const double M = Km / GNorm;             // p - 1
			const double N = Kp / GNorm;             // p + 1
			const double BigLam = -Log1p(-Tau / TStop);
			Lam = SolvePursuitLambda(TermA / TStop, TermB / TStop, M, N, BigLam);
		}
		Out.Lambda = Lam;

		// x(lam) = |x0| [(A e^{-(p-1) lam} - B e^{-(p+1) lam}) G_hat + s0 e^{-p lam} e_perp]
		// X(lam) = |x0|^2 [(A^2 E_{2p-2} / (2(K - |G|)) - B^2 E_{2p+2} / (2(K + |G|))) G_hat
		//                  + s0 (A E_{2p-1} / (2K - |G|) + B E_{2p+1} / (2K + |G|)) e_perp],   E_n = -expm1(-n lam)
		double XPar = 0.0;
		double XPerp = 0.0;
		double E2m = 1.0;
		double E2n = 1.0;
		double E2pm = 1.0;
		double E2pp = 1.0;
		if (Lam < kInfinity)
		{
			const double InvG = 1.0 / GNorm;
			const double Em = Exp(-(Km * InvG) * Lam); // e^{-(p-1) lam}
			const double E1 = Exp(-Lam);
			XPar = P.Speed * (P.A * Em - P.B * Em * (E1 * E1));
			XPerp = P.Speed * P.S0 * Em * E1;
			E2m = -Expm1(-(2.0 * Km * InvG) * Lam);
			E2n = -Expm1(-(2.0 * Kp * InvG) * Lam);
			E2pm = -Expm1(-((2.0 * K - GNorm) * InvG) * Lam);
			E2pp = -Expm1(-((2.0 * K + GNorm) * InvG) * Lam);
		}
		const double IPar = Speed2 * (P.A * P.A * E2m / (2.0 * Km) - P.B * P.B * E2n / (2.0 * Kp));
		const double IPerp = Speed2 * P.S0 * (P.A * E2pm / (2.0 * K - GNorm) + P.B * E2pp / (2.0 * K + GNorm));
		Out.X = P.GHat * XPar + P.EPerp * XPerp;
		Out.Integral = P.GHat * IPar + P.EPerp * IPerp;
		return Out;
	}

	double TiltPieceDuration(double SpeedX, double K, double GNorm, double Cs, double SinBound, double Tolerance, double MaxInterval, double Remaining)
	{
		if (PieceRunsToStop(SpeedX, K, GNorm, Cs, SinBound, Tolerance, MaxInterval))
		{
			return Remaining; // collinear (exact quadratic), tail piece to the exact stop, or invalid settings (the chain terminates)
		}

		// Unique positive root of a3 D^3 + a1 D - a0 = 0 (convex, increasing): Newton from the right of the root
		// (both start values over-estimate it) decreases monotonically.
		const double A3 = (2.0 / 81.0) * Cs * K * GNorm * SinBound;
		const double A1 = Tolerance * (K + GNorm);
		const double A0 = Tolerance * SpeedX;
		double D = A0 / A1;
		if (A3 > 0.0)
		{
			D = Min(D, Cbrt(A0 / A3));
			for (int Iteration = 0; Iteration < 100; ++Iteration)
			{
				const double D2 = D * D;
				const double F = (A3 * D2 + A1) * D - A0;
				const double Step = F / (3.0 * A3 * D2 + A1);
				if (!(Step > 0.0))
				{
					break; // at the root (rounding may leave F <= 0)
				}
				D -= Step;
				if (Step <= 1e-15 * D)
				{
					break;
				}
			}
		}
		return Min(MaxInterval, Min(Remaining, D));
	}

	MotionSegment MakeSegment(const BallState& S, double T0, const BallSpec& Spec, const ClothParams& Surface, double SupportZ, double Gravity,
		const TiltParams& Tilt)
	{
		MotionSegment Seg = MakeSegment(S, T0, Spec, Surface, SupportZ, Gravity);
		if (IsLevel(Tilt))
		{
			return Seg; // bitwise the level segment (A-MOT-3, HF-B10)
		}
		switch (S.State)
		{
		case MotionState::Airborne:
		case MotionState::PocketFall:
			if (Tilt.Slope.x != 0.0 || Tilt.Slope.y != 0.0)
			{
				// dv/dt = (g_t, -g): exact quadratic; the landing time is vertical only.
				const Vec2 Gt = InPlaneGravity(Tilt, Gravity);
				Seg.Accel2.x += 0.5 * Gt.x;
				Seg.Accel2.y += 0.5 * Gt.y;
			}
			break;
		case MotionState::Sliding:
		case MotionState::Rolling:
			MakeTiltPiece(Seg, S, InertiaFactor(Spec), Surface, SupportZ, Gravity, Tilt);
			break;
		case MotionState::Stationary: // held by the static rolling resistance (|s| <= 0.7 mu_r)
		case MotionState::Spinning:
		case MotionState::PocketPivot: // pivots neglect the tilt
		case MotionState::Pocketed:
		case MotionState::OffTable:
			break;
		}
		return Seg;
	}

	BallState EvaluateSegmentForEvent(const MotionSegment& Seg, double Tau)
	{
		BallState S = EvaluateSegment(Seg, Tau);
		if (!Seg.Tilt.Active)
		{
			return S;
		}
		const double T = ClampTau(Seg, Tau);
		const PursuitState Exact = EvaluatePursuit(Seg.Tilt.X0, Seg.Tilt.G, Seg.Tilt.K, T);
		ApplyPursuitVelocity(S, Seg, Exact.X, T);
		return S;
	}
}
