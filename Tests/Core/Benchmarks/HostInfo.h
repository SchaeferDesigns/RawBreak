#pragma once

// Owner: WP-10 (validation & benchmarks). Host facts for the performance tests (Tests/Core/Benchmarks), kept out of the test
// translation units that include the core headers (the platform headers define macros).

namespace bench
{
	// CPU time consumed by the calling thread [s] (Windows GetThreadTimes, POSIX CLOCK_THREAD_CPUTIME_ID; 0 if unavailable).
	double ThreadCpuSeconds();

	// Physical cores of the fastest class: on a hybrid CPU the performance cores (Windows EfficiencyClass), else every physical
	// core; std::thread::hardware_concurrency() / 2 (at least 1) where the platform does not tell.
	int PerformanceCores();
}
