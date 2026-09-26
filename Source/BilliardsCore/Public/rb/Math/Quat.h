#pragma once

// Unit quaternion for ball orientation (playback; in the physics only for the chalk-mark cling of
// human-factors 4.3, PhysicsParams::ChalkCling).
// Owner: WP-7 (output & playback). Header-only.
//
// Convention: Hamilton product, active rotation of vectors in the CORE (right-handed) world frame,
// q = (w, x, y, z) = (cos(a/2), sin(a/2) * axis). Integration uses world-frame angular velocity with
// LEFT multiplication: q(t + dt) = Exp(0.5 * omega * dt) (x) q(t)   (ue5-realism-plan 5.7).
// The mirror to Unreal (q_UE = (qw, -qx, qy, -qz)) is done in the UE module, never here.

#include "rb/Config.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"

namespace rb
{
	struct Quat
	{
		double w = 1.0;
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;

		constexpr Quat() = default;
		constexpr Quat(double InW, double InX, double InY, double InZ) : w(InW), x(InX), y(InY), z(InZ) {}

		static constexpr Quat Identity() { return {}; }
		constexpr bool operator==(const Quat& o) const = default;
	};

	constexpr Quat operator*(const Quat& a, const Quat& b)
	{
		return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
	}

	constexpr Quat Conjugate(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
	constexpr double NormSquared(const Quat& q) { return q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z; }

	inline Quat Normalized(const Quat& q)
	{
		const double N = Sqrt(NormSquared(q));
		return N > 0.0 ? Quat{q.w / N, q.x / N, q.y / N, q.z / N} : Quat{};
	}

	// Rotation by Angle [rad] about the unit vector Axis.
	inline Quat FromAxisAngle(const Vec3& Axis, double Angle)
	{
		const double S = Sin(0.5 * Angle);
		return {Cos(0.5 * Angle), Axis.x * S, Axis.y * S, Axis.z * S};
	}

	// Exp(0.5 * RotationVector): rotation by |RotationVector| [rad] about its direction.
	inline Quat FromRotationVector(const Vec3& RotationVector)
	{
		const double Angle = Length(RotationVector);
		if (Angle < 1e-300)
		{
			return {};
		}
		return FromAxisAngle(RotationVector / Angle, Angle);
	}

	// Active rotation of v by the unit quaternion q.
	constexpr Vec3 Rotate(const Quat& q, const Vec3& v)
	{
		const Vec3 U{q.x, q.y, q.z};
		const Vec3 T = Cross(U, v) * 2.0;
		return v + T * q.w + Cross(U, T);
	}

	// Exact update for a CONSTANT world-frame angular velocity Omega [rad/s] over Dt [s]:
	// q' = Exp(0.5 * Omega * Dt) (x) q. Renormalised.
	inline Quat IntegrateConstantOmega(const Quat& q, const Vec3& Omega, double Dt)
	{
		return Normalized(FromRotationVector(Omega * Dt) * q);
	}
}
