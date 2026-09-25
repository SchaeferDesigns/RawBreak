#pragma once

#include "rb/Config.h"

#include <cmath>

namespace rb
{
	inline constexpr double kPi = 3.14159265358979323846;
	inline constexpr double kTwoPi = 2.0 * kPi;

	// Double-precision 3D vector in the table frame (x = length, y = width, z = up, metres).
	struct Vec3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;

		constexpr Vec3() = default;
		constexpr Vec3(double InX, double InY, double InZ) : x(InX), y(InY), z(InZ) {}

		constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
		constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
		constexpr Vec3 operator-() const { return {-x, -y, -z}; }
		constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
		constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
		constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		constexpr Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
		constexpr bool operator==(const Vec3& o) const = default;

		static constexpr Vec3 Zero() { return {0.0, 0.0, 0.0}; }
		static constexpr Vec3 UnitX() { return {1.0, 0.0, 0.0}; }
		static constexpr Vec3 UnitY() { return {0.0, 1.0, 0.0}; }
		static constexpr Vec3 UnitZ() { return {0.0, 0.0, 1.0}; }
	};

	constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }

	constexpr double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

	constexpr Vec3 Cross(const Vec3& a, const Vec3& b)
	{
		return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	}

	constexpr double LengthSquared(const Vec3& v) { return Dot(v, v); }
	inline double Length(const Vec3& v) { return std::sqrt(LengthSquared(v)); }

	// Returns the zero vector for (near) zero-length input instead of NaNs.
	inline Vec3 Normalized(const Vec3& v, double Epsilon = 1e-15)
	{
		const double Len = Length(v);
		return Len > Epsilon ? v / Len : Vec3::Zero();
	}

	// Projection onto the table plane (drops z).
	constexpr Vec3 Planar(const Vec3& v) { return {v.x, v.y, 0.0}; }
}
