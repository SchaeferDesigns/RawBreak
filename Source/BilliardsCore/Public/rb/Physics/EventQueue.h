#pragma once

// Deterministic event priority queue for the simulator: fixed-capacity binary min-heap with lazy
// deletion by per-ball version stamps (prior-art 5.10) and a strict total order (collisions 3.7).
// Owner: WP-6a (simulator core loop). Header-only (infrastructure; no allocation).
//
// Order key (lexicographic, exact double comparison, no epsilon grouping):
//   (Time, Tier, BallA, BallB, FeatureKind, FeatureIndex, FeatureSub)
// Valid entries are unique under this key (one live prediction per end slot, table slot, pair slot
// and tip slot), so pop order is independent of insertion order and of Compact(). Exactly equal times
// that share a ball never depend on this order: the simulator merges them into one island
// (Docs/architecture.md 8.3, collisions 3.7); equal-time events of disjoint balls commute.

#include "rb/Config.h"

#include <cstdint>

namespace rb
{
	// Tie-break tiers for exactly equal times (collisions 3.7 order; the cue strike first).
	enum class EventTier : std::uint8_t
	{
		Strike = 0,     // cue strikes at t = 0, tip contacts (any ball)
		BallBall = 1,   // ball-ball impacts, touching/pressing contacts
		Cushion = 2,    // nose, jaw arc, facing face/edge, rail top
		Pocket = 3,     // drop edge, liner, rim, capture, pocket exit
		Slate = 4,      // landing / slate impact
		Boundary = 5,   // outer boundary, lamp apex
		Transition = 6, // sliding -> rolling -> spinning -> stationary
	};

	enum class QueuedEventKind : std::uint8_t
	{
		CueStrike,    // BallA = struck ball, FeatureIndex = strike index
		TipContact,   // follow-through tip reaches BallA (struck ball or any other ball); FeatureIndex = strike index
		BallBall,
		TableFeature, // FeatureKind / FeatureIndex / FeatureSub = rb::TableFeatureKind / Index / SubIndex (rb/Physics/Detect.h)
		Transition,   // end slot: motion transition, slate landing (Airborne) or pivot end (PocketPivot)
	};

	inline constexpr std::uint8_t kNoBallSlot = 0xFF;

	struct QueuedEvent
	{
		double Time = 0.0;                 // absolute [s]
		EventTier Tier = EventTier::Transition;
		QueuedEventKind Kind = QueuedEventKind::Transition;
		std::uint8_t BallA = kNoBallSlot;  // lower ball id
		std::uint8_t BallB = kNoBallSlot;  // higher ball id (ball-ball) or kNoBallSlot
		std::uint8_t FeatureKind = 0;
		std::uint8_t FeatureIndex = 0;
		std::uint8_t FeatureSub = 0;
		std::uint8_t Flags = 0;            // rb::ContactFlags
		std::uint32_t VersionA = 0;        // version stamp of BallA at prediction time
		std::uint32_t VersionB = 0;        // version stamp of BallB at prediction time
	};

	constexpr bool EventPrecedes(const QueuedEvent& a, const QueuedEvent& b)
	{
		if (a.Time != b.Time) return a.Time < b.Time;
		if (a.Tier != b.Tier) return a.Tier < b.Tier;
		if (a.BallA != b.BallA) return a.BallA < b.BallA;
		if (a.BallB != b.BallB) return a.BallB < b.BallB;
		if (a.FeatureKind != b.FeatureKind) return a.FeatureKind < b.FeatureKind;
		if (a.FeatureIndex != b.FeatureIndex) return a.FeatureIndex < b.FeatureIndex;
		return a.FeatureSub < b.FeatureSub;
	}

	// Capacity: live entries <= 2 kMaxBalls (end + table slot per ball) + kMaxBallPairs + kMaxStrikes
	// (one tip slot per moving cue: the earliest tip contact over all balls) = 326, plus stale entries;
	// when full, Compact() with the validity predicate before pushing again.
	inline constexpr int kEventHeapCapacity = 512; // 24 B per entry: 12 KB (prior-art P5 budget, architecture 12)

	template <int Capacity>
	class EventHeap
	{
	public:
		constexpr int Size() const { return Num; }
		constexpr bool IsEmpty() const { return Num == 0; }
		constexpr bool IsFull() const { return Num >= Capacity; }
		constexpr void Clear() { Num = 0; }

		// Returns false when full (call Compact first).
		constexpr bool Push(const QueuedEvent& Event)
		{
			if (Num >= Capacity)
			{
				return false;
			}
			int i = Num++;
			Items[i] = Event;
			while (i > 0)
			{
				const int Parent = (i - 1) / 2;
				if (!EventPrecedes(Items[i], Items[Parent]))
				{
					break;
				}
				Swap(i, Parent);
				i = Parent;
			}
			return true;
		}

		constexpr const QueuedEvent& Top() const { return Items[0]; }

		constexpr void Pop()
		{
			if (Num == 0)
			{
				return;
			}
			--Num;
			if (Num == 0)
			{
				return;
			}
			Items[0] = Items[Num];
			SiftDown(0);
		}

		// Removes every entry for which IsValid(entry) is false; O(n) rebuild, deterministic.
		template <class Predicate>
		constexpr int Compact(Predicate IsValid)
		{
			int Kept = 0;
			for (int i = 0; i < Num; ++i)
			{
				if (IsValid(Items[i]))
				{
					Items[Kept++] = Items[i];
				}
			}
			Num = Kept;
			for (int i = Num / 2 - 1; i >= 0; --i)
			{
				SiftDown(i);
			}
			return Num;
		}

	private:
		constexpr void Swap(int i, int j)
		{
			const QueuedEvent Tmp = Items[i];
			Items[i] = Items[j];
			Items[j] = Tmp;
		}

		constexpr void SiftDown(int i)
		{
			for (;;)
			{
				const int Left = 2 * i + 1;
				const int Right = Left + 1;
				int Best = i;
				if (Left < Num && EventPrecedes(Items[Left], Items[Best]))
				{
					Best = Left;
				}
				if (Right < Num && EventPrecedes(Items[Right], Items[Best]))
				{
					Best = Right;
				}
				if (Best == i)
				{
					return;
				}
				Swap(i, Best);
				i = Best;
			}
		}

		QueuedEvent Items[Capacity] = {};
		int Num = 0;
	};
}
