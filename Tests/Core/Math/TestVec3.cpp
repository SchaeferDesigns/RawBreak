#include "rbtest.h"

#include "rb/Math/Vec3.h"

using rb::Vec3;

RB_TEST(Vec3_CrossFollowsRightHandRule)
{
	const Vec3 z = rb::Cross(Vec3::UnitX(), Vec3::UnitY());
	RB_CHECK(z == Vec3::UnitZ());
	RB_CHECK(rb::Cross(Vec3::UnitY(), Vec3::UnitZ()) == Vec3::UnitX());
}

RB_TEST(Vec3_NormalizedHandlesZero)
{
	RB_CHECK(rb::Normalized(Vec3::Zero()) == Vec3::Zero());
	RB_CHECK_NEAR(rb::Length(rb::Normalized(Vec3{3.0, 4.0, 12.0})), 1.0, 1e-15);
}
