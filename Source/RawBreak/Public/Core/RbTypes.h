#pragma once

// Blueprint/config-visible mirrors of core enums and UE-side enums shared by several packages
// (Docs/ue-architecture.md 3). Owner: UE-0 (frozen: additions allowed, no renames).

#include "CoreMinimal.h"

#include "rb/Core/Ids.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"

#include "RbTypes.generated.h"

// rb::TablePreset (equipment 11.2).
UENUM(BlueprintType)
enum class ERbTablePreset : uint8
{
	NineFootPro,
	NineFootTight,
	EightFootPro,
	EightFootHome,
	SevenFootBar,
	SevenFoot78,
	SevenFootTrue,
};

// rb::BallSetPreset (equipment 6).
UENUM(BlueprintType)
enum class ERbBallSetPreset : uint8
{
	StandardPool,
	DiveBar,
	OldBarOversizedCue,
};

// rb::CuePreset (equipment 7).
UENUM(BlueprintType)
enum class ERbCuePreset : uint8
{
	Playing19oz,
	Break21oz,
	Jump9oz,
	House19oz,
};

// The disciplines the M1 UI offers (rb::rules::Discipline subset; 9-ball is the M1 default).
UENUM(BlueprintType)
enum class ERbDiscipline : uint8
{
	NineBall,
	EightBall,
	TenBall,
	StraightPool,
};

UENUM(BlueprintType)
enum class ERbMatchMode : uint8
{
	Practice, // one human shoots every turn; the rules still run (fouls shown, ball in hand, rack over -> new rack)
	HotSeat,  // two humans alternate at one PC (local, same pawn; hot-seat guests at 50 in every attribute, HF Q4)
};

// Camera presets of ue5-realism-plan 4.1 (Eyes = default, decisions 2026-09-25).
UENUM(BlueprintType)
enum class ERbCameraPreset : uint8
{
	Eyes,
	Headcam,
	Broadcast,
};

// Graphics presets of ue5-realism-plan 9.4 (quality first, decisions 2026-09-27). Custom = individual options.
UENUM(BlueprintType)
enum class ERbQualityPreset : uint8
{
	Low,
	Medium,
	High,
	Epic,
	Cinematic,
	Custom,
};

// Render parts of the procedural table (one mesh / material slot each; ue-architecture 5.3).
UENUM(BlueprintType)
enum class ERbTablePart : uint8
{
	Bed,          // slate + cloth top with the pocket cuts (cloth material)
	CushionCloth, // cloth-covered cushion rubber from the nose outline and CushionProfile (cloth material)
	RailCaps,     // flat rail caps incl. pocket surrounds (wood / laminate)
	Apron,        // outer rail faces and the table body skirt
	PocketLiners, // hole walls / leather pockets from PocketGeometry (liner material)
	Sights,       // 18 diamonds / dots on the rail caps (sight material)
	Legs,         // legs / base (M1: simple blocks)
	Count UMETA(Hidden)
};

// --- M2 additions (Docs/ue-architecture.md 18; architect) ------------------------------------------------------

namespace rb::human { enum class VenueKind : std::uint8_t; }

// Playable venues (title screen venue select, M2). Each venue is one generated level (RbTypes::MapFor).
UENUM(BlueprintType)
enum class ERbVenue : uint8
{
	TestRoom, // /Game/Generated/Maps/L_M1_TestRoom (9-ft pro table, WPA lamp)
	DiveBar,  // /Game/Generated/Maps/L_DiveBar (The Low Bridge Tavern, 7-ft coin-op bar table, Docs/specs/venue-dive-bar.md)
};

// rb::human::VenueKind (human-factors 4.5.5): the kind of venue a table stands in (slope range, ball cling).
UENUM(BlueprintType)
enum class ERbVenueKind : uint8
{
	DiveBar,
	PoolHall,
	Arena,
};

namespace RbTypes
{
	RAWBREAK_API rb::TablePreset ToCore(ERbTablePreset Preset);
	RAWBREAK_API rb::BallSetPreset ToCore(ERbBallSetPreset Preset);
	RAWBREAK_API rb::CuePreset ToCore(ERbCuePreset Preset);
	RAWBREAK_API rb::rules::Discipline ToCore(ERbDiscipline Discipline);
	// WPA rules preset of a discipline (rules.md 12.1).
	RAWBREAK_API rb::rules::RulesPreset RulesPresetFor(ERbDiscipline Discipline);
	RAWBREAK_API const TCHAR* ToString(ERbTablePart Part);

	// --- M2 additions ---
	RAWBREAK_API rb::human::VenueKind ToCore(ERbVenueKind Kind);
	// Level of a venue (RbAssetPaths).
	RAWBREAK_API const TCHAR* MapFor(ERbVenue Venue);
}
