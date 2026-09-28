#pragma once

// Procedural cue mesh (M1 "simple cue", ue5-realism-plan 5.5, 6.4): a lathe along the cue axis. Local frame:
// origin = tip DOME CENTRE, +X points from the butt TO THE TIP (the butt end lies at about X = -Length), so
// ARbCue::SetPoseCore only needs the dome centre and the direction. Sections: tip (leather, dome radius), ferrule, maple
// shaft (linear taper r_t -> joint), joint collar, forearm, wrap, butt sleeve, bumper. Radii follow
// rb::human::CueBodyState (TipRadius, ButtRadius: the same taper the clearance test and ExecuteStroke use).
// Material ids per section so one material with section masks or several slots can be used. Owner: UE-4.

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMesh3.h"

#include "rb/Equipment/Cue.h"
#include "rb/Human/CueState.h"

enum class ERbCueSection : uint8
{
	Tip,
	Ferrule,
	Shaft,
	Joint,
	Forearm,
	Wrap,
	Sleeve,
	Bumper,
	Count
};

struct FRbCueMeshOptions
{
	int32 RadialSegments = 32;
	double FerruleLengthCm = 2.5;
	double JointFromTipCm = 74.0;   // shaft length (CueBodyState::ShaftLength)
	double WrapStartFromTipCm = 100.0;
	double WrapLengthCm = 25.0;
};

namespace RbCueMeshBuilder
{
	// Units: centimetres, local frame as described above; triangle groups = ERbCueSection.
	RAWBREAK_API void BuildCue(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FRbCueMeshOptions& Options,
		UE::Geometry::FDynamicMesh3& Out);
}
