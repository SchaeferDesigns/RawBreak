#include "Core/RbTypes.h"

#include "Core/RbAssetPaths.h"

#include "rb/Human/AiProfiles.h"
#include "rb/Human/Skill.h"
#include "rb/Human/Venue.h"

namespace RbTypes
{
	rb::TablePreset ToCore(ERbTablePreset Preset)
	{
		switch (Preset)
		{
		case ERbTablePreset::NineFootPro: return rb::TablePreset::NineFootPro;
		case ERbTablePreset::NineFootTight: return rb::TablePreset::NineFootTight;
		case ERbTablePreset::EightFootPro: return rb::TablePreset::EightFootPro;
		case ERbTablePreset::EightFootHome: return rb::TablePreset::EightFootHome;
		case ERbTablePreset::SevenFootBar: return rb::TablePreset::SevenFootBar;
		case ERbTablePreset::SevenFoot78: return rb::TablePreset::SevenFoot78;
		case ERbTablePreset::SevenFootTrue: return rb::TablePreset::SevenFootTrue;
		}
		return rb::TablePreset::NineFootPro;
	}

	rb::BallSetPreset ToCore(ERbBallSetPreset Preset)
	{
		switch (Preset)
		{
		case ERbBallSetPreset::StandardPool: return rb::BallSetPreset::StandardPool;
		case ERbBallSetPreset::DiveBar: return rb::BallSetPreset::DiveBar;
		case ERbBallSetPreset::OldBarOversizedCue: return rb::BallSetPreset::OldBarOversizedCue;
		}
		return rb::BallSetPreset::StandardPool;
	}

	rb::CuePreset ToCore(ERbCuePreset Preset)
	{
		switch (Preset)
		{
		case ERbCuePreset::Playing19oz: return rb::CuePreset::Playing19oz;
		case ERbCuePreset::Break21oz: return rb::CuePreset::Break21oz;
		case ERbCuePreset::Jump9oz: return rb::CuePreset::Jump9oz;
		case ERbCuePreset::House19oz: return rb::CuePreset::House19oz;
		}
		return rb::CuePreset::Playing19oz;
	}

	rb::rules::Discipline ToCore(ERbDiscipline Discipline)
	{
		switch (Discipline)
		{
		case ERbDiscipline::NineBall: return rb::rules::Discipline::NineBall;
		case ERbDiscipline::EightBall: return rb::rules::Discipline::EightBall;
		case ERbDiscipline::TenBall: return rb::rules::Discipline::TenBall;
		case ERbDiscipline::StraightPool: return rb::rules::Discipline::StraightPool;
		}
		return rb::rules::Discipline::NineBall;
	}

	rb::rules::RulesPreset RulesPresetFor(ERbDiscipline Discipline)
	{
		switch (Discipline)
		{
		case ERbDiscipline::NineBall: return rb::rules::RulesPreset::Wpa9Ball;
		case ERbDiscipline::EightBall: return rb::rules::RulesPreset::Wpa8Ball;
		case ERbDiscipline::TenBall: return rb::rules::RulesPreset::Wpa10Ball;
		case ERbDiscipline::StraightPool: return rb::rules::RulesPreset::Wpa14_1;
		}
		return rb::rules::RulesPreset::Wpa9Ball;
	}

	const TCHAR* ToString(ERbTablePart Part)
	{
		switch (Part)
		{
		case ERbTablePart::Bed: return TEXT("Bed");
		case ERbTablePart::CushionCloth: return TEXT("CushionCloth");
		case ERbTablePart::RailCaps: return TEXT("RailCaps");
		case ERbTablePart::Apron: return TEXT("Apron");
		case ERbTablePart::PocketLiners: return TEXT("PocketLiners");
		case ERbTablePart::Sights: return TEXT("Sights");
		case ERbTablePart::Legs: return TEXT("Legs");
		// M2-L block (append-only).
		case ERbTablePart::RubberStrip: return TEXT("RubberStrip");
		case ERbTablePart::PocketBuckets: return TEXT("PocketBuckets");
		case ERbTablePart::Castings: return TEXT("Castings");
		case ERbTablePart::Cabinet: return TEXT("Cabinet");
		case ERbTablePart::Trim: return TEXT("Trim");
		case ERbTablePart::Hardware: return TEXT("Hardware");
		case ERbTablePart::Window: return TEXT("Window");
		case ERbTablePart::Count: break;
		}
		return TEXT("Unknown");
	}

	rb::human::VenueKind ToCore(ERbVenueKind Kind)
	{
		switch (Kind)
		{
		case ERbVenueKind::DiveBar: return rb::human::VenueKind::DiveBar;
		case ERbVenueKind::PoolHall: return rb::human::VenueKind::PoolHall;
		case ERbVenueKind::Arena: return rb::human::VenueKind::Arena;
		}
		return rb::human::VenueKind::DiveBar;
	}

	const TCHAR* MapFor(ERbVenue Venue)
	{
		switch (Venue)
		{
		case ERbVenue::TestRoom: return RbAssetPaths::M1TestRoomMap;
		case ERbVenue::DiveBar: return RbAssetPaths::DiveBarMap;
		}
		return RbAssetPaths::M1TestRoomMap;
	}

	// --- M3 additions (Docs/ue-architecture.md 19.3) ---

	rb::human::AiProfileId ToCore(ERbAiProfile Profile)
	{
		switch (Profile)
		{
		case ERbAiProfile::Tourist: return rb::human::AiProfileId::Tourist;
		case ERbAiProfile::BarRegular: return rb::human::AiProfileId::BarRegular;
		case ERbAiProfile::LeaguePlayer: return rb::human::AiProfileId::LeaguePlayer;
		case ERbAiProfile::LocalHustler: return rb::human::AiProfileId::LocalHustler;
		case ERbAiProfile::RoadPlayer: return rb::human::AiProfileId::RoadPlayer;
		case ERbAiProfile::TouringPro: return rb::human::AiProfileId::TouringPro;
		case ERbAiProfile::Count: break;
		}
		return rb::human::AiProfileId::BarRegular;
	}

	ERbAiProfile FromCore(rb::human::AiProfileId Id)
	{
		switch (Id)
		{
		case rb::human::AiProfileId::Tourist: return ERbAiProfile::Tourist;
		case rb::human::AiProfileId::BarRegular: return ERbAiProfile::BarRegular;
		case rb::human::AiProfileId::LeaguePlayer: return ERbAiProfile::LeaguePlayer;
		case rb::human::AiProfileId::LocalHustler: return ERbAiProfile::LocalHustler;
		case rb::human::AiProfileId::RoadPlayer: return ERbAiProfile::RoadPlayer;
		case rb::human::AiProfileId::TouringPro: return ERbAiProfile::TouringPro;
		}
		return ERbAiProfile::BarRegular;
	}

	rb::human::BridgeType ToCore(ERbBridgeType Bridge)
	{
		switch (Bridge)
		{
		case ERbBridgeType::Closed: return rb::human::BridgeType::Closed;
		case ERbBridgeType::Open: return rb::human::BridgeType::Open;
		case ERbBridgeType::Rail: return rb::human::BridgeType::Rail;
		case ERbBridgeType::Elevated: return rb::human::BridgeType::Elevated;
		case ERbBridgeType::Mechanical: return rb::human::BridgeType::Mechanical;
		}
		return rb::human::BridgeType::Closed;
	}

	ERbBridgeType FromCore(rb::human::BridgeType Bridge)
	{
		switch (Bridge)
		{
		case rb::human::BridgeType::Closed: return ERbBridgeType::Closed;
		case rb::human::BridgeType::Open: return ERbBridgeType::Open;
		case rb::human::BridgeType::Rail: return ERbBridgeType::Rail;
		case rb::human::BridgeType::Elevated: return ERbBridgeType::Elevated;
		case rb::human::BridgeType::Mechanical: return ERbBridgeType::Mechanical;
		}
		return ERbBridgeType::Closed;
	}

	const TCHAR* ToString(ERbAiProfile Profile)
	{
		switch (Profile)
		{
		case ERbAiProfile::Tourist: return TEXT("Tourist");
		case ERbAiProfile::BarRegular: return TEXT("BarRegular");
		case ERbAiProfile::LeaguePlayer: return TEXT("LeaguePlayer");
		case ERbAiProfile::LocalHustler: return TEXT("LocalHustler");
		case ERbAiProfile::RoadPlayer: return TEXT("RoadPlayer");
		case ERbAiProfile::TouringPro: return TEXT("TouringPro");
		case ERbAiProfile::Count: break;
		}
		return TEXT("Unknown");
	}

	bool ParseAiProfile(const FString& Name, ERbAiProfile& Out)
	{
		for (int32 Index = 0; Index < static_cast<int32>(ERbAiProfile::Count); ++Index)
		{
			const ERbAiProfile Profile = static_cast<ERbAiProfile>(Index);
			if (Name.Equals(ToString(Profile), ESearchCase::IgnoreCase))
			{
				Out = Profile;
				return true;
			}
		}
		return false;
	}

	const TCHAR* ToString(ERbCallPolicy Policy)
	{
		switch (Policy)
		{
		case ERbCallPolicy::Casual: return TEXT("Casual");
		case ERbCallPolicy::EveryShot: return TEXT("EveryShot");
		}
		return TEXT("Unknown");
	}

	bool ParseCallPolicy(const FString& Name, ERbCallPolicy& Out)
	{
		for (const ERbCallPolicy Policy : {ERbCallPolicy::Casual, ERbCallPolicy::EveryShot})
		{
			if (Name.Equals(ToString(Policy), ESearchCase::IgnoreCase))
			{
				Out = Policy;
				return true;
			}
		}
		return false;
	}
}
