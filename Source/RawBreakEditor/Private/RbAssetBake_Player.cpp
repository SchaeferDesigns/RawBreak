#include "RbAssetBakeLibrary.h"

#include "RawBreak.h"
#include "Core/RbAssetPaths.h"
#include "Player/RbBallInHandComponent.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

// Owner: M2-F (Docs/ue-architecture.md 18.3, P2). The procedural carrying hand and arm of the diegetic ball in hand: stand-ins until
// the M3 arms / hands (MetaHuman / mannequin) -> RbAssetPaths::HandCarryMesh (SM_RbHand_Carry) and RbHandMesh::ArmMeshPath
// (SM_RbArm_Carry), material M_RbHand (rb_make_player.py, which calls this after it made the material). Geometry contract:
// RbHandMesh (Player/RbBallInHandComponent.h).
//
// A right hand holding a pool ball from above, modelled around a 2 1/4 in ball (R0 = 2.8575 cm) whose centre is the mesh origin
// (URbBallInHandComponent places the mesh at the carried ball's centre and scales it by R / R0): +X = where the fingers point (away
// from the player), +Y = right, +Z = up. A fingertip claw: the palm hovers over the ball's top, four arched fingers rest their pads on
// the front half above the equator (far above the cloth when the ball touches it), the thumb on the back-left, so the ball stays
// visible from the player's eye; the wrist rises toward the player and the bare
// forearm runs 16 cm back-up at 30 deg into a rolled shirt cuff; the arm mesh is the sleeve (along +X, 1 m, stretched by the
// component from the cuff to the shoulder). Every part is a swept tube or an ellipsoid
// (overlapping closed shells, no booleans), smooth normals; the vertex colour is a part MASK (R = sleeve, G = nail, 0 / 1 only, so
// the static mesh build's sRGB byte encoding cannot shift it) that M_RbHand turns into skin / nail / sleeve surfaces;
// UV0 = (along the part [m], around / 2 pi). No collision, no Nanite (a small moving mesh).

namespace
{
	using UE::Geometry::FDynamicMesh3;
	using UE::Geometry::FIndex3i;

	constexpr double kR0 = RbHandMesh::ReferenceBallRadiusCm; // [cm] the ball the hand is modelled around
	constexpr int32 kSegments = 18;             // around every tube
	constexpr double kArmStartRadius = 3.3;     // [cm] the sleeve at the cuff joint ...
	constexpr double kArmEndRadius = 4.6;       // ... and at the shoulder
	const FVector4f kSkin(0.0f, 0.0f, 0.0f, 1.0f);   // part masks: R sleeve, G nail
	const FVector4f kNail(0.0f, 1.0f, 0.0f, 1.0f);
	const FVector4f kSleeve(1.0f, 0.0f, 0.0f, 1.0f);

	struct FMeshWriter
	{
		FDynamicMesh3& Mesh;
		UE::Geometry::FDynamicMeshNormalOverlay* Normals = nullptr;
		UE::Geometry::FDynamicMeshUVOverlay* UVs = nullptr;
		UE::Geometry::FDynamicMeshColorOverlay* Colors = nullptr;

		explicit FMeshWriter(FDynamicMesh3& InMesh) : Mesh(InMesh)
		{
			Mesh.EnableTriangleGroups(0);
			Mesh.EnableAttributes();
			Mesh.Attributes()->SetNumUVLayers(1);
			Mesh.Attributes()->EnablePrimaryColors();
			Normals = Mesh.Attributes()->PrimaryNormals();
			UVs = Mesh.Attributes()->PrimaryUV();
			Colors = Mesh.Attributes()->PrimaryColors();
		}

		struct FVert
		{
			int32 V, N, U, C;
		};

		FVert Add(const FVector& P, const FVector& Normal, const FVector2f& UV, const FVector4f& Color)
		{
			FVert Out;
			Out.V = Mesh.AppendVertex(FVector3d(P));
			Out.N = Normals->AppendElement(FVector3f(Normal.GetSafeNormal()));
			Out.U = UVs->AppendElement(UV);
			Out.C = Colors->AppendElement(Color);
			return Out;
		}

		void Tri(const FVert& A, const FVert& B, const FVert& C, int32 Group)
		{
			const int32 T = Mesh.AppendTriangle(A.V, B.V, C.V, Group);
			if (T < 0)
			{
				return;
			}
			Normals->SetTriangle(T, FIndex3i(A.N, B.N, C.N));
			UVs->SetTriangle(T, FIndex3i(A.U, B.U, C.U));
			Colors->SetTriangle(T, FIndex3i(A.C, B.C, C.C));
		}

		// Quads between consecutive rings (outward winding: UE's front faces are clockwise seen from outside).
		void Stitch(const TArray<TArray<FVert>>& Rings, int32 Group)
		{
			for (int32 I = 0; I + 1 < Rings.Num(); ++I)
			{
				for (int32 J = 0; J < kSegments; ++J)
				{
					const int32 K = (J + 1) % kSegments;
					Tri(Rings[I][J], Rings[I + 1][J], Rings[I][K], Group);
					Tri(Rings[I][K], Rings[I + 1][J], Rings[I + 1][K], Group);
				}
			}
		}
	};

	// A tube swept along Points with per-point Radii (parallel-transport frames), closed by rounded caps at both ends.
	void AddTube(FMeshWriter& W, const TArray<FVector>& Points, const TArray<double>& Radii, const FVector4f& Color, int32 Group,
		const FVector4f* TipColor = nullptr)
	{
		const int32 N = Points.Num();
		if (N < 2)
		{
			return;
		}
		// Tangents and a transported normal.
		TArray<FVector> T;
		for (int32 I = 0; I < N; ++I)
		{
			const FVector D = I == 0 ? Points[1] - Points[0] : (I == N - 1 ? Points[N - 1] - Points[N - 2] : Points[I + 1] - Points[I - 1]);
			T.Add(D.GetSafeNormal());
		}
		FVector Normal = FVector::CrossProduct(T[0], FMath::Abs(T[0].Z) < 0.9 ? FVector::UpVector : FVector::RightVector).GetSafeNormal();
		TArray<FVector> Ns;
		for (int32 I = 0; I < N; ++I)
		{
			if (I > 0)
			{
				const FQuat Turn = FQuat::FindBetweenNormals(T[I - 1], T[I]);
				Normal = Turn.RotateVector(Normal).GetSafeNormal();
			}
			Ns.Add(Normal);
		}
		// Rings: a cap of 4 rings at each end (quarter circle), the body in between.
		struct FRing
		{
			FVector Centre;
			FVector Tangent;
			FVector Normal;
			double Radius;
			double Along;
			double NormalTilt; // the ring normal tilts toward the tangent on the caps
		};
		TArray<FRing> RingSpecs;
		double Along = 0.0;
		constexpr int32 CapRings = 4;
		for (int32 C = CapRings; C >= 1; --C)
		{
			const double A = UE_DOUBLE_HALF_PI * C / (CapRings + 1);
			RingSpecs.Add({Points[0] - T[0] * Radii[0] * FMath::Sin(A), T[0], Ns[0], Radii[0] * FMath::Cos(A), 0.0, -A});
		}
		for (int32 I = 0; I < N; ++I)
		{
			if (I > 0)
			{
				Along += FVector::Dist(Points[I], Points[I - 1]);
			}
			RingSpecs.Add({Points[I], T[I], Ns[I], Radii[I], Along, 0.0});
		}
		for (int32 C = 1; C <= CapRings; ++C)
		{
			const double A = UE_DOUBLE_HALF_PI * C / (CapRings + 1);
			RingSpecs.Add({Points[N - 1] + T[N - 1] * Radii[N - 1] * FMath::Sin(A), T[N - 1], Ns[N - 1], Radii[N - 1] * FMath::Cos(A), Along, A});
		}
		TArray<TArray<FMeshWriter::FVert>> Rings;
		for (int32 R = 0; R < RingSpecs.Num(); ++R)
		{
			const FRing& Spec = RingSpecs[R];
			const FVector Binormal = FVector::CrossProduct(Spec.Tangent, Spec.Normal).GetSafeNormal();
			const bool bTip = TipColor && R >= RingSpecs.Num() - CapRings;
			TArray<FMeshWriter::FVert>& Ring = Rings.AddDefaulted_GetRef();
			for (int32 J = 0; J < kSegments; ++J)
			{
				const double Phi = UE_DOUBLE_TWO_PI * J / kSegments;
				const FVector Radial = Spec.Normal * FMath::Cos(Phi) + Binormal * FMath::Sin(Phi);
				const FVector P = Spec.Centre + Radial * Spec.Radius;
				const FVector Nrm = Radial * FMath::Cos(Spec.NormalTilt) + Spec.Tangent * FMath::Sin(Spec.NormalTilt);
				Ring.Add(W.Add(P, Nrm, FVector2f(static_cast<float>(0.01 * Spec.Along), static_cast<float>(J) / kSegments), bTip ? *TipColor : Color));
			}
		}
		W.Stitch(Rings, Group);
		// Close both ends with a fan to the pole.
		const FVector StartPole = Points[0] - T[0] * Radii[0];
		const FVector EndPole = Points[N - 1] + T[N - 1] * Radii[N - 1];
		const FMeshWriter::FVert A = W.Add(StartPole, -T[0], FVector2f(0.0f, 0.0f), Color);
		const FMeshWriter::FVert B = W.Add(EndPole, T[N - 1], FVector2f(static_cast<float>(0.01 * Along), 0.0f), TipColor ? *TipColor : Color);
		for (int32 J = 0; J < kSegments; ++J)
		{
			const int32 K = (J + 1) % kSegments;
			W.Tri(A, Rings[0][J], Rings[0][K], Group);
			W.Tri(B, Rings.Last()[K], Rings.Last()[J], Group);
		}
	}

	// An ellipsoid (UV sphere) with semi-axes Axes about Centre, rotated by Rotation. Rings from the bottom up (the "tangent" of the
	// tube stitching = +Z), so the same winding is outward.
	void AddEllipsoid(FMeshWriter& W, const FVector& Centre, const FVector& Axes, const FQuat& Rotation, const FVector4f& Color, int32 Group)
	{
		constexpr int32 Stacks = 12;
		TArray<TArray<FMeshWriter::FVert>> Rings;
		for (int32 S = 1; S < Stacks; ++S)
		{
			const double Theta = UE_DOUBLE_PI * (1.0 - static_cast<double>(S) / Stacks); // from the bottom up
			TArray<FMeshWriter::FVert>& Ring = Rings.AddDefaulted_GetRef();
			for (int32 J = 0; J < kSegments; ++J)
			{
				const double Phi = UE_DOUBLE_TWO_PI * J / kSegments;
				const FVector Unit(FMath::Sin(Theta) * FMath::Cos(Phi), FMath::Sin(Theta) * FMath::Sin(Phi), FMath::Cos(Theta));
				const FVector P = Centre + Rotation.RotateVector(Unit * Axes);
				const FVector Nrm = Rotation.RotateVector(FVector(Unit.X / Axes.X, Unit.Y / Axes.Y, Unit.Z / Axes.Z));
				Ring.Add(W.Add(P, Nrm, FVector2f(static_cast<float>(Phi / UE_DOUBLE_TWO_PI), static_cast<float>(Theta / UE_DOUBLE_PI)), Color));
			}
		}
		W.Stitch(Rings, Group);
		const FMeshWriter::FVert Bottom = W.Add(Centre - Rotation.RotateVector(FVector(0.0, 0.0, Axes.Z)), -Rotation.RotateVector(FVector::UpVector),
			FVector2f(0.5f, 1.0f), Color);
		const FMeshWriter::FVert Top = W.Add(Centre + Rotation.RotateVector(FVector(0.0, 0.0, Axes.Z)), Rotation.RotateVector(FVector::UpVector),
			FVector2f(0.5f, 0.0f), Color);
		for (int32 J = 0; J < kSegments; ++J)
		{
			const int32 K = (J + 1) % kSegments;
			W.Tri(Bottom, Rings[0][J], Rings[0][K], Group);
			W.Tri(Top, Rings.Last()[K], Rings.Last()[J], Group);
		}
	}

	// A point on the sphere of radius Radius about the ball centre: Theta from the top, Phi around from +X toward +Y [deg].
	FVector OnSphere(double ThetaDeg, double PhiDeg, double Radius)
	{
		const double T = FMath::DegreesToRadians(ThetaDeg);
		const double P = FMath::DegreesToRadians(PhiDeg);
		return FVector(FMath::Sin(T) * FMath::Cos(P), FMath::Sin(T) * FMath::Sin(P), FMath::Cos(T)) * Radius;
	}

	// Quadratic Bezier point.
	FVector Bezier(const FVector& A, const FVector& B, const FVector& C, double S)
	{
		return A * FMath::Square(1.0 - S) + B * (2.0 * S * (1.0 - S)) + C * FMath::Square(S);
	}

	// A finger (or the thumb) arching from its knuckle to a pad resting on the ball: a Bezier bulging away from the ball centre.
	void AddFinger(FMeshWriter& W, const FVector& Knuckle, double TipThetaDeg, double TipPhiDeg, double KnuckleRadius, double Bulge, int32 Group)
	{
		const double TipRadius = 0.72 * KnuckleRadius;
		const FVector Tip = OnSphere(TipThetaDeg, TipPhiDeg, kR0 + TipRadius + 0.05); // the pad touches the ball
		const FVector Mid = 0.5 * (Knuckle + Tip);
		const FVector Control = Mid + Mid.GetSafeNormal() * Bulge;
		TArray<FVector> Points;
		TArray<double> Radii;
		for (const double S : {0.0, 0.18, 0.36, 0.54, 0.72, 0.88, 1.0})
		{
			Points.Add(Bezier(Knuckle, Control, Tip, S));
			Radii.Add(FMath::Lerp(KnuckleRadius, TipRadius, S) * (1.0 + 0.06 * FMath::Sin(UE_DOUBLE_TWO_PI * 1.5 * S))); // joints
		}
		AddTube(W, Points, Radii, kSkin, Group, &kNail);
	}

	void BuildHand(FDynamicMesh3& Mesh)
	{
		FMeshWriter W(Mesh);
		// A fingertip claw from above (how a player lifts and sets down the cue ball): the palm hovers ~1.6 cm over the ball's top,
		// a little behind it and tilted down at the front; the finger pads rest on the front half of the ball above its equator and
		// the thumb on the back-left, so the ball's lower half and its near side stay visible from the player's eye.
		const FVector PalmCentre(-1.4, 0.2, kR0 + 3.0);
		const FQuat PalmTilt(FRotator(-12.0, 0.0, 0.0));
		AddEllipsoid(W, PalmCentre, FVector(4.3, 4.0, 1.3), PalmTilt, kSkin, 0);

		struct FFinger
		{
			double KnucklePhi; // on the palm's front arc [deg], 0 = front (+X), + = right
			double TipPhi;     // around the ball [deg]
			double TipTheta;   // [deg] from the top
			double Scale;      // thickness
		};
		const FFinger Fingers[4] = {{-38.0, -46.0, 64.0, 1.00}, {-12.0, -12.0, 68.0, 1.04}, {14.0, 22.0, 66.0, 0.98}, {38.0, 54.0, 58.0, 0.85}}; // index .. little
		for (int32 F = 0; F < 4; ++F)
		{
			const FFinger& Finger = Fingers[F];
			const double Phi = FMath::DegreesToRadians(Finger.KnucklePhi);
			const FVector Knuckle = PalmCentre + PalmTilt.RotateVector(FVector(3.6 * FMath::Cos(Phi) + 0.4, 3.4 * FMath::Sin(Phi), -0.45));
			AddFinger(W, Knuckle, Finger.TipTheta, Finger.TipPhi, 0.92 * Finger.Scale, 1.1, 1 + F);
		}
		// Thumb: from the palm's back-left to a pad on the ball's back-left.
		AddFinger(W, PalmCentre + PalmTilt.RotateVector(FVector(-1.4, -3.1, -0.6)), 70.0, -132.0, 1.15, 0.9, 5);
		// Wrist and the bare forearm rising RbHandMesh::ForearmElevationDeg toward the player's shoulder, into a rolled shirt cuff
		// that ends RbHandMesh::CuffEndCm from the wrist (the sleeve SM_RbArm_Carry continues from there to the shoulder).
		const FVector Up = RbHandMesh::ForearmDirection();
		const FVector Wrist = RbHandMesh::Wrist();
		check(Wrist.Equals(PalmCentre + FVector(-4.6, 0.1, 0.9), 1e-9));
		AddTube(W, {PalmCentre + FVector(-2.8, 0.0, 0.3), Wrist, Wrist + Up * 3.0}, {2.2, 2.25, 2.35}, kSkin, 6);
		TArray<FVector> Arm;
		TArray<double> ArmRadii;
		for (int32 I = 0; I <= 6; ++I)
		{
			const double S = 16.0 * I / 6.0;
			Arm.Add(Wrist + Up * S);
			ArmRadii.Add(2.35 + 0.65 * S / 16.0); // the forearm thickens toward the elbow
		}
		AddTube(W, Arm, ArmRadii, kSkin, 7);
		// Rolled cuff: a fat roll, then the sleeve's own width, up to the joint with the arm mesh (same radius there).
		const double CuffStart = RbHandMesh::CuffEndCm - 5.0;
		AddTube(W, {Wrist + Up * CuffStart, Wrist + Up * (CuffStart + 1.2), Wrist + Up * (CuffStart + 2.6), Wrist + Up * RbHandMesh::CuffEndCm},
			{3.35, 3.6, 3.45, kArmStartRadius}, kSleeve, 8);
	}

	// The shirt sleeve of the arm mesh: along +X from 0 to RbHandMesh::ArmLengthCm (the component stretches it from the cuff to the
	// shoulder), widening from the forearm to the upper arm, with slight folds.
	void BuildArm(FDynamicMesh3& Mesh)
	{
		FMeshWriter W(Mesh);
		TArray<FVector> Points;
		TArray<double> Radii;
		constexpr int32 Steps = 16;
		for (int32 I = 0; I <= Steps; ++I)
		{
			const double U = static_cast<double>(I) / Steps;
			Points.Add(FVector(RbHandMesh::ArmLengthCm * U, 0.0, 0.0));
			const double Fold = 0.12 * FMath::Sin(UE_DOUBLE_TWO_PI * 3.0 * U) * FMath::Sin(UE_DOUBLE_PI * U);
			Radii.Add(kArmStartRadius + (kArmEndRadius - kArmStartRadius) * FMath::SmoothStep(0.0, 1.0, U) + Fold);
		}
		AddTube(W, Points, Radii, kSleeve, 0);
	}
}

bool URbAssetBakeLibrary::BakeHandCarryMesh()
{
	UMaterialInterface* Material = nullptr;
	const FString MaterialPath = FString(RbAssetPaths::PlayerDir) + TEXT("/M_RbHand");
	if (FPackageName::DoesPackageExist(MaterialPath))
	{
		Material = LoadObject<UMaterialInterface>(nullptr, *(MaterialPath + TEXT(".M_RbHand")));
	}
	// The hand (with the bare forearm and the cuff) and the sleeve that the component stretches to the shoulder.
	struct FPart
	{
		const TCHAR* Path;
		void (*Build)(FDynamicMesh3&);
	};
	for (const FPart& Part : {FPart{RbAssetPaths::HandCarryMesh, &BuildHand}, FPart{RbHandMesh::ArmMeshPath, &BuildArm}})
	{
		FDynamicMesh3 Mesh;
		Part.Build(Mesh);
		FString Error;
		UStaticMesh* StaticMesh = WriteStaticMesh(Mesh, Part.Path, Material, false, false, Error);
		if (!StaticMesh)
		{
			UE_LOG(LogRawBreak, Error, TEXT("BakeHandCarryMesh: %s"), *Error);
			return false;
		}
		UE_LOG(LogRawBreak, Display, TEXT("BakeHandCarryMesh: %s, %d triangles, bounds %s, material %s"), Part.Path, Mesh.TriangleCount(),
			*StaticMesh->GetBoundingBox().ToString(), Material ? *Material->GetPathName() : TEXT("(none yet)"));
	}
	return true;
}
