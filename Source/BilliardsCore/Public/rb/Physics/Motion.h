#pragma once

// Ball motion on a horizontal support and in flight: closed-form segments, transition times, state
// classification (physics-motion-and-cue Part A and C.1).
// Owner: WP-1 (motion, slate & cue strike).
//
// A MotionSegment is one closed-form piece of a ball's trajectory, valid for local time
// tau = t - T0 in [0, TauEnd]:
//     r(tau)   = Pos0 + Vel0 tau + Accel2 tau^2              (Accel2 = half the constant acceleration)
//     v(tau)   = Vel0 + 2 Accel2 tau
//     w_h(tau) = Omega0_h + OmegaDotH tau                      (Sliding; Rolling: w_h = z_hat x v / R)
//     w_z(tau) = Omega0.z + OmegaZRate * min(tau, OmegaZStopTau)   (clamped at 0, A.7)
// Always evaluate in LOCAL time (implementation note 1): never build global-time polynomials.
//
// Supports: the surface states (Stationary, Spinning, Sliding, Rolling) live on a horizontal plane
// z = SupportZ: the cloth/slate/shelf (SupportZ = 0, cloth friction) or the flat rail cap
// (SupportZ = RailTopZ, friction PocketContactParams rail-cap values, collisions 6.2). The sloped
// cushion top is never a segment (rigid CLI island).
//
// Inertia: formulas are generalised with the per-ball k = I / (m R^2) (rb/Physics/BallState.h); for
// k = 2/5 they are the motion spec's formulas.
//
// Tilted table (human-factors 4.5, HF-50/51): the core frame stays attached to the bed; a slope s adds the
// in-plane gravity g_t = -g s. Sliding and Rolling then follow the pursuit law dx/dt = G - K x_hat (not a
// polynomial), which MakeSegment(..., TiltParams) covers with a CHAIN of quadratic pieces whose nodes lie on the
// exact solution (bounded error Tolerance, a TiltRefresh event at each node, rb/Physics/EventQueue.h).
// Detection is unchanged (every piece is a quadratic). Events inside a piece take the position from the piece
// and velocity/spin from the exact solution (EvaluateSegmentForEvent). With a level TiltParams every function
// here is bitwise identical to the level physics (all MOT/COL/VAL tests; HF-B10).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"

namespace rb
{
	// ---------------------------------------------------------------------------------------------
	// Cloth parameters (A.9). The presets are TUNING estimates inside the published ranges. The same
	// struct describes any horizontal support surface (the flat rail cap uses its own values).
	// ---------------------------------------------------------------------------------------------
	struct ClothParams
	{
		double SlidingFriction = 0.20;  // mu_s [1], range 0.15-0.40 (also used during slate and cushion impacts)
		double RollingResistance = 0.010; // mu_r [1] = rolling deceleration / g, range 0.005-0.015
		double SpinDeceleration = 10.0; // alpha_sp [rad/s^2], range 5-15 (parametrised directly, A.2)
	};

	inline constexpr ClothParams kClothDefault{0.20, 0.010, 10.0};
	inline constexpr ClothParams kClothWorstedFast{0.17, 0.007, 8.0}; // clean worsted, e.g. Simonis 860 / 760
	inline constexpr ClothParams kClothNappedBar{0.26, 0.014, 13.0};  // napped woolen bar cloth, worn/humid

	constexpr ClothParams ClothParamsFor(ClothPreset Preset)
	{
		switch (Preset)
		{
		case ClothPreset::WorstedFast: return kClothWorstedFast;
		case ClothPreset::NappedBar: return kClothNappedBar;
		case ClothPreset::Default: break;
		}
		return kClothDefault;
	}

	// Leckie-Greenspan spinning-friction coefficient, for reporting only: mu_sp = 2 R alpha_sp / (5 g).
	constexpr double SpinFrictionCoefficient(double AlphaSp, double Radius, double Gravity) { return 2.0 * Radius * AlphaSp / (5.0 * Gravity); }

	// ---------------------------------------------------------------------------------------------
	// Table tilt and nap (human-factors 4.5.1, 4.5.6, 4.5.7). Part of PhysicsParams (PhysicsParams::Tilt);
	// per venue table from rb/Human/Venue.h via TableCondition (rb/Physics/Simulator.h).
	// ---------------------------------------------------------------------------------------------
	struct TiltParams
	{
		Vec2 Slope;                      // s = gradient of the bed height in the core frame [1] (1 mm/m = 1e-3); a ball
		                                 //   drifts toward -s. (0, 0) = level (default; every MOT/COL/VAL test). ValidatePhysicsParams
		                                 //   requires |s| <= 0.7 mu_r of the cloth and of the rail cap (resting balls stay put)
		double Tolerance = 5e-5;         // eps_tilt [m]: max deviation of a chain piece from the exact path (AI rollouts may use 5e-4)
		double RefreshMaxInterval = 2.0; // [s] longest chain piece
		Vec2 NapPseudoSlope;             // zeta_n n_nap [1] (HF-51, Later; default 0): rolling balls drift TOWARD +NapPseudoSlope with
		                                 //   the extra drive g NapPseudoSlope (napped bar cloth 2e-4 along +x, EST; worsted 0)
		double NapResistance = 0.0;      // eta_n [1] in [0, 1) (Later; default 0): rolling resistance mu_r g (1 - eta_n v_hat . n_nap),
		                                 //   n_nap = NapPseudoSlope / |NapPseudoSlope| (ignored while NapPseudoSlope = 0), frozen per piece.
		                                 //   Nap acts only on the cloth (SupportZ == 0), never on the rail cap or inside CLI islands.
		// Validity (ValidatePhysicsParams; Simulator::Run per ball with its k): the rolling drive stays within half the
		// static rolling resistance, |Slope| / (1 + k) + |NapPseudoSlope| <= (1 - NapResistance) mu_r / 2 (|Slope| <= 0.7 mu_r
		// for k = 2/5 without nap), so resting balls stay put and every chain has K - |G| >= K / 2 > 0 (it terminates).
	};

	// True if the table is exactly level and nap-free: MakeSegment(..., Tilt) is then MakeSegment(...) bitwise.
	constexpr bool IsLevel(const TiltParams& Tilt)
	{
		return Tilt.Slope.x == 0.0 && Tilt.Slope.y == 0.0 && Tilt.NapPseudoSlope.x == 0.0 && Tilt.NapPseudoSlope.y == 0.0;
	}

	// In-plane gravity g_t = -g s [m/s^2] (4.5.1; the neglected normal factor 1/sqrt(1 + |s|^2) is <= 1.3e-5 off for |s| <= 5e-3).
	constexpr Vec2 InPlaneGravity(const TiltParams& Tilt, double Gravity) { return Tilt.Slope * (-Gravity); }

	// Pursuit data of one tilt chain piece (human-factors 4.5.2-4.5.3). Sliding: x = slip u_h, G = g_t,
	// K = mu_s g (1 + k)/k, c_s = k/(1 + k), and v = L_c + c_s x with L_c(tau) = L_c0 + (1 - c_s) G tau
	// (L_c0 = Vel0_h - c_s X0; the Coriolis invariant drifts on a tilted table). Rolling: x = v_h,
	// G = g_t/(1 + k) + g NapPseudoSlope, K = mu_r g (1 - eta_n v_hat0 . n_nap), c_s = 1. Always |G| < K.
	struct TiltChain
	{
		bool Active = false;        // Sliding / Rolling piece with G != 0 (otherwise the level closed forms apply)
		bool EndsInRefresh = false; // TauEnd is a chain node (TiltRefresh event), not the end of the phase (Transition)
		Vec2 X0;                    // pursuit variable at tau = 0 [m/s]
		Vec2 G;                     // constant drive [m/s^2]
		double K = 0.0;             // pursuit rate [m/s^2]
		double Cs = 1.0;            // c_s [1]
		Vec2 XEnd;                  // exact pursuit variable at TauEnd [m/s] (0 at the end of the phase)
	};

	// ---------------------------------------------------------------------------------------------
	// Closed-form segment
	// ---------------------------------------------------------------------------------------------
	struct MotionSegment
	{
		MotionState State = MotionState::Stationary;
		double T0 = 0.0;              // absolute start time of the segment [s] (per-ball time base)
		double TauEnd = kInfinity;    // local time of the next internal transition / landing [s]
		double Radius = kDefaultBallRadius; // R of this ball [m] (rolling constraint, spin rates)
		double SupportZ = 0.0;        // surface states: height of the horizontal support plane [m] (0 = cloth,
		                              //   RailTopZ = flat rail cap); Airborne: height of the plane the landing refers to (0)
		Vec3 Pos0;                    // r(0) [m]
		Vec3 Vel0;                    // v(0) [m/s]
		Vec3 Accel2;                  // A2 [m/s^2]: Sliding -1/2 mu_s g u_hat0; Rolling -1/2 mu_r g v_hat0;
		                              //   Airborne/PocketFall -1/2 g z_hat (+ 1/2 g_t in-plane on a tilted table); Stationary/Spinning 0;
		                              //   tilt chain piece: (r_node - Pos0 - Vel0 TauEnd) / TauEnd^2 (human-factors 4.5.3)
		Vec3 Omega0;                  // w(0) [rad/s]
		Vec3 OmegaDotH;               // Sliding: (mu_s g / (k R)) z_hat x u_hat0 [rad/s^2] (= 5 mu_s g / (2R) for k = 2/5); otherwise 0
		double OmegaZRate = 0.0;      // dw_z/dtau while w_z != 0 [rad/s^2]: -sgn(w_z0) alpha_sp on a support, 0 in flight
		double OmegaZStopTau = kInfinity; // tau at which w_z reaches 0 and then stays 0 [s]
		TiltChain Tilt;               // tilted table: this segment is a chain piece (Accel2 is then NOT parallel to Vel0 in
		                              //   general; Sliding OmegaDotH is still constant, Rolling w_h = z_hat x v / R). Inactive on a level table.
	};

	// ---------------------------------------------------------------------------------------------
	// Kinematic helpers (header-only; hot paths of detection and playback)
	// ---------------------------------------------------------------------------------------------

	// Support contact-point slip velocity u = v + w x (-R z_hat) = v + R (z_hat x w)  (0.3).
	constexpr Vec3 SlipVelocity(const Vec3& Velocity, const Vec3& Omega, double Radius)
	{
		return {Velocity.x - Radius * Omega.y, Velocity.y + Radius * Omega.x, Velocity.z};
	}

	// Horizontal spin of a ball rolling without slip: w_h = (1/R) z_hat x v.
	constexpr Vec3 RollingOmegaH(const Vec3& Velocity, double Radius) { return {-Velocity.y / Radius, Velocity.x / Radius, 0.0}; }

	// Coriolis invariant L_c = v_h / (1 + k) - (k / (1 + k)) R (z_hat x w_h) (A.5; (5/7, 2/7) for k = 2/5):
	// the velocity at which the ball will start rolling; unchanged by sliding, flight and slate impacts.
	constexpr Vec3 CoriolisInvariant(const Vec3& Velocity, const Vec3& Omega, double Radius, double InertiaK = kSolidSphereInertiaFactor)
	{
		const double A = 1.0 / (1.0 + InertiaK);
		const double B = InertiaK / (1.0 + InertiaK);
		return {A * Velocity.x + B * Radius * Omega.y, A * Velocity.y - B * Radius * Omega.x, 0.0};
	}

	// Kinetic + gravitational potential energy above rest height [J] (implementation note 12).
	constexpr double MechanicalEnergy(const BallState& S, const BallSpec& Spec, double Gravity)
	{
		const double Lift = S.Position.z > Spec.Radius ? S.Position.z - Spec.Radius : 0.0;
		return 0.5 * Spec.Mass * LengthSquared(S.Velocity) + 0.5 * Spec.Inertia * LengthSquared(S.Omega) + Spec.Mass * Gravity * Lift;
	}

	constexpr Vec3 PositionAt(const MotionSegment& Seg, double Tau) { return Seg.Pos0 + Seg.Vel0 * Tau + Seg.Accel2 * (Tau * Tau); }
	constexpr Vec3 VelocityAt(const MotionSegment& Seg, double Tau) { return Seg.Vel0 + Seg.Accel2 * (2.0 * Tau); }

	constexpr double OmegaZAt(const MotionSegment& Seg, double Tau)
	{
		return Seg.Omega0.z + Seg.OmegaZRate * (Tau < Seg.OmegaZStopTau ? Tau : Seg.OmegaZStopTau);
	}

	// Closed-form durations (A.8 table). Slide: k |u| / ((1 + k) mu_s g) = 2|u| / (7 mu_s g) for k = 2/5.
	constexpr double SlideDuration(double SlipSpeed, double MuS, double Gravity, double InertiaK = kSolidSphereInertiaFactor)
	{
		return InertiaK * SlipSpeed / ((1.0 + InertiaK) * MuS * Gravity);
	}
	constexpr double RollDuration(double Speed, double MuR, double Gravity) { return Speed / (MuR * Gravity); }
	constexpr double SpinDuration(double OmegaZ, double AlphaSp) { return Abs(OmegaZ) / AlphaSp; }

	// ---------------------------------------------------------------------------------------------
	// Segment construction, evaluation, transitions
	// ---------------------------------------------------------------------------------------------

	// A.8 classifier with the exact snaps of implementation note 2 / prior-art 5.8, relative to the
	// support plane z = SupportZ. Mutates S:
	//   Airborne if (z - SupportZ - R) > EpsZ or v_z > EpsV; otherwise z := SupportZ + R, v_z := 0 and
	//   Sliding if |u| > EpsV, Rolling if |v| > EpsV (w_h := z_hat x v / R), Spinning if |w_z| > eps_w
	//   (v := 0, w_h := 0), else Stationary (all zero).
	// States PocketPivot, PocketFall, Pocketed and OffTable are simulator-owned and returned unchanged.
	// A downward v_z at the support must be resolved by ResolveSlateImpact before calling this.
	RB_API MotionState ClassifyState(BallState& S, double Radius, double SupportZ, const NumericsConfig& Numerics);

	// Builds the closed-form segment for an already classified state S starting at absolute time T0.
	// Surface = friction parameters of the support (cloth, or the rail cap: RailCapSurface() in
	// rb/Physics/Cushion.h); SupportZ as in ClassifyState. Uses k = InertiaFactor(Spec).
	// Sets Accel2, OmegaDotH, OmegaZRate/StopTau and TauEnd:
	//   Sliding SlideDuration(|u0|); Rolling |v0|/(mu_r g); Spinning |w_z0|/alpha_sp;
	//   Airborne (v_z0 + sqrt(v_z0^2 + 2 g (z0 - R)))/g (landing at z = R, C.2); PocketFall, Stationary,
	//   Pocketed, OffTable: +inf. PocketPivot segments are built by rb/Physics/PocketDrop.h instead.
	RB_API MotionSegment MakeSegment(const BallState& S, double T0, const BallSpec& Spec, const ClothParams& Surface, double SupportZ, double Gravity);

	// State at local time Tau in [0, TauEnd] (clamped). The motion state of the result is Seg.State.
	RB_API BallState EvaluateSegment(const MotionSegment& Seg, double Tau);

	// Exact state at the segment end, snapped and re-classified (Sliding -> Rolling/Spinning/
	// Stationary, Rolling -> Spinning/Stationary, Spinning -> Stationary). For Airborne/PocketFall the
	// state just before the landing is returned unchanged (State stays Airborne): landings are routed
	// by the simulator (collisions 6.1) and resolved with ResolveSlateImpact.
	// Tilt chain piece: EndsInRefresh -> the exact node state (position PositionAt(TauEnd), velocity / spin from
	// Tilt.XEnd; State unchanged, no snap); otherwise the end of the phase with the snaps of human-factors 4.5.3
	// (sliding end: u := 0, v := v_node exact, w_h := z_hat x v / R; rolling end: v := 0, w_h := 0), then classified.
	RB_API BallState SegmentEndState(const MotionSegment& Seg, const NumericsConfig& Numerics);

	// ---------------------------------------------------------------------------------------------
	// Tilted table (human-factors 4.5.2-4.5.3). Owner: WP-1. Needs rb::Expm1 (rb/Math/Scalar.h).
	// ---------------------------------------------------------------------------------------------

	// Exact solution of the pursuit problem dx/dt = G - K x_hat with constant G, K > |G| (4.5.2).
	struct PursuitState
	{
		Vec2 X;          // x(tau) [m/s]
		Vec2 Integral;   // X(tau) = integral of x over [0, tau] [m]
		double Lambda = 0.0; // lam = -ln(w / w0), w = tan(beta / 2) (kInfinity at and after the stop)
	};

	// T_stop = |x0| (A/(K - |G|) + B/(K + |G|)), A = (1 + c0)/2, B = (1 - c0)/2, c0 = x_hat0 . G_hat [s];
	// the collinear cases are the exact quadratics |x0|/(K -+ |G|). |x0| = 0 -> 0.
	RB_API double PursuitStopTime(const Vec2& X0, const Vec2& G, double K);

	// State at Tau >= 0 through one monotone 1-D solve of t(lam) = Tau (safeguarded Newton with bisection,
	// |dlam| <= 1e-15 max(1, lam)); Tau >= T_stop gives x = 0 and X = X(inf). G = 0 reduces to the level law.
	RB_API PursuitState EvaluatePursuit(const Vec2& X0, const Vec2& G, double K, double Tau);

	// Length Delta of the next chain piece from |x_i| (4.5.3 refresh rule): Remaining (= PursuitStopTime(x_i)) if the motion is
	// collinear (SinBound <= 1e-12) or in the tail (|x_i| <= sqrt(Tolerance (K - |G|) / (4 Cs))); otherwise
	// min(MaxInterval, Remaining, the unique positive root D of (2/81) Cs K |G| SinBound D^3 + Tolerance (K + |G|) D
	// - Tolerance |x_i| = 0). SinBound = sin(beta_i) if cos(beta_i) >= 0, else 1 (beta = angle between x and G).
	RB_API double TiltPieceDuration(double SpeedX, double K, double GNorm, double Cs, double SinBound, double Tolerance, double MaxInterval, double Remaining);

	// MakeSegment on a (possibly) tilted table. IsLevel(Tilt) -> exactly MakeSegment(S, T0, Spec, Surface, SupportZ,
	// Gravity) (bitwise; A-MOT-3). Otherwise: Sliding / Rolling -> one chain piece (Tilt.Active, TauEnd = Delta of
	// TiltPieceDuration, EndsInRefresh if Delta < T_stop, node exact from EvaluatePursuit); Airborne / PocketFall ->
	// the ballistic quadratic with the in-plane part g_t / 2 in Accel2; Stationary / Spinning unchanged (static rolling
	// resistance holds the ball, |s| <= 0.7 mu_r); PocketPivot is built by rb/Physics/PocketDrop.h (tilt neglected).
	RB_API MotionSegment MakeSegment(const BallState& S, double T0, const BallSpec& Spec, const ClothParams& Surface, double SupportZ, double Gravity,
		const TiltParams& Tilt);

	// State used to RESOLVE an event at local time Tau (4.5.3, 9.2 item 6): position from the segment (what detection
	// used, at most Tolerance from the exact path), velocity and spin from the exact pursuit state for a tilt chain
	// piece (one 1-D solve). Identical to EvaluateSegment for every other segment. Approach tests (collisions 2.4
	// step 1) keep the segment velocity VelocityAt(Seg, Tau).
	RB_API BallState EvaluateSegmentForEvent(const MotionSegment& Seg, double Tau);
}
