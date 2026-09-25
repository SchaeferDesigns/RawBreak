#pragma once

// ALL numerical tolerances and termination guards of the core, in one place (Docs/architecture.md,
// "Numerical tolerances"). Physics tolerances live in NumericsConfig (recorded with every shot);
// rules tolerances (rules.md 3.6) live in RulesTolerances. Model parameters (friction, restitution,
// step sizes of integrators) are NOT tolerances; they live with their models.
// Owner: WP-0 (architecture, frozen).
//
// Where two specs disagree, the verified physics specs win (see architecture.md "Spec conflicts").

#include "rb/Config.h"

namespace rb
{
	struct NumericsConfig
	{
		// --- State classification (physics-motion-and-cue A.8) ---------------------------------------
		double EpsZ = 1e-9;             // [m]   height above rest (z - R) that counts as airborne
		double EpsV = 1e-9;             // [m/s] speed / slip magnitude below which it is zero
		double EpsWTimesRadius = 1e-9;  // [m/s] eps_w = EpsWTimesRadius / R  (= eps_v / R = 3.5e-8 rad/s for pool)
		double SnapResidualRel = 1e-9;  // [1]   debug check: residual discarded by a transition snap <= this * magnitude
		                                //       (prior-art 5.8); a larger residual is logged as a Diagnostic event

		// --- Contact geometry (physics-collisions 3.4, 3.6, 4.10) ------------------------------------
		double ContactTol = 1e-9;        // [m]   eps_touch: |gap| <= this counts as touching (numerical contact)
		double ApproachSpeedTol = 1e-9;  // [m/s] v_eps: |normal speed| <= this is "zero" for the pressing rule
		double TangencyTolPerLength = 1e-9; // [m] eps_f = 2 (R1 + R2) * this; grazing double roots are misses
		double OverlapGuard = 1e-6;      // [m]   overlap beyond this = corrupt state: log Diagnostic, treat as
		                                 //       touching, never move balls (no "make_kiss")
		double SegmentParamSlack = 1e-9; // [m]   along-segment tolerance at cushion/jaw joints (prior-art 5.7)

		// --- Root isolation (physics-collisions 3.3, 3.4; rb/Math/Polynomial.h) ----------------------
		double RootTrimRel = 1e-14;      // [1]   leading coefficients <= this * max|coef| are dropped (degree reduction)
		double RootTimeTol = 1e-13;      // [1]   Newton/bisection stop on the scaled time s in [0, 1]
		int RootMaxIterations = 100;

		// --- Zeno and termination guards (physics-collisions 7.3, motion C.4) -------------------------
		double RestSpeed = 2e-3;         // [m/s] v_rest: approach speeds below this use restitution 0
		                                 //       (ball-ball and cushion micro-impacts)
		int ZenoContactCount = 8;        // same pair (ball-ball or ball-feature) with this many contacts ...
		double ZenoWindow = 10e-3;       // [s]   ... within this window is moved into a CLI island
		int MaxEvents = 20000;           // hard cap of processed events per shot -> SimStatus::Aborted
		double TimeHorizon = 600.0;      // [s]   safety net for the shot duration -> SimStatus::HorizonReached
		double CompliantMaxDuration = 50e-3; // [s] an island still in Compliant (Hertz) mode after this long is switched to
		                                 //       Rigid mode (collisions 3.9.2 "safety"); islands then run until their contacts
		                                 //       open or the balls rest (collisions 7.3). Never a fallback to plain impulses.
		int MaxIslandSteps = 200000;     // per-shot budget of island steps (compliant + rigid, all islands). Exceeding it stops
		                                 //       all balls like MaxEvents (SimStatus::Aborted, Diagnostics.IslandBudgetExceeded)

		// --- Cushion (Mathavan) integrator ------------------------------------------------------------
		double CushionSlipEps = 1e-6;    // [m/s] s_eps: slips below this carry no friction (collisions 4.5)

		// --- Pocket pivot (physics-collisions 5.4) ----------------------------------------------------
		double PivotMinSpeed = 1e-4;     // [m/s] v0 floor for the pivot time integral (T_p ~ ln(1/v0))
		int PivotSimpsonPanels = 16;     // Simpson panels in the asinh-substituted pivot integral

		// --- Rules-facing physics tolerances (must equal the RulesTolerances defaults) ----------------
		double LineCrossEps = 1e-6;      // [m] BallLineCross fires when the center passes line +- this (= eps_line)
		double LeaveDistance = 5e-4;     // [m] eps_leave: continuesInitialFreeze ends / island records re-arm

		// --- Recording (playback accuracy, not physics) ------------------------------------------------
		double SampleTolerance = 1e-5;   // [m] island / pivot playback: a new Sampled segment starts when linear interpolation
		                                 //     of any member's center would deviate by more than this (adaptive sampling)
		double SampleMaxInterval = 10e-3;// [s] ... or after this long at the latest (and at every contact begin / end)
	};

	// rules.md 3.6 (all DERIVED defaults; ranges in rules.md 13).
	struct RulesTolerances
	{
		double TieWindow = 0.5e-3;           // [s]   eps_tie: contacts this close count as simultaneous (only helps the shooter)
		double Frozen = 1.0e-4;              // [m]   eps_frozen: resting gap that counts as touching (ball-ball, ball-rail)
		double Leave = 5.0e-4;               // [m]   eps_leave: separation after which a frozen ball has left
		double Line = 1.0e-6;                // [m]   eps_line: string/spot predicate tolerance
		double PlacementOverlap = 1.0e-7;    // [m]   eps_overlap: cue-ball placement overlap tolerance
		double PushDuration = 4.0e-3;        // [s]   T_push: longest normal tip contact
		double GrazeAngle = 1.3089969389957472; // [rad] phi_graze = 75 deg ("barely grazes", R 3.7)
		double FrozenEnvelope = 5.0e-3;      // [m]   d_sep: frozen-ball exemption envelope (F7/F8)
		double SettleWindow = 5.0;           // [s]   T_settle: hanging-ball window (R 2.2)
		double LagTie = 5.0e-4;              // [m]   eps_lag: lag distances closer than this re-lag
		double SpotCueBallGap = 1.0e-3;      // [m]   delta_cbGap: gap between a spotted ball and the cue ball
	};
}
