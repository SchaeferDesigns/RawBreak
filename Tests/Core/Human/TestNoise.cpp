// Owner: WP-11 (player model). human-factors 3.2 (hash, InvNorm, streak-guarded draws, watchable processes): HF-T01..T04,
// HF-S01..S03 and ARCH A-HUM-1. Expected values: Tools/reference/human-factors/noise.py, streak.py, recompute_v12.py.

#include "Human/HumanTestUtil.h"

#include <cmath>

using namespace rb;
using namespace rb::human;
using namespace rb::human::testhelp;

RB_TEST(HF_T01_Hash)
{
	std::uint64_t Zero = 0u; // runtime values (the functions are constexpr)
	std::uint64_t One = 1u;
	RB_CHECK(Mix64(Zero) == 0xE220A8397B1DCDAFull);
	RB_CHECK(HashKeys(One, One + 1u, One + 2u) == 0xCD8D705991914EA1ull);
	const std::uint64_t H = HashKeys(std::uint64_t{0x5EEDu}, Zero, Zero, One, One + 4u, Zero);
	RB_CHECK(H == 0x735A7BD1020B7AA3ull);
	RB_CHECK(U01(H) == 0.45059942105039663); // bit-exact
	// Compile-time evaluable (header-only integer definitions).
	static_assert(Mix64(0u) == 0xE220A8397B1DCDAFull);
	static_assert(U01(0u) == 0.0 && U01(~std::uint64_t{0}) < 1.0);
}

RB_TEST(HF_T02_InvNorm)
{
	RB_CHECK_NEAR(InvNorm(0.975), 1.959963986, 1e-9);
	RB_CHECK_NEAR(InvNorm(0.02), -2.053748909, 1e-9);
	RB_CHECK(InvNorm(0.5) == 0.0);
	RB_CHECK_NEAR(TruncNormal(0.0), -2.500000003, 1e-9);
	RB_CHECK_NEAR(TruncNormal(1.0), 2.500000003, 1e-9);
	// Oracle digits (a mistyped c4 moves the tails by ~1e-5, which this catches).
	RB_CHECK_REL(InvNorm(0.975), 1.959963986120195, 1e-12);
	RB_CHECK_REL(InvNorm(0.02), -2.0537489090030348, 1e-12);
	RB_CHECK_REL(TruncNormal(0.0), -2.5000000025659994, 1e-12);
	RB_CHECK(InvNorm(0.0) == -kInfinity && InvNorm(1.0) == kInfinity);
	// Symmetry and monotonicity across the branch split p = 0.02425.
	RB_CHECK_NEAR(InvNorm(0.3) + InvNorm(0.7), 0.0, 1e-15);
	RB_CHECK(InvNorm(0.02424) < InvNorm(0.02425) && InvNorm(0.02425) < InvNorm(0.02426));
	RB_CHECK(InvNorm(0.97574) < InvNorm(0.97575) && InvNorm(0.97575) < InvNorm(0.97576));
}

RB_TEST(HF_T03_StreakGuard)
{
	const int ExpectedEighths[16] = {7, 5, 3, 0, 4, 5, 1, 4, 7, 6, 7, 0, 5, 5, 2, 2};
	StreakHistory History;
	GuardedDraw Draws[16];
	for (std::uint32_t n = 0; n < 16u; ++n)
	{
		Draws[n] = DrawGuarded(0x5EEDu, 1u, NoiseChannel::TipA, n, History);
		PushStreak(History, Draws[n].U);
		RB_CHECK(EighthOf(Draws[n].U) == ExpectedEighths[n]);
		RB_CHECK(Draws[n].Sub == (n == 8u ? 2u : (n == 11u ? 1u : 0u)));
		RB_CHECK(Draws[n].Eps == TruncNormal(Draws[n].U));
	}
	RB_CHECK_NEAR(Draws[0].U, 0.973406374675, 1e-12);
	RB_CHECK(Draws[0].U == 0.9734063746747105);
	RB_CHECK_REL(Draws[0].Eps, 1.845629422572, 1e-9);
	RB_CHECK_REL(Draws[3].Eps, -1.340126013891, 1e-9);
	RB_CHECK_NEAR(Draws[8].U, 0.962396260752, 1e-12);
	RB_CHECK_REL(Draws[8].Eps, 1.713107401759, 1e-9);
	RB_CHECK_REL(Draws[11].Eps, -2.169666181062, 1e-9);
	RB_CHECK_REL(Draws[15].Eps, -0.347111524623, 1e-9);
	// The history rebuilt from n = 0 equals the incremental one.
	const StreakHistory Rebuilt = RebuildStreakHistory(0x5EEDu, 1u, NoiseChannel::TipA, 16u);
	RB_CHECK(Rebuilt.Count == History.Count && Rebuilt.Count == 7);
	for (int i = 0; i < kStreakWindow; ++i)
	{
		RB_CHECK(Rebuilt.Eighths[i] == History.Eighths[i]);
		RB_CHECK(Rebuilt.Eighths[i] == ExpectedEighths[9 + i]);
	}
	// DrawPerShot with a matching NoiseHistory and with a stale one gives the same draw.
	NoiseKey Key = MakeKey(0x5EEDu, 0u, 0u, 1u, 11u);
	const NoiseHistory Matching = RebuildNoiseHistory(0x5EEDu, 1u, 11u);
	const NoiseHistory Stale = RebuildNoiseHistory(0x5EEDu, 1u, 4u);
	RB_CHECK(HistoryMatchesKey(Matching, Key) && !HistoryMatchesKey(Stale, Key));
	RB_CHECK(DrawPerShot(Key, NoiseChannel::TipA, Matching, true).U == Draws[11].U);
	RB_CHECK(DrawPerShot(Key, NoiseChannel::TipA, Stale, true).U == Draws[11].U);
	RB_CHECK(DrawPerShot(Key, NoiseChannel::TipA, Stale, true).Sub == 1u);
}

RB_TEST(HF_T04_Processes)
{
	const Setup S = MakeS0();
	const WatchableProcess Drift = MakeWatchableProcess(S.Key, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
	const WatchableProcess Tremor = MakeWatchableProcess(S.Key, NoiseChannel::TremorLat, kTremorBandLo, kTremorBandHi);
	RB_CHECK_REL(ProcessValue(Drift, 2.5), 0.230366244591, 1e-9);
	RB_CHECK_REL(ProcessRate(Drift, 2.5), -3.986599586190, 1e-9);
	RB_CHECK_REL(ProcessValue(Tremor, 2.5), -0.601466481787, 1e-9);
	RB_CHECK_REL(ProcessRate(Tremor, 2.5), 33.890273864740, 1e-9);
	// Oracle digits.
	RB_CHECK_REL(ProcessValue(Drift, 2.5), 0.23036624459064217, 1e-12);
	RB_CHECK_REL(ProcessRate(Tremor, 2.5), 33.890273864739825, 1e-12);
	for (int k = 0; k < kProcessComponents; ++k)
	{
		// f_k in the k-th sixth of the band.
		RB_CHECK(Drift.Frequency[k] >= kDriftBandLo + (kDriftBandHi - kDriftBandLo) * k / 6.0);
		RB_CHECK(Drift.Frequency[k] < kDriftBandLo + (kDriftBandHi - kDriftBandLo) * (k + 1) / 6.0);
		RB_CHECK(Tremor.Frequency[k] >= 8.0 && Tremor.Frequency[k] < 12.0);
		RB_CHECK(Drift.Phase[k] >= 0.0 && Drift.Phase[k] < kTwoPi);
	}
	// D' is the derivative of D.
	const double H = 1e-6;
	RB_CHECK_NEAR((ProcessValue(Drift, 2.5 + H) - ProcessValue(Drift, 2.5 - H)) / (2.0 * H), ProcessRate(Drift, 2.5), 1e-6);
	// A second get-down on the same shot has new processes (v1.3; AddressIndex 0 is the v1.2 key).
	NoiseKey Second = S.Key;
	Second.AddressIndex = 1u;
	const WatchableProcess DriftSecond = MakeWatchableProcess(Second, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
	RB_CHECK(DriftSecond.Phase[0] != Drift.Phase[0] && ProcessValue(DriftSecond, 2.5) != ProcessValue(Drift, 2.5));
}

RB_TEST(HF_S01_TruncatedMoments)
{
	constexpr std::uint32_t kDraws = 80000;
	StreakHistory History;
	double Sum = 0.0;
	double MaxAbs = 0.0;
	int Redrawn = 0;
	std::uint32_t MaxSub = 0;
	static double Eps[kDraws];
	for (std::uint32_t n = 0; n < kDraws; ++n)
	{
		const GuardedDraw Draw = DrawGuarded(0x5EEDu, 3u, NoiseChannel::Speed, n, History);
		PushStreak(History, Draw.U);
		Eps[n] = Draw.Eps;
		Sum += Draw.Eps;
		MaxAbs = std::fmax(MaxAbs, std::fabs(Draw.Eps));
		Redrawn += Draw.Sub > 0u ? 1 : 0;
		MaxSub = Draw.Sub > MaxSub ? Draw.Sub : MaxSub;
	}
	const double Mean = Sum / kDraws;
	double SumSq = 0.0;
	for (std::uint32_t n = 0; n < kDraws; ++n)
	{
		SumSq += (Eps[n] - Mean) * (Eps[n] - Mean);
	}
	const double Sd = std::sqrt(SumSq / kDraws);
	RB_CHECK_NEAR(Mean, -0.001307, 1e-6);
	RB_CHECK_NEAR(Mean, 0.0, 0.01);
	RB_CHECK_NEAR(Sd, 0.954631, 1e-6);
	RB_CHECK_NEAR(Sd, kTruncatedSigma, 0.005);
	RB_CHECK(MaxAbs <= 2.5000001);
	RB_CHECK(std::abs(Redrawn - 16671) <= 2);
	RB_CHECK(MaxSub == 9u);
}

RB_TEST(HF_S02_StreakCap)
{
	constexpr int kDraws = 4000;
	const NoiseKey Base = MakeKey(0x5EEDu, 0u, 0u, 3u, 0u);
	NoiseHistory History = RebuildNoiseHistory(Base.MatchSeed, ShooterKey(Base), 0u);
	static int Eighths[kDraws];
	int Counts[8] = {};
	int Redrawn = 0;
	bool EveryDrawEqualsRebuilt = true;
	for (int n = 0; n < kDraws; ++n)
	{
		NoiseKey Key = Base;
		Key.ShooterShotIndex = static_cast<std::uint32_t>(n);
		const GuardedDraw Draw = DrawPerShot(Key, NoiseChannel::Speed, History, true);
		// The canonical definition: the history rebuilt from draw 0.
		const StreakHistory Rebuilt = RebuildStreakHistory(Base.MatchSeed, ShooterKey(Base), NoiseChannel::Speed, static_cast<std::uint32_t>(n));
		const GuardedDraw Canonical = DrawGuarded(Base.MatchSeed, ShooterKey(Base), NoiseChannel::Speed, static_cast<std::uint32_t>(n), Rebuilt);
		EveryDrawEqualsRebuilt = EveryDrawEqualsRebuilt && Canonical.U == Draw.U && Canonical.Sub == Draw.Sub;
		Eighths[n] = EighthOf(Draw.U);
		++Counts[Eighths[n]];
		Redrawn += Draw.Sub > 0u ? 1 : 0;
		AdvanceNoiseHistory(History);
	}
	RB_CHECK(EveryDrawEqualsRebuilt);
	int Worst8 = 0;
	for (int i = 0; i + 8 <= kDraws; ++i)
	{
		int Window[8] = {};
		for (int j = i; j < i + 8; ++j)
		{
			++Window[Eighths[j]];
		}
		for (int e = 0; e < 8; ++e)
		{
			Worst8 = Window[e] > Worst8 ? Window[e] : Worst8;
		}
	}
	RB_CHECK(Worst8 <= 2);
	int LongestRun = 1;
	int Run = 1;
	for (int n = 1; n < kDraws; ++n)
	{
		Run = Eighths[n] == Eighths[n - 1] ? Run + 1 : 1;
		LongestRun = Run > LongestRun ? Run : LongestRun;
	}
	RB_CHECK(LongestRun <= 2);
	const int ExpectedCounts[8] = {501, 498, 497, 510, 509, 493, 473, 519};
	for (int e = 0; e < 8; ++e)
	{
		RB_CHECK(Counts[e] >= 473 && Counts[e] <= 519);
		RB_CHECK(std::abs(Counts[e] - ExpectedCounts[e]) <= 2);
	}
	RB_CHECK(std::abs(Redrawn - 824) <= 2);
}

RB_TEST(HF_S03_ProcessRms)
{
	const Setup S = MakeS0();
	const WatchableProcess Drift = MakeWatchableProcess(S.Key, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
	const WatchableProcess Tremor = MakeWatchableProcess(S.Key, NoiseChannel::TremorLat, kTremorBandLo, kTremorBandHi);
	constexpr int kSamples = 600000; // 1 kHz over 600 s
	double SumDrift = 0.0;
	double SumTremor = 0.0;
	for (int i = 0; i < kSamples; ++i)
	{
		const double T = i / 1000.0;
		const double D = ProcessValue(Drift, T);
		const double Tr = ProcessValue(Tremor, T);
		SumDrift += D * D;
		SumTremor += Tr * Tr;
	}
	const double RmsDrift = std::sqrt(SumDrift / kSamples);
	const double RmsTremor = std::sqrt(SumTremor / kSamples);
	RB_CHECK_NEAR(RmsDrift, 0.99894, 1e-5);
	RB_CHECK_NEAR(RmsTremor, 1.00011, 1e-5);
	RB_CHECK_NEAR(RmsDrift, 1.0, 0.03);
	RB_CHECK_NEAR(RmsTremor, 1.0, 0.03);
}

// A-HUM-1: streak guard caching, purity, rollout keys, switch, exact fallback, address index.
RB_TEST(ARCH_HUM1_StreakGuardCacheIsAPureFunctionOfTheKey)
{
	constexpr std::uint32_t kDraws = 10000;
	const std::uint64_t Seed = 0xA11CEu;
	const std::uint64_t Shooter = 7u;
	NoiseHistory Incremental = RebuildNoiseHistory(Seed, Shooter, 0u);
	const NoiseHistory Foreign = RebuildNoiseHistory(Seed, Shooter + 1u, 0u); // another shooter's cache
	bool CachedEqualsRebuilt = true;
	bool ForeignCacheHarmless = true;
	for (std::uint32_t n = 0; n < kDraws; ++n)
	{
		NoiseKey Key = MakeKey(Seed, n / 20u, n, static_cast<std::uint32_t>(Shooter), n);
		const bool Checkpoint = n % 997u == 0u || n == kDraws - 1u || n < 16u;
		for (int Slot = 0; Slot < kStreakChannelCount; ++Slot)
		{
			const NoiseChannel Channel = StreakChannelAt(Slot);
			const GuardedDraw Draw = DrawPerShot(Key, Channel, Incremental, true);
			if (Checkpoint)
			{
				const StreakHistory Rebuilt = RebuildStreakHistory(Seed, Shooter, Channel, n);
				CachedEqualsRebuilt = CachedEqualsRebuilt && Rebuilt.Count == Incremental.Channels[Slot].Count &&
					std::memcmp(Rebuilt.Eighths, Incremental.Channels[Slot].Eighths, Rebuilt.Count) == 0;
				ForeignCacheHarmless = ForeignCacheHarmless && DrawPerShot(Key, Channel, Foreign, true).U == Draw.U;
			}
		}
		AdvanceNoiseHistory(Incremental);
	}
	RB_CHECK(CachedEqualsRebuilt);
	RB_CHECK(ForeignCacheHarmless);
	const NoiseHistory Rebuilt = RebuildNoiseHistory(Seed, Shooter, kDraws);
	RB_CHECK(Rebuilt.NextIndex == Incremental.NextIndex);
	for (int Slot = 0; Slot < kStreakChannelCount; ++Slot)
	{
		RB_CHECK(Rebuilt.Channels[Slot].Count == Incremental.Channels[Slot].Count);
		RB_CHECK(std::memcmp(Rebuilt.Channels[Slot].Eighths, Incremental.Channels[Slot].Eighths, kStreakWindow) == 0);
	}

	// Rollout keys and StreakGuard = false: the plain candidate Sub 0, the history never read.
	NoiseKey Key = MakeKey(Seed, 3u, 60u, static_cast<std::uint32_t>(Shooter), 60u);
	const NoiseKey Rollout = RolloutKey(Key, 4u);
	RB_CHECK(DrawPerShot(Rollout, NoiseChannel::TipB, Incremental, true).U ==
		DrawCandidate(Seed, ShooterKey(Rollout), NoiseChannel::TipB, 60u, 0u));
	RB_CHECK(DrawPerShot(Rollout, NoiseChannel::TipB, Foreign, true).U == DrawPerShot(Rollout, NoiseChannel::TipB, Incremental, true).U);
	RB_CHECK(DrawPerShot(Rollout, NoiseChannel::TipB, Incremental, true).U != DrawPerShot(RolloutKey(Key, 5u), NoiseChannel::TipB, Incremental, true).U);
	RB_CHECK(DrawPerShot(Key, NoiseChannel::TipB, Foreign, false).U == DrawCandidate(Seed, Shooter, NoiseChannel::TipB, 60u, 0u));
	RB_CHECK(DrawPerShot(Key, NoiseChannel::TipB, Foreign, false).Sub == 0u);

	// Exact-integer fallback: the HF 3.2 counter-example (the v1.2 float form lands in the excluded eighth 6) and a sweep.
	const std::uint32_t Allowed = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 7);
	const double Fallback = StreakFallback(7205759403792793ull, Allowed);
	RB_CHECK(EighthOf(Fallback) == 5);
	const double U = 7205759403792793.0 / 9007199254740992.0;
	const int J = static_cast<int>(5.0 * U);
	const double FloatForm = (5.0 + (5.0 * U - J)) / 8.0; // A[j] = 5 for j = 3
	RB_CHECK(J == 3 && EighthOf(FloatForm) == 6);        // the v1.2 rounding bug
	std::uint64_t State = 0x1234u;
	bool AllInAllowed = true;
	for (int i = 0; i < 100000; ++i)
	{
		const std::uint64_t R = SplitMix64Next(State);
		std::uint32_t Mask = static_cast<std::uint32_t>(R >> 56);
		int BitCount = 0;
		for (int e = 0; e < 8; ++e)
		{
			BitCount += static_cast<int>((Mask >> e) & 1u);
		}
		if (BitCount < 5)
		{
			Mask |= 0xF8u >> (R & 3u); // make at least five eighths allowed
		}
		const double V = StreakFallback(R >> 11, Mask);
		AllInAllowed = AllInAllowed && V >= 0.0 && V < 1.0 && ((Mask >> EighthOf(V)) & 1u) != 0u;
	}
	RB_CHECK(AllInAllowed);
	RB_CHECK(StreakFallback(0u, 0xFFu) == 0.0 && EighthOf(StreakFallback((std::uint64_t{1} << 53) - 1u, 0xFFu)) == 7);

	// AddressIndex 0 gives the v1.2 process key, AddressIndex 1 a different process.
	NoiseKey First = MakeKey(0x5EEDu, 0u, 0u, 1u, 0u);
	RB_CHECK(ProcessShotKey(First) == 0u);
	NoiseKey Again = First;
	Again.AddressIndex = 1u;
	RB_CHECK(ProcessShotKey(Again) == (std::uint64_t{1} << 32));
	const WatchableProcess P0 = MakeWatchableProcess(First, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
	const WatchableProcess P1 = MakeWatchableProcess(Again, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
	RB_CHECK_REL(ProcessValue(P0, 2.5), 0.23036624459064217, 1e-12);
	RB_CHECK(ProcessValue(P1, 2.5) != ProcessValue(P0, 2.5));
}
