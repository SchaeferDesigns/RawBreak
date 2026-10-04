#include "Audio/RbAudioVenue.h"

#include "Audio/RbAudioAssets.h"
#include "Core/RbAssetPaths.h"

// Owner: M2-C. Levels and positions are ESTIMATES (audio.md 2.7 / 12.1, venue-dive-bar 2.3 / 10) until the recording session
// (audio.md 3.8, UE 8.5). World positions in cm; the dive bar's world frame is the venue frame V (venue-dive-bar 2.1).

namespace
{
	FRbAmbienceEmitter MakeEmitter(const TCHAR* Name, RbAudio::EAmbienceLayer Layer, double LevelDbA, uint64 Seed, const FVector& LocationM,
		const TCHAR* Anchor)
	{
		FRbAmbienceEmitter E;
		E.Name = Name;
		E.Layer.Layer = Layer;
		E.Layer.LevelDbA = LevelDbA;
		E.Layer.Seed = Seed;
		E.Layer.MainsHz = 60.0; // US venues (neon buzz at 120 Hz); the German Kneipe later: 50
		E.DefaultLocationCm = LocationM * 100.0;
		E.Anchor = Anchor;
		return E;
	}
}

FRbVenueAudioProfile RbGetVenueAudioProfile(ERbVenue Venue)
{
	using RbAudio::EAmbienceLayer;
	FRbVenueAudioProfile P;
	P.Venue = Venue;
	if (Venue == ERbVenue::DiveBar)
	{
		// The Low Bridge Tavern at night, empty-ish (M2 has no crowd / jukebox yet): room tone 38-42 dBA at the table (AU-70),
		// ceiling diffusers (AU-71, 45 dBA near a vent), the under-counter coolers of the back bar E04 with their duty cycles
		// (AU-72, 44 dBA at 2 m), the neon transformers (AU-74, 35-40 dBA at 1 m; N3 above the back-bar mirror, N4 POOL above the
		// cue rack, N5 on the back wall).
		P.Floor = RbAudio::EFloorSurface::Vct;
		P.Rt60[0] = 0.8;
		P.Rt60[1] = 0.6;
		P.Rt60[2] = 0.5;
		P.VoiceReverbSend = 1.0f;
		P.AmbienceReverbSend = 1.0f;
		P.Emitters.Add(MakeEmitter(TEXT("RoomTone"), EAmbienceLayer::HvacBed, 36.0, 0xD1B0'0001ull, FVector::ZeroVector, nullptr));
		P.Emitters.Add(MakeEmitter(TEXT("DiffuserPool"), EAmbienceLayer::HvacDiffuser, 42.0, 0xD1B0'0002ull, FVector(12.20, 2.60, 2.72),
			RbAudioAssets::AnchorHvac));
		P.Emitters.Add(MakeEmitter(TEXT("DiffuserBar"), EAmbienceLayer::HvacDiffuser, 42.0, 0xD1B0'0003ull, FVector(5.50, 3.00, 2.72),
			RbAudioAssets::AnchorHvac));
		FRbAmbienceEmitter Cooler3 = MakeEmitter(TEXT("Cooler3Door"), EAmbienceLayer::Compressor, 50.0, 0xD1B0'0004ull, FVector(3.10, 0.35, 0.30),
			RbAudioAssets::AnchorCooler);
		Cooler3.Layer.bStartOn = true;
		P.Emitters.Add(Cooler3);
		FRbAmbienceEmitter Cooler2 = MakeEmitter(TEXT("Cooler2Door"), EAmbienceLayer::Compressor, 50.0, 0xD1B0'0005ull, FVector(7.90, 0.35, 0.30),
			RbAudioAssets::AnchorCooler);
		Cooler2.Layer.bStartOn = false;
		P.Emitters.Add(Cooler2);
		P.Emitters.Add(MakeEmitter(TEXT("NeonHollenbeck"), EAmbienceLayer::NeonHum, 38.0, 0xD1B0'0006ull, FVector(8.50, 0.05, 2.40),
			RbAudioAssets::AnchorNeon));
		P.Emitters.Add(MakeEmitter(TEXT("NeonPool"), EAmbienceLayer::NeonHum, 36.0, 0xD1B0'0007ull, FVector(13.80, 7.28, 2.18),
			RbAudioAssets::AnchorNeon));
		P.Emitters.Add(MakeEmitter(TEXT("NeonLanternFlats"), EAmbienceLayer::NeonHum, 35.0, 0xD1B0'0008ull, FVector(16.44, 2.90, 1.68),
			RbAudioAssets::AnchorNeon));
	}
	else
	{
		// The M1 test room (6.0 x 4.8 x 2.8 m around the origin, concrete, painted block): an air-handler bed and one diffuser.
		P.Floor = RbAudio::EFloorSurface::Concrete;
		P.Rt60[0] = 0.7;
		P.Rt60[1] = 0.55;
		P.Rt60[2] = 0.45;
		P.VoiceReverbSend = 1.0f;
		P.AmbienceReverbSend = 1.0f;
		P.Emitters.Add(MakeEmitter(TEXT("RoomTone"), EAmbienceLayer::HvacBed, 34.0, 0x7E57'0001ull, FVector::ZeroVector, nullptr));
		P.Emitters.Add(MakeEmitter(TEXT("Diffuser"), EAmbienceLayer::HvacDiffuser, 40.0, 0x7E57'0002ull, FVector(-2.0, 1.6, 2.78),
			RbAudioAssets::AnchorHvac));
	}
	return P;
}

const TCHAR* RbAmbienceAnchorKind(const FString& AnchorName)
{
	// Prefixes per kind: the profile's own names and the dive-bar layout's (M2-A layout.json "audio_anchors").
	struct FAlias
	{
		const TCHAR* Prefix;
		const TCHAR* Kind;
	};
	static const FAlias Aliases[] = {
		{TEXT("Hvac"), RbAudioAssets::AnchorHvac},
		{TEXT("RoomTone"), RbAudioAssets::AnchorHvac},
		{TEXT("Cooler"), RbAudioAssets::AnchorCooler},
		{TEXT("Neon"), RbAudioAssets::AnchorNeon},
	};
	for (const FAlias& A : Aliases)
	{
		if (AnchorName.StartsWith(A.Prefix, ESearchCase::IgnoreCase))
		{
			return A.Kind;
		}
	}
	return nullptr;
}

void RbPlaceAmbience(const FRbVenueAudioProfile& Profile, TConstArrayView<FRbAudioAnchor> Anchors, TArray<FRbAmbiencePlacement>& Out)
{
	Out.Reset();
	auto SameKind = [](const TCHAR* A, const TCHAR* B) { return A && B && FCString::Strcmp(A, B) == 0; };
	auto Default = [&Profile](int32 E)
	{
		FRbAmbiencePlacement P;
		P.Layer = Profile.Emitters[E].Layer;
		P.LocationCm = Profile.Emitters[E].DefaultLocationCm;
		P.Name = Profile.Emitters[E].Name;
		P.Emitter = E;
		return P;
	};
	TArray<const TCHAR*> KindsDone;
	for (int32 E = 0; E < Profile.Emitters.Num(); ++E)
	{
		const TCHAR* Kind = Profile.Emitters[E].Anchor;
		if (!Kind)
		{
			Out.Add(Default(E));
			continue;
		}
		if (KindsDone.ContainsByPredicate([&](const TCHAR* K) { return SameKind(K, Kind); }))
		{
			continue; // placed with the first emitter of its kind
		}
		KindsDone.Add(Kind);
		TArray<int32> Emitters; // the profile's emitters of this kind
		for (int32 I = E; I < Profile.Emitters.Num(); ++I)
		{
			if (SameKind(Profile.Emitters[I].Anchor, Kind))
			{
				Emitters.Add(I);
			}
		}
		TArray<const FRbAudioAnchor*> Level; // the level's anchors of this kind, by name (deterministic)
		for (const FRbAudioAnchor& A : Anchors)
		{
			if (SameKind(RbAmbienceAnchorKind(A.Name), Kind))
			{
				Level.Add(&A);
			}
		}
		Level.StableSort([](const FRbAudioAnchor& L, const FRbAudioAnchor& R) { return L.Name < R.Name; });
		if (Level.Num() > RbMaxAmbienceAnchorsPerKind)
		{
			Level.SetNum(RbMaxAmbienceAnchorsPerKind);
		}
		if (Level.Num() == 0)
		{
			for (const int32 I : Emitters)
			{
				Out.Add(Default(I));
			}
			continue;
		}
		// The designed layers at their anchors: (emitter, anchor) pairs taken closest first (each emitter and anchor once), so every
		// layer lands at the anchor that is its own in the layout and, with fewer anchors than layers, the nearest designs stay.
		struct FPair
		{
			double DistSq;
			int32 EmitterSlot;
			int32 AnchorSlot;
		};
		TArray<FPair> Pairs;
		for (int32 E2 = 0; E2 < Emitters.Num(); ++E2)
		{
			for (int32 A = 0; A < Level.Num(); ++A)
			{
				Pairs.Add({FVector::DistSquared(Level[A]->LocationCm, Profile.Emitters[Emitters[E2]].DefaultLocationCm), E2, A});
			}
		}
		Pairs.StableSort([](const FPair& L, const FPair& R) { return L.DistSq < R.DistSq; });
		TArray<bool> Taken;
		Taken.SetNumZeroed(Level.Num());
		TArray<int32> AnchorOf;
		AnchorOf.Init(INDEX_NONE, Emitters.Num());
		for (const FPair& Pair : Pairs)
		{
			if (!Taken[Pair.AnchorSlot] && AnchorOf[Pair.EmitterSlot] == INDEX_NONE)
			{
				Taken[Pair.AnchorSlot] = true;
				AnchorOf[Pair.EmitterSlot] = Pair.AnchorSlot;
			}
		}
		for (int32 E2 = 0; E2 < Emitters.Num(); ++E2)
		{
			if (AnchorOf[E2] == INDEX_NONE)
			{
				continue; // fewer anchors than designed layers: the level decides
			}
			FRbAmbiencePlacement P = Default(Emitters[E2]);
			P.LocationCm = Level[AnchorOf[E2]]->LocationCm;
			P.bAtAnchor = true;
			Out.Add(P);
		}
		// Further anchors: the layer of the nearest designed emitter, its own seed (never two identical hums / duty cycles).
		for (int32 A = 0; A < Level.Num(); ++A)
		{
			if (Taken[A])
			{
				continue;
			}
			int32 Nearest = Emitters[0];
			double BestD = TNumericLimits<double>::Max();
			for (const int32 I : Emitters)
			{
				const double D = FVector::DistSquared(Level[A]->LocationCm, Profile.Emitters[I].DefaultLocationCm);
				if (D < BestD)
				{
					Nearest = I;
					BestD = D;
				}
			}
			FRbAmbiencePlacement P = Default(Nearest);
			P.Layer.Seed = RbAudio::HashMix(P.Layer.Seed, 0xA4C0'0000ull + static_cast<uint64>(A) + 1);
			P.Layer.bStartOn = !P.Layer.bStartOn;
			P.LocationCm = Level[A]->LocationCm;
			P.Name = FString::Printf(TEXT("%s_%s"), *Profile.Emitters[Nearest].Name, *Level[A]->Name);
			P.bAtAnchor = true;
			Out.Add(P);
		}
	}
}

RbAudio::EFloorSurface RbFloorSurfaceFor(EPhysicalSurface Surface, RbAudio::EFloorSurface VenueDefault)
{
	if (Surface == RbAssetPaths::Surface::Vct)
	{
		return RbAudio::EFloorSurface::Vct;
	}
	if (Surface == RbAssetPaths::Surface::Rubber)
	{
		return RbAudio::EFloorSurface::Rubber;
	}
	if (Surface == RbAssetPaths::Surface::Wood)
	{
		return RbAudio::EFloorSurface::Wood;
	}
	if (Surface == RbAssetPaths::Surface::Concrete)
	{
		return RbAudio::EFloorSurface::Concrete;
	}
	return VenueDefault;
}
