// The architect's shared contracts (Docs/ue-architecture.md 18.1 / 18.9): Config/*.ini <-> RbAssetPaths / RbTypes constants that
// every M2 package compiles against (collision channels and profiles, physical surfaces, render / UI config, venue maps, tags,
// capture camera names, packaging) and the pure helpers of the headless capture / perf recorder (rbue.py capture / perf).
// Owner: M2-0 (architect).

#include "Core/RbAssetPaths.h"
#include "Core/RbTypes.h"
#include "Dev/RbHeadlessCaptureSubsystem.h"
#include "Tests/RbTestFlags.h"
#include "UI/Core/RbDpiScalingRule.h"

#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/PhysicsSettings.h"

#include "Ai/RbOpponentRoster.h"
#include "Game/RbMatchDirector.h"
#include "Modes/RbMatchRules.h"

#include "rb/Human/AiProfiles.h"
#include "rb/Human/Skill.h"
#include "rb/Human/Venue.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbContractTests
{
	int32 CVarInt(const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetInt() : INDEX_NONE;
	}

	// What Contracts.Packaging reports for one playable map (mirror of rbue.py cook_list_problems):
	//   exists + listed      -> Ok (the state after the architect listed it at the merge)
	//   exists + not listed  -> Error for a map that must already be listed (the test room), else a Warning (request)
	//   missing + listed     -> Error (the cook fails with "Could not find package")
	//   missing + not listed -> Ok (not generated yet)
	enum class ECookListVerdict : uint8
	{
		Ok,
		Warning,
		Error,
	};

	ECookListVerdict CookListVerdict(bool bExists, bool bListed, bool bMustBeListed)
	{
		if (bExists)
		{
			return bListed ? ECookListVerdict::Ok : (bMustBeListed ? ECookListVerdict::Error : ECookListVerdict::Warning);
		}
		return bListed ? ECookListVerdict::Error : ECookListVerdict::Ok;
	}

	// The package of one MapsToCook entry of DefaultGame.ini, `(FilePath="/Game/Generated/Maps/L_X")` -> "/Game/Generated/Maps/L_X"
	// (empty when the entry has no FilePath).
	FString MapsToCookPackage(const FString& Entry)
	{
		const int32 Key = Entry.Find(TEXT("FilePath="));
		if (Key == INDEX_NONE)
		{
			return FString();
		}
		FString Value = Entry.Mid(Key + 9).TrimStart();
		Value.RemoveFromStart(TEXT("\""));
		int32 End = INDEX_NONE;
		for (const TCHAR* Stop : {TEXT("\""), TEXT(")"), TEXT(",")})
		{
			const int32 At = Value.Find(Stop);
			End = At != INDEX_NONE && (End == INDEX_NONE || At < End) ? At : End;
		}
		return (End == INDEX_NONE ? Value : Value.Left(End)).TrimStartAndEnd();
	}

	// Every generated level (Content/Generated/Maps/**.umap) as a package name. DirectoriesToAlwaysCook = /Game/Generated cooks
	// only .uasset files (UE 5.8 CookOnTheFlyServer: FindFilesRecursive with the asset extension), never a .umap: a generated map
	// - a venue, the title, a sublevel that is streamed in by name - reaches the packaged build only through MapsToCook or as a
	// reference of a cooked map, so the contract lists every one of them (review).
	TArray<FString> GeneratedMapPackages()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Generated/Maps")),
			*(FString(TEXT("*")) + FPackageName::GetMapPackageExtension()), true, false);
		TArray<FString> Packages;
		for (const FString& File : Files)
		{
			FString Package;
			if (FPackageName::TryConvertFilenameToLongPackageName(File, Package))
			{
				Packages.AddUnique(Package);
			}
		}
		Packages.Sort();
		return Packages;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractCollisionChannels, "RawBreak.Unit.Contracts.CollisionChannels", RB_UNIT_TEST_FLAGS)
bool FRbContractCollisionChannels::RunTest(const FString& Parameters)
{
	using namespace RbAssetPaths::Collision;
	const UCollisionProfile* Profiles = UCollisionProfile::Get();
	TestEqual(TEXT("RbCueSweep is GameTraceChannel1"), Profiles->ReturnChannelNameFromContainerIndex(CueSweepChannel), FName(TEXT("RbCueSweep")));
	TestEqual(TEXT("RbLooseBall is GameTraceChannel2"), Profiles->ReturnChannelNameFromContainerIndex(LooseBallChannel), FName(TEXT("RbLooseBall")));
	TestTrue(TEXT("RbCueSweep is a trace channel"), Profiles->ConvertToTraceType(CueSweepChannel) != TraceTypeQuery_MAX);
	TestTrue(TEXT("RbLooseBall is an object channel"), Profiles->ConvertToObjectType(LooseBallChannel) != ObjectTypeQuery_MAX);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractCollisionProfiles, "RawBreak.Unit.Contracts.CollisionProfiles", RB_UNIT_TEST_FLAGS)
bool FRbContractCollisionProfiles::RunTest(const FString& Parameters)
{
	using namespace RbAssetPaths::Collision;
	const UCollisionProfile* Profiles = UCollisionProfile::Get();
	struct FExpect
	{
		ECollisionChannel Channel;
		ECollisionResponse Response;
	};
	auto Check = [this, Profiles](FName Name, ECollisionEnabled::Type Enabled, ECollisionChannel ObjectType, std::initializer_list<FExpect> Responses)
	{
		FCollisionResponseTemplate Template;
		if (!TestTrue(FString::Printf(TEXT("profile %s exists"), *Name.ToString()), Profiles->GetProfileTemplate(Name, Template)))
		{
			return;
		}
		TestEqual(FString::Printf(TEXT("%s collision enabled"), *Name.ToString()), static_cast<int32>(Template.CollisionEnabled.GetValue()), static_cast<int32>(Enabled));
		TestEqual(FString::Printf(TEXT("%s object type"), *Name.ToString()), static_cast<int32>(Template.ObjectType.GetValue()), static_cast<int32>(ObjectType));
		for (const FExpect& E : Responses)
		{
			TestEqual(FString::Printf(TEXT("%s response to %s"), *Name.ToString(), *Profiles->ReturnChannelNameFromContainerIndex(E.Channel).ToString()),
				static_cast<int32>(Template.ResponseToChannels.GetResponse(E.Channel)), static_cast<int32>(E.Response));
		}
	};
	// Walls, columns, furniture: block the pawn, the cue sweep and loose balls.
	Check(VenueBlockProfile, ECollisionEnabled::QueryAndPhysics, ECC_WorldStatic,
		{{ECC_Pawn, ECR_Block}, {CueSweepChannel, ECR_Block}, {LooseBallChannel, ECR_Block}, {ECC_Visibility, ECR_Block}});
	// Clutter: gaze traces only.
	Check(VenuePropProfile, ECollisionEnabled::QueryOnly, ECC_WorldStatic,
		{{ECC_Pawn, ECR_Ignore}, {CueSweepChannel, ECR_Ignore}, {LooseBallChannel, ECR_Ignore}, {ECC_Visibility, ECR_Block}});
	// A ball off the table: stopped by the world, invisible to the pawn, the camera and the cue sweep.
	Check(LooseBallProfile, ECollisionEnabled::QueryAndPhysics, LooseBallChannel,
		{{ECC_WorldStatic, ECR_Block}, {ECC_Pawn, ECR_Ignore}, {ECC_Camera, ECR_Ignore}, {CueSweepChannel, ECR_Ignore}});
	// The return volume overlaps loose balls only.
	Check(BallReturnProfile, ECollisionEnabled::QueryOnly, ECC_WorldStatic,
		{{LooseBallChannel, ECR_Overlap}, {ECC_Pawn, ECR_Ignore}, {ECC_Visibility, ECR_Ignore}, {ECC_Camera, ECR_Ignore}, {CueSweepChannel, ECR_Ignore},
			{ECC_WorldDynamic, ECR_Ignore}});
	// The engine's Pawn profile ignores both new channels (the pawn neither blocks the cue sweep nor kicks loose balls).
	FCollisionResponseTemplate Pawn;
	if (TestTrue(TEXT("Pawn profile"), Profiles->GetProfileTemplate(UCollisionProfile::Pawn_ProfileName, Pawn)))
	{
		TestEqual(TEXT("Pawn ignores RbCueSweep"), static_cast<int32>(Pawn.ResponseToChannels.GetResponse(CueSweepChannel)), static_cast<int32>(ECR_Ignore));
		TestEqual(TEXT("Pawn ignores RbLooseBall"), static_cast<int32>(Pawn.ResponseToChannels.GetResponse(LooseBallChannel)), static_cast<int32>(ECR_Ignore));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractPhysicalSurfaces, "RawBreak.Unit.Contracts.PhysicalSurfaces", RB_UNIT_TEST_FLAGS)
bool FRbContractPhysicalSurfaces::RunTest(const FString& Parameters)
{
	using namespace RbAssetPaths::Surface;
	const TPair<EPhysicalSurface, const TCHAR*> Expected[] = {
		{Ball, TEXT("RbBall")}, {Vct, TEXT("RbVct")}, {Rubber, TEXT("RbRubber")}, {Wood, TEXT("RbWood")}, {Concrete, TEXT("RbConcrete")}, {Cloth, TEXT("RbCloth")}};
	const TArray<FPhysicalSurfaceName>& Surfaces = UPhysicsSettings::Get()->PhysicalSurfaces;
	for (const TPair<EPhysicalSurface, const TCHAR*>& E : Expected)
	{
		const FPhysicalSurfaceName* Found = Surfaces.FindByPredicate([&E](const FPhysicalSurfaceName& S) { return S.Type == E.Key; });
		if (TestNotNull(FString::Printf(TEXT("surface type %d configured"), static_cast<int32>(E.Key)), Found))
		{
			TestEqual(FString::Printf(TEXT("surface type %d name"), static_cast<int32>(E.Key)), Found->Name, FName(E.Value));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractRenderConfig, "RawBreak.Unit.Contracts.RenderConfig", RB_UNIT_TEST_FLAGS)
bool FRbContractRenderConfig::RunTest(const FString& Parameters)
{
	// Project-wide switches of DefaultEngine.ini that are read-only at runtime (a change needs a shader recompile): the realism
	// baseline (UE-0) and the M2 virtual textures (venue H-3).
	TestEqual(TEXT("r.Substrate"), RbContractTests::CVarInt(TEXT("r.Substrate")), 1);
	TestEqual(TEXT("r.Substrate.ProjectGBufferFormat (Adaptive)"), RbContractTests::CVarInt(TEXT("r.Substrate.ProjectGBufferFormat")), 1);
	TestEqual(TEXT("r.VirtualTextures (M2)"), RbContractTests::CVarInt(TEXT("r.VirtualTextures")), 1);
	TestEqual(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"), RbContractTests::CVarInt(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange")), 1);
	TestEqual(TEXT("r.AllowStaticLighting"), RbContractTests::CVarInt(TEXT("r.AllowStaticLighting")), 0);
	TestNearlyEqual(TEXT("near clip plane 1 cm"), static_cast<double>(GNearClippingPlane), 1.0, 1e-6);
	TestTrue(TEXT("RawBreakAudioDsp module loaded (M2)"), FModuleManager::Get().IsModuleLoaded(TEXT("RawBreakAudioDsp")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractUiScaleRule, "RawBreak.Unit.Contracts.UiScaleRule", RB_UNIT_TEST_FLAGS)
bool FRbContractUiScaleRule::RunTest(const FString& Parameters)
{
	const UUserInterfaceSettings* Ui = GetDefault<UUserInterfaceSettings>();
	TestEqual(TEXT("UIScaleRule = Custom"), static_cast<int32>(Ui->UIScaleRule), static_cast<int32>(EUIScalingRule::Custom));
	TestEqual(TEXT("custom rule = URbDpiScalingRule"), Ui->CustomScalingRuleClass.ResolveClass(), URbDpiScalingRule::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractVenues, "RawBreak.Unit.Contracts.Venues", RB_UNIT_TEST_FLAGS)
bool FRbContractVenues::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("test room map"), FString(RbTypes::MapFor(ERbVenue::TestRoom)), FString(RbAssetPaths::M1TestRoomMap));
	TestEqual(TEXT("dive bar map"), FString(RbTypes::MapFor(ERbVenue::DiveBar)), FString(RbAssetPaths::DiveBarMap));
	const UEnum* Venues = StaticEnum<ERbVenue>();
	TSet<FString> Maps;
	for (int32 Index = 0; Index < Venues->NumEnums() - 1; ++Index) // - the generated _MAX
	{
		const FString Map = RbTypes::MapFor(static_cast<ERbVenue>(Venues->GetValueByIndex(Index)));
		TestTrue(FString::Printf(TEXT("%s is a generated map"), *Map), Map.StartsWith(TEXT("/Game/Generated/Maps/L_")));
		TestFalse(FString::Printf(TEXT("%s used once"), *Map), Maps.Contains(Map));
		Maps.Add(Map);
	}
	TestFalse(TEXT("title map is no venue"), Maps.Contains(FString(RbAssetPaths::TitleMap)));

	TestEqual(TEXT("venue kind DiveBar"), static_cast<int32>(RbTypes::ToCore(ERbVenueKind::DiveBar)), static_cast<int32>(rb::human::VenueKind::DiveBar));
	TestEqual(TEXT("venue kind PoolHall"), static_cast<int32>(RbTypes::ToCore(ERbVenueKind::PoolHall)), static_cast<int32>(rb::human::VenueKind::PoolHall));
	TestEqual(TEXT("venue kind Arena"), static_cast<int32>(RbTypes::ToCore(ERbVenueKind::Arena)), static_cast<int32>(rb::human::VenueKind::Arena));

	// Capture / menu camera tags (venue-dive-bar 12.2 / 12.3, ui-ux 6.3) and tags shared between packages.
	using namespace RbAssetPaths;
	TestEqual(TEXT("V01"), CaptureCamera::DiveBarView(1), FString(TEXT("RbCam_DB_V01")));
	TestEqual(TEXT("V12"), CaptureCamera::DiveBarView(12), FString(TEXT("RbCam_DB_V12")));
	TestEqual(TEXT("TH7"), CaptureCamera::DiveBarTrailer(7), FString(TEXT("RbCam_DB_TH7")));
	TestEqual(TEXT("S0"), CaptureCamera::MenuStation(0), FString(TEXT("RbCam_Menu_S0")));
	TestEqual(TEXT("audio anchor"), Tag::AudioAnchor(TEXT("Jukebox")), FName(TEXT("RbAudio_Jukebox")));
	const FName Tags[] = {Tag::PlayerTable, Tag::DiveBarCeiling, Tag::LooseBall, Tag::BallReturnVolume, Tag::VenueInfo};
	TSet<FName> Unique;
	for (const FName& T : Tags)
	{
		TestFalse(FString::Printf(TEXT("tag %s unique"), *T.ToString()), Unique.Contains(T));
		Unique.Add(T);
	}
	return true;
}

// M3 plan step (Docs/ue-architecture.md 19.3): the shared enums mirror the core ones, the URL names round-trip, the roster lists the
// six profiles, the new tags are unique, and the moved rules configuration is the M2 one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractM3Enums, "RawBreak.Unit.Contracts.M3Enums", RB_UNIT_TEST_FLAGS)
bool FRbContractM3Enums::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("six AI profiles"), static_cast<int32>(ERbAiProfile::Count), rb::human::kAiProfileCount);
	for (int32 Index = 0; Index < static_cast<int32>(ERbAiProfile::Count); ++Index)
	{
		const ERbAiProfile Profile = static_cast<ERbAiProfile>(Index);
		TestEqual(FString::Printf(TEXT("profile %d mirrors AiProfileId"), Index), static_cast<int32>(RbTypes::ToCore(Profile)), Index);
		TestTrue(FString::Printf(TEXT("profile %d round trip"), Index), RbTypes::FromCore(RbTypes::ToCore(Profile)) == Profile);
		ERbAiProfile Parsed = ERbAiProfile::Count;
		TestTrue(FString::Printf(TEXT("parse %s"), RbTypes::ToString(Profile)),
			RbTypes::ParseAiProfile(FString(RbTypes::ToString(Profile)).ToLower(), Parsed) && Parsed == Profile);
		TestTrue(FString::Printf(TEXT("roster entry %d"), Index), RbOpponentRoster::Get(Profile).Profile == Profile);
	}
	ERbAiProfile Unchanged = ERbAiProfile::TouringPro;
	TestFalse(TEXT("unknown profile refused"), RbTypes::ParseAiProfile(TEXT("NineBall"), Unchanged));
	TestTrue(TEXT("refused parse leaves the value"), Unchanged == ERbAiProfile::TouringPro);
	TestEqual(TEXT("roster size"), RbOpponentRoster::GetAll().Num(), static_cast<int32>(ERbAiProfile::Count));

	for (const ERbBridgeType Bridge : {ERbBridgeType::Closed, ERbBridgeType::Open, ERbBridgeType::Rail, ERbBridgeType::Elevated, ERbBridgeType::Mechanical})
	{
		TestEqual(TEXT("bridge mirrors BridgeType"), static_cast<int32>(RbTypes::ToCore(Bridge)), static_cast<int32>(Bridge));
		TestTrue(TEXT("bridge round trip"), RbTypes::FromCore(RbTypes::ToCore(Bridge)) == Bridge);
	}
	TestTrue(TEXT("default stroke situation = closed bridge"), RbTypes::FromCore(rb::human::StrokeSituation{}.Bridge) == ERbBridgeType::Closed);

	for (const ERbCallPolicy Policy : {ERbCallPolicy::Casual, ERbCallPolicy::EveryShot})
	{
		ERbCallPolicy Parsed = Policy == ERbCallPolicy::Casual ? ERbCallPolicy::EveryShot : ERbCallPolicy::Casual;
		TestTrue(FString::Printf(TEXT("parse %s"), RbTypes::ToString(Policy)), RbTypes::ParseCallPolicy(RbTypes::ToString(Policy), Parsed) && Parsed == Policy);
	}

	using namespace RbAssetPaths;
	const FName Tags[] = {Tag::PlayerTable, Tag::DiveBarCeiling, Tag::LooseBall, Tag::BallReturnVolume, Tag::VenueInfo, Tag::WaitSpot, Tag::ChalkCube,
		Tag::Opponent};
	TSet<FName> Unique;
	for (const FName& T : Tags)
	{
		TestFalse(FString::Printf(TEXT("tag %s unique"), *T.ToString()), Unique.Contains(T));
		Unique.Add(T);
	}

	// The configuration moved out of the director: practice / hot-seat calls stay casual (M2), the race and the seed are kept.
	FRbMatchSetup Setup;
	Setup.Mode = ERbMatchMode::HotSeat;
	Setup.Discipline = ERbDiscipline::EightBall;
	Setup.RaceTo = 3;
	const rb::rules::MatchConfig Config = RbMatchRules::MakeMatchConfig(Setup, 77, rb::rules::RulesTable{});
	TestTrue(TEXT("8-ball"), Config.Game == rb::rules::Discipline::EightBall);
	TestTrue(TEXT("hot-seat calls stay ObviousAssist"), Config.Rules.Calls == rb::rules::CallMode::ObviousAssist);
	TestTrue(TEXT("assisted input"), Config.Rules.Input == rb::rules::InputMode::Assisted);
	TestEqual(TEXT("race"), Config.RaceTo, 3);
	TestTrue(TEXT("seed"), Config.Seed == 77ull);
	Setup.Mode = ERbMatchMode::Practice;
	TestEqual(TEXT("practice: no race"), RbMatchRules::MakeMatchConfig(Setup, 1, rb::rules::RulesTable{}).RaceTo, TNumericLimits<int32>::Max());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbContractPackaging, "RawBreak.Unit.Contracts.Packaging", RB_UNIT_TEST_FLAGS)
bool FRbContractPackaging::RunTest(const FString& Parameters)
{
	// Every level the game can open (the venues of the title screen and the title itself) and every other generated level (the
	// venues' sublevels; DirectoriesToAlwaysCook never cooks a .umap, see GeneratedMapPackages) is cooked once it exists: the
	// packaged build of the owner playtest (M2-A10) must not miss a map. DefaultGame.ini lists them (architect, at the merge of the
	// package that generates the map). A listed map that does not exist would fail the cook ("Could not find package"), so a map is
	// added only together with its generated level. A generated map that is not listed yet is a WARNING, not a failure: on the branch
	// of the package that generates it nobody but the architect may edit DefaultGame.ini (18.2); at the merge the architect lists
	// it, and `rbue.py package` refuses to cook while a generated playable map is missing from MapsToCook.
	TArray<FString> Cooked;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("MapsToCook"), Cooked, GGameIni);
	TArray<FString> Listed;
	for (const FString& Entry : Cooked)
	{
		const FString Package = RbContractTests::MapsToCookPackage(Entry);
		if (TestFalse(FString::Printf(TEXT("MapsToCook entry %s has a FilePath"), *Entry), Package.IsEmpty()))
		{
			Listed.Add(Package);
		}
	}
	// The levels the game opens (the title, every venue) and every generated level incl. sublevels (see GeneratedMapPackages), plus
	// every listed map (a listed map that does not exist - a typo, a removed level - fails the cook).
	TArray<FString> Candidates = {RbAssetPaths::TitleMap};
	const UEnum* Venues = StaticEnum<ERbVenue>();
	for (int32 Index = 0; Index < Venues->NumEnums() - 1; ++Index)
	{
		Candidates.AddUnique(RbTypes::MapFor(static_cast<ERbVenue>(Venues->GetValueByIndex(Index))));
	}
	for (const FString& Map : RbContractTests::GeneratedMapPackages())
	{
		Candidates.AddUnique(Map);
	}
	for (const FString& Map : Listed)
	{
		Candidates.AddUnique(Map);
	}
	TestTrue(TEXT("the M1 test room exists (committed content)"), FPackageName::DoesPackageExist(RbAssetPaths::M1TestRoomMap));
	for (const FString& Map : Candidates)
	{
		const bool bListed = Listed.Contains(Map);
		const bool bExists = FPackageName::DoesPackageExist(Map);
		switch (RbContractTests::CookListVerdict(bExists, bListed, Map == RbAssetPaths::M1TestRoomMap))
		{
		case RbContractTests::ECookListVerdict::Ok:
			AddInfo(FString::Printf(TEXT("%s: %s"), *Map, bExists ? TEXT("generated and in MapsToCook") : TEXT("not generated yet (its package's generator)")));
			break;
		case RbContractTests::ECookListVerdict::Warning:
			AddWarning(FString::Printf(TEXT("%s is generated but not in MapsToCook yet: the architect lists it in DefaultGame.ini at the merge of its "
				"package (request; rbue.py package refuses to cook without it)"), *Map));
			break;
		case RbContractTests::ECookListVerdict::Error:
			AddError(bExists ? FString::Printf(TEXT("%s exists but is not in MapsToCook"), *Map)
							 : FString::Printf(TEXT("%s is in MapsToCook but not generated (the cook would fail)"), *Map));
			break;
		}
	}

	// The packaged build starts where the game starts: the title once M2-D generated it (M2-A10: the owner plays title -> dive bar),
	// the test room before. GameDefaultMap is the architect's (DefaultEngine.ini), switched at the merge of M2-D like MapsToCook.
	FString DefaultMapPath;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), DefaultMapPath, GEngineIni);
	const FString DefaultMap = FPackageName::ObjectPathToPackageName(DefaultMapPath);
	TestTrue(FString::Printf(TEXT("GameDefaultMap %s exists"), *DefaultMapPath), !DefaultMap.IsEmpty() && FPackageName::DoesPackageExist(DefaultMap));
	if (FPackageName::DoesPackageExist(RbAssetPaths::TitleMap) && DefaultMap != RbAssetPaths::TitleMap)
	{
		AddWarning(FString::Printf(TEXT("%s is generated but GameDefaultMap is still %s: the architect switches it at the merge of M2-D (request; "
			"rbue.py package refuses to cook without it)"), RbAssetPaths::TitleMap, *DefaultMap));
	}
	TArray<FString> AlwaysCook;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysCook"), AlwaysCook, GGameIni);
	TestTrue(TEXT("/Game/Generated always cooked (assets loaded by path)"),
		AlwaysCook.ContainsByPredicate([](const FString& Entry) { return Entry.Contains(TEXT("\"/Game/Generated\"")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPipelineCookListVerdict, "RawBreak.Unit.Pipeline.CookListVerdict", RB_UNIT_TEST_FLAGS)
bool FRbPipelineCookListVerdict::RunTest(const FString& Parameters)
{
	// The four states a playable map passes through (review: a generated map that the architect had listed at the merge must be
	// Ok, not "not generated yet and must not be listed").
	using RbContractTests::CookListVerdict;
	using V = RbContractTests::ECookListVerdict;
	TestTrue(TEXT("not generated, not listed: ok (before its package)"), CookListVerdict(false, false, false) == V::Ok);
	TestTrue(TEXT("generated, not listed: warning (request to the architect)"), CookListVerdict(true, false, false) == V::Warning);
	TestTrue(TEXT("generated and listed: ok (after the merge)"), CookListVerdict(true, true, false) == V::Ok);
	TestTrue(TEXT("listed but not generated: error (the cook fails)"), CookListVerdict(false, true, false) == V::Error);
	TestTrue(TEXT("test room not listed: error"), CookListVerdict(true, false, true) == V::Error);
	TestTrue(TEXT("test room listed: ok"), CookListVerdict(true, true, true) == V::Ok);

	// MapsToCook entries as GConfig returns them (review: listed maps are compared by their package, not by a substring).
	using RbContractTests::MapsToCookPackage;
	TestEqual(TEXT("entry"), MapsToCookPackage(TEXT("(FilePath=\"/Game/Generated/Maps/L_DiveBar\")")), FString(TEXT("/Game/Generated/Maps/L_DiveBar")));
	TestEqual(TEXT("sublevel entry"), MapsToCookPackage(TEXT("(FilePath=\"/Game/Generated/Maps/L_DiveBar_Light_Open\")")),
		FString(TEXT("/Game/Generated/Maps/L_DiveBar_Light_Open")));
	TestEqual(TEXT("unquoted entry"), MapsToCookPackage(TEXT("(FilePath=/Game/Generated/Maps/L_Title)")), FString(TEXT("/Game/Generated/Maps/L_Title")));
	TestEqual(TEXT("no FilePath"), MapsToCookPackage(TEXT("(Path=\"/Game/Generated\")")), FString());

	// The generated levels on disk include the committed test room (and, after the M2 merges, the venues' sublevels).
	TestTrue(TEXT("generated maps include the test room"), RbContractTests::GeneratedMapPackages().Contains(FString(RbAssetPaths::M1TestRoomMap)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPipelineCaptureLists, "RawBreak.Unit.Pipeline.CaptureLists", RB_UNIT_TEST_FLAGS)
bool FRbPipelineCaptureLists::RunTest(const FString& Parameters)
{
	const TArray<FString> Cameras = URbHeadlessCaptureSubsystem::ParseList(TEXT(" RbCam_DB_V01, RbCam_DB_V02 ,,RbCam_DB_TH1;\"RbCam_Menu_S0\" "));
	TestEqual(TEXT("camera count"), Cameras.Num(), 4);
	if (Cameras.Num() == 4)
	{
		TestEqual(TEXT("first"), Cameras[0], FString(TEXT("RbCam_DB_V01")));
		TestEqual(TEXT("trimmed"), Cameras[1], FString(TEXT("RbCam_DB_V02")));
		TestEqual(TEXT("semicolon"), Cameras[2], FString(TEXT("RbCam_DB_TH1")));
		TestEqual(TEXT("quotes dropped"), Cameras[3], FString(TEXT("RbCam_Menu_S0")));
	}
	TestEqual(TEXT("empty list"), URbHeadlessCaptureSubsystem::ParseList(TEXT("  ")).Num(), 0);
	// Console commands keep their spaces and commas, only ';' separates.
	const TArray<FString> Commands = URbHeadlessCaptureSubsystem::ParseList(TEXT("rb.Match.Break 9; RbStrike 8, 0 ;"), TEXT(";"));
	TestEqual(TEXT("command count"), Commands.Num(), 2);
	if (Commands.Num() == 2)
	{
		TestEqual(TEXT("command 1"), Commands[0], FString(TEXT("rb.Match.Break 9")));
		TestEqual(TEXT("command 2"), Commands[1], FString(TEXT("RbStrike 8, 0")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPipelineCaptureOutputPaths, "RawBreak.Unit.Pipeline.CaptureOutputPaths", RB_UNIT_TEST_FLAGS)
bool FRbPipelineCaptureOutputPaths::RunTest(const FString& Parameters)
{
	using S = URbHeadlessCaptureSubsystem;
	TestEqual(TEXT("token"), S::ResolveOutputPath(TEXT("C:/x/db2/{camera}.png"), TEXT("RbCam_DB_V01"), 6), FString(TEXT("C:/x/db2/RbCam_DB_V01.png")));
	TestEqual(TEXT("token, one camera"), S::ResolveOutputPath(TEXT("C:/x/{camera}_high.png"), TEXT("RbCam_Overhead"), 1), FString(TEXT("C:/x/RbCam_Overhead_high.png")));
	TestEqual(TEXT("single camera keeps the path"), S::ResolveOutputPath(TEXT("C:/x/shot.png"), TEXT("RbCam_Overhead"), 1), FString(TEXT("C:/x/shot.png")));
	TestEqual(TEXT("several cameras without a token"), S::ResolveOutputPath(TEXT("C:/x/shot.png"), TEXT("RbCam_Overhead"), 2), FString(TEXT("C:/x/shot_RbCam_Overhead.png")));
	TestEqual(TEXT("player view"), S::ResolveOutputPath(TEXT("C:/x/{camera}.png"), FString(), 1), FString(TEXT("C:/x/player.png")));
	TestEqual(TEXT("unsafe characters"), S::ResolveOutputPath(TEXT("C:/x/{camera}.png"), TEXT("a:b c/d"), 2), FString(TEXT("C:/x/a_b_c_d.png")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPipelinePerfStats, "RawBreak.Unit.Pipeline.PerfStats", RB_UNIT_TEST_FLAGS)
bool FRbPipelinePerfStats::RunTest(const FString& Parameters)
{
	TArray<double> Samples;
	for (int32 I = 100; I >= 1; --I) // unsorted on purpose
	{
		Samples.Add(static_cast<double>(I));
	}
	const FRbPerfSeriesStats S = URbHeadlessCaptureSubsystem::ComputeStats(Samples);
	TestEqual(TEXT("count"), S.Count, 100);
	TestNearlyEqual(TEXT("mean"), S.Mean, 50.5, 1e-12);
	TestNearlyEqual(TEXT("median (even count)"), S.Median, 50.5, 1e-12);
	TestNearlyEqual(TEXT("p95 nearest rank"), S.P95, 95.0, 1e-12);
	TestNearlyEqual(TEXT("p99 nearest rank"), S.P99, 99.0, 1e-12);
	TestNearlyEqual(TEXT("min"), S.Min, 1.0, 1e-12);
	TestNearlyEqual(TEXT("max"), S.Max, 100.0, 1e-12);

	const FRbPerfSeriesStats Odd = URbHeadlessCaptureSubsystem::ComputeStats({3.0, 1.0, 2.0});
	TestNearlyEqual(TEXT("median (odd count)"), Odd.Median, 2.0, 1e-12);
	TestNearlyEqual(TEXT("p95 of 3 = max"), Odd.P95, 3.0, 1e-12);

	const FRbPerfSeriesStats Empty = URbHeadlessCaptureSubsystem::ComputeStats({});
	TestEqual(TEXT("empty count"), Empty.Count, 0);
	TestNearlyEqual(TEXT("empty mean"), Empty.Mean, 0.0, 1e-12);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
