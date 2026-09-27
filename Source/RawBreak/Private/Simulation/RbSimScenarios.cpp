#include "Simulation/RbSimScenarios.h"

#include "rb/Core/Error.h"
#include "rb/Equipment/Cue.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Physics/Motion.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/TableRules.h"

// Owner: UE-6a.

namespace RbSimScenarios
{
	bool MakeBreak9(FRbShotRequest& Out, FString& OutError)
	{
		FRbTableSetup Setup;    // 9-ft pro, standard balls, level clean table, no lamp (= rbsim's EnvironmentSpec{})
		Setup.BallSetSeed = 11; // rbsim --seed feeds BuildBallSet and GenerateRack
		const TSharedPtr<const FRbTableContext> Table = FRbTableContext::Create(Setup, OutError);
		if (!Table.IsValid())
		{
			return false;
		}
		Out.Table = Table;
		rb::SimInput& In = Out.Input;
		RbShot::InitSimInput(*Table, In); // rbsim: MakePhysicsParams(Spec), RecordOptions{} + --record

		// --rack 9ball --rack-gap wooden --seed 11 (rbsim: RulesConfig{}, the rules table of the geometry)
		rb::rules::RackAssignment Rack;
		const rb::rules::RulesConfig Config;
		const rb::ErrorCode RackError =
			rb::rules::GenerateRack(rb::rules::Discipline::NineBall, Config, Table->RulesTable, 11, false, rb::kRackGapWoodenRack, Rack);
		if (!rb::Succeeded(RackError))
		{
			OutError = FString::Printf(TEXT("GenerateRack failed: %hs"), rb::ToString(RackError));
			return false;
		}
		for (int32 Id = 1; Id < rb::kPoolBallCount; ++Id)
		{
			if (Rack.Racked[Id])
			{
				rb::SimBall& B = In.Balls[Id];
				B.InPlay = true;
				B.Spec = Table->Balls.Balls[Id];
				B.State.Position = {Rack.Position[Id].x, Rack.Position[Id].y, B.Spec.Radius};
			}
		}

		// --ball 0:-0.735,0.12 (a fresh state at rest, classified like rbsim does)
		rb::SimBall& Cue = In.Balls[rb::kCueBallId];
		Cue.InPlay = true;
		Cue.Spec = Table->Balls.Balls[rb::kCueBallId];
		Cue.State = rb::BallState{};
		Cue.State.Position = {-0.735, 0.12, Cue.Spec.Radius};
		rb::ClassifyState(Cue.State, Cue.Spec.Radius, 0.0, In.Params.Numerics);

		// --cue break --speed 9 --aim -5.006 --offset 0,-0.1 (contact-point offsets), squirt on, elevation 0
		rb::StrikeRequest Strike;
		Strike.Ball = static_cast<rb::BallId>(rb::kCueBallId);
		Strike.Input.Cue = rb::GetCueSpec(rb::CuePreset::Break21oz);
		Strike.Input.Speed = 9.0;
		Strike.Input.Elevation = 0.0 * rb::kDegToRad;
		Strike.Input.Azimuth = -5.006 * rb::kDegToRad;
		Strike.Input.OffsetA = 0.0;
		Strike.Input.OffsetB = -0.1;
		Strike.Input.SquirtEnabled = true;
		In.Strikes.PushBack(Strike);
		return true;
	}

	bool MakeTwoBall(FRbShotRequest& Out, FString& OutError)
	{
		const TSharedPtr<const FRbTableContext> Table = FRbTableContext::Create(FRbTableSetup{}, OutError);
		if (!Table.IsValid())
		{
			return false;
		}
		Out.Table = Table;
		rb::SimInput& In = Out.Input;
		RbShot::InitSimInput(*Table, In);
		const int32 Ids[2] = {rb::kCueBallId, 1};
		const rb::Vec2 Positions[2] = {{-0.6, 0.0}, {0.3, 0.05}};
		for (int32 k = 0; k < 2; ++k)
		{
			rb::SimBall& B = In.Balls[Ids[k]];
			B.InPlay = true;
			B.Spec = Table->Balls.Balls[Ids[k]];
			B.State.Position = {Positions[k].x, Positions[k].y, B.Spec.Radius};
		}
		rb::StrikeRequest Strike;
		Strike.Ball = static_cast<rb::BallId>(rb::kCueBallId);
		Strike.Input.Cue = rb::GetCueSpec(rb::CuePreset::Playing19oz);
		Strike.Input.Speed = 3.0;
		Strike.Input.Azimuth = 3.0 * rb::kDegToRad;
		Strike.Input.OffsetA = 0.1;
		Strike.Input.OffsetB = 0.2;
		In.Strikes.PushBack(Strike);
		return true;
	}

	bool MakeByName(const FString& Name, FRbShotRequest& Out, FString& OutError)
	{
		if (Name.Equals(TEXT("break9"), ESearchCase::IgnoreCase))
		{
			return MakeBreak9(Out, OutError);
		}
		if (Name.Equals(TEXT("twoball"), ESearchCase::IgnoreCase))
		{
			return MakeTwoBall(Out, OutError);
		}
		OutError = FString::Printf(TEXT("unknown scenario '%s' (break9, twoball)"), *Name);
		return false;
	}
}
