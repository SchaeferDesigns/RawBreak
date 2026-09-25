#pragma once

// Fixed-capacity, allocation-free vector used everywhere inside the event loop.
// Owner: WP-0 (architecture, frozen). Header-only.

#include "rb/Config.h"

namespace rb
{
	// Contiguous storage of up to Capacity elements of a default-constructible, copyable T.
	// Never allocates. PushBack on a full vector returns false and changes nothing, so callers can
	// raise a capacity diagnostic instead of corrupting memory.
	template <class T, int Capacity>
	class FixedVector
	{
		static_assert(Capacity > 0, "FixedVector needs a positive capacity");

	public:
		static constexpr int kCapacity = Capacity;

		constexpr int Size() const { return Num; }
		constexpr bool IsEmpty() const { return Num == 0; }
		constexpr bool IsFull() const { return Num >= Capacity; }
		constexpr void Clear() { Num = 0; }

		constexpr bool PushBack(const T& Value)
		{
			if (Num >= Capacity)
			{
				return false;
			}
			Items[Num] = Value;
			++Num;
			return true;
		}

		constexpr void PopBack()
		{
			if (Num > 0)
			{
				--Num;
			}
		}

		// Order-preserving removal (deterministic iteration order is part of the contract).
		constexpr void RemoveAt(int Index)
		{
			for (int i = Index; i + 1 < Num; ++i)
			{
				Items[i] = Items[i + 1];
			}
			--Num;
		}

		// Shrinks or grows (value-initialising new slots) up to Capacity; returns false if too large.
		constexpr bool Resize(int NewCount)
		{
			if (NewCount < 0 || NewCount > Capacity)
			{
				return false;
			}
			for (int i = Num; i < NewCount; ++i)
			{
				Items[i] = T{};
			}
			Num = NewCount;
			return true;
		}

		constexpr T& operator[](int Index) { return Items[Index]; }
		constexpr const T& operator[](int Index) const { return Items[Index]; }

		constexpr T& Back() { return Items[Num - 1]; }
		constexpr const T& Back() const { return Items[Num - 1]; }

		constexpr T* Data() { return Items; }
		constexpr const T* Data() const { return Items; }

		constexpr T* begin() { return Items; }
		constexpr T* end() { return Items + Num; }
		constexpr const T* begin() const { return Items; }
		constexpr const T* end() const { return Items + Num; }

	private:
		T Items[Capacity] = {};
		int Num = 0;
	};
}
