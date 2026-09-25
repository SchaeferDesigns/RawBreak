#include "rb/Core/FpGuard.h"
// Owner: WP-5 (event detection). Spec: physics-collisions 3.1-3.6, 4.10, 5.3, 6.1-6.3; prior-art 5.5, 5.7, 5.10.
#include "rb/Physics/Detect.h"

namespace rb
{
	Polynomial BallBallGapPolynomial(const MotionSegment& /*A*/, double /*RadiusA*/, const MotionSegment& /*B*/, double /*RadiusB*/, double /*RefTime*/)
	{
		// TODO(WP-5): re-expand both segments to RefTime from their stored origins; a4..a0 of 3.2.
		return {};
	}

	ContactPrediction PredictBallBall(const MotionSegment& /*A*/, double /*RadiusA*/, const MotionSegment& /*B*/, double /*RadiusB*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): windowed first approaching root, touching / pressing / overlap rules (3.3-3.6).
		return {};
	}

	Aabb3 SweptBounds(const MotionSegment& Seg, double /*TauFrom*/, double /*TauTo*/)
	{
		// TODO(WP-5): analytic per-axis extremes of the parabola over the window.
		return {Seg.Pos0, Seg.Pos0};
	}

	ContactPrediction PredictNoseOnCloth(const MotionSegment& /*Seg*/, double /*Radius*/, const NoseSegment& /*Nose*/, double /*ContactOffset*/,
		double /*TimeLimit*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): quadratic sigma(tau) = R_c, along-segment test, touching/pressing at tau = 0 (4.10).
		return {};
	}

	ContactPrediction PredictNoseAirborne(const MotionSegment& /*Seg*/, double /*Radius*/, const NoseSegment& /*Nose*/, double /*NoseProfileRadius*/,
		double /*TimeLimit*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): quartic |Q (p - P_a)|^2 = (R + r_n)^2 with the table-side validity test (4.10).
		return {};
	}

	ContactPrediction PredictJawArcOnCloth(const MotionSegment& /*Seg*/, double /*Radius*/, const JawArc& /*Arc*/, double /*ContactOffset*/,
		double /*TimeLimit*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): quartic |p_h - O_h|^2 = (r_j + R_c)^2 within the arc's angular range (4.10).
		return {};
	}

	ContactPrediction PredictJawArcAirborne(const MotionSegment& /*Seg*/, double /*Radius*/, const JawArc& /*Arc*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): sphere approximation |p - O| = R + r_j (quartic) or exact degree 8.
		return {};
	}

	ContactPrediction PredictFacingOnShelf(const MotionSegment& /*Seg*/, double /*Radius*/, const Facing& /*Face*/, double /*ContactOffset*/,
		double /*TimeLimit*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): quadratic plan distance s_f to the facing line (5.3).
		return {};
	}

	ContactPrediction PredictFacingAirborne(const MotionSegment& /*Seg*/, double /*Radius*/, const Facing& /*Face*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): undercut plane distance = R (quadratic).
		return {};
	}

	ContactPrediction PredictFacingTopEdge(const MotionSegment& /*Seg*/, double /*Radius*/, const Facing& /*Face*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): airborne edge line contact (as the airborne nose).
		return {};
	}

	ContactPrediction PredictDropEdge(const MotionSegment& /*Seg*/, double /*Radius*/, const PocketGeometry& /*Pocket*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): horizontal distance to C_cap = a_d entering, on the front arc (5.3, pitfall 19).
		return {};
	}

	ContactPrediction PredictLinerWall(const MotionSegment& /*Seg*/, double /*Radius*/, const PocketGeometry& /*Pocket*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): r_p - R from inside moving outward; front arc only below z = -r_d, elsewhere up to WallTopZ.
		return {};
	}

	ContactPrediction PredictRimTorus(const MotionSegment& /*Seg*/, double /*Radius*/, const PocketGeometry& /*Pocket*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): degree-8 torus contact Q^2 - 4 a_d^2 rho_h^2 = 0 with Q > 0 (5.3).
		return {};
	}

	ContactPrediction PredictCaptureCircle(const MotionSegment& /*Seg*/, const PocketGeometry& /*Pocket*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): center enters the circle (CaptureCenter, CaptureRadius) (pooltool circle pockets, XREF-01).
		return {};
	}

	ContactPrediction PredictCaptureDepth(const MotionSegment& /*Seg*/, double /*Radius*/, double /*TimeLimit*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): z(tau) = -R.
		return {};
	}

	ContactPrediction PredictPocketExit(const MotionSegment& /*Seg*/, double /*Radius*/, const PocketGeometry& /*Pocket*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): center back outside a_d with z > R (rattle out).
		return {};
	}

	ContactPrediction PredictSlateLanding(const MotionSegment& /*Seg*/, double /*Radius*/, double /*TimeLimit*/)
	{
		// TODO(WP-5): z(tau) = R, later root (C.2), inside the window.
		return {};
	}

	ContactPrediction PredictRailTop(const MotionSegment& /*Seg*/, double /*Radius*/, const RailTopPolygon& /*Polygon*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): n . (p - x0) = R (quadratic), contact point over the polygon minus the cut disc (6.2).
		return {};
	}

	ContactPrediction PredictRailTopEdge(const MotionSegment& /*Seg*/, double /*Radius*/, const RailTopPolygon& /*Polygon*/, int /*Edge*/,
		double /*TimeLimit*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): straight convex edges as the airborne nose (quartic); cut rim as a circle (degree 8).
		return {};
	}

	ContactPrediction PredictSupportExit(const MotionSegment& /*Seg*/, const RailTopPolygon& /*Polygon*/, double /*TimeLimit*/, int& EdgeOut)
	{
		// TODO(WP-5): center leaves the polygon across an edge (quadratic per edge) or enters the cut disc (quartic).
		EdgeOut = -1;
		return {};
	}

	ContactPrediction PredictOuterBoundary(const MotionSegment& /*Seg*/, const Aabb2& /*Outer*/, double /*TimeLimit*/)
	{
		// TODO(WP-5): center crosses the outer rail boundary (6.3).
		return {};
	}

	ContactPrediction PredictLampApex(const MotionSegment& /*Seg*/, double /*Radius*/, const EnvironmentSpec& /*Environment*/, double /*Gravity*/,
		double /*TimeLimit*/)
	{
		// TODO(WP-5): analytic apex vs lamp underside and footprint (6.3).
		return {};
	}

	int PredictLineCrossings(const MotionSegment& /*Seg*/, const TableLandmarks& /*Landmarks*/, double /*TimeFrom*/, double /*TimeLimit*/, double /*Eps*/,
		bool /*IncludeFrom*/, LineCrossing* /*Out*/, int /*Capacity*/)
	{
		// TODO(WP-5): roots of x(t) - (x_line +- eps) / y(t) -+ eps in the window, sorted.
		return 0;
	}

	ContactPrediction PredictPlanDistanceCrossing(const MotionSegment& /*A*/, double /*RadiusA*/, const MotionSegment& /*B*/, double /*RadiusB*/,
		bool /*Entering*/, double /*TimeLimit*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): quartic of the horizontal center distance (jump-over observer, rules F9).
		return {};
	}

	FeaturePrediction PredictTableEvent(const MotionSegment& /*Seg*/, const BallSpec& /*Spec*/, const BallTableContext& /*Context*/,
		const TableGeometry& /*Table*/, const EnvironmentSpec& /*Environment*/, const DetectOptions& /*Options*/, double /*Gravity*/, double /*TimeLimit*/,
		const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-5): state- and position-dependent feature set (see Detect.h), broad phase, earliest with
		// (Kind, Index, SubIndex) tie order; never SlateLanding (end slot).
		return {};
	}

	int QueryTableFeatures(const Aabb3& /*Region*/, const TableGeometry& /*Table*/, const DetectOptions& /*Options*/, TableFeatureRef* /*Out*/,
		int /*Capacity*/, bool& Overflow)
	{
		// TODO(WP-5): bounds test of every table element against Region, (Kind, Index, SubIndex) order.
		Overflow = false;
		return 0;
	}

	FixedContact MakeFixedContact(const TableFeatureRef& /*Feature*/, const TableGeometry& /*Table*/, const BallState& /*Ball*/, const BallSpec& /*Spec*/,
		const DetectOptions& /*Options*/)
	{
		// TODO(WP-5): contact normal / into-feature direction / elevation for ResolveFixedContact.
		return {};
	}
}
