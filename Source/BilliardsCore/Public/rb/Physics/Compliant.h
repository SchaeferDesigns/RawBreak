#pragma once

// Compliant Local Integrator (CLI) for touching clusters, the break, pressing contacts, Zeno chains,
// sustained ("touching and accelerating into each other") contacts and motion on the sloped rail
// top (physics-collisions 3.9, 3.6 pressing rule, 6.2, 7.3).
// Owner: WP-3 (ball-ball & compliant islands). The SIMULATOR (WP-6b) decides when an island starts,
// who joins or leaves, which table features take part and when it ends; this class only integrates.
//
// Two modes (Docs/architecture.md 8.8):
//  * Compliant: Hertz ball-ball contact (K = 8.0587e8 N/m^1.5) with Tsuji damping (speed-independent
//    restitution), regularised Coulomb friction frozen per contact at first touch, linear compliant
//    cushions / jaws / facings, compliant cue tip, cloth support and friction; fixed dt = 1 us.
//    Used for every impact transient (clusters, break, frozen-ball strikes).
//  * Rigid: velocity-level sequential impulses (restitution above RestSpeed, 0 below) with Coulomb
//    friction and position projection, dt = 20 us, RigidIterations sweeps. Used for SUSTAINED contacts:
//    the island switches Compliant -> Rigid when SustainedContact() holds (every active contact slower
//    than SustainedSpeed for SustainedSteps steps: the Hertz transient is over) or when the compliant
//    phase exceeds NumericsConfig::CompliantMaxDuration, and for balls on the sloped rail top (6.2).
//    A rigid island runs until its contacts open or its bodies rest; there is never a fallback to
//    plain impulses (an impulse cannot resolve a zero-speed contact, collisions pitfall 16).
//
// Determinism and id independence (COL CL-8, prior-art BRK-04): contacts are processed in the order
// of an id-INDEPENDENT geometric key (lexicographic (x, |y|, z, y) of the contact point, ties by the
// canonically signed normal, then by the feature's table source (kind, index, sub)), never by ball id.
// (|y| before the sign of y makes a body's contact order the mirror image of its mirror body's order, so
// configurations symmetric about the long axis y = 0, e.g. the break, stay exactly symmetric, COL CL-7.)
// The key is evaluated when the candidate list (a Verlet list: every ball pair / ball-feature pair
// within a skin distance) is rebuilt, which happens at id-independent instants (a body moved more than
// half the skin, or bodies / features were added); pair roles are canonical (the body with the
// lexicographically smaller centre at the rebuild is "A"). Compliant mode computes every contact force
// from the state at the start of the step and then accumulates them per body in key order (Jacobi);
// Rigid mode sweeps contacts in key order (Gauss-Seidel). Pair forces are computed so that the two
// bodies receive exactly negated values. Hence permuting ball ids gives bit-identical bodies after
// mapping ids back. Tip contacts follow the pair contacts (in strike order), support (cloth) contacts
// come last and are per body (they commute).
//
// Support (cloth) model (collisions 3.9.2 "cloth"; architecture 8.8, 8.11): bodies flagged ClothSupport
// get the table reaction (v_z >= 0 at z = R) and Coulomb cloth friction with the normal impulse the cloth
// actually carries (m g dt plus any downward contact force, e.g. from a cushion nose above the centre),
// applied as a per-step impulse capped at the stop-slip value (exact Coulomb: rolling and rest are
// exact, no creep); while rolling, the rolling resistance mu_r and the spin deceleration alpha_sp of
// the cloth act like in event mode (capped, so a ball at rest on a table tilted within the validity rule
// stays exactly at rest, A-CLI-5), which lets rigid islands come to rest (8.8). Plane features (rail top)
// support bodies through their contact force; their rolling resistance and spin deceleration act in Rigid
// mode (rail-top islands are rigid, 6.2). Landings of hopping bodies inside an island are inelastic.
// Rigid mode warm-starts sustained contacts with the previous step's impulses and ends the sweeps early once
// no contact velocity changes by more than EpsV.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallBall.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Cushion.h"
#include "rb/Physics/Motion.h"

#include <cstdint>

namespace rb
{
	enum class CliMode : std::uint8_t
	{
		Compliant, // Hertz + Tsuji, dt = TimeStep (default for every island start)
		Rigid,     // sequential impulses with position projection, dt = RigidTimeStep (sustained contacts, rail top, Level B)
	};

	struct CliParams
	{
		double HertzStiffness = 8.0587e8;  // K [N/m^1.5], DERIVED from Marlow via TP B.29 (range 8e8-1.2e9)
		double TsujiAlpha = -1.0;          // alpha_T [1]; < 0 = derived from BallBallParams::Restitution with
		                                   //   TsujiAlphaForRestitution at Run start (0.03689 for e_b = 0.95, 0.05242 for 0.93),
		                                   //   so islands and impulses always use the same restitution. CL tests pin 0.03689.
		double TimeStep = 1.0e-6;          // dt_cli [s] (e error 3e-5)
		double CushionStiffness = 1.0e6;   // k_c [N/m] ESTIMATE (collisions OQ 3)
		double SlipRegularization = 1.0e-3;// s_reg [m/s]
		int ExitZeroForceSteps = 5;        // exit needs this many consecutive force-free steps (+ separation + gap >= 0)
		double JoinFactor = 1.2;           // delta_cl = max(eps_touch, JoinFactor v_n T_H(v_n))
		double RigidTimeStep = 20.0e-6;    // [s] Rigid mode (collisions 5.6 / 6.2)
		int RigidIterations = 4;           // sequential-impulse sweeps per rigid step
		double SustainedSpeed = 2.0e-3;    // [m/s] sustained-contact detector: every active contact |v_n| below this (= v_rest) ...
		int SustainedSteps = 200;          // ... for this many consecutive compliant steps (0.2 ms) -> switch to Rigid mode
		double GridCell = 0.06;            // [m] reserved: the broad phase is a Verlet candidate list (skin 2 mm, rebuilt when a
		                                   //   body moved 1 mm), so no step is O(N^2) (pitfall 11); a grid is not needed for 24 balls
		bool ClothSupport = true;          // bodies flagged ClothSupport keep v_z >= 0 at z = R and feel cloth friction (off for CL-1..CL-5)
	};

	inline constexpr int kMaxIslandFeatures = 32;
	inline constexpr int kMaxIslandContacts = 192;      // sparse contact / pair-state list (armed and touching pairs)
	inline constexpr int kMaxIslandRecordsPerStep = 32;
	inline constexpr int kMaxIslandPlaneVertices = 8;

	enum class IslandFeatureKind : std::uint8_t
	{
		EdgeLine,    // horizontal line SEGMENT (cushion nose at h, facing top edge, rail-top ridge / cap edges);
		             //   contact distance R + Radius from the segment (end caps are points)
		JawCircle,   // rounded jaw: circle of radius r_j at height h, exposed only between AngleFrom and AngleFrom + AngleSweep
		FacingPlane, // undercut facing face: plane through the plan segment [Point, Point + Length Direction] at height h,
		             //   normal Normal (into the pocket, tilted down by beta_v); valid over the segment and ZMin <= z <= ZMax
		Plane,       // bounded plane: convex plan polygon (CCW) minus an optional cut disc (rail-top polygons); support and contact
	};

	struct IslandFeature
	{
		IslandFeatureKind Kind = IslandFeatureKind::EdgeLine;
		std::uint8_t RailFeature = 0xFF; // rail feature id (Core/Ids.h) for masks/records; 0xFF = none (rail top, liner)
		std::uint8_t SourceKind = 0;     // rb::TableFeatureKind of the table element (rb/Physics/Detect.h), for records and de-duplication
		std::uint8_t SourceIndex = 0;    // its index (TableFeatureRef::Index)
		std::uint8_t SourceSub = 0;      // its sub-index (TableFeatureRef::SubIndex)
		Vec3 Point;       // EdgeLine / FacingPlane: segment start (z = height); JawCircle: center; Plane: a point on the plane
		Vec3 Direction;   // EdgeLine / FacingPlane: unit along the segment (horizontal); Plane: unit normal (away from the material)
		Vec3 Normal;      // EdgeLine: unit horizontal normal toward the free side; FacingPlane: unit face normal
		double Length = 0.0;     // EdgeLine / FacingPlane segment length [m]
		double Radius = 0.0;     // JawCircle: r_j; EdgeLine: edge rounding r_n (0 in physics)
		double AngleFrom = 0.0;  // JawCircle exposed arc start (plan angle about the center) [rad]
		double AngleSweep = 0.0; // JawCircle exposed arc sweep (> 0) [rad]
		double ZMin = 0.0;       // FacingPlane vertical extent of the face [m]
		double ZMax = 0.0;
		int VertexCount = 0;                              // Plane
		Vec2 Vertices[kMaxIslandPlaneVertices];           // Plane: CCW plan polygon
		bool HasCut = false;                              // Plane: minus the disc |p - CutCenter| < CutRadius
		Vec2 CutCenter;
		double CutRadius = 0.0;
		CushionRestitutionLaw Restitution; // e(v_perp) evaluated at first touch -> damping c_c / rigid restitution
		                                   //   (a constant law Max = Min for fixed-e elements, e.g. the rail top 0.5)
		double RestitutionScale = 1.0;     // k_f for facings (e_f = k_f e_c)
		double Friction = 0.14;            // mu_w / mu_f / mu_rt
		double RollingResistance = 0.0;    // Plane only: mu_r of the surface (rail top)
		double SpinDeceleration = 0.0;     // Plane only: alpha_sp of the surface
	};

	struct IslandBody
	{
		int Ball = -1;          // ball id
		Vec3 Position;
		Vec3 Velocity;
		Vec3 Omega;
		double Radius = 0.0;
		double Mass = 0.0;
		double Inertia = 0.0;
		bool ClothSupport = true; // on the cloth/shelf (z = R): the cloth reaction keeps v_z >= 0 and cloth friction acts.
		                          // false: airborne or supported by Plane features (rail top)
	};

	// The follow-through cue tip as a kinematic island participant (item: frozen cue ball, RUL F7/G17):
	// a sphere of radius Path.DomeRadius whose center moves along Path.Direction only (1 DOF), mass M,
	// prescribed deceleration Path.Deceleration plus the reaction of its contact force; linear compliant
	// contact of stiffness Stiffness with damping from Restitution. Positive tip force = tip contact
	// (TipBegin / TipEnd records -> StrokeRecord::TipContacts); contacts with a ball other than the one it
	// struck are "touched ball" contacts (NonTipSource::CueTip).
	struct IslandTip
	{
		CueTipPath Path;         // state when the tip joined the island (StartTime = join time)
		double Mass = 0.0;       // M [kg]
		double Stiffness = 0.0;  // [N/m] CueTipContactStiffness(Cue.ContactTime, m, M)
		double Restitution = 0.0;// e_tip
		double Friction = 0.0;   // mu_tip (no miscue logic inside islands)
	};

	enum class IslandRecordKind : std::uint8_t
	{
		BallBall,    // first positive force of a ball pair (re-armed after separating by LeaveDistance), 3.9.6
		BallFeature, // first positive force of a ball-feature pair
		TipBegin,    // tip force became positive on BallA (Strike = cue index)
		TipEnd,      // tip force returned to zero on BallA
	};

	// A contact is "active" while its force (compliant) is positive with a compression beyond the numerical touching band
	// (delta > NumericsConfig::ContactTol), or while its normal impulse (rigid) exceeds ApproachSpeedTol times its effective
	// mass: frozen neighbours resting in the touching band (a racked cluster) carry no record and do not block the exit.
	struct IslandContactRecord
	{
		IslandRecordKind Kind = IslandRecordKind::BallBall;
		double Time = 0.0;       // start of the step whose state gave the first positive force (or the zero force of a TipEnd) [s]
		int BallA = -1;          // ball-ball: the LOWER ball id; feature / tip records: the ball
		int BallB = -1;          // ball-ball partner (higher id), -1 otherwise
		int Feature = -1;        // island feature index, -1 otherwise
		int Strike = -1;         // tip records: index into SimInput::Strikes
		Vec3 Normal;             // from A to B (ball-ball, like the event-mode n_hat), from the feature / tip to the ball
		double NormalSpeed = 0.0;// approach speed at first touch [m/s]
	};

	using IslandRecordList = FixedVector<IslandContactRecord, kMaxIslandRecordsPerStep>;

	// Hertz contact duration T_H(v) = 3.2181 (m*^2 / (K^2 v))^(1/5) [s] (3.9.3): 329 us at 1 m/s.
	RB_API double HertzContactTime(double ApproachSpeed, double ReducedMass, double HertzStiffness);

	// Tsuji damping coefficient eta = alpha_T sqrt(m* K).
	RB_API double TsujiDamping(double TsujiAlpha, double ReducedMass, double HertzStiffness);

	// alpha_T(e) for the island integrator at dt = 1 us (3.9.3: 0.03689 at e = 0.95, 0.05242 at 0.93).
	// Tabulated over e in [0.5, 1] (1 -> 0) and interpolated; the WP-3 tests check the table against a
	// bisection on a 2-ball head-on Hertz collision with this integrator to 5e-5. Cheap (no integration).
	RB_API double TsujiAlphaForRestitution(double Restitution);

	// Linear tip stiffness giving a half-period contact of ContactTime for the reduced mass of ball and
	// cue: k = pi^2 m_eff / T^2, m_eff = m M / (m + M) (DERIVED; CueSpec::ContactTime is the only tuning).
	RB_API double CueTipContactStiffness(double ContactTime, double BallMass, double CueMass);

	// Island joining distance delta_cl = max(ContactTol, JoinFactor v_n T_H(v_n)) [m] (3.9.2 step 1).
	RB_API double IslandJoinDistance(double ApproachSpeed, double ReducedMass, const CliParams& Params, double ContactTol);

	// Value type, no heap. See the file comment for modes and ordering.
	class CompliantIsland
	{
	public:
		// Params.TsujiAlpha should already be resolved (>= 0; a negative value is resolved here from BallBall.Restitution).
		// Starts in Mode; bodies, features, tips and contact states are cleared.
		RB_API void Reset(double StartTime, CliMode Mode, const CliParams& Params, const BallBallParams& BallBall, const ClothParams& Cloth,
			double Gravity, const NumericsConfig& Numerics);

		// Tilted table (human-factors 4.5.1, Docs/architecture.md 8.11; v1.2 review): in-plane gravity g_t = -g s [m/s^2]
		// acting on every body at its centre, so the full gravity in the bed frame is (g_t, -Gravity). Reset sets it to
		// (0, 0); Step applies the term only when it is non-zero (adding +0.0 would turn a -0.0 velocity component into
		// +0.0), so a level-table island stays bitwise the v1.1 island. The simulator (WP-6b) calls this right after Reset
		// with rb::InPlaneGravity(Params.Tilt, g). A body at rest stays at rest while |g_t| / (1 + k) is within the static
		// rolling resistance (as in event mode, ValidatePhysicsParams); nap is not applied inside islands (A-CLI-5).
		RB_API void SetInPlaneGravity(const Vec2& Accel);
		Vec2 InPlaneGravityAcceleration() const { return InPlaneGravityAccel; }

		RB_API bool AddBody(const IslandBody& NewBody);          // false if full, the id is outside [0, kMaxBalls) or already present
		RB_API bool AddFeature(const IslandFeature& NewFeature); // false if full; an already present (SourceKind, SourceIndex, SourceSub) is ignored (true)
		// A member leaves the island (reached a drop edge, left the cloth region, left the rail top, ...):
		// its current state is returned and every contact state involving it is dropped.
		RB_API bool RemoveBody(int Ball, IslandBody& Out);

		// Cue tips (index = strike): add while the tip moves and could touch a member (its position and speed at Time() follow
		// from the path); remove when the island ends or the tip stops: its state is returned as a new analytic path starting at
		// Time() with the same deceleration (a tip pushed backwards by a ball leaves at rest). The caller closes an open tip
		// contact interval (TipEnd) when it removes a touching tip.
		RB_API bool SetTip(int Strike, const IslandTip& Tip);
		RB_API bool RemoveTip(int Strike, CueTipPath& Out);
		RB_API bool HasTip(int Strike) const;

		RB_API void SetMode(CliMode NewMode);
		CliMode Mode() const { return CurrentMode; }

		// Advances all bodies (and tips) by one step of the current mode (semi-implicit Euler with Jacobi contact forces,
		// or sequential impulses + position projection) and appends first-touch / tip records (in contact-key order; at most
		// kMaxIslandRecordsPerStep, a record that does not fit stays armed and is emitted at the next step).
		RB_API void Step(IslandRecordList& NewRecords);

		// Compliant mode: at least one active contact, and every active contact (pairs and tips) has had |v_n| < SustainedSpeed
		// for SustainedSteps consecutive steps. Always false in Rigid mode.
		RB_API bool SustainedContact() const;

		// Exit test (3.9.2 + architecture 8.8): no active contact for ExitZeroForceSteps steps, every pair within
		// LeaveDistance separating (gap rate >= -ApproachSpeedTol), every geometric gap >= -ContactTol (the touching band;
		// no overlap), and no touching pair (|gap| <= ContactTol) with |gap rate| <= ApproachSpeedTol and gap'' < 0 under the
		// accelerations the bodies will have in event mode (sliding / rolling on the cloth incl. the tilt drive, ballistic
		// otherwise): it would re-trigger the pressing rule at once. Tips: no overlap and not approaching.
		RB_API bool CanExit() const;

		// Body at rest (|v| <= EpsV, |w| R <= EpsWTimesRadius): rigid islands end on rest.
		RB_API bool BodyAtRest(int Index) const;

		// Smallest gap [m] between a sphere (Position, Radius) and any island body (joining test).
		RB_API double GapToIsland(const Vec3& Position, double Radius) const;

		double Time() const { return CurrentTime; }
		int BodyCount() const { return Bodies.Size(); }
		const IslandBody& Body(int Index) const { return Bodies[Index]; }
		int FeatureCount() const { return Features.Size(); }
		const IslandFeature& Feature(int Index) const { return Features[Index]; }
		RB_API int FindBody(int Ball) const; // index or -1
		int StepCount() const { return Steps; }

	private:
		// One candidate pair of the Verlet list (sparse) with its contact state: ball-ball (A, B body indices, A = the
		// body with the lexicographically smaller centre at the last rebuild) or ball-feature (A body, Feature index).
		// Pairs leave the list only when their gap exceeds skin + LeaveDistance at a rebuild, i.e. after they separated
		// by more than LeaveDistance: a pair that enters again starts re-armed (3.9.6).
		struct PairState
		{
			std::int8_t A = -1;          // body index
			std::int8_t B = -1;          // body index (ball-ball) or -1
			std::int8_t Feature = -1;    // feature index (ball-feature) or -1
			bool InContact = false;      // geometric overlap at the last step: friction / damping / restitution frozen at first touch
			bool Active = false;         // positive force (compliant) or impulse (rigid) beyond the touching band at the last step
			bool Armed = true;           // emits a record on the next first positive force
			int SlowSteps = 0;           // consecutive compliant steps with |v_n| < SustainedSpeed while active
			double Mu0 = 0.0;            // friction coefficient frozen at first touch
			double Damping = 0.0;        // ball-ball: Tsuji eta of the pair; ball-feature: c_c frozen at first touch
			double Restitution = 0.0;    // rigid-mode restitution frozen at first touch
			double RadiusSum = 0.0;      // ball-ball: R_A + R_B [m]
			double WarmNormal = 0.0;     // rigid mode: normal impulse of the last step (warm start of a sustained contact) [N s]
			Vec3 WarmTangent;            // rigid mode: tangential impulse of the last step [N s]
		};

		// Contact state of one cue tip with one ball (indexed by ball id).
		struct TipBallState
		{
			bool InContact = false;
			bool Active = false;
			int SlowSteps = 0;
			double Damping = 0.0;        // frozen at first touch (from e_tip and the reduced mass of ball and cue)
		};

		void StepCompliant(IslandRecordList& NewRecords);
		void StepRigid(IslandRecordList& NewRecords);
		void RebuildPairs();
		void RebuildIfMoved();

		double CurrentTime = 0.0;
		CliMode CurrentMode = CliMode::Compliant;
		CliParams Settings;
		BallBallParams ContactModel;
		ClothParams ClothSettings;
		NumericsConfig Tolerances;
		double GravityAccel = kStandardGravity;
		Vec2 InPlaneGravityAccel;    // g_t [m/s^2] of a tilted table (0 = level)
		int Steps = 0;
		int ZeroForceSteps = 0;
		bool PairsDirty = true;      // bodies / features changed: rebuild the candidate list before use
		double CandidateSkin = 0.0;  // [m] skin of the current candidate list (0 after a capacity fallback: rebuilt every step)
		FixedVector<IslandBody, kMaxBalls> Bodies;
		FixedVector<IslandFeature, kMaxIslandFeatures> Features;
		FixedVector<PairState, kMaxIslandContacts> Pairs;
		Vec3 RebuildPosition[kMaxBalls] = {}; // body centres at the last candidate rebuild (by body index)
		double ClothWarmNormal[kMaxBalls] = {}; // rigid mode: last cloth impulses per body index (warm start)
		Vec3 ClothWarmTangent[kMaxBalls] = {};
		IslandTip Tips[kMaxStrikes] = {};
		bool TipActive[kMaxStrikes] = {};
		Vec3 TipCenter[kMaxStrikes] = {};     // current tip dome centre [m]
		double TipSpeed[kMaxStrikes] = {};    // current speed along Path.Direction [m/s]
		TipBallState TipContacts[kMaxStrikes][kMaxBalls] = {};
	};
}
