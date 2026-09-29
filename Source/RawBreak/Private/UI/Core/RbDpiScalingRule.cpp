#include "UI/Core/RbDpiScalingRule.h"

// Owner: M2-D.

float URbDpiScalingRule::ScaleForViewport(FIntPoint Size)
{
	return Size.Y > 0 ? static_cast<float>(Size.Y) / 1080.0f : 1.0f;
}

float URbDpiScalingRule::GetDPIScaleBasedOnSize(FIntPoint Size) const
{
	return ScaleForViewport(Size);
}
