#pragma once

// The stroke state machine of the first-person player (ue5-realism-plan 5.3-5.5, human-factors 3.1/3.7,
// Docs/ue-architecture.md 6.2). Turns input into an rb::human::IntendedStroke at the moment the cue tip crosses the
// cue-ball surface: aim (azimuth from the look input while down), elevation (wheel, floored by the cue clearance),
// cue-axis tip offsets (arrows), and the stroke itself from TIMESTAMPED raw mouse samples mapped through the gain
// curve; the tip speed at contact is the quadratic-fit velocity at the crossing time (T9, never per-frame deltas).
// The component knows nothing about rules or the simulator: it broadcasts OnStrokeContact and the match director
// executes the stroke. Owner: UE-5a.
//
// Phases:
//   Locked        not this player's turn, simulation / playback / replay / decision running
//   Walking       free movement; GetDown -> GettingDown (if the cue ball is on the table and in reach)
//   GettingDown   camera transition (URbCameraRigComponent), the cue appears behind the cue ball
//   Down          aiming; Stroke held = practice strokes (stop PracticeStopShort before the ball unless Commit / Hardcore)
//   Contact       tip crossed the ball with Commit held: IntendedStroke built, OnStrokeContact fired once
//   Watching      after contact (the camera stays down; GetDown stands up) until the director unlocks the next shot
//   PlacingCueBall ball in hand: the cue ball follows the aim point on the cloth, Confirm places it (director validates)
// NoiseKey::AddressIndex = number of earlier get-downs on this shot (HF 3.2), counted here.
//
// What you see is what hits (architecture.md 13 item 4, HF 3.7; review R-04): while Down, the rendered cue pose is
// rb::human::SampleHand(provisional IntendedStroke, FRbStrokeContext..., t since down) - drift, tremor and, from the start
// of the committed forward stroke (ForwardStart), the ramped per-shot draws - so the ExecuteStroke result at contact is
// exactly the pose on screen. If a stroke that showed any part of the ramp (HandPose::RampShown) ends without contact, the
// component broadcasts OnStrokeAborted(true): the director spends those draws (ShooterShotIndex++, AdvanceNoiseHistory,
// HF-B13) and pushes the updated context.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Math/RbStrokeMath.h"

#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"

#include "RbStrokeComponent.generated.h"

class ARbCue;
class ARbTable;
class FRbRawMouseInput;

UENUM(BlueprintType)
enum class ERbStrokePhase : uint8
{
	Locked,
	Walking,
	GettingDown,
	Down,
	Contact,
	Watching,
	PlacingCueBall,
};

// Aim of the current address (core units).
struct FRbAimState
{
	double Azimuth = 0.0;       // phi [rad]
	double Elevation = 0.0;     // theta [rad] requested (the executed one is max(this, floor))
	double ElevationFloor = 0.0;// [rad] from RbCueClearance
	double AxisOffsetA = 0.0;   // cue-axis offset / R (+ right)
	double AxisOffsetB = 0.0;   // cue-axis offset / R (+ up)
	double BridgeLength = 0.20; // [m]
};

// Everything SampleHand / ExecuteStroke need besides the IntendedStroke, for the ACTIVE shooter and this shot. Built by
// the director (UE-6b, URbMatchDirector::MakeStrokeContext) at BeginAddress and after every abort that spent draws; the
// component adds Key.AddressIndex itself and fills Situation.ElevationFloor / FloorBy / FloorBall from RbCueClearance.
struct FRbStrokeContext
{
	rb::human::ShooterAttributes Attributes;
	rb::human::StrokeSituation Situation;   // bridge, pressure (ComputePressure), fatigue 0 in M1, intoxication 0
	rb::human::TipState Tip;
	rb::human::CueBodyState CueBody;
	rb::CueSpec Cue = rb::kCuePlaying19oz;
	rb::BallSpec CueBall;
	rb::human::NoiseKey Key;                // AddressIndex is overwritten by the component
	rb::human::NoiseHistory History;
	rb::human::HumanParams Params;
};

// Everything the director needs to execute the stroke.
struct FRbStrokeCommit
{
	rb::human::IntendedStroke Intended;
	TArray<FRbStrokeSample> InputLog;   // cue-axis hand positions of the final address (replay header)
	double ContactTime = 0.0;           // FPlatformTime::Seconds() at the crossing
	uint32 AddressIndex = 0;            // earlier get-downs on this shot
	FTransform EyeTransform;            // camera at contact (shooter replay view)
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnStrokeContact, const FRbStrokeCommit& /*Commit*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnStrokeAborted, bool /*bRampShown*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRbOnCueBallPlaced, const FVector& /*WorldPosition*/);

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbStrokeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbStrokeComponent();

	void SetTable(ARbTable* InTable);
	void SetCue(ARbCue* InCue);

	// Director -> component: which ball is struck and where it is (core, table frame), and whether input is allowed.
	void BeginAddress(const rb::Vec3& CueBallPosition, double CueBallRadius);
	void SetStrokeContext(const FRbStrokeContext& InContext);
	void SetLocked(bool bLocked);
	void BeginCueBallPlacement();

	// Input -> component (bound by the character).
	void RequestGetDownToggle();
	void AddAimInput(const FVector2D& LookDelta, bool bFine);
	void AddElevationInput(float Steps);
	void AddTipOffsetInput(const FVector2D& Delta);
	void SetStrokeHeld(bool bHeld);
	void SetCommitHeld(bool bHeld);
	void SetSettleHeld(bool bHeld);          // HF-06 Settle (exhale and hold): IntendedStroke::SettleStart
	void ConfirmPressed();

	// Scripted stroke source (automation tests, cheats): hand positions along the axis with timestamps, consumed
	// like raw mouse samples. Positions in metres of HAND travel (the gain curve still applies).
	void InjectStrokeSamples(const TArray<FRbStrokeSample>& Samples);

	ERbStrokePhase GetPhase() const { return Phase; }
	const FRbAimState& GetAim() const { return Aim; }
	double GetCueDisplacement() const { return CueDisplacement; } // x_c [m], 0 = tip touching the ball
	const rb::human::HandPose& GetHandPose() const { return HandPose; } // SampleHand of the current frame (debug, tests)

	FRbOnStrokeContact OnStrokeContact;
	FRbOnCueBallPlaced OnCueBallPlaced;
	FRbOnStrokeAborted OnStrokeAborted;

	// --- tuning (plan 5.4 defaults) -------------------------------------------------------------------
	FRbStrokeGain Gain;
	double MouseDpi = 800.0;             // counts per inch (settings)
	double FitWindowSeconds = 0.02;      // quadratic-fit window (15-25 ms)
	double PracticeStopShort = 0.004;    // [m] practice strokes stop this far before the ball
	double AimSensitivity = 0.0005;      // [rad per mouse count]
	bool bHardcore = false;              // any tip contact is a shot (plan 14 Q2)

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	void SetPhase(ERbStrokePhase NewPhase);
	// Integrates new samples into the cue displacement, detects the crossing, builds the IntendedStroke.
	void ProcessStrokeSamples(double Now);
	// Pushes the cue pose (address + displacement) to ARbCue and the eye placement to the camera rig.
	void UpdatePresentation();

private:
	ERbStrokePhase Phase = ERbStrokePhase::Locked;
	FRbAimState Aim;
	rb::Vec3 CueBallPosition;
	double CueBallRadius = 0.028575;
	double CueDisplacement = -0.10;       // start 10 cm behind the ball
	bool bStrokeHeld = false;
	bool bCommitHeld = false;
	double SettleSince = -1.0;            // FPlatformTime::Seconds() when Settle was pressed while down, < 0 = none
	FRbStrokeContext Context;
	rb::human::HandPose HandPose;
	uint32 AddressIndex = 0;
	double DownSince = 0.0;               // FPlatformTime::Seconds() of the get-down (IntendedStroke::TimeDown)
	TArray<FRbStrokeSample> HandSamples;  // hand positions of the current address
	TArray<FRbStrokeSample> PendingScripted;
	TSharedPtr<FRbRawMouseInput> RawMouse;
	TWeakObjectPtr<ARbTable> Table;
	TWeakObjectPtr<ARbCue> Cue;
};
