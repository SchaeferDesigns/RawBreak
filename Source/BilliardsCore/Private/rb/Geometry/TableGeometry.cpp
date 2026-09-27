#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 3, 4.1, 5.3, 12.8; physics-collisions 4.1, 4.9, 5.1-5.3, 6.2;
// ue5-realism-plan 6.7 (the same data drives the render meshes).
#include "rb/Geometry/TableGeometry.h"

#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Math/Scalar.h"

#include <initializer_list>

namespace rb
{
	namespace
	{
		constexpr double kSqrt2 = 1.4142135623730950488;
		constexpr double kInvSqrt2 = 0.70710678118654752440;

		// Rubber face of the art profile: from the nose down and back to the cloth at 0.4 CushionWidth behind
		// the nose line (20 mm for the 2 in cushion; ESTIMATE, equipment 4.2). Every ball of the game (incl. the
		// 52.5 mm snooker ball: tangent at 15 mm) touches only the nose.
		constexpr double kProfileFaceBaseFraction = 0.4;

		// +0 for -0 (the mirror products produce signed zeros; values compare equal, exports stay tidy).
		double Tidy(double X) { return X + 0.0; }
		Vec2 Tidy(const Vec2& V) { return {Tidy(V.x), Tidy(V.y)}; }
		Vec3 Tidy(const Vec3& V) { return {Tidy(V.x), Tidy(V.y), Tidy(V.z)}; }

		double WrapTwoPi(double Angle) { return Angle - kTwoPi * Floor(Angle / kTwoPi); }

		// Plan angle of a direction in (-pi, pi]: atan2 with a -0 y component turned into +0 (atan2(-0, x < 0) = -pi;
		// directions built from sin/cos of exact angles such as a 90 deg cut carry -0 components).
		double PlanAngle(const Vec2& V) { return Atan2(Tidy(V.y), V.x); }

		// ---------------------------------------------------------------------------------------------
		// Ids and mirrors (equipment 12.8: one corner and one side pocket, mirrored). The canonical quadrant
		// is x >= 0, y >= 0: corner P3 (FootLeft), side P4 (SideLeft, its +x half), cushions C3 and C2.
		// ---------------------------------------------------------------------------------------------
		struct PocketFrame
		{
			PocketKind Kind = PocketKind::Corner;
			double Sx = 1.0; // corner: sign of x of the corner point; side: unused
			double Sy = 1.0; // sign of y of the rail
		};

		constexpr PocketFrame kPocketFrames[kPocketCount] = {
			{PocketKind::Corner, -1.0, -1.0}, // P0 HeadRight
			{PocketKind::Side, 1.0, -1.0},    // P1 SideRight
			{PocketKind::Corner, 1.0, -1.0},  // P2 FootRight
			{PocketKind::Corner, 1.0, 1.0},   // P3 FootLeft (constructed)
			{PocketKind::Side, 1.0, 1.0},     // P4 SideLeft (constructed)
			{PocketKind::Corner, -1.0, 1.0},  // P5 HeadLeft
		};

		// Cushion directions in counter-clockwise table order (Ck runs from Pk to Pk+1); inward normal = +90 deg.
		constexpr Vec2 kNoseDirection[kCushionCount] = {{1.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {-1.0, 0.0}, {-1.0, 0.0}, {0.0, -1.0}};

		constexpr CushionId kMirrorXCushion[kCushionCount] = {CushionId::RightFoot, CushionId::RightHead, CushionId::Head, CushionId::LeftHead,
			CushionId::LeftFoot, CushionId::Foot};
		constexpr CushionId kMirrorYCushion[kCushionCount] = {CushionId::LeftHead, CushionId::LeftFoot, CushionId::Foot, CushionId::RightFoot,
			CushionId::RightHead, CushionId::Head};
		constexpr PocketId kMirrorXPocket[kPocketCount] = {PocketId::FootRight, PocketId::SideRight, PocketId::HeadRight, PocketId::HeadLeft,
			PocketId::SideLeft, PocketId::FootLeft};
		constexpr PocketId kMirrorYPocket[kPocketCount] = {PocketId::HeadLeft, PocketId::SideLeft, PocketId::FootLeft, PocketId::FootRight,
			PocketId::SideRight, PocketId::HeadRight};

		CushionId MirrorCushion(CushionId C, double Sx, double Sy, bool HasPockets)
		{
			if (C == CushionId::None)
			{
				return C;
			}
			CushionId Out = C;
			if (Sx < 0.0)
			{
				Out = kMirrorXCushion[static_cast<int>(Out)];
			}
			if (Sy < 0.0)
			{
				Out = kMirrorYCushion[static_cast<int>(Out)];
			}
			if (!HasPockets) // pocketless mapping (Ids.h): C0 = whole right rail, C3 = whole left rail
			{
				Out = Out == CushionId::RightFoot ? CushionId::RightHead : (Out == CushionId::LeftHead ? CushionId::LeftFoot : Out);
			}
			return Out;
		}

		PocketId MirrorPocket(PocketId P, double Sx, double Sy)
		{
			if (P == PocketId::None)
			{
				return P;
			}
			PocketId Out = P;
			if (Sx < 0.0)
			{
				Out = kMirrorXPocket[static_cast<int>(Out)];
			}
			if (Sy < 0.0)
			{
				Out = kMirrorYPocket[static_cast<int>(Out)];
			}
			return Out;
		}

		// ---------------------------------------------------------------------------------------------
		// Jaws (equipment 5.3): rounded jaw arc + facing, built from the virtual point and the two directions.
		// ---------------------------------------------------------------------------------------------
		struct JawFrame
		{
			Vec2 VirtualPoint; // WPA jaw point (nose line x facing line)
			Vec2 NoseAway;     // unit, along the nose line away from the pocket
			Vec2 FacingDir;    // unit, along the facing plan line from the jaw point into the rail
			Vec2 NoseInward;   // unit, inward normal of the nose line (into the playing area)
			Vec2 RailNormal;   // unit, horizontal, from the nose line into the rail (axis-aligned)
		};

		bool BuildJaw(const JawFrame& J, PocketId Pocket, JawSide Side, double CutAngle, double JawRadius, const TableSpec& Spec, JawArc& Arc, Facing& Face)
		{
			const double Half = 0.5 * CutAngle;
			const double SinHalf = Sin(Half);
			const double Depth = Dot(J.FacingDir, J.RailNormal); // rail depth gained per unit facing length (sin phi / cos beta)
			if (!(SinHalf > 0.0) || !(Depth > 0.0) || !(JawRadius >= 0.0))
			{
				return false;
			}
			const double TangentOffset = JawRadius * Cos(Half) / SinHalf; // r_j / tan(C/2)
			const double CenterOffset = JawRadius / SinHalf;              // r_j / sin(C/2)
			const double FacingToBack = Spec.CushionWidth / Depth;        // virtual point -> cushion back
			if (!(FacingToBack > TangentOffset))
			{
				return false;
			}

			// Horizontal facing normal pointing away from the cushion material, i.e. toward the other jaw.
			Vec2 PocketNormal = PerpCcw(J.FacingDir);
			if (Dot(PocketNormal, J.NoseAway) > 0.0)
			{
				PocketNormal = -PocketNormal;
			}

			Arc.Pocket = Pocket;
			Arc.Side = Side;
			Arc.VirtualPoint = J.VirtualPoint;
			Arc.Radius = JawRadius;
			Arc.Height = Spec.CushionNoseHeight;
			Arc.Center = J.VirtualPoint + Normalized(J.NoseAway + J.FacingDir) * CenterOffset; // on the bisector, in the material
			Arc.TangentOnNose = J.VirtualPoint + J.NoseAway * TangentOffset;
			Arc.TangentOnFacing = J.VirtualPoint + J.FacingDir * TangentOffset;
			// Exposed arc: from Center, the tangent points lie along the outward normals of the two lines (NoseInward,
			// PocketNormal), pi - C apart.
			const double AngleNose = PlanAngle(J.NoseInward);
			const double AngleFacing = PlanAngle(PocketNormal);
			Arc.AngleFrom = Cross(J.NoseInward, PocketNormal) > 0.0 ? AngleNose : AngleFacing;
			Arc.AngleSweep = kPi - CutAngle;

			Face.Pocket = Pocket;
			Face.Side = Side;
			Face.Start = Arc.TangentOnFacing;
			Face.End = J.VirtualPoint + J.FacingDir * FacingToBack;
			// Exactly on the cushion-back line (VirtualPoint + CushionWidth RailNormal along the rail normal), so the
			// rail-top polygons that meet there share their vertices bitwise.
			if (J.RailNormal.x != 0.0)
			{
				Face.End.x = J.VirtualPoint.x + J.RailNormal.x * Spec.CushionWidth;
			}
			else
			{
				Face.End.y = J.VirtualPoint.y + J.RailNormal.y * Spec.CushionWidth;
			}
			Face.Direction = J.FacingDir;
			Face.Length = FacingToBack - TangentOffset;
			Face.LengthFromVirtualPoint = FacingToBack;
			Face.PocketNormal = Tidy(PocketNormal);
			Face.TopHeight = Spec.CushionNoseHeight;
			Face.Backdraft = Spec.Backdraft;
			Face.Thickness = Spec.FacingThickness;
			return true;
		}

		// Front (table-side) arc of the capture circle (Center, Radius): the arc through the circle's point
		// nearest the table (FrontAngle) that stays inside the pocket opening, i.e. on the pocket side of both
		// facing lines and in front of the rail faces (the cushion-back lines). Walking from the front point in
		// each direction, the arc ends at the first crossing of any of these lines (a facing line on most presets;
		// a hole whose wall reaches the cushion back before the facing, e.g. TABLE_7FT_78's side pockets, ends at
		// a cushion-back line instead). Returns false if the front point itself is not inside the opening or if the
		// whole circle is (no back wall under the rail).
		struct HalfPlane
		{
			Vec2 Point;  // on the line
			Vec2 Inward; // unit normal toward the allowed side
		};

		// Extent of the front arc about the front angle: it runs from FrontAngle - Cw to FrontAngle + Ccw; From is the
		// start angle as atan2 returns it.
		struct ArcExtent
		{
			double Cw = 0.0;
			double Ccw = 0.0;
			double From = 0.0;
		};

		bool FrontArc(const Vec2& Center, double Radius, double FrontAngle, const HalfPlane* Planes, int Count, ArcExtent& Out)
		{
			const Vec2 Front = Center + Vec2{Cos(FrontAngle), Sin(FrontAngle)} * Radius;
			double BestCcw = kTwoPi;
			double BestCw = kTwoPi;
			double FromAngle = FrontAngle;
			for (int i = 0; i < Count; ++i)
			{
				const HalfPlane& H = Planes[i];
				if (Dot(Front - H.Point, H.Inward) < 0.0)
				{
					return false;
				}
				const double Offset = Dot(Center - H.Point, H.Inward); // signed distance of the center
				if (!(Abs(Offset) < Radius))
				{
					continue; // the whole circle is on one side (the allowed one: it contains the front point)
				}
				const double HalfChord = Sqrt((Radius - Offset) * (Radius + Offset));
				const Vec2 Foot = Center - H.Inward * Offset;
				const Vec2 Along = PerpCcw(H.Inward);
				for (const double Sign : {-1.0, 1.0})
				{
					const Vec2 X = Foot + Along * (Sign * HalfChord) - Center;
					const double Angle = PlanAngle(X);
					const double Ccw = WrapTwoPi(Angle - FrontAngle);
					const double Cw = WrapTwoPi(FrontAngle - Angle);
					if (Ccw < BestCcw)
					{
						BestCcw = Ccw;
					}
					if (Cw < BestCw)
					{
						BestCw = Cw;
						FromAngle = Angle;
					}
				}
			}
			if (BestCcw >= kTwoPi || BestCw >= kTwoPi)
			{
				// The whole circle lies inside the opening: the hole never reaches under the rail, so there is no back
				// wall and the vertical rail faces behind the opening (not modelled) would be the pocket's back.
				return false;
			}
			Out.Cw = BestCw;
			Out.Ccw = BestCcw;
			Out.From = FromAngle;
			return true;
		}

		bool BuildPocket(const TableSpec& Spec, int Index, TableGeometry& Out)
		{
			const PocketFrame& Frame = kPocketFrames[Index];
			const PocketId Id = static_cast<PocketId>(Index);
			const bool Corner = Frame.Kind == PocketKind::Corner;
			const PocketSpec& P = Corner ? Spec.Corner : Spec.Side;
			const double Hl = Out.HalfLength;
			const double Hw = Out.HalfWidth;
			const double Sx = Frame.Sx;
			const double Sy = Frame.Sy;
			if (!(P.Mouth > 0.0) || !(P.CutAngle > 0.0) || !(P.CutAngle < kPi) || !(P.CaptureRadius > 0.0) || !(P.Shelf >= 0.0) ||
				!(Spec.DropPointRadius >= 0.0))
			{
				return false;
			}

			JawFrame Incoming;
			JawFrame Outgoing;
			Vec2 Axis;
			double Convergence = 0.0;
			double Throat = 0.0;
			if (Corner)
			{
				// POCKET_FOOT_LEFT construction (equipment 5.3) with the corner signs: a = M / sqrt 2 along each rail,
				// facings at phi = pi - C from the rail direction continuing into the pocket.
				const double A = P.Mouth * kInvSqrt2;
				const double Phi = kPi - P.CutAngle;
				const double CosPhi = Cos(Phi);
				const double SinPhi = Sin(Phi);
				const JawFrame End{{Sx * Hl, Sy * (Hw - A)}, {0.0, -Sy}, {Sx * SinPhi, Sy * CosPhi}, {-Sx, 0.0}, {Sx, 0.0}};
				const JawFrame Long{{Sx * (Hl - A), Sy * Hw}, {-Sx, 0.0}, {Sx * CosPhi, Sy * SinPhi}, {0.0, -Sy}, {0.0, Sy}};
				// Counter-clockwise: at P3 / P0 the end-rail jaw terminates the incoming cushion, a single mirror swaps.
				Incoming = Sx * Sy > 0.0 ? End : Long;
				Outgoing = Sx * Sy > 0.0 ? Long : End;
				Axis = {Sx * kInvSqrt2, Sy * kInvSqrt2};
				Convergence = P.CutAngle - 0.75 * kPi;
				Throat = CornerThroat(P.Mouth, P.CutAngle, kThroatMeasureDepth);
			}
			else
			{
				// POCKET_SIDE_LEFT construction (equipment 5.3): jaws at +-M/2, facings converging by beta = C - pi/2.
				const double M = 0.5 * P.Mouth;
				const double Beta = P.CutAngle - 0.5 * kPi;
				const double SinBeta = Sin(Beta);
				const double CosBeta = Cos(Beta);
				const JawFrame Plus{{M, Sy * Hw}, {1.0, 0.0}, {-SinBeta, Sy * CosBeta}, {0.0, -Sy}, {0.0, Sy}};
				const JawFrame Minus{{-M, Sy * Hw}, {-1.0, 0.0}, {SinBeta, Sy * CosBeta}, {0.0, -Sy}, {0.0, Sy}};
				Incoming = Sy > 0.0 ? Plus : Minus;
				Outgoing = Sy > 0.0 ? Minus : Plus;
				Axis = {0.0, Sy};
				Convergence = Beta;
				Throat = SideThroat(P.Mouth, P.CutAngle, kThroatMeasureDepth);
			}

			JawArc Arcs[2];
			Facing Faces[2];
			if (!BuildJaw(Incoming, Id, JawSide::Incoming, P.CutAngle, P.JawRadius, Spec, Arcs[0], Faces[0]) ||
				!BuildJaw(Outgoing, Id, JawSide::Outgoing, P.CutAngle, P.JawRadius, Spec, Arcs[1], Faces[1]))
			{
				return false;
			}

			PocketGeometry G;
			G.Id = Id;
			G.Kind = Frame.Kind;
			G.JawPoint[0] = Incoming.VirtualPoint;
			G.JawPoint[1] = Outgoing.VirtualPoint;
			G.MouthMid = Tidy((G.JawPoint[0] + G.JawPoint[1]) * 0.5);
			G.Axis = Axis;
			G.Mouth = P.Mouth;
			G.CutAngle = P.CutAngle;
			G.FacingAngle = kPi - P.CutAngle;
			G.Convergence = Convergence;
			G.Shelf = P.Shelf;
			G.Throat = Throat;
			G.CaptureCenter = Tidy(G.MouthMid + Axis * (P.Shelf + P.CaptureRadius));
			G.CaptureRadius = P.CaptureRadius;
			G.DropRadius = Spec.DropPointRadius;
			G.DropEdgeRadius = P.CaptureRadius + Spec.DropPointRadius;
			G.LinerUndercut = Spec.LinerUndercut;
			G.Backdraft = Spec.Backdraft;
			G.WallTopZ = Spec.RailTopZ;

			// The hole must not reach the cushion noses: both jaw points outside the drop-edge circle.
			if (!(Length(Incoming.VirtualPoint - G.CaptureCenter) > G.DropEdgeRadius) || !(Length(Outgoing.VirtualPoint - G.CaptureCenter) > G.DropEdgeRadius))
			{
				return false;
			}
			// Front (table-side) arc: "the capture circle's front arc between the facings" (collisions 5.3), i.e. where the
			// r_p circle (slate cut, liner) lies inside the pocket opening. It serves the rim / drop edge (a_d, same angles:
			// the a_d points outside it lie behind a facing plan line, which a ball on the shelf never reaches) and the hole
			// wall (below the rim only on this arc, up to WallTopZ elsewhere). Taking it from the larger a_d circle would
			// cut 4-9 deg off each end and leave a phantom back wall standing in the open mouth next to each facing.
			const HalfPlane Opening[4] = {
				{Incoming.VirtualPoint, Faces[0].PocketNormal},
				{Outgoing.VirtualPoint, Faces[1].PocketNormal},
				{Faces[0].End, -Incoming.RailNormal},
				{Faces[1].End, -Outgoing.RailNormal},
			};
			const double FrontAngle = PlanAngle(-Axis);
			ArcExtent Hole;
			ArcExtent Rim;
			if (!FrontArc(G.CaptureCenter, G.CaptureRadius, FrontAngle, Opening, 4, Hole) ||
				!FrontArc(G.CaptureCenter, G.DropEdgeRadius, FrontAngle, Opening, 4, Rim))
			{
				return false; // the front point lies behind a facing or the rail, or the hole never reaches under the rail
			}
			// On every preset the a_d circle meets the facings 4-9 deg inside the r_p crossings, so the arc is the r_p one.
			// A hole centred far behind a cushion-back line can let the rounding (a_d) reach the opening at angles where
			// the slate cut (r_p) is already under the rail; the arc is widened to cover them, so a ball rolling onto the
			// drop edge always finds it (the wall sector gained there lies under the rail, beside a rail face).
			const ArcExtent& Start = Rim.Cw > Hole.Cw ? Rim : Hole;
			G.FrontArcFrom = Start.From;
			G.FrontArcSweep = Max(Hole.Cw, Rim.Cw) + Max(Hole.Ccw, Rim.Ccw);

			Out.Pockets[Index] = G;
			Out.JawArcs[2 * Index] = Arcs[0];
			Out.JawArcs[2 * Index + 1] = Arcs[1];
			Out.Facings[2 * Index] = Faces[0];
			Out.Facings[2 * Index + 1] = Faces[1];
			return true;
		}

		// ---------------------------------------------------------------------------------------------
		// Rail-top polygons (collisions 6.2, A-GEO-1)
		// ---------------------------------------------------------------------------------------------
		RailTopPolygon MakePolygon(RailTopKind Kind, CushionId Cushion, PocketId Pocket, const Vec3& PlanePoint, const Vec3& PlaneNormal,
			std::initializer_list<Vec2> Vertices, std::initializer_list<RailEdgeKind> Edges)
		{
			RailTopPolygon Poly;
			Poly.Kind = Kind;
			Poly.Cushion = Cushion;
			Poly.Pocket = Pocket;
			Poly.PlanePoint = PlanePoint;
			Poly.PlaneNormal = PlaneNormal;
			int Count = 0;
			for (const Vec2& V : Vertices)
			{
				Poly.Vertices[Count] = V;
				++Count;
			}
			Poly.VertexCount = Count;
			int EdgeCount = 0;
			for (const RailEdgeKind E : Edges)
			{
				Poly.Edges[EdgeCount] = E;
				++EdgeCount;
			}
			return Poly;
		}

		// Open disc (C, R) intersects the closed convex CCW polygon.
		bool DiscIntersectsPolygon(const Vec2& C, double R, const RailTopPolygon& Poly)
		{
			bool Inside = true;
			for (int i = 0; i < Poly.VertexCount; ++i)
			{
				const Vec2 A = Poly.Vertices[i];
				const Vec2 Ab = Poly.Vertices[(i + 1) % Poly.VertexCount] - A;
				if (Cross(Ab, C - A) < 0.0)
				{
					Inside = false;
				}
				const double Len2 = LengthSquared(Ab);
				const double T = Len2 > 0.0 ? Clamp(Dot(C - A, Ab) / Len2, 0.0, 1.0) : 0.0;
				if (LengthSquared(C - (A + Ab * T)) < R * R)
				{
					return true;
				}
			}
			return Inside;
		}

		RailTopPolygon MirrorPolygon(const RailTopPolygon& In, double Sx, double Sy, bool HasPockets)
		{
			RailTopPolygon Out = In;
			const bool Reverse = Sx * Sy < 0.0; // a single mirror reverses the orientation: keep CCW
			const int N = In.VertexCount;
			for (int i = 0; i < N; ++i)
			{
				const Vec2& V = In.Vertices[Reverse ? N - 1 - i : i];
				Out.Vertices[i] = Tidy(Vec2{Sx * V.x, Sy * V.y});
				// Reversed: new edge i runs M(v[N-1-i]) -> M(v[N-2-i]) = old edge N-2-i.
				Out.Edges[i] = In.Edges[Reverse ? (2 * N - 2 - i) % N : i];
			}
			Out.PlanePoint = Tidy(Vec3{Sx * In.PlanePoint.x, Sy * In.PlanePoint.y, In.PlanePoint.z});
			Out.PlaneNormal = Tidy(Vec3{Sx * In.PlaneNormal.x, Sy * In.PlaneNormal.y, In.PlaneNormal.z});
			Out.CutCenter = Tidy(Vec2{Sx * In.CutCenter.x, Sy * In.CutCenter.y});
			Out.Cushion = MirrorCushion(In.Cushion, Sx, Sy, HasPockets);
			Out.Pocket = MirrorPocket(In.Pocket, Sx, Sy);
			return Out;
		}

		// The canonical quadrant x >= 0, y >= 0 of the rail top, then its three mirror images. Pocket tables (seven
		// convex pieces per quadrant, 28 in total):
		//   cushion tops (sloped plane from the nose line at h to the cushion back at RailTopZ): the left rail C3
		//   split at the midpoint x_s between the virtual jaw points of P4 and P3 (A: P4 half, B: P3 half), the foot
		//   rail C2 above y = 0 (C: P3 half); each ends at a facing plan line (edge kind Facing);
		//   rail cap (flat, z = RailTopZ, from the cushion back to the outer edge): the left strip split at x = 0
		//   and x_s (D: behind P4, E: toward P3), the corner square (F) and the foot strip above y = 0 (G).
		// Cushion-back edges that border a pocket opening instead of a cushion top are split off at the facing end
		// and marked Facing. Every piece that meets a pocket's cut disc (liner cylinder r_p through the rail) carries
		// that cut; a piece may meet one disc only (checked).
		bool BuildRailTops(const TableSpec& Spec, TableGeometry& Out)
		{
			const double Hl = Out.HalfLength;
			const double Hw = Out.HalfWidth;
			const double Cw = Spec.CushionWidth;
			const double Rw = Spec.RailWidthTotal;
			const double H = Spec.CushionNoseHeight;
			const double Zc = Spec.RailTopZ;
			const double Rise = Zc - H;
			const double HlC = Hl + Cw;
			const double HwC = Hw + Cw;
			const double HlR = Hl + Rw;
			const double HwR = Hw + Rw;
			const Vec3 LeftSlope = Normalized(Vec3{0.0, -Rise, Cw}); // up and toward the table
			const Vec3 FootSlope = Normalized(Vec3{-Rise, 0.0, Cw});
			const Vec3 Up{0.0, 0.0, 1.0};
			const Vec2 Q{HlC, HwC}; // cushion-back corner

			FixedVector<RailTopPolygon, 8> Quadrant;
			if (Spec.HasPockets)
			{
				const int Corner = static_cast<int>(PocketId::FootLeft);
				const int Side = static_cast<int>(PocketId::SideLeft);
				const JawArc& SideArc = Out.JawArcs[2 * Side + static_cast<int>(JawSide::Incoming)]; // the +x jaw of P4
				const Vec2 VpSide = SideArc.VirtualPoint;
				const Vec2 FSide = Out.Facings[2 * Side + static_cast<int>(JawSide::Incoming)].End;
				const Vec2 VpEnd = Out.JawArcs[2 * Corner + static_cast<int>(JawSide::Incoming)].VirtualPoint; // on the foot rail
				const Vec2 FEnd = Out.Facings[2 * Corner + static_cast<int>(JawSide::Incoming)].End;
				const Vec2 VpLong = Out.JawArcs[2 * Corner + static_cast<int>(JawSide::Outgoing)].VirtualPoint; // on the left rail
				const Vec2 FLong = Out.Facings[2 * Corner + static_cast<int>(JawSide::Outgoing)].End;
				const double Xs = 0.5 * (VpSide.x + VpLong.x);
				// Convexity / ordering of the construction (every WPA-like pocket satisfies it).
				if (!(VpSide.x < Xs && Xs < VpLong.x && FSide.x > 0.0 && FSide.x < Xs && FLong.x > Xs && FLong.x < HlC && VpEnd.y > 0.0 &&
						FEnd.y > 0.0 && FEnd.y < HwC))
				{
					return false;
				}
				const PocketId P3 = PocketId::FootLeft;
				const PocketId P4 = PocketId::SideLeft;
				const CushionId C3 = CushionId::LeftFoot;
				const CushionId C2 = CushionId::Foot;
				using E = RailEdgeKind;
				const RailTopKind Top = RailTopKind::CushionTop;
				const RailTopKind Cap = RailTopKind::RailCap;
				Quadrant.PushBack(MakePolygon(Top, C3, P4, {Xs, Hw, H}, LeftSlope, {VpSide, {Xs, Hw}, {Xs, HwC}, FSide},
					{E::Nose, E::Seam, E::CushionBack, E::Facing}));
				Quadrant.PushBack(MakePolygon(Top, C3, P3, {Xs, Hw, H}, LeftSlope, {{Xs, Hw}, VpLong, FLong, {Xs, HwC}},
					{E::Nose, E::Facing, E::CushionBack, E::Seam}));
				Quadrant.PushBack(MakePolygon(Top, C2, P3, {Hl, 0.0, H}, FootSlope, {{Hl, 0.0}, {HlC, 0.0}, FEnd, VpEnd},
					{E::Seam, E::CushionBack, E::Facing, E::Nose}));
				Quadrant.PushBack(MakePolygon(Cap, C3, P4, {0.0, HwC, Zc}, Up, {{0.0, HwC}, FSide, {Xs, HwC}, {Xs, HwR}, {0.0, HwR}},
					{E::Facing, E::CushionBack, E::Seam, E::OuterEdge, E::Seam}));
				Quadrant.PushBack(MakePolygon(Cap, C3, P3, {Xs, HwC, Zc}, Up, {{Xs, HwC}, FLong, Q, {HlC, HwR}, {Xs, HwR}},
					{E::CushionBack, E::Facing, E::Seam, E::OuterEdge, E::Seam}));
				Quadrant.PushBack(MakePolygon(Cap, CushionId::None, P3, {HlC, HwC, Zc}, Up, {Q, {HlR, HwC}, {HlR, HwR}, {HlC, HwR}},
					{E::Seam, E::OuterEdge, E::OuterEdge, E::Seam}));
				Quadrant.PushBack(MakePolygon(Cap, C2, P3, {HlC, 0.0, Zc}, Up, {{HlC, 0.0}, {HlR, 0.0}, {HlR, HwC}, Q, FEnd},
					{E::Seam, E::OuterEdge, E::Seam, E::Facing, E::CushionBack}));

				// Cut discs: a piece meeting the disc of its own pocket carries it; any other disc is an error.
				for (RailTopPolygon& Poly : Quadrant)
				{
					for (const PocketGeometry& Pocket : Out.Pockets)
					{
						if (!DiscIntersectsPolygon(Pocket.CaptureCenter, Pocket.CaptureRadius, Poly))
						{
							continue;
						}
						if (Pocket.Id != Poly.Pocket)
						{
							return false;
						}
						Poly.HasCut = true;
						Poly.CutCenter = Pocket.CaptureCenter;
						Poly.CutRadius = Pocket.CaptureRadius;
					}
				}
			}
			else
			{
				// Pocketless: the cushion tops meet in a mitred valley on the diagonal of the corner cushion square.
				using E = RailEdgeKind;
				const CushionId C3 = CushionId::LeftFoot;
				const CushionId C2 = CushionId::Foot;
				Quadrant.PushBack(MakePolygon(RailTopKind::CushionTop, C3, PocketId::None, {0.0, Hw, H}, LeftSlope, {{0.0, Hw}, {Hl, Hw}, Q, {0.0, HwC}},
					{E::Nose, E::Seam, E::CushionBack, E::Seam}));
				Quadrant.PushBack(MakePolygon(RailTopKind::CushionTop, C2, PocketId::None, {Hl, 0.0, H}, FootSlope, {{Hl, 0.0}, {HlC, 0.0}, Q, {Hl, Hw}},
					{E::Seam, E::CushionBack, E::Seam, E::Nose}));
				Quadrant.PushBack(MakePolygon(RailTopKind::RailCap, C3, PocketId::None, {0.0, HwC, Zc}, Up, {{0.0, HwC}, Q, {HlC, HwR}, {0.0, HwR}},
					{E::CushionBack, E::Seam, E::OuterEdge, E::Seam}));
				Quadrant.PushBack(MakePolygon(RailTopKind::RailCap, CushionId::None, PocketId::None, {HlC, HwC, Zc}, Up, {Q, {HlR, HwC}, {HlR, HwR}, {HlC, HwR}},
					{E::Seam, E::OuterEdge, E::OuterEdge, E::Seam}));
				Quadrant.PushBack(MakePolygon(RailTopKind::RailCap, C2, PocketId::None, {HlC, 0.0, Zc}, Up, {{HlC, 0.0}, {HlR, 0.0}, {HlR, HwC}, Q},
					{E::Seam, E::OuterEdge, E::Seam, E::CushionBack}));
			}

			constexpr double kSigns[4][2] = {{1.0, 1.0}, {-1.0, 1.0}, {-1.0, -1.0}, {1.0, -1.0}};
			for (const auto& Signs : kSigns)
			{
				for (const RailTopPolygon& Poly : Quadrant)
				{
					if (!Out.RailTops.PushBack(MirrorPolygon(Poly, Signs[0], Signs[1], Spec.HasPockets)))
					{
						return false;
					}
				}
			}
			return true;
		}

		// ---------------------------------------------------------------------------------------------
		// Sights (equipment 3.2), landmarks (3.1), profile (4.2)
		// ---------------------------------------------------------------------------------------------
		void BuildSights(const TableSpec& Spec, TableGeometry& Out)
		{
			const double Hl = Out.HalfLength;
			const double Hw = Out.HalfWidth;
			const double LongStep = 0.125 * Spec.Length;
			const double EndStep = 0.25 * Spec.Width;
			const auto Add = [&Out, &Spec](const Vec2& Rail, const Vec2& Nose, CushionId Cushion, int Index, bool OnLongRail) {
				Sight S;
				S.Position = ToVec3(Rail, Spec.RailTopZ);
				S.NoseLinePoint = Nose;
				S.NearestCushion = Cushion;
				S.Index = static_cast<std::uint8_t>(Index);
				S.OnLongRail = OnLongRail;
				Out.Sights.PushBack(S);
			};
			const int LongIndices[6] = {1, 2, 3, 5, 6, 7}; // 4 = the side pocket (no sight)
			const double LongY = Hw + Spec.SightInset;
			const double EndX = Hl + Spec.SightInset;
			// Counter-clockwise: right rail (head -> foot), foot rail, left rail (foot -> head), head rail.
			for (const int i : LongIndices)
			{
				const double X = static_cast<double>(i - 4) * LongStep; // -L/2 + i L/8, exactly antisymmetric
				Add({X, -LongY}, {X, -Hw}, (X < 0.0 || !Spec.HasPockets) ? CushionId::RightHead : CushionId::RightFoot, i, true);
			}
			for (int j = 1; j <= 3; ++j)
			{
				const double Y = static_cast<double>(j - 2) * EndStep; // -W/2 + j W/4
				Add({EndX, Y}, {Hl, Y}, CushionId::Foot, j, false);
			}
			for (int k = 5; k >= 0; --k)
			{
				const int i = LongIndices[k];
				const double X = static_cast<double>(i - 4) * LongStep;
				Add({X, LongY}, {X, Hw}, (X > 0.0 || !Spec.HasPockets) ? CushionId::LeftFoot : CushionId::LeftHead, i, true);
			}
			for (int j = 3; j >= 1; --j)
			{
				const double Y = static_cast<double>(j - 2) * EndStep;
				Add({-EndX, Y}, {-Hl, Y}, CushionId::Head, j, false);
			}
		}

		bool ValidSpec(const TableSpec& Spec)
		{
			const double Values[] = {Spec.Length, Spec.Width, Spec.CushionNoseHeight, Spec.CushionWidth, Spec.RailWidthTotal, Spec.RailTopZ,
				Spec.SightInset, Spec.Backdraft, Spec.DropPointRadius, Spec.FacingThickness, Spec.LinerUndercut, Spec.Corner.Mouth,
				Spec.Corner.CutAngle, Spec.Corner.Shelf, Spec.Corner.JawRadius, Spec.Corner.CaptureRadius, Spec.Side.Mouth, Spec.Side.CutAngle,
				Spec.Side.Shelf, Spec.Side.JawRadius, Spec.Side.CaptureRadius};
			for (const double V : Values)
			{
				if (!IsFinite(V))
				{
					return false;
				}
			}
			// The undercut facing and liner faces are tilted from vertical by less than 90 deg (a horizontal or flipped
			// face has no contact normal; s_f = (R - (h - R) sin beta_v) / cos beta_v would blow up).
			return Spec.Length > 0.0 && Spec.Width > 0.0 && Spec.CushionNoseHeight > 0.0 && Spec.CushionWidth > 0.0 &&
				Spec.RailWidthTotal > Spec.CushionWidth && Spec.RailTopZ > 0.0 && Abs(Spec.Backdraft) < 0.5 * kPi &&
				Abs(Spec.LinerUndercut) < 0.5 * kPi;
		}
	}

	ErrorCode BuildTableGeometry(const TableSpec& Spec, TableGeometry& Out)
	{
		Out = TableGeometry{};
		Out.Spec = Spec;
		if (!ValidSpec(Spec))
		{
			return ErrorCode::InvalidTable;
		}
		const double Hl = 0.5 * Spec.Length;
		const double Hw = 0.5 * Spec.Width;
		Out.HalfLength = Hl;
		Out.HalfWidth = Hw;

		// Landmarks (equipment 3.1; the same expressions as rules::MakeRulesTable).
		TableLandmarks& Marks = Out.Landmarks;
		Marks.HeadStringX = -0.25 * Spec.Length;
		Marks.FootStringX = 0.25 * Spec.Length;
		Marks.CenterStringX = 0.0;
		Marks.LongStringY = 0.0;
		Marks.BaulkX = -0.5 * Spec.Length + 0.2 * Spec.Length;
		Marks.HeadSpot = {-0.25 * Spec.Length, 0.0};
		Marks.FootSpot = {0.25 * Spec.Length, 0.0};
		Marks.CenterSpot = {0.0, 0.0};
		Marks.DiamondSpacing = 0.125 * Spec.Length;

		Out.PlayingArea = {{-Hl, -Hw}, {Hl, Hw}};
		Out.OuterBoundary = {{-(Hl + Spec.RailWidthTotal), -(Hw + Spec.RailWidthTotal)}, {Hl + Spec.RailWidthTotal, Hw + Spec.RailWidthTotal}};

		// Art profile in (d, z) (equipment 4.2): rubber face bottom, nose, cushion back, outer rail edge.
		Out.Profile.Points.PushBack({kProfileFaceBaseFraction * Spec.CushionWidth, 0.0});
		Out.Profile.Points.PushBack({0.0, Spec.CushionNoseHeight});
		Out.Profile.Points.PushBack({Spec.CushionWidth, Spec.RailTopZ});
		Out.Profile.Points.PushBack({Spec.RailWidthTotal, Spec.RailTopZ});
		Out.Profile.NoseIndex = 1;
		Out.Profile.CushionBackIndex = 2;

		BuildSights(Spec, Out);

		Out.Noses.Resize(kCushionCount);
		if (Spec.HasPockets)
		{
			Out.Pockets.Resize(kPocketCount);
			Out.JawArcs.Resize(kMaxJaws);
			Out.Facings.Resize(kMaxJaws);
			for (int k = 0; k < kPocketCount; ++k)
			{
				if (!BuildPocket(Spec, k, Out))
				{
					Out = TableGeometry{};
					Out.Spec = Spec;
					return ErrorCode::InvalidTable;
				}
			}
			for (int k = 0; k < kCushionCount; ++k)
			{
				NoseSegment& Nose = Out.Noses[k];
				Nose.Present = true;
				Nose.Cushion = static_cast<CushionId>(k);
				Nose.Start = Out.JawArcs[2 * k + static_cast<int>(JawSide::Outgoing)].TangentOnNose;
				Nose.End = Out.JawArcs[2 * ((k + 1) % kPocketCount) + static_cast<int>(JawSide::Incoming)].TangentOnNose;
				Nose.Direction = kNoseDirection[k];
				Nose.InwardNormal = Tidy(PerpCcw(kNoseDirection[k]));
				Nose.Length = Dot(Nose.End - Nose.Start, Nose.Direction);
				Nose.Height = Spec.CushionNoseHeight;
				if (!(Nose.Length > 0.0))
				{
					Out = TableGeometry{};
					Out.Spec = Spec;
					return ErrorCode::InvalidTable;
				}
			}
		}
		else
		{
			// Pocketless (Ids.h): C0 whole right rail, C2 foot, C3 whole left rail, C5 head; C1 / C4 absent.
			const Vec2 Corners[4] = {{-Hl, -Hw}, {Hl, -Hw}, {Hl, Hw}, {-Hl, Hw}};
			const int Present[4] = {0, 2, 3, 5};
			for (int k = 0; k < kCushionCount; ++k)
			{
				NoseSegment& Nose = Out.Noses[k];
				Nose.Present = false;
				Nose.Cushion = static_cast<CushionId>(k);
				Nose.Direction = kNoseDirection[k];
				Nose.InwardNormal = Tidy(PerpCcw(kNoseDirection[k]));
				Nose.Height = Spec.CushionNoseHeight;
			}
			for (int s = 0; s < 4; ++s)
			{
				NoseSegment& Nose = Out.Noses[Present[s]];
				Nose.Present = true;
				Nose.Start = Corners[s];
				Nose.End = Corners[(s + 1) % 4];
				Nose.Length = Dot(Nose.End - Nose.Start, Nose.Direction);
			}
		}

		if (!BuildRailTops(Spec, Out))
		{
			Out = TableGeometry{};
			Out.Spec = Spec;
			return ErrorCode::InvalidTable;
		}
		return ErrorCode::Ok;
	}

	CushionContactGeometry ComputeCushionContact(double BallRadius, double NoseHeight, double NoseProfileRadius, bool PooltoolCompat)
	{
		CushionContactGeometry G;
		const double Reach = BallRadius + NoseProfileRadius; // center to the profile circle's center at contact
		if (!IsFinite(BallRadius) || !IsFinite(NoseHeight) || !IsFinite(NoseProfileRadius) || !(Reach > 0.0))
		{
			return G; // neutral default: no NaN / Inf leaks into a contact frame
		}
		const double Rise = NoseHeight - BallRadius; // nose above the ball's center
		G.SinTheta = Clamp(Rise / Reach, -1.0, 1.0);
		G.Theta = Asin(G.SinTheta);
		// sqrt((R + r_n)^2 - (h - R)^2), factored for accuracy.
		const double Offset = Sqrt(Max(0.0, (Reach - Rise) * (Reach + Rise)));
		G.CosTheta = Offset / Reach;
		G.HorizontalOffset = PooltoolCompat ? BallRadius : Offset;
		return G;
	}

	double FacingContactOffset(double BallRadius, double NoseHeight, double Backdraft)
	{
		return (BallRadius - (NoseHeight - BallRadius) * Sin(Backdraft)) / Cos(Backdraft);
	}

	double CornerThroat(double Mouth, double CutAngle, double Depth)
	{
		const double Phi = kPi - CutAngle;
		return Mouth - kSqrt2 * Depth * (Cos(Phi) / Sin(Phi) - 1.0);
	}

	double SideThroat(double Mouth, double CutAngle, double Depth)
	{
		const double Beta = CutAngle - 0.5 * kPi;
		return Mouth - 2.0 * Depth * (Sin(Beta) / Cos(Beta));
	}

	bool IsOverPocketOpening(const TableGeometry& Geometry, const Vec2& P)
	{
		for (const PocketGeometry& Pocket : Geometry.Pockets)
		{
			// Inside the drop-edge circle a_d (the slate is gone or rounded off).
			if (LengthSquared(P - Pocket.CaptureCenter) < Pocket.DropEdgeRadius * Pocket.DropEdgeRadius)
			{
				return true;
			}
			// Beyond the mouth line, between the two virtual jaw points (same predicate as rules::OverPocketOpening).
			const Vec2 J0 = Pocket.JawPoint[0];
			const Vec2 Mouth = Pocket.JawPoint[1] - J0;
			const double MouthLen2 = LengthSquared(Mouth);
			if (MouthLen2 > 0.0 && Dot(P - J0, Pocket.Axis) > 0.0)
			{
				const double T = Dot(P - J0, Mouth) / MouthLen2;
				if (T >= 0.0 && T <= 1.0)
				{
					return true;
				}
			}
		}
		return false;
	}

	int BuildNoseOutline(const TableGeometry& Geometry, int SamplesPerArc, Vec2* Out, int Capacity)
	{
		if (Geometry.Noses.Size() != kCushionCount)
		{
			return 0; // not built
		}
		if (Geometry.Pockets.IsEmpty())
		{
			const double Hl = Geometry.HalfLength;
			const double Hw = Geometry.HalfWidth;
			if (Out == nullptr || Capacity < 4)
			{
				return -1;
			}
			Out[0] = {-Hl, -Hw};
			Out[1] = {Hl, -Hw};
			Out[2] = {Hl, Hw};
			Out[3] = {-Hl, Hw};
			return 4;
		}

		const int PerArc = SamplesPerArc < 2 ? 2 : SamplesPerArc;
		// 64-bit: 12 arcs x a huge SamplesPerArc must not wrap around and pass the capacity check.
		std::int64_t Needed = 0;
		for (const JawArc& Arc : Geometry.JawArcs)
		{
			Needed += (Arc.Radius > 0.0 ? static_cast<std::int64_t>(PerArc) : 1) + 1; // arc samples + the facing end
		}
		if (Out == nullptr || static_cast<std::int64_t>(Capacity) < Needed)
		{
			return -1;
		}

		int Count = 0;
		// Samples the exposed arc from From (a tangent point) to To (the other one), both included.
		const auto EmitArc = [&Count, Out, PerArc](const JawArc& Arc, bool FromNose) {
			const Vec2 From = FromNose ? Arc.TangentOnNose : Arc.TangentOnFacing;
			const Vec2 To = FromNose ? Arc.TangentOnFacing : Arc.TangentOnNose;
			if (!(Arc.Radius > 0.0))
			{
				Out[Count++] = Arc.VirtualPoint;
				return;
			}
			// Direction of travel about the center: CCW if From sits at AngleFrom.
			const bool Ccw = Cross(From - Arc.Center, To - Arc.Center) > 0.0;
			const double Start = Ccw ? Arc.AngleFrom : Arc.AngleFrom + Arc.AngleSweep;
			const double Step = (Ccw ? Arc.AngleSweep : -Arc.AngleSweep) / static_cast<double>(PerArc - 1);
			Out[Count++] = From;
			for (int s = 1; s + 1 < PerArc; ++s)
			{
				const double Angle = Start + Step * static_cast<double>(s);
				Out[Count++] = Arc.Center + Vec2{Cos(Angle), Sin(Angle)} * Arc.Radius;
			}
			Out[Count++] = To;
		};
		// Counter-clockwise: per pocket the incoming jaw (nose -> facing), its facing end, the outgoing facing end,
		// the outgoing jaw (facing -> nose); the nose segments join the pockets.
		for (int k = 0; k < Geometry.Pockets.Size(); ++k)
		{
			EmitArc(Geometry.JawArcs[2 * k], true);
			Out[Count++] = Geometry.Facings[2 * k].End;
			Out[Count++] = Geometry.Facings[2 * k + 1].End;
			EmitArc(Geometry.JawArcs[2 * k + 1], false);
		}
		return Count;
	}
}
