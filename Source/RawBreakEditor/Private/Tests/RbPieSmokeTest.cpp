// Functional smoke test of the headless PIE path (Docs/ue-architecture.md 9.5, 10): PIE on an engine map works
// under -NullRHI and the RAW BREAK world subsystems exist in the play world. Functional tests that need PIE live in
// this editor module (UnrealEd); pure unit tests live in the RawBreak module. Owner: UE-0.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRbCheckPieWorldCommand, FAutomationTestBase*, Test);

bool FRbCheckPieWorldCommand::Update()
{
	UWorld* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
	Test->TestNotNull(TEXT("PIE world is running"), World);
	if (World)
	{
		Test->TestTrue(TEXT("PIE world has begun play"), World->HasBegunPlay());
		Test->TestNotNull(TEXT("URbSimulationSubsystem exists in PIE"), World->GetSubsystem<URbSimulationSubsystem>());
		Test->TestNotNull(TEXT("URbReplaySubsystem exists in PIE"), World->GetSubsystem<URbReplaySubsystem>());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPieSmokeTest, "RawBreak.Functional.PieSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbPieSmokeTest::RunTest(const FString& Parameters)
{
	AutomationOpenMap(TEXT("/Engine/Maps/Entry"));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FRbCheckPieWorldCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
