// WP-3: Compliant Local Integrator, physics-collisions 9.4 CL-1 ... CL-8 (K = 8.0587e8 N/m^1.5, alpha_T = 0.03689, dt = 1 us,
// equal balls; the head-on chains start exactly touching with the CB at 1 m/s and have no friction or cloth).
#include "rbtest.h"

#include "CompliantTestUtil.h"

#include "rb/Physics/BallBall.h"

#include <cstring>

using namespace rbcl;

RB_TEST(COL_CL1_TwoBallsSpeedIndependentRestitution)
{
	for (double V0 : {0.3, 1.0, 8.0})
	{
		rb::CompliantIsland Island;
		MakeChain(Island, 2, V0, kAlpha095);
		RB_REQUIRE(RunToExit(Island, 100000) > 0);
		const double E = (VelocityOf(Island, 1) - VelocityOf(Island, 0)) / V0;
		RB_CHECK_NEAR(E, 0.9500, 3e-4);
		if (V0 == 1.0)
		{
			RB_CHECK_NEAR(VelocityOf(Island, 0), 0.025, 3e-4);
			RB_CHECK_NEAR(VelocityOf(Island, 1), 0.975, 3e-4);
		}
	}
}

RB_TEST(COL_CL2_ThreeBallsElasticTpB29)
{
	rb::CompliantIsland Island;
	MakeChain(Island, 3, 1.0, 0.0);
	RB_REQUIRE(RunToExit(Island, 100000) > 0);
	RB_CHECK_NEAR(VelocityOf(Island, 0), -0.07095, 5e-4);
	RB_CHECK_NEAR(VelocityOf(Island, 1), 0.07640, 5e-4);
	RB_CHECK_NEAR(VelocityOf(Island, 2), 0.99455, 5e-4);
}

RB_TEST(COL_CL3_ThreeBallsRestitution095)
{
	rb::CompliantIsland Island;
	MakeChain(Island, 3, 1.0, kAlpha095);
	RB_REQUIRE(RunToExit(Island, 100000) > 0);
	RB_CHECK_NEAR(VelocityOf(Island, 0), -0.05469, 1e-3);
	RB_CHECK_NEAR(VelocityOf(Island, 1), 0.10011, 1e-3);
	RB_CHECK_NEAR(VelocityOf(Island, 2), 0.95458, 1e-3);
}

RB_TEST(COL_CL4_FiveBallsMomentumAndEnergy)
{
	rb::CompliantIsland Island;
	MakeChain(Island, 5, 1.0, kAlpha095);
	RB_REQUIRE(RunToExit(Island, 100000) > 0);
	const double Expected[5] = {-0.05501, -0.01794, -0.00215, 0.15049, 0.92460};
	double Momentum = 0.0;
	double Energy = 0.0;
	for (int i = 0; i < 5; ++i)
	{
		const double V = VelocityOf(Island, i);
		RB_CHECK_NEAR(V, Expected[i], 2e-3);
		Momentum += V;
		Energy += V * V;
	}
	RB_CHECK_NEAR(Momentum, 1.0, 1e-12);
	RB_CHECK_NEAR(Energy, 0.8809, 2e-3);
}

RB_TEST(COL_CL5_MicroGapsBetweenObjectBalls)
{
	const double Gap100[1] = {100e-6};
	rb::CompliantIsland Island;
	MakeChain(Island, 3, 1.0, kAlpha095, Gap100);
	RB_REQUIRE(RunToExit(Island, 100000) > 0);
	RB_CHECK_NEAR(VelocityOf(Island, 0), 0.0244, 2e-3);
	RB_CHECK_NEAR(VelocityOf(Island, 1), 0.0247, 2e-3);
	RB_CHECK_NEAR(VelocityOf(Island, 2), 0.9509, 2e-3);

	const double Gap10[1] = {10e-6};
	rb::CompliantIsland Tight;
	MakeChain(Tight, 3, 1.0, kAlpha095, Gap10);
	RB_REQUIRE(RunToExit(Tight, 100000) > 0);
	RB_CHECK_NEAR(VelocityOf(Tight, 0), -0.0300, 2e-3);
	RB_CHECK_NEAR(VelocityOf(Tight, 1), 0.0742, 2e-3);
	RB_CHECK_NEAR(VelocityOf(Tight, 2), 0.9558, 2e-3);
}

RB_TEST(COL_CL6_ThrowAgreesWithImpulseModel)
{
	// 2-ball stun cut in 3D with friction frozen at first touch (s_reg 1e-3), e = 0.95; throw within 0.25 deg of BB-3b
	// (the difference is the finite-duration rotation of n_hat). Reference CLI values 4.80 / 3.45 / 1.49 deg.
	const double Speeds[3] = {0.447, 1.341, 3.129};
	const double Cli[3] = {4.80, 3.45, 1.49};
	const double Impulse[3] = {4.8353, 3.5491, 1.6976};
	const double Phi = 30.0 * rb::kDegToRad;
	for (int v = 0; v < 3; ++v)
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(0.95), kCloth, 0.0, rb::NumericsConfig{});
		Island.AddBody(Body(0, {0.0, 0.0, kR}, {Speeds[v], 0.0, 0.0}));
		Island.AddBody(Body(1, {2.0 * kR * rb::Cos(Phi), 2.0 * kR * rb::Sin(Phi), kR}));
		RB_REQUIRE(RunToExit(Island, 100000) > 0);
		const rb::Vec3 V2 = Island.Body(Island.FindBody(1)).Velocity;
		const double Throw = -(rb::Atan2(V2.y, V2.x) - Phi) * rb::kRadToDeg;
		RB_CHECK_NEAR(Throw, Impulse[v], 0.25);
		RB_CHECK_NEAR(Throw, Cli[v], 0.25);
	}
}

namespace
{
	// CL-7 set-up: ideal 15-ball triangle (zero gaps), apex at the origin, rows toward -x; the CB touches the apex and moves
	// along -x at 8 m/s. Ids: CB 0, rack 1..15 in lattice order; Permutation maps lattice slot -> ball id.
	struct Rack
	{
		rb::Vec3 Position[16];
		rb::Vec3 Velocity[16];
	};

	Rack MakeRack()
	{
		Rack R;
		R.Position[0] = {2.0 * kR, 0.0, kR};
		R.Velocity[0] = {-8.0, 0.0, 0.0};
		int Slot = 1;
		for (int Row = 0; Row < 5; ++Row)
		{
			for (int i = 0; i <= Row; ++i)
			{
				R.Position[Slot] = {-Row * rb::Sqrt(3.0) * kR, (2.0 * i - Row) * kR, kR};
				R.Velocity[Slot] = {};
				++Slot;
			}
		}
		return R;
	}

	// Runs the break with the lattice slot s carried by ball Ids[s], bodies added in the order Order[k]; returns the step count.
	int RunBreak(rb::CompliantIsland& Island, const int* Ids, const int* Order)
	{
		const Rack R = MakeRack();
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(0.95), kCloth, 0.0, rb::NumericsConfig{});
		for (int k = 0; k < 16; ++k)
		{
			const int Slot = Order[k];
			Island.AddBody(Body(Ids[Slot], R.Position[Slot], R.Velocity[Slot]));
		}
		return RunToExit(Island, 200000);
	}

	constexpr int kIdentity[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
}

RB_TEST(COL_CL7_SymmetricRackBreak)
{
	rb::CompliantIsland Island;
	const int Steps = RunBreak(Island, kIdentity, kIdentity);
	RB_REQUIRE(Steps > 0);
	const Rack R = MakeRack();
	double Energy = 0.0;
	rb::Vec3 Momentum;
	for (int Slot = 0; Slot < 16; ++Slot)
	{
		const rb::IslandBody& B = Island.Body(Island.FindBody(Slot));
		Momentum += B.Velocity * B.Mass;
		Energy += 0.5 * B.Mass * rb::LengthSquared(B.Velocity) + 0.5 * B.Inertia * rb::LengthSquared(B.Omega);
		// Mirror partner about y = 0 (same x, opposite y).
		int Partner = -1;
		for (int Other = 0; Other < 16; ++Other)
		{
			if (R.Position[Other].x == R.Position[Slot].x && R.Position[Other].y == -R.Position[Slot].y)
			{
				Partner = Other;
			}
		}
		RB_REQUIRE(Partner >= 0);
		const rb::IslandBody& M = Island.Body(Island.FindBody(Partner));
		RB_CHECK_NEAR(B.Velocity.x, M.Velocity.x, 1e-9);
		RB_CHECK_NEAR(B.Velocity.y, -M.Velocity.y, 1e-9);
		RB_CHECK_NEAR(B.Velocity.z, M.Velocity.z, 1e-9);
		RB_CHECK_NEAR(B.Omega.x * kR, -M.Omega.x * kR, 1e-9);
		RB_CHECK_NEAR(B.Omega.y * kR, M.Omega.y * kR, 1e-9);
		RB_CHECK_NEAR(B.Omega.z * kR, -M.Omega.z * kR, 1e-9);
	}
	const double P0 = 8.0 * kM;
	RB_CHECK(rb::Length(Momentum - rb::Vec3{-P0, 0.0, 0.0}) <= 1e-12 * P0);
	RB_CHECK(Energy <= 0.5 * kM * 64.0);
	RB_CHECK(Energy > 0.5 * 0.5 * kM * 64.0); // sanity: a break does not dissipate half the energy
	// Every rack ball is set in motion.
	for (int Slot = 1; Slot < 16; ++Slot)
	{
		RB_CHECK(rb::Length(Island.Body(Island.FindBody(Slot)).Velocity) > 0.0);
	}
}

RB_TEST(COL_CL8_IdPermutationBitIdentical)
{
	rb::CompliantIsland Reference;
	const int ReferenceSteps = RunBreak(Reference, kIdentity, kIdentity);
	RB_REQUIRE(ReferenceSteps > 0);
	// Two permutations of ids and of the insertion order.
	const int IdsA[16] = {7, 3, 12, 0, 15, 9, 1, 14, 5, 11, 2, 8, 13, 6, 10, 4};
	const int OrderA[16] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
	const int IdsB[16] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
	const int OrderB[16] = {5, 0, 11, 3, 14, 8, 1, 12, 6, 9, 15, 2, 7, 13, 4, 10};
	const int* Ids[2] = {IdsA, IdsB};
	const int* Orders[2] = {OrderA, OrderB};
	for (int p = 0; p < 2; ++p)
	{
		rb::CompliantIsland Permuted;
		const int Steps = RunBreak(Permuted, Ids[p], Orders[p]);
		RB_CHECK(Steps == ReferenceSteps);
		for (int Slot = 0; Slot < 16; ++Slot)
		{
			const rb::IslandBody& A = Reference.Body(Reference.FindBody(Slot));
			const rb::IslandBody& B = Permuted.Body(Permuted.FindBody(Ids[p][Slot]));
			RB_CHECK(std::memcmp(&A.Position, &B.Position, sizeof(rb::Vec3)) == 0);
			RB_CHECK(std::memcmp(&A.Velocity, &B.Velocity, sizeof(rb::Vec3)) == 0);
			RB_CHECK(std::memcmp(&A.Omega, &B.Omega, sizeof(rb::Vec3)) == 0);
		}
	}
}
