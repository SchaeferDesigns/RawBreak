#pragma once

// The stroke execution model (human-factors 3.1, 3.5-3.7, 3.9): intended stroke + attributes + situation +
// equipment state + seeded noise -> the CueStrikeInput the physics gets, plus prediction flags, the per-channel
// breakdown for replays and the counterfactual diagnosis, and the hand pose for rendering (what you see is what
// hits). Equipment and ball state are updated AFTER the shot (ApplyShotToEquipment), never inside ExecuteStroke.
// Owner: WP-11 (player model). Part of rb::human: depends on Core, Math, Equipment/Cue.h, Physics/CueStrike.h,
// Physics/ShotResult.h and Simulator.h (after-shot update, diagnosis overrides), Shot/ShotRecord.h (NonTipContact
// data only); never included by the event loop or by the rules. The AI builds strikes only through ExecuteStroke (HF-B07; rb/Human/AiProfiles.h).
//
// Principles (human-factors 1): every imperfection acts BEFORE the tip touches the cue ball; nothing is rolled on
// outcomes (miscue, make, cling): the core physics decides from the perturbed inputs (HF-B03). ExecuteStroke and
// SampleHand are pure functions: no side effects, no allocation, deterministic (HF-B01).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Equipment/Cue.h"
#include "rb/Human/BallMarks.h"
#include "rb/Human/CueState.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"
#include "rb/Human/TipState.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallBall.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>

namespace rb::human
{
	// ---------------------------------------------------------------------------------------------
	// Inputs
	// ---------------------------------------------------------------------------------------------

	// From the UE 5.4 stroke input (player) or SyntheticHand (AI, 3.8).
	struct IntendedStroke
	{
		double Azimuth = 0.0;              // phi_i [rad]
		double Elevation = 0.0;            // theta_i [rad]
		double AxisOffsetA = 0.0;          // A_i [1]: cue-AXIS offset / R (MOT B.3), + = right
		double AxisOffsetB = 0.0;          // B_i [1], + = above center
		double Speed = 0.0;                // V_i [m/s] tip speed at contact (quadratic fit, UE 5.4)
		double TipVelocityRight = 0.0;     // v_r,i [m/s] swoop from input steering (animation only, HF-11)
		double TipVelocityUp = 0.0;        // v_u,i [m/s]
		double TimeDown = 0.0;             // t_c [s] contact time since "down on the shot"
		double ForwardStart = 0.0;         // t_fwd [s] start of the committed final forward stroke (per-shot ramp, 3.7; rendering
		                                   //   only: ExecuteStroke always applies the full draws at contact)
		double SettleStart = -1.0;         // t_s [s] since down on the shot; < 0 = no Settle (HF-06)
		double PauseDuration = 0.4;        // T_pause [s] pause at the back of the final stroke (HF-08)
		double ContactAcceleration = 0.0;  // a_c [m/s^2] tip acceleration at contact (< 0 = decelerating, HF-09)
		bool HeadMovedBeforeContact = false; // HF-10
	};

	// Another ball on the table (clearance test of the executed cue pose, path corridor of the double-hit risk).
	struct BallObstacle
	{
		BallId Id = kNoBall;
		Vec3 Position;          // center [m]
		double Radius = kDefaultBallRadius;
	};

	// ---------------------------------------------------------------------------------------------
	// Situation factors and breakdown
	// ---------------------------------------------------------------------------------------------

	// Every multiplier and sigma of 3.4 / 3.5 at the contact time t_c (for tests, replays and the stroke report).
	struct SituationFactors
	{
		double NerveScale = 1.0;    // L_N = L(Nerve; 0.125)
		double Pressure = 0.0;      // P_x = P (1 - c_calm min(1, I / I_c)): the pressure after the intoxication calm (= P at I = 0)
		double PressureGain = 1.0;  // g = 1 + (g_max - 1) P_x L_N
		double IntoxicationDrift = 1.0;  // m_alc = 1 + k_dr max(0, I - I_c) / (1 - I_c) (1 exactly at I <= I_c; Q2 hook)
		double IntoxicationTremor = 1.0; // m_alc,t = 1 + k_tr max(0, I - I_c) / (1 - I_c)
		double Bridge = 1.0;        // m_br
		double SlipSpeed = 6.0;     // V_b [m/s]
		double Slip = 1.0;          // m_slip
		double Stance = 1.0;        // m_st
		double Head = 1.0;          // m_head
		double Stick = 1.0;         // m_stick
		double Fatigue = 1.0;       // m_fat
		double OffHand = 1.0;       // m_off
		double Rush = 1.0;          // m_rush
		double Jab = 1.0;           // m_jab
		double SettleIn = 1.0;      // E(t_c)
		double Settle = 1.0;        // k_set(t_c)
		double DriftSigma = 0.0;    // sigma_dr [m] (grip, lateral)
		double TremorSigma = 0.0;   // sigma_t [m]
		double TipASigma = 0.0;     // sA [m]
		double TipBSigma = 0.0;     // sB [m]
		double ElevationSigma = 0.0;// sTh [rad]
		double SpeedSigma = 0.0;    // sV [1]
		double Flinch = 0.0;        // fl [1] (drawn)
		double GripBias = 0.0;      // bias [m] (tip drop, <= 0)
	};

	// Executed minus intended stroke, one additive contribution.
	struct StrokeDelta
	{
		double Azimuth = 0.0;    // [rad]
		double Elevation = 0.0;  // [rad]
		double AxisA = 0.0;      // [1] cue-axis offset / R
		double AxisB = 0.0;      // [1]
		double Speed = 0.0;      // [m/s]
	};

	enum class StrokeSource : std::uint8_t
	{
		Drift,        // channels 1-2 (grip-hand drift through the pivot)
		Tremor,       // channels 3-4
		TipPlacement, // channels 5-6
		Elevation,    // channel 7
		Speed,        // channel 8
		Flinch,       // channel 9 speed loss
		GripTension,  // channel 9 tip drop (bias)
		Warp,         // bow (equipment)
	};
	inline constexpr int kStrokeSourceCount = 8;

	struct StrokeBreakdown
	{
		StrokeDelta Sources[kStrokeSourceCount]; // indexed by StrokeSource; their sum is the executed change BEFORE the
		                                         //   elevation floor, the offset clamp and the speed clamp
		double DriftLat = 0.0;    // D_lat(t_c), D_vert(t_c), T_lat(t_c), T_vert(t_c) process values
		double DriftVert = 0.0;
		double TremorLat = 0.0;
		double TremorVert = 0.0;
		GuardedDraw TipA;         // the per-shot draws used (streak-guarded, 3.2)
		GuardedDraw TipB;
		GuardedDraw Elevation;
		GuardedDraw Speed;
		GuardedDraw Flinch;
		WarpEffect Warp;
		SituationFactors Factors;
	};

	// ---------------------------------------------------------------------------------------------
	// Output
	// ---------------------------------------------------------------------------------------------

	inline constexpr int kMaxShaftContactCandidates = 4;

	struct ExecutedStroke
	{
		ErrorCode Error = ErrorCode::NotImplemented; // Ok, or InvalidArgument for non-finite inputs (Strike then all zero)
		CueStrikeInput Strike;          // what the physics gets (MOT B.1 contract): Speed, Elevation, Azimuth, OffsetA/B (contact
		                                //   point, current dome radius), Cue = CueSpec with TipFriction = TipFrictionKinetic = mu,
		                                //   TipDomeRadius = r_dome, TipRestitution = e_tip; TipTouchesCloth from the executed pose
		Vec2 TipTransverseVelocity;     // (v_r, v_u) [m/s] at contact: animation / audio only, never a physics input (HF-11)
		Vec2 AxisOffset;                // (A_x, B_x) [1] executed cue-AXIS offsets / R (3.5), before AimToContactOffset and the rho
		                                //   clamp; equals SampleHand's AxisOffsetA / B at t_c with the full ramp (budget model 3.10)
		double Rho = 0.0;               // executed |(a, b)| after the clamp
		double MiscueLimit = 0.0;       // mu / sqrt(1 + mu^2) at the contact zone
		TipContactPoint Contact;        // q, beta, zone, weights, coverage, mu (tip wear after the shot)
		bool PredictedMiscue = false;   // rho > MiscueLimit: a PREDICTION; the core decides (MOT B.4, HF-B03)
		bool DoubleHitRisk = false;     // RUL F7 a/b (3.6)
		bool PushRisk = false;          // Frozen < gap <= FrozenEnvelope; a cue ball frozen to that ball is exempt (F7/F8)
		bool OffsetClamped = false;     // rho was clamped to HumanParams::OffsetClamp
		bool ElevationClamped = false;  // the elevation floor bit
		FixedVector<NonTipContact, kMaxShaftContactCandidates> ShaftContactCandidates; // clearance test of the executed pose
		                                //   (UE 5.5 taper, no margin): Ferrule / Shaft / Butt, plus FloorBall if clamped by a
		                                //   ball. UE mesh collision stays authoritative in Sim mode (RUL F10)
		StrokeBreakdown Channels;       // per-channel contributions (replay, diagnosis, stroke report)
	};

	// 3.5 / 3.6 in this order: watchable channels at t_c, per-shot draws (DrawPerShot with History and
	// Params.StreakGuard), warp (equipment: neither scaled by NoiseScale nor affected by ChannelMask), pivot geometry, clamps,
	// tip contact (LookupTipContact with the CURRENT dome radius), friction and flags. NS = Params.NoiseScale, 0 for channels
	// in Params.ChannelMask (channel 9 masks both the flinch and the grip-tension bias). OtherBalls excludes the cue ball.
	// Situation.Intoxication (Q2 hook) enters only through P_x, m_alc and m_alc,t (3.4); at 0 every factor is exactly 1, so
	// the result is bitwise the one without the hook (all HF-T / HF-S values). The per-shot values are always the full
	// draws at contact, whatever t_fwd (the ramp of 3.7 is a rendering of them).
	// History is the shooter's NoiseHistory before Key.ShooterShotIndex, a CACHE only: a history that does not match the key
	// is rebuilt (DrawPerShot), and rollout keys ignore it, so the result is a pure function of the other inputs and the key.
	// NoiseScale 0 with a straight cue (BowSag 0) and neither the elevation floor nor the offset clamp biting returns the
	// intended stroke bit-exactly (HF-T08).
	// Executed pose (3.6, UE 5.5 geometry, no margin): the dome centre is CueBallPosition + (R + r_dome) Q / R (Q = CueContactPoint
	// of the clamped (a, b)), the tip rim (cap boundary, radius w_tip / 2) sits sqrt(r_dome^2 - (w_tip / 2)^2) ahead of it on the
	// axis, and the body runs back from the rim along -d with r(s) = r_t + (r_b - r_t) s / Cue.Length. TipTouchesCloth: the rim,
	// the dome (when its lowest point lies on the cap) or the shaft (s <= CueBody.ShaftLength) below z = 0. Shaft contacts: the
	// minimum over s in [0, Cue.Length] of |P(s) - P_j| - r(s) - R_j < 0, classified by that s (Ferrule / Shaft / Butt), Time 0;
	// plus Situation.FloorBall when the elevation floor set by a ball bites. Double-hit / push risk: the first ball in the cue
	// ball's straight swept corridor (plan view along phi_x; a ball counts when its centre lies ahead within R + R_j of the path
	// line), its surface gap |P_j - C| - R - R_j and cut angle asin(offset / (R + R_j)) (3.6, RUL F7 / F8).
	RB_API ExecutedStroke ExecuteStroke(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const TipState& Tip, const CueBodyState& CueBody, const CueSpec& Cue, const BallSpec& CueBall, const Vec3& CueBallPosition,
		const BallObstacle* OtherBalls, int OtherBallCount, const NoiseKey& Key, const NoiseHistory& History, const HumanParams& Params,
		const TipParams& TipModel = TipParams{});

	// The multipliers and sigmas of 3.4 / 3.5 at time Time (E and k_set at Time; no draws, Flinch = 0; GripBias = -b_grip P_x L_N
	// at full noise scale). The offset-proportional terms of sA / sB use BallRadius (the first overload: kDefaultBallRadius).
	// ExecuteStroke's breakdown holds the same factors at t_c for the cue ball's radius, with Flinch and GripBias as applied
	// (drawn, NoiseScale and ChannelMask included).
	RB_API SituationFactors ComputeSituationFactors(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const HumanParams& Params, double Time);
	RB_API SituationFactors ComputeSituationFactors(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const HumanParams& Params, double Time, double BallRadius);

	// E(t) = 1 + exp(-t / 0.8 s) + min(0.5, 0.03 max(0, t - 10 s)) (HF-07).
	RB_API double SettleInEnvelope(double Time, const HumanParams& Params);

	// k_set(t) of HF-06 (1 without Settle or before t_s; 1 - 0.7 SmoothStep01(tau / 1.2 s); 0.3 until 5.2 s;
	// 0.3 + 0.85 SmoothStep01((tau - 5.2 s) / 1.5 s), ending at 1.15).
	RB_API double SettleFactor(double Time, double SettleStart, const HumanParams& Params);

	// ---------------------------------------------------------------------------------------------
	// Rendering (3.7): the pose at any time, evaluated with the same functions as ExecuteStroke
	// ---------------------------------------------------------------------------------------------

	struct HandPose
	{
		double Time = 0.0;          // [s] since down on the shot
		double GripLateral = 0.0;   // y_g(t) [m], + = shooter's right
		double GripVertical = 0.0;  // z_g(t) [m], + = up
		double TremorRight = 0.0;   // tr_r(t) [m] at the tip
		double TremorUp = 0.0;      // tr_u(t) [m]
		double Ramp = 0.0;          // SmoothStep01((t - t_fwd) / 0.1 s): share of the per-shot channels shown
		StrokeDelta PerShot;        // ramped per-shot contributions (tip placement, elevation, speed, flinch, grip tension)
		WarpEffect Warp;
		double Azimuth = 0.0;       // cue pose at t (equals ExecuteStroke at t = t_c: what you see is what hits)
		double Elevation = 0.0;
		double AxisOffsetA = 0.0;
		double AxisOffsetB = 0.0;
		bool RampShown = false;     // Ramp > 0: an abort now spends the draws (ShooterShotIndex advances, 3.7, HF-B13)
	};

	// Pure like ExecuteStroke (same History contract: a cache, rebuilt if it does not match the key). At Time = t_c with the
	// full ramp (t_c - t_fwd >= RampDuration) the pose equals the executed stroke (A-HUM-2).
	RB_API HandPose SampleHand(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const CueBodyState& CueBody, const CueSpec& Cue, const BallSpec& CueBall, const NoiseKey& Key, const NoiseHistory& History,
		const HumanParams& Params, double Time);

	// ---------------------------------------------------------------------------------------------
	// Counterfactual diagnosis (3.9): up to 5 re-runs with the same NoiseKey, each ONE change against the real shot
	// ---------------------------------------------------------------------------------------------

	// Every re-run starts from COPIES of the real shot's pre-shot inputs (TipState, CueBodyState, SimInput incl. the ball
	// marks, the same NoiseKey): ApplyShotToEquipment and AdvanceNoiseHistory have already changed the live state by then
	// (a stale NoiseHistory is harmless, DrawPerShot rebuilds it; the replay header holds the pre-shot state anyway).
	enum class MissCause : std::uint8_t
	{
		Equipment,         // 1: fresh chalk and no warp (BowSag 0); no channel mask
		Table,             // 2: level table, clean balls (Slope 0, NapPseudoSlope 0, ClingFactor 1, no marks, ChalkCling off)
		HandDrift,         // 3: drift and tremor off ("hand drift / nerves"): ChannelMask kWatchableChannelMask
		TipPlacementSpeed, // 4: per-shot channels off: ChannelMask kPerShotChannelMask (channels 5-9 incl. the grip bias)
		HumanLayer,        // 5: all human channels off: kWatchableChannelMask | kPerShotChannelMask (the warp is equipment, step 1)
		Input,             // none of the above turns the miss into a make: the player's aim, steering or speed
	};

	struct DiagnosisStep
	{
		MissCause Cause = MissCause::Input;
		std::uint32_t ChannelMask = 0;   // added to HumanParams::ChannelMask
		bool FreshChalkNoWarp = false;
		bool LevelCleanTable = false;
	};

	inline constexpr int kDiagnosisStepCount = 5;

	// The fixed order of 3.9 (Index 0..4).
	RB_API DiagnosisStep DiagnosisStepAt(int Index);

	// Human-side overrides of one step (on copies of the real shot's inputs): FreshChalkNoWarp -> every c_z = 1 (mu_fresh; a
	// bar cube's cap of 0.7 is equipment too, so it is not the reference), CueBody.BowSag = 0 (tip shape, glaze and overhang
	// unchanged: removing the overhang would turn a rim contact into a worse ferrule contact); ChannelMask ->
	// Human.ChannelMask |= Step.ChannelMask.
	RB_API void ApplyDiagnosisStep(const DiagnosisStep& Step, HumanParams& Human, TipState& Tip, CueBodyState& CueBody);

	// Physics-side overrides (LevelCleanTable): Tilt.Slope = 0, Tilt.NapPseudoSlope = 0, BallBall.ClingFactor = 1,
	// ChalkCling = false, every ball's ChalkMarks cleared. Balls = SimInput::Balls (kMaxBalls entries).
	RB_API void ApplyDiagnosisStep(const DiagnosisStep& Step, PhysicsParams& Physics, SimBall* Balls);

	// Stroke report shares (Practice / Assisted, 3.9): the cue-ball direction error of each group in the budget model of
	// 3.10 (d_phi + (d alpha_sq / da) d_a, alpha_sq = SquirtAngle with the cue's m / m_e), normalised to sum 1.
	// InputError = the player's own error (intended minus the stroke that makes the shot, from the game's planner).
	struct StrokeShares
	{
		double Input = 0.0;     // "input"
		double Hand = 0.0;      // drift, tremor, tip placement, elevation, speed, flinch, grip tension
		double Equipment = 0.0; // warp; chalk (a miscue from a bare zone) is reported by the diagnosis, not here
	};

	RB_API StrokeShares ComputeStrokeShares(const ExecutedStroke& Stroke, const StrokeDelta& InputError, const CueSpec& Cue, const BallSpec& CueBall);

	// ---------------------------------------------------------------------------------------------
	// After the shot (4.1-4.3): tip wear, chalk-mark deposit and fading
	// ---------------------------------------------------------------------------------------------

	// Tip: HitSeverity(V_x, rho, the core's StrikeResult::Miscue) -> ApplyTipWear at Stroke.Contact. Marks: deposit on the
	// struck ball at the executed contact point (CueContactPoint of MOT B.3, ball orientation SimBall::Orientation at t = 0),
	// then FadeChalkMarks of every ball in play by its ComputeTravelDistances (needs RecordOptions::Trajectories).
	// Marks = the per-ball mark lists (kMaxBalls entries, index = ball id) carried into the next SimInput (nullptr: tip only).
	// One call per strike of the shot (the lag has two cues), in strike order; the fading runs once per shot, in the call for
	// the LAST strike (StrikeIndex == Result.Strikes.Size() - 1, or a shot without strikes), after every deposit. A StrikeIndex
	// outside Result.Strikes changes no tip and deposits nothing. The miscue severity comes from the CORE (HF-B03), never
	// from ExecutedStroke::PredictedMiscue.
	RB_API void ApplyShotToEquipment(const ExecutedStroke& Stroke, const ShotResult& Result, int StrikeIndex, const Quat& StruckBallOrientation,
		TipState& Tip, BallChalkMarks* Marks, const TipParams& TipModel = TipParams{}, const MarkParams& MarkModel = MarkParams{});
}
