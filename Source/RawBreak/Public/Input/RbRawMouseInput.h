#pragma once

// Timestamped raw mouse reports for the stroke (ue5-realism-plan 5.4, pitfall 19): Enhanced Input aggregates
// mouse deltas per frame, which quantises the stroke speed and makes it frame-rate dependent. Owner: UE-5a.
//
// DESIGN (review R-01, verified against the UE 5.8 source, WindowsApplication.cpp): UE registers the mouse for raw
// input on the GAME window and receives WM_INPUT in the once-per-frame message pump (FWindowsApplication::PumpMessages).
// An IWindowsMessageHandler on that window therefore sees every report, but all reports of a frame arrive in one burst,
// so a QPC timestamp taken in the handler is the PUMP time, not the report time - exactly the per-frame quantisation the
// plan forbids. (UE's optional worker thread, cvar WindowsApplication.UseWorkerThreadForRawInput, enqueues RAWMOUSE
// without timestamps and bypasses message handlers.) Therefore:
//   1. Primary: this class owns a dedicated input thread (pattern of UE's FWindowsRawInputRunnable): a message-only
//      window, RegisterRawInputDevices(usage page 1 / usage 2, hwndTarget = that window, flags 0), a blocking
//      GetMessage loop, and a QPC timestamp (FPlatformTime::Seconds domain) taken the moment each WM_INPUT is
//      dispatched; reports go into an SPSC ring drained by the game thread. Raw-input registration is one window per
//      device class and process, so the thread takes the mouse over from UE: the game thread FORWARDS every drained
//      delta to FSlateApplication::Get().OnRawMouseMove(DX, DY) (what UE itself does for its worker-thread mode), so
//      Enhanced Input look / aim keep working. UE re-registers its window whenever it re-enters high-precision mouse
//      mode (focus / capture changes); an IWindowsMessageHandler that sees WM_INPUT arrive on the game window detects
//      this and asks the thread to register again (reports of that frame are forwarded by UE itself, none are lost).
//      Leaving high-precision mode (cursor shown: console, alt-tab) makes UE call RIDEV_REMOVE, which ends the thread's
//      registration too; nothing is forwarded then, and the thread registers again only after WM_INPUT reappears on
//      the game window (UE back in high-precision mode).
//      The thread stops (RIDEV_REMOVE, WM_QUIT) in the destructor, before the module unloads.
//   2. Fallback (thread could not register, e.g. remote desktop, or -RbRawInputThread=0): the handler on the game
//      window; the N reports of one pump are given reconstructed times spaced by the measured report interval and
//      ending at the pump time. HasTrueTimestamps() tells the stroke component (and the F2 debug block) which one runs.
// Headless / non-Windows / -nullrhi / commandlets: Create() returns an inactive instance (IsActive() == false); the
// stroke then comes from URbStrokeComponent's scripted source (tests, cheats).

#include "CoreMinimal.h"

struct FRbRawMouseReport
{
	double Time = 0.0; // [s] FPlatformTime::Seconds() of the report
	int32 DeltaX = 0;  // counts
	int32 DeltaY = 0;  // counts (+ = mouse moved toward the user, Windows convention)
};

class RAWBREAK_API FRbRawMouseInput
{
public:
	// Registers the message handler when a Windows application with a window exists; otherwise inactive.
	static TSharedRef<FRbRawMouseInput> Create();

	~FRbRawMouseInput();

	bool IsActive() const;

	// True when reports carry per-report arrival timestamps (dedicated input thread); false for the pump-time
	// reconstruction fallback. Shown in the F2 debug block; the playtest (A9) must run with true timestamps.
	bool HasTrueTimestamps() const;

	// Moves every report received since the last call into Out (appends, time ordered). Game thread.
	int32 Drain(TArray<FRbRawMouseReport>& Out);

	// Drops pending reports (e.g. when the stroke mode starts).
	void Reset();

private:
	FRbRawMouseInput();

	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
