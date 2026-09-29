#pragma once

// P1 of the M1 playtest (Docs/ue-architecture.md 18.3): after the cue contact the view stays calm on the shot. M1 routed EVERY
// look input to the head once the stroke component left Down (ARbPlayerCharacter::HandleLook, rig DownOnShot branch), so the
// forward follow-through of the mouse stroke - often with the Stroke button still held - pitched / yawed the view away from the
// balls. The gate decides which look input is a deliberate head movement:
//   * mouse motion while the Stroke button is held is ALWAYS the stroke (never look), also after the contact;
//   * after the button is released (or at the contact without a held button) look stays closed for QuietSeconds;
//   * then it opens only when the mouse travels more than DeadZoneCm within WindowSeconds (a deliberate new move) and fades in
//     over FadeInSeconds (no jump); smaller motion is dropped, not accumulated;
//   * right click still stands up at any time (not the gate's business).
// Works in cm of mouse travel (RbAimResponse::CountsToCm) and timestamps, so the result does not depend on the frame rate.
// Owner: M2-F (stub by the M2 architect step: passes everything through; TODO(M2-F)).

#include "CoreMinimal.h"

struct FRbLookIntentParams
{
	double QuietSeconds = 0.25;
	double DeadZoneCm = 1.5;
	double WindowSeconds = 0.40;
	double FadeInSeconds = 0.20;
};

class RAWBREAK_API FRbLookIntentGate
{
public:
	FRbLookIntentParams Params;

	// Closes the gate at the contact (NowSeconds in the input clock).
	void Arm(double NowSeconds);
	// Back to normal look (new address, standing up).
	void Disarm();
	void SetStrokeHeld(bool bHeld, double NowSeconds);

	// The part of a look delta [cm of mouse travel] that turns the head.
	FVector2D Filter(const FVector2D& DeltaCm, double NowSeconds);

	bool IsArmed() const { return bArmed; }
	bool IsOpen() const { return !bArmed || bOpen; }

private:
	bool bArmed = false;
	bool bOpen = false;
	bool bStrokeHeld = false;
	double QuietSince = 0.0;
};
