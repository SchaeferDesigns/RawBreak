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
//      window, RegisterRawInputDevices(usage page 1 / usage 2, hwndTarget = that window, flags 0 - legacy mouse
//      messages, i.e. the buttons, keep flowing to the game window), a blocking GetMessage loop, and a QPC timestamp
//      (FPlatformTime::Seconds domain) taken the moment each WM_INPUT is dispatched; reports go into an SPSC ring
//      (FRbRawReportRing) drained by the game thread. Raw-input registration is one window per device class and
//      process, so the thread takes the mouse over from UE: the game thread FORWARDS every drained delta to
//      FSlateApplication::Get().OnRawMouseMove(DX, DY) (what UE itself does for its worker-thread mode), once per frame
//      at FCoreDelegates::OnBeginFrame (before the message pump, the same latency as UE's own path), so Enhanced Input
//      look / aim keep working. The thread registers only after an IWindowsMessageHandler saw WM_INPUT arrive on the game
//      window, i.e. while UE is in high-precision mouse mode; UE re-registers its window whenever it re-enters that mode
//      (focus / capture changes), the handler sees WM_INPUT again and asks the thread to register again (the reports of
//      that frame are delivered by UE itself; they are kept with reconstructed times, see 2). Leaving high-precision mode
//      (cursor shown: console, alt-tab) makes UE call RIDEV_REMOVE, which ends the thread's registration too; nothing
//      is forwarded then.
//      The thread stops (RIDEV_REMOVE of its own registration only, WM_QUIT, join) in the destructor; if UE is still in
//      high-precision mode, its game window is registered again so mouse look keeps working after the hand-over.
//      Absolute reports (pen tablet, touch, VM / streaming tools outside a remote-desktop session) carry no counts for the
//      stroke; UE turns them into cursor-based moves (OnMouseMove). The thread counts them and the game thread makes the
//      same OnMouseMove call once per frame, so look / aim keep working with such a device (the stroke needs a relative
//      mouse in every mode).
//   2. Fallback (thread could not start, a remote session, or -RbRawInputThread=0): the handler on the game window
//      records the reports UE delivers; the N reports of one pump get reconstructed times spaced by the measured
//      report interval and ending at the pump time (RbRawMouse::ReconstructPumpTimes). HasTrueTimestamps() tells the
//      stroke component (and the F2 debug block) which one runs; every report carries bTrueTimestamp.
// Headless / non-Windows / -nullrhi / -RenderOffscreen / commandlets / no Slate application: Create() returns an inactive
// instance (IsActive() == false); the stroke then comes from URbStrokeComponent's scripted source (tests, cheats).
// The message handler needs the Windows platform application: under -RenderOffscreen Slate runs on UE's NULL application
// (FNullPlatformApplicationMisc), so even a forced instance (tests) installs no handler and forwards nothing there.
// One instance per process (raw input is per process): Create() returns the live instance while one exists.

#include "CoreMinimal.h"
#include "Containers/CircularQueue.h"

#include <atomic>

struct FRbRawMouseReport
{
	double Time = 0.0; // [s] FPlatformTime::Seconds() of the report
	int32 DeltaX = 0;  // counts
	int32 DeltaY = 0;  // counts (+ = mouse moved toward the user, Windows convention)
	bool bTrueTimestamp = false; // stamped on arrival by the input thread (else reconstructed from the pump time)
};

// Lock-free single-producer single-consumer ring (input thread -> game thread). Order preserved; when full the NEWEST
// report is dropped and counted (the game thread drains every frame, so 4096 reports = 4 s at 1 kHz never fill it).
class RAWBREAK_API FRbRawReportRing
{
public:
	explicit FRbRawReportRing(uint32 Capacity = 4096);

	bool Push(const FRbRawMouseReport& Report); // producer thread only
	bool Pop(FRbRawMouseReport& Out);           // consumer thread only
	uint32 GetDropped() const { return Dropped.load(std::memory_order_relaxed); }

private:
	TCircularQueue<FRbRawMouseReport> Queue;
	std::atomic<uint32> Dropped{0};
};

namespace RbRawMouse
{
	// Fallback time reconstruction (Docs/ue-architecture.md 6.2.1 item 3): Count reports delivered in ONE message pump at
	// PumpTime get times spaced by Interval, the last one at PumpTime. The spacing shrinks to (PumpTime - PreviousPumpTime) /
	// Count when the reports would otherwise reach back to the previous pump (PreviousPumpTime <= 0: no previous pump), so
	// times stay strictly increasing across pumps. Returns the spacing used.
	RAWBREAK_API double ReconstructPumpTimes(double PumpTime, double PreviousPumpTime, double Interval, int32 Count, double* OutTimes);

	// Running estimate of the report interval from pump batches: a pump period of P with Count reports of a moving mouse
	// means reports every P / Count. Only batches with at least 4 reports update it (start / stop of a motion under-fills
	// a frame); exponential average, clamped to [1/8000, 1/125] s (the USB polling range).
	RAWBREAK_API double UpdateIntervalEstimate(double Estimate, double PumpTime, double PreviousPumpTime, int32 Count);
}

struct FRbRawMouseInputOptions
{
	bool bAllowThread = true;        // false = fallback (handler + reconstruction); -RbRawInputThread=0
	bool bInstallHandler = true;     // IWindowsMessageHandler on the game window (needs a Windows Slate application)
	bool bForwardToSlate = true;     // forward the thread's deltas to FSlateApplication::OnRawMouseMove
	bool bForceActive = false;       // tests: start the thread even headless (it never registers without a handler)
};

class RAWBREAK_API FRbRawMouseInput
{
public:
	// The process instance (created on first use, shared while alive). Reads -RbRawInputThread=0; inactive headless.
	static TSharedRef<FRbRawMouseInput> Create();

	// A separate instance with explicit options (automation tests: thread start / stop, injected reports).
	static TSharedRef<FRbRawMouseInput> CreateWithOptions(const FRbRawMouseInputOptions& Options);

	~FRbRawMouseInput();

	bool IsActive() const;

	// True when reports carry per-report arrival timestamps (dedicated input thread); false for the pump-time
	// reconstruction fallback. Shown in the F2 debug block; the playtest (A9) must run with true timestamps.
	bool HasTrueTimestamps() const;

	// The input thread runs (message-only window created). Its raw-input registration is taken on the first WM_INPUT
	// UE delivers to the game window (high-precision mouse mode).
	bool IsThreadRunning() const;

	// Moves every report received since the last call into Out (appends, time ordered). Game thread.
	int32 Drain(TArray<FRbRawMouseReport>& Out);

	// Drops pending reports (e.g. when the stroke mode starts).
	void Reset();

	// Test hook: posts a synthetic report to the input thread, which stamps and queues it exactly like a WM_INPUT
	// (thread -> ring -> game thread path without a physical mouse). bAbsolute: an absolute report (tablet / touch), which
	// the thread only counts. False without a running thread.
	bool InjectTestReport(int32 DeltaX, int32 DeltaY, bool bAbsolute = false);

	// Counters for the debug block and the A9 log check.
	struct FStats
	{
		uint64 ThreadReports = 0;      // stamped on arrival by the thread
		uint64 EngineReports = 0;      // delivered by UE on the game window (reconstructed times)
		uint64 AbsoluteReports = 0;    // absolute reports the thread saw (forwarded as cursor moves, no stroke samples)
		uint32 Registrations = 0;      // times the thread took the mouse over
		uint32 Dropped = 0;            // ring overflow
		double IntervalEstimate = 0.0; // [s] measured report interval
		bool bMessageHandler = false;  // IWindowsMessageHandler installed on the Windows application
	};
	FStats GetStats() const;

private:
	FRbRawMouseInput();

	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
