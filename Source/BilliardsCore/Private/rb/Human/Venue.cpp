#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.4, 4.5.5, HF-41, HF-B11.
#include "rb/Human/Venue.h"

#include "rb/Math/Scalar.h"
#include "rb/Math/Vec3.h"

namespace rb::human
{
	namespace
	{
		std::uint64_t IndexKey(int Index) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(Index)); }

		// Field draws of one house-cue slot: U01(HashKeys(VenueSeed, kVenueHouseCuePurpose, RackSlot, Generation, Field)).
		enum class HouseCueField : std::uint64_t
		{
			Mass = 0,
			ShortCue = 1,
			ShortLength = 2,
			BowClass = 3,
			BowSize = 4,
			TipWidth = 5,
			TipDome = 6,
			TipGlaze = 7,
			TipOverhang = 8,
			TipLoose = 9,
			TipRestitution = 10,
		};

		double Lerp(double Lo, double Hi, double U) { return Lo + (Hi - Lo) * U; }
	}

	Vec2 SeedTableSlope(std::uint64_t VenueSeed, int TableIndex, VenueKind Kind, bool FirstCareerTable)
	{
		double Lo = 0.0;
		double Hi = 0.1e-3;
		switch (Kind)
		{
		case VenueKind::DiveBar: Lo = 0.5e-3; Hi = 2.5e-3; break;
		case VenueKind::PoolHall: Lo = 0.0; Hi = 0.3e-3; break;
		case VenueKind::Arena: break;
		}
		if (FirstCareerTable)
		{
			// Fairness (4.5.5): the first career table is seeded at <= 1 mm/m; the range is rescaled, so it stays uniform.
			Hi = Min(Hi, 1.0e-3);
			Lo = Min(Lo, Hi);
		}
		const double Magnitude = Lerp(Lo, Hi, U01(HashKeys(VenueSeed, kVenueSlopePurpose, IndexKey(TableIndex), std::uint64_t{0})));
		const double Direction = kTwoPi * U01(HashKeys(VenueSeed, kVenueSlopePurpose, IndexKey(TableIndex), std::uint64_t{1}));
		return {Magnitude * Cos(Direction), Magnitude * Sin(Direction)};
	}

	double VenueBallCling(VenueKind Kind)
	{
		return Kind == VenueKind::DiveBar ? 1.3 : 1.0;
	}

	TableCondition MakeVenueTableCondition(std::uint64_t VenueSeed, int TableIndex, VenueKind Kind, bool FirstCareerTable, bool ChalkCling)
	{
		TableCondition Condition;
		Condition.Slope = SeedTableSlope(VenueSeed, TableIndex, Kind, FirstCareerTable);
		Condition.BallCling = VenueBallCling(Kind);
		Condition.ChalkCling = ChalkCling;
		return Condition;
	}

	std::uint64_t VenueBallSetSeed(std::uint64_t VenueSeed, int TableIndex)
	{
		return HashKeys(VenueSeed, kVenueBallSetPurpose, IndexKey(TableIndex));
	}

	HouseCue SeedHouseCue(std::uint64_t VenueSeed, int RackSlot, std::uint32_t Generation)
	{
		const std::uint64_t Prefix = HashKeys(VenueSeed, kVenueHouseCuePurpose, IndexKey(RackSlot), static_cast<std::uint64_t>(Generation));
		const auto Draw = [Prefix](HouseCueField Field) { return U01(Mix64(Prefix ^ static_cast<std::uint64_t>(Field))); };

		HouseCue Cue;
		Cue.Spec = kCueHouse19oz; // m / m_e = 15 (EndMass 0.170 / 15), 57 in

		// Mass U{18, 19, 20, 21} oz.
		const int MassIndex = static_cast<int>(Min(3.0, Floor(4.0 * Draw(HouseCueField::Mass)))); // U01 in [0, 1): 0 .. 3
		Cue.Spec.Mass = (18.0 + static_cast<double>(MassIndex)) * kOunce;

		// Length 57 in; short 48 / 52 in cues near walls (HF-32): one slot in eight, TUNING.
		Cue.Short = Draw(HouseCueField::ShortCue) < 0.125;
		if (Cue.Short)
		{
			Cue.Spec.Length = (Draw(HouseCueField::ShortLength) < 0.5 ? 48.0 : 52.0) * kInch;
		}

		// Bow s_w: 60 % < 0.5 mm, 30 % 0.5-2 mm, 10 % 2-5 mm (HF-30); direction random per pickup (channel 10).
		const double BowClass = Draw(HouseCueField::BowClass);
		const double BowSize = Draw(HouseCueField::BowSize);
		Cue.Body.BowSag = BowClass < 0.6 ? Lerp(0.0, 0.5e-3, BowSize) : (BowClass < 0.9 ? Lerp(0.5e-3, 2.0e-3, BowSize) : Lerp(2.0e-3, 5.0e-3, BowSize));
		Cue.Body.WarpKnown = false;

		// Tip: w_tip 11-13 mm, r_dome 12-20 mm, glaze 0.3-0.9, overhang 0-1 mm, 10 % loose, e_tip 0.68-0.72 (4.4).
		Cue.Tip.Width = Lerp(11.0e-3, 13.0e-3, Draw(HouseCueField::TipWidth));
		Cue.Tip.DomeRadius = Lerp(12.0e-3, 20.0e-3, Draw(HouseCueField::TipDome));
		Cue.Tip.Glaze = Lerp(0.3, 0.9, Draw(HouseCueField::TipGlaze));
		Cue.Tip.Overhang = Lerp(0.0, 1.0e-3, Draw(HouseCueField::TipOverhang));
		Cue.Tip.Loose = Draw(HouseCueField::TipLoose) < 0.1;
		Cue.Tip.Restitution = Lerp(0.68, 0.72, Draw(HouseCueField::TipRestitution));
		Cue.Body.TipRadius = 0.5 * Cue.Tip.Width;

		// TipState is authoritative (it feeds the strike); the CueSpec view agrees with it.
		Cue.Spec.TipRestitution = EffectiveTipRestitution(Cue.Tip, TipParams{});
		Cue.Spec.TipDomeRadius = Cue.Tip.DomeRadius;
		Cue.Spec.TipDiameter = Cue.Tip.Width;
		return Cue;
	}
}
