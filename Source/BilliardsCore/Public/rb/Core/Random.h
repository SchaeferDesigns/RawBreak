#pragma once

// Deterministic, seedable pseudo-random numbers for the few places where the core consumes
// randomness that the GAME LAYER seeded (rack fill, rack micro-gaps, per-ball bar-ball variation).
// The physics event loop itself never draws random numbers.
// Owner: WP-0 (architecture, frozen). Header-only.

#include "rb/Config.h"
#include "rb/Math/Scalar.h"

#include <cstdint>

namespace rb
{
	// SplitMix64 (Steele, Lea, Flood 2014): used to expand one 64-bit seed into generator state.
	constexpr std::uint64_t SplitMix64Next(std::uint64_t& State)
	{
		State += 0x9E3779B97F4A7C15ull;
		std::uint64_t Z = State;
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		return Z ^ (Z >> 31);
	}

	// xoshiro256** (Blackman & Vigna 2018). Value type: copy it to fork a stream; no global state.
	class Rng
	{
	public:
		constexpr explicit Rng(std::uint64_t Seed = 0x5EED5EED5EED5EEDull)
		{
			std::uint64_t Mix = Seed;
			for (std::uint64_t& Word : S)
			{
				Word = SplitMix64Next(Mix);
			}
		}

		constexpr std::uint64_t NextU64()
		{
			const std::uint64_t Result = RotL(S[1] * 5u, 7) * 9u;
			const std::uint64_t T = S[1] << 17;
			S[2] ^= S[0];
			S[3] ^= S[1];
			S[1] ^= S[2];
			S[0] ^= S[3];
			S[2] ^= T;
			S[3] = RotL(S[3], 45);
			return Result;
		}

		// Uniform in [0, 1) with 53 random bits.
		constexpr double NextDouble01() { return static_cast<double>(NextU64() >> 11) * (1.0 / 9007199254740992.0); }

		// Uniform in [Lo, Hi).
		constexpr double NextUniform(double Lo, double Hi) { return Lo + (Hi - Lo) * NextDouble01(); }

		// Unbiased integer in [0, Bound) for Bound > 0 (rejection sampling; deterministic).
		constexpr std::uint32_t NextBelow(std::uint32_t Bound)
		{
			const std::uint64_t Limit = (0x100000000ull / Bound) * Bound;
			for (;;)
			{
				const std::uint64_t X = NextU64() >> 32;
				if (X < Limit)
				{
					return static_cast<std::uint32_t>(X % Bound);
				}
			}
		}

		// Standard normal deviate (Box-Muller, cosine branch only; uses rb::Log/Sqrt/Cos so that
		// results are reproducible wherever those wrappers are).
		double NextNormal()
		{
			double U1 = NextDouble01();
			if (U1 <= 0.0)
			{
				U1 = 1.0 / 9007199254740992.0;
			}
			const double U2 = NextDouble01();
			return Sqrt(-2.0 * Log(U1)) * Cos(2.0 * 3.14159265358979323846 * U2);
		}

		// Fisher-Yates shuffle of Count elements.
		template <class T>
		constexpr void Shuffle(T* Items, int Count)
		{
			for (int i = Count - 1; i > 0; --i)
			{
				const int j = static_cast<int>(NextBelow(static_cast<std::uint32_t>(i + 1)));
				const T Tmp = Items[i];
				Items[i] = Items[j];
				Items[j] = Tmp;
			}
		}

	private:
		static constexpr std::uint64_t RotL(std::uint64_t X, int K) { return (X << K) | (X >> (64 - K)); }

		std::uint64_t S[4] = {};
	};
}
