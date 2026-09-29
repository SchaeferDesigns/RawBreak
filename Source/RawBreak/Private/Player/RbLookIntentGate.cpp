#include "Player/RbLookIntentGate.h"

// Owner: M2-F.

void FRbLookIntentGate::Arm(double NowSeconds)
{
	bArmed = true;
	bOpen = false;
	QuietSince = NowSeconds;
}

void FRbLookIntentGate::Disarm()
{
	bArmed = false;
	bOpen = false;
}

void FRbLookIntentGate::SetStrokeHeld(bool bHeld, double NowSeconds)
{
	if (bStrokeHeld && !bHeld)
	{
		QuietSince = NowSeconds;
	}
	bStrokeHeld = bHeld;
}

FVector2D FRbLookIntentGate::Filter(const FVector2D& DeltaCm, double /*NowSeconds*/)
{
	// TODO(M2-F): quiet period, dead zone within the window, fade-in (header). The stub keeps M1's behaviour.
	return DeltaCm;
}
