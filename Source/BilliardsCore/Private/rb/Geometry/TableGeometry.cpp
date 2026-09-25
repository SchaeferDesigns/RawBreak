#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 3, 4.1, 5.3; physics-collisions 4.1, 5.1-5.3, 6.2.
#include "rb/Geometry/TableGeometry.h"

namespace rb
{
	ErrorCode BuildTableGeometry(const TableSpec& Spec, TableGeometry& Out)
	{
		// TODO(WP-2): noses (6 entries, pocketless C1/C4 absent), jaw arcs, facings, pockets (C_cap, a_d, front arc, WallTopZ),
		// rail-top polygons covering the whole rail top incl. pocket surrounds (edge kinds, cut discs), sights, landmarks,
		// profile, boundaries; one corner + one side pocket constructed and mirrored (equipment 12.8).
		Out = TableGeometry{};
		Out.Spec = Spec;
		return ErrorCode::NotImplemented;
	}

	CushionContactGeometry ComputeCushionContact(double /*BallRadius*/, double /*NoseHeight*/, double /*NoseProfileRadius*/, bool /*PooltoolCompat*/)
	{
		// TODO(WP-2): theta_c = asin((h - R)/(R + r_n)), R_c (equipment 4.1, collisions 4.10).
		return {};
	}

	double FacingContactOffset(double /*BallRadius*/, double /*NoseHeight*/, double /*Backdraft*/)
	{
		// TODO(WP-2): s_f = (R - (h - R) sin beta_v) / cos beta_v (collisions 5.3).
		return 0.0;
	}

	double CornerThroat(double /*Mouth*/, double /*CutAngle*/, double /*Depth*/)
	{
		// TODO(WP-2): M - sqrt(2) t (cot phi - 1) (equipment 5.3).
		return 0.0;
	}

	double SideThroat(double /*Mouth*/, double /*CutAngle*/, double /*Depth*/)
	{
		// TODO(WP-2): M - 2 t tan(beta) (equipment 5.3).
		return 0.0;
	}

	bool IsOverPocketOpening(const TableGeometry& /*Geometry*/, const Vec2& /*P*/)
	{
		// TODO(WP-2): beyond a mouth line or inside a drop-edge circle.
		return false;
	}

	int BuildNoseOutline(const TableGeometry& /*Geometry*/, int /*SamplesPerArc*/, Vec2* /*Out*/, int /*Capacity*/)
	{
		// TODO(WP-2): closed CCW nose outline for mesh generation.
		return 0;
	}
}
