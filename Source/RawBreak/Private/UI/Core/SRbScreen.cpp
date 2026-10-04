#include "UI/Core/SRbScreen.h"

#include "UI/Core/RbUiStyle.h"

#include "InputCoreTypes.h"

// Owner: M2-D. Focus model and key map of every menu screen (header).

void SRbScreen::InitScreen(ERbUiScreen InId, const TSharedRef<FRbUiHost>& InHost)
{
	ScreenId = InId;
	Host = InHost;
	Appear = 0.0f;
	bAnimating = true;
	SetRenderOpacity(0.0f);
}

bool SRbScreen::HandleBack()
{
	if (Host.IsValid() && Host->CloseTop)
	{
		Host->CloseTop();
		return true;
	}
	return false;
}

bool SRbScreen::HandleKey(const FKey& Key, const FModifierKeysState& /*Modifiers*/)
{
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		return HandleBack();
	}
	if (Key == EKeys::Tab)
	{
		return true; // Tab never moves focus (UX-T26: it is Glance in play)
	}
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)
	{
		MoveFocus(-1);
		return true;
	}
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)
	{
		MoveFocus(1);
		return true;
	}
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left)
	{
		AdjustFocused(-1);
		return true;
	}
	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right)
	{
		AdjustFocused(1);
		return true;
	}
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		ActivateFocused();
		return true;
	}
	return false;
}

TSharedPtr<SWidget> SRbScreen::GetFocusedWidget() const
{
	return FocusItems.IsValidIndex(FocusIndex) ? FocusItems[FocusIndex].Widget : TSharedPtr<SWidget>();
}

void SRbScreen::SetFocusIndex(int32 Index, bool bFromMouse)
{
	if (FocusItems.Num() == 0)
	{
		FocusIndex = 0;
		return;
	}
	const int32 Clamped = FMath::Clamp(Index, 0, FocusItems.Num() - 1);
	if (Clamped != FocusIndex)
	{
		FocusIndex = Clamped;
		bFocusFromMouse = bFromMouse;
		OnFocusChanged();
	}
	MirrorFocus();
}

void SRbScreen::SetFocusItems(TArray<FRbFocusItem> Items, int32 PreferredIndex)
{
	FocusItems = MoveTemp(Items);
	bFocusFromMouse = false;
	int32 Index = PreferredIndex != INDEX_NONE ? PreferredIndex : FocusIndex;
	if (FocusItems.Num() == 0)
	{
		FocusIndex = 0;
		return;
	}
	Index = FMath::Clamp(Index, 0, FocusItems.Num() - 1);
	// Start on an enabled item when there is one (a disabled item may still take focus later, to show its reason).
	if (!FocusItems[Index].bEnabled)
	{
		for (int32 Probe = 0; Probe < FocusItems.Num(); ++Probe)
		{
			if (FocusItems[Probe].bEnabled)
			{
				Index = Probe;
				break;
			}
		}
	}
	FocusIndex = Index;
	OnFocusChanged();
	MirrorFocus();
}

void SRbScreen::MoveFocus(int32 Direction)
{
	if (FocusItems.Num() == 0 || Direction == 0)
	{
		return;
	}
	SetFocusIndex(FocusIndex + (Direction > 0 ? 1 : -1));
}

void SRbScreen::MirrorFocus() const
{
	if (Host.IsValid() && Host->SetFocus)
	{
		Host->SetFocus(GetFocusedWidget());
	}
}

void SRbScreen::OnActivated()
{
	MirrorFocus();
}

bool SRbScreen::IsRepeatableKey(const FKey& Key)
{
	return Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Up ||
		Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Up ||
		Key == EKeys::Gamepad_LeftStick_Down || Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right;
}

FReply SRbScreen::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// The OS auto-repeats a held key. Only moving the focus and changing a row repeat; a held Enter / Space / Esc acts once, so it
	// never chains through the menus (title: Play -> venue -> mode -> travel within a second) or toggles the pause menu shut again
	// right after the Esc press that opened it.
	if (InKeyEvent.IsRepeat() && !IsRepeatableKey(InKeyEvent.GetKey()))
	{
		return FReply::Handled();
	}
	return HandleKey(InKeyEvent.GetKey(), InKeyEvent.GetModifierKeys()) ? FReply::Handled() : FReply::Unhandled();
}

FReply SRbScreen::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return HandleKeyUp(InKeyEvent.GetKey()) ? FReply::Handled() : FReply::Unhandled();
}

FReply SRbScreen::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// A click on the background keeps the keyboard in the menu (the focused item stays the focused item).
	MirrorFocus();
	return FReply::Handled();
}

FNavigationReply SRbScreen::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	return FNavigationReply::Stop(); // the screen's own focus list is the navigation (header)
}

void SRbScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bAnimating)
	{
		return;
	}
	const bool bReduced = Host.IsValid() && Host->bReducedMotion;
	const float Duration = bReduced ? RbUi::PushReduced : RbUi::Push;
	Appear = FMath::Min(1.0f, Appear + FMath::Clamp(InDeltaTime, 0.0f, 0.1f) / Duration);
	const float Eased = RbUi::EaseOut(Appear);
	SetRenderOpacity(Eased);
	SetRenderTransform(bReduced ? FSlateRenderTransform() : FSlateRenderTransform(FVector2f(0.0f, (1.0f - Eased) * RbUi::PushSlidePx)));
	if (Appear >= 1.0f)
	{
		bAnimating = false;
		SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
}
