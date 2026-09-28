#pragma once

// Immutable per-table data shared by everything that works with one physical table (Docs/ue-architecture.md
// 5.1): spec, exact geometry, physics parameters, ball specs and the rules' table view - all built ONCE from
// the single source of truth (rb::TableSpec presets, architecture.md 13 items 2-3, 8). Shared read-only
// (TSharedPtr<const>) between the game thread (meshes, rules, cue clearance) and simulation worker threads.
// Owner: UE-6a.

#include "CoreMinimal.h"

#include "Core/RbTypes.h"

#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/RulesTypes.h"

struct FRbTableSetup
{
	ERbTablePreset Table = ERbTablePreset::NineFootPro;
	ERbBallSetPreset BallSet = ERbBallSetPreset::StandardPool;
	uint64 BallSetSeed = 0;          // BuildBallSet seed (venue ball sets: rb::human::VenueBallSetSeed)
	rb::TableCondition Condition;    // level, clean balls in M1 (venues: rb::human::MakeVenueTableCondition)
	double LampUndersideZ = rb::kInfinity; // [m] above the cloth; the lamp footprint defaults to the whole table
};

struct RAWBREAK_API FRbTableContext
{
	FRbTableSetup Setup;
	rb::TableSpec Spec;
	rb::TableGeometry Geometry;         // BuildTableGeometry(Spec)
	rb::PhysicsParams Physics;          // MakePhysicsParams(Spec, Setup.Condition) - never hand-built (architecture 13.3)
	rb::EnvironmentSpec Environment;    // lamp (off-table apex check)
	rb::BallSet Balls;                  // BuildBallSet(Setup.BallSet, Setup.BallSetSeed)
	rb::rules::RulesTable RulesTable;   // BuildRulesTable(Geometry, object-ball radius, per-ball radii)

	// Builds and validates everything; nullptr with OutError on any core error (invalid table, ball set).
	static TSharedPtr<const FRbTableContext> Create(const FRbTableSetup& Setup, FString& OutError);

	// Radius [m] of ball Id (0 = cue ball); the standard radius for ids beyond the set.
	double BallRadius(int32 Id) const;

	// Bed height above the floor [m] (TableSpec::BedHeight): the table actor's cloth plane sits this high.
	double BedHeight() const { return Spec.BedHeight; }
};
