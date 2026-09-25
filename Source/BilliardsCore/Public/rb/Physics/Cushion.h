#pragma once

// Ball vs fixed geometry contact resolution: Mathavan et al. 2010 (default for balls on the cloth),
// Han 2005 (option), the pooltool cross-validation models "mirror" (unrealistic) and Stronge
// compliant (prior-art XREF-01/02), generic 3D rigid impulse GRI (airborne balls, liner, rim, rail
// top, slate parity), the speed-dependent cushion restitution law, and the dispatcher used by the
// simulator. physics-collisions 4.1-4.9, 5.3 (element contact models), 5.5 (facing params), 6.2 (rail top).
// Owner: WP-4 (cushion, facing & pocket-edge resolution).
//
// Inertia: every model uses the per-ball k = I / (m R^2) (rb/Physics/BallState.h); the spec's
// k_w = 5 / (2 m R) becomes 1 / (k m R) and GRI's 2m/7 becomes k m / (1 + k).
//
// Cushion-local frame (4.1): Y_hat horizontal INTO the feature, Z_hat = z_hat, X_hat = Y_hat x Z_hat;
// a proper rotation about z, so v and w transform identically. GRI normal k_hat points from the
// contact point to the ball center; impulse on the ball is +P_N k_hat + P_f (pitfall 1).

#include "rb/Config.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"

#include <cstdint>

namespace rb
{
	enum class CushionModel : std::uint8_t
	{
		Mathavan2010,     // RK4 over the normal impulse, cushion + cloth friction, energetic restitution (default,
		                  //   also for the AI: the AI plans with the referee's physics)
		Han2005,          // closed-form single impulse at the nose, v_Z discarded (option, 2.6-5.6 deg too flat)
		Mirror,           // pooltool "unrealistic" (XREF-01 only): v_Y' = -e v_Y, tangential velocity and spin unchanged
		StrongeCompliant, // port of pooltool's Stronge compliant cushion (XREF-02 only; Apache-2.0, THIRD_PARTY_NOTICES.md)
	};

	// e_c(v_perp) = clamp(Max - Slope * max(0, v_perp - Knee), Min, Max) (4.8, DERIVED/TUNING):
	// e_c(0.5) = e_c(1) = 0.97, e_c(3) = 0.90, e_c(10) = 0.655, e_c(20) = 0.60 (M-7).
	struct CushionRestitutionLaw
	{
		double Max = 0.97;
		double Slope = 0.035; // [s/m]
		double Knee = 1.0;    // [m/s]
		double Min = 0.60;
	};

	struct CushionParams
	{
		CushionModel OnClothModel = CushionModel::Mathavan2010;
		CushionRestitutionLaw Restitution;   // also used for GRI (airborne) at v_c (ESTIMATE)
		double Friction = 0.14;              // mu_w (Mathavan 2010; pooltool 0.2), range 0.1-0.3
		int MathavanSteps = 32;              // RK4 steps N over (1 + e) m v_Y. DECISION (architecture 15): N is the smallest
		                                     //   value for which M-2..M-4 stay within 2e-4 of the N = 20 000 reference WITH
		                                     //   slip-reversal splitting; 32 is the placeholder until WP-4 measures it
		                                     //   (expected 20-40). The spec's N = 200 is pinned only by M-6.
		int MathavanMaxBisections = 60;      // step bisection on v_Y = 0 and on the work target
		bool MathavanSplitAtSlipReversal = true; // 4th-order convergence: split steps at slip zero crossings (collisions 4.5)
		double StrongeOmegaRatio = 1.8;      // StrongeCompliant only: pooltool omega_ratio (XREF-02)
		double NoseProfileRadius = 0.0;      // r_n used by the physics (the 1 mm art radius is not)
		bool PooltoolCompat = false;         // contact distance R instead of R_c (equipment 12.2)
		double FacingRestitutionScale = 1.0; // k_f: e_f = k_f e_c(v_perp); 0.85 for soft bar facings (ESTIMATE)
		double FacingFriction = 0.14;        // mu_f = mu_w (ESTIMATE)
	};

	struct PocketContactParams
	{
		double LinerRestitution = 0.3;   // e_l leather/rubber liner (ESTIMATE, 5.3)
		double LinerFriction = 0.3;      // mu_l
		double RimRestitution = 0.6;     // rounded slate edge hit from inside (torus, 5.3), ESTIMATE = e_slate
		double RimFriction = 0.2;        // ESTIMATE = mu_s
		double RailTopRestitution = 0.5; // e_rt cloth-covered wood (ESTIMATE, 6.2)
		double RailTopFriction = 0.3;    // mu_rt (impacts; also the sliding friction of a ball moving on the rail top)
		double RailTopRollingResistance = 0.010; // mu_r on the cushion top / rail cap (ESTIMATE, architecture decision)
		double RailTopSpinDeceleration = 10.0;   // alpha_sp on the rail top [rad/s^2] (ESTIMATE)
	};

	// Friction parameters of the flat rail cap as a support surface (MakeSegment with SupportZ = RailTopZ).
	constexpr ClothParams RailCapSurface(const PocketContactParams& P) { return {P.RailTopFriction, P.RailTopRollingResistance, P.RailTopSpinDeceleration}; }

	struct CushionFrame
	{
		Vec3 X; // along the feature
		Vec3 Y; // horizontal, into the feature
		Vec3 Z; // up
	};

	RB_API CushionFrame MakeCushionFrame(const Vec3& IntoFeatureHorizontal);

	constexpr Vec3 ToLocal(const CushionFrame& F, const Vec3& V) { return {Dot(V, F.X), Dot(V, F.Y), Dot(V, F.Z)}; }
	constexpr Vec3 ToWorld(const CushionFrame& F, const Vec3& V) { return F.X * V.x + F.Y * V.y + F.Z * V.z; }

	enum class FixedContactKind : std::uint8_t
	{
		NoseEdge,      // cushion nose line (edge at height h)
		JawArcEdge,    // rounded jaw point (edge at height h)
		FacingFace,    // undercut facing plane (on the shelf: Mathavan with theta = beta_v)
		FacingTopEdge, // facing upper edge (airborne only)
		Liner,         // hole wall / liner cylinder, undercut beta_l (GRI)
		RimTorus,      // rounded slate edge hit from inside the pocket (GRI with the torus normal)
		RailTop,       // cushion-top slope or rail cap plane (GRI)
		RailTopEdge,   // convex rail-top edge (cushion-back ridge, cap edge at a pocket cut, outer edge) hit by an airborne ball (GRI)
		Slate,         // k_hat = z_hat: GRI parity with motion spec C.3 (G-3)
	};

	// Contact frame produced by the detection package (rb/Physics/Detect.h MakeFixedContact).
	struct FixedContact
	{
		FixedContactKind Kind = FixedContactKind::NoseEdge;
		bool BallOnCloth = true;      // selects Mathavan/Han (on cloth / shelf) vs GRI (airborne)
		Vec3 Normal;                  // k_hat: from the contact point to the ball center (unit)
		Vec3 IntoFeature;             // Y_hat: horizontal unit into the feature (on-cloth models)
		double Elevation = 0.0;       // theta_c (edges) or beta_v (facing face) [rad]
		std::uint8_t RailFeature = 0xFF; // rail feature id (Core/Ids.h), 0xFF for non-rail features
		PocketId Pocket = PocketId::None;
	};

	struct CushionImpactResult
	{
		Vec3 Velocity;               // after the impact (world frame for ResolveGri/ResolveFixedContact, local for Mathavan/Han)
		Vec3 Omega;
		double NormalSpeed = 0.0;    // approach speed used for e and audio [m/s]
		double NormalImpulse = 0.0;  // accumulated normal impulse P [N s]
		double Restitution = 0.0;    // e actually used (0 for a resting contact)
		bool Stick = false;          // Han / GRI stick branch
		bool Resting = false;        // approach speed < v_rest: e := 0, only tangential motion kept (7.3)
	};

	RB_API double CushionRestitution(double NormalSpeed, const CushionRestitutionLaw& Law);

	struct MathavanSettings
	{
		double Elevation = 0.2733943;  // theta (theta_c for edges, beta_v for facing faces) [rad]
		double Restitution = 0.97;     // e (energetic, Stronge), evaluated ONCE at the pre-impact v_Y
		double CushionFriction = 0.14; // mu_w
		double ClothFriction = 0.2;    // mu_s
		int Steps = 32;
		int MaxBisections = 60;
		bool SplitAtSlipReversal = true;
		double SlipEps = 1e-6;         // [m/s]
		double RestSpeed = 2e-3;       // [m/s] v_Y below this: v_Y := 0 (resting / pressing contact)
	};

	// Mathavan 2010 (4.5), LOCAL frame, ball on the cloth (v_Z = 0 throughout); precondition v_Y > 0.
	// k_w = 1 / (k m R) with k = InertiaFactor(Spec) (5 / (2 m R) for a solid ball). Returns local
	// v' = (v_X, v_Y, 0) and w'. Performance gate: <= 2 us per hit (Release, reference CPU) at the default N.
	RB_API CushionImpactResult ResolveMathavan(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& Spec, const MathavanSettings& Settings);

	// Han 2005 (4.4), LOCAL frame; returns the full impulse result INCLUDING v_Z (C-H1 lists it); the
	// dispatcher discards v_Z for balls on the cloth.
	RB_API CushionImpactResult ResolveHan(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& Spec, double Elevation, double Restitution,
		double Friction);

	// pooltool "unrealistic" cushion (XREF-01), LOCAL frame: v_Y' = -e v_Y; v_X, v_Z and w unchanged.
	RB_API CushionImpactResult ResolveMirror(const Vec3& VelocityLocal, const Vec3& OmegaLocal, double Restitution);

	// Port of pooltool's Stronge compliant cushion model (XREF-02), LOCAL frame, with OmegaRatio,
	// restitution and friction as in pooltool (prior-art 2.8 / pitfall 12: Apache-2.0 attribution).
	RB_API CushionImpactResult ResolveStronge(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& Spec, double Elevation, double Restitution,
		double Friction, double OmegaRatio);

	// Generic 3D rigid impulse (4.6), WORLD frame: P_N = (1 + e) m v_c, stick if (2m/7)|s| <= mu P_N
	// (general inertia: effective tangential mass from Spec), v' = v + P/m, w' = w + (r_I x P)/I.
	RB_API CushionImpactResult ResolveGri(const Vec3& Velocity, const Vec3& Omega, const BallSpec& Spec, const Vec3& Normal, double Restitution,
		double Friction);

	// Dispatcher (4.7): on-cloth edge/face contacts -> OnClothModel (Mathavan: theta_c for edges,
	// theta = beta_v and e_f = k_f e_c for facing faces; Han / Mirror / StrongeCompliant when selected);
	// airborne, liner, rim, rail top and rail-top edges -> GRI with the element's e/mu. Applies the
	// resting-contact rule (v_n < RestSpeed -> e = 0). Output in the world frame; the caller classifies
	// (ClassifyState) and re-predicts.
	RB_API CushionImpactResult ResolveFixedContact(const FixedContact& Contact, const BallState& Ball, const BallSpec& Spec, const CushionParams& Cushion,
		const PocketContactParams& Pocket, const ClothParams& Cloth, const NumericsConfig& Numerics);
}
