// Owner: WP-4. Closed-form cushion and fixed-contact models: Han 2005 (collisions C-H1..C-H4), the generic 3D rigid
// impulse GRI (G-1..G-3), the e_c law (M-7), the pooltool "mirror" model (A-CUSH-3), the Stronge compliant port
// (XREF-02 support, checked against pooltool 0.6.0 as an oracle), the cushion frame and the dispatcher (4.7, 5.3, 6.2,
// 7.1/7.3 resting rule).

#include "Cushion/CushionTestUtil.h"

#include "rb/Core/Tolerances.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

using namespace rbcushiontest;

namespace
{
	void CheckVec(const rb::Vec3& Actual, const rb::Vec3& Expected, double Tol)
	{
		RB_CHECK_NEAR(Actual.x, Expected.x, Tol);
		RB_CHECK_NEAR(Actual.y, Expected.y, Tol);
		RB_CHECK_NEAR(Actual.z, Expected.z, Tol);
	}

	// Airborne ball whose center is Height above the nose line (G-1): k_hat = q / |q| from the nose line to the center.
	rb::Vec3 AirborneNoseNormal(double Height)
	{
		const double D = rb::Sqrt(kR * kR - Height * Height);
		return {0.0, -D / kR, Height / kR};
	}

	rb::FixedContact NoseContact(const rb::Vec3& IntoFeature, double Elevation)
	{
		rb::FixedContact C;
		C.Kind = rb::FixedContactKind::NoseEdge;
		C.BallOnCloth = true;
		C.IntoFeature = IntoFeature;
		C.Elevation = Elevation;
		const rb::CushionFrame F = rb::MakeCushionFrame(IntoFeature);
		C.Normal = F.Y * (-rb::Cos(Elevation)) + F.Z * (-rb::Sin(Elevation));
		return C;
	}

	rb::BallState BallWith(const rb::Vec3& V, const rb::Vec3& W)
	{
		rb::BallState S;
		S.Position = {0.0, 0.0, kR};
		S.Velocity = V;
		S.Omega = W;
		S.State = rb::MotionState::Sliding;
		return S;
	}
}

// ---- Han 2005 (collisions 4.4, 9.3) --------------------------------------------------------------------------------

RB_TEST(COL_CH1_HanRollingPerpendicularLinearInV)
{
	// e_c = 0.85, mu = 0.2, pool nose; rolling perpendicular: v' = (0, -0.811325 V, -0.137922 V), R w' = (-0.109354 V, 0, 0),
	// slide; linear in V (the dispatcher discards v_Z on the cloth).
	const rb::BallSpec Ball = PoolBall();
	const double Speeds[] = {0.5, 1.0, 2.0, 4.0};
	for (double V : Speeds)
	{
		const rb::Vec3 Vin{0.0, V, 0.0};
		const rb::CushionImpactResult R = rb::ResolveHan(Vin, RollingOmega(Vin, kR), Ball, PoolElevation(), 0.85, 0.2);
		CheckVec(R.Velocity, rb::Vec3{0.0, -0.811325, -0.137922} * V, 1e-6 * V);
		CheckVec(R.Omega * kR, rb::Vec3{-0.109354, 0.0, 0.0} * V, 1e-6 * V);
		RB_CHECK(!R.Stick);
		RB_CHECK_NEAR(R.NormalImpulse, 1.85 * kM * V * rb::Cos(PoolElevation()), 1e-15);
	}
}

RB_TEST(COL_CH2_HanStunPerpendicularSticks)
{
	const rb::CushionImpactResult R = rb::ResolveHan({0.0, 1.0, 0.0}, {}, PoolBall(), PoolElevation(), 0.85, 0.2);
	CheckVec(R.Velocity, {0.0, -0.735964, -0.406671}, 1e-6);
	CheckVec(R.Omega * kR, {0.192857, 0.0, 0.0}, 1e-6);
	RB_CHECK(R.Stick);
}

RB_TEST(COL_CH3_HanRolling45)
{
	const rb::Vec3 V = AtIncidence(1.0, 45.0);
	const rb::CushionImpactResult R = rb::ResolveHan(V, RollingOmega(V, kR), PoolBall(), PoolElevation(), 0.85, 0.2);
	CheckVec(R.Velocity, {0.528978, -0.553772, -0.168569}, 1e-6);
	CheckVec(R.Omega * kR, {-0.261784, 0.586870, 0.428784}, 1e-6);
}

RB_TEST(COL_CH4_HanFrictionlessStructuralLoss)
{
	// v_Y'/v_Y = 1 - (1 + e) cos^2(theta_c) = -0.715135 for e = 0.85 (cos^2 = 1 - 0.27^2 = 0.9271).
	const double Cos2 = 1.0 - 0.27 * 0.27;
	const double Speeds[] = {0.3, 1.0, 5.0};
	for (double V : Speeds)
	{
		const rb::CushionImpactResult R = rb::ResolveHan({0.0, V, 0.0}, {}, PoolBall(), PoolElevation(), 0.85, 0.0);
		RB_CHECK_NEAR(R.Velocity.y / V, -0.715135, 1e-9);
		RB_CHECK_NEAR(R.Velocity.y / V, 1.0 - 1.85 * Cos2, 1e-12);
		RB_CHECK_NEAR(R.Velocity.x, 0.0, 1e-15);
	}
	// Even e = 1 cannot exceed 1 - 2 cos^2 in magnitude (0.854).
	const rb::CushionImpactResult E1 = rb::ResolveHan({0.0, 1.0, 0.0}, {}, PoolBall(), PoolElevation(), 1.0, 0.0);
	RB_CHECK_NEAR(-E1.Velocity.y, 2.0 * Cos2 - 1.0, 1e-12);
	RB_CHECK(-E1.Velocity.y < 0.8543);
}

// ---- GRI (collisions 4.6, 9.3) --------------------------------------------------------------------------------------

RB_TEST(COL_G1_GriAirborneNoseHop)
{
	const rb::Vec3 K = AirborneNoseNormal(0.01);
	CheckVec(K, {0.0, -0.936766, 0.349956}, 1e-6);
	const rb::CushionImpactResult R = rb::ResolveGri({0.0, 2.0, 0.0}, {}, PoolBall(), K, 0.85, 0.2);
	CheckVec(R.Velocity, {0.0, -1.316846, 1.025631}, 1e-6);
	CheckVec(R.Omega * kR, {-0.499938, 0.0, 0.0}, 1e-6);
	RB_CHECK(R.Stick);
	RB_CHECK_NEAR(R.Velocity.z * R.Velocity.z / (2.0 * kG), 0.053633, 1e-6);
	RB_CHECK_NEAR(R.NormalSpeed, 2.0 * 0.936766, 1e-6);
}

RB_TEST(COL_G2_GriAirborneNoseWithTopspin)
{
	const rb::CushionImpactResult R = rb::ResolveGri({0.0, 2.0, 0.0}, {-2.0 / kR, 0.0, 0.0}, PoolBall(), AirborneNoseNormal(0.01), 0.85, 0.2);
	CheckVec(R.Velocity, {0.0, -1.116871, 1.560925}, 1e-6);
	CheckVec(R.Omega * kR, {-1.071366, 0.0, 0.0}, 1e-6);
}

RB_TEST(COL_G3_GriSlateParity)
{
	// k_hat = z_hat reproduces motion C.3: v = (1, 0, -1), w = (0, -20, 0), e = 0.5, mu = 0.2 -> slide.
	const rb::CushionImpactResult R = rb::ResolveGri({1.0, 0.0, -1.0}, {0.0, -20.0, 0.0}, PoolBall(), rb::Vec3::UnitZ(), 0.5, 0.2);
	CheckVec(R.Velocity, {0.7, 0.0, 0.5}, 1e-9);
	CheckVec(R.Omega * kR, {0.0, 0.1785, 0.0}, 1e-9);
	RB_CHECK(!R.Stick);
	RB_CHECK_NEAR(R.NormalImpulse, 1.5 * kM, 1e-15);
}

RB_TEST(Integ_COL_G3_GriEqualsResolveSlateImpact)
{
	// The same impact through WP-1's slate resolver (C.3 without the C.4 guard: v_z' = 0.5 > v_z_min).
	const rb::BallSpec Ball = PoolBall();
	const rb::Vec3 V{1.0, 0.0, -1.0};
	const rb::Vec3 W{0.0, -20.0, 0.0};
	const rb::CushionImpactResult G = rb::ResolveGri(V, W, Ball, rb::Vec3::UnitZ(), 0.5, 0.2);
	const rb::SlateImpactResult S = rb::ResolveSlateImpact(V, W, Ball, 0.5, 0.2, rb::SlateParams{}, 1, kG, rb::NumericsConfig{});
	RB_CHECK(MaxAbsDiff(G.Velocity, S.Velocity) <= 1e-12);
	RB_CHECK(MaxAbsDiff(G.Omega, S.Omega) * kR <= 1e-12);
}

RB_TEST(COL_GRI_GeneralInertiaAndSeparatingContact)
{
	// General inertia: a hollow-ish ball (k = 0.5) sticks with the effective tangential mass k m / (1 + k).
	const rb::BallSpec Ball{kR, kM, 0.5 * kM * kR * kR};
	const rb::CushionImpactResult R = rb::ResolveGri({0.3, 0.0, -1.0}, {}, Ball, rb::Vec3::UnitZ(), 0.5, 1.0);
	RB_CHECK(R.Stick);
	// Stick: the contact point stops slipping (v_x - R w_y = 0 after the impulse).
	RB_CHECK_NEAR(R.Velocity.x - kR * R.Omega.y, 0.0, 1e-14);
	RB_CHECK_NEAR(R.Velocity.x, 0.3 * (1.0 - 0.5 / 1.5), 1e-14);
	// Separating: no impulse, flagged resting.
	const rb::CushionImpactResult S = rb::ResolveGri({0.0, 0.0, 1.0}, {1.0, 2.0, 3.0}, PoolBall(), rb::Vec3::UnitZ(), 0.5, 0.2);
	RB_CHECK(S.Resting);
	RB_CHECK(S.Velocity == (rb::Vec3{0.0, 0.0, 1.0}));
	RB_CHECK(S.Omega == (rb::Vec3{1.0, 2.0, 3.0}));
	RB_CHECK(S.NormalImpulse == 0.0);
}

// ---- e_c law (collisions 4.8, M-7) ----------------------------------------------------------------------------------

RB_TEST(COL_M7_CushionRestitutionLaw)
{
	const rb::CushionRestitutionLaw Law{};
	RB_CHECK_NEAR(rb::CushionRestitution(0.5, Law), 0.97, 1e-12);
	RB_CHECK_NEAR(rb::CushionRestitution(1.0, Law), 0.97, 1e-12);
	RB_CHECK_NEAR(rb::CushionRestitution(3.0, Law), 0.90, 1e-12);
	RB_CHECK_NEAR(rb::CushionRestitution(10.0, Law), 0.655, 1e-12);
	RB_CHECK_NEAR(rb::CushionRestitution(20.0, Law), 0.60, 1e-12);
	RB_CHECK_NEAR(rb::CushionRestitution(0.0, Law), 0.97, 1e-12);
}

// ---- Mirror (pooltool "unrealistic", XREF-01) ------------------------------------------------------------------------

RB_TEST(ARCH_CUSH3_MirrorModel)
{
	// v_Y' = -e v_Y; v_X, v_Z and w unchanged (pooltool 0.6.0 UnrealisticLinear: e = 1 by default, e_c with restitution).
	const rb::Vec3 V{0.3, 1.2, 0.0};
	const rb::Vec3 W{1.0, 2.0, 3.0};
	const rb::CushionImpactResult E1 = rb::ResolveMirror(V, W, 1.0);
	CheckVec(E1.Velocity, {0.3, -1.2, 0.0}, 0.0);
	RB_CHECK(E1.Omega == W);
	RB_CHECK_NEAR(E1.NormalSpeed, 1.2, 0.0);
	const rb::CushionImpactResult E8 = rb::ResolveMirror(V, W, 0.8);
	RB_CHECK_NEAR(E8.Velocity.y, -0.96, 1e-15);
	RB_CHECK(E8.Velocity.x == 0.3 && E8.Omega == W);
	// Frictionless rebound angle from the normal: atan(tan(theta) / e) (the prior-art CUSH-01 check for this model).
	const rb::Vec3 In{rb::Sin(30.0 * rb::kPi / 180.0), rb::Cos(30.0 * rb::kPi / 180.0), 0.0};
	const rb::CushionImpactResult A = rb::ResolveMirror(In, {}, 0.8);
	RB_CHECK_NEAR(Degrees(rb::Atan2(A.Velocity.x, -A.Velocity.y)), 35.8175, 1e-4);
	// Separating input: nothing changes.
	const rb::CushionImpactResult S = rb::ResolveMirror({0.3, -1.0, 0.0}, W, 0.8);
	RB_CHECK(S.Resting && S.Velocity.y == -1.0);

	// Through the dispatcher (world frame, cushion along x with Y_hat = +x): impulse (1 + e) m v_Y, v_Z dropped.
	rb::CushionParams Cushion;
	Cushion.OnClothModel = rb::CushionModel::Mirror;
	Cushion.Restitution = {1.0, 0.0, 1.0, 1.0}; // constant e = 1 (pooltool's default)
	const rb::FixedContact C = NoseContact({1.0, 0.0, 0.0}, PoolElevation());
	const rb::CushionImpactResult D = rb::ResolveFixedContact(C, BallWith({1.2, -0.3, 0.0}, W), PoolBall(), Cushion, rb::PocketContactParams{},
		rb::kClothDefault, rb::NumericsConfig{});
	CheckVec(D.Velocity, {-1.2, -0.3, 0.0}, 1e-15);
	RB_CHECK(MaxAbsDiff(D.Omega, W) <= 1e-14);
	RB_CHECK_NEAR(D.NormalImpulse, 2.0 * kM * 1.2, 1e-15);
	RB_CHECK_NEAR(D.Restitution, 1.0, 0.0);
}

// ---- Stronge compliant (pooltool's default cushion, XREF-02) ---------------------------------------------------------

namespace
{
	struct StrongeCase
	{
		const char* Name;
		rb::Vec3 V;
		rb::Vec3 W;
		double E;
		double Mu;
		rb::Vec3 ExpectedV; // pooltool 0.6.0 StrongeCompliantLinear(omega_ratio = 1.8), v_z dropped by pooltool
		rb::Vec3 ExpectedW;
	};
}

RB_TEST(COL_Stronge_MatchesPooltoolOracle)
{
	// Oracle: pooltool 0.6.0 (Tools/xref/.venv), a LinearCushionSegment along x at height h = 0.635 D, the ball touching it
	// (local frame = world frame), omega_ratio 1.8, m and R of the pool ball; outputs printed with 17 digits. Cases cover
	// gross slip, initial stick, slip-stick-slip, side spin and a low e (1 / e > omega_ratio).
	const double RollX = 0.70710678118654757;
	const StrongeCase Cases[] = {
		{"rolling45", {RollX, RollX, 0.0}, {-24.745644136012164, 24.745644136012164, 0.0}, 0.97, 0.14, {0.57432834275997169, -0.62019402995958262, 0.0},
			{-13.128982856346736, 21.609145590502497, 11.185222222222217}},
		{"stun30", {1.7320508075688774, 0.99999999999999989, 0.0}, {0.0, 0.0, 34.99562554680665}, 0.97, 0.14, {1.4814823977732603, -0.85013461183442485, 0.0},
			{7.695023438782008, -5.9189388140696995, 56.10344530010677}},
		{"perp_roll", {0.0, 1.5, 0.0}, {-52.493438320209975, 0.0, 0.0}, 0.9, 0.2, {0.0, -1.2904192035812174, 0.0}, {-4.4767818342832726, 0.0, 0.0}},
		{"perp_stun", {0.0, 1.0, 0.0}, {0.0, 0.0, 0.0}, 0.85, 0.2, {0.0, -0.73848746470627213, 0.0}, {7.5669825042197356, 0.0, 0.0}},
		{"glance", {3.0, 0.40000000000000002, 0.0}, {0.0, 0.0, -40.0}, 0.95, 0.2, {2.8498458606851593, -0.32420578246780651, 0.0},
			{0.34599736489633198, -3.546948172791514, -27.351053109788261}},
		{"draw60", {1.5, 2.5979999999999999, 0.0}, {45.0, -26.0, 30.0}, 0.9, 0.14, {1.3169693915999847, -1.9174200210408303, 0.0},
			{25.256310891037085, -30.323557678740517, 45.418452368339828}},
		{"lowe", {0.5, 1.0, 0.0}, {-10.0, 5.0, 20.0}, 0.5, 0.2, {0.50552840251305176, -0.46156131333292461, 0.0},
			{12.977646003993748, 5.1305921853476804, 19.534288764236592}},
		{"side_only", {0.0, 2.0, 0.0}, {0.0, 0.0, 50.0}, 0.92, 0.2, {0.56330210353340404, -1.6197646917150246, 0.0},
			{19.345028260595882, 13.306348902363878, 2.5475726260291367}},
	};
	const rb::BallSpec Ball = PoolBall();
	for (const StrongeCase& C : Cases)
	{
		const rb::CushionImpactResult R = rb::ResolveStronge(C.V, C.W, Ball, PoolElevation(), C.E, C.Mu, 1.8);
		const bool Ok = rb::Abs(R.Velocity.x - C.ExpectedV.x) <= 1e-9 && rb::Abs(R.Velocity.y - C.ExpectedV.y) <= 1e-9 &&
			MaxAbsDiff(R.Omega, C.ExpectedW) * kR <= 1e-9;
		if (!Ok)
		{
			std::printf("  Stronge case %s: v (%.12f %.12f) w (%.9f %.9f %.9f)\n", C.Name, R.Velocity.x, R.Velocity.y, R.Omega.x, R.Omega.y, R.Omega.z);
		}
		RB_CHECK(Ok);
		// Normal impulse (1 + e) m U along k_hat; energy never increases.
		RB_CHECK(KineticEnergy(Ball, rb::Planar(R.Velocity), R.Omega) <= KineticEnergy(Ball, C.V, C.W) + 1e-12);
	}
	// e -> 0 (the dispatcher's resting rule) falls back to the rigid impulse.
	const rb::CushionImpactResult Rest = rb::ResolveStronge({0.0, 1.0, 0.0}, {}, Ball, PoolElevation(), 0.0, 0.2, 1.8);
	const rb::CushionImpactResult Han = rb::ResolveHan({0.0, 1.0, 0.0}, {}, Ball, PoolElevation(), 0.0, 0.2);
	RB_CHECK(Rest.Velocity == Han.Velocity && Rest.Omega == Han.Omega);
}

// ---- Frame and dispatcher (collisions 4.1, 4.7, 5.3, 6.2, 7.1, 7.3) --------------------------------------------------

RB_TEST(COL_CushionFrameIsAProperRotation)
{
	// RAIL_LEFT (y = +W/2): local and world axes coincide.
	const rb::CushionFrame Left = rb::MakeCushionFrame({0.0, 1.0, 0.0});
	RB_CHECK(Left.X == (rb::Vec3{1.0, 0.0, 0.0}) && Left.Y == (rb::Vec3{0.0, 1.0, 0.0}) && Left.Z == rb::Vec3::UnitZ());
	// An arbitrary horizontal (and a non-horizontal, projected) direction: orthonormal, right-handed, round trip.
	const rb::CushionFrame F = rb::MakeCushionFrame({0.6, -0.8, 0.3});
	RB_CHECK_NEAR(rb::Length(F.X), 1.0, 1e-15);
	RB_CHECK_NEAR(rb::Length(F.Y), 1.0, 1e-15);
	RB_CHECK_NEAR(rb::Dot(F.X, F.Y), 0.0, 1e-15);
	RB_CHECK(MaxAbsDiff(rb::Cross(F.X, F.Y), F.Z) <= 1e-15);
	RB_CHECK(MaxAbsDiff(F.Y, {0.6, -0.8, 0.0}) <= 1e-15);
	const rb::Vec3 Q{0.3, -1.7, 2.2};
	RB_CHECK(MaxAbsDiff(rb::ToWorld(F, rb::ToLocal(F, Q)), Q) <= 1e-15);
}

RB_TEST(COL_DispatcherOnClothUsesMathavanInTheLocalFrame)
{
	// A nose whose Y_hat points along -x (the head rail): the dispatcher result equals ResolveMathavan in the local
	// frame, rotated back; v_Z = 0; Restitution = e_c(v_Y) of the default law.
	const rb::BallSpec Ball = PoolBall();
	rb::CushionParams Cushion; // Mathavan, N by the gate, split on
	const rb::ClothParams Cloth = rb::kClothDefault;
	const rb::NumericsConfig Numerics;
	const rb::FixedContact C = NoseContact({-1.0, 0.0, 0.0}, PoolElevation());
	const rb::CushionFrame F = rb::MakeCushionFrame(C.IntoFeature);
	const rb::Vec3 LocalV = AtIncidence(2.5, 35.0);
	const rb::Vec3 LocalW = RollingOmega(LocalV, kR) + rb::Vec3{0.0, 0.0, 20.0};
	const rb::CushionImpactResult D = rb::ResolveFixedContact(C, BallWith(rb::ToWorld(F, LocalV), rb::ToWorld(F, LocalW)), Ball, Cushion,
		rb::PocketContactParams{}, Cloth, Numerics);

	rb::MathavanSettings S;
	S.Elevation = PoolElevation();
	S.Restitution = rb::CushionRestitution(LocalV.y, Cushion.Restitution);
	S.CushionFriction = Cushion.Friction;
	S.ClothFriction = Cloth.SlidingFriction;
	S.Steps = Cushion.MathavanSteps;
	S.SlipEps = Numerics.CushionSlipEps;
	S.RestSpeed = Numerics.RestSpeed;
	const rb::CushionImpactResult L = rb::ResolveMathavan(LocalV, LocalW, Ball, S);
	RB_CHECK(MaxAbsDiff(rb::ToLocal(F, D.Velocity), L.Velocity) <= 1e-12);
	RB_CHECK(MaxAbsDiff(rb::ToLocal(F, D.Omega), L.Omega) * kR <= 1e-12);
	RB_CHECK(D.Velocity.z == 0.0);
	RB_CHECK_NEAR(D.Restitution, rb::CushionRestitution(LocalV.y, Cushion.Restitution), 0.0);
	RB_CHECK_NEAR(D.NormalSpeed, LocalV.y, 1e-15);
	RB_CHECK(!D.Resting);
	// The ball leaves the cushion (local v_Y' < 0).
	RB_CHECK(rb::ToLocal(F, D.Velocity).y < 0.0);
}

RB_TEST(COL_DispatcherRestingRuleBelowRestSpeed)
{
	// v_perp < v_rest (2 mm/s): e = 0, v_Y := 0, nothing else changes (7.1, 7.3); the same for Han and GRI.
	const rb::BallSpec Ball = PoolBall();
	const rb::FixedContact C = NoseContact({0.0, 1.0, 0.0}, PoolElevation());
	const rb::Vec3 V{0.4, 0.0015, 0.0};
	const rb::Vec3 W{-0.0015 / kR, 0.4 / kR, 3.0};
	rb::CushionParams Cushion;
	const rb::CushionImpactResult M = rb::ResolveFixedContact(C, BallWith(V, W), Ball, Cushion, rb::PocketContactParams{}, rb::kClothDefault,
		rb::NumericsConfig{});
	RB_CHECK(M.Resting);
	RB_CHECK(M.Restitution == 0.0);
	CheckVec(M.Velocity, {0.4, 0.0, 0.0}, 0.0);
	RB_CHECK(M.Omega == W);
	Cushion.OnClothModel = rb::CushionModel::Han2005;
	const rb::CushionImpactResult H = rb::ResolveFixedContact(C, BallWith(V, W), Ball, Cushion, rb::PocketContactParams{}, rb::kClothDefault,
		rb::NumericsConfig{});
	RB_CHECK(H.Resting && H.Restitution == 0.0);
	RB_CHECK(H.Velocity.z == 0.0);
	// GRI (airborne) at 1 mm/s approach: e = 0.
	rb::FixedContact Air = C;
	Air.BallOnCloth = false;
	const rb::CushionImpactResult G = rb::ResolveFixedContact(Air, BallWith({0.0, 0.001, 0.0}, {}), Ball, rb::CushionParams{}, rb::PocketContactParams{},
		rb::kClothDefault, rb::NumericsConfig{});
	RB_CHECK(G.Resting && G.Restitution == 0.0);
	RB_CHECK_NEAR(rb::Dot(G.Velocity, Air.Normal), 0.0, 1e-18);
}

RB_TEST(COL_DispatcherFacingFaceOnTheShelf)
{
	// Facing face, ball on the shelf: Mathavan with theta = beta_v, e_f = k_f e_c(v_perp), mu_f (5.3, 5.5).
	const rb::BallSpec Ball = PoolBall();
	const double Backdraft = 12.0 * rb::kPi / 180.0;
	rb::FixedContact C = NoseContact({0.0, 1.0, 0.0}, Backdraft);
	C.Kind = rb::FixedContactKind::FacingFace;
	rb::CushionParams Cushion;
	Cushion.FacingRestitutionScale = 0.85;
	Cushion.FacingFriction = 0.2;
	const rb::Vec3 V = AtIncidence(1.5, 50.0);
	const rb::Vec3 W = RollingOmega(V, kR);
	const rb::CushionImpactResult D = rb::ResolveFixedContact(C, BallWith(V, W), Ball, Cushion, rb::PocketContactParams{}, rb::kClothDefault,
		rb::NumericsConfig{});
	rb::MathavanSettings S;
	S.Elevation = Backdraft;
	S.Restitution = 0.85 * rb::CushionRestitution(V.y, Cushion.Restitution);
	S.CushionFriction = 0.2;
	S.ClothFriction = rb::kClothDefault.SlidingFriction;
	S.Steps = Cushion.MathavanSteps;
	const rb::CushionImpactResult L = rb::ResolveMathavan(V, W, Ball, S);
	RB_CHECK(MaxAbsDiff(D.Velocity, L.Velocity) <= 1e-12);
	RB_CHECK(MaxAbsDiff(D.Omega, L.Omega) * kR <= 1e-12);
	RB_CHECK_NEAR(D.Restitution, S.Restitution, 1e-15);
}

RB_TEST(COL_DispatcherGriElementsUseTheirRestitutionAndFriction)
{
	// Frictionless head-on GRI impacts (no spin, velocity along -k_hat) isolate e: v' = +e v_c k_hat, P = (1 + e) m v_c.
	const rb::BallSpec Ball = PoolBall();
	rb::PocketContactParams Pocket;
	Pocket.LinerRestitution = 0.3;
	Pocket.RimRestitution = 0.6;
	Pocket.RailTopRestitution = 0.5;
	rb::CushionParams Cushion;
	Cushion.FacingRestitutionScale = 0.85;
	struct KindCase
	{
		rb::FixedContactKind Kind;
		double ExpectedE;
	};
	const double Vc = 2.0;
	const KindCase Cases[] = {
		{rb::FixedContactKind::Liner, 0.3},
		{rb::FixedContactKind::RimTorus, 0.6},
		{rb::FixedContactKind::RailTop, 0.5},
		{rb::FixedContactKind::RailTopEdge, 0.5},
		{rb::FixedContactKind::NoseEdge, rb::CushionRestitution(Vc, Cushion.Restitution)},
		{rb::FixedContactKind::JawArcEdge, rb::CushionRestitution(Vc, Cushion.Restitution)},
		{rb::FixedContactKind::FacingTopEdge, 0.85 * rb::CushionRestitution(Vc, Cushion.Restitution)},
		{rb::FixedContactKind::Slate, 0.6},
	};
	const rb::Vec3 K = rb::Normalized({0.2, -0.9, 0.4});
	for (const KindCase& Kc : Cases)
	{
		rb::FixedContact C;
		C.Kind = Kc.Kind;
		C.BallOnCloth = false;
		C.Normal = K;
		C.IntoFeature = rb::Planar(-K);
		const rb::CushionImpactResult R = rb::ResolveFixedContact(C, BallWith(K * (-Vc), {}), Ball, Cushion, Pocket, rb::kClothDefault, rb::NumericsConfig{});
		RB_CHECK(MaxAbsDiff(R.Velocity, K * (Kc.ExpectedE * Vc)) <= 1e-14);
		RB_CHECK_NEAR(R.NormalImpulse, (1.0 + Kc.ExpectedE) * kM * Vc, 1e-14);
		RB_CHECK_NEAR(R.Restitution, Kc.ExpectedE, 1e-15);
	}
	// A liner hit with slip uses mu_l: slide branch friction = mu_l P_N.
	rb::FixedContact Liner;
	Liner.Kind = rb::FixedContactKind::Liner;
	Liner.BallOnCloth = false;
	Liner.Normal = {0.0, -1.0, 0.0};
	Liner.IntoFeature = {0.0, 1.0, 0.0};
	const rb::CushionImpactResult L = rb::ResolveFixedContact(Liner, BallWith({3.0, 1.0, 0.0}, {}), Ball, Cushion, Pocket, rb::kClothDefault, rb::NumericsConfig{});
	RB_CHECK(!L.Stick);
	RB_CHECK_NEAR(3.0 - L.Velocity.x, Pocket.LinerFriction * (1.0 + Pocket.LinerRestitution) * 1.0, 1e-14);
}

RB_TEST(COL_DispatcherAirborneNoseIsGriWithTheCushionLaw)
{
	// An airborne ball at a nose (G-1 geometry, 2 m/s): GRI with e_c(v_c) and mu_w; v_Z kept (the ball can hop the rail).
	const rb::BallSpec Ball = PoolBall();
	rb::FixedContact C;
	C.Kind = rb::FixedContactKind::NoseEdge;
	C.BallOnCloth = false;
	C.Normal = AirborneNoseNormal(0.01);
	C.IntoFeature = {0.0, 1.0, 0.0};
	const rb::CushionParams Cushion;
	const rb::CushionImpactResult D = rb::ResolveFixedContact(C, BallWith({0.0, 2.0, 0.0}, {}), Ball, Cushion, rb::PocketContactParams{}, rb::kClothDefault,
		rb::NumericsConfig{});
	const double Vc = 2.0 * -C.Normal.y;
	const rb::CushionImpactResult G = rb::ResolveGri({0.0, 2.0, 0.0}, {}, Ball, C.Normal, rb::CushionRestitution(Vc, Cushion.Restitution), Cushion.Friction);
	RB_CHECK(D.Velocity == G.Velocity && D.Omega == G.Omega);
	RB_CHECK(D.Velocity.z > 0.0);
}

RB_TEST(COL_DispatcherHanAndStrongeDropVz)
{
	// Han / Stronge produce a v_Z; for a ball on the cloth the slate blocks it (4.4: pooltool discards it too).
	const rb::BallSpec Ball = PoolBall();
	const rb::FixedContact C = NoseContact({0.0, 1.0, 0.0}, PoolElevation());
	const rb::Vec3 V{0.0, 1.0, 0.0};
	const rb::Vec3 W = RollingOmega(V, kR);
	rb::CushionParams Cushion;
	Cushion.Restitution = {0.85, 0.0, 1.0, 0.85};
	Cushion.Friction = 0.2;
	Cushion.OnClothModel = rb::CushionModel::Han2005;
	const rb::CushionImpactResult H = rb::ResolveFixedContact(C, BallWith(V, W), Ball, Cushion, rb::PocketContactParams{}, rb::kClothDefault,
		rb::NumericsConfig{});
	CheckVec(H.Velocity, {0.0, -0.811325, 0.0}, 1e-6); // C-H1 with v_Z discarded
	Cushion.OnClothModel = rb::CushionModel::StrongeCompliant;
	const rb::CushionImpactResult S = rb::ResolveFixedContact(C, BallWith(V, W), Ball, Cushion, rb::PocketContactParams{}, rb::kClothDefault,
		rb::NumericsConfig{});
	RB_CHECK(S.Velocity.z == 0.0);
	RB_CHECK(S.Velocity.y < 0.0);
}
