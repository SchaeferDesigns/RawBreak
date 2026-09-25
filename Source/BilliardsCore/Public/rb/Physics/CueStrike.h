#pragma once

// Cue strike (cue tip -> cue ball): impulse, spin, squirt, miscue, elevated cue with slate reaction
// at t = 0+ (jump / swerve / masse), and the follow-through tip path used to detect tip re-contacts
// (double hit, push shot). physics-motion-and-cue Part B.
// Owner: WP-1 (motion, slate & cue strike).
//
// Cue frame (B.2): d = (cos th cos ph, cos th sin ph, -sin th) (butt -> tip), e_r = (sin ph, -cos ph, 0)
// (shooter's right), e_u = e_r x d. Offsets (a, b) are CONTACT-POINT offsets / R in the cue face plane:
// a > 0 = right English (gives w_z > 0), b > 0 = above center (follow). pooltool: a_pt = -a, b_pt = b.
// Angles in radians. No randomness: miscue "chaos" and human error are seeded inputs from the game.

#include "rb/Config.h"
#include "rb/Core/Error.h"
#include "rb/Core/Tolerances.h"
#include "rb/Equipment/Cue.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Slate.h"

namespace rb
{
	// Offsets with rho >= this are invalid input (B.1, implementation note 9, T-B18).
	inline constexpr double kCueOffsetValidLimit = 0.95;

	// Largest accepted tip speed [m/s] (prior-art 5.11); the UE input layer clamps to 12 m/s (B.9).
	inline constexpr double kMaxCueSpeed = 15.0;

	// Pinch schedule for elevated cues (B.8.2, TUNING): lambda = smooth((theta - Theta0)/(Theta1 - Theta0)).
	struct PinchParams
	{
		double Theta0Cue = 30.0 * kDegToRad;   // playing / break cues (M > JumpCueMaxMass)
		double Theta1Cue = 70.0 * kDegToRad;
		double Theta0Jump = 60.0 * kDegToRad;  // jump cues (M <= JumpCueMaxMass or CueSpec::JumpCue)
		double Theta1Jump = 85.0 * kDegToRad;
		double PinchRestitution = 0.2;         // e_pinch: rebound of the pinched part (0-0.4)
		double JumpCueMaxMass = 0.35;          // [kg]
	};

	struct CueStrikeInput
	{
		double Speed = 0.0;        // V [m/s]: tip speed along the cue axis just before impact, [0, kMaxCueSpeed]
		double Elevation = 0.0;    // theta [rad], [0, pi/2): butt raised
		double Azimuth = 0.0;      // phi [rad]: stroke direction in the table plane, from +x toward +y
		double OffsetA = 0.0;      // a [1]: contact-point side offset / R (+ = right)
		double OffsetB = 0.0;      // b [1]: contact-point offset / R along e_u (+ = above center)
		CueSpec Cue;
		double LambdaOverride = -1.0; // pinch fraction in [0, 1]; < 0 uses the PinchParams schedule
		bool SquirtEnabled = true;    // false = alpha_sq := 0 (test convention of the motion spec)
		bool TipTouchesCloth = false; // from the UE cue-body model; copied to the rules StrokeRecord (scoop, F9)
	};

	struct CueFrame
	{
		Vec3 Axis;   // d, butt -> tip
		Vec3 Right;  // e_r
		Vec3 Up;     // e_u = e_r x d
	};

	struct StrikeResult
	{
		ErrorCode Error = ErrorCode::NotImplemented;
		BallState State;              // ball right after the strike INCLUDING the slate reaction, classified (A.8)
		Vec3 VelocityAfterTip;        // before the slate reaction [m/s]
		Vec3 OmegaAfterTip;           // before the slate reaction [rad/s]
		Vec3 ImpulseDirection;        // p_hat (with squirt in the grip branch) [1]
		double Impulse = 0.0;         // J [N s]
		double SquirtAngle = 0.0;     // alpha_sq [rad] (0 in the miscue branch)
		double Lambda = 0.0;          // pinch fraction used
		double Rho = 0.0;             // |(a, b)| [1]
		double MiscueLimit = 0.0;     // rho_max = mu_tip / sqrt(1 + mu_tip^2) [1]
		bool Miscue = false;          // rho > rho_max: tip slid, friction-cone impulse (B.5)
		double SeparationMargin = 0.0;// (2/5) e_tip (1 + m/M) - rho^2 [1] (B.6)
		double CueSpeedAfter = 0.0;   // V' = V - J (d . p_hat) / M [m/s] (animation, audio, follow-through)
		double ContactDuration = 0.0; // [s] from CueSpec::ContactTime (records; the strike itself is instantaneous)
		bool SlateContact = false;    // downward impulse reached the slate at t = 0+ (B.8.3)
		bool SlateStick = false;      // slate friction stick branch
	};

	RB_API ErrorCode ValidateCueStrike(const CueStrikeInput& Input);

	RB_API CueFrame MakeCueFrame(double Elevation, double Azimuth);

	// Contact point relative to the ball center: Q = R (a e_r + b e_u - c d), c = sqrt(1 - a^2 - b^2) (B.3).
	RB_API Vec3 CueContactPoint(const CueFrame& Frame, double OffsetA, double OffsetB, double Radius);

	// Cue-AXIS offsets (A, B) / R -> contact-point offsets: (a, b) = (A, B) R / (R + r_tip) (B.3, T-B11).
	RB_API Vec2 AimToContactOffset(const Vec2& AxisOffset, double Radius, double TipDomeRadius);

	// rho_max = mu_tip / sqrt(1 + mu_tip^2) (B.4): 0.5144958 for mu_tip = 0.6.
	RB_API double MiscueLimit(double TipFriction);

	// alpha_sq = atan2((1/k) a sqrt(1 - a^2), 1 + m_r + (1/k)(1 - a^2)), m_r = m / m_e (B.7, TP A.31;
	// 1/k = 5/2 for a solid ball). Positive for a > 0 (the ball deflects LEFT). Returns radians.
	RB_API double SquirtAngle(double OffsetA, double BallToEndMassRatio, double InertiaK = kSolidSphereInertiaFactor);

	// Pinch fraction lambda(theta) (B.8.2).
	RB_API double PinchLambda(double Elevation, const CueSpec& Cue, const PinchParams& Pinch);

	// k e_tip (1 + m/M) - rho^2 (B.6; k = 2/5 for a solid ball).
	RB_API double SeparationMargin(double Rho, double TipRestitution, double BallMass, double CueMass, double InertiaK = kSolidSphereInertiaFactor);

	// Full strike (B.8.5, generalised with k = InertiaFactor(Spec)): validates, computes J / p_hat / w
	// (grip or miscue branch), applies squirt
	// once (grip branch only), resolves the slate reaction at t = 0+ (B.8.3, with e_eff and v_z_min)
	// and classifies. Ball must be at rest (Stationary) unless Numerics-free test use; on invalid input
	// Error != Ok and State == Ball (no state change, T-B18).
	RB_API StrikeResult StrikeCueBall(const CueStrikeInput& Input, const BallState& Ball, const BallSpec& Spec, const ClothParams& Cloth,
		const SlateParams& Slate, const PinchParams& Pinch, double Gravity, const NumericsConfig& Numerics);

	// ---------------------------------------------------------------------------------------------
	// Follow-through (ARCHITECTURE DECISION, DERIVED/TUNING): after the impact the cue keeps moving
	// along d with speed V' and decelerates uniformly to rest over CueSpec::FollowThroughDistance. The
	// tip dome (sphere of radius r_tip, centered on the cue axis) can touch ANY in-play ball again: the
	// struck ball (double hit / push) or another ball (a "touched ball", rules F10, recorded as
	// NonTipContact{Source = CueTip}). Each re-contact of a ball in event mode is a TipRecontact event
	// (tier Strike) resolved as a new tip impulse; if the touched ball is inside a CLI island, the tip is
	// an island participant instead (rb/Physics/Compliant.h IslandTip) and contact intervals come from
	// positive tip force. Contacts closer than Cue.ContactTime are merged into one long tip contact.
	// rules.md F7/F8 read the resulting StrokeRecord::TipContacts (double hit = two intervals, push =
	// one interval longer than T_push). Every tip path piece is also stored in ShotResult::CueTips so
	// that the renderer animates exactly the cue the rules judged.
	// ---------------------------------------------------------------------------------------------
	struct CueTipPath
	{
		int Strike = 0;            // index into SimInput::Strikes (the lag has two cues)
		BallId StruckBall = kNoBall; // ball struck at t = 0 by this cue
		Vec3 Start;                // tip dome center at the first contact (t = 0) [m]
		Vec3 Direction;            // d [1]
		double Speed0 = 0.0;       // V' along d at t = 0+ [m/s] (after the latest tip impulse)
		double Deceleration = 0.0; // [m/s^2] = V'^2 / (2 FollowThroughDistance)
		double StartTime = 0.0;    // absolute time of Start/Speed0 [s]
		double StopTime = 0.0;     // absolute time the cue comes to rest [s]
		double DomeRadius = 0.0;   // r_tip [m]
	};

	// Path right after the strike (t = 0+). Strike / StruckBall are filled by the caller.
	RB_API CueTipPath MakeCueTipPath(const CueStrikeInput& Input, const StrikeResult& Strike, const Vec3& BallCenter, double BallRadius);

	// The tip dome center as a quadratic trajectory (State = Airborne-like, no gravity), so that tip
	// re-contacts are detected with the ball-ball quartic (rb/Physics/Detect.h PredictBallBall with the
	// tip dome radius). TauEnd = StopTime - StartTime.
	RB_API MotionSegment CueTipAsSegment(const CueTipPath& Path);

	struct TipRecontactResult
	{
		BallState Ball;            // after the re-contact impulse (slate reaction applied, classified)
		CueTipPath Tip;            // cue after the impulse (slower)
		double Impulse = 0.0;      // [N s]
		double RelativeSpeed = 0.0;// closing speed along the contact normal [m/s]
	};

	// Impulse between the moving tip (mass M, constrained to move along d) and a ball (the struck ball
	// or any other ball in event mode) at absolute time Time, using the tip restitution and the
	// grip/miscue logic of B.5 with the relative velocity; the table reaction uses Surface (the support
	// the ball is on; ignored if it is airborne).
	RB_API TipRecontactResult ResolveTipRecontact(const CueTipPath& Tip, double Time, const BallState& Ball, const BallSpec& Spec, const CueSpec& Cue,
		const ClothParams& Surface, const SlateParams& Slate, double Gravity, const NumericsConfig& Numerics);
}
