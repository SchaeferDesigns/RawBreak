#pragma once

// Rolling over the rounded drop edge into a pocket: analytic PIVOT macro-step (physics-collisions 5.4).
// Owner: WP-4 (cushion, facing & pocket-edge resolution).
//
// A ball whose center reaches the drop-edge circle a_d = r_p + r_d (moving inward) pivots about the
// rounding axis (circle of radius a_d at z = -r_d) on a circle of radius rho = R + r_d, rolling
// without slip, until N = 0: cos(psi_leave) = (2 + (1 + k) v0^2 / (g rho)) / (3 + k), which is the
// spec's (10 + 7 v0^2 / (g rho)) / 17 for k = 2/5 (k = I / (m R^2)). If v0 >= sqrt(g rho) it leaves
// immediately. T_p is integrated with the asinh substitution (16 Simpson panels, v0 >= 1e-4).

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
		Vec3 EdgeTangent;         // horizontal unit tangent of the edge
		double NormalSpeed0 = 0.0;    // v0 along n_e at the crossing [m/s]
		double TangentialSpeed = 0.0; // speed along EdgeTangent, unchanged during the pivot [m/s]
		PivotResult Result;
	};

	// Builds the pivot for a ball on the cloth whose center is exactly on the drop-edge circle.
	RB_API PivotPath MakePivotPath(const BallState& AtDropEdge, double T0, const PocketGeometry& Pocket, const BallSpec& Spec, double Gravity,
		const NumericsConfig& Numerics);

	// Exact state at local time Tau in [0, Duration] (psi(Tau) by inverting T(psi)); State = PocketPivot.
	RB_API BallState EvaluatePivot(const PivotPath& Path, double Tau);

	// State at the leave angle (State = PocketFall, ballistic from here). Equals the DropEdge state
	// with the center at psi = 0 when Result.Immediate.
	RB_API BallState PivotLeaveState(const PivotPath& Path);

	// Quadratic proxy of the pivot path for event detection (the true path is a circle): r(tau) through
	// the start position/velocity and the leave position at Duration. The simulator predicts on it
	// against other balls AND against the pocket's facings (face + top edge) and jaw arcs with the
	// airborne predictors (collisions 5.4: "the ball can touch a facing or another ball"). Contacts found
	// on the proxy truncate the pivot and are resolved by GRI with the ball free.
	RB_API MotionSegment PivotDetectionProxy(const PivotPath& Path);
}
