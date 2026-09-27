#pragma once

// BilliardsCore <-> Unreal coordinate adapter (ue5-realism-plan 5.6, architecture.md 13 item 1,
// Docs/ue-architecture.md 4). THE ONLY place where the mirror lives; the core never contains it.
// Owner: UE-0 (frozen contract, implemented). Tests: UE T14-T16 (Private/Tests/RbCoordsTests.cpp).
//
// Core frame: right-handed, metres, origin at the bed centre on the cloth, +x toward the foot rail, +y left
// (for a player at the head end), +z up. Unreal: left-handed, centimetres, z up.
// TABLE-LOCAL UE axes: X = x, Y = -y, Z = z (mirror M = diag(1, -1, 1)), scaled by 100. The table actor
// (ARbTable) places this local frame in the world: world = TableToWorld * local (scale must be 1).
//
//   p_UE [cm]      = 100 (x, -y, z)
//   v_UE [cm/s]    = 100 (vx, -vy, vz)
//   w_UE [rad/s]   = (-wx, wy, -wz)          angular velocity is a PSEUDOVECTOR: w' = det(M) M w
//   q_UE (w,x,y,z) = (qw, -qx, qy, -qz)      orientation quaternion under the same mirror
//   directions     = (dx, -dy, dz)           unit vectors (no scale)
//   azimuth        phi = atan2(-Y, X) of a table-local UE direction (core phi from +x toward +y)
//
// FQuat::RotateVector is the plain Hamilton formula, so q_UE rotates UE vectors exactly as q rotates core
// vectors under the mirror (T15). Never convert ball orientations through FRotator (pitfall 3).

#include "CoreMinimal.h"

#include "rb/Math/Quat.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"

struct FRbCoords
{
	static constexpr double CmPerMeter = 100.0;
	static constexpr double MetersPerCm = 0.01;

	// --- positions / velocities (table-local) -------------------------------------------------------

	static FVector PositionToUE(const rb::Vec3& P) { return FVector(CmPerMeter * P.x, -CmPerMeter * P.y, CmPerMeter * P.z); }
	static rb::Vec3 PositionToCore(const FVector& P) { return rb::Vec3(MetersPerCm * P.X, -MetersPerCm * P.Y, MetersPerCm * P.Z); }

	static FVector VelocityToUE(const rb::Vec3& V) { return PositionToUE(V); }
	static rb::Vec3 VelocityToCore(const FVector& V) { return PositionToCore(V); }

	// Plan-view point on the cloth (z = 0 unless given).
	static FVector PlanToUE(const rb::Vec2& P, double ZMeters = 0.0) { return PositionToUE(rb::Vec3(P.x, P.y, ZMeters)); }
	static rb::Vec2 PlanToCore(const FVector& P) { return rb::Vec2(MetersPerCm * P.X, -MetersPerCm * P.Y); }

	// --- unit directions ----------------------------------------------------------------------------

	static FVector DirectionToUE(const rb::Vec3& D) { return FVector(D.x, -D.y, D.z); }
	static rb::Vec3 DirectionToCore(const FVector& D) { return rb::Vec3(D.X, -D.Y, D.Z); }

	// --- angular velocity (pseudovector) ------------------------------------------------------------

	static FVector AngularVelocityToUE(const rb::Vec3& W) { return FVector(-W.x, W.y, -W.z); }
	static rb::Vec3 AngularVelocityToCore(const FVector& W) { return rb::Vec3(-W.X, W.Y, -W.Z); }

	// --- orientation --------------------------------------------------------------------------------

	static FQuat OrientationToUE(const rb::Quat& Q) { return FQuat(-Q.x, Q.y, -Q.z, Q.w); } // FQuat(X, Y, Z, W)
	static rb::Quat OrientationToCore(const FQuat& Q) { return rb::Quat(Q.W, -Q.X, Q.Y, -Q.Z); }

	// --- stroke direction ---------------------------------------------------------------------------

	// Core azimuth phi [rad] of a TABLE-LOCAL UE direction (only its plan projection matters).
	static double AzimuthFromUEDirection(const FVector& LocalDirection) { return FMath::Atan2(-LocalDirection.Y, LocalDirection.X); }

	// Cue axis butt -> tip in table-local UE axes for azimuth phi and elevation theta (butt raised, theta >= 0):
	// core d = (cos th cos ph, cos th sin ph, -sin th) (physics-motion-and-cue B.2) mirrored.
	static FVector CueDirectionToUE(double Azimuth, double Elevation)
	{
		const double CT = FMath::Cos(Elevation);
		return DirectionToUE(rb::Vec3(CT * FMath::Cos(Azimuth), CT * FMath::Sin(Azimuth), -FMath::Sin(Elevation)));
	}
};
