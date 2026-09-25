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
// of an id-INDEPENDENT geometric key (lexicographic (x, y, z) of the contact point, ties by the
// canonically signed normal, then by feature index), never by ball id. Compliant mode computes every
// contact force from the state at the start of the step and then accumulates them per body in key
// order (Jacobi); Rigid mode sweeps contacts in key order (Gauss-Seidel). Pair forces are computed so
// that the two bodies receive exactly negated values. Hence permuting ball ids gives bit-identical
// bodies after mapping ids back.

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
		double GridCell = 0.06;            // [m] uniform broad-phase grid cell for large islands (pitfall 11)
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

	struct IslandContactRecord
	{
		IslandRecordKind Kind = IslandRecordKind::BallBall;
		double Time = 0.0;
		int BallA = -1;
		int BallB = -1;          // ball-ball partner, -1 otherwise
		int Feature = -1;        // island feature index, -1 otherwise
		int Strike = -1;         // tip records: index into SimInput::Strikes
		Vec3 Normal;             // from A to B (ball-ball), from the feature / tip to the ball
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
		// Params.TsujiAlpha must already be resolved (>= 0). Starts in Mode.
		RB_API void Reset(double StartTime, CliMode Mode, const CliParams& Params, const BallBallParams& BallBall, const ClothParams& Cloth,
			double Gravity, const NumericsConfig& Numerics);

		RB_API bool AddBody(const IslandBody& NewBody);          // false if full or the ball is already present
		RB_API bool AddFeature(const IslandFeature& NewFeature); // false if full; an already present (SourceKind, SourceIndex, SourceSub) is ignored (true)
		// A member leaves the island (reached a drop edge, left the cloth region, left the rail top, ...):
		// its current state is returned and every contact state involving it is dropped.
		RB_API bool RemoveBody(int Ball, IslandBody& Out);

		// Cue tips (index = strike): add while the tip moves and could touch a member; remove when the
		// island ends or the tip stops (its state is returned as a new analytic path at Time()).
		RB_API bool SetTip(int Strike, const IslandTip& Tip);
		RB_API bool RemoveTip(int Strike, CueTipPath& Out);
		RB_API bool HasTip(int Strike) const;

		RB_API void SetMode(CliMode NewMode);
		CliMode Mode() const { return CurrentMode; }

		// Advances all bodies (and tips) by one step of the current mode and appends first-touch / tip
		// records (in contact-key order).
		RB_API void Step(IslandRecordList& NewRecords);

		// Compliant mode: every active contact has had |v_n| < SustainedSpeed for SustainedSteps steps.
		RB_API bool SustainedContact() const;

		// Exit test (3.9.2 + architecture 8.8): no contact force for ExitZeroForceSteps steps, every pair
		// separating (f' >= 0), every geometric gap >= 0, and no touching pair (|gap| <= ContactTol) with
		// |f'| <= ApproachSpeedTol and f'' < 0 (it would re-trigger the pressing rule at once).
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
		// One contact / pair state (sparse): ball-ball, ball-feature or tip-ball.
		struct ContactState
		{
			std::int16_t BodyBall = -1;  // lower ball id (ball-ball) or the ball (feature / tip)
			std::int16_t Other = -1;     // partner ball id, or kFeatureBase + feature, or kTipBase + strike
			double Mu0 = 0.0;            // friction coefficient frozen at first touch
			double Damping = 0.0;        // eta / c_c / tip damping, frozen at first touch
			bool Touching = false;
			bool Armed = true;           // emits a record on the next first touch
			int SlowSteps = 0;           // consecutive steps with |v_n| < SustainedSpeed while touching
		};

		double CurrentTime = 0.0;
		CliMode CurrentMode = CliMode::Compliant;
		CliParams Settings;
		BallBallParams ContactModel;
		ClothParams ClothSettings;
		NumericsConfig Tolerances;
		double GravityAccel = kStandardGravity;
		int Steps = 0;
		int ZeroForceSteps = 0;
		FixedVector<IslandBody, kMaxBalls> Bodies;
		FixedVector<IslandFeature, kMaxIslandFeatures> Features;
		FixedVector<ContactState, kMaxIslandContacts> Contacts;
		IslandTip Tips[kMaxStrikes] = {};
		bool TipActive[kMaxStrikes] = {};
	};
}
