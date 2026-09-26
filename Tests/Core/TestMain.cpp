#include "rbtest.h"

#include <cstring>

// Filters (Docs/architecture.md 18): every argument is a substring of the test name; a leading '-' excludes. A test
// runs when its name contains every include and no exclude, e.g. "MOT_", "Integ_", "-Integ_ -_Slow_".
static bool Selected(const char* Name, int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		const char* Filter = argv[i];
		const bool Exclude = Filter[0] == '-' && Filter[1] != '\0';
		const bool Contains = std::strstr(Name, Exclude ? Filter + 1 : Filter) != nullptr;
		if (Contains == Exclude)
		{
			return false;
		}
	}
	return true;
}

int main(int argc, char** argv)
{
	int Ran = 0;
	int FailedTests = 0;

	for (const rbtest::TestCase& Test : rbtest::Registry())
	{
		if (!Selected(Test.Name, argc, argv))
		{
			continue;
		}
		const int FailuresBefore = rbtest::FailureCount();
		Test.Fn();
		++Ran;
		if (rbtest::FailureCount() != FailuresBefore)
		{
			++FailedTests;
			std::printf("[FAIL] %s\n", Test.Name);
		}
	}

	std::printf("%d test(s) run, %d failed, %d failed check(s)\n", Ran, FailedTests, rbtest::FailureCount());
	return FailedTests == 0 ? 0 : 1;
}
