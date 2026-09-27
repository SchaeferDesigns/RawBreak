// A-REC-2: BuildRulesTable (rules.md 2.1-2.2 from the single geometry source): landmarks, pocket openings, per-ball
// radii; the rules' OverPocketOpening on the result is the geometry's IsOverPocketOpening (WP-7).

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/TableRules.h"
#include "rb/Shot/ShotRecordBuilder.h"

#include <memory>

using namespace playtest;

RB_TEST(ARCH_REC2_BuildRulesTableFromGeometry)
{
	const rb::TablePreset Presets[] = {rb::TablePreset::NineFootPro, rb::TablePreset::NineFootTight, rb::TablePreset::EightFootPro,
		rb::TablePreset::EightFootHome, rb::TablePreset::SevenFootBar, rb::TablePreset::SevenFoot78, rb::TablePreset::SevenFootTrue};
	for (const rb::TablePreset Preset : Presets)
	{
		const std::unique_ptr<rb::TableGeometry> G = std::make_unique<rb::TableGeometry>();
		RB_REQUIRE(rb::BuildTableGeometry(rb::GetTableSpec(Preset), *G) == rb::ErrorCode::Ok);

		// Per-ball radii: an oversized cue ball, the rest nominal; ids beyond Count get the nominal radius.
		double Radii[3] = {rb::kOversizedCueBall.Radius, 0.028575, 0.0};
		const rb::rules::RulesTable T = rb::BuildRulesTable(*G, 0.028575, Radii, 3);
		RB_CHECK(T.BallRadius[0] == rb::kOversizedCueBall.Radius);
		RB_CHECK(T.BallRadius[1] == 0.028575);
		RB_CHECK(T.BallRadius[2] == 0.028575); // non-positive entry -> nominal
		RB_CHECK(T.BallRadius[15] == 0.028575);
		RB_CHECK(T.NominalBallRadius == 0.028575);

		// Landmarks: the geometry's, bitwise equal to MakeRulesTable's expressions.
		const rb::rules::RulesTable M = rb::rules::MakeRulesTable(G->Spec.Length, G->Spec.Width, 0.028575);
		RB_CHECK(SameBits(T.Length, G->Spec.Length) && SameBits(T.Width, G->Spec.Width));
		RB_CHECK(SameBits(T.HeadStringX, G->Landmarks.HeadStringX) && SameBits(T.HeadStringX, M.HeadStringX));
		RB_CHECK(SameBits(T.FootStringX, G->Landmarks.FootStringX) && SameBits(T.FootStringX, M.FootStringX));
		RB_CHECK(SameBits(T.BaulkX, G->Landmarks.BaulkX) && SameBits(T.BaulkX, M.BaulkX));
		RB_CHECK(SameBits(T.HeadSpot.x, M.HeadSpot.x) && SameBits(T.FootSpot.x, M.FootSpot.x) && SameBits(T.CenterSpot.x, M.CenterSpot.x));

		// Pocket openings, index = PocketId.
		RB_REQUIRE(T.PocketCount == rb::kPocketCount);
		for (int k = 0; k < rb::kPocketCount; ++k)
		{
			const rb::PocketGeometry& P = G->Pockets[k];
			const rb::rules::PocketOpening& O = T.Pockets[static_cast<int>(P.Id)];
			RB_CHECK(SameBits(O.JawPoint[0].x, P.JawPoint[0].x) && SameBits(O.JawPoint[0].y, P.JawPoint[0].y));
			RB_CHECK(SameBits(O.JawPoint[1].x, P.JawPoint[1].x) && SameBits(O.JawPoint[1].y, P.JawPoint[1].y));
			RB_CHECK(SameBits(O.Axis.x, P.Axis.x) && SameBits(O.Axis.y, P.Axis.y));
			RB_CHECK(SameBits(O.CaptureCenter.x, P.CaptureCenter.x) && SameBits(O.CaptureCenter.y, P.CaptureCenter.y));
			RB_CHECK(SameBits(O.DropEdgeRadius, P.DropEdgeRadius));
		}

		// The rules' opening predicate equals the geometry's everywhere (random points near every pocket and on the bed).
		rb::Rng Rng(static_cast<std::uint64_t>(Preset) + 11u);
		int Over = 0;
		for (int i = 0; i < 20000; ++i)
		{
			rb::Vec2 P;
			if (i % 2 == 0)
			{
				const rb::PocketGeometry& Pocket = G->Pockets[i / 2 % rb::kPocketCount];
				P = Pocket.MouthMid + rb::Vec2{Rng.NextUniform(-0.12, 0.12), Rng.NextUniform(-0.12, 0.12)};
			}
			else
			{
				P = {Rng.NextUniform(-0.5 * G->Spec.Length, 0.5 * G->Spec.Length), Rng.NextUniform(-0.5 * G->Spec.Width, 0.5 * G->Spec.Width)};
			}
			const bool Geometric = rb::IsOverPocketOpening(*G, P);
			RB_CHECK(Geometric == rb::rules::OverPocketOpening(P, T));
			Over += Geometric ? 1 : 0;
		}
		RB_CHECK(Over > 1000);
		RB_CHECK(!rb::rules::OverPocketOpening({0.0, 0.0}, T));
		RB_CHECK(rb::rules::OverPocketOpening(G->Pockets[2].CaptureCenter, T));
	}

	// Pocketless table: no openings.
	rb::TableSpec Carom = rb::kTableNineFootPro;
	Carom.HasPockets = false;
	const std::unique_ptr<rb::TableGeometry> G = std::make_unique<rb::TableGeometry>();
	RB_REQUIRE(rb::BuildTableGeometry(Carom, *G) == rb::ErrorCode::Ok);
	const rb::rules::RulesTable T = rb::BuildRulesTable(*G, 0.03, nullptr, 0);
	RB_CHECK(T.PocketCount == 0);
	RB_CHECK(T.BallRadius[0] == 0.03 && T.BallRadius[7] == 0.03);
	RB_CHECK(!rb::rules::OverPocketOpening({0.5 * Carom.Length - 0.01, 0.5 * Carom.Width - 0.01}, T));

	// An unbuilt geometry still gives MakeRulesTable's landmarks.
	rb::TableGeometry Empty;
	Empty.Spec = rb::kTableSevenFootBar;
	const rb::rules::RulesTable E = rb::BuildRulesTable(Empty, 0.028575, nullptr, 0);
	const rb::rules::RulesTable M = rb::rules::MakeRulesTable(Empty.Spec.Length, Empty.Spec.Width, 0.028575);
	RB_CHECK(SameBits(E.HeadStringX, M.HeadStringX) && SameBits(E.BaulkX, M.BaulkX) && E.PocketCount == 0);
}
