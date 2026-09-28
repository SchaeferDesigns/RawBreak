#include "Camera/RbCameraModel.h"

// Owner: UE-5b. TODO(UE-5b): Headcam / Broadcast values of plan 4.9 (V = 58.7, k1 0.12, k2 0.02, A 1.1 mm,
// shutter 180, AE 3.0 / 2.0, grain 0.05 / 0.35, head translation 1.0), test Defaults == plan table.

const FRbCameraPresetParams& URbCameraModel::Get(ERbCameraPreset Preset) const
{
	return Preset == ERbCameraPreset::Headcam ? Headcam : Eyes;
}

namespace RbCameraModel
{
	FRbCameraPresetParams Defaults(ERbCameraPreset /*Preset*/)
	{
		return FRbCameraPresetParams{}; // Eyes defaults; TODO(UE-5b)
	}
}
