#pragma once

// Enhanced Input actions and the default mapping context, created AT RUNTIME in C++ (no .uasset input files:
// the headless pipeline never needs the editor for input; Docs/ue-architecture.md 6.5). Owner: UE-5a.
//
// M1 default bindings (keyboard + mouse; controller later):
//   Move            W A S D                      walk around the table (2D axis)
//   Look            mouse XY                     look while walking; AIM (azimuth) while down on the shot
//   GetDown         right mouse button (press)   get down on the shot / stand up again (toggle)
//   Stroke          left mouse button (hold)     stroke mode: mouse Y moves the cue along its axis (raw input)
//   Commit          Space (hold)                 the stroke is live: crossing the ball is a shot (else practice
//                                                strokes stop short, plan 5.4 / 14 Q2); Hardcore = always live
//   Elevation       mouse wheel                  butt up / down (floored by the clearance)
//   TipOffset       arrow keys (2D)              cue-axis offset on the cue ball (english / follow / draw)
//   FineAim         Left Shift (hold)            x0.2 aim sensitivity
//   Settle          Left Ctrl (hold)             exhale and hold while down (HF-06, IntendedStroke::SettleStart)
//   Glance          Tab (hold)                   glance at the match info (score, fouls, called ball)
//   ToggleOverlay   F1                           pin the info overlay; F2 toggles the physics debug block
//   ToggleDebug     F2
//   Replay          R                            replay the last shot (again = cycle camera, Esc = back)
//   Confirm         Enter / F                    place the cue ball (ball in hand), accept a decision, next rack / new
//                                                match after RackOver / MatchOver. Left mouse is NOT bound twice: a
//                                                Stroke press in PlacingCueBall is routed to Confirm by the stroke
//                                                component (review R-17)
//   CycleOption     Q / E                        cycle decision options / called pocket
//   Pause           Esc (press, also while paused) M2 (architect, 18.4): leaves a running replay, else opens / closes the pause
//                                                menu (URbUiSubsystem, M2-D). Esc is never bound to anything else.
//   M3 (plan step, Docs/ue-architecture.md 19.3; keys of ui-ux 3.4 / 9.4 / 9.5 / 9.12):
//   Call            C (hold)                     the diegetic shot call: select a ball, then a pocket by gaze, release on the
//                                                pocket (URbCallShotComponent, M3-G; the body points the cue tip, M3-H)
//   Declare         X (press)                    toggles the declaration the rules allow now: push-out / safety (M3-G)
//   Chalk           G (press)                    chalk the tip now (one more chalking, the R-mode reload; the body, M3-H)

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "RbInputSetup.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class RAWBREAK_API URbInputSetup : public UObject
{
	GENERATED_BODY()

public:
	// Creates every action and the mapping context with the default bindings above (transient objects owned by Outer).
	// Triggers: held actions have none (implicit Down: Triggered while held, Completed on release); pressed actions use
	// UInputTriggerPressed (Triggered once per press); TipOffset uses a pulse (one step per press, repeated while held).
	static URbInputSetup* CreateDefault(UObject* Outer);

	// Every action above, in declaration order (tests: each one is mapped).
	TArray<UInputAction*> GetAllActions() const;

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Context;

	UPROPERTY(Transient) TObjectPtr<UInputAction> Move;          // Axis2D
	UPROPERTY(Transient) TObjectPtr<UInputAction> Look;          // Axis2D
	UPROPERTY(Transient) TObjectPtr<UInputAction> GetDown;       // Boolean (pressed)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Stroke;        // Boolean (held)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Commit;        // Boolean (held)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Elevation;     // Axis1D
	UPROPERTY(Transient) TObjectPtr<UInputAction> TipOffset;     // Axis2D
	UPROPERTY(Transient) TObjectPtr<UInputAction> FineAim;       // Boolean (held)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Settle;        // Boolean (held)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Glance;        // Boolean (held)
	UPROPERTY(Transient) TObjectPtr<UInputAction> ToggleOverlay; // Boolean (pressed)
	UPROPERTY(Transient) TObjectPtr<UInputAction> ToggleDebug;   // Boolean (pressed)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Replay;        // Boolean (pressed)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Confirm;       // Boolean (pressed)
	UPROPERTY(Transient) TObjectPtr<UInputAction> CycleOption;   // Axis1D (-1 / +1)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Pause;         // Boolean (pressed; triggers while the game is paused) - M2
	UPROPERTY(Transient) TObjectPtr<UInputAction> Call;          // Boolean (held) - M3
	UPROPERTY(Transient) TObjectPtr<UInputAction> Declare;       // Boolean (pressed) - M3
	UPROPERTY(Transient) TObjectPtr<UInputAction> Chalk;         // Boolean (pressed) - M3
};
