#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 2, 4, 5, 11 (ValidateWpa sketch), T-WPA-1..3.
#include "rb/Equipment/TableSpec.h"

#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		// Allowance for the decimal -> binary rounding of the limits (e.g. 4.5 in = 0.1143 m, 12 deg): the
		// WPA limits themselves pass. Far below every manufacturing tolerance.
		constexpr double kLengthSlack = 1e-9; // [m]
		constexpr double kAngleSlack = 1e-9;  // [rad]

		// Closed range [Lo, Hi] with slack; NaN fails.
		constexpr bool InRange(double Value, double Lo, double Hi, double Slack)
		{
			return Value >= Lo - Slack && Value <= Hi + Slack;
		}

		constexpr bool Near(double Value, double Nominal, double Tolerance, double Slack)
		{
			return InRange(Value, Nominal - Tolerance, Nominal + Tolerance, Slack);
		}

		constexpr std::uint32_t Bit(WpaCheck Check) { return 1u << static_cast<unsigned>(Check); }
	}

	WpaReport ValidateWpa(const TableSpec& Spec)
	{
		// WPA Recommended Equipment Specifications (equipment 2, 4, 5.2, 11): section 5 playing surface,
		// 2 bed height, 6 rail width, 7 nose height, 9 pockets, facing thickness.
		const double Eighth = 0.125 * kInch;
		const bool NineFoot = Near(Spec.Length, 100.0 * kInch, Eighth, kLengthSlack) && Near(Spec.Width, 50.0 * kInch, Eighth, kLengthSlack);
		const bool EightFoot = Near(Spec.Length, 92.0 * kInch, Eighth, kLengthSlack) && Near(Spec.Width, 46.0 * kInch, Eighth, kLengthSlack);

		WpaReport Report;
		const auto Check = [&Report](WpaCheck Which, bool Passed) {
			if (!Passed)
			{
				Report.FailedMask |= Bit(Which);
			}
		};
		Check(WpaCheck::PlayingSurfaceSize, NineFoot || EightFoot);
		Check(WpaCheck::BedHeight, InRange(Spec.BedHeight, 29.25 * kInch, 31.0 * kInch, kLengthSlack));
		Check(WpaCheck::RailWidth, InRange(Spec.RailWidthTotal, 4.0 * kInch, 7.5 * kInch, kLengthSlack));
		Check(WpaCheck::CushionNoseHeight, InRange(Spec.CushionNoseHeight, 0.625 * kBallDiameter, 0.645 * kBallDiameter, kLengthSlack));
		Check(WpaCheck::CornerMouth, InRange(Spec.Corner.Mouth, 4.5 * kInch, 4.625 * kInch, kLengthSlack));
		Check(WpaCheck::SideMouth, InRange(Spec.Side.Mouth, 5.0 * kInch, 5.125 * kInch, kLengthSlack));
		Check(WpaCheck::CornerCutAngle, Near(Spec.Corner.CutAngle, 142.0 * kDegToRad, 1.0 * kDegToRad, kAngleSlack));
		Check(WpaCheck::SideCutAngle, Near(Spec.Side.CutAngle, 104.0 * kDegToRad, 1.0 * kDegToRad, kAngleSlack));
		Check(WpaCheck::CornerShelf, InRange(Spec.Corner.Shelf, 1.0 * kInch, 2.25 * kInch, kLengthSlack));
		Check(WpaCheck::SideShelf, InRange(Spec.Side.Shelf, 0.0, 0.375 * kInch, kLengthSlack));
		Check(WpaCheck::Backdraft, InRange(Spec.Backdraft, 12.0 * kDegToRad, 15.0 * kDegToRad, kAngleSlack));
		Check(WpaCheck::FacingThickness, InRange(Spec.FacingThickness, 0.0625 * kInch, 0.25 * kInch, kLengthSlack));
		return Report;
	}
}
