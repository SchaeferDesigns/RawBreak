#pragma once

// Owner: WP-2 (equipment & table geometry). Shared helpers for the Geometry / Equipment tests.

#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"

namespace rb::geotest
{
	inline constexpr TablePreset kAllPresets[] = {TablePreset::NineFootPro, TablePreset::NineFootTight, TablePreset::EightFootPro,
		TablePreset::EightFootHome, TablePreset::SevenFootBar, TablePreset::SevenFoot78, TablePreset::SevenFootTrue};

	inline bool Near(double A, double B, double Tol) { return Abs(A - B) <= Tol; }
	inline bool Near(const Vec2& A, const Vec2& B, double Tol) { return Near(A.x, B.x, Tol) && Near(A.y, B.y, Tol); }
	inline bool Near(const Vec3& A, const Vec3& B, double Tol) { return Near(A.x, B.x, Tol) && Near(A.y, B.y, Tol) && Near(A.z, B.z, Tol); }

	inline Vec2 Mirror(const Vec2& P, double Sx, double Sy) { return {Sx * P.x, Sy * P.y}; }

	// Signed distance of P from the directed line A -> B (positive on the left).
	inline double LineSide(const Vec2& A, const Vec2& B, const Vec2& P) { return Cross(B - A, P - A) / Length(B - A); }

	// Point in a convex CCW polygon with margin: Margin > 0 requires P at least Margin inside every edge,
	// Margin < 0 accepts points up to -Margin outside.
	inline bool InConvex(const Vec2* V, int N, const Vec2& P, double Margin)
	{
		for (int i = 0; i < N; ++i)
		{
			if (LineSide(V[i], V[(i + 1) % N], P) < Margin)
			{
				return false;
			}
		}
		return true;
	}

	// Rail-top surface over P (inside the polygon, outside the cut disc), with margin as InConvex.
	inline bool OverSurface(const RailTopPolygon& Poly, const Vec2& P, double Margin)
	{
		if (!InConvex(Poly.Vertices, Poly.VertexCount, P, Margin))
		{
			return false;
		}
		return !Poly.HasCut || Length(P - Poly.CutCenter) >= Poly.CutRadius + Margin;
	}

	// Pocket opening between the facings, from the mouth line back to the cushion-back line(s), as a convex CCW
	// polygon (corner: jaw points, facing ends and the cushion-back corner; side: jaw points and facing ends).
	inline int PocketOpening(const TableGeometry& G, int Pocket, Vec2* Out)
	{
		const PocketGeometry& P = G.Pockets[Pocket];
		Vec2 Pts[5];
		int N = 0;
		Pts[N++] = P.JawPoint[0];
		Pts[N++] = P.JawPoint[1];
		Pts[N++] = G.Facings[2 * Pocket].End;
		Pts[N++] = G.Facings[2 * Pocket + 1].End;
		if (P.Kind == PocketKind::Corner)
		{
			const double Sx = P.Axis.x > 0.0 ? 1.0 : -1.0;
			const double Sy = P.Axis.y > 0.0 ? 1.0 : -1.0;
			Pts[N++] = {Sx * (G.HalfLength + G.Spec.CushionWidth), Sy * (G.HalfWidth + G.Spec.CushionWidth)};
		}
		Vec2 C;
		for (int i = 0; i < N; ++i)
		{
			C += Pts[i] / static_cast<double>(N);
		}
		// Sort by angle about the centroid (CCW).
		double A[5];
		for (int i = 0; i < N; ++i)
		{
			A[i] = Atan2(Pts[i].y - C.y, Pts[i].x - C.x);
		}
		for (int i = 1; i < N; ++i)
		{
			for (int j = i; j > 0 && A[j - 1] > A[j]; --j)
			{
				const double Ta = A[j];
				A[j] = A[j - 1];
				A[j - 1] = Ta;
				const Vec2 Tp = Pts[j];
				Pts[j] = Pts[j - 1];
				Pts[j - 1] = Tp;
			}
		}
		for (int i = 0; i < N; ++i)
		{
			Out[i] = Pts[i];
		}
		return N;
	}

	inline bool ArcContainsAngle(double From, double Sweep, double Angle)
	{
		const double Rel = Angle - From - kTwoPi * Floor((Angle - From) / kTwoPi);
		return Rel <= Sweep;
	}
}
