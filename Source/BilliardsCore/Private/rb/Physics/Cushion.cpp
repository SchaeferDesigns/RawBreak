#include "rb/Core/FpGuard.h"
// Owner: WP-4 (cushion, facing & pocket-edge resolution). Spec: physics-collisions 4.1-4.9, 5.3, 5.5, 6.2, 7.1, 7.3.
// This file: cushion frame, restitution law, GRI (4.6), Han (4.4), Mirror (XREF-01) and the dispatcher (4.7).
// Mathavan 2010 lives in CushionMathavan.cpp, the Stronge compliant model in CushionStronge.cpp.
#include "rb/Physics/Cushion.h"

#include "rb/Core/Assert.h"
#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		// Contact normal k_hat = -(cos(theta) Y_hat + sin(theta) Z_hat) of an edge / face contact above the ball's
		// equator, in the cushion-local frame (4.1): it points from the contact point I to the ball center.
		Vec3 LocalEdgeNormal(double Elevation)
		{
			return {0.0, -Cos(Elevation), -Sin(Elevation)};
		}

		bool IsCushionLikeKind(FixedContactKind Kind)
		{
			return Kind == FixedContactKind::NoseEdge || Kind == FixedContactKind::JawArcEdge || Kind == FixedContactKind::FacingFace;
		}

		bool IsFacingKind(FixedContactKind Kind)
		{
			return Kind == FixedContactKind::FacingFace || Kind == FixedContactKind::FacingTopEdge;
		}
	}

	CushionFrame MakeCushionFrame(const Vec3& IntoFeatureHorizontal)
	{
		// 4.1: Y = horizontal unit vector into the feature, Z = z_hat, X = Y x Z (a proper rotation about z, so
		// velocities and angular velocities transform alike). The input is projected onto the table plane first.
		CushionFrame Frame;
		const Vec3 Horizontal = Planar(IntoFeatureHorizontal);
		const double Len = Length(Horizontal);
		RB_ASSERT(Len > 0.0);
		Frame.Y = Len > 0.0 ? Horizontal / Len : Vec3::UnitY();
		Frame.Z = Vec3::UnitZ();
		Frame.X = Cross(Frame.Y, Frame.Z);
		return Frame;
	}

	double CushionRestitution(double NormalSpeed, const CushionRestitutionLaw& Law)
	{
		// 4.8 (DERIVED/TUNING): e_c(v) = clamp(Max - Slope max(0, v - Knee), Min, Max) (M-7).
		const double Excess = Max(0.0, NormalSpeed - Law.Knee);
		return Clamp(Law.Max - Law.Slope * Excess, Law.Min, Law.Max);
	}

	CushionImpactResult ResolveGri(const Vec3& Velocity, const Vec3& Omega, const BallSpec& Spec, const Vec3& Normal, double Restitution,
		double Friction)
	{
		// 4.6 generic 3D rigid impulse. Normal = k_hat from the contact point I to the center, r_I = -R k_hat.
		// General inertia (architecture 7.2): the tangential inverse effective mass at a surface point is
		// 1/m + R^2/I = (1 + k)/(k m), so the spec's 2m/7 becomes k m/(1 + k).
		CushionImpactResult Result;
		Result.Velocity = Velocity;
		Result.Omega = Omega;
		Result.Restitution = Restitution;

		const double ApproachSpeed = -Dot(Velocity, Normal); // v_c (the rotation term cancels: w x r_I is normal to k_hat)
		if (!(ApproachSpeed > 0.0))
		{
			// Separating or grazing: no impulse (the approach test of the detector never produces this).
			Result.Restitution = 0.0;
			Result.Resting = true;
			return Result;
		}

		const double Mass = Spec.Mass;
		const double K = InertiaFactor(Spec);
		const double TangentialMass = K * Mass / (1.0 + K);
		const Vec3 ContactArm = Normal * (-Spec.Radius);
		const Vec3 ContactVelocity = Velocity + Cross(Omega, ContactArm);
		const double NormalImpulse = (1.0 + Restitution) * Mass * ApproachSpeed;
		const Vec3 Slip = ContactVelocity - Normal * Dot(ContactVelocity, Normal);
		const double SlipSpeed = Length(Slip);

		Vec3 FrictionImpulse;
		if (TangentialMass * SlipSpeed <= Friction * NormalImpulse)
		{
			FrictionImpulse = Slip * (-TangentialMass); // stick: the contact point stops slipping
			Result.Stick = true;
		}
		else
		{
			FrictionImpulse = Slip * (-Friction * NormalImpulse / SlipSpeed); // slide (SlipSpeed > 0 here)
		}

		const Vec3 Impulse = Normal * NormalImpulse + FrictionImpulse;
		Result.Velocity = Velocity + Impulse / Mass;
		Result.Omega = Omega + Cross(ContactArm, Impulse) / Spec.Inertia;
		Result.NormalSpeed = ApproachSpeed;
		Result.NormalImpulse = NormalImpulse;
		return Result;
	}

	CushionImpactResult ResolveHan(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& Spec, double Elevation, double Restitution,
		double Friction)
	{
		// 4.4: Han 2005 is exactly GRI with k_hat = -(cos(theta) Y + sin(theta) Z) (4.6, last paragraph). The full
		// result incl. v_Z is returned (C-H1..C-H3 list it); the dispatcher discards v_Z for balls on the cloth.
		return ResolveGri(VelocityLocal, OmegaLocal, Spec, LocalEdgeNormal(Elevation), Restitution, Friction);
	}

	CushionImpactResult ResolveMirror(const Vec3& VelocityLocal, const Vec3& OmegaLocal, double Restitution)
	{
		// pooltool "unrealistic" (XREF-01): v_Y' = -e v_Y; v_X, v_Z and w unchanged. The model has no mass, so
		// NormalImpulse stays 0 here (the dispatcher fills it with (1 + e) m v_Y).
		CushionImpactResult Result;
		Result.Velocity = VelocityLocal;
		Result.Omega = OmegaLocal;
		Result.Restitution = Restitution;
		if (!(VelocityLocal.y > 0.0))
		{
			Result.Restitution = 0.0;
			Result.Resting = true;
			return Result;
		}
		Result.Velocity.y = -Restitution * VelocityLocal.y;
		Result.NormalSpeed = VelocityLocal.y;
		return Result;
	}

	CushionImpactResult ResolveFixedContact(const FixedContact& Contact, const BallState& Ball, const BallSpec& Spec, const CushionParams& Cushion,
		const PocketContactParams& Pocket, const ClothParams& Cloth, const NumericsConfig& Numerics)
	{
		// 4.7 model selection. Ball on the cloth (or the shelf) against an edge or a facing face -> the on-cloth model
		// (Mathavan by default) in the cushion-local frame; everything else -> GRI with the element's e / mu.
		// Resting-contact rule (7.1, 7.3): an approach speed below v_rest uses e = 0 (Mathavan: v_Y := 0, nothing else).
		const bool Facing = IsFacingKind(Contact.Kind);
		const double RestitutionScale = Facing ? Cushion.FacingRestitutionScale : 1.0;

		if (Contact.BallOnCloth && IsCushionLikeKind(Contact.Kind))
		{
			const CushionFrame Frame = MakeCushionFrame(Contact.IntoFeature);
			const Vec3 VelocityLocal = ToLocal(Frame, Ball.Velocity);
			const Vec3 OmegaLocal = ToLocal(Frame, Ball.Omega);
			const double NormalSpeed = VelocityLocal.y; // v_perp = v_Y (4.8), horizontal approach speed
			const double Friction = Facing ? Cushion.FacingFriction : Cushion.Friction;
			const bool Resting = NormalSpeed < Numerics.RestSpeed;
			const double Restitution = Resting ? 0.0 : RestitutionScale * CushionRestitution(NormalSpeed, Cushion.Restitution);

			CushionImpactResult Local;
			switch (Cushion.OnClothModel)
			{
			case CushionModel::Han2005:
				Local = ResolveHan(VelocityLocal, OmegaLocal, Spec, Contact.Elevation, Restitution, Friction);
				break;
			case CushionModel::Mirror:
				Local = ResolveMirror(VelocityLocal, OmegaLocal, Restitution);
				Local.NormalImpulse = (1.0 + Restitution) * Spec.Mass * Max(0.0, NormalSpeed);
				break;
			case CushionModel::StrongeCompliant:
				Local = ResolveStronge(VelocityLocal, OmegaLocal, Spec, Contact.Elevation, Restitution, Friction, Cushion.StrongeOmegaRatio);
				break;
			case CushionModel::Mathavan2010:
			default:
			{
				MathavanSettings Settings;
				Settings.Elevation = Contact.Elevation;
				Settings.Restitution = Restitution;
				Settings.CushionFriction = Friction;
				Settings.ClothFriction = Cloth.SlidingFriction;
				Settings.Steps = Cushion.MathavanSteps;
				Settings.MaxBisections = Cushion.MathavanMaxBisections;
				Settings.SplitAtSlipReversal = Cushion.MathavanSplitAtSlipReversal;
				Settings.SlipEps = Numerics.CushionSlipEps;
				Settings.RestSpeed = Numerics.RestSpeed;
				Local = ResolveMathavan(VelocityLocal, OmegaLocal, Spec, Settings);
				break;
			}
			}

			// The slate blocks the vertical DOF of a ball on the cloth: v_Z is discarded (Han / Stronge produce one,
			// Mathavan never does); the caller re-classifies the state (A.8).
			Local.Velocity.z = 0.0;
			CushionImpactResult Result = Local;
			Result.Velocity = ToWorld(Frame, Local.Velocity);
			Result.Omega = ToWorld(Frame, Local.Omega);
			Result.NormalSpeed = Max(0.0, NormalSpeed);
			Result.Resting = Local.Resting || Resting;
			if (Result.Resting)
			{
				Result.Restitution = 0.0;
			}
			return Result;
		}

		// GRI for airborne balls and every other fixed obstacle (4.6, 5.3, 6.2) with the element's restitution and
		// friction. Cushion noses / jaw arcs use the e_c law at v_c (4.8, ESTIMATE) and mu_w; facings k_f e_c and mu_f.
		double Restitution = 0.0;
		double Friction = 0.0;
		const double ApproachSpeed = -Dot(Ball.Velocity, Contact.Normal);
		switch (Contact.Kind)
		{
		case FixedContactKind::NoseEdge:
		case FixedContactKind::JawArcEdge:
			Restitution = CushionRestitution(ApproachSpeed, Cushion.Restitution);
			Friction = Cushion.Friction;
			break;
		case FixedContactKind::FacingFace:
		case FixedContactKind::FacingTopEdge:
			Restitution = RestitutionScale * CushionRestitution(ApproachSpeed, Cushion.Restitution);
			Friction = Cushion.FacingFriction;
			break;
		case FixedContactKind::Liner:
			Restitution = Pocket.LinerRestitution;
			Friction = Pocket.LinerFriction;
			break;
		case FixedContactKind::RimTorus:
			Restitution = Pocket.RimRestitution;
			Friction = Pocket.RimFriction;
			break;
		case FixedContactKind::RailTop:
		case FixedContactKind::RailTopEdge:
			Restitution = Pocket.RailTopRestitution;
			Friction = Pocket.RailTopFriction;
			break;
		case FixedContactKind::Slate:
		default:
			// Parity kind (G-3): the slate itself. Landings are resolved by ResolveSlateImpact (motion C.3), never
			// routed here by the simulator; the dispatcher uses e_slate = the rim value (the same slate, 5.3) and mu_s.
			Restitution = Pocket.RimRestitution;
			Friction = Cloth.SlidingFriction;
			break;
		}

		const bool Resting = ApproachSpeed < Numerics.RestSpeed;
		CushionImpactResult Result = ResolveGri(Ball.Velocity, Ball.Omega, Spec, Contact.Normal, Resting ? 0.0 : Restitution, Friction);
		Result.Resting = Result.Resting || Resting;
		if (Result.Resting)
		{
			Result.Restitution = 0.0;
		}
		return Result;
	}
}
