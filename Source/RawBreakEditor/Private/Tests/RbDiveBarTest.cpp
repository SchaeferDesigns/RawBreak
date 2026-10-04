// Functional tests of the dive-bar level L_DiveBar (M2-A; Docs/ue-architecture.md 18.8, venue-dive-bar 2.5, 2.6, 16):
//
// RawBreak.Functional.DiveBar.Walkability (editor world, DB-1): the generated level's collision as the pawn sees it - a capsule of
// the pawn's size (r 0.25 m, half height 0.88 m, profile Pawn) sweeps through the tight spots of 2.5 / 2.6 (the 1.04 m gap between
// the left rail and column C3, the 1.10 m aisle in front of the cue rack, the 1.02 m diagonal between the head-right corner and the
// jukebox, the 1.52 m foot end) and along the main walking routes (entrance -> table, bar walkway, corridor) without a hit, while
// sweeps INTO the table, the column, the jukebox and the cue rack are blocked (the gaps are real gaps). Every route point stands on
// the floor.
//
// RawBreak.Functional.DiveBar.CueSweeps (editor world, VDB-T3 in-engine): the engine's cue-sweep model of 2.5 (tapered cue r 6.5 ->
// 15.9 mm + 1 mm margin, tip at the contact point on the oversized cue ball, elevation max(4 deg, rail-bridge term), a sphere chain
// every 2 cm out to the cue length + backswing) run against the level's collision on the RbCueSweep channel (the table ignored). At
// the 12 scripted positions (4 at TS-1 wall, 2 at TS-2 cue rack, 2 at TS-3 column C3, 1 at TS-4 jukebox, 1 at TS-6 foot end, 2 open
// controls) the in-engine threshold is measured by bisection and must equal 2.5's within +-2 cm (cue_sweep_check.py reproduces
// the same thresholds from layout.json within 1 mm); the 52 / 48-in offers exist where 2.5 predicts them.
//
// RawBreak.Functional.DiveBar.Validator (editor world): ARbVenueInfo::ValidateVenueLevel passes on the saved level.
//
// RawBreak.Functional.DiveBarRack (PIE on L_DiveBar, DB-1 walkability with the real pawn + the M2 "playable" check): the pawn walks
// the tight spots with its CharacterMovement (the 1.04 m, 1.10 m and 1.02 m gaps and the foot end); then a complete 9-ball practice
// rack through the cheats (like M1Rack) on the placed 7-ft bar table with the OLD-BAR ball set (oversized 60.325 mm cue ball in the
// director's table context and on screen): break, planned pots, a deliberate SCRATCH (cue ball pocketed) -> foul, ball in hand
// anywhere, an illegal placement refused, a straight-in placement, ... the 9 legally; a replay of the winning shot returns to the
// live table; then a hot-seat match: break and a shot that pockets nothing -> the turn changes to the other player.
// Helpers live in namespace RbDiveBarTest (unity builds, M2-0 rule). Owner: M2-A.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

#include "Balls/RbBallSet.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbAssetPaths.h"
#include "Dev/RbCheatManager.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Game/RbTableSubsystem.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbShot.h"
#include "Simulation/RbSimulationSubsystem.h"
#include "Table/RbTable.h"
#include "UI/RbOverlayComponent.h"
#include "Venue/RbVenueInfo.h"
#include "Venue/RbVenueJson.h"

#include "rb/Physics/ShotResult.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbDiveBarTest
{
	constexpr double kCueBallRadius = 0.0301625;  // OldBarOversizedCue: 60.325 mm (decisions.md, venue-dive-bar 3.2)
	constexpr double kObjectBallRadius = 0.028575; // 57.15 mm
	constexpr int32 kMaxShots = 45;
	constexpr int32 kMaxRacks = 3;
	constexpr int32 kLastObjectBall = 9;
	constexpr float kLiveRate = 4.0f;

	// --- the level ----------------------------------------------------------------------------------------------------------

	bool MapExists(FAutomationTestBase& Test)
	{
		if (!FPackageName::DoesPackageExist(RbAssetPaths::DiveBarMap))
		{
			Test.AddError(FString::Printf(TEXT("%s missing: run Tools/unreal/editor/rb_make_divebar.py"), RbAssetPaths::DiveBarMap));
			return false;
		}
		return true;
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	FVector V(double X, double Y, double Z)
	{
		return FVector(100.0 * X, 100.0 * Y, 100.0 * Z);
	}

	// The table numbers of layout.json (V frame, metres) the sweeps use.
	struct FTableFrame
	{
		double NoseX0 = 12.743, NoseX1 = 14.775, NoseY0 = 4.919, NoseY1 = 5.935;
		double BedZ = 0.743;
		double RailTopAboveBed = 0.048;

		bool Load(FString& OutError)
		{
			FRbJson Layout;
			if (!FRbJson::LoadProjectFile(TEXT("Art/DiveBar/layout.json"), Layout, &OutError))
			{
				return false;
			}
			const FRbJson& Table = Layout[TEXT("tables")][0];
			const FRbJson& Noses = Table[TEXT("noses")];
			if (!Table.IsObject() || Noses.Num() != 2)
			{
				OutError = TEXT("layout.json tables[0].noses missing");
				return false;
			}
			NoseX0 = Noses[0][0].AsNumber();
			NoseX1 = Noses[0][1].AsNumber();
			NoseY0 = Noses[1][0].AsNumber();
			NoseY1 = Noses[1][1].AsNumber();
			BedZ = Table.GetNumber(TEXT("bed_height_m"), BedZ);
			RailTopAboveBed = Table.GetNumber(TEXT("rail_top_z_m"), BedZ + RailTopAboveBed) - BedZ;
			return true;
		}
	};

	// --- walkability ---------------------------------------------------------------------------------------------------------

	constexpr float kCapsuleRadiusCm = 25.0f;     // ARbPlayerCharacter (venue-dive-bar 2.5: r 0.25 m)
	constexpr float kCapsuleHalfHeightCm = 88.0f;
	constexpr double kFloorClearanceCm = 2.0;     // the capsule's bottom above the floor during the sweeps

	struct FRoute
	{
		const TCHAR* Name;
		TArray<FVector2D> Points; // V frame [m]
	};

	TArray<FRoute> Routes()
	{
		return {
			{TEXT("entrance -> bar walkway (between the stools and the columns)"), {{1.00, 5.90}, {2.20, 3.20}, {10.00, 3.20}}},
			{TEXT("bar walkway -> head end of the table"), {{10.00, 3.20}, {12.20, 4.30}}},
			{TEXT("column C3 gap (1.04 m, TS-3 / P5)"), {{12.20, 4.235}, {15.30, 4.235}}},
			{TEXT("foot end (1.52 m, TS-6)"), {{15.30, 4.235}, {15.70, 5.43}, {15.30, 6.65}}},
			{TEXT("wall aisle at the cue rack (1.10 m, TS-1 / TS-2)"), {{15.30, 6.65}, {12.35, 6.65}}},
			{TEXT("head-right corner / jukebox diagonal (1.02 m, TS-4)"), {{12.35, 6.65}, {12.14, 6.36}, {11.95, 5.75}}},
			{TEXT("head end"), {{11.95, 5.75}, {12.20, 4.30}}},
			{TEXT("corridor opening -> back exit (E18 / E21)"), {{15.80, 0.85}, {19.85, 0.85}}},
		};
	}

	struct FBlockedProbe
	{
		const TCHAR* Name;
		FVector2D From;
		FVector2D To;
	};

	// Sweeps that must hit: the gaps above are bounded by real obstacles.
	TArray<FBlockedProbe> BlockedProbes()
	{
		return {
			{TEXT("into the table's left rail from the column gap"), {13.20, 4.235}, {13.20, 5.20}},
			{TEXT("into column C3 from the column gap"), {13.72, 4.235}, {13.72, 3.30}},
			{TEXT("into the cue rack from the wall aisle"), {13.80, 6.65}, {13.80, 7.30}},
			{TEXT("into the table's right rail from the wall aisle"), {13.80, 6.65}, {13.80, 5.60}},
			{TEXT("into the jukebox from the diagonal"), {12.14, 6.36}, {11.30, 6.95}},
			{TEXT("into the foot end of the table"), {15.70, 5.43}, {14.60, 5.43}},
			{TEXT("into the back wall at the foot end"), {15.70, 5.43}, {16.80, 5.43}},
		};
	}

	FCollisionQueryParams QueryParams(const TCHAR* Tag)
	{
		FCollisionQueryParams Params(FName(Tag), false);
		return Params;
	}

	bool SweepCapsule(UWorld& World, const FVector2D& A, const FVector2D& B, FHitResult& OutHit)
	{
		const double Z = kCapsuleHalfHeightCm + kFloorClearanceCm;
		return World.SweepSingleByProfile(OutHit, FVector(100.0 * A.X, 100.0 * A.Y, Z), FVector(100.0 * B.X, 100.0 * B.Y, Z), FQuat::Identity,
			UCollisionProfile::Pawn_ProfileName, FCollisionShape::MakeCapsule(kCapsuleRadiusCm, kCapsuleHalfHeightCm), QueryParams(TEXT("RbDiveBarWalk")));
	}

	bool FloorAt(UWorld& World, const FVector2D& P, double& OutZ)
	{
		FHitResult Hit;
		const bool bHit = World.LineTraceSingleByChannel(Hit, FVector(100.0 * P.X, 100.0 * P.Y, 150.0), FVector(100.0 * P.X, 100.0 * P.Y, -50.0), ECC_Visibility,
			QueryParams(TEXT("RbDiveBarFloor")));
		OutZ = bHit ? Hit.ImpactPoint.Z : -1.0e9;
		return bHit;
	}

	FString HitName(const FHitResult& Hit)
	{
		const AActor* Actor = Hit.GetActor();
		return Actor ? FString::Printf(TEXT("%s (%s)"), *Actor->GetActorLabel(), *Hit.ImpactPoint.ToString()) : TEXT("?");
	}

	// --- cue sweeps (2.5, the engine's model) -------------------------------------------------------------------------------

	constexpr double kTipRadius = 0.0065;
	constexpr double kButtRadius = 0.0159;
	constexpr double kMargin = 0.001;
	constexpr double kBaseElevationDeg = 4.0;
	constexpr double kStep = 0.02;
	constexpr double kFull = 0.30;  // URbStrokeComponent::MaxBackswing
	constexpr double kShort = 0.10;
	constexpr double kCue58 = 1.4732, kCue52 = 1.3208, kCue48 = 1.2192;

	struct FCueSweeper
	{
		UWorld* World = nullptr;
		FTableFrame Table;
		FCollisionQueryParams Params = QueryParams(TEXT("RbDiveBarCueSweep"));
		mutable int32 Overlaps = 0;

		// Distance from P (inside the noses) backward along -D to the nose rectangle.
		double DistToNose(const FVector2D& P, const FVector2D& D) const
		{
			double Best = TNumericLimits<double>::Max();
			const double CompX = -D.X, CompY = -D.Y;
			if (CompX > 1e-9) { Best = FMath::Min(Best, (Table.NoseX1 - P.X) / CompX); }
			if (CompX < -1e-9) { Best = FMath::Min(Best, (Table.NoseX0 - P.X) / CompX); }
			if (CompY > 1e-9) { Best = FMath::Min(Best, (Table.NoseY1 - P.Y) / CompY); }
			if (CompY < -1e-9) { Best = FMath::Min(Best, (Table.NoseY0 - P.Y) / CompY); }
			return Best;
		}

		// True when the swept cue (length L, backswing B) of a shot at the ball B (V, m) in the unit direction D hits the venue.
		bool Blocked(const FVector2D& Ball, const FVector2D& D, double Length, double Backswing) const
		{
			const FVector2D Contact = Ball - D * kCueBallRadius;
			const double Run = FMath::Max(0.12, DistToNose(Contact, D) + 0.08);
			const double Rise = Table.RailTopAboveBed + 0.020 - (kCueBallRadius + 0.015);
			const double Rail = FMath::Atan2(Rise, Run);
			const bool bRail = Rail > FMath::DegreesToRadians(kBaseElevationDeg);
			const double Elevation = bRail ? Rail : FMath::DegreesToRadians(kBaseElevationDeg);
			const double ContactZ = bRail ? kCueBallRadius + 0.015 : kCueBallRadius;
			const double Max = Length + Backswing;
			const int32 N = FMath::CeilToInt(Max / kStep);
			for (int32 I = 0; I <= N; ++I)
			{
				const double S = Max * I / N;
				const double R = kTipRadius + (kButtRadius - kTipRadius) * FMath::Min(S, Length) / Length + kMargin;
				const FVector2D P = Contact - D * (FMath::Cos(Elevation) * S);
				const double Z = Table.BedZ + ContactZ + FMath::Sin(Elevation) * S;
				++Overlaps;
				if (World->OverlapBlockingTestByChannel(V(P.X, P.Y, Z), FQuat::Identity, RbAssetPaths::Collision::CueSweepChannel,
					FCollisionShape::MakeSphere(static_cast<float>(100.0 * R)), Params))
				{
					return true;
				}
			}
			return false;
		}

		// Perpendicular shot away from an obstacle: the smallest ball-centre distance from the nose with a clear sweep.
		double Threshold(const FVector2D& NosePoint, const FVector2D& Inward, double Length, double Backswing) const
		{
			double Lo = kCueBallRadius, Hi = 0.9;
			if (!Blocked(NosePoint + Inward * Lo, Inward, Length, Backswing))
			{
				return Lo;
			}
			for (int32 K = 0; K < 22; ++K)
			{
				const double Mid = 0.5 * (Lo + Hi);
				(Blocked(NosePoint + Inward * Mid, Inward, Length, Backswing) ? Lo : Hi) = Mid;
			}
			return Hi;
		}
	};

	// --- the rack (planning like M1Rack) -------------------------------------------------------------------------------------

	UWorld* GameWorld()
	{
		return PlayWorld();
	}

	ARbGameMode* GameMode()
	{
		UWorld* World = PlayWorld();
		return World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
	}

	struct FStrike
	{
		float Speed = 0.0f;
		float PhiDeg = 0.0f;
		float B = 0.0f;
	};

	struct FPlan
	{
		bool bValid = false;
		FStrike Strike;
		int32 Target = -1;
		int32 Pocket = -1;
		int32 Width = 0;
		double CutDeg = 0.0;
	};

	TSharedRef<FRbShot> Simulate(const URbMatchDirector& Director, const FRbTableState& Table, const FStrike& S)
	{
		FRbShotRequest Request;
		Request.Table = Director.GetTableContext();
		RbShot::InitSimInput(*Request.Table, Request.Input);
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			Request.Input.Balls[Id] = Table.Balls[Id];
		}
		Request.Input.Context.FrozenTolerance = Director.GetMatchConfig().Rules.Tolerances.Frozen;
		rb::StrikeRequest Strike;
		Strike.Ball = static_cast<rb::BallId>(rb::kCueBallId);
		Strike.Input.Speed = S.Speed;
		Strike.Input.Azimuth = FMath::DegreesToRadians(S.PhiDeg);
		Strike.Input.Elevation = FMath::DegreesToRadians(0.0f);
		Strike.Input.OffsetA = 0.0f;
		Strike.Input.OffsetB = S.B;
		Strike.Input.Cue = Director.GetShooter(Director.GetActivePlayer()).Cue;
		Request.Input.Strikes.PushBack(Strike);
		return URbSimulationSubsystem::RunShotBlocking(MoveTemp(Request));
	}

	int32 FirstCueBallContact(const rb::ShotResult& Result)
	{
		for (const rb::ShotEvent& Event : Result.Events)
		{
			if (Event.Type == rb::ShotEventType::BallBall && (Event.A == rb::kCueBallId || Event.B == rb::kCueBallId))
			{
				return Event.A == rb::kCueBallId ? Event.B : Event.A;
			}
		}
		return -1;
	}

	bool HasTipRecontact(const rb::ShotResult& Result)
	{
		for (const rb::ShotEvent& Event : Result.Events)
		{
			if (Event.Type == rb::ShotEventType::TipRecontact)
			{
				return true;
			}
		}
		return false;
	}

	// Some ball leaves the table (engine physics would take over; the rack stays on the table).
	bool LeavesTable(const rb::ShotResult& Result)
	{
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			if (Result.Finals[Id].Status == rb::BallFinalStatus::OffTable)
			{
				return true;
			}
		}
		return false;
	}

	int32 CountPocketed(const FRbTableState& Table, const rb::ShotResult& Result)
	{
		int32 Count = 0;
		for (int32 Id = 1; Id < rb::kMaxBalls; ++Id)
		{
			Count += Table.Balls[Id].InPlay && Result.Finals[Id].Status == rb::BallFinalStatus::Pocketed ? 1 : 0;
		}
		return Count;
	}

	int32 LowestBall(const FRbTableState& Table)
	{
		for (int32 Id = 1; Id < rb::kMaxBalls; ++Id)
		{
			if (Table.Balls[Id].InPlay)
			{
				return Id;
			}
		}
		return -1;
	}

	bool Pots(const rb::ShotResult& Result, int32 Target)
	{
		return Result.Status == rb::SimStatus::Ok && FirstCueBallContact(Result) == Target && Result.Finals[Target].Status == rb::BallFinalStatus::Pocketed &&
			Result.Finals[rb::kCueBallId].Status == rb::BallFinalStatus::OnTable && !HasTipRecontact(Result) && !LeavesTable(Result);
	}

	double SegmentDistance(const rb::Vec2& P, const rb::Vec2& A, const rb::Vec2& B)
	{
		const rb::Vec2 AB = B - A;
		const double L2 = rb::LengthSquared(AB);
		const double T = L2 > 0.0 ? FMath::Clamp(rb::Dot(P - A, AB) / L2, 0.0, 1.0) : 0.0;
		return rb::Length(P - (A + AB * T));
	}

	bool PathClear(const FRbTableState& Table, const rb::Vec2& A, const rb::Vec2& B, double Clearance, int32 IgnoreA, int32 IgnoreB)
	{
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			if (Id != IgnoreA && Id != IgnoreB && Table.Balls[Id].InPlay && SegmentDistance(rb::XY(Table.Balls[Id].State.Position), A, B) < Clearance)
			{
				return false;
			}
		}
		return true;
	}

	// OutCentre (optional): the best plan at B = 0. RbStroke (M2-F's human stroke layer) strikes the centre of the cue ball, so only a
	// centre plan may be played through it: a draw plan played at the centre follows the object ball in with the oversized cue ball
	// (integration round: the four ball-in-hand straight-ins of the merged tree all scratched).
	FPlan PlanPot(const URbMatchDirector& Director, const FRbTableState& Table, int32 OnlyPocket = -1, bool bQuick = false, FPlan* OutCentre = nullptr)
	{
		FPlan Best;
		const int32 Target = LowestBall(Table);
		if (Target < 0 || !Table.Balls[rb::kCueBallId].InPlay)
		{
			return Best;
		}
		const FRbTableContext& Context = *Director.GetTableContext();
		const double R = Context.BallRadius(Target);
		const double RCue = Context.BallRadius(rb::kCueBallId);
		const rb::Vec2 Cue = rb::XY(Table.Balls[rb::kCueBallId].State.Position);
		const rb::Vec2 Object = rb::XY(Table.Balls[Target].State.Position);
		const int32 Span = bQuick ? 15 : 30;
		const TArray<float> Speeds = bQuick ? TArray<float>{2.0f, 2.8f} : TArray<float>{1.8f, 2.6f, 3.4f};
		for (int32 P = 0; P < static_cast<int32>(Context.Geometry.Pockets.Size()); ++P)
		{
			if (OnlyPocket >= 0 && P != OnlyPocket)
			{
				continue;
			}
			const rb::PocketGeometry& Pocket = Context.Geometry.Pockets[P];
			const rb::Vec2 U = rb::Normalized(Pocket.MouthMid - Object);
			const rb::Vec2 Ghost = Object - U * (R + RCue); // the oversized cue ball: ghost at r_object + r_cue
			const rb::Vec2 ToGhost = Ghost - Cue;
			if (rb::Length(ToGhost) < 1.0e-3)
			{
				continue;
			}
			const double CutDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(rb::Dot(rb::Normalized(ToGhost), U), -1.0, 1.0)));
			if (CutDeg > 70.0 || !PathClear(Table, Object, Pocket.MouthMid, 2.0 * R, rb::kCueBallId, Target) ||
				!PathClear(Table, Cue, Ghost, R + RCue, rb::kCueBallId, Target))
			{
				continue;
			}
			const double GhostDeg = FMath::RadiansToDegrees(FMath::Atan2(ToGhost.y, ToGhost.x));
			for (const float Speed : Speeds)
			{
				for (const float B : {-0.3f, 0.0f})
				{
					int32 Run = 0, BestRun = 0, BestEnd = 0;
					for (int32 K = -Span; K <= Span; ++K)
					{
						const FStrike S{Speed, static_cast<float>(GhostDeg + 0.1 * K), B};
						if (Pots(Simulate(Director, Table, S)->Result, Target))
						{
							if (++Run > BestRun)
							{
								BestRun = Run;
								BestEnd = K;
							}
						}
						else
						{
							Run = 0;
						}
					}
					FPlan Candidate;
					Candidate.bValid = BestRun > 0;
					Candidate.Width = BestRun;
					Candidate.Strike = FStrike{Speed, static_cast<float>(GhostDeg + 0.1 * (BestEnd - (BestRun - 1) / 2)), B};
					Candidate.Target = Target;
					Candidate.Pocket = P;
					Candidate.CutDeg = CutDeg;
					if (BestRun > Best.Width)
					{
						Best = Candidate;
					}
					if (OutCentre && B == 0.0f && BestRun > OutCentre->Width)
					{
						*OutCentre = Candidate;
					}
				}
			}
		}
		return Best;
	}

	FPlan PlanAnyPot(const URbMatchDirector& Director, const FRbTableState& Table)
	{
		FPlan Plan;
		const int32 Target = LowestBall(Table);
		for (const float Speed : {2.5f, 4.0f})
		{
			for (int32 Deg = 0; Deg < 360; ++Deg)
			{
				const FStrike S{Speed, static_cast<float>(Deg), -0.2f};
				const TSharedRef<FRbShot> Shot = Simulate(Director, Table, S);
				const rb::ShotResult& Result = Shot->Result;
				if (Result.Status != rb::SimStatus::Ok || FirstCueBallContact(Result) != Target ||
					Result.Finals[rb::kCueBallId].Status != rb::BallFinalStatus::OnTable || HasTipRecontact(Result) || LeavesTable(Result))
				{
					continue;
				}
				for (int32 Id = 1; Id < rb::kMaxBalls; ++Id)
				{
					if (Table.Balls[Id].InPlay && Result.Finals[Id].Status == rb::BallFinalStatus::Pocketed)
					{
						Plan.bValid = true;
						Plan.Strike = S;
						Plan.Target = Id; // the ball this strike drops (a legal combination / carom in 9-ball)
						Plan.Width = 1;
						return Plan;
					}
				}
			}
		}
		return Plan;
	}

	// A deliberate scratch: the cue ball rolls into a pocket (the oversized cue ball drops in the 7-ft's pockets too).
	bool PlanScratch(const URbMatchDirector& Director, const FRbTableState& Table, FStrike& Out)
	{
		const FRbTableContext& Context = *Director.GetTableContext();
		const double RCue = Context.BallRadius(rb::kCueBallId);
		const rb::Vec2 Cue = rb::XY(Table.Balls[rb::kCueBallId].State.Position);
		auto Accept = [&](const FStrike& S)
		{
			const TSharedRef<FRbShot> Shot = Simulate(Director, Table, S);
			const rb::ShotResult& Result = Shot->Result;
			return Result.Status == rb::SimStatus::Ok && Result.Finals[rb::kCueBallId].Status == rb::BallFinalStatus::Pocketed && !HasTipRecontact(Result) &&
				!LeavesTable(Result) && Result.Finals[kLastObjectBall].Status != rb::BallFinalStatus::Pocketed;
		};
		for (int32 P = 0; P < static_cast<int32>(Context.Geometry.Pockets.Size()); ++P)
		{
			const rb::Vec2 Mouth = Context.Geometry.Pockets[P].MouthMid;
			if (!PathClear(Table, Cue, Mouth, 2.0 * RCue + 0.005, rb::kCueBallId, -1))
			{
				continue;
			}
			const double Phi = FMath::RadiansToDegrees(FMath::Atan2(Mouth.y - Cue.y, Mouth.x - Cue.x));
			for (const float Speed : {1.4f, 1.9f, 2.5f})
			{
				for (const double Offset : {0.0, 0.3, -0.3, 0.6, -0.6})
				{
					const FStrike S{Speed, static_cast<float>(Phi + Offset), 0.0f};
					if (Accept(S))
					{
						Out = S;
						return true;
					}
				}
			}
		}
		for (const float Speed : {2.0f, 3.0f})
		{
			for (int32 Tenth = 0; Tenth < 3600; Tenth += 5)
			{
				const FStrike S{Speed, 0.1f * Tenth, 0.0f};
				if (Accept(S))
				{
					Out = S;
					return true;
				}
			}
		}
		return false;
	}

	// A legal shot that pockets nothing (hot-seat turn change): the cue ball hits the lowest ball first, a ball reaches a cushion after
	// the contact (9-ball), nothing drops, nothing leaves the table.
	bool PlanMiss(const URbMatchDirector& Director, const FRbTableState& Table, FStrike& Out)
	{
		const int32 Target = LowestBall(Table);
		if (Target < 0)
		{
			return false;
		}
		const rb::Vec2 Cue = rb::XY(Table.Balls[rb::kCueBallId].State.Position);
		const rb::Vec2 Object = rb::XY(Table.Balls[Target].State.Position);
		const double Phi = FMath::RadiansToDegrees(FMath::Atan2(Object.y - Cue.y, Object.x - Cue.x));
		for (const float Speed : {1.6f, 2.2f, 1.2f, 3.0f})
		{
			for (const double Offset : {0.0, 1.5, -1.5, 3.0, -3.0, 5.0, -5.0})
			{
				const FStrike S{Speed, static_cast<float>(Phi + Offset), 0.0f};
				const TSharedRef<FRbShot> Shot = Simulate(Director, Table, S);
				const rb::ShotResult& Result = Shot->Result;
				if (Result.Status != rb::SimStatus::Ok || FirstCueBallContact(Result) != Target || CountPocketed(Table, Result) > 0 ||
					Result.Finals[rb::kCueBallId].Status != rb::BallFinalStatus::OnTable || HasTipRecontact(Result) || LeavesTable(Result))
				{
					continue;
				}
				bool bContact = false, bRailAfter = false;
				for (const rb::ShotEvent& Event : Result.Events)
				{
					bContact = bContact || (Event.Type == rb::ShotEventType::BallBall && (Event.A == rb::kCueBallId || Event.B == rb::kCueBallId));
					bRailAfter = bRailAfter || (bContact && Event.Type == rb::ShotEventType::BallCushion);
				}
				if (bRailAfter)
				{
					Out = S;
					return true;
				}
			}
		}
		return false;
	}

	bool PlanPlacement(const URbMatchDirector& Director, const FRbTableState& Table, rb::Vec2& OutPlace, FPlan& OutPlan)
	{
		const int32 Target = LowestBall(Table);
		if (Target < 0)
		{
			return false;
		}
		const FRbTableContext& Context = *Director.GetTableContext();
		const double R = Context.BallRadius(Target);
		const double RCue = Context.BallRadius(rb::kCueBallId);
		const rb::Vec2 Object = rb::XY(Table.Balls[Target].State.Position);
		TArray<int32> Pockets;
		for (int32 P = 0; P < static_cast<int32>(Context.Geometry.Pockets.Size()); ++P)
		{
			Pockets.Add(P);
		}
		Pockets.Sort([&](int32 A, int32 B) {
			return rb::Length(Context.Geometry.Pockets[A].MouthMid - Object) < rb::Length(Context.Geometry.Pockets[B].MouthMid - Object);
		});
		for (const int32 P : Pockets)
		{
			const rb::Vec2 Mouth = Context.Geometry.Pockets[P].MouthMid;
			if (!PathClear(Table, Object, Mouth, 2.0 * R, rb::kCueBallId, Target))
			{
				continue;
			}
			const rb::Vec2 U = rb::Normalized(Mouth - Object);
			for (const double Gap : {0.12, 0.20, 0.30, 0.45})
			{
				const rb::Vec2 Exact = Object - U * (R + RCue + Gap);
				const rb::Vec2 Place(static_cast<float>(Exact.x), static_cast<float>(Exact.y));
				if (!Director.CanPlaceCueBall(Place) || !PathClear(Table, Place, Object, R + RCue, rb::kCueBallId, Target))
				{
					continue;
				}
				FRbTableState Hypothesis = Table;
				rb::SimBall& CueBall = Hypothesis.Balls[rb::kCueBallId];
				CueBall.InPlay = true;
				CueBall.State = rb::BallState();
				CueBall.State.Position = rb::Vec3(Place.x, Place.y, CueBall.Spec.Radius);
				CueBall.State.State = rb::MotionState::Stationary;
				const FPlan Plan = PlanPot(Director, Hypothesis, P, true);
				if (Plan.bValid && Plan.Width >= 3)
				{
					OutPlace = Place;
					OutPlan = Plan;
					return true;
				}
			}
		}
		return false;
	}

	FString StrikeCommand(const FStrike& S)
	{
		return FString::Printf(TEXT("RbStrike %.9g %.9g 0 0 %.9g"), S.Speed, S.PhiDeg, S.B);
	}

	FString Pocketed(const FRbLastShotSummary& Last)
	{
		FString Out;
		for (const int32 Ball : Last.Pocketed)
		{
			Out += FString::Printf(TEXT("%s%d"), Out.IsEmpty() ? TEXT("") : TEXT(","), Ball);
		}
		return Out.IsEmpty() ? TEXT("-") : Out;
	}
}

// ==================================================================================================================================
// RawBreak.Functional.DiveBar.Walkability / CueSweeps (editor world)
// ==================================================================================================================================

class FRbDiveBarWalkabilityCommand : public IAutomationLatentCommand
{
public:
	explicit FRbDiveBarWalkabilityCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace RbDiveBarTest;
		UWorld* World = EditorWorld();
		if (!Test->TestNotNull(TEXT("editor world"), World))
		{
			return true;
		}
		// Tables through URbTableSubsystem (18.6.2 rule 1, RawBreak.Unit.MultiTable.NoSingleTableLookups; integration round).
		const URbTableSubsystem* TableSubsystem = URbTableSubsystem::Get(World);
		const int32 Tables = TableSubsystem ? TableSubsystem->GetTables().Num() : 0;
		Test->TestEqual(TEXT("L_DiveBar holds its one table"), Tables, 1);
		int32 Segments = 0;
		for (const FRoute& Route : Routes())
		{
			for (int32 I = 0; I + 1 < Route.Points.Num(); ++I)
			{
				FHitResult Hit;
				const bool bHit = SweepCapsule(*World, Route.Points[I], Route.Points[I + 1], Hit);
				++Segments;
				Test->TestFalse(*FString::Printf(TEXT("walk %s: segment %d (%.2f, %.2f) -> (%.2f, %.2f) is clear for the pawn capsule%s"), Route.Name, I,
					Route.Points[I].X, Route.Points[I].Y, Route.Points[I + 1].X, Route.Points[I + 1].Y, bHit ? *(TEXT(": hit ") + HitName(Hit)) : TEXT("")), bHit);
			}
			for (const FVector2D& P : Route.Points)
			{
				double FloorZ = 0.0;
				const bool bFloor = FloorAt(*World, P, FloorZ);
				Test->TestTrue(*FString::Printf(TEXT("walk %s: floor under (%.2f, %.2f) at z %.1f cm"), Route.Name, P.X, P.Y, FloorZ), bFloor && FMath::Abs(FloorZ) < 2.0);
			}
		}
		for (const FBlockedProbe& Probe : BlockedProbes())
		{
			FHitResult Hit;
			const bool bHit = SweepCapsule(*World, Probe.From, Probe.To, Hit);
			Test->TestTrue(*FString::Printf(TEXT("blocked: %s%s"), Probe.Name, bHit ? *(TEXT(" (hit ") + HitName(Hit) + TEXT(")")) : TEXT("")), bHit);
		}
		Test->AddInfo(FString::Printf(TEXT("walkability: %d route segments swept with the pawn capsule (r %.0f cm, half height %.0f cm), %d blocked probes"),
			Segments, kCapsuleRadiusCm, kCapsuleHalfHeightCm, BlockedProbes().Num()));
		return true;
	}

private:
	FAutomationTestBase* Test;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbDiveBarWalkabilityTest, "RawBreak.Functional.DiveBar.Walkability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbDiveBarWalkabilityTest::RunTest(const FString& Parameters)
{
	if (!RbDiveBarTest::MapExists(*this))
	{
		return false;
	}
	AutomationOpenMap(RbAssetPaths::DiveBarMap);
	ADD_LATENT_AUTOMATION_COMMAND(FRbDiveBarWalkabilityCommand(this));
	return true;
}

class FRbDiveBarCueSweepCommand : public IAutomationLatentCommand
{
public:
	explicit FRbDiveBarCueSweepCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace RbDiveBarTest;
		UWorld* World = EditorWorld();
		if (!Test->TestNotNull(TEXT("editor world"), World))
		{
			return true;
		}
		FCueSweeper Sweeper;
		Sweeper.World = World;
		FString Error;
		if (!Test->TestTrue(*FString::Printf(TEXT("layout.json table frame %s"), *Error), Sweeper.Table.Load(Error)))
		{
			return true;
		}
		if (const URbTableSubsystem* TableSubsystem = URbTableSubsystem::Get(World))
		{
			for (ARbTable* Table : TableSubsystem->GetTables())
			{
				Sweeper.Params.AddIgnoredActor(Table); // the cue passes over the rail: the core's elevation handles the table
			}
		}
		const FTableFrame& T = Sweeper.Table;
		const double CentreY = 0.5 * (T.NoseY0 + T.NoseY1);
		const double CentreX = 0.5 * (T.NoseX0 + T.NoseX1);
		// 2.5: perpendicular thresholds (ball-centre distance from the nose, +-2 cm) at TS-1 (wall, V X 14.60), TS-2 (cue rack, X 13.80),
		// TS-3 (column C3, X 13.72); the ball sits inside the nose, the cue points at the obstacle.
		struct FCase
		{
			const TCHAR* Name;
			FVector2D Nose;
			FVector2D Inward;
			double Length;
			double Backswing;
			double Spec;
		};
		const FVector2D WallSide(14.60, T.NoseY1), RackSide(13.80, T.NoseY1), ColumnSide(13.72, T.NoseY0);
		const FVector2D AwayFromWall(0.0, -1.0), AwayFromColumn(0.0, 1.0);
		const TArray<FCase> Cases = {
			{TEXT("TS-1 wall, 58 in, full 0.30 m backswing"), WallSide, AwayFromWall, kCue58, kFull, 0.432},
			{TEXT("TS-1 wall, 58 in, shortened 0.10 m backswing"), WallSide, AwayFromWall, kCue58, kShort, 0.231},
			{TEXT("TS-1 wall, 52 in offer, 0.10 m backswing"), WallSide, AwayFromWall, kCue52, kShort, 0.059},
			{TEXT("TS-1 wall, 48 in offer, 0.10 m backswing"), WallSide, AwayFromWall, kCue48, kShort, 0.030},
			{TEXT("TS-2 cue rack, 58 in, full backswing"), RackSide, AwayFromWall, kCue58, kFull, 0.552},
			{TEXT("TS-2 cue rack, 58 in, shortened backswing"), RackSide, AwayFromWall, kCue58, kShort, 0.352},
			{TEXT("TS-3 column C3, 58 in, full backswing"), ColumnSide, AwayFromColumn, kCue58, kFull, 0.615},
			{TEXT("TS-3 column C3, 58 in, shortened backswing"), ColumnSide, AwayFromColumn, kCue58, kShort, 0.415},
		};
		FString Report = TEXT("VDB-T3 in-engine cue sweeps (RbCueSweep channel, L_DiveBar collision) vs venue-dive-bar 2.5:");
		for (const FCase& Case : Cases)
		{
			const double Measured = Sweeper.Threshold(Case.Nose, Case.Inward, Case.Length, Case.Backswing);
			const bool bOk = FMath::Abs(Measured - Case.Spec) <= 0.02;
			Report += FString::Printf(TEXT("\n  %-48s %.3f m (2.5: %.3f) %s"), Case.Name, Measured, Case.Spec, bOk ? TEXT("ok") : TEXT("OUT"));
			Test->TestTrue(*FString::Printf(TEXT("%s: blocked below %.3f m from the nose (2.5: %.3f +- 0.02)"), Case.Name, Measured, Case.Spec), bOk);
		}
		// The 52 / 48-in offers exist where 2.5 predicts them: between the 58-in shortened threshold and the 52-in one the 58-in does not
		// fit (even shortened) while the 52-in fits with 0.10 m; below the 52-in one the 48-in fits.
		{
			const FVector2D Ball52 = WallSide + AwayFromWall * 0.15, Ball48 = WallSide + AwayFromWall * 0.045;
			Test->TestTrue(TEXT("TS-1 at 0.15 m: the 58 in does not fit even shortened"), Sweeper.Blocked(Ball52, AwayFromWall, kCue58, kShort));
			Test->TestFalse(TEXT("TS-1 at 0.15 m: the 52-in short cue fits (0.10 m backswing)"), Sweeper.Blocked(Ball52, AwayFromWall, kCue52, kShort));
			Test->TestTrue(TEXT("TS-1 at 0.045 m: the 52 in does not fit"), Sweeper.Blocked(Ball48, AwayFromWall, kCue52, kShort));
			Test->TestFalse(TEXT("TS-1 at 0.045 m: the 48-in short cue fits"), Sweeper.Blocked(Ball48, AwayFromWall, kCue48, kShort));
		}
		// TS-4: the diagonal from the head-right nose corner toward the table centre, shot away from the jukebox (cue_sweep_check.py: 0.305 m
		// for the E10 box of 2.3). Integration round: once M2-B's jukebox stands there its hulls follow the real body (the body 2 cm, the
		// arched top 8 cm less deep than the box: db_jukebox.py), so the 58-in cue gets free earlier - 0.260 m measured in L_DiveBar.
		{
			bool bJukeboxProp = false;
			for (TActorIterator<AActor> It(World); It && !bJukeboxProp; ++It)
			{
				bJukeboxProp = It->ActorHasTag(TEXT("RbDB_E10")) && It->ActorHasTag(TEXT("RbDB_Prop"));
			}
			const double Expected = bJukeboxProp ? 0.260 : 0.305;
			const FVector2D Corner(T.NoseX0, T.NoseY1);
			const FVector2D U = (FVector2D(CentreX, CentreY) - Corner).GetSafeNormal();
			double Lo = kCueBallRadius * 1.5, Hi = 1.0;
			for (int32 K = 0; K < 22; ++K)
			{
				const double Mid = 0.5 * (Lo + Hi);
				(Sweeper.Blocked(Corner + U * Mid, U, kCue58, kFull) ? Lo : Hi) = Mid;
			}
			const bool bOk = FMath::Abs(Hi - Expected) <= 0.02;
			Report += FString::Printf(TEXT("\n  %-48s %.3f m (%s: %.3f) %s"), TEXT("TS-4 jukebox diagonal, 58 in, full"), Hi,
				bJukeboxProp ? TEXT("M2-B jukebox hulls") : TEXT("cue_sweep_check.py"), Expected, bOk ? TEXT("ok") : TEXT("OUT"));
			Test->TestTrue(*FString::Printf(TEXT("TS-4 jukebox diagonal: free from %.3f m along the diagonal (%.3f +- 0.02, %s)"), Hi, Expected,
				bJukeboxProp ? TEXT("M2-B's jukebox") : TEXT("the E10 greybox")), bOk);
		}
		// TS-6: a ball frozen to the foot cushion on the long string, shot toward the head: the longest clear backswing (2.5: 0.224 m).
		{
			const FVector2D Frozen(T.NoseX1 - kCueBallRadius, CentreY);
			const FVector2D Toward(-1.0, 0.0);
			double Lo = 0.0, Hi = kFull;
			if (!Sweeper.Blocked(Frozen, Toward, kCue58, Hi))
			{
				Lo = Hi;
			}
			else
			{
				for (int32 K = 0; K < 22; ++K)
				{
					const double Mid = 0.5 * (Lo + Hi);
					(Sweeper.Blocked(Frozen, Toward, kCue58, Mid) ? Hi : Lo) = Mid;
				}
			}
			const bool bOk = FMath::Abs(Lo - 0.224) <= 0.02;
			Report += FString::Printf(TEXT("\n  %-48s %.3f m (2.5: 0.224) %s"), TEXT("TS-6 foot end, frozen ball, free backswing"), Lo, bOk ? TEXT("ok") : TEXT("OUT"));
			Test->TestTrue(*FString::Printf(TEXT("TS-6 frozen at the foot cushion: free backswing %.3f m (2.5: 0.224 +- 0.02)"), Lo), bOk);
			Test->TestFalse(TEXT("TS-6 0.10 m off the foot cushion: the full 0.30 m backswing is free"),
				Sweeper.Blocked(Frozen + Toward * 0.10, Toward, kCue58, kFull));
		}
		// Open controls (nothing in the way of the 58 in with the full backswing): the table centre along the long axis (the cue toward the
		// bar), and across the table at V X 13.20 (the cue toward the left-wall ledge, outside column C3's shadow; at the centre the butt
		// would reach C3 itself - that is TS-3).
		struct FControl
		{
			FVector2D Ball;
			FVector2D Dir;
		};
		for (const FControl& Control : {FControl{FVector2D(CentreX, CentreY), FVector2D(1.0, 0.0)}, FControl{FVector2D(13.20, CentreY), FVector2D(0.0, 1.0)}})
		{
			const bool bBlocked = Sweeper.Blocked(Control.Ball, Control.Dir, kCue58, kFull);
			Report += FString::Printf(TEXT("\n  open control, ball (%.2f, %.2f), direction (%+.0f, %+.0f): %s"), Control.Ball.X, Control.Ball.Y, Control.Dir.X,
				Control.Dir.Y, bBlocked ? TEXT("BLOCKED") : TEXT("free"));
			Test->TestFalse(*FString::Printf(TEXT("open control at (%.2f, %.2f), direction (%.0f, %.0f): free"), Control.Ball.X, Control.Ball.Y, Control.Dir.X,
				Control.Dir.Y), bBlocked);
		}
		Report += FString::Printf(TEXT("\n  (%d sphere overlaps)"), Sweeper.Overlaps);
		Test->AddInfo(Report);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Report);
		return true;
	}

private:
	FAutomationTestBase* Test;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbDiveBarCueSweepTest, "RawBreak.Functional.DiveBar.CueSweeps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbDiveBarCueSweepTest::RunTest(const FString& Parameters)
{
	if (!RbDiveBarTest::MapExists(*this))
	{
		return false;
	}
	AutomationOpenMap(RbAssetPaths::DiveBarMap);
	ADD_LATENT_AUTOMATION_COMMAND(FRbDiveBarCueSweepCommand(this));
	return true;
}

// ==================================================================================================================================
// RawBreak.Functional.DiveBar.Validator (editor world): the saved L_DiveBar passes its own level validator (VDB-T1 analytic, T8, T10,
// T11, T12 parts, cameras, tables, sublevels) - the generator runs it after a build, this keeps a stale or hand-edited map from
// passing the suite (review M2-A).
// ==================================================================================================================================

class FRbDiveBarValidatorCommand : public IAutomationLatentCommand
{
public:
	explicit FRbDiveBarValidatorCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		UWorld* World = RbDiveBarTest::EditorWorld();
		if (!Test->TestNotNull(TEXT("editor world"), World))
		{
			return true;
		}
		bool bOk = false;
		const FString Report = ARbVenueInfo::ValidateVenueLevel(World, bOk);
		TArray<FString> Lines;
		Report.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("FAIL")))
			{
				Test->AddError(Line);
			}
		}
		Test->AddInfo(Report);
		Test->TestTrue(TEXT("ARbVenueInfo::ValidateVenueLevel on the saved L_DiveBar"), bOk);
		return true;
	}

private:
	FAutomationTestBase* Test;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbDiveBarValidatorTest, "RawBreak.Functional.DiveBar.Validator", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbDiveBarValidatorTest::RunTest(const FString& Parameters)
{
	if (!RbDiveBarTest::MapExists(*this))
	{
		return false;
	}
	AutomationOpenMap(RbAssetPaths::DiveBarMap);
	ADD_LATENT_AUTOMATION_COMMAND(FRbDiveBarValidatorCommand(this));
	return true;
}

// ==================================================================================================================================
// RawBreak.Functional.DiveBarRack (PIE)
// ==================================================================================================================================

class FRbDiveBarWaitForMatchCommand : public IAutomationLatentCommand
{
public:
	FRbDiveBarWaitForMatchCommand(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const ARbGameMode* Mode = RbDiveBarTest::GameMode();
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (Director && Director->GetPhase() != ERbDirectorPhase::Idle)
		{
			return true;
		}
		if (GetCurrentRunTime() > Timeout)
		{
			Test->AddError(TEXT("PIE on L_DiveBar did not start an ARbGameMode match"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Timeout;
};

class FRbDiveBarRackCommand : public IAutomationLatentCommand
{
public:
	explicit FRbDiveBarRackCommand(FAutomationTestBase* InTest, int32 InRestarts = 0) : Test(InTest), Restarts(InRestarts) {}

	virtual bool Update() override
	{
		const ARbGameMode* Current = RbDiveBarTest::GameMode();
		if (!Current || !Current->GetDirector() || Current->GetDirector()->GetPhase() == ERbDirectorPhase::Idle)
		{
			if (GetCurrentRunTime() > 600.0)
			{
				Test->AddError(TEXT("no PIE match"));
				return true;
			}
			return false;
		}
		if (!Resolve())
		{
			return true;
		}
		if (Stage != EStage::Setup && SetupWorld.Get() != RbDiveBarTest::PlayWorld())
		{
			if (++Restarts > 2)
			{
				Test->AddError(TEXT("the play world keeps changing"));
				return true;
			}
			Test->AddInfo(TEXT("new PIE world: setting the match up again"));
			*this = FRbDiveBarRackCommand(Test, Restarts);
			return false;
		}
		ObservePlayback();
		bool bDone = false;
		switch (Stage)
		{
		case EStage::Setup: bDone = StageSetup(); break;
		case EStage::Walk: bDone = StageWalk(); break;
		case EStage::Break: bDone = StageBreak(); break;
		case EStage::WaitShot: bDone = StageWaitShot(); break;
		case EStage::NextShot: bDone = StageNextShot(); break;
		case EStage::Replay: bDone = StageReplay(); break;
		case EStage::HotSeatSetup: bDone = StageHotSeatSetup(); break;
		case EStage::HotSeatNext: bDone = StageHotSeatNext(); break;
		case EStage::Finish: bDone = StageFinish(); break;
		}
		if (bDone)
		{
			return true;
		}
		if (Stage != LastStage)
		{
			LastStage = Stage;
			StageStart = FPlatformTime::Seconds();
		}
		else if (FPlatformTime::Seconds() - StageStart > (Stage == EStage::Walk ? 120.0 : 60.0))
		{
			Test->AddError(FString::Printf(TEXT("stage %d timed out (%s)"), static_cast<int32>(Stage), Cheats ? *Cheats->MakeStateLine() : TEXT("")));
			return true;
		}
		return false;
	}

private:
	enum class EStage : uint8
	{
		Setup,
		Walk,
		Break,
		WaitShot,
		NextShot,
		Replay,
		HotSeatSetup,
		HotSeatNext,
		Finish,
	};

	bool Resolve()
	{
		using namespace RbDiveBarTest;
		UWorld* World = PlayWorld();
		Mode = GameMode();
		Director = Mode ? Mode->GetDirector() : nullptr;
		PC = World ? Cast<ARbPlayerController>(World->GetFirstPlayerController()) : nullptr;
		Cheats = PC ? Cast<URbCheatManager>(PC->CheatManager) : nullptr;
		Table = Mode ? Mode->GetTable() : nullptr;
		Balls = Mode ? Mode->GetBallSet() : nullptr;
		Playback = Balls ? Balls->GetPlayback() : nullptr;
		Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
		Character = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
		return Test->TestNotNull(TEXT("ARbGameMode"), Mode) && Test->TestNotNull(TEXT("director"), Director) &&
			Test->TestNotNull(TEXT("URbCheatManager"), Cheats) && Test->TestNotNull(TEXT("table"), Table) && Test->TestNotNull(TEXT("playback"), Playback) &&
			Test->TestNotNull(TEXT("replay subsystem"), Replay) && Test->TestNotNull(TEXT("player character"), Character);
	}

	void Cmd(const FString& Command)
	{
		Test->AddInfo(FString::Printf(TEXT("> %s"), *Command));
		PC->ConsoleCommand(Command);
	}

	bool IsSettled() const
	{
		const ERbDirectorPhase Phase = Director->GetPhase();
		return Phase != ERbDirectorPhase::Simulating && Phase != ERbDirectorPhase::PlayingBack && !Cheats->IsStrokeInFlight() &&
			Cheats->GetQueuedCount() == 0 && !Replay->IsReplaying();
	}

	void ObservePlayback()
	{
		if (Director->GetPhase() == ERbDirectorPhase::PlayingBack && Playback->IsPlaying() && Playback->GetShot() == Director->GetPendingShot() &&
			Playback->GetShotTime() != LastPlaybackTime)
		{
			LastPlaybackTime = Playback->GetShotTime();
			++PlaybackFrames;
		}
	}

	// Every ball in play at its FRbTableState position (1e-3 cm), pocketed balls hidden.
	void CheckTableShown(const TCHAR* When)
	{
		const FRbTableState& State = Director->GetTableState();
		int32 Wrong = 0;
		double Worst = 0.0;
		for (int32 Id = 0; Id <= RbDiveBarTest::kLastObjectBall; ++Id)
		{
			const UStaticMeshComponent* Ball = Balls->GetBallComponent(Id);
			if (!Ball)
			{
				++Wrong;
				continue;
			}
			if (State.Balls[Id].InPlay)
			{
				const double Error = FVector::Dist(Ball->GetComponentLocation(), Table->CoreToWorld(State.Balls[Id].State.Position));
				Worst = FMath::Max(Worst, Error);
				Wrong += Error > 1.0e-3 ? 1 : 0;
			}
			else
			{
				Wrong += Balls->IsBallVisible(Id) ? 1 : 0;
			}
		}
		Test->TestEqual(*FString::Printf(TEXT("%s: the ball set shows the committed table state (worst %.2g cm)"), When, Worst), Wrong, 0);
	}

	void Shoot(const FString& Command, const TCHAR* Kind)
	{
		ShotIndexBefore = Director->GetMatchShotIndex();
		ActiveBefore = Director->GetActivePlayer();
		PlaybackFrames = 0;
		LastPlaybackTime = -1.0;
		ShotKind = Kind;
		ShotSent = FPlatformTime::Seconds();
		Cmd(Command);
		Stage = EStage::WaitShot;
	}

	// --- setup: the level, the table, the oversized cue ball ------------------------------------------------------------
	bool StageSetup()
	{
		using namespace RbDiveBarTest;
		SetupWorld = PlayWorld();
		// The placed table of layout.json (the game mode's player session uses it, no spawned table).
		Test->TestTrue(TEXT("the player's table is the level's table at (1375.9, 542.7, 0) cm, yaw 0"),
			Table->GetActorLocation().Equals(FVector(1375.9, 542.7, 0.0), 0.05) && FMath::IsNearlyZero(Table->GetActorRotation().Yaw, 0.01));
		Test->TestEqual(TEXT("table preset SevenFootBar"), Table->Preset, ERbTablePreset::SevenFootBar);
		Test->TestEqual(TEXT("ball set OldBarOversizedCue"), Table->BallSet, ERbBallSetPreset::OldBarOversizedCue);
		Test->TestTrue(TEXT("tagged RbPlayerTable"), Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable));
		const FRbTableContext& Context = *Director->GetTableContext();
		Test->TestTrue(*FString::Printf(TEXT("the director plays the oversized cue ball: radius %.7f m (60.325 mm)"), Context.BallRadius(rb::kCueBallId)),
			FMath::IsNearlyEqual(Context.BallRadius(rb::kCueBallId), kCueBallRadius, 1.0e-6));
		// object balls sampled per venue seed (3.2 / 3.1: 57.00 - 57.15 mm; the cue-ball gauge passes them)
		Test->TestTrue(*FString::Printf(TEXT("object balls 57.00 - 57.15 mm: radius %.7f m"), Context.BallRadius(1)),
			Context.BallRadius(1) >= 0.02850 - 1.0e-7 && Context.BallRadius(1) <= kObjectBallRadius + 1.0e-7);
		if (const UStaticMeshComponent* CueBall = Balls->GetBallComponent(rb::kCueBallId))
		{
			const double Shown = CueBall->Bounds.BoxExtent.GetMax();
			Test->TestTrue(*FString::Printf(TEXT("the cue ball on screen is the oversized one: half extent %.3f cm (3.016)"), Shown), FMath::IsNearlyEqual(Shown, 100.0 * kCueBallRadius, 0.05));
		}
		const ARbVenueInfo* Info = ARbVenueInfo::Find(SetupWorld.Get());
		Test->TestTrue(TEXT("ARbVenueInfo (DiveBar) in the level"), Info && Info->Venue == ERbVenue::DiveBar);
		Test->TestEqual(TEXT("lighting state Open"), Info ? Info->GetLightingState() : ERbLightingState::AfterHours, ERbLightingState::Open);
		// The walk first (the pawn stands at the head end), then the rack.
		WalkRoutes = {
			{{12.20, 4.235}, {15.30, 4.235}},                   // column C3 gap 1.04 m
			{{15.70, 5.43}, {15.30, 6.65}},                     // foot end 1.52 m
			{{12.35, 6.65}},                                    // wall aisle at the cue rack 1.10 m
			{{12.14, 6.36}, {11.95, 5.75}, {12.20, 5.43}},      // jukebox diagonal 1.02 m, back to the head end
		};
		WalkNames = {TEXT("column C3 gap (1.04 m)"), TEXT("foot end (1.52 m)"), TEXT("wall aisle at the cue rack (1.10 m)"), TEXT("jukebox diagonal (1.02 m)")};
		const FVector Start = RbDiveBarTest::V(12.20, 5.43, 0.0);
		Character->TeleportTo(FVector(Start.X, Start.Y, Character->GetActorLocation().Z), FRotator::ZeroRotator);
		WalkRoute = 0;
		WalkPoint = 0;
		WalkPointStart = FPlatformTime::Seconds();
		Stage = EStage::Walk;
		return false;
	}

	// --- DB-1: the pawn walks the tight spots with its own CharacterMovement ---------------------------------------------------
	bool StageWalk()
	{
		if (WalkRoute >= WalkRoutes.Num())
		{
			Test->AddInfo(FString::Printf(TEXT("walk: %d route(s) with %d waypoint(s) reached by the pawn (capsule r %.0f cm)"), WalkRoutes.Num(), WalkReached,
				Character->GetCapsuleComponent()->GetScaledCapsuleRadius()));
			const FVector Home = RbDiveBarTest::V(12.20, 5.43, 0.0);
			Character->TeleportTo(FVector(Home.X, Home.Y, Character->GetActorLocation().Z), FRotator::ZeroRotator);
			Character->GetCharacterMovement()->StopMovementImmediately();
			Stage = EStage::Break;
			return false;
		}
		const FVector2D Target2 = WalkRoutes[WalkRoute][WalkPoint];
		const FVector Here = Character->GetActorLocation();
		const FVector2D Delta(100.0 * Target2.X - Here.X, 100.0 * Target2.Y - Here.Y);
		if (Delta.Size() < 12.0)
		{
			++WalkReached;
			if (++WalkPoint >= WalkRoutes[WalkRoute].Num())
			{
				Test->AddInfo(FString::Printf(TEXT("walk: %s passed"), WalkNames[WalkRoute]));
				++WalkRoute;
				WalkPoint = 0;
			}
			WalkPointStart = FPlatformTime::Seconds();
			return false;
		}
		if (FPlatformTime::Seconds() - WalkPointStart > 25.0)
		{
			Test->AddError(FString::Printf(TEXT("walk: the pawn did not get through %s: stuck at (%.2f, %.2f) m, %.0f cm short of (%.2f, %.2f)"), WalkNames[WalkRoute],
				Here.X / 100.0, Here.Y / 100.0, Delta.Size(), Target2.X, Target2.Y));
			return true;
		}
		Character->AddMovementInput(FVector(Delta.X, Delta.Y, 0.0).GetSafeNormal(), 1.0f, true);
		return false;
	}

	// --- break ----------------------------------------------------------------------------------------------------------
	bool StageBreak()
	{
		if (!IsSettled())
		{
			return false;
		}
		if (!bRackSetUp)
		{
			FRbMatchSetup Setup = Mode->GetStartSetup();
			Setup.Mode = ERbMatchMode::Practice;
			Setup.Discipline = ERbDiscipline::NineBall;
			Setup.bLag = false;
			Setup.Seed = 1958;
			if (!Test->TestTrue(TEXT("9-ball practice match"), Director->StartMatch(Setup)))
			{
				return true;
			}
			Cmd(FString::Printf(TEXT("RbPlaybackRate %g"), RbDiveBarTest::kLiveRate));
			bRackSetUp = true;
			return false;
		}
		if (!Test->TestEqual(TEXT("break: ball in hand"), Director->GetPhase(), ERbDirectorPhase::AwaitPlacement))
		{
			return true;
		}
		++Racks;
		ShotsThisRack = 0;
		const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
		Cmd(FString::Printf(TEXT("RbPlaceCueBall %.9g 0.12"), static_cast<float>(Rules.HeadStringX - 0.10)));
		if (!Test->TestEqual(TEXT("break: cue ball placed behind the head string"), Director->GetPhase(), ERbDirectorPhase::AwaitStroke))
		{
			return true;
		}
		const rb::Vec3 Cue = Director->GetTableState().Balls[0].State.Position;
		const rb::Vec3 Apex = Director->GetTableState().Balls[1].State.Position;
		const float Phi = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Apex.y - Cue.y, Apex.x - Cue.x)));
		bFallbackStrike = true;
		FallbackStrike = RbDiveBarTest::FStrike{8.0f, Phi, 0.0f};
		Shoot(FString::Printf(TEXT("RbStroke 8 %.9g"), Phi), TEXT("break (RbStroke, human layer)"));
		return false;
	}

	// --- one shot ---------------------------------------------------------------------------------------------------------
	bool StageWaitShot()
	{
		if (!IsSettled())
		{
			return false;
		}
		if (Director->GetMatchShotIndex() == ShotIndexBefore)
		{
			if (FPlatformTime::Seconds() - ShotSent < 1.0)
			{
				return false;
			}
			if (bFallbackStrike)
			{
				Test->AddInfo(FString::Printf(TEXT("%s refused (%s): scripted strike instead"), *ShotKind, *Director->GetLastError()));
				bFallbackStrike = false;
				Shoot(RbDiveBarTest::StrikeCommand(FallbackStrike), TEXT("RbStrike (fallback of a refused RbStroke)"));
				return false;
			}
			Test->AddError(FString::Printf(TEXT("%s: no shot committed (%s)"), *ShotKind, *Director->GetLastError()));
			return true;
		}
		bFallbackStrike = false;
		++Shots;
		++ShotsThisRack;
		const FRbLastShotSummary& Last = Director->GetLastShot();
		const TSharedPtr<const FRbShot> Shot = Director->GetLastCommittedShot();
		Test->AddInfo(FString::Printf(TEXT("shot %d (rack %d #%d, player %d) %s: pocketed [%s], fouls %08x, next %d, speed %.3f m/s, %d playback frames"), Shots, Racks,
			ShotsThisRack, ActiveBefore, *ShotKind, *RbDiveBarTest::Pocketed(Last), Last.Fouls.Bits, static_cast<int32>(Last.Next), Last.CueSpeed, PlaybackFrames));
		Test->AddInfo(Cheats->MakeStateLine());
		Test->TestTrue(*FString::Printf(TEXT("shot %d: committed through the rules"), Shots), Last.bValid && Shot.IsValid());
		Test->TestTrue(*FString::Printf(TEXT("shot %d: played back over several frames (%d)"), Shots, PlaybackFrames), PlaybackFrames >= 2);
		CheckTableShown(*FString::Printf(TEXT("shot %d"), Shots));
		if (bPlanned && Shot.IsValid())
		{
			const bool bPotted = Shot->Result.Finals[PlannedTarget].Status == rb::BallFinalStatus::Pocketed;
			Test->TestTrue(*FString::Printf(TEXT("shot %d: the planned pot of the %d dropped"), Shots, PlannedTarget), bPotted);
			bPlanned = false;
		}
		BallsPocketed += Last.Pocketed.Num();
		if (bScratchShot)
		{
			bScratchShot = false;
			CheckScratch(Shot);
		}
		if (bHotSeat)
		{
			const int32 ActiveAfter = Director->GetActivePlayer();
			if (Last.Pocketed.Num() == 0 && Director->GetPhase() != ERbDirectorPhase::RackOver)
			{
				Test->TestNotEqual(*FString::Printf(TEXT("hot-seat: nothing dropped -> the turn changes (player %d -> %d)"), ActiveBefore, ActiveAfter), ActiveAfter,
					ActiveBefore);
				bTurnChangeChecked = bTurnChangeChecked || ActiveAfter != ActiveBefore;
			}
			Stage = EStage::HotSeatNext;
			return false;
		}
		Stage = EStage::NextShot;
		return false;
	}

	void CheckScratch(const TSharedPtr<const FRbShot>& Shot)
	{
		const FRbLastShotSummary& Last = Director->GetLastShot();
		Test->TestTrue(TEXT("scratch: the oversized cue ball dropped"), Shot.IsValid() && Shot->Result.Finals[rb::kCueBallId].Status == rb::BallFinalStatus::Pocketed);
		Test->TestTrue(TEXT("scratch: a foul was called"), Last.Fouls.Bits != 0 && Last.Enforced != rb::rules::Foul::Count);
		Test->AddInfo(FString::Printf(TEXT("scratch: foul enforced %d, rule \"%s\""), static_cast<int32>(Last.Enforced), *Last.RuleRef));
		Test->TestEqual(TEXT("scratch: ball in hand for the incoming shooter"), Director->GetPhase(), ERbDirectorPhase::AwaitPlacement);
		Test->TestTrue(TEXT("scratch: cue ball in hand anywhere"), Director->IsCueBallInHand() &&
			Director->GetConstraints().PlacementRegion == rb::rules::CueBallNext::InHandAnywhere);
		Test->TestFalse(TEXT("scratch: the cue ball is off the table until placed"), Balls->IsBallVisible(rb::kCueBallId));
		if (URbOverlayComponent* Overlay = PC->GetOverlay())
		{
			Overlay->Refresh();
			Test->TestTrue(TEXT("scratch: the mandatory overlay lines are on screen (ball in hand)"), Overlay->AreMandatoryLinesShown());
		}
		bScratchChecked = true;
	}

	bool PlaceBallInHand()
	{
		using namespace RbDiveBarTest;
		const FRbTableState& State = Director->GetTableState();
		const int32 Target = LowestBall(State);
		if (!bIllegalPlacementChecked && Target > 0)
		{
			const rb::Vec3 On = State.Balls[Target].State.Position;
			Cmd(FString::Printf(TEXT("RbPlaceCueBall %.9g %.9g"), static_cast<float>(On.x), static_cast<float>(On.y)));
			Test->TestTrue(TEXT("illegal placement (on a ball) refused"), Director->GetPhase() == ERbDirectorPhase::AwaitPlacement && !Director->IsCueBallPlaced());
			bIllegalPlacementChecked = true;
		}
		rb::Vec2 Place;
		FPlan Plan;
		if (PlanPlacement(*Director, State, Place, Plan))
		{
			Test->AddInfo(FString::Printf(TEXT("ball in hand: straight-in on the %d into pocket %d from (%.3f, %.3f)"), Plan.Target, Plan.Pocket, Place.x, Place.y));
		}
		else
		{
			const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
			Place = rb::Vec2(static_cast<float>(Rules.HeadStringX - 0.20), 0.0f);
			Test->AddInfo(TEXT("ball in hand: no straight-in found, placing at the head"));
		}
		Cmd(FString::Printf(TEXT("RbPlaceCueBall %.9g %.9g"), Place.x, Place.y));
		return Test->TestEqual(TEXT("ball in hand placed"), Director->GetPhase(), ERbDirectorPhase::AwaitStroke);
	}

	// --- the next shot of the practice rack -------------------------------------------------------------------------------
	bool StageNextShot()
	{
		using namespace RbDiveBarTest;
		if (!IsSettled())
		{
			return false;
		}
		if (Shots >= kMaxShots)
		{
			Test->AddError(FString::Printf(TEXT("no rack won after %d shots"), Shots));
			return true;
		}
		const FRbTableState& State = Director->GetTableState();
		switch (Director->GetPhase())
		{
		case ERbDirectorPhase::RackOver:
		{
			const FRbLastShotSummary& Last = Director->GetLastShot();
			Test->AddInfo(FString::Printf(TEXT("rack %d over after %d shot(s): next %d, pocketed on the last shot [%s]"), Racks, ShotsThisRack,
				static_cast<int32>(Last.Next), *Pocketed(Last)));
			const bool bWon = Last.Next == rb::rules::NextAction::RackWon || Last.Next == rb::rules::NextAction::MatchWon;
			if (!bWon)
			{
				// A practice rack can also end on fouls (three in a row): no 9 to expect, rack again (integration round).
				Test->AddInfo(FString::Printf(TEXT("rack %d ended without a win (next %d): racking again"), Racks, static_cast<int32>(Last.Next)));
			}
			if (bWon && ShotsThisRack >= 4 && bScratchChecked)
			{
				Test->TestTrue(TEXT("the rack is won"), Last.Next == rb::rules::NextAction::RackWon || Last.Next == rb::rules::NextAction::MatchWon);
				Test->TestTrue(TEXT("the 9 is off the table"), !State.Balls[kLastObjectBall].InPlay);
				Test->TestTrue(TEXT("the 9 dropped on the last shot"), Last.Pocketed.Contains(kLastObjectBall));
				Stage = EStage::Replay;
				return false;
			}
			if (Racks >= kMaxRacks)
			{
				Test->AddError(TEXT("no rack with several shots and a scratch"));
				return true;
			}
			const int32 RackBefore = Director->GetMatchState().RackNumber;
			Test->TestTrue(TEXT("Confirm racks again"), Director->Confirm() && Director->GetMatchState().RackNumber == RackBefore + 1);
			bRackSetUp = true;
			Stage = EStage::Break;
			return false;
		}
		case ERbDirectorPhase::AwaitDecision:
			Cmd(TEXT("RbChoose 0"));
			return false;
		case ERbDirectorPhase::AwaitPlacement:
			return !PlaceBallInHand();
		case ERbDirectorPhase::AwaitStroke:
			break;
		default:
			return false;
		}
		if (!bScratchDone && ShotsThisRack >= 2 && LowestBall(State) != kLastObjectBall)
		{
			FStrike Scratch;
			if (PlanScratch(*Director, State, Scratch))
			{
				bScratchDone = true;
				bScratchShot = true;
				Shoot(StrikeCommand(Scratch), TEXT("deliberate scratch (the cue ball into a pocket)"));
				return false;
			}
			Test->AddInfo(TEXT("no scratch line from here: next shot"));
		}
		FPlan Centre;
		FPlan Plan = PlanPot(*Director, State, -1, false, &Centre);
		if (Centre.bValid && Centre.Width >= 8)
		{
			// The human layer hits the centre (B = 0): played only with a centre plan; a draw plan goes through RbStrike below.
			bFallbackStrike = true;
			FallbackStrike = Centre.Strike;
			bPlanned = false;
			Shoot(FString::Printf(TEXT("RbStroke %.9g %.9g"), Centre.Strike.Speed, Centre.Strike.PhiDeg),
				*FString::Printf(TEXT("RbStroke at the %d (human layer, planned centre window %.1f deg)"), Centre.Target, 0.1 * Centre.Width));
			return false;
		}
		if (!Plan.bValid)
		{
			Plan = PlanAnyPot(*Director, State);
		}
		if (Plan.bValid)
		{
			bPlanned = true;
			PlannedTarget = Plan.Target;
			Shoot(StrikeCommand(Plan.Strike), *FString::Printf(TEXT("RbStrike pot of the %d (pocket %d, cut %.0f deg, window %.1f deg)"), Plan.Target, Plan.Pocket,
				Plan.CutDeg, 0.1 * Plan.Width));
			return false;
		}
		const int32 Target = LowestBall(State);
		const rb::Vec3 Cue = State.Balls[0].State.Position;
		const rb::Vec3 Object = State.Balls[FMath::Max(Target, 1)].State.Position;
		const FStrike Direct{3.0f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Object.y - Cue.y, Object.x - Cue.x))), 0.0f};
		Shoot(StrikeCommand(Direct), *FString::Printf(TEXT("RbStrike safety at the %d (no pot found)"), Target));
		return false;
	}

	// --- a replay of the winning shot returns to the live table --------------------------------------------------------------
	bool StageReplay()
	{
		if (!bReplayStarted)
		{
			if (Cheats->GetQueuedCount() > 0 || Replay->IsReplaying())
			{
				return false;
			}
			ReplayShots = Replay->GetShotCount();
			ReplayShotIndex = Director->GetMatchShotIndex();
			Cmd(FString::Printf(TEXT("RbReplay %d 4"), static_cast<int32>(ERbReplayView::Shooter)));
			if (!Test->TestTrue(TEXT("replay of the winning shot started"), Replay->IsReplaying()))
			{
				return true;
			}
			bReplayStarted = true;
			return false;
		}
		if (Replay->IsReplaying())
		{
			return false;
		}
		Test->TestFalse(TEXT("replay over: the director is live again"), Director->IsReplayActive());
		Test->TestEqual(TEXT("replay over: no shot committed"), Director->GetMatchShotIndex(), ReplayShotIndex);
		Test->TestEqual(TEXT("replay over: history unchanged"), Replay->GetShotCount(), ReplayShots);
		Test->TestTrue(TEXT("replay over: the view is back on the pawn"), PC->GetViewTarget() == PC->GetPawn());
		CheckTableShown(TEXT("after the replay"));
		Stage = EStage::HotSeatSetup;
		return false;
	}

	// --- hot seat: break, then a shot that pockets nothing -> the other player -------------------------------------------------
	bool StageHotSeatSetup()
	{
		if (!IsSettled())
		{
			return false;
		}
		FRbMatchSetup Setup = Mode->GetStartSetup();
		Setup.Mode = ERbMatchMode::HotSeat;
		Setup.Discipline = ERbDiscipline::NineBall;
		Setup.bLag = false;
		Setup.Seed = 2026;
		if (!Test->TestTrue(TEXT("hot-seat 9-ball match"), Director->StartMatch(Setup)))
		{
			return true;
		}
		Cmd(FString::Printf(TEXT("RbPlaybackRate %g"), RbDiveBarTest::kLiveRate));
		bHotSeat = true;
		Stage = EStage::HotSeatNext;
		return false;
	}

	bool StageHotSeatNext()
	{
		using namespace RbDiveBarTest;
		if (!IsSettled())
		{
			return false;
		}
		if (bTurnChangeChecked)
		{
			Stage = EStage::Finish;
			return false;
		}
		if (++HotSeatShots > 8)
		{
			Test->AddError(TEXT("hot-seat: no turn change after 8 shots"));
			return true;
		}
		const FRbTableState& State = Director->GetTableState();
		switch (Director->GetPhase())
		{
		case ERbDirectorPhase::RackOver:
			Test->TestTrue(TEXT("hot-seat: Confirm racks again"), Director->Confirm());
			return false;
		case ERbDirectorPhase::AwaitDecision:
			Cmd(TEXT("RbChoose 0"));
			return false;
		case ERbDirectorPhase::AwaitPlacement:
		{
			if (State.Balls[1].InPlay && Director->GetMatchState().Game.IsBreakShot)
			{
				const rb::rules::RulesTable& Rules = Director->GetMatchConfig().Table;
				Cmd(FString::Printf(TEXT("RbPlaceCueBall %.9g 0.10"), static_cast<float>(Rules.HeadStringX - 0.10)));
				const rb::Vec3 Cue = Director->GetTableState().Balls[0].State.Position;
				const rb::Vec3 Apex = Director->GetTableState().Balls[1].State.Position;
				const FStrike Break{7.5f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Apex.y - Cue.y, Apex.x - Cue.x))), 0.0f};
				Shoot(StrikeCommand(Break), TEXT("hot-seat break (RbStrike)"));
				return false;
			}
			return !PlaceBallInHand();
		}
		case ERbDirectorPhase::AwaitStroke:
			break;
		default:
			return false;
		}
		FStrike Miss;
		if (PlanMiss(*Director, State, Miss))
		{
			Shoot(StrikeCommand(Miss), *FString::Printf(TEXT("hot-seat safety on the %d (nothing drops)"), LowestBall(State)));
			return false;
		}
		const int32 Target = LowestBall(State);
		const rb::Vec3 Cue = State.Balls[0].State.Position;
		const rb::Vec3 Object = State.Balls[FMath::Max(Target, 1)].State.Position;
		Shoot(StrikeCommand(FStrike{1.5f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Object.y - Cue.y, Object.x - Cue.x))), 0.0f}),
			TEXT("hot-seat soft hit (no safety line found)"));
		return false;
	}

	bool StageFinish()
	{
		Test->AddInfo(FString::Printf(TEXT("dive-bar rack: %d rack(s), %d shots, %d balls pocketed, walk %d waypoint(s)"), Racks, Shots, BallsPocketed, WalkReached));
		Test->TestTrue(TEXT("the pawn walked every tight spot"), WalkRoute >= WalkRoutes.Num());
		Test->TestTrue(TEXT("a scratch with ball in hand was played"), bScratchChecked);
		Test->TestTrue(TEXT("an illegal placement was refused"), bIllegalPlacementChecked);
		Test->TestTrue(TEXT("hot-seat: a turn change"), bTurnChangeChecked);
		CheckTableShown(TEXT("end"));
		return true;
	}

	FAutomationTestBase* Test;
	int32 Restarts = 0;
	TWeakObjectPtr<UWorld> SetupWorld;
	EStage Stage = EStage::Setup;
	EStage LastStage = EStage::Setup;
	double StageStart = FPlatformTime::Seconds();

	ARbGameMode* Mode = nullptr;
	URbMatchDirector* Director = nullptr;
	ARbPlayerController* PC = nullptr;
	URbCheatManager* Cheats = nullptr;
	ARbTable* Table = nullptr;
	ARbBallSet* Balls = nullptr;
	URbShotPlaybackComponent* Playback = nullptr;
	URbReplaySubsystem* Replay = nullptr;
	ARbPlayerCharacter* Character = nullptr;

	TArray<TArray<FVector2D>> WalkRoutes;
	TArray<const TCHAR*> WalkNames;
	int32 WalkRoute = 0;
	int32 WalkPoint = 0;
	int32 WalkReached = 0;
	double WalkPointStart = 0.0;

	uint32 ShotIndexBefore = 0;
	int32 ActiveBefore = 0;
	double ShotSent = 0.0;
	FString ShotKind;
	int32 PlaybackFrames = 0;
	double LastPlaybackTime = -1.0;
	int32 Shots = 0;
	int32 ShotsThisRack = 0;
	int32 Racks = 0;
	int32 BallsPocketed = 0;
	bool bRackSetUp = false;
	bool bFallbackStrike = false;
	RbDiveBarTest::FStrike FallbackStrike;
	bool bPlanned = false;
	int32 PlannedTarget = -1;
	bool bScratchDone = false;
	bool bScratchShot = false;
	bool bScratchChecked = false;
	bool bIllegalPlacementChecked = false;
	bool bReplayStarted = false;
	int32 ReplayShots = 0;
	uint32 ReplayShotIndex = 0;
	bool bHotSeat = false;
	int32 HotSeatShots = 0;
	bool bTurnChangeChecked = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbDiveBarRackTest, "RawBreak.Functional.DiveBarRack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbDiveBarRackTest::RunTest(const FString& Parameters)
{
	if (!RbDiveBarTest::MapExists(*this))
	{
		return false;
	}
	AutomationOpenMap(RbAssetPaths::DiveBarMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbDiveBarWaitForMatchCommand(this, 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbDiveBarRackCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
