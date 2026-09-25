#include "rb/Core/FpGuard.h"
// Owner: WP-4 (cushion, facing & pocket-edge resolution). Spec: physics-collisions 4.1-4.9, 5.3, 5.5, 6.2; prior-art 2.8 (Stronge port).
#include "rb/Physics/Cushion.h"

namespace rb
{
	CushionFrame MakeCushionFrame(const Vec3& IntoFeatureHorizontal)
	{
		// TODO(WP-4): Y = normalized horizontal into-feature, Z = z_hat, X = Y x Z (4.1).
		CushionFrame Frame;
		Frame.Y = IntoFeatureHorizontal;
		Frame.Z = Vec3::UnitZ();
		Frame.X = Cross(Frame.Y, Frame.Z);
		return Frame;
	}

	double CushionRestitution(double /*NormalSpeed*/, const CushionRestitutionLaw& Law)
	{
		// TODO(WP-4): clamp(Max - Slope * max(0, v - Knee), Min, Max) (4.8, M-7).
		return Law.Max;
	}

	CushionImpactResult ResolveMathavan(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& /*Spec*/, const MathavanSettings& /*Settings*/)
	{
		// TODO(WP-4): RK4 over P (N steps, split at slip reversal), compression/restitution by Stronge work, bisection (4.5),
		// k_w = 1 / (k m R). Choose the default N by the M-2..M-4 accuracy gate; micro-benchmark <= 2 us per hit.
		CushionImpactResult Result;
		Result.Velocity = VelocityLocal;
		Result.Omega = OmegaLocal;
		return Result;
	}

	CushionImpactResult ResolveHan(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& /*Spec*/, double /*Elevation*/,
		double /*Restitution*/, double /*Friction*/)
	{
		// TODO(WP-4): Han 2005 single impulse (4.4).
		CushionImpactResult Result;
		Result.Velocity = VelocityLocal;
		Result.Omega = OmegaLocal;
		return Result;
	}

	CushionImpactResult ResolveMirror(const Vec3& VelocityLocal, const Vec3& OmegaLocal, double /*Restitution*/)
	{
		// TODO(WP-4): pooltool "unrealistic": v_Y' = -e v_Y, everything else unchanged (XREF-01).
		CushionImpactResult Result;
		Result.Velocity = VelocityLocal;
		Result.Omega = OmegaLocal;
		return Result;
	}

	CushionImpactResult ResolveStronge(const Vec3& VelocityLocal, const Vec3& OmegaLocal, const BallSpec& /*Spec*/, double /*Elevation*/,
		double /*Restitution*/, double /*Friction*/, double /*OmegaRatio*/)
	{
		// TODO(WP-4): port of pooltool's Stronge compliant cushion (Apache-2.0 header + THIRD_PARTY_NOTICES.md entry, XREF-02).
		CushionImpactResult Result;
		Result.Velocity = VelocityLocal;
		Result.Omega = OmegaLocal;
		return Result;
	}

	CushionImpactResult ResolveGri(const Vec3& Velocity, const Vec3& Omega, const BallSpec& /*Spec*/, const Vec3& /*Normal*/, double /*Restitution*/,
		double /*Friction*/)
	{
		// TODO(WP-4): generic 3D rigid impulse (4.6) with the per-ball inertia.
		CushionImpactResult Result;
		Result.Velocity = Velocity;
		Result.Omega = Omega;
		return Result;
	}

	CushionImpactResult ResolveFixedContact(const FixedContact& /*Contact*/, const BallState& Ball, const BallSpec& /*Spec*/, const CushionParams& /*Cushion*/,
		const PocketContactParams& /*Pocket*/, const ClothParams& /*Cloth*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-4): model dispatch (4.7), resting-contact rule (7.3), world-frame output.
		CushionImpactResult Result;
		Result.Velocity = Ball.Velocity;
		Result.Omega = Ball.Omega;
		return Result;
	}
}
