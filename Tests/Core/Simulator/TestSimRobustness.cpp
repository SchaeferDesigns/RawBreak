// Owner: WP-6a (simulator core loop). prior-art 9 robustness rows of the loop: ROB-10 (determinism, standalone half), ROB-11
// (invariant monitor; Integ_: the benchmark sets need islands and pockets), ROB-12 (energy monitor), ROB-14 (cushion joints and jaw
// arcs of every preset; Integ_: balls shot at jaws end in pockets). Benchmark protocol B1 / B2 of prior-art 7.5.

#include "rbtest.h"

#include "Simulator/SimTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/TableRules.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace rb;
using namespace simtest;

namespace
{
#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	constexpr int kScale = 1; // Debug: reduced counts (architecture 18: heavy property tests)
#else
	constexpr int kScale = 5;
#endif

	// prior-art 7.5 B1: a mid-game 9-ball layout (2-9 object balls, seeded), V in [0.5, 6] m/s, tip offsets within +-0.5 R, cue
	// elevation 0-15 deg. The stroke is aimed at a random object ball with a random cut so that most shots have contacts.
	void MakeB1Shot(std::uint64_t Seed, const TableGeometry& T, SimInput& In)
	{
		In = SimInput{};
		In.Table = &T;
		In.Params = ValParams();
		Rng Random(Seed);
		const int Balls = 3 + static_cast<int>(Random.NextBelow(9)); // CB + 2..10 balls (ids 0..)
		const double Margin = kR + 0.03;
		int Placed = 0;
		for (int Attempt = 0; Attempt < 2000 && Placed < Balls; ++Attempt)
		{
			const Vec3 P{Random.NextUniform(-T.HalfLength + Margin, T.HalfLength - Margin), Random.NextUniform(-T.HalfWidth + Margin, T.HalfWidth - Margin), kR};
			bool Free = true;
			for (int j = 0; j < Placed && Free; ++j)
			{
				Free = Length(P - In.Balls[j].State.Position) > 2.0 * kR + 0.005;
			}
			for (int p = 0; p < T.Pockets.Size() && Free; ++p)
			{
				Free = Length(XY(P) - T.Pockets[p].MouthMid) > 0.12;
			}
			if (Free)
			{
				Place(In, Placed, P);
				++Placed;
			}
		}
		const int Target = 1 + static_cast<int>(Random.NextBelow(static_cast<std::uint32_t>(Placed - 1)));
		const Vec3 Aim = In.Balls[Target].State.Position - In.Balls[0].State.Position;
		const double Azimuth = std::atan2(Aim.y, Aim.x) + Random.NextUniform(-0.35, 0.35);
		const double A = Random.NextUniform(-0.5, 0.5);
		const double B = Random.NextUniform(-0.5, 0.5);
		In.Strikes.PushBack(Strike(0, Random.NextUniform(0.5, 6.0), Azimuth, Random.NextUniform(0.0, 15.0) * kDegToRad, A, B));
	}

	// prior-art 7.5 B2: a 9-ball break with seeded rack gaps, V in [8, 13] m/s from the head string area.
	void MakeB2Break(std::uint64_t Seed, const TableGeometry& T, SimInput& In)
	{
		In = SimInput{};
		In.Table = &T;
		In.Params = ValParams();
		Rng Random(Seed);
		const rules::RulesTable Rules = rules::MakeRulesTable(T.Spec.Length, T.Spec.Width, kR);
		rules::RackAssignment Rack;
		rules::GenerateRack(rules::Discipline::NineBall, rules::MakeRulesConfig(rules::RulesPreset::Wpa9Ball), Rules, Seed, false, kRackGapMixture, Rack);
		for (int b = 1; b < rules::kRulesBallCount; ++b)
		{
			if (Rack.Racked[b])
			{
				Place(In, b, ToVec3(Rack.Position[b], kR));
			}
		}
		const Vec3 Cue{-T.HalfLength / 2.0 - 0.05, Random.NextUniform(-0.3, 0.3), kR};
		Place(In, 0, Cue);
		const int Apex = Rack.BallAtSite[0] >= 0 ? Rack.BallAtSite[0] : 1;
		const Vec3 Aim = In.Balls[Apex].State.Position - Cue;
		In.Strikes.PushBack(Strike(0, Random.NextUniform(8.0, 13.0), std::atan2(Aim.y, Aim.x), 0.0, 0.0, Random.NextUniform(-0.2, 0.0)));
	}

	// RB_ROB10_PRINT set in the environment: print the ROB-10 hash (Debug / Release / UE comparison).
	bool PrintRequested()
	{
#if defined(_MSC_VER)
		char* Value = nullptr;
		std::size_t Size = 0;
		const bool Set = _dupenv_s(&Value, &Size, "RB_ROB10_PRINT") == 0 && Value != nullptr;
		std::free(Value);
		return Set;
#else
		return std::getenv("RB_ROB10_PRINT") != nullptr;
#endif
	}

	bool FiniteState(const BallState& S)
	{
		return std::isfinite(S.Position.x) && std::isfinite(S.Position.y) && std::isfinite(S.Position.z) && std::isfinite(S.Velocity.x) &&
			std::isfinite(S.Velocity.y) && std::isfinite(S.Velocity.z) && std::isfinite(S.Omega.x) && std::isfinite(S.Omega.y) && std::isfinite(S.Omega.z);
	}

	// ROB-11 monitor: at every logged event time all ball-ball gaps >= -1e-9 m and every on-cloth ball's nose gap >= -1e-9 m (within
	// the nose's extent), on the exact (Analytic) segments; island stretches are Sampled for playback (linear within 10 um), where the
	// island's own contact model applies. Returns the number of violations.
	int InvariantViolations(const ShotResult& R, const SimInput& In, const TableGeometry& T)
	{
		int Violations = 0;
		for (const ShotEvent& E : R.Events)
		{
			BallState S[kMaxBalls];
			bool Live[kMaxBalls] = {};
			for (int b = 0; b < kMaxBalls; ++b)
			{
				SegmentKind Kind = SegmentKind::Analytic;
				Live[b] = In.Balls[b].InPlay && TrackState(R, b, E.Time, S[b], &Kind) && Kind == SegmentKind::Analytic && !IsTerminal(S[b].State) &&
					!IsInPocket(S[b].State);
			}
			for (int i = 0; i < kMaxBalls; ++i)
			{
				if (!Live[i])
				{
					continue;
				}
				for (int j = i + 1; j < kMaxBalls; ++j)
				{
					if (Live[j] && Length(S[j].Position - S[i].Position) - (In.Balls[i].Spec.Radius + In.Balls[j].Spec.Radius) < -1e-9)
					{
						++Violations;
					}
				}
				if (S[i].Position.z - In.Balls[i].Spec.Radius > 1e-9)
				{
					continue;
				}
				for (int c = 0; c < T.Noses.Size(); ++c)
				{
					const NoseSegment& Nose = T.Noses[c];
					const double Along = Dot(XY(S[i].Position) - Nose.Start, Nose.Direction);
					if (Nose.Present && Along >= 0.0 && Along <= Nose.Length && NoseGap(T, c, S[i].Position, In.Balls[i].Spec.Radius) < -1e-9)
					{
						++Violations;
					}
				}
			}
		}
		return Violations;
	}
}

// VAL ROB-10 (standalone half): identical inputs give bitwise identical results - the serialized event logs of B1 shots hash
// identically with a reused and a fresh simulator, in forward and reverse order (no state survives a Run). The Debug / Release
// comparison of the same hash runs with RB_ROB10_PRINT set (the value is printed; it must be equal in both builds and in the UE
// module build, prior-art 5.9).
RB_TEST(VAL_ROB10_DeterministicEventLogs)
{
	const TableGeometry& T = NineFoot();
	const int Shots = 40 * kScale;
	static std::uint64_t Hashes[1000];
	SimInput& In = InputSlot();
	ShotResult& R = ResultSlot();
	Simulator Reused;
	std::uint64_t Combined = 0xCBF29CE484222325ull; // the first 40 shots (the Debug set) for the build comparison
	for (int s = 0; s < Shots; ++s)
	{
		MakeB1Shot(1000u + static_cast<std::uint64_t>(s), T, In);
		RB_REQUIRE(Reused.Run(In, R) == SimStatus::Ok);
		Hashes[s] = ResultHash(R);
		if (s < 40)
		{
			Mix(Combined, Hashes[s]);
		}
	}
	int Mismatches = 0;
	for (int s = Shots - 1; s >= 0; --s)
	{
		MakeB1Shot(1000u + static_cast<std::uint64_t>(s), T, In);
		Simulator Fresh;
		ShotResult& Other = ResultSlot(1);
		Other = ShotResult{};
		Fresh.Run(In, Other);
		Mismatches += ResultHash(Other) != Hashes[s] ? 1 : 0;
		Reused.Run(In, R); // reversed order on the reused simulator
		Mismatches += ResultHash(R) != Hashes[s] ? 1 : 0;
	}
	RB_CHECK(Mismatches == 0);
	if (PrintRequested())
	{
		std::printf("ROB-10 hash of the first 40 B1 shots: %016llx\n", static_cast<unsigned long long>(Combined));
	}
}

// VAL ROB-11: invariant monitor on the benchmark sets (B1 shots and B2 breaks): no ball-ball or cushion penetration beyond 1e-9 m at
// any event, no de-penetration (overlap diagnostic) logged. Needs WP-6b (clusters, frozen balls and pockets).
RB_TEST(Integ_VAL_ROB11_NoPenetrationOnBenchmarkSets)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = InputSlot();
	ShotResult& R = ResultSlot();
	Simulator Sim;
	int Violations = 0;
	int Overlaps = 0;
	for (int s = 0; s < 40 * kScale; ++s)
	{
		MakeB1Shot(5000u + static_cast<std::uint64_t>(s), T, In);
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		Violations += InvariantViolations(R, In, T);
		Overlaps += R.Diagnostics.OverlapWarnings;
	}
	for (int s = 0; s < 4 * kScale; ++s)
	{
		MakeB2Break(9000u + static_cast<std::uint64_t>(s), T, In);
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		Violations += InvariantViolations(R, In, T);
		Overlaps += R.Diagnostics.OverlapWarnings;
	}
	RB_CHECK(Violations == 0);
	RB_CHECK(Overlaps == 0);
}

// VAL ROB-12: energy monitor - total mechanical energy (translational + rotational + lift) never increases along a segment or across
// a cue-free event (+1e-12 J), except at the stick events (the cue strike and follow-through tip impulses); ball-ball events are
// checked on the pair's total.
RB_TEST(VAL_ROB12_EnergyNeverIncreasesBetweenEvents)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = InputSlot();
	ShotResult& R = ResultSlot();
	Simulator Sim;
	int Increases = 0;
	int Checked = 0;
	for (int s = 0; s < 30 * kScale; ++s)
	{
		MakeB1Shot(7000u + static_cast<std::uint64_t>(s), T, In);
		In.Strikes[0].Input.Elevation = 0.0; // level strokes: no slate landings (WP-6b) in this monitor
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		const double G = In.Params.Gravity;
		for (const ShotEvent& E : R.Events)
		{
			if (E.Type == ShotEventType::BallBall)
			{
				const double Before = KineticEnergy(E.Pre[0], In.Balls[E.A].Spec, G) + KineticEnergy(E.Pre[1], In.Balls[E.B].Spec, G);
				const double After = KineticEnergy(E.Post[0], In.Balls[E.A].Spec, G) + KineticEnergy(E.Post[1], In.Balls[E.B].Spec, G);
				Increases += After > Before + 1e-12 ? 1 : 0;
				++Checked;
			}
			else if (E.Type == ShotEventType::BallCushion || E.Type == ShotEventType::BallJaw || E.Type == ShotEventType::MotionTransition)
			{
				Increases += KineticEnergy(E.Post[0], In.Balls[E.A].Spec, G) > KineticEnergy(E.Pre[0], In.Balls[E.A].Spec, G) + 1e-12 ? 1 : 0;
				++Checked;
			}
		}
		for (int b = 0; b < kMaxBalls; ++b)
		{
			for (const TrajectorySegment& S : R.Tracks[b].Segments)
			{
				if (S.Kind != SegmentKind::Analytic || !(S.T1 < kInfinity))
				{
					continue;
				}
				const double Start = KineticEnergy(EvaluateSegment(S.Motion, 0.0), In.Balls[b].Spec, G);
				const double End = KineticEnergy(EvaluateSegment(S.Motion, S.T1 - S.Motion.T0), In.Balls[b].Spec, G);
				Increases += End > Start + 1e-12 ? 1 : 0;
				++Checked;
			}
		}
	}
	RB_CHECK(Checked > 200);
	RB_CHECK(Increases == 0);
}

// VAL ROB-14: every cushion joint (nose end at a jaw tangent point) and every jaw arc of every table preset, balls shot at it with
// random sub-mm offsets, angles and speeds: no escape, no event before the previous one, no NaN. Needs WP-6b (pocketing).
RB_TEST(Integ_VAL_ROB14_CushionJointsWatertight)
{
	const TablePreset Presets[] = {TablePreset::NineFootPro, TablePreset::NineFootTight, TablePreset::EightFootPro, TablePreset::EightFootHome,
		TablePreset::SevenFootBar, TablePreset::SevenFoot78, TablePreset::SevenFootTrue};
	SimInput& In = InputSlot();
	ShotResult& R = ResultSlot();
	Simulator Sim;
	int Escapes = 0;
	int Disorders = 0;
	int NonFinite = 0;
	int Shots = 0;
	Rng Random(14);
	for (TablePreset Preset : Presets)
	{
		const TableGeometry& T = Table(GetTableSpec(Preset));
		for (int k = 0; k < 2000 * kScale; ++k)
		{
			// Target: a nose end (a joint with the jaw arc) or a jaw arc point.
			Vec2 Target;
			Vec2 Inward;
			const int Jaw = static_cast<int>(Random.NextBelow(static_cast<std::uint32_t>(T.JawArcs.Size())));
			const JawArc& Arc = T.JawArcs[Jaw];
			if (Random.NextBelow(2) == 0)
			{
				Target = Arc.TangentOnNose;
				const double Angle = Arc.AngleFrom; // outward normal of the nose at the tangent point
				Inward = -Vec2{std::cos(Angle), std::sin(Angle)};
			}
			else
			{
				const double Angle = Arc.AngleFrom + Random.NextUniform(0.0, Arc.AngleSweep);
				Target = Arc.Center + Vec2{std::cos(Angle), std::sin(Angle)} * Arc.Radius;
				Inward = -Vec2{std::cos(Angle), std::sin(Angle)};
			}
			const double Turn = Random.NextUniform(-1.2, 1.2);
			const Vec2 Dir{Inward.x * std::cos(Turn) - Inward.y * std::sin(Turn), Inward.x * std::sin(Turn) + Inward.y * std::cos(Turn)};
			const Vec2 Aim = Target + Vec2{Random.NextUniform(-5e-4, 5e-4), Random.NextUniform(-5e-4, 5e-4)};
			const Vec2 Start = Aim - Dir * 0.25;
			if (!T.PlayingArea.Contains(Start) || std::fabs(Start.x) > T.HalfLength - kR || std::fabs(Start.y) > T.HalfWidth - kR)
			{
				continue;
			}
			In = SimInput{};
			In.Table = &T;
			In.Params = ValParams();
			const double Speed = Random.NextUniform(0.3, 4.0);
			PlaceRolling(In, 0, ToVec3(Start, kR), ToVec3(Dir * Speed));
			if (Sim.Run(In, R) != SimStatus::Ok)
			{
				++Escapes;
				continue;
			}
			++Shots;
			double Last = 0.0;
			for (const ShotEvent& E : R.Events)
			{
				Disorders += E.Time < Last ? 1 : 0;
				Last = E.Time;
			}
			const BallFinal& F = R.Finals[0];
			NonFinite += FiniteState(F.State) ? 0 : 1;
			if (F.Status == BallFinalStatus::OnTable)
			{
				bool Inside = std::fabs(F.State.Position.x) <= T.HalfLength && std::fabs(F.State.Position.y) <= T.HalfWidth;
				for (int p = 0; p < T.Pockets.Size() && !Inside; ++p)
				{
					Inside = Length(XY(F.State.Position) - T.Pockets[p].MouthMid) < 0.12; // resting on a shelf in the pocket mouth
				}
				Escapes += Inside ? 0 : 1;
			}
			else if (F.Status == BallFinalStatus::OffTable)
			{
				++Escapes; // a rolling ball cannot leave the table
			}
		}
	}
	RB_CHECK(Shots > 1000 * kScale);
	RB_CHECK(Escapes == 0);
	RB_CHECK(Disorders == 0);
	RB_CHECK(NonFinite == 0);
}
