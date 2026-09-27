#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 3.2 and section 7 Q1 (streak guard). Oracles:
// Tools/reference/human-factors/noise.py (hash, InvNorm, processes), streak.py (streak guard).
#include "rb/Human/NoiseHash.h"

#include "rb/Core/Constants.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"

namespace rb::human
{
	namespace
	{
		// Acklam's coefficients (published values; c4 = -2.549732539343734, HF-T02).
		constexpr double kAcklamA[6] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02,
			-3.066479806614716e+01, 2.506628277459239e+00};
		constexpr double kAcklamB[5] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01,
			-1.328068155288572e+01};
		constexpr double kAcklamC[6] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00,
			4.374664141464968e+00, 2.938163982698783e+00};
		constexpr double kAcklamD[4] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00};
		constexpr double kAcklamLow = 0.02425;

		constexpr std::uint64_t kMantissaMask = (std::uint64_t{1} << 53) - 1u;
		constexpr double kTwoPowMinus53 = 1.0 / 9007199254740992.0;

		// Tail branch of Acklam (Q = sqrt(-2 ln p) of the smaller tail probability).
		double AcklamTail(double Q)
		{
			return (((((kAcklamC[0] * Q + kAcklamC[1]) * Q + kAcklamC[2]) * Q + kAcklamC[3]) * Q + kAcklamC[4]) * Q + kAcklamC[5]) /
				((((kAcklamD[0] * Q + kAcklamD[1]) * Q + kAcklamD[2]) * Q + kAcklamD[3]) * Q + 1.0);
		}

		// Accepted uniform of draw Index of one (MatchSeed, Shooter, Channel) given its history (3.2); InvNorm is not
		// evaluated, so history rebuilds stay cheap. Sub receives the accepted candidate (kStreakMaxRedraws = fallback).
		double GuardedUniform(std::uint64_t MatchSeed, std::uint64_t Shooter, NoiseChannel Channel, std::uint32_t Index, const StreakHistory& History,
			std::uint32_t& Sub)
		{
			int Counts[8] = {};
			const int Count = History.Count <= kStreakWindow ? static_cast<int>(History.Count) : kStreakWindow;
			for (int i = 0; i < Count; ++i)
			{
				++Counts[History.Eighths[i] & 7u];
			}
			// HashKeys(MatchSeed, Shooter, Channel, Index, Sub) = Mix64(HashKeys(MatchSeed, Shooter, Channel, Index) ^ Sub).
			const std::uint64_t Prefix = HashKeys(MatchSeed, Shooter, static_cast<std::uint64_t>(Channel), static_cast<std::uint64_t>(Index));
			std::uint64_t Hash = 0;
			for (std::uint32_t Candidate = 0; Candidate < static_cast<std::uint32_t>(kStreakMaxRedraws); ++Candidate)
			{
				Hash = Mix64(Prefix ^ static_cast<std::uint64_t>(Candidate));
				const double U = U01(Hash);
				if (Counts[EighthOf(U)] < kStreakMaxPerEighth)
				{
					Sub = Candidate;
					return U;
				}
			}
			std::uint32_t Allowed = 0;
			for (int e = 0; e < 8; ++e)
			{
				if (Counts[e] < kStreakMaxPerEighth)
				{
					Allowed |= 1u << e;
				}
			}
			Sub = static_cast<std::uint32_t>(kStreakMaxRedraws);
			return StreakFallback(Hash >> 11, Allowed);
		}

		void PushEighth(StreakHistory& History, std::uint8_t Eighth)
		{
			if (History.Count >= kStreakWindow)
			{
				for (int i = 0; i + 1 < kStreakWindow; ++i)
				{
					History.Eighths[i] = History.Eighths[i + 1];
				}
				History.Eighths[kStreakWindow - 1] = Eighth;
				History.Count = static_cast<std::uint8_t>(kStreakWindow);
				return;
			}
			History.Eighths[History.Count] = Eighth;
			++History.Count;
		}
	}

	double InvNorm(double P)
	{
		if (!(P > 0.0))
		{
			return -kInfinity; // also NaN -> -inf (never produced by U01)
		}
		if (P >= 1.0)
		{
			return kInfinity;
		}
		if (P < kAcklamLow)
		{
			return AcklamTail(Sqrt(-2.0 * Log(P)));
		}
		if (P > 1.0 - kAcklamLow)
		{
			return -AcklamTail(Sqrt(-2.0 * Log(1.0 - P)));
		}
		const double Q = P - 0.5;
		const double R = Q * Q;
		return (((((kAcklamA[0] * R + kAcklamA[1]) * R + kAcklamA[2]) * R + kAcklamA[3]) * R + kAcklamA[4]) * R + kAcklamA[5]) * Q /
			(((((kAcklamB[0] * R + kAcklamB[1]) * R + kAcklamB[2]) * R + kAcklamB[3]) * R + kAcklamB[4]) * R + 1.0);
	}

	double TruncNormal(double U)
	{
		return InvNorm(kNoisePhiLo + U * (1.0 - 2.0 * kNoisePhiLo));
	}

	double StreakFallback(std::uint64_t Candidate53, std::uint32_t AllowedEighths)
	{
		const std::uint64_t N = Candidate53 & kMantissaMask;
		int Allowed[8] = {};
		int M = 0;
		for (int e = 0; e < 8; ++e)
		{
			if ((AllowedEighths >> e) & 1u)
			{
				Allowed[M++] = e;
			}
		}
		if (M == 0)
		{
			return static_cast<double>(N) * kTwoPowMinus53;
		}
		const std::uint64_t P = static_cast<std::uint64_t>(M) * N; // < 2^56
		const std::uint64_t J = P >> 53;                            // < M
		const std::uint64_t V = (static_cast<std::uint64_t>(Allowed[J]) << 50) + ((P & kMantissaMask) >> 3); // < 2^53: exact
		return static_cast<double>(V) * kTwoPowMinus53;
	}

	GuardedDraw DrawGuarded(std::uint64_t MatchSeed, std::uint64_t Shooter, NoiseChannel Channel, std::uint32_t Index, const StreakHistory& History)
	{
		GuardedDraw Draw;
		Draw.U = GuardedUniform(MatchSeed, Shooter, Channel, Index, History, Draw.Sub);
		Draw.Eps = TruncNormal(Draw.U);
		return Draw;
	}

	void PushStreak(StreakHistory& History, double U)
	{
		PushEighth(History, static_cast<std::uint8_t>(EighthOf(U) & 7));
	}

	StreakHistory RebuildStreakHistory(std::uint64_t MatchSeed, std::uint64_t Shooter, NoiseChannel Channel, std::uint32_t Index)
	{
		StreakHistory History;
		for (std::uint32_t n = 0; n < Index; ++n)
		{
			std::uint32_t Sub = 0;
			PushStreak(History, GuardedUniform(MatchSeed, Shooter, Channel, n, History, Sub));
		}
		return History;
	}

	NoiseHistory RebuildNoiseHistory(std::uint64_t MatchSeed, std::uint64_t Shooter, std::uint32_t NextIndex)
	{
		NoiseHistory History;
		History.MatchSeed = MatchSeed;
		History.Shooter = Shooter;
		History.NextIndex = NextIndex;
		for (int Slot = 0; Slot < kStreakChannelCount; ++Slot)
		{
			History.Channels[Slot] = RebuildStreakHistory(MatchSeed, Shooter, StreakChannelAt(Slot), NextIndex);
		}
		return History;
	}

	void AdvanceNoiseHistory(NoiseHistory& History)
	{
		for (int Slot = 0; Slot < kStreakChannelCount; ++Slot)
		{
			std::uint32_t Sub = 0;
			const double U = GuardedUniform(History.MatchSeed, History.Shooter, StreakChannelAt(Slot), History.NextIndex, History.Channels[Slot], Sub);
			PushStreak(History.Channels[Slot], U);
		}
		++History.NextIndex;
	}

	GuardedDraw DrawPerShot(const NoiseKey& Key, NoiseChannel Channel, const NoiseHistory& History, bool StreakGuard)
	{
		const std::uint64_t Shooter = ShooterKey(Key);
		const int Slot = StreakSlot(Channel);
		GuardedDraw Draw;
		if (!StreakGuard || IsRolloutKey(Key) || Slot < 0)
		{
			// Plain independent draw (streak guard off, AI rollout keys, unguarded channels): never reads the history.
			Draw.U = DrawCandidate(Key.MatchSeed, Shooter, Channel, Key.ShooterShotIndex, 0u);
			Draw.Sub = 0;
		}
		else if (HistoryMatchesKey(History, Key))
		{
			Draw.U = GuardedUniform(Key.MatchSeed, Shooter, Channel, Key.ShooterShotIndex, History.Channels[Slot], Draw.Sub);
		}
		else
		{
			// A cache that does not belong to this key is never trusted: the draw is a pure function of the key.
			const StreakHistory Rebuilt = RebuildStreakHistory(Key.MatchSeed, Shooter, Channel, Key.ShooterShotIndex);
			Draw.U = GuardedUniform(Key.MatchSeed, Shooter, Channel, Key.ShooterShotIndex, Rebuilt, Draw.Sub);
		}
		Draw.Eps = TruncNormal(Draw.U);
		return Draw;
	}

	WatchableProcess MakeWatchableProcess(const NoiseKey& Key, NoiseChannel Channel, double BandLo, double BandHi)
	{
		WatchableProcess Process;
		const std::uint64_t Rack = static_cast<std::uint64_t>(Key.RackIndex);
		const std::uint64_t Shot = ProcessShotKey(Key);
		const std::uint64_t Shooter = ShooterKey(Key);
		const std::uint64_t ChannelKey = static_cast<std::uint64_t>(Channel);
		// HashKeys(MatchSeed, RackIndex, ShotKey, S, Channel, j) = Mix64(prefix ^ j).
		const std::uint64_t Prefix = HashKeys(Key.MatchSeed, Rack, Shot, Shooter, ChannelKey);
		for (int k = 0; k < kProcessComponents; ++k)
		{
			const std::uint64_t K2 = static_cast<std::uint64_t>(2 * k);
			const double UFrequency = U01(Mix64(Prefix ^ K2));
			const double UPhase = U01(Mix64(Prefix ^ (K2 + 1u)));
			Process.Frequency[k] = BandLo + (BandHi - BandLo) * (static_cast<double>(k) + UFrequency) / static_cast<double>(kProcessComponents);
			Process.Phase[k] = kTwoPi * UPhase;
		}
		return Process;
	}

	double ProcessValue(const WatchableProcess& Process, double Time)
	{
		double Sum = 0.0;
		for (int k = 0; k < kProcessComponents; ++k)
		{
			Sum += Cos(kTwoPi * Process.Frequency[k] * Time + Process.Phase[k]);
		}
		return Sqrt(2.0 / static_cast<double>(kProcessComponents)) * Sum;
	}

	double ProcessRate(const WatchableProcess& Process, double Time)
	{
		double Sum = 0.0;
		for (int k = 0; k < kProcessComponents; ++k)
		{
			Sum += kTwoPi * Process.Frequency[k] * Sin(kTwoPi * Process.Frequency[k] * Time + Process.Phase[k]);
		}
		return -Sqrt(2.0 / static_cast<double>(kProcessComponents)) * Sum;
	}
}
