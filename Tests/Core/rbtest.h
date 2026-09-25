#pragma once

// Minimal, dependency-free test harness (no exceptions, no RTTI).
//
//   RB_TEST(Suite_Name) { RB_CHECK(x == 1); RB_CHECK_NEAR(a, b, 1e-9); RB_REQUIRE(ptr); }
//
// Run: BilliardsCoreTests [substring-filter]

#include <cmath>
#include <cstdio>
#include <vector>

namespace rbtest
{
	using TestFn = void (*)();

	struct TestCase
	{
		const char* Name;
		TestFn Fn;
	};

	inline std::vector<TestCase>& Registry()
	{
		static std::vector<TestCase> Tests;
		return Tests;
	}

	inline int& FailureCount()
	{
		static int Count = 0;
		return Count;
	}

	struct Registrar
	{
		Registrar(const char* Name, TestFn Fn) { Registry().push_back({Name, Fn}); }
	};

	inline void ReportFailure(const char* File, int Line, const char* Expr)
	{
		++FailureCount();
		std::printf("  FAILED %s(%d): %s\n", File, Line, Expr);
	}

	inline void ReportNearFailure(const char* File, int Line, const char* A, const char* B, double ValA, double ValB, double Tol)
	{
		++FailureCount();
		std::printf("  FAILED %s(%d): |%s - %s| <= %g  (%.12g vs %.12g, diff %.3g)\n", File, Line, A, B, Tol, ValA, ValB, std::fabs(ValA - ValB));
	}
}

#define RB_TEST_CONCAT_INNER(a, b) a##b
#define RB_TEST_CONCAT(a, b) RB_TEST_CONCAT_INNER(a, b)

#define RB_TEST(Name)                                                                              \
	static void Name();                                                                            \
	static ::rbtest::Registrar RB_TEST_CONCAT(RbTestRegistrar_, Name)(#Name, &Name);               \
	static void Name()

#define RB_CHECK(Expr)                                                                             \
	do                                                                                             \
	{                                                                                              \
		if (!(Expr)) ::rbtest::ReportFailure(__FILE__, __LINE__, #Expr);                          \
	} while (0)

#define RB_REQUIRE(Expr)                                                                           \
	do                                                                                             \
	{                                                                                              \
		if (!(Expr))                                                                               \
		{                                                                                          \
			::rbtest::ReportFailure(__FILE__, __LINE__, #Expr);                                    \
			return;                                                                                \
		}                                                                                          \
	} while (0)

#define RB_CHECK_NEAR(A, B, Tol)                                                                   \
	do                                                                                             \
	{                                                                                              \
		const double RbValA_ = static_cast<double>(A);                                             \
		const double RbValB_ = static_cast<double>(B);                                             \
		if (!(std::fabs(RbValA_ - RbValB_) <= (Tol)))                                              \
			::rbtest::ReportNearFailure(__FILE__, __LINE__, #A, #B, RbValA_, RbValB_, (Tol));      \
	} while (0)
