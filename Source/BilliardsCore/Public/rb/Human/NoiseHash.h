#pragma once

// Deterministic seeded noise of the player model (human-factors 3.2; product-owner decision Q1, section 7):
// a counter-based hash, uniform and truncated-normal draws, STREAK-GUARDED per-shot draws and the band-limited
// "watchable" processes of the grip hand and the tremor. There is no RNG state: every value is a pure function
// of its keys (and, for the streak guard, of the earlier draws of the same keys), so a replay reproduces bit
// for bit (human-factors principle 3, Docs/architecture.md 11).
// Owner: WP-11 (player model). Part of rb::human: never included by the event loop (rb/Physics/**) or by the
// rules (rb/Rules/**, rb/Shot/**); the root CMakeLists.txt checks this at configure time.
//
// Streak guard (Q1, replaces the fixed bags of 8 of human-factors v1.1): per shooter key S and channel c, the
// draw n = ShooterShotIndex takes the first candidate u_{n,Sub} = U01(HashKeys(MatchSeed, S, c, n, Sub)),
// Sub = 0, 1, ..., whose eighth floor(8 u) occurs fewer than kStreakMaxPerEighth (2) times among the eighths of
// the ACCEPTED draws n-7 .. n-1 of the same (S, c). Hence no eighth occurs more than twice in any 8
// consecutive draws, at most 3 eighths are ever excluded (the next draw is never known in advance), and the
// marginal distribution stays uniform. After kStreakMaxRedraws rejected candidates (probability
// <= (3/8)^32 = 2.4e-14) the last candidate, N = HashKeys(...) >> 11 (u = N 2^-53), is mapped into the allowed eighths
// A (ascending, m = |A| >= 5) in exact integer arithmetic: P = m N (< 2^56), j = P >> 53, u := (A[j] 2^50 + ((P mod 2^53)
// >> 3)) 2^-53, so u lies in eighth A[j] exactly (the floating-point (A[j] + (m u - j)) / 8 of human-factors v1.2 rounds
// up into eighth A[j] + 1, which may be excluded, for some N, e.g. N = 7205759403792793 with A = {0, 1, 3, 5, 7}).
// Streak guard off, or an AI rollout key (Purpose != 0): u = u_{n,0} (plain independent
// draws; rollouts never read the shooter's history).
// The draws are a pure function of (MatchSeed, S, c, n): NoiseHistory is only a cache. Every consumer (DrawPerShot,
// ExecuteStroke, SampleHand, SyntheticHand) uses it only if HistoryMatchesKey, and otherwise rebuilds the history
// (RebuildStreakHistory, O(n), allocation-free), so a stale or foreign history can never change a draw (v1.2 review).
// Oracle: Tools/reference/human-factors/streak.py.

#include "rb/Config.h"
#include "rb/Core/Random.h"

#include <cstdint>

namespace rb::human
{
	// ---------------------------------------------------------------------------------------------
	// Hash primitives (exact integer definitions; header-only)
	// ---------------------------------------------------------------------------------------------

	// Mix64(x) = SplitMix64Next(copy of x): add 0x9E3779B97F4A7C15, then the SplitMix64 finaliser.
	constexpr std::uint64_t Mix64(std::uint64_t X)
	{
		std::uint64_t State = X;
		return SplitMix64Next(State);
	}

	inline constexpr std::uint64_t kHashSeed = 0x243F6A8885A308D3ull;

	// HashKeys(k_0, ..., k_{n-1}): h = kHashSeed; for each key in order h = Mix64(h XOR k_i). Keys are uint64
	// (narrower unsigned keys are widened; never pass negative values).
	template <class... TKeys>
	constexpr std::uint64_t HashKeys(TKeys... Keys)
	{
		static_assert(sizeof...(TKeys) > 0, "HashKeys needs at least one key");
		std::uint64_t H = kHashSeed;
		((H = Mix64(H ^ static_cast<std::uint64_t>(Keys))), ...);
		return H;
	}

	// (h >> 11) 2^-53: uniform in [0, 1).
	constexpr double U01(std::uint64_t H) { return static_cast<double>(H >> 11) * (1.0 / 9007199254740992.0); }

	// ---------------------------------------------------------------------------------------------
	// Normal quantile and truncated draws
	// ---------------------------------------------------------------------------------------------

	inline constexpr double kNoiseTruncation = 2.5;              // per-shot draws are truncated at +-2.5 sigma (principle 8)
	inline constexpr double kNoisePhiLo = 0.0062096653257761;    // Phi(-2.5)
	inline constexpr double kTruncatedSigma = 0.9545974863445806; // standard deviation of a truncated unit draw (3.3)

	// Acklam's rational approximation of the standard normal quantile, split at p = 0.02425, no refinement step
	// (|rel err| < 1.15e-9), through rb::Log / rb::Sqrt; published coefficients (c4 = -2.549732539343734, HF-T02).
	// P <= 0 -> -inf, P >= 1 -> +inf.
	RB_API double InvNorm(double P);

	// InvNorm(kNoisePhiLo + U (1 - 2 kNoisePhiLo)): z in [-2.5, 2.5] for U in [0, 1].
	RB_API double TruncNormal(double U);

	// ---------------------------------------------------------------------------------------------
	// Channels and keys
	// ---------------------------------------------------------------------------------------------

	enum class NoiseChannel : std::uint32_t
	{
		DriftLat = 1,      // watchable grip-hand drift, lateral (HF-01)
		DriftVert = 2,     // ... vertical
		TremorLat = 3,     // watchable tremor at the tip (HF-05)
		TremorVert = 4,
		TipA = 5,          // per-shot, streak-guarded: lateral tip placement (HF-02)
		TipB = 6,          //   vertical tip placement
		Elevation = 7,     //   unintended elevation (HF-04)
		Speed = 8,         //   speed scatter (HF-03)
		Flinch = 9,        //   flinch speed loss (HF-14; uses u directly)
		WarpRoll = 10,     // bow orientation per cue pickup (HF-31, 4.4)
		HandAim = 20,      // AI synthetic hand (3.8), streak-guarded: aim scatter
		HandSteer = 21,    //   steering through the pivot
		HandSpeed = 22,    //   speed scatter
		HandPause = 23,    // AI synthetic hand, plain per-shot uniforms: backstroke pause
		HandJab = 24,      //   jab
		HandTimeDown = 25, //   time down on the shot
		HandHead = 26,     //   head lift
		HandReserved = 27,
	};

	// Bit of a channel in HumanParams::ChannelMask (bit set = channel off: diagnosis re-runs, 3.9).
	constexpr std::uint32_t ChannelBit(NoiseChannel Channel) { return 1u << static_cast<std::uint32_t>(Channel); }

	inline constexpr std::uint32_t kWatchableChannelMask = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
	inline constexpr std::uint32_t kPerShotChannelMask = (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9);

	struct NoiseKey
	{
		std::uint64_t MatchSeed = 0;
		std::uint32_t RackIndex = 0;
		std::uint32_t ShotIndex = 0;        // shot of the match (both players)
		std::uint32_t ShooterId = 0;
		std::uint32_t ShooterShotIndex = 0; // this shooter's REVEALED per-shot draws in the match (3.7): advances on every tip
		                                    //   contact and on every aborted stroke that showed part of the per-shot ramp
		std::uint32_t CuePickupIndex = 0;   // pickups of a cue by this shooter (warp roll, 4.4)
		std::uint32_t Purpose = 0;          // 0 = the match stream; 1 + s = AI rollout sample s in [0, K) (never the match stream)
		std::uint32_t AddressIndex = 0;     // earlier get-downs on this shot (input log): standing up and getting down again gives
		                                    //   NEW drift / tremor processes, so a dry run cannot reveal them (3.2); AI: 0
	};

	// The shooter key of every hash: ShooterId widened to 64 bits, the rollout purpose in the high word (3.2).
	constexpr std::uint64_t ShooterKey(const NoiseKey& Key)
	{
		return static_cast<std::uint64_t>(Key.ShooterId) | (static_cast<std::uint64_t>(Key.Purpose) << 32);
	}

	constexpr bool IsRolloutKey(const NoiseKey& Key) { return Key.Purpose != 0; }

	// The shot key of the watchable processes: ShotIndex widened to 64 bits, AddressIndex in the high word (3.2). Equals
	// ShotIndex for the first get-down, so every value keyed with AddressIndex 0 (HF-T04, HF-S03) is unchanged.
	constexpr std::uint64_t ProcessShotKey(const NoiseKey& Key)
	{
		return static_cast<std::uint64_t>(Key.ShotIndex) | (static_cast<std::uint64_t>(Key.AddressIndex) << 32);
	}

	// Key of rollout sample Sample (common random numbers: all candidate shots of one decision use the same K keys).
	constexpr NoiseKey RolloutKey(const NoiseKey& MatchKey, std::uint32_t Sample)
	{
		NoiseKey Key = MatchKey;
		Key.Purpose = 1u + Sample;
		return Key;
	}

	// ---------------------------------------------------------------------------------------------
	// Streak-guarded per-shot draws (Q1)
	// ---------------------------------------------------------------------------------------------

	inline constexpr int kStreakWindow = 7;       // the accepted draws n-7 .. n-1 of the same shooter and channel
	inline constexpr int kStreakMaxPerEighth = 2; // an eighth occurs at most twice in any 8 consecutive draws
	inline constexpr int kStreakMaxRedraws = 32;  // candidates Sub = 0 .. 31 before the deterministic fallback
	inline constexpr int kStreakChannelCount = 8; // TipA, TipB, Elevation, Speed, Flinch, HandAim, HandSteer, HandSpeed

	// Slot 0 .. 7 of a streak-guarded channel in NoiseHistory::Channels; -1 for every other channel.
	constexpr int StreakSlot(NoiseChannel Channel)
	{
		const std::uint32_t C = static_cast<std::uint32_t>(Channel);
		if (C >= 5u && C <= 9u)
		{
			return static_cast<int>(C - 5u);
		}
		if (C >= 20u && C <= 22u)
		{
			return static_cast<int>(C - 20u) + 5;
		}
		return -1;
	}

	constexpr NoiseChannel StreakChannelAt(int Slot)
	{
		return Slot < 5 ? static_cast<NoiseChannel>(5 + Slot) : static_cast<NoiseChannel>(20 + (Slot - 5));
	}

	// Eighths floor(8 u) of the last accepted draws of one shooter and channel, oldest first.
	struct StreakHistory
	{
		std::uint8_t Count = 0;                   // 0 .. kStreakWindow
		std::uint8_t Eighths[kStreakWindow] = {};
	};

	// Streak history of every guarded channel of one shooter before draw NextIndex (part of the shooter's match
	// state; a cache of RebuildNoiseHistory, which is the definition).
	struct NoiseHistory
	{
		std::uint64_t MatchSeed = 0;
		std::uint64_t Shooter = 0;    // ShooterKey of the match stream (Purpose 0)
		std::uint32_t NextIndex = 0;  // ShooterShotIndex of the next draw
		StreakHistory Channels[kStreakChannelCount];
	};

	struct GuardedDraw
	{
		double U = 0.0;          // accepted uniform in [0, 1) (EighthOf(U) in 0 .. 7, also for the fallback)
		double Eps = 0.0;        // TruncNormal(U) (the Flinch channel uses U)
		std::uint32_t Sub = 0;   // accepted candidate (kStreakMaxRedraws = fallback used)
	};

	// Candidate u_{n,Sub} = U01(HashKeys(MatchSeed, Shooter, Channel, Index, Sub)).
	constexpr double DrawCandidate(std::uint64_t MatchSeed, std::uint64_t Shooter, NoiseChannel Channel, std::uint32_t Index, std::uint32_t Sub)
	{
		return U01(HashKeys(MatchSeed, Shooter, static_cast<std::uint64_t>(Channel), static_cast<std::uint64_t>(Index), static_cast<std::uint64_t>(Sub)));
	}

	// Eighth of a uniform in [0, 1): floor(8 U) (8 U is exact).
	constexpr int EighthOf(double U) { return static_cast<int>(U * 8.0); }

	// Accepted draw Index given the history of draws Index-7 .. Index-1 of the same (MatchSeed, Shooter, Channel).
	RB_API GuardedDraw DrawGuarded(std::uint64_t MatchSeed, std::uint64_t Shooter, NoiseChannel Channel, std::uint32_t Index, const StreakHistory& History);

	// Appends EighthOf(U), dropping the oldest entry beyond kStreakWindow.
	RB_API void PushStreak(StreakHistory& History, double U);

	// Canonical history before draw Index: recursion from draw 0 with an empty history (O(Index)). The draw sequence of
	// one (MatchSeed, Shooter, Channel) is therefore a pure function of the index.
	RB_API StreakHistory RebuildStreakHistory(std::uint64_t MatchSeed, std::uint64_t Shooter, NoiseChannel Channel, std::uint32_t Index);

	// Canonical NoiseHistory of every guarded channel before draw NextIndex.
	RB_API NoiseHistory RebuildNoiseHistory(std::uint64_t MatchSeed, std::uint64_t Shooter, std::uint32_t NextIndex);

	// Records the accepted draws of index History.NextIndex of every guarded channel and advances NextIndex. Called once
	// per revealed per-shot draw (a tip contact, or an aborted stroke that showed the ramp: 3.7, HF-B13), for every
	// channel whether the shooter used it or not, so the history stays equal to RebuildNoiseHistory.
	RB_API void AdvanceNoiseHistory(NoiseHistory& History);

	// True if History is the cache for the draw of Key: same match seed, same shooter key (a match-stream key, Purpose 0;
	// rollout keys never match and never read a history) and NextIndex == Key.ShooterShotIndex.
	constexpr bool HistoryMatchesKey(const NoiseHistory& History, const NoiseKey& Key)
	{
		return History.MatchSeed == Key.MatchSeed && History.Shooter == ShooterKey(Key) && History.NextIndex == Key.ShooterShotIndex;
	}

	// The per-shot draw of Channel for Key (3.2): match stream with StreakGuard -> DrawGuarded with the history slot of
	// the channel if HistoryMatchesKey(History, Key), otherwise with RebuildStreakHistory(Key.MatchSeed, ShooterKey(Key),
	// Channel, Key.ShooterShotIndex) (the result never depends on the cache: e.g. the diagnosis re-runs of 3.9 after the
	// post-shot AdvanceNoiseHistory, or a replay without the cached history); StreakGuard off or a rollout key -> the
	// plain candidate Sub 0 (History unused).
	RB_API GuardedDraw DrawPerShot(const NoiseKey& Key, NoiseChannel Channel, const NoiseHistory& History, bool StreakGuard);

	// Plain per-shot uniform U_c = U01(HashKeys(MatchSeed, RackIndex, ShotIndex, S, Channel, 0)) (synthetic hand
	// channels 23-26, 3.8).
	constexpr double PlainUniform(const NoiseKey& Key, NoiseChannel Channel)
	{
		return U01(HashKeys(Key.MatchSeed, static_cast<std::uint64_t>(Key.RackIndex), static_cast<std::uint64_t>(Key.ShotIndex), ShooterKey(Key),
			static_cast<std::uint64_t>(Channel), std::uint64_t{0}));
	}

	// ---------------------------------------------------------------------------------------------
	// Watchable processes (channels 1-4): band-limited sums of K cosines, evaluable at any time in O(K)
	// ---------------------------------------------------------------------------------------------

	inline constexpr int kProcessComponents = 6;     // K
	inline constexpr double kDriftBandLo = 0.15;     // [Hz] slow enough to watch and time
	inline constexpr double kDriftBandHi = 0.6;      // [Hz]
	inline constexpr double kTremorBandLo = 8.0;     // [Hz] physiological tremor
	inline constexpr double kTremorBandHi = 12.0;    // [Hz]

	// f_k = lo + (hi - lo) (k + U01(HashKeys(MatchSeed, RackIndex, ProcessShotKey, S, Channel, 2k))) / K,
	// ph_k = 2 pi U01(HashKeys(..., 2k + 1)). One process per get-down (AddressIndex), t = time since that get-down.
	struct WatchableProcess
	{
		double Frequency[kProcessComponents] = {}; // [Hz]
		double Phase[kProcessComponents] = {};     // [rad]
	};

	RB_API WatchableProcess MakeWatchableProcess(const NoiseKey& Key, NoiseChannel Channel, double BandLo, double BandHi);

	// D(t) = sqrt(2/K) SUM cos(2 pi f_k t + ph_k) (unit mean square); t = time since "down on the shot" [s].
	RB_API double ProcessValue(const WatchableProcess& Process, double Time);

	// D'(t) = -sqrt(2/K) SUM 2 pi f_k sin(2 pi f_k t + ph_k) [1/s].
	RB_API double ProcessRate(const WatchableProcess& Process, double Time);
}
