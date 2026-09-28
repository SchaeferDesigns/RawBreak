#include "Settings/RbGameUserSettings.h"

#include "Engine/Engine.h"

// Owner: UE-8. TODO(UE-8): ApplyQualityPreset (scalability groups + project cvars per plan 9.4 row), persistence test.

URbGameUserSettings* URbGameUserSettings::Get()
{
	return GEngine ? Cast<URbGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void URbGameUserSettings::ApplyQualityPreset(ERbQualityPreset Preset)
{
	QualityPreset = Preset; // TODO(UE-8)
}
