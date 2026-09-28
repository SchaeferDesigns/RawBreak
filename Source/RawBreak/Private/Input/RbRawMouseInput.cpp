#include "Input/RbRawMouseInput.h"

// Owner: UE-5a. TODO(UE-5a): the design of the header (review R-01): on PLATFORM_WINDOWS a dedicated FRunnable input
// thread with a message-only window owning the raw mouse registration, QPC timestamp per WM_INPUT (GetRawInputData,
// RIM_TYPEMOUSE, relative motion), SPSC ring (TCircularQueue) to the game thread, forwarding of every drained delta to
// FSlateApplication::OnRawMouseMove, re-registration when an IWindowsMessageHandler (FWindowsApplication::AddMessageHandler)
// sees WM_INPUT on the game window again; fallback = handler-only with reconstructed report times. Inactive under
// -nullrhi / commandlets / no Slate application.

struct FRbRawMouseInput::FImpl
{
	bool bActive = false;
};

FRbRawMouseInput::FRbRawMouseInput()
	: Impl(MakeUnique<FImpl>())
{
}

FRbRawMouseInput::~FRbRawMouseInput() = default;

TSharedRef<FRbRawMouseInput> FRbRawMouseInput::Create()
{
	return MakeShareable(new FRbRawMouseInput());
}

bool FRbRawMouseInput::IsActive() const
{
	return Impl->bActive;
}

bool FRbRawMouseInput::HasTrueTimestamps() const
{
	return false; // TODO(UE-5a)
}

int32 FRbRawMouseInput::Drain(TArray<FRbRawMouseReport>& /*Out*/)
{
	return 0; // TODO(UE-5a)
}

void FRbRawMouseInput::Reset()
{
	// TODO(UE-5a)
}
