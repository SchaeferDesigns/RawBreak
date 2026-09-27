#include "rb/Core/FpGuard.h"
// Owner: WP-6a (simulator core loop). Spec: Docs/architecture.md "Event loop" (8.1-8.7, 8.10, 8.11), 9, 11; physics-collisions 1,
// 3.6-3.7, 7.1-7.4; physics-motion-and-cue implementation notes 1, 2, 6, 8, 10, 12; prior-art 5.1-5.4, 5.8-5.11.
// This file: parameter construction and validation, input validation, the Simulator object and the main loop (8.4).
// Slots, recording, observers and services: SimLoop.cpp; event processing and strikes: SimLoopEvents.cpp.
#include "rb/Physics/Simulator.h"

#include "SimInternal.h"

#include "rb/Math/Scalar.h"
#include "rb/Shot/ShotRecordBuilder.h"

namespace rb
{
	// Private per-simulator state (allocated once in the constructor, reused by every Run).
	struct Simulator::Workspace
	{
		sim::Workspace Data;
	};

	namespace
	{
		bool Positive(double X) { return IsFinite(X) && X > 0.0; }
		bool NonNegative(double X) { return IsFinite(X) && X >= 0.0; }
		bool UnitInterval(double X) { return IsFinite(X) && X >= 0.0 && X <= 1.0; }

		// Largest drive of a resting ball on a tilted table against its static rolling resistance (human-factors 4.5.1 with the
		// per-ball k): |Slope| / (1 + k) + |Nap| <= (1 - eta_n) mu_r / 2 on the cloth, |Slope| / (1 + k) <= mu_r / 2 on the cap.
		bool TiltWithinRestLimit(const TiltParams& Tilt, double InertiaK, double ClothMuR, double CapMuR)
		{
			const double Slope = Length(Tilt.Slope);
			const double Nap = Length(Tilt.NapPseudoSlope);
			const double Drive = Slope / (1.0 + InertiaK);
			return !(Drive + Nap > (1.0 - Tilt.NapResistance) * ClothMuR * 0.5) && !(Drive > CapMuR * 0.5);
		}

		// Point strictly usable as "on the flat cap": inside the convex CCW polygon and outside its cut disc.
		bool InsidePolygon(const RailTopPolygon& Poly, const Vec2& Q)
		{
			const int Count = Poly.VertexCount;
			if (Count < 3)
			{
				return false;
			}
			for (int i = 0; i < Count; ++i)
			{
				const Vec2& A = Poly.Vertices[i];
				const Vec2& B = Poly.Vertices[(i + 1) % Count];
				if (Cross(B - A, Q - A) < 0.0)
				{
					return false;
				}
			}
			return !(Poly.HasCut && LengthSquared(Q - Poly.CutCenter) < Poly.CutRadius * Poly.CutRadius);
		}

		bool FiniteState(const BallState& S)
		{
			return IsFinite(S.Position.x) && IsFinite(S.Position.y) && IsFinite(S.Position.z) && IsFinite(S.Velocity.x) && IsFinite(S.Velocity.y) &&
				IsFinite(S.Velocity.z) && IsFinite(S.Omega.x) && IsFinite(S.Omega.y) && IsFinite(S.Omega.z);
		}

		// Input validation (architecture 8.4) and the classification of the initial states. Contexts get the support surface
		// (cloth, or the flat rail cap for a ball resting on a cap polygon).
		ErrorCode ValidateInput(const SimInput& Input, BallState* States, BallTableContext* Contexts)
		{
			if (Input.Table == nullptr)
			{
				return ErrorCode::InvalidArgument;
			}
			const TableGeometry& Table = *Input.Table;
			if (Table.Noses.Size() != kCushionCount || !(Table.Spec.Length > 0.0) || !(Table.Spec.Width > 0.0))
			{
				return ErrorCode::InvalidTable;
			}
			const PhysicsParams& Params = Input.Params;
			const ErrorCode ParamsError = ValidatePhysicsParams(Params);
			if (ParamsError != ErrorCode::Ok)
			{
				return ParamsError;
			}
			const NumericsConfig& Numerics = Params.Numerics;
			const double CapZ = Table.Spec.RailTopZ;
			for (int b = 0; b < kMaxBalls; ++b)
			{
				const SimBall& Ball = Input.Balls[b];
				if (!Ball.InPlay)
				{
					continue;
				}
				const BallSpec& Spec = Ball.Spec;
				if (!Positive(Spec.Radius) || !Positive(Spec.Mass) || !Positive(Spec.Inertia))
				{
					return ErrorCode::InvalidParameter;
				}
				const double K = InertiaFactor(Spec);
				if (!(K > 0.0) || !(K <= 2.0 / 3.0))
				{
					return ErrorCode::InvalidParameter;
				}
				if (!TiltWithinRestLimit(Params.Tilt, K, Params.Cloth.RollingResistance, Params.PocketContacts.RailTopRollingResistance))
				{
					return ErrorCode::InvalidParameter;
				}
				BallState S = Ball.State;
				if (!FiniteState(S))
				{
					return ErrorCode::InvalidState;
				}
				if (S.State != MotionState::Stationary && S.State != MotionState::Spinning && S.State != MotionState::Sliding &&
					S.State != MotionState::Rolling && S.State != MotionState::Airborne)
				{
					return ErrorCode::InvalidState; // pocket / terminal states are simulator-owned
				}
				const Vec2 Plan = XY(S.Position);
				if (!Table.OuterBoundary.Contains(Plan))
				{
					return ErrorCode::InvalidState;
				}
				BallTableContext Context;
				double SupportZ = 0.0;
				if (!Table.PlayingArea.Contains(Plan) && Abs(S.Position.z - (CapZ + Spec.Radius)) <= Numerics.EpsZ)
				{
					for (int p = 0; p < Table.RailTops.Size(); ++p)
					{
						const RailTopPolygon& Poly = Table.RailTops[p];
						if (Poly.Kind == RailTopKind::RailCap && InsidePolygon(Poly, Plan))
						{
							Context.Support = SupportKind::RailCap;
							Context.SupportPolygon = static_cast<std::uint8_t>(p);
							SupportZ = CapZ;
							break;
						}
					}
				}
				if (S.Position.z < SupportZ + Spec.Radius - Numerics.OverlapGuard)
				{
					return ErrorCode::InvalidState; // sunk into its support
				}
				const bool OnCloth = Context.Support == SupportKind::Cloth && S.Position.z - Spec.Radius <= Numerics.EpsZ && S.Velocity.z <= Numerics.EpsV;
				if (OnCloth && !Table.PlayingArea.Contains(Plan) && !IsOverPocketOpening(Table, Plan))
				{
					return ErrorCode::InvalidState; // on the cloth height under a rail
				}
				ClassifyState(S, Spec.Radius, SupportZ, Numerics);
				States[b] = S;
				Contexts[b] = Context;
			}
			for (int i = 0; i < kMaxBalls; ++i)
			{
				if (!Input.Balls[i].InPlay)
				{
					continue;
				}
				for (int j = i + 1; j < kMaxBalls; ++j)
				{
					if (!Input.Balls[j].InPlay)
					{
						continue;
					}
					const double Gap = Length(States[j].Position - States[i].Position) - (Input.Balls[i].Spec.Radius + Input.Balls[j].Spec.Radius);
					if (Gap < -Numerics.OverlapGuard)
					{
						return ErrorCode::InvalidState;
					}
				}
			}
			std::uint32_t Struck = 0;
			for (const StrikeRequest& Request : Input.Strikes)
			{
				const int b = Request.Ball;
				if (b < 0 || b >= kMaxBalls || !Input.Balls[b].InPlay || (Struck & (1u << b)) != 0u)
				{
					return ErrorCode::InvalidArgument;
				}
				Struck |= 1u << b;
				if (States[b].State != MotionState::Stationary)
				{
					return ErrorCode::BallNotAtRest;
				}
				const ErrorCode StrikeError = ValidateCueStrike(Request.Input);
				if (StrikeError != ErrorCode::Ok)
				{
					return StrikeError;
				}
			}
			return ErrorCode::Ok;
		}

		// Initial freeze sets (architecture 8.1): rail features (noses, jaw arcs, facings) and balls within
		// ShotContext::FrozenTolerance at t = 0 (continuesInitialFreeze, rules.md 3.3).
		void InitialFreezes(sim::Workspace& Ws, const BallState* States)
		{
			const SimInput& Input = *Ws.Input;
			const TableGeometry& Table = *Input.Table;
			const double Frozen = Input.Context.FrozenTolerance;
			if (!(Frozen >= 0.0))
			{
				return;
			}
			for (int b = 0; b < kMaxBalls; ++b)
			{
				if (!Input.Balls[b].InPlay)
				{
					continue;
				}
				sim::BallSlot& Slot = Ws.Balls[b];
				const Vec3& P = States[b].Position;
				const double R = Input.Balls[b].Spec.Radius;
				for (int i = 0; i < Table.Noses.Size(); ++i)
				{
					const TableFeatureRef F{TableFeatureKind::NoseSegment, static_cast<std::uint8_t>(i), 0};
					if (sim::loop::FeatureGap(Ws, F, P, R) <= Frozen)
					{
						Slot.InitialFreezeRails |= 1u << RailFeatureOfCushion(static_cast<CushionId>(i));
					}
				}
				for (int i = 0; i < Table.JawArcs.Size(); ++i)
				{
					const TableFeatureRef Arc{TableFeatureKind::JawArc, static_cast<std::uint8_t>(i), 0};
					const TableFeatureRef Face{TableFeatureKind::FacingFace, static_cast<std::uint8_t>(i), 0};
					if (sim::loop::FeatureGap(Ws, Arc, P, R) <= Frozen || (i < Table.Facings.Size() && sim::loop::FeatureGap(Ws, Face, P, R) <= Frozen))
					{
						Slot.InitialFreezeRails |= 1u << (kCushionCount + i);
					}
				}
				for (int j = b + 1; j < kMaxBalls; ++j)
				{
					if (!Input.Balls[j].InPlay)
					{
						continue;
					}
					const double Gap = Length(States[j].Position - P) - (R + Input.Balls[j].Spec.Radius);
					if (Gap <= Frozen)
					{
						Slot.InitialFreezeBalls |= 1u << j;
						Ws.Balls[j].InitialFreezeBalls |= 1u << b;
					}
				}
			}
		}

		// Final per-ball entries, StopTime, and the orientation of balls resting on the table.
		void FillFinals(sim::Workspace& Ws)
		{
			const SimInput& Input = *Ws.Input;
			ShotResult& Result = *Ws.Result;
			double StopTime = 0.0;
			for (int b = 0; b < kMaxBalls; ++b)
			{
				if (!Input.Balls[b].InPlay)
				{
					continue;
				}
				sim::BallSlot& Slot = Ws.Balls[b];
				BallFinal& Final = Result.Finals[b];
				if (!IsTerminal(Slot.Seg.State))
				{
					// Resting (or, only with unconsumed hooks, parked at the end of its last segment).
					const bool HasEnd = Slot.Seg.TauEnd < kInfinity;
					Final.Status = BallFinalStatus::OnTable;
					Final.State = HasEnd ? SegmentEndState(Slot.Seg, Ws.Params.Numerics) : EvaluateSegment(Slot.Seg, 0.0);
					Final.Time = HasEnd ? Slot.Seg.T0 + Slot.Seg.TauEnd : Slot.Seg.T0;
					Final.Pocket = Slot.Context.Pocket;
					Final.Orientation = sim::loop::TracksOrientation(Ws, b) ? Slot.Orientation0 : Input.Balls[b].Orientation;
				}
				StopTime = Max(StopTime, Final.Time);
			}
			Result.StopTime = StopTime;
		}
	}

	PhysicsParams MakePhysicsParams(const TableSpec& Spec)
	{
		PhysicsParams Params;
		Params.Origin = ParamsOrigin::Table;
		Params.Cloth = ClothParamsFor(Spec.Cloth);
		Params.Cushion.FacingRestitutionScale = Spec.FacingRestitutionScale;
		Params.PocketContacts.LinerRestitution = Spec.LinerRestitution;
		Params.PocketContacts.LinerFriction = Spec.LinerFriction;
		return Params;
	}

	PhysicsParams MakePhysicsParams(const TableSpec& Spec, const TableCondition& Condition)
	{
		PhysicsParams Params = MakePhysicsParams(Spec);
		Params.Tilt.Slope = Condition.Slope;
		Params.BallBall.ClingFactor = Condition.BallCling;
		Params.ChalkCling = Condition.ChalkCling;
		return Params;
	}

	ErrorCode ValidatePhysicsParams(const PhysicsParams& P)
	{
		// prior-art 5.11 / R11 / ROB-06: non-physical parameters are rejected (no clamping, no hang). Support-surface frictions
		// (cloth, rail cap) must be positive: a frictionless support never ends a slide or a roll (infinite loop R11); contact
		// frictions (ball-ball constants, cushion, facing, liner, rim) may be 0 (frictionless test models).
		constexpr ErrorCode Bad = ErrorCode::InvalidParameter;
		if (P.Origin != ParamsOrigin::Table && P.Origin != ParamsOrigin::Explicit)
		{
			return Bad; // Unset (single parameter source, architecture 11 item 6) or out of range
		}
		if (!Positive(P.Gravity))
		{
			return Bad;
		}
		if (!Positive(P.Cloth.SlidingFriction) || !Positive(P.Cloth.RollingResistance) || !Positive(P.Cloth.SpinDeceleration))
		{
			return Bad;
		}
		if (!UnitInterval(P.Slate.Restitution) || !NonNegative(P.Slate.MinBounceHeight) || P.Slate.MaxBounces < 1)
		{
			return Bad;
		}
		const PinchParams& Pinch = P.Pinch;
		if (!IsFinite(Pinch.Theta0Cue) || !IsFinite(Pinch.Theta1Cue) || !IsFinite(Pinch.Theta0Jump) || !IsFinite(Pinch.Theta1Jump) ||
			!UnitInterval(Pinch.PinchRestitution) || !NonNegative(Pinch.JumpCueMaxMass))
		{
			return Bad;
		}
		const BallBallParams& BallBall = P.BallBall;
		if (BallBall.Model != BallBallModel::FrictionalImpulse && BallBall.Model != BallBallModel::Mathavan2014)
		{
			return Bad;
		}
		if (BallBall.Friction != BallBallFrictionModel::Alciatore && BallBall.Friction != BallBallFrictionModel::Constant &&
			BallBall.Friction != BallBallFrictionModel::None)
		{
			return Bad;
		}
		if (!UnitInterval(BallBall.Restitution) || !NonNegative(BallBall.MuA) || !NonNegative(BallBall.MuB) || !NonNegative(BallBall.MuC) ||
			!NonNegative(BallBall.MuConstant) || !Positive(BallBall.ClingFactor) || !Positive(BallBall.ChalkClingFactor))
		{
			return Bad;
		}
		const CushionParams& Cushion = P.Cushion;
		if (Cushion.OnClothModel != CushionModel::Mathavan2010 && Cushion.OnClothModel != CushionModel::Han2005 && Cushion.OnClothModel != CushionModel::Mirror &&
			Cushion.OnClothModel != CushionModel::StrongeCompliant)
		{
			return Bad;
		}
		const CushionRestitutionLaw& Law = Cushion.Restitution;
		if (!UnitInterval(Law.Max) || !UnitInterval(Law.Min) || Law.Min > Law.Max || !NonNegative(Law.Slope) || !IsFinite(Law.Knee))
		{
			return Bad;
		}
		if (!NonNegative(Cushion.Friction) || Cushion.MathavanSteps < 1 || Cushion.MathavanMaxBisections < 0 || !Positive(Cushion.StrongeOmegaRatio) ||
			!NonNegative(Cushion.NoseProfileRadius) || !NonNegative(Cushion.FacingRestitutionScale) || Cushion.FacingRestitutionScale * Law.Max > 1.0 ||
			!NonNegative(Cushion.FacingFriction))
		{
			return Bad;
		}
		const PocketContactParams& Pocket = P.PocketContacts;
		if (!UnitInterval(Pocket.LinerRestitution) || !NonNegative(Pocket.LinerFriction) || !UnitInterval(Pocket.RimRestitution) ||
			!NonNegative(Pocket.RimFriction) || !UnitInterval(Pocket.RailTopRestitution) || !Positive(Pocket.RailTopFriction) ||
			!Positive(Pocket.RailTopRollingResistance) || !Positive(Pocket.RailTopSpinDeceleration))
		{
			return Bad;
		}
		if (P.Pockets != PocketModel::GeometricLevelA && P.Pockets != PocketModel::CaptureCircle)
		{
			return Bad;
		}
		const CliParams& Cli = P.Cli;
		if (!Positive(Cli.HertzStiffness) || (!(Cli.TsujiAlpha < 0.0) && !IsFinite(Cli.TsujiAlpha)) || !Positive(Cli.TimeStep) ||
			!Positive(Cli.CushionStiffness) || !Positive(Cli.SlipRegularization) || Cli.ExitZeroForceSteps < 1 || !NonNegative(Cli.JoinFactor) ||
			!Positive(Cli.RigidTimeStep) || Cli.RigidIterations < 1 || !NonNegative(Cli.SustainedSpeed) || Cli.SustainedSteps < 1 || !Positive(Cli.GridCell))
		{
			return Bad;
		}
		const NumericsConfig& N = P.Numerics;
		if (!Positive(N.EpsZ) || !Positive(N.EpsV) || !Positive(N.EpsWTimesRadius) || !NonNegative(N.SnapResidualRel) || !Positive(N.ContactTol) ||
			!NonNegative(N.ApproachSpeedTol) || !NonNegative(N.TangencyTolPerLength) || !Positive(N.OverlapGuard) || !NonNegative(N.SegmentParamSlack) ||
			!NonNegative(N.RootTrimRel) || !Positive(N.RootTimeTol) || N.RootMaxIterations < 1)
		{
			return Bad;
		}
		if (!NonNegative(N.RestSpeed) || N.ZenoContactCount < 1 || !NonNegative(N.ZenoWindow) || N.MaxEvents < 1 || !Positive(N.TimeHorizon) ||
			!Positive(N.CompliantMaxDuration) || N.MaxIslandSteps < 1 || !NonNegative(N.CushionSlipEps) || !Positive(N.PivotMinSpeed) ||
			N.PivotSimpsonPanels < 2 || !NonNegative(N.LineCrossEps) || !NonNegative(N.LeaveDistance) || !Positive(N.SampleTolerance) ||
			!Positive(N.SampleMaxInterval))
		{
			return Bad;
		}
		// Tilt and nap (human-factors 4.5.1, 4.5.6, 4.5.7; Simulator.h): finite; Tolerance and RefreshMaxInterval > 0 (WP-1 request:
		// TiltPieceDuration would otherwise run whole phases as single pieces); eta_n in [0, 1); resting balls stay put (k = 2/5 form
		// here, Run repeats it per ball with its own k).
		const TiltParams& Tilt = P.Tilt;
		if (!IsFinite(Tilt.Slope.x) || !IsFinite(Tilt.Slope.y) || !IsFinite(Tilt.NapPseudoSlope.x) || !IsFinite(Tilt.NapPseudoSlope.y) ||
			!Positive(Tilt.Tolerance) || !Positive(Tilt.RefreshMaxInterval) || !(Tilt.NapResistance >= 0.0) || !(Tilt.NapResistance < 1.0))
		{
			return Bad;
		}
		if (!TiltWithinRestLimit(Tilt, kSolidSphereInertiaFactor, P.Cloth.RollingResistance, Pocket.RailTopRollingResistance))
		{
			return Bad;
		}
		return ErrorCode::Ok;
	}

	Simulator::Simulator(const ResultCapacity& InCapacity)
		: Ws(new Workspace())
		, Caps(InCapacity)
	{
	}

	Simulator::~Simulator()
	{
		delete Ws;
	}

	Simulator::Simulator(Simulator&& Other) noexcept
		: Ws(Other.Ws)
		, Caps(Other.Caps)
	{
		Other.Ws = nullptr;
	}

	Simulator& Simulator::operator=(Simulator&& Other) noexcept
	{
		if (this != &Other)
		{
			delete Ws;
			Ws = Other.Ws;
			Caps = Other.Caps;
			Other.Ws = nullptr;
		}
		return *this;
	}

	SimStatus Simulator::Run(const SimInput& Input, ShotResult& Result)
	{
		if (Ws == nullptr)
		{
			Ws = new Workspace(); // a moved-from simulator (allocation before the loop only)
		}
		ReserveShotResult(Result, Caps);
		ResetShotResult(Result);

		// --- validation (architecture 8.4) ---------------------------------------------------------------------------
		BallState States[kMaxBalls];
		BallTableContext Contexts[kMaxBalls];
		const ErrorCode InputError = ValidateInput(Input, States, Contexts);
		if (InputError != ErrorCode::Ok)
		{
			Result.Diagnostics.InputError = InputError;
			Result.Status = SimStatus::InvalidInput;
			return Result.Status;
		}

		// --- workspace ------------------------------------------------------------------------------------------------
		sim::Workspace& W = Ws->Data;
		W.Input = &Input;
		W.Result = &Result;
		W.Params = Input.Params;
		if (W.Params.Cli.TsujiAlpha < 0.0)
		{
			W.Params.Cli.TsujiAlpha = TsujiAlphaForRestitution(W.Params.BallBall.Restitution); // review item 27
		}
		W.Detection.NoseProfileRadius = W.Params.Cushion.NoseProfileRadius;
		W.Detection.PooltoolCompat = W.Params.Cushion.PooltoolCompat;
		W.Detection.Pockets = W.Params.Pockets;
		W.Queue.Clear();
		W.Zeno.Clear();
		W.Island.Active = false;
		W.Island.StartTime = 0.0;
		W.Island.RigidSince = -1.0;
		W.Island.StepRecords.Clear();
		for (int b = 0; b < kMaxBalls; ++b)
		{
			sim::BallSlot& Slot = W.Balls[b];
			if (Input.Balls[b].InPlay)
			{
				Slot = sim::BallSlot{};
			}
			else
			{
				Slot.InPlay = false; // never read beyond these fields (IsLive, observer scans)
				Slot.InIsland = false;
				Slot.Observers.Clear();
			}
		}
		for (sim::TipSlot& Tip : W.Tips)
		{
			Tip = sim::TipSlot{};
		}
		W.Now = 0.0;
		W.EventsProcessed = 0;
		W.IslandStepsUsed = 0;
		W.RecordSequence = 0;
		W.Caps = Caps;
		W.Cache = sim::LoopCache{};

		SimDiagnostics& Diag = Result.Diagnostics;
		std::uint32_t Live = 0;
		for (int b = 0; b < kMaxBalls; ++b)
		{
			if (!Input.Balls[b].InPlay)
			{
				continue;
			}
			Live |= 1u << b;
			sim::BallSlot& Slot = W.Balls[b];
			Slot.InPlay = true;
			Slot.Context = Contexts[b];
			Slot.Orientation0 = Input.Balls[b].Orientation;
			BallFinal& Final = Result.Finals[b];
			Final.Status = BallFinalStatus::OnTable;
			Final.State = States[b];
			Final.Orientation = Input.Balls[b].Orientation;
		}
		Result.BallsInPlay = Live;
		if (Input.Record.ShotRecord)
		{
			BeginShotRecord(Input, Result.Record);
		}
		InitialFreezes(W, States);

		// --- strikes at t = 0 (8.6), initial segments, predictions --------------------------------------------------------
		sim::loop::ProcessStrikes(W, States);
		for (int b = 0; b < kMaxBalls; ++b)
		{
			if ((Live & (1u << b)) != 0u)
			{
				sim::loop::SetSegment(W, b, States[b], 0.0, true);
			}
		}
		sim::loop::PredictBalls(W, Live);

		// --- main loop (8.4) ---------------------------------------------------------------------------------------------
		SimStatus Status = SimStatus::Ok;
		const double Horizon = W.Params.Numerics.TimeHorizon;
		const int MaxEvents = W.Params.Numerics.MaxEvents;
		int IslandStalls = 0;
		constexpr int kGroupCapacity = 64;
		for (;;)
		{
			while (!W.Queue.IsEmpty() && !sim::loop::IsValid(W, W.Queue.Top()))
			{
				W.Queue.Pop();
				++Diag.StaleEventsSkipped;
			}
			const double TEvent = W.Queue.IsEmpty() ? kInfinity : W.Queue.Top().Time;
			const double TPending = sim::loop::EarliestPending(W);
			const double TNext = Min(TEvent, TPending);

			if (W.Island.Active)
			{
				const double TStep = sim::NextIslandStepTime(W);
				if (TNext > TStep)
				{
					if (TStep > Horizon)
					{
						sim::loop::StopAllBalls(W, Horizon);
						Status = SimStatus::HorizonReached;
						break;
					}
					if (!sim::AdvanceIsland(W, TNext))
					{
						Diag.IslandBudgetExceeded = true;
						sim::loop::StopAllBalls(W, W.Now);
						Status = SimStatus::Aborted;
						break;
					}
					// A hook that neither steps nor ends the island would spin here forever: treat as a spent budget.
					IslandStalls = (W.Island.Active && sim::NextIslandStepTime(W) == TStep) ? IslandStalls + 1 : 0;
					if (IslandStalls > 64)
					{
						Diag.IslandBudgetExceeded = true;
						sim::loop::StopAllBalls(W, W.Now);
						Status = SimStatus::Aborted;
						break;
					}
					continue;
				}
			}
			if (!(TNext < kInfinity))
			{
				break; // every ball stationary or terminal, no island
			}
			if (TPending < TEvent)
			{
				// Observers and tip-contact ends are emitted before the next event (8.7).
				if (TPending > Horizon)
				{
					sim::loop::StopAllBalls(W, Horizon);
					Status = SimStatus::HorizonReached;
					break;
				}
				sim::loop::EmitPending(W, TPending, true);
				W.Now = Max(W.Now, TPending); // the log has reached TPending: a later stop never precedes a logged item
				continue;
			}

			const QueuedEvent E = W.Queue.Top();
			if (E.Time > Horizon)
			{
				sim::loop::StopAllBalls(W, Horizon);
				Status = SimStatus::HorizonReached;
				break;
			}
			if (++W.EventsProcessed > MaxEvents)
			{
				W.EventsProcessed = MaxEvents;
				sim::loop::StopAllBalls(W, W.Now); // collisions 7.3 guard 4: stop where they are
				Status = SimStatus::Aborted;
				break;
			}
			W.Queue.Pop();
			W.Now = E.Time;

			if (!sim::loop::IsImpulseContact(E))
			{
				sim::loop::ProcessEvent(W, E);
			}
			else
			{
				// Exact simultaneity (8.3): every other valid impulse contact at exactly E.Time that shares a ball (or a tip) with E,
				// directly or transitively, joins E in one island. Everything else at that time is pushed back unchanged.
				QueuedEvent Others[kGroupCapacity];
				int NumOthers = 0;
				while (NumOthers < kGroupCapacity && !W.Queue.IsEmpty() && W.Queue.Top().Time == E.Time)
				{
					const QueuedEvent X = W.Queue.Top();
					W.Queue.Pop();
					if (sim::loop::IsValid(W, X))
					{
						Others[NumOthers++] = X;
					}
					else
					{
						++Diag.StaleEventsSkipped;
					}
				}
				const auto Nodes = [](const QueuedEvent& X)
				{
					std::uint32_t Mask = 1u << X.BallA;
					if (X.Kind == QueuedEventKind::BallBall && X.BallB != kNoBallSlot)
					{
						Mask |= 1u << X.BallB;
					}
					if (X.Kind == QueuedEventKind::TipContact)
					{
						Mask |= 1u << (kMaxBalls + X.FeatureIndex);
					}
					return Mask;
				};
				bool Member[kGroupCapacity] = {};
				std::uint32_t GroupNodes = Nodes(E);
				int Members = 1;
				for (bool Grew = true; Grew;)
				{
					Grew = false;
					for (int k = 0; k < NumOthers; ++k)
					{
						if (!Member[k] && sim::loop::IsImpulseContact(Others[k]) && (Nodes(Others[k]) & GroupNodes) != 0u)
						{
							Member[k] = true;
							GroupNodes |= Nodes(Others[k]);
							++Members;
							Grew = true;
						}
					}
				}
				for (int k = 0; k < NumOthers; ++k)
				{
					if (!Member[k])
					{
						sim::loop::Push(W, Others[k]);
					}
				}
				if (Members == 1)
				{
					sim::loop::ProcessEvent(W, E);
				}
				else
				{
					sim::loop::HandOffToIsland(W, sim::loop::SeedOf(E), E.Time);
					for (int k = 0; k < NumOthers; ++k)
					{
						if (Member[k])
						{
							++W.EventsProcessed;
							sim::loop::HandOffToIsland(W, sim::loop::SeedOf(Others[k]), E.Time);
						}
					}
				}
			}
			// Observers at exactly E.Time: after E (8.7); replaced segments recomputed theirs on [E.Time, ...).
			sim::loop::EmitPending(W, E.Time, true);
		}

		// --- end of the shot (8.10) ------------------------------------------------------------------------------------------
		const double FlushUntil = Status == SimStatus::Ok ? kInfinity : W.Now;
		sim::loop::EmitPending(W, FlushUntil, true);
		for (sim::BallSlot& Slot : W.Balls)
		{
			Slot.Observers.Clear(); // beyond an abort / horizon stop: never emitted
		}
		FillFinals(W);
		Diag.EventsProcessed = W.EventsProcessed;
		Diag.IslandSteps = W.IslandStepsUsed > Diag.IslandSteps ? W.IslandStepsUsed : Diag.IslandSteps;
		Result.Status = Status;
		if (Input.Record.ShotRecord)
		{
			if (Result.Record.Truncated)
			{
				Diag.RecordOverflow = true;
			}
			FinishShotRecord(Input, Result, Result.Record);
			if (Status != SimStatus::Ok)
			{
				Result.Record.Truncated = true; // the rules replay an aborted shot (collisions 7.3)
			}
		}
		W.Input = nullptr;
		W.Result = nullptr;
		return Result.Status;
	}
}
