#include "Camera/RbCameraModel.h"

#include "Math/RbCameraMath.h"

// Owner: UE-5b. The compiled-in presets of ue5-realism-plan 4.9 (Eyes, Headcam) plus the replay Broadcast preset; tests
// RawBreak.Unit.Camera.Defaults_Plan49 (Private/Tests/RbCameraRigTests.cpp).

URbCameraModel::URbCameraModel()
{
	Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	Headcam = RbCameraModel::Defaults(ERbCameraPreset::Headcam);
	Broadcast = RbCameraModel::Defaults(ERbCameraPreset::Broadcast);
}

const FRbCameraPresetParams& URbCameraModel::Get(ERbCameraPreset Preset) const
{
	switch (Preset)
	{
	case ERbCameraPreset::Headcam: return Headcam;
	case ERbCameraPreset::Broadcast: return Broadcast;
	default: return Eyes;
	}
}

namespace RbCameraModel
{
	FRbCameraPresetParams Defaults(ERbCameraPreset Preset)
	{
		FRbCameraPresetParams P; // the struct defaults ARE the Eyes column of table 4.9
		switch (Preset)
		{
		case ERbCameraPreset::Eyes:
			break;

		case ERbCameraPreset::Headcam:
			// Base horizontal 90 deg at 16:9 authored as its vertical FOV (58.7 deg), so ultrawide stays Hor+ like the Eyes.
			P.VerticalFovDeg = RbCameraMath::VerticalFromHorizontalFovDeg(90.0, 16.0 / 9.0);
			P.DistortionK1 = 0.12;
			P.DistortionK2 = 0.02;
			P.ApertureDiameterMm = 1.1;           // f ~ 2.7 mm at f/2.5: nearly everything sharp
			P.FocusEaseSeconds = 0.3;             // contrast AF hunts a little slower than the eye
			P.ShutterAngleDeg = 180.0;
			P.ChromaticAberration = 0.4;
			P.BloomIntensity = 0.6;               // lens veiling glare
			P.bConvolutionBloom = false;          // standard bloom, slightly higher (plan 4.4)
			P.AdaptSpeedUp = 3.0;                 // camera AE is fast
			P.AdaptSpeedDown = 2.0;
			P.ExposureCompensation = -0.3;        // cameras protect highlights
			P.MeteringSigma = 0.25;               // stronger centre weight
			P.HistogramLowPercent = 35.0;
			P.HistogramHighPercent = 95.0;
			P.LocalExposureHighlightContrast = 1.0; // less tone compression = more "camera"
			P.LocalExposureShadowContrast = 1.0;
			P.GrainG0 = 0.05;
			P.GrainMax = 0.35;
			P.Vignette = 0.3;
			P.HeadTranslationScale = 1.0;
			P.bStabiliseGaze = false;             // a camera has no vestibulo-ocular reflex
			P.MountJitterDeg = 0.1;
			P.CueAxisSmoothingSeconds = 0.05;
			break;

		case ERbCameraPreset::Broadcast:
			// TV camera on a tripod over / around the table (replays): long lens, deep but not infinite focus, clean sensor.
			P.VerticalFovDeg = RbCameraMath::VerticalFromHorizontalFovDeg(35.0, 16.0 / 9.0);
			P.ApertureDiameterMm = 8.0;
			P.FocusEaseSeconds = 0.35;
			P.ShutterAngleDeg = 180.0;
			P.BloomIntensity = 0.4;
			P.bConvolutionBloom = false;
			P.AdaptSpeedUp = 2.0;
			P.AdaptSpeedDown = 1.5;
			P.ExposureCompensation = 0.0;
			P.MeteringSigma = 0.4;
			P.HistogramLowPercent = 40.0;
			P.LocalExposureHighlightContrast = 0.9;
			P.LocalExposureShadowContrast = 1.0;
			P.GrainG0 = 0.008;
			P.GrainMax = 0.03;
			P.Vignette = 0.05;
			P.HeadTranslationScale = 0.0;         // tripod / crane
			P.bStabiliseGaze = false;
			P.MountJitterDeg = 0.0;
			break;
		}
		return P;
	}

	double Overscan(const FRbCameraPresetParams& Params, double AspectWidthOverHeight)
	{
		const double BaseHorizontal = RbCameraMath::HorizontalFromVerticalFovDeg(Params.VerticalFovDeg, AspectWidthOverHeight);
		return RbCameraMath::DistortionOverscan(BaseHorizontal, AspectWidthOverHeight, Params.DistortionK1, Params.DistortionK2).Overscan;
	}
}
