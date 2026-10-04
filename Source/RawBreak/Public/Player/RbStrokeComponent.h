#pragma once

// The stroke state machine of the first-person player (ue5-realism-plan 5.3-5.5, human-factors 3.1/3.7,
// Docs/ue-architecture.md 6.2). Turns input into an rb::human::IntendedStroke at the moment the cue tip crosses the
// cue-ball surface: aim (azimuth from the look input while down), elevation (wheel, floored by the cue clearance),
// cue-axis tip offsets (arrows), and the stroke itself from TIMESTAMPED raw mouse samples mapped through the gain
// curve; the tip speed at contact is the quadratic-fit velocity at the crossing time (T9, never per-frame deltas).
// The component knows nothing about rules or the simulator: it broadcasts OnStrokeContact and the match director
// executes the stroke. Owner: UE-5a, M2-F.
//
// M2-F (Docs/ue-architecture.md 18.3):
//   * Aim in centimetres of mouse travel (P3): AddAimInput takes raw mouse COUNTS; RbAimResponse maps them with MouseDpi and
//     Controls (FRbControlSettings: 7.2 deg/cm coarse = 90 deg per 12.5 cm, x FineAimFactor 0.075 with Shift, optional
//     acceleration). The linear path accumulates the counts of the address and applies the factor once (azimuth = base - counts x
//     rad/count), so the same counts give BITWISE the same azimuth in any frame split; a settings change or an explicit aim
//     rebases. Controls / MouseDpi / StrokeSensitivity come from URbGameUserSettings (BeginPlay, OnSettingsChanged) unless
//     bApplyUserSettings is off (tests write the members directly).
//   * The get-down is the camera rig's human posture change (P5): Down starts when the rig's eye arrives
//     (URbCameraRigComponent::GetPostureArrivalSeconds; GetDownSeconds without a rig). The component pushes the posture seed
//     (match seed, shooter, shooter shot index, address index), the pressure and the watching state to the rig.
//   * Diegetic ball in hand (P2): in PlacingCueBall the owner's URbBallInHandComponent carries the cue ball over the look point
//     (UpdatePlacementPoint -> SetTargetCore); Confirm asks it to set the ball down, and OnCueBallPlaced fires only when the ball
//     touches the cloth, at exactly the previewed spot (PlacementValidator = the director's legality: an illegal spot is refused,
//     nothing is placed). Without that component (tests, tools) Confirm places at once as in M1.
//   * Pause (18.2 M2-D contract): while the world is paused the component keeps ticking only to notice it: a held stroke is
//     dropped (a committed one aborts), pending scripted samples and the raw reports of the pause are discarded, and a Stroke
//     button still held on resume must be released first - no contact can come out of a pause. The input handlers check the
//     pause FIRST (review): Enhanced Input fires Completed for the held Stroke / Commit actions on the first paused frame (their
//     trigger state is forced to None while paused), possibly before this component's tick noticed the pause - that release must
//     neither process the stroke's samples (a contact while paused) nor count as the release after the pause. A press or release
//     while paused changes nothing; on resume the pawn's IsStrokeButtonDown (the physical key state) decides whether the button
//     must still be released (held through the pause) or not (let go during the pause, so the next press strokes). The address's
//     human clock skips the pause (review, 3rd pass): DownSince and the Settle start move on by the paused time, so a pause during
//     the get-down still starts Down when the (frozen) eye arrives, the cue's drift / tremor continue without a jump, and the time
//     in the menu never counts as time down on the shot (HF-07 envelope, IntendedStroke::TimeDown).
//
// Phases:
//   Locked        not this player's turn, simulation / playback / replay / decision running
//   Walking       free movement; GetDown -> GettingDown (if the cue ball is on the table and in reach)
//   GettingDown   camera transition (URbCameraRigComponent), the cue appears behind the cue ball; Down at
//                 DownSince = press time + GetDownSeconds (analytic, never accumulated frame times)
//   Down          aiming; Stroke held = practice strokes (stop PracticeStopShort before the ball unless Commit / Hardcore)
//   Contact       tip crossed the ball with Commit held: IntendedStroke built, OnStrokeContact fired once
//   Watching      after contact (the camera stays down; GetDown stands up) until the director unlocks the next shot
//   PlacingCueBall ball in hand: the cue ball follows the aim point on the cloth, Confirm places it (director validates);
//                 a Stroke (left mouse) press is routed to Confirm here (review R-17: no key bound twice)
// NoiseKey::AddressIndex = number of earlier get-downs on this shot (HF 3.2), counted here (every stand-up from
// GettingDown / Down increments it; BeginAddress resets it).
//
// Frame-rate independence (pitfall 19, UE-5a acceptance): hand samples are processed one by one in time order
// (FRbCueIntegrator), whatever frame they arrive in; the crossing is interpolated inside the sample step; speed and
// acceleration come from a quadratic fit over the samples of the last FitWindowSeconds (plus the crossing sample),
// evaluated at the crossing time. TimeDown, ForwardStart and PauseDuration are sample times relative to DownSince. So the
// same samples give a bitwise identical IntendedStroke at 30, 60 or 144 fps.
//
// Input-log derivations (Docs/ue-architecture.md 6.2):
//   ForwardStart    the moment the cue leaves the back of the stroke (end of the pause) of a forward stroke made while
//                   live (Commit / Hardcore); a commit pressed during a forward stroke starts it at that sample
//   PauseDuration   time the cue stayed within PauseTolerance of the back-most point of the final stroke (a still
//                   mouse sends no reports, so the silence counts; the departure is taken one report interval before the
//                   first sample beyond the tolerance)
//   ContactAcceleration  d v_c / d v_m * a_m of the hand fit at the crossing (the gain curve's slope included)
//   HeadMovedBeforeContact  look input after ForwardStart that the mouse stroke does not explain (look input while the
//                   Stroke button is held IS the stroke and never counts), above HeadMoveThreshold [rad]
//   Steering        lateral hand travel since ForwardStart x SteeringGain = grip offset y_g (plan 5.4): azimuth + y_g / L_bg,
//                   axis offset A - y_g (L_b + R) / (L_bg R) (the pivot geometry of human-factors 3.5); TipVelocityRight =
//                   the swoop -dy_g/dt L_b / L_bg at contact (animation only, HF-11)
//
// What you see is what hits (architecture.md 13 item 4, HF 3.7; review R-04): while Down, the rendered cue pose is
// rb::human::SampleHand(provisional IntendedStroke, FRbStrokeContext..., t since down) - drift, tremor and, from the start
// of the committed forward stroke (ForwardStart), the ramped per-shot draws - so the ExecuteStroke result at contact is
// exactly the pose on screen (at contact the pose is SampleHand of the FINAL IntendedStroke at TimeDown). If a committed
// stroke ends without contact (reversed, Commit or Stroke released, stood up, locked), the component broadcasts
// OnStrokeAborted(bRampShown): true when a PRESENTED frame showed part of the ramp (HandPose::RampShown); then the
// director spends those draws (ShooterShotIndex++, AdvanceNoiseHistory, HF-B13) and pushes the updated context.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Input/RbRawMouseInput.h"
#include "Math/RbStrokeMath.h"
#include "Settings/RbSettingsTypes.h"

#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"

#include "RbStrokeComponent.generated.h"

class ARbCue;
class ARbTable;
class FRbRawMouseInput;
class URbCameraRigComponent;

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
	double BridgeLength = 0.20; // [m] (= FRbStrokeContext::Situation.BridgeLength)
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
	// Balls on the table except the cue ball (core frame, from FRbTableState): the clearance floor (RbCueClearance) and
	// ExecuteStroke's OtherBalls. Empty = no ball obstacles (the floor then only sees rails / environment).
	TArray<rb::human::BallObstacle> OtherBalls;
};

// Everything the director needs to execute the stroke.
struct FRbStrokeCommit
{
	rb::human::IntendedStroke Intended;
	TArray<FRbStrokeSample> InputLog;   // hand positions of the final address up to the crossing sample (replay header)
	double ContactTime = 0.0;           // FPlatformTime::Seconds() at the crossing
	uint32 AddressIndex = 0;            // earlier get-downs on this shot
	FTransform EyeTransform;            // camera at contact (shooter replay view)
	// The context the component rendered with (Key.AddressIndex and the Situation's elevation floor filled in):
	// ExecuteStroke must use exactly this one (what you see is what hits).
	FRbStrokeContext Context;
	rb::human::HandPose ContactPose;    // SampleHand(Intended, Context, Intended.TimeDown): the pose on screen at contact
	bool bTrueTimestamps = false;       // samples came from the raw-input thread (false: scripted or reconstructed)
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
	// BeginAddress starts a new shot: AddressIndex 0, tip offsets and elevation reset, phase Walking (stands up).
	void BeginAddress(const rb::Vec3& CueBallPosition, double CueBallRadius);
	void SetStrokeContext(const FRbStrokeContext& InContext);
	void SetLocked(bool bLocked);
	void BeginCueBallPlacement();

	// Input -> component (bound by the character).
	void RequestGetDownToggle();
	// Look input while down [raw mouse counts, + X = right]: the aim (X) through RbAimResponse. DeltaSeconds = the frame time of
	// the delta (only the optional acceleration uses it; <= 0 = linear). Frozen while the Stroke button is held or must first be
	// released (IsStrokeReleaseRequired, review): that mouse motion is a stroke, never aim.
	void AddAimInput(const FVector2D& LookCounts, bool bFine, double DeltaSeconds = 0.0);
	void AddElevationInput(float Steps);
	void AddTipOffsetInput(const FVector2D& Delta);
	void SetStrokeHeld(bool bHeld);
	void SetCommitHeld(bool bHeld);
	void SetSettleHeld(bool bHeld);          // HF-06 Settle (exhale and hold): IntendedStroke::SettleStart
	void ConfirmPressed();
	// The world was paused (or resumed): a held / scripted stroke is dropped, see the header. TickComponent and the input handlers
	// call it on their own (SyncWorldPause).
	void NotifyWorldPaused(bool bPaused);
	bool IsPausedByWorld() const { return bWorldPaused; }
	// Whether a key of the Stroke action is physically down (the pawn reads its player input); asked on resume. Unset = unknown:
	// a button that was held at the pause must then send a release after the resume first.
	TFunction<bool()> IsStrokeButtonDown;
	// A Stroke press was routed elsewhere (ball-in-hand Confirm, held through a pause): ignored until the button is released.
	bool IsStrokeReleaseRequired() const { return bStrokeNeedsRelease; }
	// The Stroke button counts as held (a press the component accepted, not yet released or dropped).
	bool IsStrokeHeld() const { return bStrokeHeld; }

	// Ball in hand (P2): the legality of a placement (plan position, core table frame) - the pawn sets the director's
	// CanPlaceCueBall. Unset = every spot is legal (the director still validates PlaceCueBall).
	TFunction<bool(const rb::Vec2&)> PlacementValidator;
	// The ball-in-hand target of the current frame (core plan position) and whether one exists.
	bool GetPlacementTargetCore(rb::Vec2& OutPlan) const;

	// Scripted stroke source (automation tests, cheats): hand positions along the axis with timestamps, consumed
	// like raw mouse samples. Positions in metres of HAND travel (the gain curve still applies). A scripted stream implies
	// the Stroke button (it does not need SetStrokeHeld); Commit / Hardcore still decide practice vs shot. Samples are
	// consumed while Down once the clock reaches their time; samples before the get-down completed are dropped, and so is a
	// sample older than the last one of its stroke (a stroke stays time ordered).
	// bNewStroke (default): the first sample of this call starts a NEW stroke (its own hand origin, like a fresh Stroke press),
	// so consecutive MakeScriptedStroke injections (the RbStroke cheat: a practice stroke, then the shot) never continue the
	// previous stroke's hand path; a committed stroke still in progress then ends as an abort. false = the samples continue the
	// current stream (one stroke fed in chunks). A physical Stroke press takes over once the scripted stream is used up.
	void InjectStrokeSamples(const TArray<FRbStrokeSample>& Samples, bool bNewStroke = true);

	// Scripted stroke of the component's current state (cue displacement, gain, backswing limit) that crosses the ball at
	// TipSpeed [m/s]; first sample at StartTime (FPlatformTime domain / the clock). For tests and the RbStroke cheat:
	// InjectStrokeSamples(MakeScriptedStroke(V, Now + 0.05)).
	TArray<FRbStrokeSample> MakeScriptedStroke(double TipSpeed, double StartTime) const;

	// Aim of the current address (cheats / tests; the floor is recomputed). Kept by later get-downs of this shot (the view
	// direction sets the azimuth at a get-down only without an explicit aim); BeginAddress clears it.
	void SetAim(double Azimuth, double Elevation, double AxisOffsetA, double AxisOffsetB);

	ERbStrokePhase GetPhase() const { return Phase; }
	// The component's clock (FPlatformTime::Seconds() or ClockOverride): input timestamps of the pawn's look gate.
	double GetClockNow() const { return ClockNow(); }
	const FRbAimState& GetAim() const { return Aim; }
	double GetCueDisplacement() const { return CueDisplacement; } // x_c [m], 0 = tip touching the ball
	const rb::human::HandPose& GetHandPose() const { return HandPose; } // SampleHand of the current frame (debug, tests)
	const FRbStrokeContext& GetContext() const { return Context; }
	const FRbStrokeCommit& GetLastCommit() const { return LastCommit; }
	uint32 GetAddressIndex() const { return AddressIndex; }
	double GetDownSince() const { return DownSince; }
	bool IsCommittedStrokeInProgress() const { return bCommittedStroke; }
	bool IsStrokeActive() const { return bStrokeActive; } // a stroke source drives the cue (Stroke held / scripted stream)
	bool IsRawStrokeActive() const { return bStrokeActive && !bScriptedStroke; } // the Stroke button's raw reports drive it
	bool HasTrueTimestamps() const;       // raw input with per-report timestamps (F2 debug block)
	// Rendered cue pose (core, table frame): tip dome centre and butt -> tip direction.
	void GetCuePoseCore(rb::Vec3& OutTipDomeCenter, rb::Vec3& OutDirection) const { OutTipDomeCenter = CuePoseTip; OutDirection = CuePoseDir; }

	// Cue pose of a hand pose (the geometry of ExecuteStroke's executed pose, human-factors 3.6): axis from (elevation,
	// azimuth), contact offsets = AimToContactOffset(axis offsets) clamped to OffsetClamp, dome centre on the ball at
	// C + Q (R + r_dome) / R, then moved along the axis by the cue displacement X (< 0 = behind the ball).
	static void ComputeCuePoseCore(const rb::human::HandPose& Pose, const rb::Vec3& BallCenter, double BallRadius, double DomeRadius,
		double OffsetClamp, double CueDisplacementX, rb::Vec3& OutTipDomeCenter, rb::Vec3& OutDirection);

	// Advances the machine to time NowSeconds (FPlatformTime domain): phase timing, sample processing, presentation. Called
	// by TickComponent with the clock; tests and dev tools call it directly with their own clock.
	void TickStroke(double NowSeconds);

	// Test / tool clock (unset = FPlatformTime::Seconds()). Every input call stamps with it.
	TFunction<double()> ClockOverride;

	// Tests: the raw mouse source (BeginPlay takes the process instance, FRbRawMouseInput::Create). An automation test passes a
	// forced input thread (FRbRawMouseInput::CreateWithOptions) and injects reports that are stamped on arrival like WM_INPUT.
	void SetRawMouseInput(const TSharedPtr<FRbRawMouseInput>& InRawMouse) { RawMouse = InRawMouse; }

	// Dev tool (Tools/unreal/editor/rb_dev_ue5a.py): runs a scripted stroke through a transient component (no world) and
	// returns the rendered cue pose at every frame (table-local UE cm; location = tip dome centre, X axis = butt -> tip)
	// from the get-down until contact or the end of the samples. NoiseScale scales the human layer (1 = Sim; larger values
	// exaggerate the drift for a visible dev capture); the stroke starts HoldSeconds after the get-down. OutTimes = seconds
	// since the get-down was requested.
	UFUNCTION(BlueprintCallable, Category = "RawBreak|Dev")
	static TArray<FTransform> DevRecordStrokePoses(float TipSpeed, float FrameRate, float NoiseScale, float HoldSeconds, bool bCommit, int32 Seed,
		TArray<float>& OutTimes);

	FRbOnStrokeContact OnStrokeContact;
	FRbOnCueBallPlaced OnCueBallPlaced;
	FRbOnStrokeAborted OnStrokeAborted;
	FRbOnCueBallPlaced OnPlacementPointChanged; // ball in hand: the cue-ball centre under the aim point moved (world)

	// --- tuning (plan 5.4 defaults) -------------------------------------------------------------------
	FRbStrokeGain Gain;
	double MouseDpi = 800.0;             // counts per inch (settings URbGameUserSettings::MouseDpi)
	FRbControlSettings Controls;         // aim / look / stroke speeds (settings; M2-F, P3)
	bool bApplyUserSettings = true;      // BeginPlay reads URbGameUserSettings (MouseDpi, Controls, bHardcoreStroke)
	double FitWindowSeconds = 0.02;      // quadratic-fit window (15-25 ms)
	double PracticeStopShort = 0.004;    // [m] practice strokes stop this far before the ball
	bool bHardcore = false;              // any tip contact is a shot (plan 14 Q2)
	double AddressDistance = 0.03;       // [m] tip behind the ball after getting down
	double MaxBackswing = 0.30;          // [m] x_c never goes further back (also the clearance sweep's backswing)
	double GetDownSeconds = 1.0;         // camera transition GettingDown -> Down (0.8-1.5 s)
	double MaxReach = 1.5;               // [m] plan distance pawn -> cue ball for GetDown
	double ElevationStep = 0.5 * UE_DOUBLE_PI / 180.0; // [rad] per wheel notch
	double MaxElevation = 85.0 * UE_DOUBLE_PI / 180.0;
	double TipOffsetStep = 0.05;         // [R] per arrow step
	double MaxTipOffset = 0.7;           // [R] radius of the allowed axis-offset disc
	double SteeringGain = 0.25;          // G_lat (plan 5.4; 0 = straight stroke assist)
	double ForwardHysteresis = 0.002;    // [m] cue travel that starts a forward stroke
	double ReverseHysteresis = 0.002;    // [m] cue travel back from the forward peak that ends it
	double PauseTolerance = 0.001;       // [m] "still at the back" band of the pause detection
	double MaxReportInterval = 0.008;    // [s] a moving mouse reports at least every 8 ms (125 Hz)
	double HeadMoveThreshold = 0.005;    // [rad] of unexplained look input after ForwardStart (HF-10)

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Re-reads URbGameUserSettings (MouseDpi, Controls, bHardcoreStroke); the aim keeps its current azimuth.
	void ApplyUserSettings();

protected:
	void SetPhase(ERbStrokePhase NewPhase);
	// Integrates new samples (raw reports and scripted samples up to Now) into the cue displacement, detects the
	// crossing, builds the IntendedStroke.
	void ProcessStrokeSamples(double NowSeconds);
	// Pushes the cue pose (SampleHand pose + displacement) to ARbCue and the eye placement to the camera rig.
	void UpdatePresentation();

private:
	double ClockNow() const;
	double StrokeMetersPerCount() const;     // hand metres per raw count (MouseDpi, StrokeSensitivity)
	void SyncWorldPause();                   // NotifyWorldPaused(the world's pause state) - before any input is processed
	void RebaseAim();                        // Aim.Azimuth becomes the new base of the counted aim input
	void PushRigContext();                   // posture seed + pressure to the camera rig
	class URbBallInHandComponent* FindBallInHand() const;
	void HandleBallSetDown(const rb::Vec2& PlanCore);
	bool IsLive() const { return bCommitHeld || bHardcore; }
	void BeginStroke(bool bRawSource, double StartTime);
	void EndStroke();                         // stroke source released / stood up / locked (aborts a committed stroke)
	// The held Stroke button takes over from a scripted stream that is used up (else the button would stay dead until the
	// player stands up). True if a raw stroke now runs.
	bool TakeOverExhaustedScriptedStroke(double StartTime);
	void ResetAddress();                      // clears the input log of the address
	bool ProcessHandSample(const FRbStrokeSample& Sample); // false = contact reached (stop consuming)
	void TrackForwardStroke(int32 Index);
	void StartCommittedStroke(double StartTime, int32 StartIndex);
	void AbortCommittedStroke();
	void OnCrossing(double CrossingTime, int32 CrossingIndex);
	rb::human::IntendedStroke BuildIntended(double AbsoluteTime, double Speed, double Acceleration, double LateralNow, double LateralVelocity) const;
	rb::human::HandPose SampleHandAt(const rb::human::IntendedStroke& Intended, double TimeSinceDown) const;
	void PushPresentation();
	void UpdateElevationFloor();
	void UpdatePlacementPoint();
	bool IsCueBallInReach() const;
	void InitAimFromView();
	FTransform GetEyeTransform() const;
	URbCameraRigComponent* FindRig() const;
	FVector CoreToWorld(const rb::Vec3& P) const;
	FVector CoreDirectionToWorld(const rb::Vec3& D) const;

	ERbStrokePhase Phase = ERbStrokePhase::Locked;
	FRbAimState Aim;
	// Counted aim input of the address (P3): Aim.Azimuth = AimBase - (CoarseCounts rad/count + FineCounts rad/count) - AccelRadians.
	double AimBase = 0.0;
	double AimCoarseCounts = 0.0;         // integer counts (exact sums in doubles)
	double AimFineCounts = 0.0;
	double AimAccelRadians = 0.0;         // the non-linear part (acceleration on)
	bool bWorldPaused = false;
	double PausedAt = 0.0;                // ClockNow() when the pause was noticed (the address clock skips the pause on resume)
	FDelegateHandle SettingsHandle;
	FDelegateHandle SetDownHandle;
	rb::Vec3 CueBallPosition;
	double CueBallRadius = 0.028575;
	double CueDisplacement = -0.03;       // x_c [m]; AddressDistance behind the ball after a get-down
	bool bStrokeHeld = false;
	bool bStrokeNeedsRelease = false;     // a Stroke press was routed to Confirm: ignore it until released
	double StrokeHeldSince = 0.0;
	bool bCommitHeld = false;
	double SettleSince = -1.0;            // Now() when Settle was pressed while down, < 0 = none
	FRbStrokeContext Context;
	rb::human::HandPose HandPose;
	uint32 AddressIndex = 0;
	double DownSince = 0.0;               // Now() when Down begins (IntendedStroke::TimeDown origin)
	double LastTickTime = 0.0;            // clock of the last TickStroke (presentation time)
	bool bFloorDirty = true;
	bool bAimSetExplicitly = false;       // SetAim this shot: a get-down keeps it (cheats), else the view sets the azimuth

	// Stroke of the current address.
	TArray<FRbStrokeSample> HandSamples;  // hand positions of the current address (the input log)
	TArray<FRbStrokeSample> CueSamples;   // x_c at the same times (Position = x_c), parallel to HandSamples
	TArray<FRbStrokeSample> PendingScripted;
	TArray<bool> PendingScriptedStarts;   // parallel to PendingScripted: the sample starts a new stroke (InjectStrokeSamples)
	bool bScriptedRestart = false;        // a consumed / dropped sample asked for a new stroke; the next kept sample begins it
	TArray<FRbRawMouseReport> ReportScratch; // drained raw reports of this call (reused: no per-frame allocation)
	FRbCueIntegrator Integrator;
	bool bStrokeActive = false;           // a stroke source (Stroke button or a scripted stream) is driving the cue
	bool bScriptedStroke = false;
	int32 StrokeStartIndex = 0;           // first sample of the current stroke in HandSamples
	int64 RawCountsX = 0;                 // accumulated raw counts of the address (+X right, +Y toward the user)
	int64 RawCountsY = 0;
	bool bForwardPhase = false;
	double BackMostX = 0.0;
	int32 BackMostIndex = 0;
	double ForwardPeakX = 0.0;
	double ForwardMotionStart = 0.0;      // end of the pause of the current forward motion [s, absolute]
	int32 ForwardMotionIndex = 0;
	double PauseDuration = 0.4;
	bool bCommittedStroke = false;
	double ForwardStart = 0.0;            // [s, absolute] of the committed forward stroke
	int32 ForwardStartIndex = 0;
	bool bRampShownLatched = false;       // a presented frame of the committed stroke showed the per-shot ramp
	double HeadMoveAccum = 0.0;

	FRbStrokeCommit LastCommit;
	rb::Vec3 CuePoseTip;
	rb::Vec3 CuePoseDir = rb::Vec3(1.0, 0.0, 0.0);
	FVector PlacementWorld = FVector::ZeroVector;
	bool bHasPlacement = false;
	TSharedPtr<FRbRawMouseInput> RawMouse;
	TWeakObjectPtr<ARbTable> Table;
	TWeakObjectPtr<ARbCue> Cue;
};
