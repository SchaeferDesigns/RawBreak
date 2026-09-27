#pragma once

// Shared helpers of the end-to-end tests (Tests/Core/EndToEnd, integration round 2): readable event dumps for failure
// diagnosis, whole-shot invariants (rest, overlaps, cushion penetration, energy between events, bitwise determinism), physical
// measurements on real simulator output, and the benchmark generators of prior-art 7.5 (B1 shots, B2 breaks). Every check
// reads only public API output (ShotResult, ShotRecord).

#include "rb/Core/Constants.h"
#include "rb/Core/Random.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/Playback.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/TableRules.h"

#include "Simulator/SimTestUtil.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace e2e
{
	inline const char* EventName(rb::ShotEventType T)
	{
		switch (T)
		{
		case rb::ShotEventType::CueStrike: return "CueStrike";
		case rb::ShotEventType::TipRecontact: return "TipRecontact";
		case rb::ShotEventType::TipContactBegin: return "TipContactBegin";
		case rb::ShotEventType::TipContactEnd: return "TipContactEnd";
		case rb::ShotEventType::BallBall: return "BallBall";
		case rb::ShotEventType::BallCushion: return "BallCushion";
		case rb::ShotEventType::BallJaw: return "BallJaw";
		case rb::ShotEventType::BallRailTop: return "BallRailTop";
		case rb::ShotEventType::BallSlate: return "BallSlate";
		case rb::ShotEventType::BallAirborne: return "BallAirborne";
		case rb::ShotEventType::BallLand: return "BallLand";
		case rb::ShotEventType::BallPocketEnter: return "BallPocketEnter";
		case rb::ShotEventType::BallPocketRim: return "BallPocketRim";
		case rb::ShotEventType::BallLiner: return "BallLiner";
		case rb::ShotEventType::BallPocketExit: return "BallPocketExit";
		case rb::ShotEventType::BallPocketed: return "BallPocketed";
		case rb::ShotEventType::BallOffTable: return "BallOffTable";
		case rb::ShotEventType::BallExternalContact: return "BallExternalContact";
		case rb::ShotEventType::MotionTransition: return "MotionTransition";
		case rb::ShotEventType::BallLineCross: return "BallLineCross";
		case rb::ShotEventType::BallJumpedOver: return "BallJumpedOver";
		case rb::ShotEventType::IslandBegin: return "IslandBegin";
		case rb::ShotEventType::IslandRigid: return "IslandRigid";
		case rb::ShotEventType::IslandEnd: return "IslandEnd";
		case rb::ShotEventType::ZenoGuard: return "ZenoGuard";
		case rb::ShotEventType::Diagnostic: return "Diagnostic";
		case rb::ShotEventType::TiltRefresh: return "TiltRefresh";
		}
		return "?";
	}

	inline const char* StateName(rb::MotionState S)
	{
		switch (S)
		{
		case rb::MotionState::Stationary: return "Stationary";
		case rb::MotionState::Spinning: return "Spinning";
		case rb::MotionState::Sliding: return "Sliding";
		case rb::MotionState::Rolling: return "Rolling";
		case rb::MotionState::Airborne: return "Airborne";
		case rb::MotionState::PocketPivot: return "PocketPivot";
		case rb::MotionState::PocketFall: return "PocketFall";
		case rb::MotionState::Pocketed: return "Pocketed";
		case rb::MotionState::OffTable: return "OffTable";
		}
		return "?";
	}

	inline const char* StatusName(rb::SimStatus S)
	{
		switch (S)
		{
		case rb::SimStatus::Ok: return "Ok";
		case rb::SimStatus::InvalidInput: return "InvalidInput";
		case rb::SimStatus::Aborted: return "Aborted";
		case rb::SimStatus::HorizonReached: return "HorizonReached";
		case rb::SimStatus::NotImplemented: return "NotImplemented";
		}
		return "?";
	}

	// Prints the event log (at most MaxLines lines) and the finals: the diagnosis aid of a failing end-to-end test.
	inline void Dump(const rb::ShotResult& R, int MaxLines = 400)
	{
		std::printf("  status %s, stop %.6f s, events %d processed, %zu logged, islands %d (hand-offs %d, steps %d), missed %d, overlaps %d\n",
			StatusName(R.Status), R.StopTime, R.Diagnostics.EventsProcessed, R.Events.size(), R.Diagnostics.Islands, R.Diagnostics.IslandHandOffs,
			R.Diagnostics.IslandSteps, R.Diagnostics.MissedEvents, R.Diagnostics.OverlapWarnings);
		int Line = 0;
		for (const rb::ShotEvent& E : R.Events)
		{
			if (++Line > MaxLines)
			{
				std::printf("  ... (%zu more)\n", R.Events.size() - static_cast<std::size_t>(MaxLines));
				break;
			}
			std::printf("  %10.6f %-16s A=%2d B=%3d f=%3d s=%3d fl=%02x vn=%9.5f J=%9.6f val=%10.6f pos (%.4f %.4f %.4f) v (%.3f %.3f %.3f)\n", E.Time,
				EventName(E.Type), static_cast<int>(E.A), static_cast<int>(E.B), static_cast<int>(E.Feature), static_cast<int>(E.SubFeature),
				static_cast<unsigned>(E.Flags), E.NormalSpeed, E.NormalImpulse, E.Value, E.Pre[0].Position.x, E.Pre[0].Position.y, E.Pre[0].Position.z,
				E.Pre[0].Velocity.x, E.Pre[0].Velocity.y, E.Pre[0].Velocity.z);
		}
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			const rb::BallFinal& F = R.Finals[b];
			if (F.Status != rb::BallFinalStatus::NotInPlay)
			{
				std::printf("  final %2d: status %d %-10s at (%.5f %.5f %.5f) t=%.5f pocket %d\n", b, static_cast<int>(F.Status), StateName(F.State.State),
					F.State.Position.x, F.State.Position.y, F.State.Position.z, F.Time, static_cast<int>(F.Pocket));
			}
		}
	}

	inline int CountEvents(const rb::ShotResult& R, rb::ShotEventType Type, int A = -2, int B = -2)
	{
		int N = 0;
		for (const rb::ShotEvent& E : R.Events)
		{
			N += E.Type == Type && (A == -2 || E.A == A) && (B == -2 || E.B == B) ? 1 : 0;
		}
		return N;
	}

	// First event of Type (on ball A / with ball B, -2 = any) at or after From.
	inline const rb::ShotEvent* FirstEvent(const rb::ShotResult& R, rb::ShotEventType Type, int A = -2, int B = -2, double From = -1.0)
	{
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Type == Type && (A == -2 || E.A == A) && (B == -2 || E.B == B) && E.Time >= From)
			{
				return &E;
			}
		}
		return nullptr;
	}

	// Every in-play ball ended at rest on the table or terminal (pocketed / off the table).
	inline bool AllAtRest(const rb::ShotResult& R)
	{
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			const rb::BallFinal& F = R.Finals[b];
			if (F.Status == rb::BallFinalStatus::OnTable && (F.State.State != rb::MotionState::Stationary || rb::LengthSquared(F.State.Velocity) > 0.0))
			{
				return false;
			}
		}
		return true;
	}

	// Largest pairwise overlap [m] of the balls on the table in the final state (<= 0 if none overlap).
	inline double FinalOverlap(const rb::ShotResult& R, const rb::SimInput& In)
	{
		double Worst = -rb::kInfinity;
		for (int i = 0; i < rb::kMaxBalls; ++i)
		{
			for (int j = i + 1; j < rb::kMaxBalls && R.Finals[i].Status == rb::BallFinalStatus::OnTable; ++j)
			{
				if (R.Finals[j].Status == rb::BallFinalStatus::OnTable)
				{
					const double D = rb::Length(R.Finals[i].State.Position - R.Finals[j].State.Position);
					Worst = rb::Max(Worst, In.Balls[i].Spec.Radius + In.Balls[j].Spec.Radius - D);
				}
			}
		}
		return Worst;
	}

	// Largest penetration [m] of a ball resting on the cloth into a cushion (nose line at the contact distance R_c within the
	// segment, jaw arcs at r_j + R_c on their exposed arc); <= 0 when no ball is inside a cushion.
	inline double FinalCushionPenetration(const rb::ShotResult& R, const rb::SimInput& In)
	{
		const rb::TableGeometry& T = *In.Table;
		double Worst = -rb::kInfinity;
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			const rb::BallFinal& F = R.Finals[b];
			const double Radius = In.Balls[b].Spec.Radius;
			if (F.Status != rb::BallFinalStatus::OnTable || F.State.Position.z > Radius + 1e-6)
			{
				continue;
			}
			for (int c = 0; c < T.Noses.Size(); ++c)
			{
				const rb::NoseSegment& Nose = T.Noses[c];
				const double Along = rb::Dot(rb::XY(F.State.Position) - Nose.Start, Nose.Direction);
				if (Nose.Present && Along >= 0.0 && Along <= Nose.Length)
				{
					Worst = rb::Max(Worst, -simtest::NoseGap(T, c, F.State.Position, Radius));
				}
			}
			for (int a = 0; a < T.JawArcs.Size(); ++a)
			{
				const rb::JawArc& Arc = T.JawArcs[a];
				const rb::Vec2 Rel = rb::XY(F.State.Position) - Arc.Center;
				double Delta = std::atan2(Rel.y, Rel.x) - Arc.AngleFrom;
				while (Delta < 0.0)
				{
					Delta += 2.0 * rb::kPi;
				}
				const double Rc = rb::ComputeCushionContact(Radius, Arc.Height, 0.0, false).HorizontalOffset;
				if (Delta <= Arc.AngleSweep)
				{
					Worst = rb::Max(Worst, Arc.Radius + Rc - rb::Length(Rel));
				}
			}
		}
		return Worst;
	}

	// Mechanical energy [J]: kinetic + rotational + m g (z - R) with its sign (a ball below the cloth in a pocket has lost
	// potential energy; the rail cap is higher). The table's own slope is not included (level tables only).
	inline double Energy(const rb::BallState& S, const rb::BallSpec& Spec, double Gravity)
	{
		return 0.5 * Spec.Mass * rb::LengthSquared(S.Velocity) + 0.5 * Spec.Inertia * rb::LengthSquared(S.Omega) + Spec.Mass * Gravity * (S.Position.z - Spec.Radius);
	}

	// Total energy at time T from the recorded tracks: Analytic segments exactly; a Sampled piece (island, pivot) carries no exact
	// velocity, so Valid = false then; a Terminal ball keeps only the potential energy where it stopped (its kinetic energy was
	// dissipated at the capture; dropping the negative potential of a ball below the cloth would look like a gain).
	inline double TotalEnergyAt(const rb::ShotResult& R, const rb::SimInput& In, double T, bool& Valid)
	{
		Valid = true;
		double Sum = 0.0;
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			if (!In.Balls[b].InPlay)
			{
				continue;
			}
			const auto& Segments = R.Tracks[b].Segments;
			for (std::size_t k = 0; k < Segments.size(); ++k)
			{
				const rb::TrajectorySegment& S = Segments[k];
				const bool Last = k + 1 == Segments.size();
				if (T < S.Motion.T0 || (!Last && T >= S.T1))
				{
					continue;
				}
				if (S.Kind == rb::SegmentKind::Sampled)
				{
					Valid = false;
				}
				else if (S.Kind == rb::SegmentKind::Terminal)
				{
					Sum += In.Balls[b].Spec.Mass * In.Params.Gravity * (S.Motion.Pos0.z - In.Balls[b].Spec.Radius);
				}
				else if (S.Kind == rb::SegmentKind::Analytic)
				{
					Sum += Energy(rb::EvaluateSegment(S.Motion, T - S.Motion.T0), In.Balls[b].Spec, In.Params.Gravity);
				}
				break;
			}
		}
		return Sum;
	}

	// Energy never increases between events: the total is sampled just after every logged event and compared with the previous
	// sample; comparisons across an island or a pivot (a Sampled piece at either instant) are skipped. Returns the largest increase
	// [J] (negative when every step decreased) and the number of comparisons.
	inline double WorstEnergyIncrease(const rb::ShotResult& R, const rb::SimInput& In, int& Compared)
	{
		Compared = 0;
		double Worst = -rb::kInfinity;
		double Previous = 0.0;
		bool Have = false;
		double Last = -1.0;
		for (const rb::ShotEvent& E : R.Events)
		{
			const double T = E.Time + 1e-9;
			if (!(T > Last) || T > R.StopTime)
			{
				continue;
			}
			Last = T;
			bool Valid = false;
			const double Total = TotalEnergyAt(R, In, T, Valid);
			if (!Valid)
			{
				Have = false;
				continue;
			}
			if (Have)
			{
				Worst = rb::Max(Worst, Total - Previous);
				++Compared;
			}
			Previous = Total;
			Have = true;
		}
		return Worst;
	}

	inline bool SameRecordEvent(const rb::RecordEvent& A, const rb::RecordEvent& B)
	{
		using simtest::Bits;
		return Bits(A.Time) == Bits(B.Time) && A.Sequence == B.Sequence && A.Type == B.Type && A.A == B.A && A.B == B.B && A.Feature == B.Feature &&
			A.Side == B.Side && A.ContinuesInitialFreeze == B.ContinuesInitialFreeze && A.OtherContactBefore == B.OtherContactBefore && A.From == B.From &&
			A.To == B.To && Bits(A.PositionA.x) == Bits(B.PositionA.x) && Bits(A.PositionA.y) == Bits(B.PositionA.y) &&
			Bits(A.PositionB.x) == Bits(B.PositionB.x) && Bits(A.PositionB.y) == Bits(B.PositionB.y) && Bits(A.Normal.x) == Bits(B.Normal.x) &&
			Bits(A.Normal.y) == Bits(B.Normal.y) && Bits(A.Normal.z) == Bits(B.Normal.z) && Bits(A.CutAngle) == Bits(B.CutAngle) &&
			Bits(A.ZMax) == Bits(B.ZMax) && Bits(A.Value) == Bits(B.Value);
	}

	// Bitwise equality of two results: status, stop time, the event log (times, ids, impulses, states), finals, diagnostics
	// counters (ResultHash), every track segment's start and the rules record.
	inline bool BitwiseEqual(const rb::ShotResult& A, const rb::ShotResult& B)
	{
		using simtest::Bits;
		if (simtest::ResultHash(A) != simtest::ResultHash(B) || A.Events.size() != B.Events.size() || A.Record.Events.size() != B.Record.Events.size())
		{
			return false;
		}
		for (std::size_t k = 0; k < A.Record.Events.size(); ++k)
		{
			if (!SameRecordEvent(A.Record.Events[k], B.Record.Events[k]))
			{
				return false;
			}
		}
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			const auto& SA = A.Tracks[b].Segments;
			const auto& SB = B.Tracks[b].Segments;
			if (SA.size() != SB.size())
			{
				return false;
			}
			for (std::size_t k = 0; k < SA.size(); ++k)
			{
				if (Bits(SA[k].Motion.T0) != Bits(SB[k].Motion.T0) || Bits(SA[k].T1) != Bits(SB[k].T1) || Bits(SA[k].Motion.Pos0.x) != Bits(SB[k].Motion.Pos0.x) ||
					Bits(SA[k].Motion.Pos0.y) != Bits(SB[k].Motion.Pos0.y) || Bits(SA[k].Orientation0.w) != Bits(SB[k].Orientation0.w) ||
					Bits(SA[k].Orientation0.x) != Bits(SB[k].Orientation0.x))
				{
					return false;
				}
			}
		}
		return true;
	}

	// The whole-shot invariants every end-to-end shot must satisfy; prints what failed (the caller RB_CHECKs the result).
	struct ShotInvariants
	{
		bool Ok = true;
		bool AtRest = true;
		double Overlap = 0.0;           // largest final ball-ball overlap [m]
		double CushionPenetration = 0.0;// largest final ball-in-cushion depth [m]
		double EnergyRise = 0.0;        // largest energy increase between events [J]
		int EnergyComparisons = 0;
		bool Clean = true;              // no overlap / missed-event / budget diagnostics
	};

	inline ShotInvariants CheckInvariants(const rb::ShotResult& R, const rb::SimInput& In, bool LevelTable = true)
	{
		ShotInvariants I;
		I.Ok = R.Status == rb::SimStatus::Ok;
		I.AtRest = AllAtRest(R);
		I.Overlap = FinalOverlap(R, In);
		I.CushionPenetration = FinalCushionPenetration(R, In);
		I.EnergyRise = LevelTable ? WorstEnergyIncrease(R, In, I.EnergyComparisons) : -1.0;
		I.Clean = R.Diagnostics.OverlapWarnings == 0 && R.Diagnostics.MissedEvents == 0 && !R.Diagnostics.IslandBudgetExceeded && !R.Record.Truncated;
		return I;
	}

	// prior-art 7.5 B1 (the generator of the WP-6a robustness tests): a mid-game 9-ball layout (2-9 object balls, seeded),
	// V in [0.5, 6] m/s, tip offsets within +-0.5 R, cue elevation 0-15 deg, aimed at a random object ball with a random cut.
	inline void MakeB1Shot(std::uint64_t Seed, const rb::TableGeometry& T, rb::SimInput& In)
	{
		using namespace rb;
		In = SimInput{};
		In.Table = &T;
		In.Params = simtest::ValParams();
		Rng Random(Seed);
		const int Balls = 3 + static_cast<int>(Random.NextBelow(8));
		const double R = simtest::kR;
		const double Margin = R + 0.03;
		int Placed = 0;
		for (int Attempt = 0; Attempt < 2000 && Placed < Balls; ++Attempt)
		{
			const Vec3 P{Random.NextUniform(-T.HalfLength + Margin, T.HalfLength - Margin), Random.NextUniform(-T.HalfWidth + Margin, T.HalfWidth - Margin), R};
			bool Free = true;
			for (int j = 0; j < Placed && Free; ++j)
			{
				Free = Length(P - In.Balls[j].State.Position) > 2.0 * R + 0.005;
			}
			for (int p = 0; p < T.Pockets.Size() && Free; ++p)
			{
				Free = Length(XY(P) - T.Pockets[p].MouthMid) > 0.12;
			}
			if (Free)
			{
				simtest::Place(In, Placed, P);
				++Placed;
			}
		}
		const int Target = 1 + static_cast<int>(Random.NextBelow(static_cast<std::uint32_t>(Placed - 1)));
		const Vec3 Aim = In.Balls[Target].State.Position - In.Balls[0].State.Position;
		const double Azimuth = std::atan2(Aim.y, Aim.x) + Random.NextUniform(-0.35, 0.35);
		const double A = Random.NextUniform(-0.5, 0.5);
		const double B = Random.NextUniform(-0.5, 0.5);
		In.Strikes.PushBack(simtest::Strike(0, Random.NextUniform(0.5, 6.0), Azimuth, Random.NextUniform(0.0, 15.0) * kDegToRad, A, B));
	}

	// Racks the discipline's balls on table T (rules::GenerateRack with Gaps and Seed, per-ball specs from Set or the standard
	// ball); returns the ball at the apex site.
	inline int PlaceRack(rb::SimInput& In, const rb::TableGeometry& T, rb::rules::Discipline Discipline, rb::rules::RulesPreset Preset, std::uint64_t Seed,
		const rb::RackGapParams& Gaps, const rb::BallSet* Set)
	{
		using namespace rb;
		const rules::RulesTable Rules = rules::MakeRulesTable(T.Spec.Length, T.Spec.Width, simtest::kR);
		rules::RackAssignment Rack;
		rules::GenerateRack(Discipline, rules::MakeRulesConfig(Preset), Rules, Seed, false, Gaps, Rack);
		for (int b = 1; b < rules::kRulesBallCount; ++b)
		{
			if (Rack.Racked[b])
			{
				const BallSpec Spec = Set != nullptr ? Set->Balls[b] : MakeBallSpec(simtest::kR, kDefaultBallMass);
				simtest::Place(In, b, ToVec3(Rack.Position[b], Spec.Radius), {}, {}, Spec);
			}
		}
		return Rack.BallAtSite[0] >= 0 ? Rack.BallAtSite[0] : 1;
	}

	// prior-art 7.5 B2: a 9-ball (or 8-ball) break with seeded rack gaps (kRackGapMixture), V in [8, 13] m/s from the head string area.
	inline void MakeB2Break(std::uint64_t Seed, const rb::TableGeometry& T, rb::SimInput& In, bool EightBall = false)
	{
		using namespace rb;
		In = SimInput{};
		In.Table = &T;
		In.Params = simtest::ValParams();
		Rng Random(Seed);
		const int Apex = PlaceRack(In, T, EightBall ? rules::Discipline::EightBall : rules::Discipline::NineBall,
			EightBall ? rules::RulesPreset::Wpa8Ball : rules::RulesPreset::Wpa9Ball, Seed, kRackGapMixture, nullptr);
		const Vec3 Cue{-T.HalfLength / 2.0 - 0.05, Random.NextUniform(-0.3, 0.3), simtest::kR};
		simtest::Place(In, 0, Cue);
		const Vec3 Aim = In.Balls[Apex].State.Position - Cue;
		In.Strikes.PushBack(simtest::Strike(0, Random.NextUniform(8.0, 13.0), std::atan2(Aim.y, Aim.x), 0.0, 0.0, Random.NextUniform(-0.2, 0.0)));
	}

	// Where a level stroke's struck ball has no spin about the axis across its path (the stun point, motion A.2 / A.4): the
	// post-strike state from StrikeCueBall (the loop's own strike) evolved with MakeSegment / EvaluateSegment (closed forms); the
	// spin component along z_hat x d crosses zero by bisection. Returns the distance from the start.
	inline double StunDistance(const rb::SimInput& In, const rb::StrikeRequest& S)
	{
		using namespace rb;
		const PhysicsParams& P = In.Params;
		const BallSpec& Spec = In.Balls[S.Ball].Spec;
		const StrikeResult Hit = StrikeCueBall(S.Input, In.Balls[S.Ball].State, Spec, P.Cloth, P.Slate, P.Pinch, P.Gravity, P.Numerics);
		const MotionSegment Seg = MakeSegment(Hit.State, 0.0, Spec, P.Cloth, 0.0, P.Gravity, P.Tilt);
		const Vec3 D = Normalized(Vec3{Hit.State.Velocity.x, Hit.State.Velocity.y, 0.0});
		const Vec3 Across = Cross(Vec3::UnitZ(), D);
		const auto Spin = [&](double Tau) { return Dot(EvaluateSegment(Seg, Tau).Omega, Across); };
		double Lo = 0.0;
		double Hi = Seg.TauEnd;
		for (int k = 0; k < 200 && Hi - Lo > 1e-15; ++k)
		{
			const double Mid = 0.5 * (Lo + Hi);
			(Spin(Mid) < 0.0 ? Lo : Hi) = Mid;
		}
		return Dot(EvaluateSegment(Seg, Lo).Position - In.Balls[S.Ball].State.Position, D);
	}

	// A straight-in stun shot along the unit plan direction Dir: ball Object at ObjectAt, ball Cue behind it at the distance where a
	// level center-line stroke at Speed with draw B loses its spin (translation invariant on a level table), so it arrives with no
	// spin and stops (collisions 2: a full stun hit leaves it (1 - e_b)/2 of its speed). Standard balls unless already placed with
	// a spec. Returns the cue ball's start.
	inline rb::Vec2 SetupStunShot(rb::SimInput& In, int Cue, int Object, const rb::Vec2& ObjectAt, const rb::Vec2& Dir, double Speed, double B)
	{
		using namespace rb;
		const double R = simtest::kR;
		simtest::Place(In, Cue, ToVec3(ObjectAt, R));
		StrikeRequest S = simtest::Strike(Cue, Speed, std::atan2(Dir.y, Dir.x), 0.0, 0.0, B);
		S.Input.SquirtEnabled = false;
		const double Stun = StunDistance(In, S);
		const Vec2 Start = ObjectAt - Dir * (Stun + 2.0 * R); // touching the object ball exactly at the stun point
		simtest::Place(In, Cue, ToVec3(Start, R));
		simtest::Place(In, Object, ToVec3(ObjectAt, R));
		In.Strikes.PushBack(S);
		return Start;
	}
}
