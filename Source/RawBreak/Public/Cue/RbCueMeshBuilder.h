#pragma once

// Procedural cue mesh (M1 "simple cue", ue5-realism-plan 5.5, 6.4): a lathe along the cue axis. Local frame:
// origin = tip DOME CENTRE, +X points from the butt TO THE TIP (the butt end lies at about X = -Length), so
// ARbCue::SetPoseCore only needs the dome centre and the direction. Sections: tip (leather, dome radius), ferrule, maple
// shaft (linear taper r_t -> joint), joint collar, forearm, wrap, butt sleeve, bumper. Radii follow
// rb::human::CueBodyState (TipRadius, ButtRadius: the same taper the clearance test and ExecuteStroke use).
// Material ids per section so one material with section masks or several slots can be used. Owner: UE-4.
//
// Geometry = the core's executed cue pose (human-factors 3.6, rb::human::ExecuteStroke), so the rendered cue, the
// clearance floor (RbCueClearance) and the core's shaft-contact test see the same body:
//  * dome: sphere cap of radius CueSpec::TipDomeRadius about the origin, apex at X = r_dome, rim (cap boundary, radius
//    CueSpec::TipDiameter / 2) at X = RimDepth = sqrt(r_dome^2 - (w_tip / 2)^2);
//  * body: runs back from the rim, s = RimDepth - X = distance behind the rim [cm], r(s) = r_t + (r_b - r_t) s / L with
//    L = CueSpec::Length (the body ends at s = L, X = RimDepth - L);
//  * sections along s: leather tip [0, TipHeight] (a flush cone from w_tip / 2 to r(TipHeight)), ferrule [TipHeight,
//    CueBodyState::FerruleLength] (the core's "Ferrule" contact zone = leather + ferrule), maple shaft [.., ShaftLength]
//    (the joint), joint collar, forearm, wrap, butt sleeve, rubber bumper whose end is rounded by a small fillet and
//    closed by a flat disc at s = L. Every vertex of the ferrule .. bumper lies ON the taper r(s) (the fillet and the end
//    disc lie inside it), so the clearance envelope is exact and conservative.
// Attributes: smooth analytic normals per section (hard edges at the rim, the fillet / disc and section seams share the
// geometric normal), UV0 = (s [m] behind the rim, azimuth / 2 pi) - "u along the axis in metres", UV1 = (section index,
// normalised position within the section 0..1), vertex colour = linear default albedo of the section (RGB) and a
// roughness hint (A) for a simple vertex-colour material, triangle group = ERbCueSection. One material slot (the bake
// writer's): M_RbCue reads the section from UV1.x / the vertex colour.

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
	int32 RadialSegments = 48;        // sagitta 0.034 mm at the 15.9 mm butt, 0.014 mm at the 6.5 mm ferrule (multiple of 4)
	int32 DomeRings = 8;              // rings of the tip dome cap
	double TipHeightCm = 0.6;         // leather above the ferrule (rb::human::TipState::Height)
	double FerruleLengthCm = -1.0;    // rim -> end of the ferrule (incl. the leather); < 0: CueBodyState::FerruleLength
	double JointFromTipCm = -1.0;     // rim -> joint (shaft length); < 0: CueBodyState::ShaftLength
	double JointCollarCm = 2.0;
	double WrapStartFromTipCm = 100.0;
	double WrapLengthCm = 25.0;
	double BumperLengthCm = 1.2;
	double BumperFilletCm = 0.3;      // rounded end of the bumper
	double MaxRingSpacingCm = 5.0;    // rings along the taper (the silhouette is straight; rings for UVs / vertex density)
};

// One section of the built cue: [StartS, EndS] behind the rim [cm] (the dome: StartS = -(r_dome - RimDepth)).
struct FRbCueSectionRange
{
	ERbCueSection Section = ERbCueSection::Tip;
	double StartS = 0.0;
	double EndS = 0.0;
};

namespace RbCueMeshBuilder
{
	// Units: centimetres, local frame as described above; triangle groups = ERbCueSection.
	RAWBREAK_API void BuildCue(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FRbCueMeshOptions& Options,
		UE::Geometry::FDynamicMesh3& Out);

	// Options with every length resolved and clamped into a valid, monotonic layout for this cue (RadialSegments a multiple of
	// 4 >= 8, DomeRings >= 2, FerruleLengthCm / JointFromTipCm from the body when < 0, every section inside the cue). The wrap
	// options describe the 58 in cue and scale with the length (L / 147.32 cm); the resolved values are the positions as
	// built for THIS cue (for reading, not for feeding back into BuildCue of another length).
	RAWBREAK_API FRbCueMeshOptions Resolve(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, const FRbCueMeshOptions& Options);

	// Sections along s of the resolved layout, tip first; empty sections are left out.
	RAWBREAK_API TArray<FRbCueSectionRange> SectionRanges(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body,
		const FRbCueMeshOptions& Options);

	// Tip rim distance ahead of the dome centre [cm]: sqrt(r_dome^2 - (w_tip / 2)^2) (0 when w_tip / 2 >= r_dome).
	RAWBREAK_API double RimDepthCm(const rb::CueSpec& Cue);

	// Taper radius r(s) [cm] at s [cm] behind the rim (the core's linear taper; s beyond the length keeps r_b).
	RAWBREAK_API double TaperRadiusCm(const rb::CueSpec& Cue, const rb::human::CueBodyState& Body, double SCm);

	// Linear default albedo (RGB) + roughness hint (A) of a section (ESTIMATE, plan 6.4: maple shaft, ivory ferrule, chalked
	// leather, dark lacquered forearm / sleeve, black linen wrap, rubber bumper).
	RAWBREAK_API FVector4f SectionColor(ERbCueSection Section);

	RAWBREAK_API const TCHAR* SectionName(ERbCueSection Section);
}
