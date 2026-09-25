#pragma once

// Axis-aligned boxes for broad-phase culling and table extents. Owner: WP-0 (frozen). Header-only.

#include "rb/Config.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"

namespace rb
{
	struct Aabb2
	{
		Vec2 Lo;
		Vec2 Hi;

		constexpr bool Contains(const Vec2& p) const { return p.x >= Lo.x && p.x <= Hi.x && p.y >= Lo.y && p.y <= Hi.y; }
		constexpr bool Overlaps(const Aabb2& o) const { return Lo.x <= o.Hi.x && o.Lo.x <= Hi.x && Lo.y <= o.Hi.y && o.Lo.y <= Hi.y; }
	};

	struct Aabb3
	{
		Vec3 Lo;
		Vec3 Hi;

		constexpr bool Overlaps(const Aabb3& o) const
		{
			return Lo.x <= o.Hi.x && o.Lo.x <= Hi.x && Lo.y <= o.Hi.y && o.Lo.y <= Hi.y && Lo.z <= o.Hi.z && o.Lo.z <= Hi.z;
		}

		constexpr Aabb3 Inflated(double Margin) const
		{
			return {{Lo.x - Margin, Lo.y - Margin, Lo.z - Margin}, {Hi.x + Margin, Hi.y + Margin, Hi.z + Margin}};
		}
	};
}
