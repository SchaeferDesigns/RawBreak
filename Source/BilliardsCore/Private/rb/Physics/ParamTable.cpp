#include "rb/Core/FpGuard.h"
// Owner: WP-6a (PhysicsParams owner). Reflected PhysicsParams keys (rb/Physics/ParamTable.h).
// Generated once from the PhysicsParams layout; keep the key list append-only (replay / tooling contract).
#include "rb/Physics/ParamTable.h"

#include "rb/Math/Scalar.h"

#include <cstring>

namespace rb
{
	namespace
	{
		struct ParamEntry
		{
			PhysicsParamInfo Info;
			double (*Get)(const PhysicsParams&);
			void (*Set)(PhysicsParams&, double);
		};

		constexpr ParamEntry kEntries[] = {
			{{"origin", ParamType::Enum, 2, "1"}, [](const PhysicsParams& P) { return static_cast<double>(static_cast<int>(P.Origin)); }, [](PhysicsParams& P, double V) { P.Origin = static_cast<ParamsOrigin>(static_cast<int>(V)); }},
			{{"gravity", ParamType::Real, 0, "m/s^2"}, [](const PhysicsParams& P) { return P.Gravity; }, [](PhysicsParams& P, double V) { P.Gravity = V; }},
			{{"cloth.mu_s", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cloth.SlidingFriction; }, [](PhysicsParams& P, double V) { P.Cloth.SlidingFriction = V; }},
			{{"cloth.mu_r", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cloth.RollingResistance; }, [](PhysicsParams& P, double V) { P.Cloth.RollingResistance = V; }},
			{{"cloth.alpha_sp", ParamType::Real, 0, "rad/s^2"}, [](const PhysicsParams& P) { return P.Cloth.SpinDeceleration; }, [](PhysicsParams& P, double V) { P.Cloth.SpinDeceleration = V; }},
			{{"slate.e", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Slate.Restitution; }, [](PhysicsParams& P, double V) { P.Slate.Restitution = V; }},
			{{"slate.h_min", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Slate.MinBounceHeight; }, [](PhysicsParams& P, double V) { P.Slate.MinBounceHeight = V; }},
			{{"slate.n_max", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Slate.MaxBounces); }, [](PhysicsParams& P, double V) { P.Slate.MaxBounces = static_cast<int>(V); }},
			{{"pinch.theta0_cue", ParamType::Real, 0, "rad"}, [](const PhysicsParams& P) { return P.Pinch.Theta0Cue; }, [](PhysicsParams& P, double V) { P.Pinch.Theta0Cue = V; }},
			{{"pinch.theta1_cue", ParamType::Real, 0, "rad"}, [](const PhysicsParams& P) { return P.Pinch.Theta1Cue; }, [](PhysicsParams& P, double V) { P.Pinch.Theta1Cue = V; }},
			{{"pinch.theta0_jump", ParamType::Real, 0, "rad"}, [](const PhysicsParams& P) { return P.Pinch.Theta0Jump; }, [](PhysicsParams& P, double V) { P.Pinch.Theta0Jump = V; }},
			{{"pinch.theta1_jump", ParamType::Real, 0, "rad"}, [](const PhysicsParams& P) { return P.Pinch.Theta1Jump; }, [](PhysicsParams& P, double V) { P.Pinch.Theta1Jump = V; }},
			{{"pinch.e_pinch", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Pinch.PinchRestitution; }, [](PhysicsParams& P, double V) { P.Pinch.PinchRestitution = V; }},
			{{"pinch.jump_cue_max_mass", ParamType::Real, 0, "kg"}, [](const PhysicsParams& P) { return P.Pinch.JumpCueMaxMass; }, [](PhysicsParams& P, double V) { P.Pinch.JumpCueMaxMass = V; }},
			{{"ballball.model", ParamType::Enum, 1, "1"}, [](const PhysicsParams& P) { return static_cast<double>(static_cast<int>(P.BallBall.Model)); }, [](PhysicsParams& P, double V) { P.BallBall.Model = static_cast<BallBallModel>(static_cast<int>(V)); }},
			{{"ballball.e", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.BallBall.Restitution; }, [](PhysicsParams& P, double V) { P.BallBall.Restitution = V; }},
			{{"ballball.friction", ParamType::Enum, 2, "1"}, [](const PhysicsParams& P) { return static_cast<double>(static_cast<int>(P.BallBall.Friction)); }, [](PhysicsParams& P, double V) { P.BallBall.Friction = static_cast<BallBallFrictionModel>(static_cast<int>(V)); }},
			{{"ballball.mu_a", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.BallBall.MuA; }, [](PhysicsParams& P, double V) { P.BallBall.MuA = V; }},
			{{"ballball.mu_b", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.BallBall.MuB; }, [](PhysicsParams& P, double V) { P.BallBall.MuB = V; }},
			{{"ballball.mu_c", ParamType::Real, 0, "s/m"}, [](const PhysicsParams& P) { return P.BallBall.MuC; }, [](PhysicsParams& P, double V) { P.BallBall.MuC = V; }},
			{{"ballball.mu_constant", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.BallBall.MuConstant; }, [](PhysicsParams& P, double V) { P.BallBall.MuConstant = V; }},
			{{"ballball.k_cling", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.BallBall.ClingFactor; }, [](PhysicsParams& P, double V) { P.BallBall.ClingFactor = V; }},
			{{"cushion.model", ParamType::Enum, 3, "1"}, [](const PhysicsParams& P) { return static_cast<double>(static_cast<int>(P.Cushion.OnClothModel)); }, [](PhysicsParams& P, double V) { P.Cushion.OnClothModel = static_cast<CushionModel>(static_cast<int>(V)); }},
			{{"cushion.e_max", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.Restitution.Max; }, [](PhysicsParams& P, double V) { P.Cushion.Restitution.Max = V; }},
			{{"cushion.e_slope", ParamType::Real, 0, "s/m"}, [](const PhysicsParams& P) { return P.Cushion.Restitution.Slope; }, [](PhysicsParams& P, double V) { P.Cushion.Restitution.Slope = V; }},
			{{"cushion.e_knee", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Cushion.Restitution.Knee; }, [](PhysicsParams& P, double V) { P.Cushion.Restitution.Knee = V; }},
			{{"cushion.e_min", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.Restitution.Min; }, [](PhysicsParams& P, double V) { P.Cushion.Restitution.Min = V; }},
			{{"cushion.mu_w", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.Friction; }, [](PhysicsParams& P, double V) { P.Cushion.Friction = V; }},
			{{"cushion.mathavan_steps", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Cushion.MathavanSteps); }, [](PhysicsParams& P, double V) { P.Cushion.MathavanSteps = static_cast<int>(V); }},
			{{"cushion.mathavan_max_bisections", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Cushion.MathavanMaxBisections); }, [](PhysicsParams& P, double V) { P.Cushion.MathavanMaxBisections = static_cast<int>(V); }},
			{{"cushion.mathavan_split", ParamType::Boolean, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.MathavanSplitAtSlipReversal ? 1.0 : 0.0; }, [](PhysicsParams& P, double V) { P.Cushion.MathavanSplitAtSlipReversal = V != 0.0; }},
			{{"cushion.stronge_omega_ratio", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.StrongeOmegaRatio; }, [](PhysicsParams& P, double V) { P.Cushion.StrongeOmegaRatio = V; }},
			{{"cushion.nose_profile_radius", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Cushion.NoseProfileRadius; }, [](PhysicsParams& P, double V) { P.Cushion.NoseProfileRadius = V; }},
			{{"cushion.pooltool_compat", ParamType::Boolean, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.PooltoolCompat ? 1.0 : 0.0; }, [](PhysicsParams& P, double V) { P.Cushion.PooltoolCompat = V != 0.0; }},
			{{"cushion.facing_k_f", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.FacingRestitutionScale; }, [](PhysicsParams& P, double V) { P.Cushion.FacingRestitutionScale = V; }},
			{{"cushion.facing_mu", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cushion.FacingFriction; }, [](PhysicsParams& P, double V) { P.Cushion.FacingFriction = V; }},
			{{"pocket.liner_e", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.PocketContacts.LinerRestitution; }, [](PhysicsParams& P, double V) { P.PocketContacts.LinerRestitution = V; }},
			{{"pocket.liner_mu", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.PocketContacts.LinerFriction; }, [](PhysicsParams& P, double V) { P.PocketContacts.LinerFriction = V; }},
			{{"pocket.rim_e", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.PocketContacts.RimRestitution; }, [](PhysicsParams& P, double V) { P.PocketContacts.RimRestitution = V; }},
			{{"pocket.rim_mu", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.PocketContacts.RimFriction; }, [](PhysicsParams& P, double V) { P.PocketContacts.RimFriction = V; }},
			{{"pocket.railtop_e", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.PocketContacts.RailTopRestitution; }, [](PhysicsParams& P, double V) { P.PocketContacts.RailTopRestitution = V; }},
			{{"pocket.railtop_mu", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.PocketContacts.RailTopFriction; }, [](PhysicsParams& P, double V) { P.PocketContacts.RailTopFriction = V; }},
			{{"pocket.railtop_mu_r", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.PocketContacts.RailTopRollingResistance; }, [](PhysicsParams& P, double V) { P.PocketContacts.RailTopRollingResistance = V; }},
			{{"pocket.railtop_alpha_sp", ParamType::Real, 0, "rad/s^2"}, [](const PhysicsParams& P) { return P.PocketContacts.RailTopSpinDeceleration; }, [](PhysicsParams& P, double V) { P.PocketContacts.RailTopSpinDeceleration = V; }},
			{{"pockets.model", ParamType::Enum, 1, "1"}, [](const PhysicsParams& P) { return static_cast<double>(static_cast<int>(P.Pockets)); }, [](PhysicsParams& P, double V) { P.Pockets = static_cast<PocketModel>(static_cast<int>(V)); }},
			{{"cli.hertz_k", ParamType::Real, 0, "N/m^1.5"}, [](const PhysicsParams& P) { return P.Cli.HertzStiffness; }, [](PhysicsParams& P, double V) { P.Cli.HertzStiffness = V; }},
			{{"cli.tsuji_alpha", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cli.TsujiAlpha; }, [](PhysicsParams& P, double V) { P.Cli.TsujiAlpha = V; }},
			{{"cli.dt", ParamType::Real, 0, "s"}, [](const PhysicsParams& P) { return P.Cli.TimeStep; }, [](PhysicsParams& P, double V) { P.Cli.TimeStep = V; }},
			{{"cli.k_c", ParamType::Real, 0, "N/m"}, [](const PhysicsParams& P) { return P.Cli.CushionStiffness; }, [](PhysicsParams& P, double V) { P.Cli.CushionStiffness = V; }},
			{{"cli.s_reg", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Cli.SlipRegularization; }, [](PhysicsParams& P, double V) { P.Cli.SlipRegularization = V; }},
			{{"cli.exit_zero_force_steps", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Cli.ExitZeroForceSteps); }, [](PhysicsParams& P, double V) { P.Cli.ExitZeroForceSteps = static_cast<int>(V); }},
			{{"cli.join_factor", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Cli.JoinFactor; }, [](PhysicsParams& P, double V) { P.Cli.JoinFactor = V; }},
			{{"cli.rigid_dt", ParamType::Real, 0, "s"}, [](const PhysicsParams& P) { return P.Cli.RigidTimeStep; }, [](PhysicsParams& P, double V) { P.Cli.RigidTimeStep = V; }},
			{{"cli.rigid_iterations", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Cli.RigidIterations); }, [](PhysicsParams& P, double V) { P.Cli.RigidIterations = static_cast<int>(V); }},
			{{"cli.sustained_speed", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Cli.SustainedSpeed; }, [](PhysicsParams& P, double V) { P.Cli.SustainedSpeed = V; }},
			{{"cli.sustained_steps", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Cli.SustainedSteps); }, [](PhysicsParams& P, double V) { P.Cli.SustainedSteps = static_cast<int>(V); }},
			{{"cli.grid_cell", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Cli.GridCell; }, [](PhysicsParams& P, double V) { P.Cli.GridCell = V; }},
			{{"cli.cloth_support", ParamType::Boolean, 0, "1"}, [](const PhysicsParams& P) { return P.Cli.ClothSupport ? 1.0 : 0.0; }, [](PhysicsParams& P, double V) { P.Cli.ClothSupport = V != 0.0; }},
			{{"numerics.eps_z", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.EpsZ; }, [](PhysicsParams& P, double V) { P.Numerics.EpsZ = V; }},
			{{"numerics.eps_v", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Numerics.EpsV; }, [](PhysicsParams& P, double V) { P.Numerics.EpsV = V; }},
			{{"numerics.eps_w_r", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Numerics.EpsWTimesRadius; }, [](PhysicsParams& P, double V) { P.Numerics.EpsWTimesRadius = V; }},
			{{"numerics.snap_residual_rel", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Numerics.SnapResidualRel; }, [](PhysicsParams& P, double V) { P.Numerics.SnapResidualRel = V; }},
			{{"numerics.contact_tol", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.ContactTol; }, [](PhysicsParams& P, double V) { P.Numerics.ContactTol = V; }},
			{{"numerics.approach_speed_tol", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Numerics.ApproachSpeedTol; }, [](PhysicsParams& P, double V) { P.Numerics.ApproachSpeedTol = V; }},
			{{"numerics.tangency_tol_per_length", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.TangencyTolPerLength; }, [](PhysicsParams& P, double V) { P.Numerics.TangencyTolPerLength = V; }},
			{{"numerics.overlap_guard", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.OverlapGuard; }, [](PhysicsParams& P, double V) { P.Numerics.OverlapGuard = V; }},
			{{"numerics.segment_param_slack", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.SegmentParamSlack; }, [](PhysicsParams& P, double V) { P.Numerics.SegmentParamSlack = V; }},
			{{"numerics.root_trim_rel", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Numerics.RootTrimRel; }, [](PhysicsParams& P, double V) { P.Numerics.RootTrimRel = V; }},
			{{"numerics.root_time_tol", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Numerics.RootTimeTol; }, [](PhysicsParams& P, double V) { P.Numerics.RootTimeTol = V; }},
			{{"numerics.root_max_iterations", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Numerics.RootMaxIterations); }, [](PhysicsParams& P, double V) { P.Numerics.RootMaxIterations = static_cast<int>(V); }},
			{{"numerics.rest_speed", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Numerics.RestSpeed; }, [](PhysicsParams& P, double V) { P.Numerics.RestSpeed = V; }},
			{{"numerics.zeno_contact_count", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Numerics.ZenoContactCount); }, [](PhysicsParams& P, double V) { P.Numerics.ZenoContactCount = static_cast<int>(V); }},
			{{"numerics.zeno_window", ParamType::Real, 0, "s"}, [](const PhysicsParams& P) { return P.Numerics.ZenoWindow; }, [](PhysicsParams& P, double V) { P.Numerics.ZenoWindow = V; }},
			{{"numerics.max_events", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Numerics.MaxEvents); }, [](PhysicsParams& P, double V) { P.Numerics.MaxEvents = static_cast<int>(V); }},
			{{"numerics.time_horizon", ParamType::Real, 0, "s"}, [](const PhysicsParams& P) { return P.Numerics.TimeHorizon; }, [](PhysicsParams& P, double V) { P.Numerics.TimeHorizon = V; }},
			{{"numerics.compliant_max_duration", ParamType::Real, 0, "s"}, [](const PhysicsParams& P) { return P.Numerics.CompliantMaxDuration; }, [](PhysicsParams& P, double V) { P.Numerics.CompliantMaxDuration = V; }},
			{{"numerics.max_island_steps", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Numerics.MaxIslandSteps); }, [](PhysicsParams& P, double V) { P.Numerics.MaxIslandSteps = static_cast<int>(V); }},
			{{"numerics.cushion_slip_eps", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Numerics.CushionSlipEps; }, [](PhysicsParams& P, double V) { P.Numerics.CushionSlipEps = V; }},
			{{"numerics.pivot_min_speed", ParamType::Real, 0, "m/s"}, [](const PhysicsParams& P) { return P.Numerics.PivotMinSpeed; }, [](PhysicsParams& P, double V) { P.Numerics.PivotMinSpeed = V; }},
			{{"numerics.pivot_simpson_panels", ParamType::Integer, 0, "1"}, [](const PhysicsParams& P) { return static_cast<double>(P.Numerics.PivotSimpsonPanels); }, [](PhysicsParams& P, double V) { P.Numerics.PivotSimpsonPanels = static_cast<int>(V); }},
			{{"numerics.line_cross_eps", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.LineCrossEps; }, [](PhysicsParams& P, double V) { P.Numerics.LineCrossEps = V; }},
			{{"numerics.leave_distance", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.LeaveDistance; }, [](PhysicsParams& P, double V) { P.Numerics.LeaveDistance = V; }},
			{{"numerics.sample_tolerance", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Numerics.SampleTolerance; }, [](PhysicsParams& P, double V) { P.Numerics.SampleTolerance = V; }},
			{{"numerics.sample_max_interval", ParamType::Real, 0, "s"}, [](const PhysicsParams& P) { return P.Numerics.SampleMaxInterval; }, [](PhysicsParams& P, double V) { P.Numerics.SampleMaxInterval = V; }},
			// human-factors 4.3 / 4.5 (architecture v1.2): table tilt, nap, chalk-mark cling
			{{"tilt.slope_x", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Tilt.Slope.x; }, [](PhysicsParams& P, double V) { P.Tilt.Slope.x = V; }},
			{{"tilt.slope_y", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Tilt.Slope.y; }, [](PhysicsParams& P, double V) { P.Tilt.Slope.y = V; }},
			{{"tilt.tolerance", ParamType::Real, 0, "m"}, [](const PhysicsParams& P) { return P.Tilt.Tolerance; }, [](PhysicsParams& P, double V) { P.Tilt.Tolerance = V; }},
			{{"tilt.refresh_max_interval", ParamType::Real, 0, "s"}, [](const PhysicsParams& P) { return P.Tilt.RefreshMaxInterval; }, [](PhysicsParams& P, double V) { P.Tilt.RefreshMaxInterval = V; }},
			{{"tilt.nap_x", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Tilt.NapPseudoSlope.x; }, [](PhysicsParams& P, double V) { P.Tilt.NapPseudoSlope.x = V; }},
			{{"tilt.nap_y", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Tilt.NapPseudoSlope.y; }, [](PhysicsParams& P, double V) { P.Tilt.NapPseudoSlope.y = V; }},
			{{"tilt.nap_resistance", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.Tilt.NapResistance; }, [](PhysicsParams& P, double V) { P.Tilt.NapResistance = V; }},
			{{"ballball.k_chalk", ParamType::Real, 0, "1"}, [](const PhysicsParams& P) { return P.BallBall.ChalkClingFactor; }, [](PhysicsParams& P, double V) { P.BallBall.ChalkClingFactor = V; }},
			{{"ballball.chalk_cling", ParamType::Boolean, 0, "1"}, [](const PhysicsParams& P) { return P.ChalkCling ? 1.0 : 0.0; }, [](PhysicsParams& P, double V) { P.ChalkCling = V != 0.0; }},
		};

		constexpr int kEntryCount = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));

		const ParamEntry* Find(const char* Key)
		{
			if (Key == nullptr)
			{
				return nullptr;
			}
			for (const ParamEntry& Entry : kEntries)
			{
				if (std::strcmp(Entry.Info.Key, Key) == 0)
				{
					return &Entry;
				}
			}
			return nullptr;
		}
	}

	int PhysicsParamCount()
	{
		return kEntryCount;
	}

	PhysicsParamInfo PhysicsParamAt(int Index)
	{
		return (Index >= 0 && Index < kEntryCount) ? kEntries[Index].Info : PhysicsParamInfo{};
	}

	bool GetPhysicsParam(const PhysicsParams& Params, const char* Key, double& Out)
	{
		const ParamEntry* Entry = Find(Key);
		if (Entry == nullptr)
		{
			return false;
		}
		Out = Entry->Get(Params);
		return true;
	}

	bool SetPhysicsParam(PhysicsParams& Params, const char* Key, double Value)
	{
		const ParamEntry* Entry = Find(Key);
		if (Entry == nullptr || !IsFinite(Value))
		{
			return false;
		}
		if (Entry->Info.Type != ParamType::Real)
		{
			const double Min = -2147483648.0;
			const double Max = 2147483647.0;
			if (Value < Min || Value > Max || static_cast<double>(static_cast<long long>(Value)) != Value)
			{
				return false;
			}
			if (Entry->Info.Type == ParamType::Boolean && Value != 0.0 && Value != 1.0)
			{
				return false;
			}
			if (Entry->Info.Type == ParamType::Enum && (Value < 0.0 || Value > static_cast<double>(Entry->Info.EnumMax)))
			{
				return false;
			}
		}
		Entry->Set(Params, Value);
		return true;
	}
}
