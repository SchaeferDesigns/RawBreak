#include "Player/RbLookIntentGate.h"

// Owner: M2-F. The gate of the header (P1).

void FRbLookIntentGate::Arm(double NowSeconds)
{
	bArmed = true;
	bOpen = false;
	QuietSince = NowSeconds;
	OpenedAt = 0.0;
	Recent.Reset();
}

void FRbLookIntentGate::Disarm()
{
	bArmed = false;
	bOpen = false;
	Recent.Reset();
}

void FRbLookIntentGate::SetStrokeHeld(bool bHeld, double NowSeconds)
{
	if (bStrokeHeld && !bHeld)
	{
		QuietSince = FMath::Max(QuietSince, NowSeconds); // the quiet period starts at the release
	}
	if (bHeld && bArmed && !bOpen)
	{
		Recent.Reset(); // a new press: the stroke owns the mouse again
	}
	bStrokeHeld = bHeld;
}

double FRbLookIntentGate::GetOpenFraction(double NowSeconds) const
{
	if (!bArmed)
	{
		return 1.0;
	}
	if (!bOpen)
	{
		return 0.0;
	}
	return Params.FadeInSeconds > 0.0 ? FMath::Clamp((NowSeconds - OpenedAt) / Params.FadeInSeconds, 0.0, 1.0) : 1.0;
}

FVector2D FRbLookIntentGate::Filter(const FVector2D& DeltaCm, double NowSeconds)
{
	if (!bArmed)
	{
		return DeltaCm;
	}
	if (bOpen)
	{
		return DeltaCm * GetOpenFraction(NowSeconds);
	}
	// Closed: the stroke owns the mouse while its button is held, and the quiet period after the contact / release drops everything
	// (the follow-through and the hand coming to rest).
	if (bStrokeHeld || NowSeconds < QuietSince + Params.QuietSeconds)
	{
		Recent.Reset();
		return FVector2D::ZeroVector;
	}
	// A deliberate new move: more than the dead zone of path length within the window. Older motion leaves the window unused.
	// Every delta of the window counts, however many frames it spans (no cap on the count: the decision must not depend on the
	// frame rate; the window pruning below bounds the array).
	const double Travel = DeltaCm.Size();
	if (Travel > 0.0)
	{
		Recent.Add({NowSeconds, Travel});
	}
	int32 FirstInWindow = 0;
	while (FirstInWindow < Recent.Num() && Recent[FirstInWindow].Time < NowSeconds - Params.WindowSeconds)
	{
		++FirstInWindow;
	}
	if (FirstInWindow > 0)
	{
		Recent.RemoveAt(0, FirstInWindow, EAllowShrinking::No);
	}
	double Sum = 0.0;
	for (const FMotion& Motion : Recent)
	{
		Sum += Motion.TravelCm;
	}
	if (Sum > Params.DeadZoneCm)
	{
		bOpen = true;
		OpenedAt = NowSeconds;
		Recent.Reset();
	}
	return FVector2D::ZeroVector; // the motion that opened the gate only proves the intent
}
