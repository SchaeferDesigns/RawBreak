#include "Audio/RbAudioScenarios.h"

#include "Simulation/RbSimScenarios.h"
#include "Simulation/RbSimulationSubsystem.h"

#include "rb/Core/Error.h"
#include "rb/Equipment/Cue.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Physics/Motion.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/TableRules.h"

#include <cmath>

// Owner: M2-C.

namespace RbAudioScenarios
{
	bool MakeDiveBarBreak8(FRbShotRequest& Out, FString& OutError)
	{
		FRbTableSetup Setup;                           // level clean table, no lamp (= rbsim's EnvironmentSpec{})
		Setup.Table = ERbTablePreset::SevenFootBar;     // --table 7ft-bar
		Setup.BallSet = ERbBallSetPreset::OldBarOversizedCue; // --balls oldbar
		Setup.BallSetSeed = 13;                        // --seed 13 feeds BuildBallSet and GenerateRack
		const TSharedPtr<const FRbTableContext> Table = FRbTableContext::Create(Setup, OutError);
		if (!Table.IsValid())
		{
			return false;
		}
		Out.Table = Table;
		rb::SimInput& In = Out.Input;
		RbShot::InitSimInput(*Table, In);

		// --rack 8ball --rack-gap sloppy --seed 13
		rb::rules::RackAssignment Rack;
		const rb::rules::RulesConfig Config;
		const rb::ErrorCode RackError =
			rb::rules::GenerateRack(rb::rules::Discipline::EightBall, Config, Table->RulesTable, 13, false, rb::kRackGapSloppyBar, Rack);
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

		// --ball 0:-0.62,0.10
		rb::SimBall& Cue = In.Balls[rb::kCueBallId];
		Cue.InPlay = true;
		Cue.Spec = Table->Balls.Balls[rb::kCueBallId];
		Cue.State = rb::BallState{};
		Cue.State.Position = {-0.62, 0.10, Cue.Spec.Radius};
		rb::ClassifyState(Cue.State, Cue.Spec.Radius, 0.0, In.Params.Numerics);

		// --cue house --speed 8 --aim -5.07 --offset 0,-0.05 (contact-point offsets), squirt on, elevation 0
		rb::StrikeRequest Strike;
		Strike.Ball = static_cast<rb::BallId>(rb::kCueBallId);
		Strike.Input.Cue = rb::GetCueSpec(rb::CuePreset::House19oz);
		Strike.Input.Speed = 8.0;
		Strike.Input.Elevation = 0.0 * rb::kDegToRad;
		Strike.Input.Azimuth = -5.07 * rb::kDegToRad;
		Strike.Input.OffsetA = 0.0;
		Strike.Input.OffsetB = -0.05;
		Strike.Input.SquirtEnabled = true;
		In.Strikes.PushBack(Strike);
		return true;
	}

	bool MakeTestRoomBreak9(FRbShotRequest& Out, FString& OutError)
	{
		return RbSimScenarios::MakeBreak9(Out, OutError);
	}

	bool BreakerHead(const FRbShotRequest& Request, rb::Vec3& OutHead, rb::Vec3& OutLeft)
	{
		if (Request.Input.Strikes.Size() == 0)
		{
			return false;
		}
		const rb::StrikeRequest& Strike = Request.Input.Strikes[0];
		const rb::SimBall& Ball = Request.Input.Balls[Strike.Ball];
		const double Phi = Strike.Input.Azimuth;
		const rb::Vec3 Aim(std::cos(Phi), std::sin(Phi), 0.0);
		OutLeft = rb::Vec3(-std::sin(Phi), std::cos(Phi), 0.0);
		const rb::Vec3 P0 = Ball.State.Position;
		OutHead = P0 - Aim * 0.55 + rb::Vec3(0.0, 0.0, 0.36 - P0.z);
		return true;
	}

	TSharedPtr<FRbShot> Simulate(FRbShotRequest&& Request, FString& OutError)
	{
		const TSharedRef<FRbShot> Shot = URbSimulationSubsystem::RunShotBlocking(MoveTemp(Request));
		if (Shot->Result.Status != rb::SimStatus::Ok)
		{
			OutError = TEXT("simulation not Ok");
		}
		return Shot;
	}
}
