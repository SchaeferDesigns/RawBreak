#include "Simulation/RbShot.h"

// Owner: UE-6a.

namespace
{
	uint64 Bits(double V)
	{
		uint64 B = 0;
		FMemory::Memcpy(&B, &V, sizeof(B));
		return B;
	}

	void Mix(uint64& H, uint64 V)
	{
		// TODO(UE-6a): must be bit-identical to Mix() in Tests/Core/Simulator/SimTestUtil.h (copy it verbatim).
		H ^= V;
		H *= 0x100000001B3ull;
	}
}

namespace RbShot
{
	uint64 ResultHash(const rb::ShotResult& Result)
	{
		// TODO(UE-6a): full definition of SimTestUtil.h ResultHash (events incl. states, finals, diagnostics counters).
		uint64 H = 0xCBF29CE484222325ull;
		Mix(H, static_cast<uint64>(Result.Status));
		Mix(H, Bits(Result.StopTime));
		return H;
	}

	uint64 InputHash(const rb::SimInput& Input)
	{
		// TODO(UE-6a): full definition shared with rbsim --hash (every SimInput field, doubles by bit pattern).
		uint64 H = 0xCBF29CE484222325ull;
		Mix(H, static_cast<uint64>(Input.Strikes.Size()));
		return H;
	}

	void CopyCompact(const rb::ShotResult& Source, rb::ShotResult& Out)
	{
		Out = Source; // std::vector copy allocates size(), not capacity() - TODO(UE-6a): verify + test the footprint
	}

	void InitSimInput(const FRbTableContext& Table, rb::SimInput& Out)
	{
		Out = rb::SimInput{};
		Out.Table = &Table.Geometry;
		Out.Environment = Table.Environment;
		Out.Params = Table.Physics;
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			Out.Balls[Id].InPlay = false;
			Out.Balls[Id].Spec = Id < Table.Balls.Count ? Table.Balls.Balls[Id] : rb::BallSpec{};
		}
	}
}
