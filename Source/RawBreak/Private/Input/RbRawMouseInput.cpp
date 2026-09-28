#include "Input/RbRawMouseInput.h"

// Owner: UE-5a. Design: header + Docs/ue-architecture.md 6.2.1 (review R-01).

#include "RawBreak.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Null/NullPlatformApplicationMisc.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsApplication.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#endif

// ---------------------------------------------------------------------------------------------------------
// Ring and pure helpers
// ---------------------------------------------------------------------------------------------------------

FRbRawReportRing::FRbRawReportRing(uint32 Capacity)
	: Queue(Capacity + 1)
{
}

bool FRbRawReportRing::Push(const FRbRawMouseReport& Report)
{
	if (!Queue.Enqueue(Report))
	{
		Dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	return true;
}

bool FRbRawReportRing::Pop(FRbRawMouseReport& Out)
{
	return Queue.Dequeue(Out);
}

namespace RbRawMouse
{
	double ReconstructPumpTimes(double PumpTime, double PreviousPumpTime, double Interval, int32 Count, double* OutTimes)
	{
		if (Count <= 0 || !OutTimes)
		{
			return 0.0;
		}
		double Spacing = Interval > 0.0 ? Interval : 0.0;
		if (PreviousPumpTime > 0.0)
		{
			const double Period = PumpTime - PreviousPumpTime;
			Spacing = Period > 0.0 ? FMath::Min(Spacing, Period / Count) : 0.0;
		}
		for (int32 i = 0; i < Count; ++i)
		{
			OutTimes[i] = PumpTime - static_cast<double>(Count - 1 - i) * Spacing;
		}
		return Spacing;
	}

	double UpdateIntervalEstimate(double Estimate, double PumpTime, double PreviousPumpTime, int32 Count)
	{
		constexpr double MinInterval = 1.0 / 8000.0;
		constexpr double MaxInterval = 1.0 / 125.0;
		if (Count >= 4 && PreviousPumpTime > 0.0 && PumpTime > PreviousPumpTime)
		{
			const double Sample = (PumpTime - PreviousPumpTime) / Count;
			Estimate = Estimate > 0.0 ? 0.8 * Estimate + 0.2 * Sample : Sample;
		}
		if (!(Estimate > 0.0))
		{
			Estimate = 1.0 / 1000.0;
		}
		return FMath::Clamp(Estimate, MinInterval, MaxInterval);
	}
}

// ---------------------------------------------------------------------------------------------------------
// Input thread (Windows)
// ---------------------------------------------------------------------------------------------------------

#if PLATFORM_WINDOWS
namespace
{
	constexpr UINT WM_RB_QUIT = WM_APP + 0x5B0;
	constexpr UINT WM_RB_REGISTER = WM_APP + 0x5B1;
	constexpr UINT WM_RB_UNREGISTER = WM_APP + 0x5B2;
	constexpr UINT WM_RB_INJECT = WM_APP + 0x5B3;
	constexpr UINT WM_RB_INJECT_ABSOLUTE = WM_APP + 0x5B4;

	std::atomic<uint32> GRbRawInputWindowCounter{0};

	// The Slate application runs on the Windows platform application (under -RenderOffscreen UE creates its NULL application,
	// FNullPlatformApplicationMisc: casting that to FWindowsApplication and adding a message handler would corrupt memory).
	bool HasWindowsSlateApplication()
	{
		return FSlateApplication::IsInitialized() && !FNullPlatformApplicationMisc::IsUsingNullApplication();
	}

	// Parses one WM_INPUT. True for a relative mouse report with motion; bAbsolute for absolute (tablet / remote) reports.
	bool ReadRelativeMouse(LPARAM LParam, int32& OutDX, int32& OutDY, bool& bOutAbsolute)
	{
		bOutAbsolute = false;
		alignas(16) uint8 Buffer[256];
		UINT Size = 0;
		if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(LParam), RID_INPUT, nullptr, &Size, sizeof(RAWINPUTHEADER)) != 0 || Size == 0 ||
			Size > sizeof(Buffer))
		{
			return false;
		}
		if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(LParam), RID_INPUT, Buffer, &Size, sizeof(RAWINPUTHEADER)) != Size)
		{
			return false;
		}
		const RAWINPUT* Raw = reinterpret_cast<const RAWINPUT*>(Buffer);
		if (Raw->header.dwType != RIM_TYPEMOUSE)
		{
			return false;
		}
		const RAWMOUSE& Mouse = Raw->data.mouse;
		if ((Mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == MOUSE_MOVE_ABSOLUTE)
		{
			bOutAbsolute = true;
			return false;
		}
		OutDX = Mouse.lLastX;
		OutDY = Mouse.lLastY;
		return OutDX != 0 || OutDY != 0;
	}

	class FRbRawInputRunnable final : public FRunnable
	{
	public:
		explicit FRbRawInputRunnable(FRbRawReportRing& InRing)
			: Ring(InRing)
		{
			ReadyEvent = FPlatformProcess::GetSynchEventFromPool(true);
			ClassName = FString::Printf(TEXT("RawBreakRawInput_%u_%u"), FPlatformProcess::GetCurrentProcessId(), GRbRawInputWindowCounter.fetch_add(1));
		}

		virtual ~FRbRawInputRunnable() override
		{
			FPlatformProcess::ReturnSynchEventToPool(ReadyEvent);
			ReadyEvent = nullptr;
		}

		bool WaitReady(uint32 Milliseconds) { return ReadyEvent->Wait(Milliseconds); }
		HWND GetWindow() const { return Window.load(); }

		// Game thread -> input thread requests (asynchronous, never block the game thread).
		void RequestRegister()
		{
			if (HWND W = Window.load(); W && !bRegisterPending.exchange(true))
			{
				if (!::PostMessageW(W, WM_RB_REGISTER, 0, 0))
				{
					bRegisterPending.store(false); // not queued: the next WM_INPUT on the game window asks again
				}
			}
		}
		bool PostInject(int32 DX, int32 DY, bool bAbsolute)
		{
			HWND W = Window.load();
			return W && ::PostMessageW(W, bAbsolute ? WM_RB_INJECT_ABSOLUTE : WM_RB_INJECT, static_cast<WPARAM>(static_cast<intptr_t>(DX)),
				static_cast<LPARAM>(static_cast<intptr_t>(DY))) != 0;
		}

		std::atomic<uint32> Registrations{0};
		std::atomic<uint64> Reports{0};
		std::atomic<uint64> AbsoluteReports{0};
		std::atomic<bool> bOwnsRegistration{false};

		virtual uint32 Run() override
		{
			const HINSTANCE Instance = ::GetModuleHandleW(nullptr);
			WNDCLASSEXW Class{};
			Class.cbSize = sizeof(Class);
			Class.lpfnWndProc = &FRbRawInputRunnable::StaticWndProc;
			Class.hInstance = Instance;
			Class.lpszClassName = *ClassName;
			if (!::RegisterClassExW(&Class))
			{
				UE_LOG(LogRawBreak, Warning, TEXT("RbRawMouse: RegisterClassEx failed (%u)"), ::GetLastError());
				ReadyEvent->Trigger();
				return 1;
			}
			HWND W = ::CreateWindowExW(0, *ClassName, *ClassName, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, Instance, nullptr);
			if (!W)
			{
				UE_LOG(LogRawBreak, Warning, TEXT("RbRawMouse: CreateWindowEx failed (%u)"), ::GetLastError());
				::UnregisterClassW(*ClassName, Instance);
				ReadyEvent->Trigger();
				return 1;
			}
			::SetWindowLongPtrW(W, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
			Window.store(W);
			ReadyEvent->Trigger();

			// A Stop() that ran before the window existed could not post WM_RB_QUIT: it set the flag instead (both sides use
			// sequentially consistent atomics, so either Stop() sees the window or this sees the flag).
			if (!bStopRequested.load())
			{
				MSG Msg;
				while (::GetMessageW(&Msg, nullptr, 0, 0) > 0)
				{
					::TranslateMessage(&Msg);
					::DispatchMessageW(&Msg);
				}
			}

			UnregisterOwn();
			Window.store(nullptr);
			::DestroyWindow(W);
			::UnregisterClassW(*ClassName, Instance);
			return 0;
		}

		virtual void Stop() override
		{
			bStopRequested.store(true);
			if (HWND W = Window.load())
			{
				::PostMessageW(W, WM_RB_QUIT, 0, 0);
			}
		}

	private:
		static LRESULT CALLBACK StaticWndProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
		{
			FRbRawInputRunnable* Self = reinterpret_cast<FRbRawInputRunnable*>(::GetWindowLongPtrW(Hwnd, GWLP_USERDATA));
			return Self ? Self->WndProc(Hwnd, Msg, WParam, LParam) : ::DefWindowProcW(Hwnd, Msg, WParam, LParam);
		}

		LRESULT WndProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
		{
			switch (Msg)
			{
			case WM_INPUT:
			{
				// The stamp is taken FIRST: the time this report reached the process (the thread sleeps in GetMessage).
				const double Now = FPlatformTime::Seconds();
				int32 DX = 0;
				int32 DY = 0;
				bool bAbsolute = false;
				if (ReadRelativeMouse(LParam, DX, DY, bAbsolute))
				{
					FRbRawMouseReport Report;
					Report.Time = Now;
					Report.DeltaX = DX;
					Report.DeltaY = DY;
					Report.bTrueTimestamp = true;
					Ring.Push(Report);
					Reports.fetch_add(1, std::memory_order_relaxed);
				}
				else if (bAbsolute)
				{
					AbsoluteReports.fetch_add(1, std::memory_order_relaxed); // the game thread turns them into a cursor move
				}
				::DefWindowProcW(Hwnd, Msg, WParam, LParam); // RIM_INPUT cleanup
				return 0;
			}
			case WM_RB_REGISTER:
			{
				RAWINPUTDEVICE Device;
				Device.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
				Device.usUsage = 0x02;     // HID_USAGE_GENERIC_MOUSE
				Device.dwFlags = 0;        // legacy messages (buttons, WM_MOUSEMOVE) keep flowing to the game window
				Device.hwndTarget = Hwnd;
				if (::RegisterRawInputDevices(&Device, 1, sizeof(Device)))
				{
					bOwnsRegistration.store(true);
					Registrations.fetch_add(1);
				}
				bRegisterPending.store(false);
				return 0;
			}
			case WM_RB_UNREGISTER:
				UnregisterOwn();
				return 0;
			case WM_RB_INJECT:
			{
				FRbRawMouseReport Report;
				Report.Time = FPlatformTime::Seconds();
				Report.DeltaX = static_cast<int32>(static_cast<intptr_t>(WParam));
				Report.DeltaY = static_cast<int32>(static_cast<intptr_t>(LParam));
				Report.bTrueTimestamp = true;
				Ring.Push(Report);
				Reports.fetch_add(1, std::memory_order_relaxed);
				return 0;
			}
			case WM_RB_INJECT_ABSOLUTE:
				AbsoluteReports.fetch_add(1, std::memory_order_relaxed); // same path as an absolute WM_INPUT
				return 0;
			case WM_RB_QUIT:
				::PostQuitMessage(0);
				return 0;
			default:
				break;
			}
			return ::DefWindowProcW(Hwnd, Msg, WParam, LParam);
		}

		// Removes the mouse registration only while it targets OUR window (a RIDEV_REMOVE is process-wide and would
		// otherwise also remove UE's registration of the game window).
		void UnregisterOwn()
		{
			HWND W = Window.load();
			UINT Num = 0;
			if (W && ::GetRegisteredRawInputDevices(nullptr, &Num, sizeof(RAWINPUTDEVICE)) == 0 && Num > 0)
			{
				TArray<RAWINPUTDEVICE> Devices;
				Devices.SetNumZeroed(static_cast<int32>(Num));
				const UINT Got = ::GetRegisteredRawInputDevices(Devices.GetData(), &Num, sizeof(RAWINPUTDEVICE));
				for (UINT i = 0; i < Got && i < Num; ++i)
				{
					if (Devices[i].usUsagePage == 0x01 && Devices[i].usUsage == 0x02 && Devices[i].hwndTarget == W)
					{
						RAWINPUTDEVICE Remove;
						Remove.usUsagePage = 0x01;
						Remove.usUsage = 0x02;
						Remove.dwFlags = RIDEV_REMOVE;
						Remove.hwndTarget = nullptr;
						::RegisterRawInputDevices(&Remove, 1, sizeof(Remove));
					}
				}
			}
			bOwnsRegistration.store(false);
		}

		FRbRawReportRing& Ring;
		FEvent* ReadyEvent = nullptr;
		FString ClassName;
		std::atomic<HWND> Window{nullptr};
		std::atomic<bool> bRegisterPending{false};
		std::atomic<bool> bStopRequested{false};
	};
}
#endif // PLATFORM_WINDOWS

// ---------------------------------------------------------------------------------------------------------
// FImpl
// ---------------------------------------------------------------------------------------------------------

struct FRbRawMouseInput::FImpl
#if PLATFORM_WINDOWS
	: public IWindowsMessageHandler
#endif
{
	FRbRawMouseInputOptions Options;
	bool bActive = false;
	bool bThreadMode = false;
	FRbRawReportRing Ring;
	TArray<FRbRawMouseReport> Pending;   // game thread: reports not yet drained, time ordered
	TArray<FRbRawMouseReport> PumpBatch; // game thread: reports UE delivered in the current pump (fallback path)
	double LastPumpTime = 0.0;
	double IntervalEstimate = 1.0 / 1000.0;
	int64 ForwardX = 0; // thread deltas not yet forwarded to Slate (game thread)
	int64 ForwardY = 0;
	uint64 EngineReports = 0;
	uint64 AbsoluteForwarded = 0; // thread's absolute-report count already turned into a cursor move (game thread)
	bool bAbsoluteWarned = false;
	bool bFirstThreadReportLogged = false;
	bool bHandlerInstalled = false;
	TArray<double> TimesScratch; // fallback time reconstruction (reused: an 8 kHz mouse sends > 100 reports per frame)
	FDelegateHandle BeginFrameHandle;

	static constexpr int32 MaxPending = 16384;

	virtual ~FImpl() = default;

#if PLATFORM_WINDOWS
	FWindowsApplication* WinApp = nullptr;
	HWND EngineWindow = nullptr;
	TUniquePtr<FRbRawInputRunnable> Runnable;
	FRunnableThread* Thread = nullptr;

	virtual bool ProcessMessage(HWND Hwnd, uint32 Msg, WPARAM WParam, LPARAM LParam, int32& OutResult) override
	{
		if (Msg != WM_INPUT)
		{
			return false;
		}
		// UE delivers this report on the game window: it is in high-precision mode and holds the registration. The pump
		// time is all we know (every report of this frame arrives now): fallback times, reconstructed in PumpGameThread.
		const double Now = FPlatformTime::Seconds();
		EngineWindow = Hwnd;
		int32 DX = 0;
		int32 DY = 0;
		bool bAbsolute = false;
		if (ReadRelativeMouse(LParam, DX, DY, bAbsolute))
		{
			FRbRawMouseReport Report;
			Report.Time = Now;
			Report.DeltaX = DX;
			Report.DeltaY = DY;
			Report.bTrueTimestamp = false;
			PumpBatch.Add(Report);
			++EngineReports;
		}
		if (bThreadMode && Runnable)
		{
			Runnable->RequestRegister(); // take the mouse over (again); UE forwards this pump's reports itself
		}
		return false; // UE processes the message as usual
	}
#endif

	// Once per frame before the message pump (UE's own raw input is processed in the pump that follows, so the look input
	// reaches the player with the same latency as without the thread).
	void OnBeginFrame()
	{
		PumpGameThread();
		if ((ForwardX != 0 || ForwardY != 0) && Options.bForwardToSlate && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnRawMouseMove(static_cast<int32>(ForwardX), static_cast<int32>(ForwardY));
		}
		ForwardX = 0;
		ForwardY = 0;
#if PLATFORM_WINDOWS
		// Absolute reports (tablet, touch, VM / streaming tools) that the thread took from UE: UE answers them with a
		// cursor-based OnMouseMove (WindowsApplication.cpp, WM_INPUT MOUSE_MOVE_ABSOLUTE); one call per frame carries the
		// whole cursor delta, so look / aim keep working with such a device.
		if (Runnable)
		{
			const uint64 Absolute = Runnable->AbsoluteReports.load(std::memory_order_relaxed);
			if (Absolute != AbsoluteForwarded)
			{
				AbsoluteForwarded = Absolute;
				if (Options.bForwardToSlate && FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().OnMouseMove();
				}
			}
		}
#endif
	}

	void PumpGameThread()
	{
		check(IsInGameThread());
		// 1. True-timestamped reports of the input thread: keep them for the stroke; their deltas are forwarded to Slate at
		//    the next OnBeginFrame (UE sees no WM_INPUT on its window while the thread holds the registration). Forwarding
		//    never happens inside a Drain during the world tick.
		bool bThreadReports = false;
		FRbRawMouseReport Report;
		while (Ring.Pop(Report))
		{
			Pending.Add(Report);
			ForwardX += Report.DeltaX;
			ForwardY += Report.DeltaY;
			bThreadReports = true;
		}
		if (bThreadReports && !bFirstThreadReportLogged && Options.bInstallHandler)
		{
			bFirstThreadReportLogged = true;
			UE_LOG(LogRawBreak, Display, TEXT("RbRawMouse: TRUE per-report timestamps - first reports stamped on arrival by the raw-input thread"));
		}

		// 2. Reports UE delivered in one pump (fallback, or the frame in which the thread re-registers).
		if (PumpBatch.Num() > 0)
		{
			const double PumpTime = PumpBatch.Last().Time;
			IntervalEstimate = RbRawMouse::UpdateIntervalEstimate(IntervalEstimate, PumpTime, LastPumpTime, PumpBatch.Num());
			TimesScratch.SetNumUninitialized(PumpBatch.Num(), EAllowShrinking::No);
			RbRawMouse::ReconstructPumpTimes(PumpTime, LastPumpTime, IntervalEstimate, PumpBatch.Num(), TimesScratch.GetData());
			for (int32 i = 0; i < PumpBatch.Num(); ++i)
			{
				PumpBatch[i].Time = TimesScratch[i];
				Pending.Add(PumpBatch[i]);
			}
			LastPumpTime = PumpTime;
			PumpBatch.Reset();
			if (bThreadReports)
			{
				Pending.StableSort([](const FRbRawMouseReport& A, const FRbRawMouseReport& B) { return A.Time < B.Time; });
			}
		}

#if PLATFORM_WINDOWS
		if (Runnable && Runnable->AbsoluteReports.load(std::memory_order_relaxed) > 0 && !bAbsoluteWarned)
		{
			bAbsoluteWarned = true;
			UE_LOG(LogRawBreak, Warning, TEXT("RbRawMouse: absolute pointer reports (tablet / touch / remote tool): forwarded as cursor moves for look / aim; the mouse stroke needs a relative mouse"));
		}
#endif
		if (Pending.Num() > MaxPending)
		{
			Pending.RemoveAt(0, Pending.Num() - MaxPending, EAllowShrinking::No); // nobody drains: keep the newest
		}
	}

	void Start()
	{
#if PLATFORM_WINDOWS
		if (Options.bAllowThread)
		{
			Runnable = MakeUnique<FRbRawInputRunnable>(Ring);
			Thread = FRunnableThread::Create(Runnable.Get(), TEXT("RbRawInputThread"), 0, TPri_Highest);
			if (Thread && Runnable->WaitReady(2000) && Runnable->GetWindow())
			{
				bThreadMode = true;
			}
			else
			{
				StopThread();
			}
		}
		if (Options.bInstallHandler && HasWindowsSlateApplication())
		{
			TSharedPtr<GenericApplication> App = FSlateApplication::Get().GetPlatformApplication();
			WinApp = static_cast<FWindowsApplication*>(App.Get()); // FWindowsApplication: checked above (not the NULL application)
			if (WinApp)
			{
				WinApp->AddMessageHandler(*this);
				bHandlerInstalled = true;
			}
		}
		bActive = bThreadMode || WinApp != nullptr;
		if (bActive && Options.bInstallHandler)
		{
			BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddRaw(this, &FImpl::OnBeginFrame);
		}
#endif
	}

	void StopThread()
	{
#if PLATFORM_WINDOWS
		if (Thread)
		{
			Thread->Kill(true); // Stop() posts WM_RB_QUIT; Run() removes its own registration, destroys the window, returns
			delete Thread;
			Thread = nullptr;
		}
		Runnable.Reset();
		bThreadMode = false;
#endif
	}

	void Shutdown()
	{
		if (BeginFrameHandle.IsValid())
		{
			FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);
			BeginFrameHandle.Reset();
		}
#if PLATFORM_WINDOWS
		const bool bTookOver = Runnable && Runnable->Registrations.load() > 0;
		if (WinApp && FSlateApplication::IsInitialized())
		{
			WinApp->RemoveMessageHandler(*this);
		}
		bHandlerInstalled = false;
		StopThread();
		// Hand the mouse back: UE still believes its game window is registered while it is in high-precision mode.
		if (bTookOver && WinApp && FSlateApplication::IsInitialized() && WinApp->IsUsingHighPrecisionMouseMode() && EngineWindow &&
			::IsWindow(EngineWindow))
		{
			RAWINPUTDEVICE Device;
			Device.usUsagePage = 0x01;
			Device.usUsage = 0x02;
			Device.dwFlags = 0;
			Device.hwndTarget = EngineWindow;
			::RegisterRawInputDevices(&Device, 1, sizeof(Device));
		}
		WinApp = nullptr;
#endif
		bActive = false;
	}
};

// ---------------------------------------------------------------------------------------------------------
// FRbRawMouseInput
// ---------------------------------------------------------------------------------------------------------

namespace
{
	TWeakPtr<FRbRawMouseInput> GRbRawMouseInstance;
}

FRbRawMouseInput::FRbRawMouseInput()
	: Impl(MakeUnique<FImpl>())
{
}

FRbRawMouseInput::~FRbRawMouseInput()
{
	Impl->Shutdown();
}

TSharedRef<FRbRawMouseInput> FRbRawMouseInput::Create()
{
	check(IsInGameThread());
	if (TSharedPtr<FRbRawMouseInput> Existing = GRbRawMouseInstance.Pin())
	{
		return Existing.ToSharedRef();
	}
	FRbRawMouseInputOptions Options;
	const TCHAR* Cmd = FCommandLine::Get();
	int32 ThreadSwitch = 1;
	if (FParse::Value(Cmd, TEXT("RbRawInputThread="), ThreadSwitch) && ThreadSwitch == 0)
	{
		Options.bAllowThread = false;
	}
	if (FPlatformMisc::IsRemoteSession())
	{
		Options.bAllowThread = false; // remote desktop delivers absolute reports; UE simulates relative motion from them
	}
	TSharedRef<FRbRawMouseInput> Instance = CreateWithOptions(Options);
	GRbRawMouseInstance = Instance;
	return Instance;
}

TSharedRef<FRbRawMouseInput> FRbRawMouseInput::CreateWithOptions(const FRbRawMouseInputOptions& Options)
{
	TSharedRef<FRbRawMouseInput> Instance = MakeShareable(new FRbRawMouseInput());
	FImpl& Impl = *Instance->Impl;
	Impl.Options = Options;
#if PLATFORM_WINDOWS
	const TCHAR* Cmd = FCommandLine::Get();
	const bool bHeadless = !FApp::CanEverRender() || IsRunningCommandlet() || IsRunningDedicatedServer() ||
		FParse::Param(Cmd, TEXT("RenderOffscreen")) || !HasWindowsSlateApplication();
	if (Options.bForceActive || !bHeadless)
	{
		if (Options.bForceActive && !HasWindowsSlateApplication())
		{
			Impl.Options.bInstallHandler = false;
			Impl.Options.bForwardToSlate = false;
		}
		Impl.Start();
	}
#endif
	if (Options.bInstallHandler)
	{
		if (!Impl.bActive)
		{
			UE_LOG(LogRawBreak, Display, TEXT("RbRawMouse: inactive (headless / no Windows Slate application): strokes come from scripted samples"));
		}
		else if (Impl.bThreadMode)
		{
			UE_LOG(LogRawBreak, Display, TEXT("RbRawMouse: raw-input thread running - TRUE per-report timestamps (QPC on arrival, review R-01)"));
		}
		else
		{
			UE_LOG(LogRawBreak, Warning, TEXT("RbRawMouse: FALLBACK - reconstructed report times from the message pump (no true timestamps; A9 needs the thread)"));
		}
	}
	return Instance;
}

bool FRbRawMouseInput::IsActive() const
{
	return Impl->bActive;
}

bool FRbRawMouseInput::HasTrueTimestamps() const
{
	return Impl->bActive && Impl->bThreadMode;
}

bool FRbRawMouseInput::IsThreadRunning() const
{
#if PLATFORM_WINDOWS
	return Impl->bThreadMode && Impl->Runnable && Impl->Runnable->GetWindow() != nullptr;
#else
	return false;
#endif
}

int32 FRbRawMouseInput::Drain(TArray<FRbRawMouseReport>& Out)
{
	if (!Impl->bActive)
	{
		return 0;
	}
	Impl->PumpGameThread();
	const int32 Count = Impl->Pending.Num();
	Out.Append(Impl->Pending);
	Impl->Pending.Reset();
	return Count;
}

void FRbRawMouseInput::Reset()
{
	if (!Impl->bActive)
	{
		return;
	}
	Impl->PumpGameThread(); // forward what arrived (look input), then drop it for the stroke
	Impl->Pending.Reset();
}

bool FRbRawMouseInput::InjectTestReport(int32 DeltaX, int32 DeltaY, bool bAbsolute)
{
#if PLATFORM_WINDOWS
	return Impl->bThreadMode && Impl->Runnable && Impl->Runnable->PostInject(DeltaX, DeltaY, bAbsolute);
#else
	return false;
#endif
}

FRbRawMouseInput::FStats FRbRawMouseInput::GetStats() const
{
	FStats Stats;
	Stats.EngineReports = Impl->EngineReports;
	Stats.Dropped = Impl->Ring.GetDropped();
	Stats.IntervalEstimate = Impl->IntervalEstimate;
#if PLATFORM_WINDOWS
	if (Impl->Runnable)
	{
		Stats.ThreadReports = Impl->Runnable->Reports.load();
		Stats.AbsoluteReports = Impl->Runnable->AbsoluteReports.load();
		Stats.Registrations = Impl->Runnable->Registrations.load();
	}
	Stats.bMessageHandler = Impl->bHandlerInstalled;
#endif
	return Stats;
}

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif
