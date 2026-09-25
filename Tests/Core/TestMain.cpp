#include "rbtest.h"

#include <cstring>

int main(int argc, char** argv)
{
	const char* Filter = argc > 1 ? argv[1] : nullptr;
	int Ran = 0;
	int FailedTests = 0;

	for (const rbtest::TestCase& Test : rbtest::Registry())
	{
		if (Filter && !std::strstr(Test.Name, Filter))
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
