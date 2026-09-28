#pragma once

// Rolling over the rounded drop edge into a pocket: analytic PIVOT macro-step (physics-collisions 5.4).
// Owner: WP-4 (cushion, facing & pocket-edge resolution).
//
// A ball whose center reaches the drop-edge circle a_d = r_p + r_d (moving inward) pivots about the
// rounding axis (circle of radius a_d at z = -r_d) on a circle of radius rho = R + r_d, rolling
// without slip, until N = 0: cos(psi_leave) = (2 + (1 + k) v0^2 / (g rho)) / (3 + k), which is the
// spec's (10 + 7 v0^2 / (g rho)) / 17 for k = 2/5 (k = I / (m R^2)). If v0 >= sqrt(g rho) it leaves
// immediately. T_p is integrated with the asinh substitution (16 Simpson panels, v0 >= 1e-4).
//
// General inertia (DERIVED): energy (1 + k)/2 m (v^2 - v0^2) = m g rho (1 - cos psi), so
// v(psi)^2 = v0^2 + b sin^2(psi/2) with b = 4 g rho / (1 + k) ((20/7) g rho for k = 2/5), and with
// x = sin(psi/2) = sqrt(a/b) sinh(u), a = v0^2: T(psi) = (2 rho / sqrt(b)) integral_0^U du / sqrt(1 - (a/b) sinh^2 u).
// The pivot keeps the tangential speed along the edge and rolls without slip in both directions:
// w = (v(psi)/R) t_e + (v_t/R)(sin psi z_hat - cos psi n_e) + w_z0 e_r, where e_r is the direction from the axis
// to the center; the last term keeps the pre-pivot spin about the contact normal (w_z on the cloth), so a ball
// rolling onto the edge has no spin jump at psi = 0 (spec: |w| = v/R for v_t = w_z0 = 0).
// Along the edge (integration round 2, cross-package fix; v1 drifted STRAIGHT along the crossing tangent): the rounding
// axis is the circle a_d about the capture center, so the whole meridian-plane pivot (axis point, n_e, t_e, the state)
// turns about the pocket's vertical axis by theta(tau) = -v_t tau / a_d (the axis point moves along the circle at v_t).
// The center then stays exactly rho from the rounding axis: the straight drift of v1 put a ball entering at an angle
// up to a few mm INTO the rounded slate edge at the leave (rim-torus overlap diagnostics, VAL ROB-11). The speeds, the
// leave angle and T_p are unchanged (energy as the spec); v_t = 0 gives bitwise the v1 path.

#include "rb/Config.h"
#include "rb/Core/Tolerances.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"

#include <cstdint>

namespace rb
{
	// Pocket model (PhysicsParams::Pockets).
	enum class PocketModel : std::uint8_t
	{
		GeometricLevelA, // collisions 5.3-5.4: drop edge a_d, pivot, facings, liner / back wall, rim torus, capture depth (default)
		CaptureCircle,   // pooltool circle pockets (prior-art XREF-01 only): captured when the CENTER enters the circle
		                 //   (PocketGeometry::CaptureCenter, CaptureRadius); no pivot, rim or liner
	};

	struct PivotResult
	{
		bool Immediate = false;   // v0 >= sqrt(g rho): no pivot, leave the edge ballistically at once
		double LeaveAngle = 0.0;  // psi_leave from vertical [rad] (53.968 deg at v0 = 0)
		double Duration = 0.0;    // T_p [s]
		double LeaveSpeed = 0.0;  // v_leave [m/s] (normal-plane speed; the tangential component is unchanged)
	};

	// Leave angle / time / speed for normal speed V0 over the edge and pivot radius Rho (P-2), for a
	// ball with inertia factor InertiaK (0.4 = solid sphere).
	RB_API PivotResult ComputePivot(double V0, double Rho, double InertiaK, double Gravity, const NumericsConfig& Numerics);

	struct PivotPath
	{
		PocketId Pocket = PocketId::None;
		double T0 = 0.0;          // absolute time of the DropEdge event [s]
		double Radius = 0.0;      // ball radius R [m]
		double Rho = 0.0;         // R + r_d [m]
		Vec3 AxisPoint;           // point on the rounding axis below the crossing point (z = -r_d) [m]
		Vec3 EdgeNormal;          // n_e: horizontal unit normal of the drop edge, toward the hole
		Vec3 EdgeTangent;         // horizontal unit tangent of the edge, z_hat x n_e
		double NormalSpeed0 = 0.0;    // v0 along n_e at the crossing [m/s]
		double TangentialSpeed = 0.0; // speed along EdgeTangent, unchanged during the pivot [m/s]
		PivotResult Result;
		// v1 additions (WP-4): what EvaluatePivot needs to invert T(psi) and to rebuild the spin without the caller's
		// parameters. PivotSpeed0 = max(NormalSpeed0, NumericsConfig::PivotMinSpeed) is the v0 the path uses.
		double Gravity = 0.0;         // g [m/s^2]
		double InertiaK = kSolidSphereInertiaFactor; // k = I / (m R^2)
		double PivotSpeed0 = 0.0;     // v0 of the pivot integral (floored) [m/s]
		int SimpsonPanels = 16;       // panels of the asinh-substituted integral (NumericsConfig::PivotSimpsonPanels, even)
		double UpperLimit = 0.0;      // U(psi_leave) = asinh(sqrt(b/a) sin(psi_leave / 2)) of the substitution [1]
		Vec3 Velocity0;               // ball velocity at the DropEdge event [m/s] (Immediate leave: unchanged)
		Vec3 Omega0;                  // ball spin at the DropEdge event [rad/s] (Immediate leave: unchanged; its z
		                              //   component is kept as spin about the contact normal during the pivot)
		// Integration round 2 additions: the rounding axis is a circle, so the tangential motion follows it (see EvaluatePivot).
		Vec3 AxisCenter;              // center of the rounding-axis circle: the capture center at z = -r_d [m]
		double AxisRadius = 0.0;      // its radius a_d = r_p + r_d [m] (0: no curvature, the straight drift of v1)
	};

	// Builds the pivot for a ball on the cloth whose center is exactly on the drop-edge circle.
	RB_API PivotPath MakePivotPath(const BallState& AtDropEdge, double T0, const PocketGeometry& Pocket, const BallSpec& Spec, double Gravity,
		const NumericsConfig& Numerics);

	// Exact state at local time Tau in [0, Duration] (psi(Tau) by inverting T(psi)); State = PocketPivot.
	RB_API BallState EvaluatePivot(const PivotPath& Path, double Tau);

	// State at the leave angle (State = PocketFall, ballistic from here). Equals the DropEdge state
	// with the center at psi = 0 when Result.Immediate.
	RB_API BallState PivotLeaveState(const PivotPath& Path);

	// Quadratic proxy of the whole pivot path (the true path is a circle): r(tau) through the start position/velocity and the
	// leave position at Duration. It deviates from the true arc by up to ~0.1 mm, so the simulator no longer detects on it
	// (WP-10: contacts found on it were resolved with the balls up to 52 um inside each other, VAL ROB-11); kept for tools and
	// tests. The simulator uses the pieces of MakePivotProxyPiece.
	RB_API MotionSegment PivotDetectionProxy(const PivotPath& Path);

	// Piecewise detection proxy of the pivot (WP-10 fix of VAL ROB-11; collisions 5.4 "the ball can touch a facing or another
	// ball"). The pivot [0, T_p] is covered by consecutive quadratic PIECES. Each piece interpolates the TRUE path (EvaluatePivot)
	// at three nodes of its span - start, the node of the mid substitution value, end - so the true path stays within Tolerance
	// of the piece over the whole span (DERIVED: the interpolation remainder |e| <= M3 G(mu) h^3 / 6 with an analytic bound M3
	// of |d^3 r / d tau^3| over the span, safety factor 2; see PocketDrop.cpp). The simulator predicts the pocket's facings and
	// jaw arcs and the other balls on the current piece, starts the next piece at its end (a no-impulse node event, like a tilt
	// refresh) and resolves a contact found on a piece at the true state, so the true gap at a resolved contact is >= -Tolerance.
	// Nodes are exact points of the true path (errors do not accumulate). Pieces are chosen in the substitution variable U of the
	// time law (no inversion of T(psi) is needed): a piece from FromU ends at UpperU; the last piece ends at UpperLimit (T_p).
	inline constexpr double kPivotProxyTolerance = 1.0e-7; // [m] max distance of the true pivot path from its detection piece

	struct PivotProxyPiece
	{
		MotionSegment Seg;   // State PocketPivot, T0 = the piece start (absolute), TauEnd = its span
		double UpperU = 0.0; // substitution variable U at the piece end (FromU of the next piece)
		bool Last = true;    // the piece ends at the pivot end (Seg.T0 + Seg.TauEnd = Path.T0 + Duration)
		double Deviation = 0.0; // the bound of the true path's distance from the piece over its span (<= Tolerance) [m]
	};

	// Piece of a (non-immediate) pivot from the substitution value FromU (0 = the drop edge), starting at absolute time T0 (the
	// caller passes Path.T0 for the first piece and the previous piece's end time afterwards).
	RB_API PivotProxyPiece MakePivotProxyPiece(const PivotPath& Path, double FromU, double T0, double Tolerance);
}
