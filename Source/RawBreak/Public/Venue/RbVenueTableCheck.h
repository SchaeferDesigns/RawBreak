#pragma once

// Venue table checks of VDB-T10 (Docs/specs/venue-dive-bar.md 3.2): the seeded roll-off of a venue table, checked BY BEHAVIOUR
// (not by the sign convention of the slope vector): a ball rolled at 0.5 m/s along the long string, once from the head spot toward
// the foot and once from the foot spot toward the head, must end displaced toward the downhill side, and the uphill roll must come
// up shorter; the downhill azimuth (the slope vector with the sign the rolls prove) must lie within the tolerance of the target
// (the Low Bridge: toward the jukebox, core azimuth -147.9 deg +- 25 deg). Also the seed search that picked the layout's seed.
// Pure core simulation (no world, no UObjects). Owner: M2-A.

#include "CoreMinimal.h"

#include "Core/RbTypes.h"

struct RAWBREAK_API FRbRollOffCheck
{
	bool bValid = false;              // the table context could be built and both rolls ran
	bool bPass = false;
	double SlopeMmPerM = 0.0;         // |slope|
	double DownhillAzimuthDeg = 0.0;  // core frame, from +x toward +y
	double AzimuthErrorDeg = 0.0;     // |downhill - target| (wrapped)
	int32 SlopeSign = 0;              // +1: the ball runs toward +Slope, -1: toward -Slope (proved by the rolls)
	double TowardFootTravel = 0.0;    // x travelled by the roll toward the foot [m]
	double TowardHeadTravel = 0.0;    // |x| travelled by the roll toward the head [m]
	double TowardFootDriftY = 0.0;    // lateral end displacement [m]
	double TowardHeadDriftY = 0.0;
	FString Error;

	FString ToString() const;
};

namespace RbVenueTableCheck
{
	// Target of the Low Bridge (3.2): the jukebox (11.30, 6.97) seen from the cloth centre (13.759, 5.427) = V azimuth 147.9 deg,
	// core azimuth -147.9 deg (core +y = V -Y).
	inline constexpr double DiveBarRollOffAzimuthCoreDeg = -147.9;
	inline constexpr double DiveBarRollOffToleranceDeg = 25.0;
	inline constexpr double RollSpeed = 0.5; // [m/s]

	RAWBREAK_API FRbRollOffCheck CheckRollOff(ERbTablePreset Preset, ERbBallSetPreset BallSet, ERbVenueKind Kind, int64 VenueSeed, int32 TableIndex,
		bool bFirstCareerTable, double TargetAzimuthDeg = DiveBarRollOffAzimuthCoreDeg, double ToleranceDeg = DiveBarRollOffToleranceDeg);

	// First seed >= StartSeed (up to MaxTries) whose roll-off passes for BOTH FirstCareerTable values; -1 if none.
	RAWBREAK_API int64 FindRollOffSeed(ERbTablePreset Preset, ERbBallSetPreset BallSet, ERbVenueKind Kind, int32 TableIndex, int64 StartSeed,
		int32 MaxTries, double TargetAzimuthDeg = DiveBarRollOffAzimuthCoreDeg, double ToleranceDeg = DiveBarRollOffToleranceDeg);
}
