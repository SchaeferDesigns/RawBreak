// Owner: WP-10 (validation & benchmarks). See HostInfo.h. The only test source that includes platform headers.
#include "Benchmarks/HostInfo.h"

#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace bench
{
	double ThreadCpuSeconds()
	{
#if defined(_WIN32)
		FILETIME Creation;
		FILETIME Exit;
		FILETIME Kernel;
		FILETIME User;
		if (!GetThreadTimes(GetCurrentThread(), &Creation, &Exit, &Kernel, &User))
		{
			return 0.0;
		}
		const auto Ticks = [](const FILETIME& F) { return (static_cast<unsigned long long>(F.dwHighDateTime) << 32) | F.dwLowDateTime; };
		return static_cast<double>(Ticks(Kernel) + Ticks(User)) * 1e-7; // 100 ns ticks
#else
		timespec T{};
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &T) != 0)
		{
			return 0.0;
		}
		return static_cast<double>(T.tv_sec) + 1e-9 * static_cast<double>(T.tv_nsec);
#endif
	}

	int PerformanceCores()
	{
		const unsigned Logical = std::thread::hardware_concurrency();
		const int Fallback = Logical >= 2 ? static_cast<int>(Logical / 2) : 1;
#if defined(_WIN32)
		DWORD Length = 0;
		GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &Length);
		if (Length == 0)
		{
			return Fallback;
		}
		std::vector<unsigned char> Buffer(Length);
		auto* First = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(Buffer.data());
		if (!GetLogicalProcessorInformationEx(RelationProcessorCore, First, &Length))
		{
			return Fallback;
		}
		int Best = -1;
		int Count = 0;
		for (DWORD Offset = 0; Offset < Length;)
		{
			const auto* Info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(Buffer.data() + Offset);
			if (Info->Relationship == RelationProcessorCore)
			{
				const int Class = Info->Processor.EfficiencyClass;
				if (Class > Best)
				{
					Best = Class;
					Count = 0;
				}
				Count += Class == Best ? 1 : 0;
			}
			Offset += Info->Size;
		}
		return Count > 0 ? Count : Fallback;
#else
		return Fallback;
#endif
	}
}
