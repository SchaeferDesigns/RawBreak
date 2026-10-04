#include "Venue/RbVenueInfo.h"

#include "RawBreak.h"
#include "Core/RbAssetPaths.h"
#include "Dev/RbLookDevCamera.h"
#include "Game/RbGameMode.h"
#include "Game/RbTableSubsystem.h"
#include "Game/RbTestRoom.h"
#include "Table/RbTable.h"
#include "Venue/RbVenueJson.h"
#include "Venue/RbVenueLighting.h"
#include "Venue/RbVenueTableCheck.h"

#include "Camera/CameraActor.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "rb/Equipment/TableSpec.h"

// Owner: M2-A. The venue level validator (rb_make_divebar.py, rb_make_all.py) and the VDB-T1 lux report.

namespace RbVenueValidationPrivate
{
	void Line(FString& Report, bool& bOk, bool bPass, const FString& Check, const FString& Detail)
	{
		Report += FString::Printf(TEXT("%s %s: %s\n"), bPass ? TEXT("OK  ") : TEXT("FAIL"), *Check, *Detail);
		bOk &= bPass;
	}

	void Note(FString& Report, const FString& Check, const FString& Detail)
	{
		Report += FString::Printf(TEXT("INFO %s: %s\n"), *Check, *Detail);
	}


	template <class TEnum>
	FString EnumName(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	// Luminance weight of a light colour (Rec. 709) for the flash counter.
	double Luma(const FLinearColor& C)
	{
		return RbVenueLighting::Luminance(C);
	}

	// Complex (render-triangle) traces: the lamp's shades occlude their bulbs beyond the cut-off exactly like the rendered shadow does,
	// while the light leaves through the open bottom (a convex hull around a shade would block everything).
	bool LineOfSight(UWorld* World, const FVector& From, const FVector& To, const AActor* IgnoreA)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RbVenueLux), true);
		if (IgnoreA)
		{
			Params.AddIgnoredActor(IgnoreA);
		}
		const FVector Dir = (To - From).GetSafeNormal();
		const FVector End = To - Dir * 6.0; // stop short of the bulb glass / lamp internals around the source
		FHitResult Hit;
		return !World->LineTraceSingleByChannel(Hit, From, End, ECC_Visibility, Params);
	}

	struct FLampLights
	{
		TArray<const ULocalLightComponent*> Lamp;      // bulbs (analytic model positions + UE direct)
		TArray<const ULocalLightComponent*> Reflector; // reflector shares (UE direct)
		TArray<double> BulbFlux;
	};

	FLampLights CollectLampLights(const UWorld* World, const ARbVenueInfo& Info)
	{
		FLampLights Out;
		TMap<FName, const FRbVenueLight*> ById;
		for (const FRbVenueLight& L : Info.Lights)
		{
			ById.Add(RbVenueLighting::LightTag(L.Id), &L);
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			for (const FName& Tag : It->Tags)
			{
				const FRbVenueLight* const* Light = ById.Find(Tag);
				if (!Light)
				{
					continue;
				}
				const ULocalLightComponent* Component = It->FindComponentByClass<ULocalLightComponent>();
				if (!Component)
				{
					continue;
				}
				if ((*Light)->Group == Info.LampGroup)
				{
					Out.Lamp.Add(Component);
					Out.BulbFlux.Add((*Light)->Intensity);
				}
				else if ((*Light)->Group == Info.LampReflectorGroup)
				{
					Out.Reflector.Add(Component);
				}
			}
		}
		return Out;
	}
}

FString ARbVenueInfo::ComputeLuxReport(const UObject* WorldContextObject, bool& bOutOk)
{
	using namespace RbVenueValidationPrivate;
	bOutOk = true;
	FString Report;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	ARbVenueInfo* Info = World ? Find(World) : nullptr;
	URbTableSubsystem* Tables = World ? URbTableSubsystem::Get(World) : nullptr;
	ARbTable* Table = Tables ? Tables->GetPlayerTable() : nullptr;
	if (!Info || !Table || !Table->HasContext())
	{
		bOutOk = false;
		return TEXT("FAIL lux: no venue info / table with a context\n");
	}
	const rb::TableSpec& Spec = Table->GetContext().Spec;
	const FLampLights Lamp = CollectLampLights(World, *Info);
	Line(Report, bOutOk, Lamp.Lamp.Num() == 3, TEXT("lamp bulbs"), FString::Printf(TEXT("%d bulb light(s) in group %s, %d reflector share(s)"), Lamp.Lamp.Num(),
		*Info->LampGroup.ToString(), Lamp.Reflector.Num()));

	// Analytic 4.4 model on the placed bulbs.
	FRbLampModel Model;
	Model.Efficiency = Info->LampEfficiency;
	Model.CutoffDeg = Info->LampCutoffDeg;
	Model.FluxLm = Lamp.BulbFlux.Num() ? Lamp.BulbFlux[0] : 1100.0;
	for (const ULocalLightComponent* Bulb : Lamp.Lamp)
	{
		const rb::Vec3 Core = Table->WorldToCore(Bulb->GetComponentLocation());
		Model.BulbsCore.Add(FVector3d(Core.x, Core.y, Core.z));
		Note(Report, TEXT("bulb"), FString::Printf(TEXT("%s at core (%.4f, %.4f, %.4f) m, %.0f lm"), *Bulb->GetOwner()->GetName(), Core.x, Core.y, Core.z,
			Bulb->Intensity));
	}
	Report += FString::Printf(TEXT("INFO lamp model: I0 = %.1f cd (eta %.2f, cut-off %.1f deg)\n"), Model.I0(), Model.Efficiency, Model.CutoffDeg);

	TArray<FRbLuxPoint> Points = RbVenueLighting::LuxBandPoints(0.5 * Spec.Length, 0.5 * Spec.Width, Spec.RailWidthTotal, Spec.RailTopZ);
	Report += TEXT("VDB-T1 lamp-only lux (analytic 4.4 model | UE direct light of the placed lamp lights, traced) vs band:\n");
	bool bAnalyticOk = true, bDirectOk = true;
	for (FRbLuxPoint& P : Points)
	{
		P.Lux = Model.IlluminanceAt(P.Core);
		const FVector World3 = Table->CoreToWorld(rb::Vec3(P.Core.X, P.Core.Y, P.Core.Z));
		double Direct = 0.0;
		for (const TArray<const ULocalLightComponent*>* Group : {&Lamp.Lamp, &Lamp.Reflector})
		{
			for (const ULocalLightComponent* L : *Group)
			{
				if (LineOfSight(World, World3 + FVector(0, 0, 0.5), L->GetComponentLocation(), Table))
				{
					Direct += ARbTestRoom::IlluminanceFromLight(*L, World3, FVector::UpVector);
				}
			}
		}
		const bool bA = P.Lux >= P.BandMin && P.Lux <= P.BandMax;
		const bool bD = Direct >= P.BandMin && Direct <= P.BandMax;
		bAnalyticOk &= bA;
		bDirectOk &= bD;
		Report += FString::Printf(TEXT("  %-28s core (%+.3f, %+.3f, %.3f): %6.1f %s | %6.1f %s   band %.0f-%.0f\n"), P.Name, P.Core.X, P.Core.Y, P.Core.Z, P.Lux,
			bA ? TEXT("ok  ") : TEXT("OUT "), Direct, bD ? TEXT("ok  ") : TEXT("OUT "), P.BandMin, P.BandMax);
	}
	Line(Report, bOutOk, bAnalyticOk, TEXT("VDB-T1 analytic 4.4 model, every point in its band"), TEXT("see table above"));
	// The UE lights' direct light is below the bands by design: Lumen supplies the bounce off the shades' white enamel interior and the
	// reflector share carries only the measured shortfall (4.4). The in-engine gate is the rendered white card (capture_divebar.py --lux).
	Note(Report, TEXT("VDB-T1 UE lights (direct only, lamp only)"), FString::Printf(TEXT("%s; Lumen adds the shade-interior bounce - the in-engine check is the rendered "
		"white card, Tools/unreal/capture_divebar.py --lux -> Docs/images/divebar/db2/lux_report.txt"), bDirectOk ? TEXT("inside the bands") : TEXT("below the bands")));

	// Bed + rails grid (5 cm): minimum / maximum / uniformity of the analytic model.
	double Min = TNumericLimits<double>::Max(), Max = 0.0;
	const double HL = 0.5 * Spec.Length, HW = 0.5 * Spec.Width, RW = Spec.RailWidthTotal;
	for (double X = -HL - RW; X <= HL + RW + 1e-9; X += 0.05)
	{
		for (double Y = -HW - RW; Y <= HW + RW + 1e-9; Y += 0.05)
		{
			const bool bBed = FMath::Abs(X) <= HL && FMath::Abs(Y) <= HW;
			const double E = Model.IlluminanceAt(FVector3d(X, Y, bBed ? 0.0 : Spec.RailTopZ));
			Min = FMath::Min(Min, E);
			Max = FMath::Max(Max, E);
		}
	}
	Line(Report, bOutOk, Min >= 90.0, TEXT("VDB-T1 bed + rails minimum >= 90 lux"), FString::Printf(TEXT("min %.1f, max %.1f, U0 %.2f (spec 99 / 828 / 0.12)"), Min, Max,
		Max > 0.0 ? Min / Max : 0.0));
	Note(Report, TEXT("WPA"), FString::Printf(TEXT(">= 520 lux everywhere: %s (failed by design, 4.4)"), Min >= 520.0 ? TEXT("met") : TEXT("not met")));

	// All venue lights at their current intensity (informational).
	double AllCentre = 0.0;
	const FVector Centre = Table->GetBedCenterWorld();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<ULocalLightComponent*> Components(*It);
		for (const ULocalLightComponent* L : Components)
		{
			if (L->IsVisible() && LineOfSight(World, Centre + FVector(0, 0, 0.5), L->GetComponentLocation(), Table))
			{
				AllCentre += ARbTestRoom::IlluminanceFromLight(*L, Centre, FVector::UpVector);
			}
		}
	}
	Note(Report, TEXT("all lights (informational)"), FString::Printf(TEXT("bed centre %.1f lux (direct, every visible light)"), AllCentre));
	return Report;
}

FString ARbVenueInfo::ValidateVenueLevel(const UObject* WorldContextObject, bool& bOutOk)
{
	using namespace RbVenueValidationPrivate;
	bOutOk = true;
	FString R;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		bOutOk = false;
		return TEXT("FAIL world: no world\n");
	}

	// --- venue info --------------------------------------------------------------------------------------------------------------
	TArray<ARbVenueInfo*> Infos;
	for (TActorIterator<ARbVenueInfo> It(World); It; ++It)
	{
		Infos.Add(*It);
	}
	Line(R, bOutOk, Infos.Num() == 1, TEXT("venue info"), FString::Printf(TEXT("%d ARbVenueInfo actor(s)"), Infos.Num()));
	ARbVenueInfo* Info = Infos.Num() ? Infos[0] : nullptr;
	if (!Info)
	{
		return R;
	}
	Line(R, bOutOk, Info->Venue == ERbVenue::DiveBar && Info->VenueKind == ERbVenueKind::DiveBar && FMath::IsNearlyEqual(Info->Age, 0.8f, 1e-4f), TEXT("venue identity"),
		FString::Printf(TEXT("%s / %s, Age %.2f, seed %lld"), *EnumName(Info->Venue), *EnumName(Info->VenueKind), Info->Age, Info->VenueSeed));

	FRbJson Layout;
	FString LayoutError;
	const bool bLayout = FRbJson::LoadProjectFile(TEXT("Art/DiveBar/layout.json"), Layout, &LayoutError);
	Line(R, bOutOk, bLayout, TEXT("layout.json"), bLayout ? FString(TEXT("parsed")) : LayoutError);

	// --- game mode, player start ----------------------------------------------------------------------------------------------------
	const AWorldSettings* Settings = World->GetWorldSettings();
	const UClass* GameMode = Settings ? Settings->DefaultGameMode.Get() : nullptr;
	Line(R, bOutOk, GameMode && GameMode->IsChildOf(ARbGameMode::StaticClass()), TEXT("world settings game mode"), GameMode ? GameMode->GetPathName() : FString(TEXT("none")));

	TArray<APlayerStart*> Starts;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Starts.Add(*It);
	}
	if (Starts.Num() == 1 && bLayout)
	{
		const FRbJson& PS = Layout[TEXT("player_start")];
		const FVector Want = PS[TEXT("location")].AsVector(100.0);
		const FVector Have = Starts[0]->GetActorLocation();
		const bool bPass = FVector::Dist2D(Want, Have) < 1.0 && FMath::Abs(FRotator::NormalizeAxis(Starts[0]->GetActorRotation().Yaw - PS.GetNumber(TEXT("yaw_deg")))) < 1.0;
		Line(R, bOutOk, bPass, TEXT("player start at the head end facing +X"), FString::Printf(TEXT("%s yaw %.1f"), *Have.ToString(), Starts[0]->GetActorRotation().Yaw));
	}
	else
	{
		Line(R, bOutOk, false, TEXT("player start"), FString::Printf(TEXT("%d PlayerStart actor(s) (expected 1)"), Starts.Num()));
	}

	// --- tables (VDB-T10) ------------------------------------------------------------------------------------------------------
	URbTableSubsystem* TableSubsystem = URbTableSubsystem::Get(World);
	FString TableReport;
	const bool bTables = TableSubsystem && TableSubsystem->ValidateTables(TableReport);
	Line(R, bOutOk, bTables, TEXT("URbTableSubsystem::ValidateTables"), TableReport.Replace(TEXT("\n"), TEXT(" | ")));
	if (bLayout && TableSubsystem)
	{
		const TArray<FRbJson>& LayoutTables = Layout[TEXT("tables")].Array;
		Line(R, bOutOk, LayoutTables.Num() == TableSubsystem->GetTables().Num(), TEXT("table count == layout.json"), FString::Printf(TEXT("%d in the level, %d in layout.json"),
			TableSubsystem->GetTables().Num(), LayoutTables.Num()));
		for (const FRbJson& T : LayoutTables)
		{
			const int32 Index = static_cast<int32>(T.GetNumber(TEXT("table_index")));
			ARbTable* Table = TableSubsystem->FindTable(Index);
			const FString Name = FString::Printf(TEXT("table %d"), Index);
			if (!Table)
			{
				Line(R, bOutOk, false, Name, TEXT("missing"));
				continue;
			}
			const FVector Want = T[TEXT("location_m")].AsVector(100.0);
			const bool bPlace = Table->GetActorLocation().Equals(Want, 0.01) && FMath::IsNearlyEqual(Table->GetActorRotation().Yaw, T.GetNumber(TEXT("yaw_deg")), 1e-3);
			Line(R, bOutOk, bPlace, Name + TEXT(" placement"), FString::Printf(TEXT("%s yaw %.4f (layout %s)"), *Table->GetActorLocation().ToString(), Table->GetActorRotation().Yaw,
				*Want.ToString()));
			const bool bPreset = EnumName(Table->Preset) == T.GetString(TEXT("preset")) && EnumName(Table->BallSet) == T.GetString(TEXT("ball_set"));
			Line(R, bOutOk, bPreset, Name + TEXT(" preset / ball set"), FString::Printf(TEXT("%s / %s"), *EnumName(Table->Preset), *EnumName(Table->BallSet)));
			const bool bPlayerTag = !T.GetBool(TEXT("player_table")) || Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable);
			Line(R, bOutOk, bPlayerTag, Name + TEXT(" player table tag"), Table->ActorHasTag(RbAssetPaths::Tag::PlayerTable) ? TEXT("RbPlayerTable") : TEXT("untagged"));
			const int64 Seed = static_cast<int64>(T.GetNumber(TEXT("venue_seed")));
			const bool bFirst = T.GetBool(TEXT("first_career_table"));
			const bool bVenue = Table->bUseVenueCondition && Table->VenueKind == ERbVenueKind::DiveBar && Table->VenueSeed == Seed && Info->VenueSeed == Seed &&
				Table->bFirstCareerTable == bFirst;
			Line(R, bOutOk, bVenue, Name + TEXT(" venue condition"), FString::Printf(TEXT("use %d, kind %s, seed %lld (layout %lld, venue info %lld), first career table %d"),
				Table->bUseVenueCondition ? 1 : 0, *EnumName(Table->VenueKind), Table->VenueSeed, Seed, Info->VenueSeed, Table->bFirstCareerTable ? 1 : 0));
			const double Lamp = T.GetNumber(TEXT("lamp_underside_height_m"));
			Line(R, bOutOk, FMath::IsNearlyEqual(Table->LampUndersideHeight, Lamp, 1e-9), Name + TEXT(" LampUndersideHeight (VDB-T10)"),
				FString::Printf(TEXT("%.4f m (layout %.4f)"), Table->LampUndersideHeight, Lamp));
			const FRbJson& Footprint = T[TEXT("lamp_footprint_core_m")];
			const FRbJson& Lo = Footprint[TEXT("min")];
			const FRbJson& Hi = Footprint[TEXT("max")];
			const bool bFoot = Table->bUseLampFootprint && FMath::IsNearlyEqual(Table->LampFootprintMin.X, Lo[0].AsNumber(), 1e-9) &&
				FMath::IsNearlyEqual(Table->LampFootprintMin.Y, Lo[1].AsNumber(), 1e-9) && FMath::IsNearlyEqual(Table->LampFootprintMax.X, Hi[0].AsNumber(), 1e-9) &&
				FMath::IsNearlyEqual(Table->LampFootprintMax.Y, Hi[1].AsNumber(), 1e-9);
			Line(R, bOutOk, bFoot, Name + TEXT(" lamp footprint"), FString::Printf(TEXT("use %d, x [%.3f, %.3f] y [%.3f, %.3f]"), Table->bUseLampFootprint ? 1 : 0,
				Table->LampFootprintMin.X, Table->LampFootprintMax.X, Table->LampFootprintMin.Y, Table->LampFootprintMax.Y));
			const double Bed = T.GetNumber(TEXT("bed_height_m"));
			const bool bBed = Table->HasContext() && FMath::IsNearlyEqual(Table->GetContext().BedHeight(), Bed, 1e-9);
			Line(R, bOutOk, bBed, Name + TEXT(" bed height == FRbTableContext::BedHeight()"), Table->HasContext() ?
				FString::Printf(TEXT("%.4f m (layout %.4f)"), Table->GetContext().BedHeight(), Bed) : FString(TEXT("no table context")));
			for (const bool bFirstCareer : {false, true})
			{
				const FRbRollOffCheck Roll = RbVenueTableCheck::CheckRollOff(Table->Preset, Table->BallSet, Table->VenueKind, Seed, Index, bFirstCareer,
					RbVenueTableCheck::DiveBarRollOffAzimuthCoreDeg, T.GetNumber(TEXT("roll_off_tolerance_deg")));
				Line(R, bOutOk, Roll.bPass, FString::Printf(TEXT("%s roll-off toward the jukebox (VDB-T10, FirstCareerTable %d)"), *Name, bFirstCareer ? 1 : 0), Roll.ToString());
			}
		}
	}

	// --- cameras (12.2, 12.3, UX 6.3; VDB-T12 part) ------------------------------------------------------------------------------
	if (bLayout)
	{
		const FRbJson& Cameras = Layout[TEXT("cameras")];
		int32 Checked = 0, Wrong = 0;
		FString Bad;
		for (const TCHAR* Set : {TEXT("views"), TEXT("trailer"), TEXT("menu")})
		{
			for (const FRbJson& C : Cameras[Set].Array)
			{
				const FName Tag(*C.GetString(TEXT("tag")));
				const FVector Eye = C[TEXT("eye")].AsVector(100.0);
				int32 LookDev = 0, Plain = 0;
				bool bPose = false;
				for (TActorIterator<ACameraActor> It(World); It; ++It)
				{
					if (It->Tags.Contains(Tag))
					{
						const ARbLookDevCamera* LookDevCamera = Cast<ARbLookDevCamera>(*It);
						(LookDevCamera ? LookDev : Plain) += 1;
						// A chin-on-cue camera places its eye from the cue axis (plan 4.2): the layout eye is only its authoring point.
						bPose = FVector::Dist(It->GetActorLocation(), Eye) < 1.0 ||
							(LookDevCamera && LookDevCamera->Placement == ERbLookDevPlacement::ChinOnCue && C.Has(TEXT("chin_on_cue")));
					}
				}
				++Checked;
				if (LookDev != 1 || Plain != 0 || !bPose)
				{
					++Wrong;
					Bad += FString::Printf(TEXT(" %s(%d/%d/%s)"), *Tag.ToString(), LookDev, Plain, bPose ? TEXT("pose") : TEXT("POSE"));
				}
			}
		}
		// The contract tags of RbAssetPaths::CaptureCamera must all be covered.
		int32 Missing = 0;
		auto Has = [&World](const FString& Tag)
		{
			for (TActorIterator<ARbLookDevCamera> It(World); It; ++It)
			{
				if (It->Tags.Contains(FName(*Tag)))
				{
					return true;
				}
			}
			return false;
		};
		for (int32 I = 1; I <= 12; ++I) { Missing += Has(RbAssetPaths::CaptureCamera::DiveBarView(I)) ? 0 : 1; }
		for (int32 I = 1; I <= 7; ++I) { Missing += Has(RbAssetPaths::CaptureCamera::DiveBarTrailer(I)) ? 0 : 1; }
		for (int32 I = 0; I <= 7; ++I) { Missing += Has(RbAssetPaths::CaptureCamera::MenuStation(I)) ? 0 : 1; }
		Line(R, bOutOk, Wrong == 0 && Missing == 0, TEXT("capture cameras V01-V12, TH1-TH7, menu stations S0-S7"),
			FString::Printf(TEXT("%d checked (ARbLookDevCamera, layout pose within 1 cm), %d wrong%s, %d contract tag(s) missing"), Checked, Wrong, *Bad, Missing));
	}

	// --- lights: VDB-T11 flags, photometry, ramps; VDB-T8 photosensitivity -----------------------------------------------------
	{
		TMap<FName, const ULocalLightComponent*> ByTag;
		int32 AllLights = 0, VolShadowBad = 0, SourceBad = 0, Orphans = 0;
		FString VolBad, SrcBad;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<ULocalLightComponent*> Components(*It);
			for (const ULocalLightComponent* L : Components)
			{
				++AllLights;
				if (L->VolumetricScatteringIntensity > 0.0f && !L->bCastVolumetricShadow)
				{
					++VolShadowBad;
					VolBad += TEXT(" ") + It->GetName();
				}
				bool bSourceOk = true;
				if (const URectLightComponent* Rect = Cast<URectLightComponent>(L))
				{
					bSourceOk = Rect->SourceWidth > 0.0f && Rect->SourceHeight > 0.0f;
				}
				else if (const UPointLightComponent* Point = Cast<UPointLightComponent>(L))
				{
					bSourceOk = Point->SourceRadius > 0.0f || Point->SourceLength > 0.0f;
				}
				if (!bSourceOk)
				{
					++SourceBad;
					SrcBad += TEXT(" ") + It->GetName();
				}
				bool bTagged = false;
				for (const FName& Tag : It->Tags)
				{
					if (Tag.ToString().StartsWith(TEXT("LT_DB_")))
					{
						ByTag.Add(Tag, L);
						bTagged = true;
					}
				}
				Orphans += bTagged ? 0 : 1;
			}
		}
		Line(R, bOutOk, VolShadowBad == 0, TEXT("VDB-T11 volumetric scattering > 0 casts volumetric shadows"), FString::Printf(TEXT("%d light(s), %d violation(s)%s"), AllLights,
			VolShadowBad, *VolBad));
		Line(R, bOutOk, SourceBad == 0, TEXT("VDB-T11 no source radius / length / size of 0"), FString::Printf(TEXT("%d violation(s)%s"), SourceBad, *SrcBad));
		Line(R, bOutOk, Orphans == 0, TEXT("every light is a venue light (LT_DB_<Id>)"), FString::Printf(TEXT("%d untagged light(s)"), Orphans));

		int32 Missing = 0, NeonBad = 0, EnclosedBad = 0, RampBad = 0, FlashBad = 0;
		FString Details;
		double WorstFlash = 0.0;
		for (const FRbVenueLight& Light : Info->Lights)
		{
			const ULocalLightComponent* const* Found = ByTag.Find(RbVenueLighting::LightTag(Light.Id));
			if (!Found)
			{
				++Missing;
				Details += FString::Printf(TEXT(" missing:%s"), *Light.Id.ToString());
				continue;
			}
			const ULocalLightComponent* L = *Found;
			if (Light.TubeFluxLm > 0.0f)
			{
				// Photometric flux of the proxy: UE scales the lumens by the linear light colour (not normalised), so a red-gold proxy
				// carries Intensity x Y(colour); the proxy must be coloured like its gas mix (4.3: "per gas"; a white proxy lit the
				// walls white while the tubes glowed red / blue).
				const double Want = RbVenueLighting::NeonProxyFlux(Light.TubeFluxLm);
				const FLinearColor Color = L->bUseTemperature ? FLinearColor::White : L->GetLightColor();
				const double Photometric = Light.Intensity * RbVenueLighting::Luminance(Color);
				const bool bFlux = L->IntensityUnits == ELightUnits::Lumens && FMath::Abs(Photometric / Want - 1.0) <= 0.05 &&
					FMath::Abs(L->Intensity - Light.Intensity * Light.OpenFactor) <= 0.01 * FMath::Max(1.0f, Light.Intensity);
				const bool bSpec = FMath::IsNearlyZero(L->SpecularScale);
				const bool bColoured = Color.GetMax() - FMath::Min3(Color.R, Color.G, Color.B) >= 0.15f;
				if (!bFlux || !bSpec || !bColoured)
				{
					++NeonBad;
					Details += FString::Printf(TEXT(" neon:%s(%.1f lm x Y %.3f = %.1f vs %.1f, spec %.2f, colour %s)"), *Light.Id.ToString(), L->Intensity,
						RbVenueLighting::Luminance(Color), Photometric, Want, L->SpecularScale, *Color.ToString());
				}
			}
			if ((Light.bOutside || Light.bEnclosed) && L->VolumetricScatteringIntensity > 0.0f)
			{
				++EnclosedBad;
				Details += FString::Printf(TEXT(" scatter:%s"), *Light.Id.ToString());
			}
			if (Light.RampSeconds < RbVenueLighting::MinRampSeconds)
			{
				++RampBad;
			}
			if (Light.Animation != ERbVenueLightAnimation::None)
			{
				// VDB-T8: 180 s at 60 Hz of the light's luminance (intensity x colour luma).
				TArray<double> Samples;
				Samples.Reserve(180 * 60);
				for (int32 I = 0; I < 180 * 60; ++I)
				{
					const double T = I / 60.0;
					Samples.Add(RbVenueLighting::AnimationFactor(Light, T) * Luma(RbVenueLighting::AnimationColor(Light, T)));
				}
				const double Flashes = RbVenueLighting::MaxFlashesPerSecondOf(Samples, 60.0);
				WorstFlash = FMath::Max(WorstFlash, Flashes);
				const bool bInterval = Light.Animation != ERbVenueLightAnimation::Headlights || Light.IntervalRange.X >= 20.0;
				if (Flashes > RbVenueLighting::MaxFlashesPerSecond || !bInterval)
				{
					++FlashBad;
					Details += FString::Printf(TEXT(" flash:%s(%.0f/s)"), *Light.Id.ToString(), Flashes);
				}
			}
		}
		Line(R, bOutOk, Missing == 0 && Info->Lights.Num() > 0, TEXT("venue lights bound (lights.json)"), FString::Printf(TEXT("%d listed, %d missing"), Info->Lights.Num(), Missing));
		Line(R, bOutOk, NeonBad == 0, TEXT("VDB-T11 neon proxies: SpecularScale 0, photometric flux = tube flux / pi (+-5 %), coloured like the gases"),
			FString::Printf(TEXT("%d violation(s)"), NeonBad));
		Line(R, bOutOk, EnclosedBad == 0, TEXT("VDB-T11 outside / enclosed lights scatter 0"), FString::Printf(TEXT("%d violation(s)"), EnclosedBad));
		Line(R, bOutOk, RampBad == 0, TEXT("VDB-T11 / T12 every state change ramps >= 0.8 s"), FString::Printf(TEXT("%d light(s) below"), RampBad));
		Line(R, bOutOk, FlashBad == 0, TEXT("VDB-T8 photosensitivity (<= 3 flashes / s, headlights >= 20 s apart)"), FString::Printf(TEXT("worst %.0f flash(es) / s%s"), WorstFlash,
			*Details));
	}

	// --- lighting sublevels (4.6, VDB-T12) -----------------------------------------------------------------------------------------
	{
		TArray<FString> Names;
		for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming)
			{
				Names.Add(FPackageName::GetShortName(Streaming->GetWorldAssetPackageName()));
			}
		}
		int32 Missing = 0;
		for (const TCHAR* Want : {TEXT("L_DiveBar_Geo"), TEXT("L_DiveBar_Light_Open"), TEXT("L_DiveBar_Light_LightsUp"), TEXT("L_DiveBar_Light_AfterHours")})
		{
			Missing += Names.Contains(Want) ? 0 : 1;
		}
		Line(R, bOutOk, Missing == 0, TEXT("sublevels _Geo, _Light_Open, _Light_LightsUp, _Light_AfterHours"), FString::Printf(TEXT("[%s]"), *FString::Join(Names, TEXT(", "))));
	}

	// --- geometry, collision, ball return, ceiling, fog, audio anchors --------------------------------------------------------------
	{
		int32 Geo = 0, ProfileBad = 0, Ceiling = 0, Return = 0, HeightFog = 0, LocalFog = 0;
		FString Bad;
		TSet<FName> AllTags;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			for (const FName& Tag : It->Tags)
			{
				AllTags.Add(Tag);
			}
			Ceiling += It->ActorHasTag(CeilingTag) ? 1 : 0;
			if (It->ActorHasTag(GeometryTag))
			{
				++Geo;
				TInlineComponentArray<UPrimitiveComponent*> Prims(*It);
				for (const UPrimitiveComponent* P : Prims)
				{
					const FName Profile = P->GetCollisionProfileName();
					if (P->IsCollisionEnabled() && Profile != RbAssetPaths::Collision::VenueBlockProfile && Profile != RbAssetPaths::Collision::VenuePropProfile)
					{
						++ProfileBad;
						Bad += FString::Printf(TEXT(" %s(%s)"), *It->GetName(), *Profile.ToString());
					}
				}
			}
			if (It->ActorHasTag(RbAssetPaths::Tag::BallReturnVolume))
			{
				TInlineComponentArray<UPrimitiveComponent*> Prims(*It);
				for (const UPrimitiveComponent* P : Prims)
				{
					Return += P->GetCollisionProfileName() == RbAssetPaths::Collision::BallReturnProfile ? 1 : 0;
				}
			}
			if (const UExponentialHeightFogComponent* Fog = It->FindComponentByClass<UExponentialHeightFogComponent>())
			{
				HeightFog += Fog->bEnableVolumetricFog ? 1 : 0;
			}
			LocalFog += It->FindComponentByClass<ULocalFogVolumeComponent>() ? 1 : 0;
		}
		Line(R, bOutOk, Geo > 0 && ProfileBad == 0, TEXT("venue geometry collision profiles RbVenueBlock / RbVenueProp"), FString::Printf(TEXT("%d generated actor(s), %d bad%s"),
			Geo, ProfileBad, *Bad.Left(600)));
		Line(R, bOutOk, Ceiling > 0, TEXT("ceiling actors tagged RbDB_Ceiling (V10)"), FString::Printf(TEXT("%d"), Ceiling));
		Line(R, bOutOk, Return >= 1, TEXT("RbBallReturn volume behind the bar (13.5)"), FString::Printf(TEXT("%d volume(s) with profile RbBallReturn"), Return));
		Line(R, bOutOk, HeightFog == 1 && LocalFog >= 1, TEXT("haze (4.7): height fog with volumetric fog + local fog volume"), FString::Printf(TEXT("%d height fog, %d local fog"),
			HeightFog, LocalFog));
		if (bLayout)
		{
			int32 MissingAnchors = 0;
			FString Which;
			for (const FRbJson& Anchor : Layout[TEXT("audio_anchors")].Array)
			{
				const FString AnchorName = Anchor.GetString(TEXT("anchor"));
				if (!AllTags.Contains(RbAssetPaths::Tag::AudioAnchor(*AnchorName)))
				{
					++MissingAnchors;
					Which += TEXT(" ") + AnchorName;
				}
			}
			Line(R, bOutOk, MissingAnchors == 0, TEXT("audio anchors RbAudio_<Anchor> (venue-dive-bar 10)"), FString::Printf(TEXT("%d missing%s"), MissingAnchors, *Which));
		}
	}

	// --- VDB-T1 lux (lamp only) ------------------------------------------------------------------------------------------------------
	{
		bool bLux = true;
		const FString Lux = ComputeLuxReport(World, bLux);
		for (const FString& L : TArray<FString>{Lux})
		{
			R += L;
		}
		bOutOk &= bLux;
	}
	return R;
}
