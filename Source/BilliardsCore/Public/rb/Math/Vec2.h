#pragma once

// Plan-view (table plane) 2D vector, metres. Owner: WP-0 (architecture, frozen). Header-only.

#include "rb/Config.h"
#include "rb/Math/Vec3.h"

#include <cmath>

namespace rb
{
	struct Vec2
	{
		double x = 0.0;
		double y = 0.0;

		constexpr Vec2() = default;
		constexpr Vec2(double InX, double InY) : x(InX), y(InY) {}

		constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
		constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
		constexpr Vec2 operator-() const { return {-x, -y}; }
		constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
		constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
		constexpr Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
		constexpr Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
		constexpr bool operator==(const Vec2& o) const = default;
	};

	constexpr Vec2 operator*(double s, const Vec2& v) { return v * s; }
	constexpr double Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

	// z component of the 3D cross product (a, 0) x (b, 0).
	constexpr double Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

	constexpr double LengthSquared(const Vec2& v) { return Dot(v, v); }
	inline double Length(const Vec2& v) { return std::sqrt(LengthSquared(v)); }

	inline Vec2 Normalized(const Vec2& v, double Epsilon = 1e-15)
	{
		const double Len = Length(v);
		return Len > Epsilon ? v / Len : Vec2{};
	}

	// Counter-clockwise perpendicular (rotate +90 deg about +z).
	constexpr Vec2 PerpCcw(const Vec2& v) { return {-v.y, v.x}; }

	constexpr Vec2 XY(const Vec3& v) { return {v.x, v.y}; }
	constexpr Vec3 ToVec3(const Vec2& v, double z = 0.0) { return {v.x, v.y, z}; }
}
