#include "Settings/RbSettingsRegistry.h"

// Owner: M2-D.

const TArray<FRbSettingDef>& FRbSettingsRegistry::Rows()
{
	// TODO(M2-D): the rows of Docs/ue-architecture.md 18.4 (quality preset + individual quality rows, window mode, resolution,
	// frame cap, v-sync, camera look, FOV, comfort, DPI, aim / look / stroke sensitivity, fine-aim factor, acceleration, invert,
	// key hints, volumes).
	static const TArray<FRbSettingDef> Empty;
	return Empty;
}

const FRbSettingDef* FRbSettingsRegistry::Find(FName Id)
{
	return Rows().FindByPredicate([Id](const FRbSettingDef& Row) { return Row.Id == Id; });
}
